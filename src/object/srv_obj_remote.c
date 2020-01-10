/**
 * (C) Copyright 2019 Intel Corporation.
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
#include <daos_srv/daos_server.h>
#include <daos_srv/dtx_srv.h>
#include "obj_rpc.h"
#include "obj_internal.h"

struct obj_remote_cb_arg {
	dtx_sub_comp_cb_t		comp_cb;
	crt_rpc_t			*parent_req;
	struct dtx_leader_handle	*dlh;
	int				idx;
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
	struct dtx_sub_status		*sub = &dlh->dlh_subs[arg->idx];
	int				rc = cb_info->cci_rc;
	int				rc1 = 0;

	if (orw_parent->orw_map_ver < orwo->orw_map_version) {
		D_DEBUG(DB_IO, DF_UOID": map_ver stale (%d < %d).\n",
			DP_UOID(orw_parent->orw_oid), orw_parent->orw_map_ver,
			orwo->orw_map_version);
		rc1 = -DER_STALE;
	} else {
		rc1 = orwo->orw_ret;
		if (rc1 == -DER_INPROGRESS) {
			daos_dti_copy(&sub->dss_dce.dce_xid,
				      &orwo->orw_dti_conflict);
			sub->dss_dce.dce_dkey = orwo->orw_dkey_conflict;
		}
	}

	if (rc >= 0)
		rc = rc1;

	if (arg->comp_cb)
		arg->comp_cb(dlh, arg->idx, rc);

	crt_req_decref(parent_req);
	D_FREE_PTR(arg);
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
					       tgt_idx);
		D_ASSERT(tgt_oiod != NULL);
		orw->orw_iod_array.oia_oiods = tgt_oiod->oto_oiods;
		orw->orw_iod_array.oia_oiod_nr = orw->orw_iod_array.oia_iod_nr;
		orw->orw_iod_array.oia_offs = tgt_oiod->oto_offs;
	}
	orw->orw_oid.id_shard = shard_tgt->st_shard;
	uuid_copy(orw->orw_co_hdl, orw_parent->orw_co_hdl);
	uuid_copy(orw->orw_co_uuid, orw_parent->orw_co_uuid);
	orw->orw_shard_tgts.ca_count	= 0;
	orw->orw_shard_tgts.ca_arrays	= NULL;
	orw->orw_flags |= ORF_BULK_BIND | obj_exec_arg->flags;
	orw->orw_dti_cos.ca_count	= dth->dth_dti_cos_count;
	orw->orw_dti_cos.ca_arrays	= dth->dth_dti_cos;

	D_DEBUG(DB_TRACE, DF_UOID" forwarding to rank:%d tag:%d.\n",
		DP_UOID(orw->orw_oid), tgt_ep.ep_rank, tgt_ep.ep_tag);
	rc = crt_req_send(req, shard_update_req_cb, remote_arg);
	if (rc != 0) {
		D_ERROR("crt_req_send failed, rc "DF_RC"\n", DP_RC(rc));
		crt_req_decref(req);
		D_GOTO(out, rc);
	}

out:
	if (rc) {
		sub->dss_result = rc;
		if (comp_cb)
			comp_cb(dlh, idx, rc);
		if (remote_arg) {
			if (remote_arg->parent_req)
				crt_req_decref(remote_arg->parent_req);
			D_FREE_PTR(remote_arg);
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
	struct dtx_sub_status		*sub = &dlh->dlh_subs[arg->idx];
	int				rc = cb_info->cci_rc;
	int				rc1 = 0;

	if (opi_parent->opi_map_ver < opo->opo_map_version) {
		D_DEBUG(DB_IO, DF_UOID": map_ver stale (%d < %d).\n",
			DP_UOID(opi_parent->opi_oid), opi_parent->opi_map_ver,
			opo->opo_map_version);
		rc1 = -DER_STALE;
	} else {
		rc1 = opo->opo_ret;
		if (rc1 == -DER_INPROGRESS) {
			daos_dti_copy(&sub->dss_dce.dce_xid,
				      &opo->opo_dti_conflict);
			sub->dss_dce.dce_dkey = opo->opo_dkey_conflict;
		}
	}

	if (rc >= 0)
		rc = rc1;

	if (arg->comp_cb)
		arg->comp_cb(dlh, arg->idx, rc);

	crt_req_decref(parent_req);
	D_FREE_PTR(arg);
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
	opi->opi_oid.id_shard = shard_tgt->st_shard;
	uuid_copy(opi->opi_co_hdl, opi_parent->opi_co_hdl);
	uuid_copy(opi->opi_co_uuid, opi_parent->opi_co_uuid);
	opi->opi_shard_tgts.ca_count = 0;
	opi->opi_shard_tgts.ca_arrays = NULL;
	opi->opi_flags |= obj_exec_arg->flags;
	opi->opi_dti_cos.ca_count = dth->dth_dti_cos_count;
	opi->opi_dti_cos.ca_arrays = dth->dth_dti_cos;

	D_DEBUG(DB_TRACE, DF_UOID" forwarding to rank:%d tag:%d.\n",
		DP_UOID(opi->opi_oid), tgt_ep.ep_rank, tgt_ep.ep_tag);

	rc = crt_req_send(req, shard_punch_req_cb, remote_arg);
	if (rc != 0) {
		D_ERROR("crt_req_send failed, rc "DF_RC"\n", DP_RC(rc));
		crt_req_decref(req);
		D_GOTO(out, rc);
	}

out:
	if (rc) {
		sub->dss_result = rc;
		if (comp_cb != NULL)
			comp_cb(dlh, idx, rc);
		if (remote_arg) {
			if (remote_arg->parent_req)
				crt_req_decref(remote_arg->parent_req);
			D_FREE_PTR(remote_arg);
		}
	}
	return rc;
}
