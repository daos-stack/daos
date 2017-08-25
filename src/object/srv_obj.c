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
#define DD_SUBSYS	DD_FAC(object)

#include <uuid/uuid.h>

#include <abt.h>
#include <daos/rpc.h>
#include <daos_srv/rebuild.h>
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
ds_obj_rw_complete(crt_rpc_t *rpc, daos_handle_t ioh, int status,
		   uint32_t map_version, uuid_t cookie)
{
	int	rc;

	if (!daos_handle_is_inval(ioh)) {
		struct obj_rw_in *orwi;

		orwi = crt_req_get(rpc);
		D_ASSERT(orwi != NULL);

		if (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_UPDATE) {
			rc = vos_obj_zc_update_end(ioh, cookie, map_version,
						   &orwi->orw_dkey,
						   orwi->orw_nr,
						   orwi->orw_iods.da_arrays,
						   status);

		} else {
			D_ASSERT(opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_FETCH);
			rc = vos_obj_zc_fetch_end(ioh, &orwi->orw_dkey,
						  orwi->orw_nr,
						  orwi->orw_iods.da_arrays,
						  status);
		}

		if (rc != 0) {
			D_ERROR(DF_UOID "%x ZC end failed: %d\n",
				DP_UOID(orwi->orw_oid), opc_get(rpc->cr_opc),
				rc);
			if (status == 0)
				status = rc;
		}
	}

	obj_reply_set_status(rpc, status);
	obj_reply_map_version_set(rpc, map_version);

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: %d\n", rc);

	if (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_FETCH) {
		struct obj_rw_out *orwo;

		orwo = crt_reply_get(rpc);
		D_ASSERT(orwo != NULL);

		if (orwo->orw_sizes.da_arrays != NULL) {
			D_FREE(orwo->orw_sizes.da_arrays,
			       orwo->orw_sizes.da_count * sizeof(uint64_t));
			orwo->orw_sizes.da_count = 0;
		}

		if (orwo->orw_nrs.da_arrays != NULL) {
			D_FREE(orwo->orw_nrs.da_arrays,
			       orwo->orw_nrs.da_count * sizeof(uint32_t));
			orwo->orw_nrs.da_count = 0;
		}
	}
}

struct ds_bulk_async_args {
	ABT_future	future;
	int		result;
};

static int
bulk_complete_cb(const struct crt_bulk_cb_info *cb_info)
{
	struct ds_bulk_async_args	*arg;
	struct crt_bulk_desc		*bulk_desc;
	crt_rpc_t			*rpc;
	crt_bulk_t			local_bulk_hdl;
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

	crt_bulk_free(local_bulk_hdl);
	crt_req_decref(rpc);
	return rc;
}

static int
ds_bulk_transfer(crt_rpc_t *rpc, crt_bulk_op_t bulk_op,
		 crt_bulk_t *remote_bulks, daos_handle_t ioh,
		 daos_sg_list_t **sgls, int sgl_nr)
{
	crt_bulk_opid_t		bulk_opid;
	crt_bulk_perm_t		bulk_perm;
	ABT_future		future;
	struct ds_bulk_async_args arg;
	int			i;
	int			rc;

	bulk_perm = bulk_op == CRT_BULK_PUT ? CRT_BULK_RO : CRT_BULK_RW;
	rc = ABT_future_create(sgl_nr, NULL, &future);
	if (rc != 0)
		return dss_abterr2der(rc);

	memset(&arg, 0, sizeof(arg));
	arg.future = future;
	for (i = 0; i < sgl_nr; i++) {
		daos_sg_list_t		*sgl;
		struct crt_bulk_desc	 bulk_desc;
		crt_bulk_t		 local_bulk_hdl;
		int			 ret = 0;
		daos_size_t		 offset = 0;
		unsigned int		 idx = 0;
		bool			 did_something = false;

		if (remote_bulks[i] == NULL) {
			ABT_future_set(future, &ret);
			continue;
		}

		if (sgls != NULL) {
			sgl = sgls[i];
		} else {
			D_ASSERT(!daos_handle_is_inval(ioh));
			ret = vos_obj_zc_sgl_at(ioh, i, &sgl);
			if (ret)
				ABT_future_set(future, &ret);
			D_ASSERT(sgl != NULL);
		}

		/**
		 * Let's walk through the sgl to check if the iov is empty,
		 * which is usually gotten from punched/empty records (see
		 * vos_recx_fetch()), and skip these empty iov during bulk
		 * transfer to avoid touching the input buffer.
		 *
		 * XXX: initial value of future can't match with bulk transfers
		 * if there are holes.
		 */
		while (idx < sgl->sg_nr.num_out) {
			daos_sg_list_t	sgl_sent;
			daos_size_t	length = 0;
			unsigned int	start;

			/**
			 * Skip the punched/empty record, let's also skip the
			 * them record in the input buffer instead of memset
			 * it to 0.
			 */
			while (sgl->sg_iovs[idx].iov_buf == NULL &&
			       idx < sgl->sg_nr.num_out) {
				offset += sgl->sg_iovs[idx].iov_len;
				idx++;
			}

			if (idx == sgl->sg_nr.num_out)
				break;

			did_something = true;
			start = idx;
			sgl_sent.sg_iovs = &sgl->sg_iovs[start];
			/* Find the end of the non-empty record */
			while (sgl->sg_iovs[idx].iov_buf != NULL &&
			       idx < sgl->sg_nr.num_out) {
				length += sgl->sg_iovs[idx].iov_len;
				idx++;
			}

			sgl_sent.sg_nr.num = idx - start;
			sgl_sent.sg_nr.num_out = idx - start;

			ret = crt_bulk_create(rpc->cr_ctx,
					      daos2crt_sg(&sgl_sent),
					      bulk_perm, &local_bulk_hdl);
			if (ret != 0) {
				D_ERROR("crt_bulk_create i %d failed, rc: %d\n",
					i, ret);
				/**
				 * Sigh, future can not be abort now, let's
				 * continue until of all of future compartments
				 * have been set.
				 */
				ABT_future_set(future, &ret);
				if (rc == 0)
					rc = ret;
			}

			crt_req_addref(rpc);

			bulk_desc.bd_rpc	= rpc;
			bulk_desc.bd_bulk_op	= bulk_op;
			bulk_desc.bd_remote_hdl	= remote_bulks[i];
			bulk_desc.bd_local_hdl	= local_bulk_hdl;
			bulk_desc.bd_len	= length;
			bulk_desc.bd_remote_off	= offset;
			bulk_desc.bd_local_off	= 0;

			ret = crt_bulk_transfer(&bulk_desc, bulk_complete_cb,
						&arg, &bulk_opid);
			if (ret < 0) {
				D_ERROR("crt_bulk_transfer failed, rc: %d.\n",
					ret);
				crt_bulk_free(local_bulk_hdl);
				crt_req_decref(rpc);
				ABT_future_set(future, &ret);
				if (rc == 0)
					rc = ret;
			}
			offset += length;
		}
		if (!did_something)
			ABT_future_set(future, &ret);
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
ds_obj_update_sizes_in_reply(crt_rpc_t *rpc)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	daos_iod_t		*iods;
	uint64_t		*sizes;
	int			size_count;
	int			i;

	D_ASSERT(opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_FETCH);

	D_ASSERT(orwo != NULL);
	D_ASSERT(orw != NULL);

	iods = orw->orw_iods.da_arrays;
	size_count = orw->orw_iods.da_count;

	orwo->orw_sizes.da_count = size_count;
	D_ALLOC(sizes, size_count * sizeof(*sizes));
	if (sizes == NULL)
		return -DER_NOMEM;

	for (i = 0; i < orw->orw_iods.da_count; i++)
		sizes[i] = iods[i].iod_size;

	orwo->orw_sizes.da_arrays = sizes;
	return 0;
}

/**
 * Pack nrs in sgls inside the reply, so the client can update
 * sgls before it returns to application. Note: this is only
 * needed for bulk transfer, for inline transfer, it will pack
 * the complete sgls inside the req/reply, see ds_obj_rw_inline()
 * and obj_shard_rw().
 */
static int
ds_obj_update_nrs_in_reply(crt_rpc_t *rpc, daos_handle_t ioh)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	uint32_t		*nrs;
	uint32_t		nrs_count = orw->orw_nr;
	int			i;
	int			rc = 0;

	if (nrs_count == 0)
		return 0;

	D_ASSERT(!daos_handle_is_inval(ioh));
	/* return num_out for sgl */
	orwo->orw_nrs.da_count = nrs_count;
	D_ALLOC(orwo->orw_nrs.da_arrays,
		nrs_count * sizeof(uint32_t));

	if (orwo->orw_nrs.da_arrays == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	nrs = orwo->orw_nrs.da_arrays;
	for (i = 0; i < nrs_count; i++) {
		daos_sg_list_t	*sgl;

		rc = vos_obj_zc_sgl_at(ioh, i, &sgl);
		if (rc)
			D_GOTO(out, rc);
		D_ASSERT(sgl != NULL);
		nrs[i] = sgl->sg_nr.num_out;
	}
out:
	return rc;
}

static int
ds_obj_rw_inline(crt_rpc_t *rpc, struct ds_cont *cont, uuid_t cookie,
		 uint32_t pm_ver)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	daos_sg_list_t		*sgls = orw->orw_sgls.da_arrays;
	int			rc;

	if (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_UPDATE) {
		rc = vos_obj_update(cont->sc_hdl, orw->orw_oid, orw->orw_epoch,
				    cookie, pm_ver, &orw->orw_dkey, orw->orw_nr,
				    orw->orw_iods.da_arrays, sgls);
	} else {
		struct obj_rw_out *orwo;

		orwo = crt_reply_get(rpc);
		rc = vos_obj_fetch(cont->sc_hdl, orw->orw_oid, orw->orw_epoch,
				   &orw->orw_dkey, orw->orw_nr,
				   orw->orw_iods.da_arrays, sgls);
		if (rc != 0)
			D_GOTO(out, rc);

		orwo->orw_sgls.da_arrays = sgls;
		orwo->orw_sgls.da_count = orw->orw_sgls.da_count;
		rc = ds_obj_update_sizes_in_reply(rpc);
		if (rc != 0)
			D_GOTO(out, rc);
	}
out:
	D_DEBUG(DB_IO, "obj"DF_OID" rw inline rc = %d\n",
		DP_OID(orw->orw_oid.id_pub), rc);

	return rc;
}

/**
 * Lookup and return the container handle, if it is a rebuild handle, which
 * will never associate a particular container, then the contaier structure
 * will be returned to \a contp.
 */
static int
ds_check_container(uuid_t cont_hdl_uuid, uuid_t cont_uuid,
		   struct ds_cont_hdl **hdlp, struct ds_cont **contp)
{
	struct ds_cont_hdl	*cont_hdl;
	int			 rc;

	cont_hdl = ds_cont_hdl_lookup(cont_hdl_uuid);
	if (cont_hdl == NULL)
		D_GOTO(failed, rc = -DER_NO_HDL);

	if (cont_hdl->sch_cont != NULL) { /* a regular container */
		*contp = cont_hdl->sch_cont;
		D_GOTO(out, rc = 0);
	}

	if (!is_rebuild_container(cont_hdl_uuid)) {
		D_ERROR("Empty container "DF_UUID" (ref=%d) handle?\n",
			DP_UUID(cont_uuid), cont_hdl->sch_ref);
		ds_cont_hdl_put(cont_hdl);
		D_GOTO(failed, rc = -DER_NO_HDL);
	}
	/* rebuild handle is a dummy and never attached by a real container */

	D_DEBUG(DB_TRACE, DF_UUID"/%p is rebuild cont hdl\n",
		DP_UUID(cont_hdl_uuid), cont_hdl);

	/* load or create VOS container on demand */
	rc = ds_cont_lookup(cont_hdl->sch_pool->spc_uuid, cont_uuid, contp);
	if (rc) {
		ds_cont_hdl_put(cont_hdl);
		D_GOTO(failed, rc);
	}
out:
	*hdlp = cont_hdl;
failed:
	return rc;
}

void
ds_obj_rw_handler(crt_rpc_t *rpc)
{
	struct obj_rw_in	*orw;
	struct ds_cont_hdl	*cont_hdl = NULL;
	struct ds_cont		*cont = NULL;
	daos_handle_t		ioh = DAOS_HDL_INVAL;
	crt_bulk_op_t		bulk_op;
	uint32_t		map_version = 0;
	int			rc;

	orw = crt_req_get(rpc);
	D_ASSERT(orw != NULL);

	rc = ds_check_container(orw->orw_co_hdl, orw->orw_co_uuid,
				&cont_hdl, &cont);
	if (rc)
		D_GOTO(out, rc);

	if (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_UPDATE &&
	    !(cont_hdl->sch_capas & DAOS_COO_RW))
		D_GOTO(out, rc = -DER_NO_PERM);

	D_ASSERT(cont_hdl->sch_pool != NULL);
	map_version = cont_hdl->sch_pool->spc_map_version;
	if (orw->orw_map_ver < map_version) {
		/* XXX Let's only output a warnning for update, because
		 * return -DESTALE might delay write, which might
		 * cause rebuild missing some data.
		 * This is just the temporary solution XXX.(DAOS-308).
		 */
		D_WARN("stale version req %d map_version %d\n",
			orw->orw_map_ver, map_version);

		if (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_FETCH)
			D_GOTO(out, rc = -DER_STALE);
	}

	D_DEBUG(DB_TRACE, "opc %d "DF_UOID" tag %d\n", opc_get(rpc->cr_opc),
		DP_UOID(orw->orw_oid), dss_get_module_info()->dmi_tid);
	/* Inline update/fetch */
	if (orw->orw_bulks.da_arrays == NULL && orw->orw_bulks.da_count == 0) {
		rc = ds_obj_rw_inline(rpc, cont, cont_hdl->sch_uuid,
				      map_version);
		D_GOTO(out, rc);
	}

	/* bulk update/fetch */
	if (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_UPDATE) {
		rc = vos_obj_zc_update_begin(cont->sc_hdl,
					     orw->orw_oid, orw->orw_epoch,
					     &orw->orw_dkey, orw->orw_nr,
					     orw->orw_iods.da_arrays, &ioh);
		if (rc != 0) {
			D_ERROR(DF_UOID"preparing update fails: %d\n",
				DP_UOID(orw->orw_oid), rc);
			D_GOTO(out, rc);
		}

		bulk_op = CRT_BULK_GET;
	} else {
		struct obj_rw_out *orwo = crt_reply_get(rpc);

		D_ASSERT(orwo != NULL);

		rc = vos_obj_zc_fetch_begin(cont->sc_hdl,
					    orw->orw_oid, orw->orw_epoch,
					    &orw->orw_dkey, orw->orw_nr,
					    orw->orw_iods.da_arrays, &ioh);
		if (rc != 0) {
			D_ERROR(DF_UOID"preparing fetch fails: %d\n",
				DP_UOID(orw->orw_oid), rc);
			D_GOTO(out, rc);
		}

		bulk_op = CRT_BULK_PUT;

		rc = ds_obj_update_sizes_in_reply(rpc);
		if (rc != 0)
			D_GOTO(out, rc);

		/* no in_line transfer */
		orwo->orw_sgls.da_count = 0;
		orwo->orw_sgls.da_arrays = NULL;

		rc = ds_obj_update_nrs_in_reply(rpc, ioh);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	rc = ds_bulk_transfer(rpc, bulk_op, orw->orw_bulks.da_arrays,
			      ioh, NULL, orw->orw_nr);
	D_EXIT;
out:
	ds_obj_rw_complete(rpc, ioh, rc, map_version,
			   cont_hdl ? cont_hdl->sch_uuid : NULL);
	if (cont_hdl) {
		if (!cont_hdl->sch_cont)
			ds_cont_put(cont); /* -1 for rebuild container */
		ds_cont_hdl_put(cont_hdl);
	}
}

static void
ds_eu_complete(crt_rpc_t *rpc, int status, uint32_t map_version)
{
	struct obj_key_enum_out *oeo;
	struct obj_key_enum_in *oei;
	int rc;

	obj_reply_set_status(rpc, status);
	obj_reply_map_version_set(rpc, map_version);
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: %d\n", rc);

	oei = crt_req_get(rpc);
	D_ASSERT(oei != NULL);
	oeo = crt_reply_get(rpc);
	D_ASSERT(oeo != NULL);

	if (oeo->oeo_kds.da_arrays != NULL)
		D_FREE(oeo->oeo_kds.da_arrays,
		       oei->oei_nr * sizeof(daos_key_desc_t));

	if (oeo->oeo_eprs.da_arrays != NULL)
		D_FREE(oeo->oeo_eprs.da_arrays,
		       oei->oei_nr * sizeof(daos_epoch_range_t));

	if (oeo->oeo_recxs.da_arrays != NULL)
		D_FREE(oeo->oeo_recxs.da_arrays,
		       oei->oei_nr * sizeof(daos_recx_t));

	if (oeo->oeo_cookies.da_arrays != NULL)
		D_FREE(oeo->oeo_cookies.da_arrays,
		       oei->oei_nr * sizeof(uuid_t));

	if (oeo->oeo_vers.da_arrays != NULL)
		D_FREE(oeo->oeo_vers.da_arrays,
		       oei->oei_nr * sizeof(uint32_t));

	if (oeo->oeo_sgl.sg_iovs != NULL) {
		ds_sgls_free(&oeo->oeo_sgl, 1);
		oeo->oeo_sgl.sg_iovs = NULL;
	}
}

int
fill_key(vos_iter_entry_t *key_ent, struct obj_key_enum_in *oei,
	 struct obj_key_enum_out *oeo, unsigned int *kds_idx,
	 unsigned int *iovs_idx)
{
	daos_iov_t	*iovs = oeo->oeo_sgl.sg_iovs;
	daos_key_desc_t	*kds = oeo->oeo_kds.da_arrays;
	unsigned int	iovs_nr = oeo->oeo_sgl.sg_nr.num;
	unsigned int	kds_nr = oei->oei_nr;

	while (*iovs_idx < iovs_nr) {
		if (iovs[*iovs_idx].iov_len + key_ent->ie_key.iov_len >=
			    iovs[*iovs_idx].iov_buf_len) {
			(*iovs_idx)++;
			continue;
		}

		D_ASSERT(*kds_idx < kds_nr);
		kds[*kds_idx].kd_key_len = key_ent->ie_key.iov_len;
		kds[*kds_idx].kd_csum_len = 0;
		oeo->oeo_kds.da_count++;
		(*kds_idx)++;

		memcpy(iovs[*iovs_idx].iov_buf + iovs[*iovs_idx].iov_len,
		       key_ent->ie_key.iov_buf, key_ent->ie_key.iov_len);
		iovs[*iovs_idx].iov_len += key_ent->ie_key.iov_len;

		if (oeo->oeo_sgl.sg_nr.num_out < *iovs_idx + 1)
			oeo->oeo_sgl.sg_nr.num_out = *iovs_idx + 1;

		break;
	}

	/* if it reaches the end of iovs and kds, then return 1 to
	 * break the loop
	 */
	if (*iovs_idx >= iovs_nr || *kds_idx >= kds_nr)
		return 1;

	return 0;
}

int
fill_rec(vos_iter_entry_t *key_ent, struct obj_key_enum_in *oei,
	 struct obj_key_enum_out *oeo, unsigned int *idx)
{
	daos_epoch_range_t *eprs = oeo->oeo_eprs.da_arrays;
	uuid_t		   *cookies = oeo->oeo_cookies.da_arrays;
	daos_recx_t	   *recxs = oeo->oeo_recxs.da_arrays;
	uint32_t	   *vers = oeo->oeo_vers.da_arrays;

	D_ASSERT(*idx < oei->oei_nr);
	eprs[*idx] = key_ent->ie_epr;
	uuid_copy(cookies[*idx], key_ent->ie_cookie);
	recxs[*idx] = key_ent->ie_recx;
	vers[*idx] = key_ent->ie_ver;

	if (oeo->oeo_size == 0)
		oeo->oeo_size = key_ent->ie_rsize;
	else if (oeo->oeo_size != key_ent->ie_rsize)
		return -DER_INVAL;

	oeo->oeo_eprs.da_count++;
	oeo->oeo_cookies.da_count++;
	oeo->oeo_recxs.da_count++;
	oeo->oeo_vers.da_count++;
	(*idx)++;

	if (*idx >= oei->oei_nr)
		return 1;

	return 0;
}

struct ds_iter_arg {
	struct obj_key_enum_in *oei;
	struct obj_key_enum_out *oeo;
	unsigned int	iovs_idx;
	unsigned int	map_version;
	unsigned int	key_nr;
	int		status;
};

struct ds_task_arg {
	unsigned int	opc;
	union {
		struct ds_iter_arg iter_arg;
	} u;
};

static int
ds_iter_single_vos(void *data)
{
	struct ds_task_arg	*arg = data;
	struct ds_iter_arg	*iter_arg = &arg->u.iter_arg;
	struct obj_key_enum_in	*oei = iter_arg->oei;
	struct obj_key_enum_out	*oeo = iter_arg->oeo;
	struct ds_cont_hdl	*cont_hdl;
	struct ds_cont		*cont;
	vos_iter_entry_t	key_ent;
	vos_iter_param_t	param;
	daos_handle_t		ih;
	daos_hash_out_t		*probe_hash;
	int			type;
	int			rc;

	rc = ds_check_container(oei->oei_co_hdl, oei->oei_co_uuid,
				&cont_hdl, &cont);
	if (rc)
		D_GOTO(out, rc);

	D_ASSERT(cont_hdl->sch_pool != NULL);
	if (iter_arg->map_version == 0)
		iter_arg->map_version = cont_hdl->sch_pool->spc_map_version;

	if (oei->oei_map_ver < iter_arg->map_version) {
		D_DEBUG(DB_TRACE, "oei map_version %d map_ver %d\n",
			oei->oei_map_ver, iter_arg->map_version);
		D_GOTO(out_cont_hdl, rc = -DER_STALE);
	}

	if (arg->opc == DAOS_OBJ_AKEY_RPC_ENUMERATE) {
		type = VOS_ITER_AKEY;
	} else if (arg->opc == DAOS_OBJ_DKEY_RPC_ENUMERATE) {
		type = VOS_ITER_DKEY;
	} else {
		if (oei->oei_rec_type == DAOS_IOD_ARRAY)
			type = VOS_ITER_RECX;
		else
			type = VOS_ITER_SINGLE;
	}

	/* prepare iterate parameters */
	memset(&param, 0, sizeof(param));
	param.ip_hdl	= cont->sc_hdl;
	param.ip_oid	= oei->oei_oid;

	if (type == VOS_ITER_RECX || type == VOS_ITER_SINGLE) {
		if (oei->oei_dkey.iov_len == 0 ||
		    oei->oei_akey.iov_len == 0)
			D_GOTO(out_cont_hdl, rc = -DER_PROTO);
		param.ip_dkey = oei->oei_dkey;
		param.ip_akey = oei->oei_akey;

		param.ip_epr.epr_lo = 0;
		param.ip_epr.epr_hi = oei->oei_epoch;
		param.ip_epc_expr = VOS_IT_EPC_RE;
	} else {
		/* XXX epoch is ignored by key enumeration for the time being */
		param.ip_epr.epr_lo = oei->oei_epoch;
		if (type == VOS_ITER_AKEY) {
			if (oei->oei_dkey.iov_len == 0)
				D_GOTO(out_cont_hdl, rc = -DER_PROTO);
			param.ip_dkey = oei->oei_dkey;
		} else {
			if (oei->oei_akey.iov_len > 0)
				param.ip_akey = oei->oei_akey;
		}
	}

	D_DEBUG(DB_TRACE, ""DF_UOID" iterate type %d tag %d\n",
		DP_UOID(oei->oei_oid), type, dss_get_module_info()->dmi_tid);

	rc = vos_iter_prepare(type, &param, &ih);
	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			daos_hash_set_eof(&oeo->oeo_anchor);
			rc = 0;
		} else {
			D_ERROR("Failed to prepare d-key iterator: %d\n", rc);
		}
		D_GOTO(out_cont_hdl, rc);
	}

	if (daos_hash_is_zero(&oei->oei_anchor))
		probe_hash = NULL;
	else
		probe_hash = &oei->oei_anchor;

	rc = vos_iter_probe(ih, probe_hash);
	if (rc != 0) {
		if (rc == -DER_NONEXIST || rc == -DER_AGAIN) {
			daos_hash_set_eof(&oeo->oeo_anchor);
			rc = 0;
		}
		D_GOTO(out_iter_fini, rc);
	}

	while (iter_arg->key_nr < oei->oei_nr) {
		rc = vos_iter_fetch(ih, &key_ent, &oeo->oeo_anchor);
		if (rc != 0)
			break;

		/* fill the key to iov if there are enough space */
		if (type == VOS_ITER_AKEY || type == VOS_ITER_DKEY)
			rc = fill_key(&key_ent, oei, oeo, &iter_arg->key_nr,
				      &iter_arg->iovs_idx);
		else
			rc = fill_rec(&key_ent, oei, oeo, &iter_arg->key_nr);

		if (rc != 0) {
			if (rc == 1)
				rc = vos_iter_next(ih);
			break;
		}
		vos_iter_next(ih);
	}

	if (rc == 0) /* anchor for the next call */
		rc = vos_iter_fetch(ih, &key_ent, &oeo->oeo_anchor);

	if (rc == -DER_NONEXIST) {
		daos_hash_set_eof(&oeo->oeo_anchor);
		rc = 0;
	}
	D_EXIT;
out_iter_fini:
	vos_iter_finish(ih);
out_cont_hdl:
	if (!cont_hdl->sch_cont)
		ds_cont_put(cont); /* -1 for rebuild container */
	ds_cont_hdl_put(cont_hdl);
out:
	return rc;
}

void
ds_obj_enum_handler(crt_rpc_t *rpc)
{
	struct ds_task_arg	task_arg;
	struct obj_key_enum_in	*oei;
	struct obj_key_enum_out	*oeo;
	int			rc = 0;
	int			tag;

	memset(&task_arg, 0, sizeof(task_arg));
	oei = crt_req_get(rpc);
	D_ASSERT(oei != NULL);
	oeo = crt_reply_get(rpc);
	D_ASSERT(oeo != NULL);
	if (opc_get(rpc->cr_opc) == DAOS_OBJ_RECX_RPC_ENUMERATE) {
		if (oei->oei_dkey.iov_len == 0 ||
		    oei->oei_akey.iov_len == 0)
			D_GOTO(out, rc = -DER_PROTO);

		oeo->oeo_eprs.da_count = 0;
		D_ALLOC(oeo->oeo_eprs.da_arrays,
			oei->oei_nr * sizeof(daos_epoch_range_t));
		if (oeo->oeo_eprs.da_arrays == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		oeo->oeo_recxs.da_count = 0;
		D_ALLOC(oeo->oeo_recxs.da_arrays,
			oei->oei_nr * sizeof(daos_recx_t));
		if (oeo->oeo_recxs.da_arrays == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		oeo->oeo_cookies.da_count = 0;
		D_ALLOC(oeo->oeo_cookies.da_arrays,
			oei->oei_nr * sizeof(uuid_t));
		if (oeo->oeo_cookies.da_arrays == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		oeo->oeo_vers.da_count = 0;
		D_ALLOC(oeo->oeo_vers.da_arrays,
			oei->oei_nr * sizeof(uint32_t));
		if (oeo->oeo_vers.da_arrays == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	} else {
		/* prepare buffer for enumerate */
		rc = ds_sgls_prep(&oeo->oeo_sgl, &oei->oei_sgl, 1);
		if (rc != 0)
			D_GOTO(out, rc);

		/* Prepare key desciptor buffer */
		oeo->oeo_kds.da_count = 0;
		D_ALLOC(oeo->oeo_kds.da_arrays,
			oei->oei_nr * sizeof(daos_key_desc_t));
		if (oeo->oeo_kds.da_arrays == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	task_arg.opc = opc_get(rpc->cr_opc);
	task_arg.u.iter_arg.oei = crt_req_get(rpc);
	task_arg.u.iter_arg.oeo = crt_reply_get(rpc);
	task_arg.u.iter_arg.iovs_idx = 0;
	task_arg.u.iter_arg.key_nr = 0;
	task_arg.u.iter_arg.status = 0;
	task_arg.u.iter_arg.map_version = 0;

	/* keep trying until the key_buffer is fully filled or
	 * reaching the end of the stream
	 */
	tag = dss_get_module_info()->dmi_tid;

	while (task_arg.u.iter_arg.key_nr < oei->oei_nr &&
	       tag < dss_get_threads_number()) {
		if (tag == dss_get_module_info()->dmi_tid ||
		    opc_get(rpc->cr_opc) != DAOS_OBJ_DKEY_RPC_ENUMERATE)
			rc = ds_iter_single_vos(&task_arg);
		else
			rc = dss_ult_create_execute(ds_iter_single_vos,
						    &task_arg, tag);

		if (rc != 0)
			D_GOTO(out, rc);

		/* reset hash if needed. Note: Only dkey iteration might
		 * go multiple tags.
		 */
		if (daos_hash_is_eof(&oeo->oeo_anchor)) {
			if (opc_get(rpc->cr_opc) ==
			    DAOS_OBJ_DKEY_RPC_ENUMERATE &&
			    tag < dss_get_threads_number() - 1) {
				tag++;
				enum_anchor_reset_hkey(&oeo->oeo_anchor);
				enum_anchor_reset_hkey(&oei->oei_anchor);
			} else {
				break;
			}
		}
	}

	enum_anchor_set_tag(&oeo->oeo_anchor, tag);
	if (oei->oei_bulk != NULL) {
		daos_sg_list_t *sgl = &oeo->oeo_sgl;

		rc = ds_bulk_transfer(rpc, CRT_BULK_PUT, &oei->oei_bulk,
				      DAOS_HDL_INVAL, &sgl, 1);

		/* If the keys will be replied by bulk, then let's empty
		 * the sgl in the reply to avoid confusing
		 */
		ds_sgls_free(sgl, 1);
		oeo->oeo_sgl.sg_iovs = NULL;
		oeo->oeo_sgl.sg_nr.num = 0;
		oeo->oeo_sgl.sg_nr.num_out = 0;
	}

out:
	if (rc == 0)
		rc = task_arg.u.iter_arg.status;

	ds_eu_complete(rpc, rc, task_arg.u.iter_arg.map_version);
}
