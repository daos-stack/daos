/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * object server operations
 *
 * This file contains the object server remote object api.
 */
#define D_LOGFAC	DD_FAC(object)

#include <uuid/uuid.h>

#include <abt.h>
#include <daos/rpc.h>
#include <daos_srv/pool.h>
#include <daos_srv/rebuild.h>
#include <daos_srv/container.h>
#include <daos_srv/vos.h>
#include <daos_srv/bio.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/dtx_srv.h>
#include "obj_rpc.h"
#include "obj_internal.h"

struct obj_remote_cb_arg {
	dtx_sub_comp_cb_t		comp_cb;
	crt_rpc_t			*parent_req;
	struct dtx_leader_handle	*dlh;
	int				idx;
	void				*cpd_reqs;
	void				*cpd_desc;
	void				*cpd_head;
	void				*cpd_dcsr;
	void				*cpd_dcde;
};

static void
shard_update_req_cb(const struct crt_cb_info *cb_info)
{
	crt_rpc_t			*req = cb_info->cci_rpc;
	struct obj_remote_cb_arg	*arg = cb_info->cci_arg;
	crt_rpc_t			*parent_req = arg->parent_req;
	struct obj_rw_out		*orwo = crt_reply_get(req);
	struct obj_rw_in		*orw_parent = crt_req_get(parent_req);
	struct dtx_leader_handle	*dlh = arg->dlh;
	int				rc = cb_info->cci_rc;
	int				rc1 = 0;

	if (orw_parent->orw_map_ver < orwo->orw_map_version) {
		D_DEBUG(DB_IO, DF_UOID": map_ver stale (%d < %d).\n",
			DP_UOID(orw_parent->orw_oid), orw_parent->orw_map_ver,
			orwo->orw_map_version);
		rc1 = -DER_STALE;
	} else {
		rc1 = orwo->orw_ret;
	}

	if (rc >= 0)
		rc = rc1;

	if (arg->comp_cb)
		arg->comp_cb(dlh, arg->idx, rc);

	crt_req_decref(parent_req);
	D_FREE(arg);
}

/* Execute update on the remote target */
int
ds_obj_remote_update(struct dtx_leader_handle *dlh, void *data, int idx,
		     dtx_sub_comp_cb_t comp_cb)
{
	struct ds_obj_exec_arg		*obj_exec_arg = data;
	struct obj_ec_split_req		*split_req = obj_exec_arg->args;
	struct obj_tgt_oiod		*tgt_oiod;
	struct daos_shard_tgt		*shard_tgt;
	crt_endpoint_t			 tgt_ep;
	crt_rpc_t			*parent_req = obj_exec_arg->rpc;
	crt_rpc_t			*req;
	struct dtx_sub_status		*sub;
	struct dtx_handle		*dth = &dlh->dlh_handle;
	struct obj_remote_cb_arg	*remote_arg = NULL;
	struct obj_rw_in		*orw;
	struct obj_rw_in		*orw_parent;
	uint32_t			 tgt_idx;
	int				 rc = 0;

	D_ASSERT(idx < dlh->dlh_sub_cnt);
	sub = &dlh->dlh_subs[idx];
	shard_tgt = &sub->dss_tgt;
	if (DAOS_FAIL_CHECK(DAOS_OBJ_TGT_IDX_CHANGE)) {
		/* to trigger retry on all other shards */
		if (shard_tgt->st_shard != daos_fail_value_get()) {
			D_DEBUG(DB_TRACE, "complete shard %d update as "
				"-DER_TIMEDOUT.\n", shard_tgt->st_shard);
			D_GOTO(out, rc = -DER_TIMEDOUT);
		}
	}

	D_ALLOC_PTR(remote_arg);
	if (remote_arg == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	tgt_ep.ep_grp = NULL;
	tgt_ep.ep_rank = shard_tgt->st_rank;
	tgt_ep.ep_tag = shard_tgt->st_tgt_idx;

	remote_arg->dlh = dlh;
	remote_arg->comp_cb = comp_cb;
	remote_arg->idx = idx;
	crt_req_addref(parent_req);
	remote_arg->parent_req = parent_req;

	rc = obj_req_create(dss_get_module_info()->dmi_ctx, &tgt_ep,
			    DAOS_OBJ_RPC_TGT_UPDATE, &req);
	if (rc != 0) {
		D_ERROR("crt_req_create failed, rc "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	orw_parent = crt_req_get(parent_req);
	orw = crt_req_get(req);
	*orw = *orw_parent;
	if (split_req != NULL) {
		tgt_idx = shard_tgt->st_shard;
		tgt_oiod = obj_ec_tgt_oiod_get(split_req->osr_tgt_oiods,
					       dlh->dlh_sub_cnt + 1,
					       tgt_idx - obj_exec_arg->start);
		D_ASSERT(tgt_oiod != NULL);
		orw->orw_iod_array.oia_oiods = tgt_oiod->oto_oiods;
		orw->orw_iod_array.oia_oiod_nr = orw->orw_iod_array.oia_iod_nr;
		orw->orw_iod_array.oia_offs = tgt_oiod->oto_offs;
	}
	orw->orw_oid.id_shard = shard_tgt->st_shard_id;
	uuid_copy(orw->orw_co_hdl, orw_parent->orw_co_hdl);
	uuid_copy(orw->orw_co_uuid, orw_parent->orw_co_uuid);
	orw->orw_shard_tgts.ca_count	= orw_parent->orw_shard_tgts.ca_count;
	orw->orw_shard_tgts.ca_arrays	= orw_parent->orw_shard_tgts.ca_arrays;
	orw->orw_flags |= ORF_BULK_BIND | obj_exec_arg->flags;
	orw->orw_dti_cos.ca_count	= dth->dth_dti_cos_count;
	orw->orw_dti_cos.ca_arrays	= dth->dth_dti_cos;

	D_DEBUG(DB_TRACE, DF_UOID" forwarding to rank:%d tag:%d.\n",
		DP_UOID(orw->orw_oid), tgt_ep.ep_rank, tgt_ep.ep_tag);
	rc = crt_req_send(req, shard_update_req_cb, remote_arg);
	if (rc != 0)
		D_ERROR("crt_req_send failed, rc "DF_RC"\n", DP_RC(rc));
	return rc;

out:
	if (rc) {
		sub->dss_result = rc;
		if (comp_cb)
			comp_cb(dlh, idx, rc);
		if (remote_arg) {
			crt_req_decref(parent_req);
			D_FREE(remote_arg);
		}
	}
	return rc;
}

static void
shard_punch_req_cb(const struct crt_cb_info *cb_info)
{
	crt_rpc_t			*req = cb_info->cci_rpc;
	struct obj_remote_cb_arg	*arg = cb_info->cci_arg;
	crt_rpc_t			*parent_req = arg->parent_req;
	struct obj_punch_out		*opo = crt_reply_get(req);
	struct obj_punch_in		*opi_parent = crt_req_get(req);
	struct dtx_leader_handle	*dlh = arg->dlh;
	int				rc = cb_info->cci_rc;
	int				rc1 = 0;

	if (opi_parent->opi_map_ver < opo->opo_map_version) {
		D_DEBUG(DB_IO, DF_UOID": map_ver stale (%d < %d).\n",
			DP_UOID(opi_parent->opi_oid), opi_parent->opi_map_ver,
			opo->opo_map_version);
		rc1 = -DER_STALE;
	} else {
		rc1 = opo->opo_ret;
	}

	if (rc >= 0)
		rc = rc1;

	if (arg->comp_cb)
		arg->comp_cb(dlh, arg->idx, rc);

	crt_req_decref(parent_req);
	D_FREE(arg);
}

/* Execute punch on the remote target */
int
ds_obj_remote_punch(struct dtx_leader_handle *dlh, void *data, int idx,
		    dtx_sub_comp_cb_t comp_cb)
{
	struct ds_obj_exec_arg		*obj_exec_arg = data;
	struct daos_shard_tgt		*shard_tgt;
	struct obj_remote_cb_arg	*remote_arg;
	struct dtx_handle		*dth = &dlh->dlh_handle;
	struct dtx_sub_status		*sub;
	crt_endpoint_t			 tgt_ep;
	crt_rpc_t			*parent_req = obj_exec_arg->rpc;
	crt_rpc_t			*req;
	struct obj_punch_in		*opi;
	struct obj_punch_in		*opi_parent;
	crt_opcode_t			opc;
	int				rc = 0;

	D_ASSERT(idx < dlh->dlh_sub_cnt);
	sub = &dlh->dlh_subs[idx];
	shard_tgt = &sub->dss_tgt;
	D_ALLOC_PTR(remote_arg);
	if (remote_arg == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	tgt_ep.ep_grp = NULL;
	tgt_ep.ep_rank = shard_tgt->st_rank;
	tgt_ep.ep_tag = shard_tgt->st_tgt_idx;

	remote_arg->dlh = dlh;
	remote_arg->comp_cb = comp_cb;
	remote_arg->idx = idx;
	crt_req_addref(parent_req);
	remote_arg->parent_req = parent_req;

	if (opc_get(parent_req->cr_opc) == DAOS_OBJ_RPC_PUNCH)
		opc = DAOS_OBJ_RPC_TGT_PUNCH;
	else if (opc_get(parent_req->cr_opc) == DAOS_OBJ_RPC_TGT_PUNCH_DKEYS)
		opc = DAOS_OBJ_RPC_TGT_PUNCH_DKEYS;
	else
		opc = DAOS_OBJ_RPC_TGT_PUNCH_AKEYS;

	rc = obj_req_create(dss_get_module_info()->dmi_ctx, &tgt_ep, opc, &req);
	if (rc != 0) {
		D_ERROR("crt_req_create failed, rc "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	opi_parent = crt_req_get(parent_req);
	opi = crt_req_get(req);
	*opi = *opi_parent;
	opi->opi_oid.id_shard = shard_tgt->st_shard_id;
	uuid_copy(opi->opi_co_hdl, opi_parent->opi_co_hdl);
	uuid_copy(opi->opi_co_uuid, opi_parent->opi_co_uuid);
	opi->opi_shard_tgts.ca_count = opi_parent->opi_shard_tgts.ca_count;
	opi->opi_shard_tgts.ca_arrays = opi_parent->opi_shard_tgts.ca_arrays;
	opi->opi_flags |= obj_exec_arg->flags;
	opi->opi_dti_cos.ca_count = dth->dth_dti_cos_count;
	opi->opi_dti_cos.ca_arrays = dth->dth_dti_cos;

	D_DEBUG(DB_TRACE, DF_UOID" forwarding to rank:%d tag:%d.\n",
		DP_UOID(opi->opi_oid), tgt_ep.ep_rank, tgt_ep.ep_tag);

	rc = crt_req_send(req, shard_punch_req_cb, remote_arg);
	if (rc != 0)
		D_ERROR("crt_req_send failed, rc "DF_RC"\n", DP_RC(rc));
	return rc;

out:
	if (rc) {
		sub->dss_result = rc;
		if (comp_cb != NULL)
			comp_cb(dlh, idx, rc);
		if (remote_arg) {
			crt_req_decref(parent_req);
			D_FREE(remote_arg);
		}
	}
	return rc;
}

static void
shard_cpd_req_cb(const struct crt_cb_info *cb_info)
{
	crt_rpc_t			*req = cb_info->cci_rpc;
	struct obj_remote_cb_arg	*arg = cb_info->cci_arg;
	struct obj_cpd_out		*oco = crt_reply_get(req);
	int				rc = cb_info->cci_rc;

	if (rc >= 0)
		rc = oco->oco_ret;

	arg->comp_cb(arg->dlh, arg->idx, rc);
	crt_req_decref(arg->parent_req);
	D_FREE(arg->cpd_reqs);
	D_FREE(arg->cpd_desc);
	D_FREE(arg->cpd_head);
	D_FREE(arg->cpd_dcsr);
	D_FREE(arg->cpd_dcde);
	D_FREE(arg);
}

static int
ds_obj_cpd_clone_reqs(struct dtx_leader_handle *dlh, struct daos_shard_tgt *tgt,
		      struct daos_cpd_disp_ent *dcde_parent,
		      struct daos_cpd_sub_req *dcsr_parent, int total,
		      struct daos_cpd_disp_ent **p_dcde,
		      struct daos_cpd_sub_req **p_dcsr)
{
	struct daos_cpd_disp_ent	*dcde = NULL;
	struct daos_cpd_sub_req		*dcsr = NULL;
	int				 count;
	int				 rc = 0;
	int				 i;

	count = dcde_parent->dcde_read_cnt + dcde_parent->dcde_write_cnt;
	D_ALLOC_ARRAY(dcsr, count);
	if (dcsr == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC(dcde, sizeof(*dcde) +
		sizeof(struct daos_cpd_req_idx) * count);
	if (dcde == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dcde->dcde_reqs = (struct daos_cpd_req_idx *)(dcde + 1);
	dcde->dcde_read_cnt = dcde_parent->dcde_read_cnt;
	dcde->dcde_write_cnt = dcde_parent->dcde_write_cnt;

	for (i = 0; i < count; i++) {
		struct daos_cpd_req_idx		*dcri_parent;
		int				 idx;

		dcri_parent = &dcde_parent->dcde_reqs[i];
		idx = dcri_parent->dcri_req_idx;
		D_ASSERT(idx < total);

		memcpy(&dcsr[i], &dcsr_parent[idx], sizeof(dcsr[i]));

		if (dcsr_parent[idx].dcsr_opc == DCSO_UPDATE) {
			struct daos_cpd_update	*dcu_parent;
			struct daos_cpd_update	*dcu;
			struct obj_ec_split_req	*split;

			dcu_parent = &dcsr_parent[idx].dcsr_update;
			dcu = &dcsr[i].dcsr_update;

			/* For non-leader, does not need split EC sub-req. */
			dcu->dcu_ec_tgts = NULL;
			dcu->dcu_ec_split_req = NULL;
			dcsr[i].dcsr_ec_tgt_nr = 0;

			split = dcu_parent->dcu_ec_split_req;
			if (split != NULL) {
				struct obj_tgt_oiod	*oiod;

				oiod = obj_ec_tgt_oiod_get(split->osr_tgt_oiods,
						dcsr_parent[idx].dcsr_ec_tgt_nr,
						dcri_parent->dcri_shard_off -
						dcu_parent->dcu_start_shard);
				D_ASSERT(oiod != NULL);

				dcu->dcu_iod_array.oia_oiods = oiod->oto_oiods;
				dcu->dcu_iod_array.oia_oiod_nr =
					dcu_parent->dcu_iod_array.oia_iod_nr;
				dcu->dcu_iod_array.oia_offs = oiod->oto_offs;
			}
		}

		dcde->dcde_reqs[i].dcri_shard_off = dcri_parent->dcri_shard_off;
		dcde->dcde_reqs[i].dcri_shard_id = dcri_parent->dcri_shard_id;
		dcde->dcde_reqs[i].dcri_req_idx = i;
		dcde->dcde_reqs[i].dcri_padding = dcri_parent->dcri_padding;
	}

out:
	if (rc != 0) {
		D_FREE(dcde);
		D_FREE(dcsr);
	} else {
		*p_dcde = dcde;
		*p_dcsr = dcsr;
	}

	return rc;
}

/* Dispatch CPD RPC and handle sub requests remotely */
int
ds_obj_cpd_dispatch(struct dtx_leader_handle *dlh, void *arg, int idx,
		    dtx_sub_comp_cb_t comp_cb)
{
	struct ds_obj_exec_arg		*exec_arg = arg;
	struct daos_cpd_args		*dca = exec_arg->args;
	struct daos_cpd_sub_head	*dcsh;
	struct daos_cpd_disp_ent	*dcde_parent = NULL;
	struct daos_cpd_disp_ent	*dcde = NULL;
	struct daos_cpd_sub_req		*dcsr_parent = NULL;
	struct daos_cpd_sub_req		*dcsr = NULL;
	struct obj_cpd_in		*oci;
	struct obj_cpd_in		*oci_parent;
	struct obj_remote_cb_arg	*remote_arg = NULL;
	struct daos_shard_tgt		*shard_tgt;
	crt_rpc_t			*parent_req = exec_arg->rpc;
	crt_rpc_t			*req = NULL;
	struct dtx_sub_status		*sub;
	crt_endpoint_t			 tgt_ep;
	struct daos_cpd_sg		*head_dcs = NULL;
	struct daos_cpd_sg		*dcsr_dcs = NULL;
	struct daos_cpd_sg		*dcde_dcs = NULL;
	int				 total;
	int				 count;
	int				 rc = 0;

	D_ASSERT(idx < dlh->dlh_sub_cnt);

	sub = &dlh->dlh_subs[idx];
	shard_tgt = &sub->dss_tgt;

	D_ALLOC_PTR(head_dcs);
	if (head_dcs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC_PTR(dcsr_dcs);
	if (dcsr_dcs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC_PTR(dcde_dcs);
	if (dcde_dcs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC_PTR(remote_arg);
	if (remote_arg == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	remote_arg->dlh = dlh;
	remote_arg->comp_cb = comp_cb;
	remote_arg->idx = idx;
	crt_req_addref(parent_req);
	remote_arg->parent_req = parent_req;

	remote_arg->cpd_head = head_dcs;
	remote_arg->cpd_dcsr = dcsr_dcs;
	remote_arg->cpd_dcde = dcde_dcs;

	tgt_ep.ep_grp = NULL;
	tgt_ep.ep_rank = shard_tgt->st_rank;
	tgt_ep.ep_tag = shard_tgt->st_tgt_idx;

	rc = obj_req_create(dss_get_module_info()->dmi_ctx, &tgt_ep,
			    DAOS_OBJ_RPC_CPD, &req);
	if (rc != 0) {
		D_ERROR("CPD crt_req_create failed, idx %u: "DF_RC"\n",
			idx, DP_RC(rc));
		D_GOTO(out, rc);
	}

	oci_parent = crt_req_get(parent_req);
	oci = crt_req_get(req);

	uuid_copy(oci->oci_pool_uuid, oci_parent->oci_pool_uuid);
	uuid_copy(oci->oci_co_hdl, oci_parent->oci_co_hdl);
	uuid_copy(oci->oci_co_uuid, oci_parent->oci_co_uuid);
	oci->oci_map_ver = oci_parent->oci_map_ver;
	oci->oci_flags = (oci_parent->oci_flags | exec_arg->flags) &
			~(ORF_HAS_EC_SPLIT | ORF_CPD_LEADER);

	oci->oci_disp_tgts.ca_arrays = NULL;
	oci->oci_disp_tgts.ca_count = 0;

	dcsh = ds_obj_cpd_get_dcsh(dca->dca_rpc, dca->dca_idx);
	head_dcs->dcs_type = DCST_HEAD;
	head_dcs->dcs_nr = 1;
	head_dcs->dcs_buf = dcsh;
	oci->oci_sub_heads.ca_arrays = head_dcs;
	oci->oci_sub_heads.ca_count = 1;

	dcsr_parent = ds_obj_cpd_get_dcsr(dca->dca_rpc, dca->dca_idx);
	total = ds_obj_cpd_get_dcsr_cnt(dca->dca_rpc, dca->dca_idx);

	/* IDX[0] is for leader. */
	dcde_parent = ds_obj_cpd_get_dcde(dca->dca_rpc, dca->dca_idx, idx + 1);
	if (dcde_parent == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	count = dcde_parent->dcde_read_cnt + dcde_parent->dcde_write_cnt;
	if (count < total || (exec_arg->flags & ORF_HAS_EC_SPLIT)) {
		rc = ds_obj_cpd_clone_reqs(dlh, shard_tgt, dcde_parent,
					   dcsr_parent, total, &dcde, &dcsr);
		if (rc != 0)
			D_GOTO(out, rc);

		remote_arg->cpd_reqs = dcsr;
		remote_arg->cpd_desc = dcde;
	} else {
		dcsr = dcsr_parent;
		dcde = dcde_parent;
	}

	dcsr_dcs->dcs_type = DCST_REQ_SRV;
	dcsr_dcs->dcs_nr = count;
	dcsr_dcs->dcs_buf = dcsr;
	oci->oci_sub_reqs.ca_arrays = dcsr_dcs;
	oci->oci_sub_reqs.ca_count = 1;

	dcde_dcs->dcs_type = DCST_DISP;
	dcde_dcs->dcs_nr = 1;
	dcde_dcs->dcs_buf = dcde;
	oci->oci_disp_ents.ca_arrays = dcde_dcs;
	oci->oci_disp_ents.ca_count = 1;

	D_DEBUG(DB_TRACE, "Forwarding CPD RPC to rank:%d tag:%d idx %u for DXT "
		DF_DTI"\n",
		tgt_ep.ep_rank, tgt_ep.ep_tag, idx, DP_DTI(&dcsh->dcsh_xid));

	rc = crt_req_send(req, shard_cpd_req_cb, remote_arg);
	if (rc != 0)
		D_ERROR("crt_req_send failed, rc "DF_RC"\n", DP_RC(rc));

	D_CDEBUG(rc != 0, DLOG_ERR, DB_TRACE,
		 "Forwarded CPD RPC to rank:%d tag:%d idx %u for DXT "
		 DF_DTI": "DF_RC"\n", tgt_ep.ep_rank, tgt_ep.ep_tag, idx,
		 DP_DTI(&dcsh->dcsh_xid), DP_RC(rc));

	return rc;
out:
	if (req != NULL)
		crt_req_decref(req);

	comp_cb(dlh, idx, rc);

	if (remote_arg != NULL) {
		crt_req_decref(parent_req);
		D_FREE(remote_arg);
	}

	if (dcsr != dcsr_parent)
		D_FREE(dcsr);
	if (dcde != dcde_parent)
		D_FREE(dcde);

	D_FREE(head_dcs);
	D_FREE(dcsr_dcs);
	D_FREE(dcde_dcs);

	return rc;
}
