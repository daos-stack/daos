/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * object server operations
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related to object.
 */

#include <uuid/uuid.h>

#include <abt.h>
#include <daos/transport.h>
#include <daos_srv/pool.h>
#include <daos_srv/container.h>
#include <daos_srv/vos.h>
#include <daos_srv/daos_server.h>
#include "obj_rpc.h"
#include "obj_internal.h"

/**
 * Free a single entry sgl/iov for server side use.
 **/
static void
ds_sgls_free(daos_sg_list_t *sgls, int nr)
{
	int i;
	int j;

	if (sgls == NULL)
		return;

	for (i = 0; i < nr; i++) {
		if (sgls[i].sg_nr.num == 0 || sgls[i].sg_iovs == NULL)
			continue;

		for (j = 0; j < sgls[i].sg_nr.num; j++) {
			if (sgls[i].sg_iovs[j].iov_buf == NULL)
				continue;

			D_FREE(sgls[i].sg_iovs[j].iov_buf,
			       sgls[i].sg_iovs[j].iov_buf_len);
		}

		D_FREE(sgls[i].sg_iovs,
		       sgls[i].sg_nr.num * sizeof(sgls[i].sg_iovs[0]));
	}
}

/**
 * After bulk finish, let's send reply, then release the resource.
 */
static void
ds_obj_rw_complete(dtp_rpc_t *rpc, daos_handle_t ioh, int status,
		   uint32_t map_version)
{
	struct obj_rw_in	*orwi;
	uint64_t		cid = 0;
	int rc;

	obj_reply_set_status(rpc, status);
	obj_reply_map_version_set(rpc, map_version);
	rc = dtp_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: %d\n", rc);

	if (opc_get(rpc->dr_opc) == DAOS_OBJ_RPC_FETCH) {
		struct obj_rw_out *orwo;

		orwo = dtp_reply_get(rpc);
		D_ASSERT(orwo != NULL);

		if (orwo->orw_sgls.da_arrays != NULL) {
			ds_sgls_free(orwo->orw_sgls.da_arrays,
				     orwo->orw_sgls.da_count);
			D_FREE(orwo->orw_sgls.da_arrays,
			      orwo->orw_sgls.da_count * sizeof(daos_sg_list_t));
			orwo->orw_sgls.da_count = 0;
		}
		if (orwo->orw_sizes.da_arrays != NULL) {
			D_FREE(orwo->orw_sizes.da_arrays,
			       orwo->orw_sizes.da_count * sizeof(uint64_t));
			orwo->orw_sizes.da_count = 0;
		}
	}

	if (daos_handle_is_inval(ioh))
		return;

	orwi = dtp_req_get(rpc);
	D_ASSERT(orwi != NULL);
	if (opc_get(rpc->dr_opc) == DAOS_OBJ_RPC_UPDATE)
		rc = vos_obj_zc_update_end(ioh, cid, &orwi->orw_dkey,
					   orwi->orw_nr,
					   orwi->orw_iods.da_arrays,
					   0);
	else if (opc_get(rpc->dr_opc) == DAOS_OBJ_RPC_FETCH)
		rc = vos_obj_zc_fetch_end(ioh, &orwi->orw_dkey,
					  orwi->orw_nr,
					  orwi->orw_iods.da_arrays,
					  0);
	if (rc != 0)
		D_ERROR(DF_UOID "%x send reply failed: %d\n",
			DP_UOID(orwi->orw_oid),
			opc_get(rpc->dr_opc), rc);
}

struct ds_bulk_async_args {
	ABT_future	future;
	int		result;
};

static int
bulk_complete_cb(const struct dtp_bulk_cb_info *cb_info)
{
	struct ds_bulk_async_args	*arg;
	struct dtp_bulk_desc		*bulk_desc;
	dtp_rpc_t			*rpc;
	dtp_bulk_t			local_bulk_hdl;
	int				rc = 0;

	rc = cb_info->bci_rc;
	if (rc != 0)
		D_ERROR("bulk transfer failed: rc = %d\n", rc);

	bulk_desc = cb_info->bci_bulk_desc;
	local_bulk_hdl = bulk_desc->bd_local_hdl;
	rpc = bulk_desc->bd_rpc;
	arg = (struct ds_bulk_async_args *)cb_info->bci_arg;
	/**
	 * Note: only one thread will access arg.result, so
	 * it should be safe here.
	 **/
	if (arg->result == 0)
		arg->result = rc;
	ABT_future_set(arg->future, &rc);

	dtp_bulk_free(local_bulk_hdl);
	dtp_req_decref(rpc);
	return rc;
}

static int
ds_bulk_transfer(dtp_rpc_t *rpc, dtp_bulk_t *remote_bulks,
		 daos_sg_list_t **sgls, daos_handle_t ioh,
		 int nr, dtp_bulk_op_t bulk_op)
{
	dtp_bulk_opid_t		bulk_opid;
	dtp_bulk_perm_t		bulk_perm;
	ABT_future		future;
	struct ds_bulk_async_args arg;
	int			i;
	int			rc;

	bulk_perm = bulk_op == DTP_BULK_PUT ? DTP_BULK_RO : DTP_BULK_RW;
	rc = ABT_future_create(nr, NULL, &future);
	if (rc != 0)
		return dss_abterr2der(rc);

	memset(&arg, 0, sizeof(arg));
	arg.future = future;
	for (i = 0; i < nr; i++) {
		struct dtp_bulk_desc	bulk_desc;
		dtp_bulk_t		local_bulk_hdl;
		int			ret = 0;

		if (remote_bulks[i] == NULL) {
			ABT_future_set(future, &ret);
			continue;
		}

		ret = dtp_bulk_create(rpc->dr_ctx, sgls[i], bulk_perm,
				     &local_bulk_hdl);
		if (ret != 0) {
			D_ERROR("dtp_bulk_create i %d failed, rc: %d.\n",
				i, ret);
			/**
			 * Sigh, future can not be abort now, let's
			 * continue until of all of future compartments
			 * have been set.
			 **/
			ABT_future_set(future, &ret);
			if (rc == 0)
				rc = ret;
		}

		dtp_req_addref(rpc);

		bulk_desc.bd_rpc = rpc;
		bulk_desc.bd_bulk_op = bulk_op;
		bulk_desc.bd_remote_hdl = remote_bulks[i];
		bulk_desc.bd_local_hdl = local_bulk_hdl;
		bulk_desc.bd_len = sgls[i]->sg_iovs->iov_len;
		bulk_desc.bd_remote_off = 0;
		bulk_desc.bd_local_off = 0;

		ret = dtp_bulk_transfer(&bulk_desc, bulk_complete_cb,
					&arg, &bulk_opid);
		if (ret < 0) {
			D_ERROR("dtp_bulk_transfer failed, rc: %d.\n", ret);
			dtp_bulk_free(local_bulk_hdl);
			dtp_req_decref(rpc);
			ABT_future_set(future, &ret);
			if (rc == 0)
				rc = ret;
		}
	}

	ABT_future_wait(future);
	if (rc == 0)
		rc = arg.result;

	ABT_future_free(&future);
	return rc;
}

static int
ds_sgls_prep(daos_sg_list_t *dst_sgls, daos_sg_list_t *sgls, int number)
{
	int i;
	int j;
	int rc = 0;

	for (i = 0; i < number; i++) {
		dst_sgls[i].sg_nr.num = sgls[i].sg_nr.num;
		D_ALLOC(dst_sgls[i].sg_iovs,
			sgls[i].sg_nr.num * sizeof(*sgls[i].sg_iovs));
		if (dst_sgls[i].sg_iovs == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		for (j = 0; j < dst_sgls[i].sg_nr.num; j++) {
			dst_sgls[i].sg_iovs[j].iov_buf_len =
				sgls[i].sg_iovs[j].iov_buf_len;

			D_ALLOC(dst_sgls[i].sg_iovs[j].iov_buf,
				dst_sgls[i].sg_iovs[j].iov_buf_len);
			if (dst_sgls[i].sg_iovs[j].iov_buf == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}
	}
out:
	return rc;
}

static int
ds_obj_update_sizes_in_reply(dtp_rpc_t *rpc)
{
	struct obj_rw_in	*orw = dtp_req_get(rpc);
	struct obj_rw_out	*orwo = dtp_reply_get(rpc);
	daos_vec_iod_t		*vecs;
	uint64_t		*sizes;
	int			size_count = 0;
	int			idx = 0;
	int			i;
	int			j;

	D_ASSERT(opc_get(rpc->dr_opc) == DAOS_OBJ_RPC_FETCH);

	D_ASSERT(orwo != NULL);
	D_ASSERT(orw != NULL);

	vecs = orw->orw_iods.da_arrays;
	for (i = 0; i < orw->orw_iods.da_count; i++)
		size_count += vecs[i].vd_nr;

	orwo->orw_sizes.da_count = size_count;
	D_ALLOC(orwo->orw_sizes.da_arrays, size_count * sizeof(uint64_t));
	if (orwo->orw_sizes.da_arrays == NULL)
		return -DER_NOMEM;

	sizes = orwo->orw_sizes.da_arrays;
	for (i = 0; i < orw->orw_iods.da_count; i++) {
		for (j = 0; j < vecs[i].vd_nr; j++) {
			sizes[idx] = vecs[i].vd_recxs[j].rx_rsize;
			idx++;
		}
	}

	return 0;
}

static int
ds_obj_rw_inline(dtp_rpc_t *rpc, struct ds_cont_hdl *cont_hdl)
{
	struct obj_rw_in	*orw = dtp_req_get(rpc);
	daos_sg_list_t		*sgls = orw->orw_sgls.da_arrays;
	int			rc;

	if (opc_get(rpc->dr_opc) == DAOS_OBJ_RPC_UPDATE) {
		uint64_t cookie = 0;

		rc = vos_obj_update(cont_hdl->sch_cont->sc_hdl,
				    orw->orw_oid, orw->orw_epoch,
				    cookie, &orw->orw_dkey, orw->orw_nr,
				    orw->orw_iods.da_arrays, sgls);
	} else {
		struct obj_rw_out *orwo;

		orwo = dtp_reply_get(rpc);
		orwo->orw_sgls.da_count = orw->orw_sgls.da_count;
		if (orwo->orw_sgls.da_arrays == NULL)
			D_ALLOC(orwo->orw_sgls.da_arrays,
				orwo->orw_sgls.da_count * sizeof(*sgls));

		rc = ds_sgls_prep(orwo->orw_sgls.da_arrays, sgls,
				  orw->orw_sgls.da_count);
		if (rc < 0)
			D_GOTO(out_sgl, rc);

		rc = vos_obj_fetch(cont_hdl->sch_cont->sc_hdl,
				   orw->orw_oid, orw->orw_epoch,
				   &orw->orw_dkey, orw->orw_nr,
				   orw->orw_iods.da_arrays,
				   orwo->orw_sgls.da_arrays);
		if (rc != 0)
			D_GOTO(out_sgl, rc);

		rc = ds_obj_update_sizes_in_reply(rpc);
		if (rc != 0)
			D_GOTO(out_sgl, rc);

out_sgl:
		if (rc != 0) {
			ds_sgls_free(orwo->orw_sgls.da_arrays,
				     orwo->orw_sgls.da_count);
			D_FREE(orwo->orw_sgls.da_arrays,
			       orwo->orw_sgls.da_count * sizeof(*sgls));
			orwo->orw_sgls.da_arrays = NULL;
			orwo->orw_sgls.da_count = 0;
		}
	}

	D_DEBUG(DF_MISC, "obj"DF_OID" rw inline rc = %d\n",
		DP_OID(orw->orw_oid.id_pub), rc);

	return rc;
}

int
ds_obj_rw_handler(dtp_rpc_t *rpc)
{
	struct obj_rw_in	*orw;
	struct ds_cont_hdl	*cont_hdl;
	daos_handle_t		ioh = DAOS_HDL_INVAL;
	daos_sg_list_t		**sgls = NULL;
	dtp_bulk_op_t		bulk_op;
	uint32_t		map_version = 0;
	int			i;
	int			rc;

	orw = dtp_req_get(rpc);
	D_ASSERT(orw != NULL);

	cont_hdl = ds_cont_hdl_lookup(orw->orw_co_hdl);
	if (cont_hdl == NULL)
		D_GOTO(out, rc = -DER_NO_PERM);

	D_ASSERT(cont_hdl->sch_pool != NULL);
	map_version = cont_hdl->sch_pool->spc_map_version;
	if (orw->orw_map_ver < map_version)
		D_GOTO(out_tch, rc = -DER_STALE);

	/* Inline update/fetch */
	if (orw->orw_sgls.da_arrays != NULL && orw->orw_sgls.da_count > 0) {
		rc = ds_obj_rw_inline(rpc, cont_hdl);
		D_GOTO(out_tch, rc);
	}

	/* bulk update/fetch */
	if (opc_get(rpc->dr_opc) == DAOS_OBJ_RPC_UPDATE) {
		rc = vos_obj_zc_update_begin(cont_hdl->sch_cont->sc_hdl,
					     orw->orw_oid, orw->orw_epoch,
					     &orw->orw_dkey, orw->orw_nr,
					     orw->orw_iods.da_arrays, &ioh);
		if (rc != 0) {
			D_ERROR(DF_UOID"preparing update fails: %d\n",
				DP_UOID(orw->orw_oid), rc);
			D_GOTO(out_tch, rc);
		}

		bulk_op = DTP_BULK_GET;
	} else {
		rc = vos_obj_zc_fetch_begin(cont_hdl->sch_cont->sc_hdl,
					    orw->orw_oid, orw->orw_epoch,
					    &orw->orw_dkey, orw->orw_nr,
					    orw->orw_iods.da_arrays, &ioh);
		if (rc != 0) {
			D_ERROR(DF_UOID"preparing fetch fails: %d\n",
				DP_UOID(orw->orw_oid), rc);
			D_GOTO(out_tch, rc);
		}

		bulk_op = DTP_BULK_PUT;

		rc = ds_obj_update_sizes_in_reply(rpc);
		if (rc != 0)
			D_GOTO(out_tch, rc);
	}

	D_ALLOC(sgls, orw->orw_nr * sizeof(*sgls));
	if (sgls == NULL)
		D_GOTO(out_tch, rc);

	for (i = 0; i < orw->orw_nr; i++)
		vos_obj_zc_vec2sgl(ioh, i, &sgls[i]);

	rc = ds_bulk_transfer(rpc, orw->orw_bulks.da_arrays,
			      sgls, ioh, orw->orw_nr, bulk_op);
out_tch:
	ds_cont_hdl_put(cont_hdl);
out:
	ds_obj_rw_complete(rpc, ioh, rc, map_version);

	return rc;
}

static void
ds_eu_complete(dtp_rpc_t *rpc, daos_sg_list_t *sgl, int status,
	       uint32_t map_version)
{
	struct obj_key_enum_out *oeo;
	int rc;

	obj_reply_set_status(rpc, status);
	obj_reply_map_version_set(rpc, map_version);
	rc = dtp_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: %d\n", rc);

	oeo = dtp_reply_get(rpc);
	D_ASSERT(oeo != NULL);
	D_FREE(oeo->oeo_kds.da_arrays,
	       oeo->oeo_kds.da_count * sizeof(daos_key_desc_t));

	ds_sgls_free(sgl, 1);
	oeo->oeo_sgl.sg_iovs = NULL;
}

int
ds_obj_enum_handler(dtp_rpc_t *rpc)
{
	struct obj_key_enum_in		*oei;
	struct obj_key_enum_out		*oeo;
	struct ds_cont_hdl		*cont_hdl;
	daos_iov_t			*iovs = NULL;
	int				iovs_nr;
	int				iovs_idx;
	vos_iter_param_t		param;
	daos_key_desc_t			*kds;
	daos_handle_t			ih;
	daos_sg_list_t			sgl;
	uint32_t			map_version = 0;
	int				rc = 0;
	int				dkey_nr = 0;
	int				type;

	if (opc_get(rpc->dr_opc) == DAOS_OBJ_AKEY_RPC_ENUMERATE)
		type = VOS_ITER_AKEY;
	else
		type = VOS_ITER_DKEY;
	oei = dtp_req_get(rpc);
	D_ASSERT(oei != NULL);

	cont_hdl = ds_cont_hdl_lookup(oei->oei_co_hdl);
	if (cont_hdl == NULL)
		D_GOTO(out, rc = -DER_NO_PERM);

	D_ASSERT(cont_hdl->sch_pool != NULL);
	map_version = cont_hdl->sch_pool->spc_map_version;
	if (oei->oei_map_ver < map_version)
		D_GOTO(out_tch, rc = -DER_STALE);

	oeo = dtp_reply_get(rpc);
	D_ASSERT(oeo != NULL);

	/* prepare buffer for enumerate */
	rc = ds_sgls_prep(&sgl, &oei->oei_sgl, 1);
	if (rc != 0)
		D_GOTO(out_tch, rc);

	iovs = sgl.sg_iovs;
	iovs_nr = sgl.sg_nr.num;

	memset(&param, 0, sizeof(param));
	param.ip_hdl	= cont_hdl->sch_cont->sc_hdl;
	param.ip_oid	= oei->oei_oid;
	param.ip_epr.epr_lo = oei->oei_epoch;
	if (type == VOS_ITER_AKEY) {
		if (oei->oei_key.iov_len == 0)
			D_GOTO(out_tch, rc = -DER_PROTO);
		param.ip_dkey = oei->oei_key;
	} else {
		if (oei->oei_key.iov_len > 0)
			param.ip_akey = oei->oei_key;
	}

	rc = vos_iter_prepare(type, &param, &ih);
	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			daos_hash_set_eof(&oeo->oeo_anchor);
			oeo->oeo_kds.da_count = 0;
			rc = 0;
		} else {
			D_ERROR("Failed to prepare d-key iterator: %d\n", rc);
		}
		D_GOTO(out_tch, rc);
	}

	rc = vos_iter_probe(ih, &oei->oei_anchor);
	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			daos_hash_set_eof(&oeo->oeo_anchor);
			oeo->oeo_kds.da_count = 0;
			rc = 0;
		}
		vos_iter_finish(ih);
		D_GOTO(out_tch, rc);
	}

	/* Prepare key desciptor buffer */
	oeo->oeo_kds.da_count = oei->oei_nr;
	D_ALLOC(oeo->oeo_kds.da_arrays,
		oei->oei_nr * sizeof(daos_key_desc_t));
	if (oeo->oeo_kds.da_arrays == NULL)
		D_GOTO(out_tch, rc = -DER_NOMEM);

	iovs_idx = 0;
	dkey_nr = 0;
	kds = oeo->oeo_kds.da_arrays;
	while (1) {
		vos_iter_entry_t  dkey_ent;

		rc = vos_iter_fetch(ih, &dkey_ent, &oeo->oeo_anchor);
		if (rc != 0)
			D_GOTO(next, rc);

		D_DEBUG(DF_MISC, "get key %s len "DF_U64
			"iov_len "DF_U64" buflen "DF_U64"\n",
			(char *)dkey_ent.ie_key.iov_buf,
			dkey_ent.ie_key.iov_len,
			iovs[iovs_idx].iov_len, iovs[iovs_idx].iov_buf_len);

		/* fill the key to iov if there are enough space */
		while (iovs_idx < iovs_nr) {
			if (iovs[iovs_idx].iov_len + dkey_ent.ie_key.iov_len <
			    iovs[iovs_idx].iov_buf_len) {
				/* Fill key descriptor */
				/* FIXME no checksum now */
				kds[dkey_nr].kd_key_len =
						dkey_ent.ie_key.iov_len;
				kds[dkey_nr].kd_csum_len = 0;
				dkey_nr++;

				memcpy(iovs[iovs_idx].iov_buf +
				       iovs[iovs_idx].iov_len,
				       dkey_ent.ie_key.iov_buf,
				       dkey_ent.ie_key.iov_len);
				iovs[iovs_idx].iov_len +=
						dkey_ent.ie_key.iov_len;

				rc = vos_iter_next(ih);
				break;
			}
			iovs_idx++;
		}
next:
		if (dkey_nr >= oei->oei_nr || iovs_idx >= iovs_nr || rc != 0) {
			/* it means iteration hit the end */
			if (rc == -DER_NONEXIST) {
				daos_hash_set_eof(&oeo->oeo_anchor);
				rc = 0;
			} else if (rc == 0) {
				rc = vos_iter_fetch(ih, &dkey_ent,
						    &oeo->oeo_anchor);
			}
			break;
		}
	}
	vos_iter_finish(ih);
	if (rc < 0) {
		D_ERROR("Failed to fetch dkey: %d\n", rc);
		D_GOTO(out_tch, rc);
	}

	oeo->oeo_kds.da_count = dkey_nr;
	if (oei->oei_bulk != NULL) {
		daos_sg_list_t *sgls = &sgl;

		rc = ds_bulk_transfer(rpc, &oei->oei_bulk, &sgls,
				      DAOS_HDL_INVAL, 1, DTP_BULK_PUT);
		if (rc != 0)
			D_GOTO(out_tch, rc);

		oeo->oeo_sgl.sg_iovs = NULL;
		oeo->oeo_sgl.sg_nr.num = 0;
		oeo->oeo_sgl.sg_nr.num_out = 0;
	} else {
		oeo->oeo_sgl = sgl;
		oeo->oeo_sgl.sg_nr.num = iovs_idx;
	}
out_tch:
	ds_cont_hdl_put(cont_hdl);
out:
	ds_eu_complete(rpc, &sgl, rc, map_version);
	return rc;
}
