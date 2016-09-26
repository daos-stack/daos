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
 * dsms: object Operations
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related to object.
 */

#include <uuid/uuid.h>

#include <abt.h>
#include <daos/transport.h>
#include <daos_srv/daos_m_srv.h>
#include <daos_srv/vos.h>
#include <daos_srv/daos_server.h>

#include "dsm_rpc.h"
#include "dsms_internal.h"
#include "dsms_layout.h"

static void
dsms_eu_free_iovs_sgls(daos_iov_t *iovs, daos_sg_list_t **sgls, int nr)
{
	int i;

	if (iovs != NULL) {
		for (i = 0; i < nr; i++) {
			if (iovs[i].iov_buf != NULL)
				D_FREE(iovs[i].iov_buf,
				       iovs[i].iov_buf_len);
		}
		D_FREE(iovs, nr * sizeof(*iovs));
	}

	if (sgls != NULL) {
		for (i = 0; i < nr; i++) {
			if (sgls[i] != NULL)
				D_FREE_PTR(sgls[i]);
		}
		D_FREE(sgls, nr * sizeof(*sgls));
	}
}

/**
 * After bulk finish, let's send reply, then release the resource.
 */
static void
dsms_rw_complete(dtp_rpc_t *rpc, daos_handle_t ioh, int status)
{
	struct object_update_in	*oui;
	int rc;

	dsm_set_reply_status(rpc, status);
	rc = dtp_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: %d\n", rc);

	if (opc_get(rpc->dr_opc) == DSM_TGT_OBJ_FETCH) {
		struct object_fetch_out *ofo;

		ofo = dtp_reply_get(rpc);

		D_ASSERT(ofo != NULL);
		D_FREE(ofo->ofo_sizes.da_arrays,
		       ofo->ofo_sizes.da_count * sizeof(uint64_t));
	}

	if (daos_handle_is_inval(ioh))
		return;

	oui = dtp_req_get(rpc);
	D_ASSERT(oui != NULL);
	if (opc_get(rpc->dr_opc) == DSM_TGT_OBJ_UPDATE)
		rc = vos_obj_zc_update_end(ioh, &oui->oui_dkey,
					   oui->oui_nr,
					   oui->oui_iods.da_arrays,
					   0, NULL);
	else
		rc = vos_obj_zc_fetch_end(ioh, &oui->oui_dkey,
					  oui->oui_nr,
					  oui->oui_iods.da_arrays,
					  0, NULL);
	if (rc != 0)
		D_ERROR(DF_UOID "%x send reply failed: %d\n",
			DP_UOID(oui->oui_oid),
			opc_get(rpc->dr_opc), rc);
}

static void
dsms_eu_complete(dtp_rpc_t *rpc, daos_sg_list_t **sgls,
		 daos_iov_t *iov, int status)
{
	struct object_enumerate_out *oeo;
	int rc;

	dsm_set_reply_status(rpc, status);
	rc = dtp_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: %d\n", rc);

	dsms_eu_free_iovs_sgls(iov, sgls, 1);

	oeo = dtp_reply_get(rpc);
	D_ASSERT(oeo != NULL);
	D_FREE(oeo->oeo_kds.da_arrays,
	       oeo->oeo_kds.da_count * sizeof(daos_key_desc_t));
}

struct dsms_bulk_async_args {
	ABT_future	future;
	int		result;
};

static int
bulk_complete_cb(const struct dtp_bulk_cb_info *cb_info)
{
	struct dsms_bulk_async_args	*arg;
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
	arg = (struct dsms_bulk_async_args *)cb_info->bci_arg;
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
dsms_bulk_transfer(dtp_rpc_t *rpc, daos_handle_t dch, dtp_bulk_t *remote_bulks,
		   daos_sg_list_t **sgls, daos_iov_t *iovs, daos_handle_t ioh,
		   int nr, dtp_bulk_op_t bulk_op)
{
	dtp_bulk_opid_t		bulk_opid;
	dtp_bulk_perm_t		bulk_perm;
	ABT_future		future;
	struct dsms_bulk_async_args arg;
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

int
dsms_hdlr_object_rw(dtp_rpc_t *rpc)
{
	struct object_update_in	*oui;
	struct tgt_cont_hdl	*tch;
	daos_handle_t		ioh = DAOS_HDL_INVAL;
	daos_sg_list_t		**sgls = NULL;
	dtp_bulk_op_t		bulk_op;
	struct dsm_tls		*tls = dsm_tls_get();
	int			i;
	int			rc;

	oui = dtp_req_get(rpc);
	if (oui == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	tch = dsms_tgt_cont_hdl_lookup(&tls->dt_cont_hdl_hash, oui->oui_co_hdl);
	if (tch == NULL)
		D_GOTO(out, rc = -DER_NO_PERM);

	if (opc_get(rpc->dr_opc) == DSM_TGT_OBJ_UPDATE) {
		rc = vos_obj_zc_update_begin(tch->tch_cont->dvc_hdl,
					     oui->oui_oid, oui->oui_epoch,
					     &oui->oui_dkey, oui->oui_nr,
					     oui->oui_iods.da_arrays, &ioh,
					     NULL);
		if (rc != 0) {
			D_ERROR(DF_UOID"preparing update fails: %d\n",
				DP_UOID(oui->oui_oid), rc);
			D_GOTO(out_tch, rc);
		}

		bulk_op = DTP_BULK_GET;
	} else {
		struct object_fetch_out *ofo;
		daos_vec_iod_t	*vecs;
		uint64_t	*sizes;
		int		size_count = 0;
		int		i;
		int		j;
		int		idx = 0;

		rc = vos_obj_zc_fetch_begin(tch->tch_cont->dvc_hdl,
					    oui->oui_oid, oui->oui_epoch,
					    &oui->oui_dkey, oui->oui_nr,
					    oui->oui_iods.da_arrays, &ioh,
					    NULL);
		if (rc != 0) {
			D_ERROR(DF_UOID"preparing fetch fails: %d\n",
				DP_UOID(oui->oui_oid), rc);
			D_GOTO(out_tch, rc);
		}

		bulk_op = DTP_BULK_PUT;

		/* update the sizes in reply */
		vecs = oui->oui_iods.da_arrays;
		for (i = 0; i < oui->oui_iods.da_count; i++)
			size_count += vecs[i].vd_nr;

		ofo = dtp_reply_get(rpc);
		ofo->ofo_sizes.da_count = size_count;
		D_ALLOC(ofo->ofo_sizes.da_arrays,
			size_count * sizeof(uint64_t));
		if (ofo->ofo_sizes.da_arrays == NULL)
			D_GOTO(out_tch, rc = -DER_NOMEM);

		sizes = ofo->ofo_sizes.da_arrays;
		for (i = 0; i < oui->oui_iods.da_count; i++) {
			for (j = 0; j < vecs[i].vd_nr; j++) {
				sizes[idx] = vecs[i].vd_recxs[j].rx_rsize;
				idx++;
			}
		}
	}

	D_ALLOC(sgls, oui->oui_nr * sizeof(*sgls));
	if (sgls == NULL)
		D_GOTO(out_tch, rc);

	for (i = 0; i < oui->oui_nr; i++)
		vos_obj_zc_vec2sgl(ioh, i, &sgls[i]);

	rc = dsms_bulk_transfer(rpc, tch->tch_cont->dvc_hdl,
				oui->oui_bulks.da_arrays, sgls, NULL, ioh,
				oui->oui_nr, bulk_op);
out_tch:
	dsms_tgt_cont_hdl_put(&tls->dt_cont_hdl_hash, tch);
out:
	dsms_rw_complete(rpc, ioh, rc);

	return rc;
}

static int
dsms_eu_bulks_prep(dtp_rpc_t *rpc, int nr, daos_iov_t **piovs,
		   daos_sg_list_t ***psgls, dtp_bulk_t *remote_bulks)
{
	daos_iov_t	*iovs = NULL;
	daos_sg_list_t	**sgls = NULL;
	int		i;
	int		rc;

	D_ALLOC(iovs, nr * sizeof(*iovs));
	if (iovs == NULL)
		return -DER_NOMEM;

	D_ALLOC(sgls, nr * sizeof(*sgls));
	if (sgls == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < nr; i++) {
		daos_size_t bulk_len = 0;

		if (remote_bulks[i] != NULL) {
			/** FIXME must support more than one iov per sg */
			rc = dtp_bulk_get_len(remote_bulks[i], &bulk_len);
			if (rc != 0) {
				D_ERROR("i %d get bulk len error.: rc = %d\n",
					 i, rc);
				D_GOTO(out, rc);
			}
		}

		iovs[i].iov_len = 0;
		iovs[i].iov_buf_len = bulk_len;
		D_ALLOC(iovs[i].iov_buf, bulk_len);
		if (iovs[i].iov_buf == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		D_ALLOC_PTR(sgls[i]);
		if (sgls[i] == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		sgls[i]->sg_nr.num = 1;
		sgls[i]->sg_iovs = &iovs[i];
	}

	*piovs = iovs;
	*psgls = sgls;
out:
	if (rc != 0)
		dsms_eu_free_iovs_sgls(iovs, sgls, 1);

	return rc;
}

int
dsms_hdlr_object_enumerate(dtp_rpc_t *rpc)
{
	struct object_enumerate_in	*oei;
	struct object_enumerate_out	*oeo;
	struct tgt_cont_hdl		*tch;
	daos_iov_t			*iovs = NULL;
	daos_sg_list_t			**sgls = NULL;
	vos_iter_param_t		param;
	daos_key_desc_t			*kds;
	daos_handle_t			ih;
	struct dsm_tls			*tls = dsm_tls_get();
	int				rc = 0;
	int				dkey_nr = 0;

	oei = dtp_req_get(rpc);
	if (oei == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	rc = dsms_eu_bulks_prep(rpc, 1, &iovs, &sgls, &oei->oei_bulk);
	if (rc != 0)
		D_GOTO(out, rc);

	tch = dsms_tgt_cont_hdl_lookup(&tls->dt_cont_hdl_hash, oei->oei_co_hdl);
	if (tch == NULL)
		D_GOTO(out, rc = -DER_NO_PERM);

	oeo = dtp_reply_get(rpc);
	if (oei == NULL)
		D_GOTO(out_tch, rc = -DER_INVAL);
	memset(&param, 0, sizeof(param));
	param.ip_hdl	= tch->tch_cont->dvc_hdl;
	param.ip_oid	= oei->oei_oid;
	param.ip_epr.epr_lo = oei->oei_epoch;
	rc = vos_iter_prepare(VOS_ITER_DKEY, &param, &ih);
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

	dkey_nr = 0;
	kds = oeo->oeo_kds.da_arrays;
	while (1) {
		vos_iter_entry_t  dkey_ent;
		bool nospace = false;

		rc = vos_iter_fetch(ih, &dkey_ent, &oeo->oeo_anchor);
		if (rc != 0)
			break;

		D_DEBUG(DF_MISC, "get key %s len "DF_U64
			"iov_len "DF_U64" buflen "DF_U64"\n",
			(char *)dkey_ent.ie_key.iov_buf,
			dkey_ent.ie_key.iov_len,
			iovs->iov_len, iovs->iov_buf_len);

		/* fill the key to iovs if there are enough space */
		if (iovs->iov_len + dkey_ent.ie_key.iov_len <
		    iovs->iov_buf_len) {
			/* Fill key descriptor */
			kds[dkey_nr].kd_key_len = dkey_ent.ie_key.iov_len;
			/* FIXME no checksum now */
			kds[dkey_nr].kd_csum_len = 0;
			dkey_nr++;

			memcpy(iovs->iov_buf + iovs->iov_len,
			       dkey_ent.ie_key.iov_buf,
			       dkey_ent.ie_key.iov_len);
			iovs->iov_len += dkey_ent.ie_key.iov_len;
			rc = vos_iter_next(ih);
			if (rc != 0) {
				if (rc == -DER_NONEXIST)
					daos_hash_set_eof(&oeo->oeo_anchor);
				break;
			}
		} else {
			nospace = true;
		}

		if (dkey_nr >= oei->oei_nr || nospace) {
			rc = vos_iter_fetch(ih, &dkey_ent, &oeo->oeo_anchor);
			break;
		}
	}
	vos_iter_finish(ih);

	/* it means iteration hit the end */
	if (rc == -DER_NONEXIST) {
		rc = 0;
	} else if (rc < 0) {
		D_ERROR("Failed to fetch dkey: %d\n", rc);
		D_GOTO(out_tch, rc);
	}

	oeo->oeo_kds.da_count = dkey_nr;

	rc = dsms_bulk_transfer(rpc, tch->tch_cont->dvc_hdl, &oei->oei_bulk,
				sgls, iovs, DAOS_HDL_INVAL, 1, DTP_BULK_PUT);

out_tch:
	dsms_tgt_cont_hdl_put(&tls->dt_cont_hdl_hash, tch);
out:
	dsms_eu_complete(rpc, sgls, iovs, rc);
	return rc;
}
