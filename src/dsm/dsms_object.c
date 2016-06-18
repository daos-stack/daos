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

#include <daos/transport.h>
#include <daos_srv/daos_m_srv.h>
#include <daos_srv/vos.h>
#include <daos_srv/daos_server.h>

#include "dsm_rpc.h"
#include "dsms_internal.h"
#include "dsms_layout.h"

/* XXX let's keep the pool open until module fini */
void
dsms_pools_close()
{
	struct dsm_tls *tls = dsm_tls_get();
	struct dsms_vpool *dvp;
	struct dsms_vpool *tmp;

	daos_list_for_each_entry_safe(dvp, tmp, &tls->dt_pool_list, dvp_list) {
		daos_list_del(&dvp->dvp_list);
		vos_pool_close(dvp->dvp_hdl, NULL);
		D_FREE_PTR(dvp);
	}
}

static struct dsms_vpool *
dsms_vpool_lookup(const uuid_t vp_uuid)
{
	struct dsm_tls *tls = dsm_tls_get();
	struct dsms_vpool *dvp;

	daos_list_for_each_entry(dvp, &tls->dt_pool_list, dvp_list) {
		if (uuid_compare(vp_uuid, dvp->dvp_uuid) == 0) {
			return dvp;
		}
	}
	return NULL;
}

void
dsms_conts_close()
{
	struct dsm_tls *tls = dsm_tls_get();
	struct dsms_vcont *dcont;
	struct dsms_vcont *tmp;

	daos_list_for_each_entry_safe(dcont, tmp, &tls->dt_cont_list,
				      dvc_list) {
		daos_list_del(&dcont->dvc_list);
		vos_co_close(dcont->dvc_hdl, NULL);
		D_FREE_PTR(dcont);
	}
}

static struct dsms_vcont *
dsms_vcont_lookup(const uuid_t dc_uuid)
{
	struct dsm_tls *tls = dsm_tls_get();
	struct dsms_vcont *dcont;

	daos_list_for_each_entry(dcont, &tls->dt_cont_list, dvc_list) {
		if (uuid_compare(dc_uuid, dcont->dvc_uuid) == 0)
			return dcont;
	}

	return NULL;
}

static int
dsms_co_open_create(daos_handle_t pool_hdl, uuid_t co_uuid,
		    daos_handle_t *co_hdl)
{
	struct dsms_vcont *dcont;
	struct dsm_tls *tls = dsm_tls_get();
	int rc;

	D_DEBUG(DF_MISC, "opening container "DF_UUID"\n",
		DP_UUID(co_uuid));

	dcont = dsms_vcont_lookup(co_uuid);
	if (dcont != NULL) {
		*co_hdl = dcont->dvc_hdl;
		D_DEBUG(DF_MISC, "get container "DF_UUID" from cache.\n",
			DP_UUID(co_uuid));
		return 0;
	}

	rc = vos_co_open(pool_hdl, co_uuid, co_hdl, NULL);
	if (rc == -DER_NONEXIST) {
		D_DEBUG(DF_MISC, "creating container "DF_UUID"\n",
			DP_UUID(co_uuid));
		/** create container on-the-fly */
		rc = vos_co_create(pool_hdl, co_uuid, NULL);
		if (rc != 0)
			return rc;
		/** attempt to open again now that it is created ... */
		rc = vos_co_open(pool_hdl, co_uuid, co_hdl, NULL);
		if (rc != 0)
			return rc;
	}

	/* Add container to cache */
	D_ALLOC_PTR(dcont);
	if (dcont == NULL) {
		vos_co_close(*co_hdl, NULL);
		return -DER_NOMEM;
	}

	uuid_copy(dcont->dvc_uuid, co_uuid);
	dcont->dvc_hdl = *co_hdl;
	daos_list_add(&dcont->dvc_list, &tls->dt_cont_list);
	return rc;
}

static int
dsms_pool_open(const uuid_t pool_uuid, daos_handle_t *vph)
{
	struct dss_module_info	*dmi;
	struct dsms_vpool	*vpool;
	struct dsms_vpool	*new;
	struct dsm_tls		*tls = dsm_tls_get();
	char			*path;
	int			 rc;

	vpool = dsms_vpool_lookup(pool_uuid);
	if (vpool != NULL) {
		*vph = vpool->dvp_hdl;
		D_DEBUG(DF_MISC, "get pool "DF_UUID" from cache.\n",
			DP_UUID(pool_uuid));
		return 0;
	}

	dmi = dss_get_module_info();
	rc = dmgs_tgt_file(pool_uuid, VOS_FILE, &dmi->dmi_tid, &path);
	if (rc != 0)
		return rc;

	rc = vos_pool_open(path, (unsigned char *)pool_uuid, vph, NULL);
	if (rc != 0)
		D_GOTO(out_free, rc);

	D_ALLOC_PTR(new);
	if (new == NULL)
		D_GOTO(out_close, rc = -DER_NOMEM);

	uuid_copy(new->dvp_uuid, pool_uuid);
	new->dvp_hdl = *vph;
	DAOS_INIT_LIST_HEAD(&new->dvp_list);

	D_DEBUG(DF_MISC, "add pool "DF_UUID"\n",
		DP_UUID(pool_uuid));
	daos_list_add(&new->dvp_list, &tls->dt_pool_list);

	D_DEBUG(DF_MISC, "open pool "DF_X64"\n", vph->cookie);

out_close:
	if (rc != 0)
		vos_pool_close(*vph, NULL);
out_free:
	free(path);
	return rc;
}

struct dsms_bulk_async_args {
	int		nr;
	daos_sg_list_t	**sgls;
	daos_iov_t	*iovs;
	daos_handle_t	iohdl;
};

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
		D_FREE(ofo->ofo_sizes.arrays,
		       ofo->ofo_sizes.count * sizeof(uint64_t));
	}

	if (daos_handle_is_inval(ioh))
		return;

	oui = dtp_req_get(rpc);
	D_ASSERT(oui != NULL);
	if (opc_get(rpc->dr_opc) == DSM_TGT_OBJ_UPDATE)
		rc = vos_obj_zc_update_end(ioh, &oui->oui_dkey,
					   oui->oui_nr,
					   oui->oui_iods.arrays,
					   0, NULL);
	else
		rc = vos_obj_zc_fetch_end(ioh, &oui->oui_dkey,
					  oui->oui_nr,
					  oui->oui_iods.arrays,
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
	D_FREE(oeo->oeo_kds.arrays,
	       oeo->oeo_kds.count * sizeof(daos_key_desc_t));
}

static int
bulk_complete_cb(const struct dtp_bulk_cb_info *cb_info)
{
	struct dsms_bulk_async_args	*args;
	struct dtp_bulk_desc		*bulk_desc;
	dtp_rpc_t			*rpc;
	dtp_bulk_t			local_bulk_hdl;
	struct daos_ref			*dref;
	int				rc = 0;

	rc = cb_info->bci_rc;
	if (rc != 0)
		D_ERROR("bulk transfer failed: rc = %d\n", rc);

	bulk_desc = cb_info->bci_bulk_desc;
	local_bulk_hdl = bulk_desc->bd_local_hdl;
	rpc = bulk_desc->bd_rpc;
	dref = rpc->dr_data;
	args = (struct dsms_bulk_async_args *)cb_info->bci_arg;

	/* NB: this will only be called if all of bulks
	 * complete successfully inside the handler, other
	 * wise async_complete will be called in the error
	 * handler path
	 **/
	if (daos_ref_dec_and_test(dref)) {
		if (opc_get(rpc->dr_opc) == DSM_TGT_OBJ_ENUMERATE)
			dsms_eu_complete(rpc, args->sgls, args->iovs, rc);
		else
			dsms_rw_complete(rpc, args->iohdl, rc);
	}
	dtp_bulk_free(local_bulk_hdl);
	dtp_req_decref(rpc);
	D_FREE_PTR(args);
	return rc;
}

static int
obj_rpc_final_cb(dtp_rpc_t *rpc)
{
	struct daos_ref *dr = rpc->dr_data;

	if (dr != NULL) {
		pthread_mutex_destroy(&dr->dr_lock);
		D_FREE_PTR(dr);
	}

	return 0;
}

static int
dsms_bulk_transfer(dtp_rpc_t *rpc, daos_handle_t dph, daos_handle_t dch,
		   dtp_bulk_t *remote_bulks, daos_sg_list_t **sgls,
		   daos_iov_t *iovs, daos_handle_t ioh, int nr,
		   dtp_bulk_op_t bulk_op, bool *bulk_sent)
{
	dtp_bulk_opid_t		bulk_opid;
	dtp_bulk_perm_t		bulk_perm;
	int			i;
	int			rc = 0;

	bulk_perm = bulk_op == DTP_BULK_PUT ? DTP_BULK_RO : DTP_BULK_RW;

	if (bulk_sent != NULL)
		*bulk_sent = false;

	for (i = 0; i < nr && remote_bulks[i] != NULL; i++) {
		struct dtp_bulk_desc		bulk_desc;
		struct dsms_bulk_async_args	*arg;
		dtp_bulk_t			local_bulk_hdl;

		D_ALLOC_PTR(arg);
		if (arg == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		arg->nr = nr;
		arg->sgls = sgls;
		arg->iovs = iovs;
		arg->iohdl = ioh;

		rc = dtp_bulk_create(rpc->dr_ctx, sgls[i], bulk_perm,
				     &local_bulk_hdl);
		if (rc != 0) {
			D_ERROR("dtp_bulk_create i %d failed, rc: %d.\n",
				i, rc);
			D_FREE_PTR(arg);
			D_GOTO(out, rc);
		}

		dtp_req_addref(rpc);

		bulk_desc.bd_rpc = rpc;
		bulk_desc.bd_bulk_op = bulk_op;
		bulk_desc.bd_remote_hdl = remote_bulks[i];
		bulk_desc.bd_local_hdl = local_bulk_hdl;
		bulk_desc.bd_len = sgls[i]->sg_iovs->iov_len;
		bulk_desc.bd_remote_off = 0;
		bulk_desc.bd_local_off = 0;

		rc = dtp_bulk_transfer(&bulk_desc,
				       bulk_complete_cb,
				       arg, &bulk_opid);
		if (rc < 0) {
			D_ERROR("dtp_bulk_transfer failed, rc: %d.\n", rc);
			dtp_bulk_free(local_bulk_hdl);
			dtp_req_decref(rpc);
			D_FREE_PTR(arg);
			D_GOTO(out, rc);
		}
		if (bulk_sent != NULL)
			*bulk_sent = true;
	}
out:
	return rc;
}

int
dsms_hdlr_object_rw(dtp_rpc_t *rpc)
{
	struct object_update_in	*oui;
	daos_handle_t		dph = DAOS_HDL_INVAL;
	daos_handle_t		dch = DAOS_HDL_INVAL;
	daos_handle_t		ioh = DAOS_HDL_INVAL;
	daos_sg_list_t		**sgls = NULL;
	dtp_bulk_op_t		bulk_op;
	struct daos_ref		*dref;
	bool			bulk_sent = false;
	int			i;
	int			rc;

	oui = dtp_req_get(rpc);
	if (oui == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* Open the pool and container */
	rc = dsms_pool_open(oui->oui_pool_uuid, &dph);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = dsms_co_open_create(dph, oui->oui_co_uuid, &dch);
	if (rc != 0)
		D_GOTO(out, rc);

	D_ALLOC_PTR(dref);
	if (dref == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rpc->dr_final_cb = obj_rpc_final_cb;
	rpc->dr_data = dref;
	daos_ref_init(dref, oui->oui_nr);
	if (opc_get(rpc->dr_opc) == DSM_TGT_OBJ_UPDATE) {
		rc = vos_obj_zc_update_begin(dch, oui->oui_oid, oui->oui_epoch,
					     &oui->oui_dkey, oui->oui_nr,
					     oui->oui_iods.arrays, &ioh, NULL);
		if (rc != 0) {
			D_ERROR(DF_UOID"preparing update fails: %d\n",
				DP_UOID(oui->oui_oid), rc);
			D_GOTO(out, rc);
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

		rc = vos_obj_zc_fetch_begin(dch, oui->oui_oid, oui->oui_epoch,
					    &oui->oui_dkey, oui->oui_nr,
					    oui->oui_iods.arrays, &ioh, NULL);
		if (rc != 0) {
			D_ERROR(DF_UOID"preparing fetch fails: %d\n",
				DP_UOID(oui->oui_oid), rc);
			D_GOTO(out, rc);
		}

		bulk_op = DTP_BULK_PUT;

		/* update the sizes in reply */
		vecs = oui->oui_iods.arrays;
		for (i = 0; i < oui->oui_iods.count; i++)
			size_count += vecs[i].vd_nr;

		ofo = dtp_reply_get(rpc);
		ofo->ofo_sizes.count = size_count;
		D_ALLOC(ofo->ofo_sizes.arrays,
			size_count * sizeof(uint64_t));
		if (ofo->ofo_sizes.arrays == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		sizes = ofo->ofo_sizes.arrays;
		for (i = 0; i < oui->oui_iods.count; i++) {
			for (j = 0; j < vecs[i].vd_nr; j++) {
				sizes[idx] = vecs[i].vd_recxs[j].rx_rsize;
				idx++;
			}
		}
	}

	D_ALLOC(sgls, oui->oui_nr * sizeof(*sgls));
	if (sgls == NULL)
		D_GOTO(out, rc);

	for (i = 0; i < oui->oui_nr; i++)
		vos_obj_zc_vec2sgl(ioh, i, &sgls[i]);

	rc = dsms_bulk_transfer(rpc, dph, dch, oui->oui_bulks.arrays,
				sgls, NULL, ioh, oui->oui_nr, bulk_op,
				&bulk_sent);
	if (!bulk_sent)
		D_GOTO(out, rc);

	return rc;
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
	struct daos_ref	*dref;
	int		i;
	int		rc;

	D_ALLOC(iovs, nr * sizeof(*iovs));
	if (iovs == NULL)
		return -DER_NOMEM;

	D_ALLOC(sgls, nr * sizeof(*sgls));
	if (sgls == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC_PTR(dref);
	if (dref == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rpc->dr_final_cb = obj_rpc_final_cb;
	rpc->dr_data = dref;
	daos_ref_init(dref, nr);
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
	daos_handle_t			dph = DAOS_HDL_INVAL;
	daos_handle_t			dch = DAOS_HDL_INVAL;
	daos_iov_t			*iovs = NULL;
	daos_sg_list_t			**sgls = NULL;
	vos_iter_param_t		param;
	daos_key_desc_t			*kds;
	daos_handle_t			ih;
	int				rc = 0;
	int				dkey_nr = 0;
	bool				bulk_sent = false;

	oei = dtp_req_get(rpc);
	if (oei == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	rc = dsms_eu_bulks_prep(rpc, 1, &iovs, &sgls, &oei->oei_bulk);
	if (rc != 0)
		D_GOTO(out, rc);

	/* Open the pool and container */
	rc = dsms_pool_open(oei->oei_pool_uuid, &dph);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = dsms_co_open_create(dph, oei->oei_co_uuid, &dch);
	if (rc != 0)
		D_GOTO(out, rc);

	oeo = dtp_reply_get(rpc);
	if (oei == NULL)
		D_GOTO(out, rc = -DER_INVAL);
	memset(&param, 0, sizeof(param));
	param.ip_hdl	= dch;
	param.ip_oid	= oei->oei_oid;
	param.ip_epr.epr_lo = oei->oei_epoch;
	rc = vos_iter_prepare(VOS_ITER_DKEY, &param, &ih);
	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			daos_hash_set_eof(&oeo->oeo_anchor);
			oeo->oeo_kds.count = 0;
			rc = 0;
		} else {
			D_ERROR("Failed to prepare d-key iterator: %d\n", rc);
		}
		D_GOTO(out, rc);
	}

	rc = vos_iter_probe(ih, &oei->oei_anchor);
	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			daos_hash_set_eof(&oeo->oeo_anchor);
			oeo->oeo_kds.count = 0;
			rc = 0;
		}
		vos_iter_finish(ih);
		D_GOTO(out, rc);
	}

	/* Prepare key desciptor buffer */
	oeo->oeo_kds.count = oei->oei_nr;
	D_ALLOC(oeo->oeo_kds.arrays,
		oei->oei_nr * sizeof(daos_key_desc_t));
	if (oeo->oeo_kds.arrays == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dkey_nr = 0;
	kds = oeo->oeo_kds.arrays;
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
		D_GOTO(out, rc);
	}

	oeo->oeo_kds.count = dkey_nr;

	rc = dsms_bulk_transfer(rpc, dph, dch, &oei->oei_bulk,
				sgls, iovs, DAOS_HDL_INVAL, 1,
				DTP_BULK_PUT, &bulk_sent);
	if (!bulk_sent)
		D_GOTO(out, rc);

	return rc;
out:
	dsms_eu_complete(rpc, sgls, iovs, rc);
	return rc;
}
