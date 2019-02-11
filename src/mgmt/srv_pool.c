/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
/*
 * ds_mgmt: Pool Methods
 */
#define D_LOGFAC	DD_FAC(mgmt)

#include "srv_internal.h"

#include <daos_srv/pool.h>

static int
ds_mgmt_tgt_pool_destroy(uuid_t pool_uuid, crt_group_t *grp)
{
	crt_rpc_t			*td_req;
	struct mgmt_tgt_destroy_in	*td_in;
	struct mgmt_tgt_destroy_out	*td_out;
	unsigned int			opc;
	int				topo;
	int				rc;

	/* Collective RPC to destroy the pool on all of targets */
	topo = crt_tree_topo(CRT_TREE_KNOMIAL, 4);
	opc = DAOS_RPC_OPCODE(MGMT_TGT_DESTROY, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_corpc_req_create(dss_get_module_info()->dmi_ctx, grp, NULL,
				  opc, NULL, NULL, 0, topo, &td_req);
	if (rc)
		return rc;

	td_in = crt_req_get(td_req);
	D_ASSERT(td_in != NULL);
	uuid_copy(td_in->td_pool_uuid, pool_uuid);

	rc = dss_rpc_send(td_req);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	td_out = crt_reply_get(td_req);
	rc = td_out->td_rc;
	if (rc != 0)
		D_ERROR(DF_UUID": failed to update pool map on %d targets\n",
			DP_UUID(pool_uuid), rc);
out_rpc:
	crt_req_decref(td_req);

	return rc;
}

static int
ds_mgmt_pool_svc_create(uuid_t pool_uuid, unsigned int uid, unsigned int gid,
			unsigned int mode, int ntargets, uuid_t target_uuids[],
			const char *group, d_rank_list_t *ranks,
			daos_prop_t *prop, d_rank_list_t *svc_list)
{
	int	doms[ntargets];
	int	rc;
	int	i;

	D_DEBUG(DB_MGMT, DF_UUID": all tgts created, setting up pool "
		"svc\n", DP_UUID(pool_uuid));

	for (i = 0; i < ntargets; i++)
		doms[i] = 1;

	/**
	 * TODO: fetch domain list from external source
	 * Report 1 domain per target for now
	 */
	rc = ds_pool_svc_create(pool_uuid, uid, gid, mode, ranks->rl_nr,
				target_uuids, group, ranks, ARRAY_SIZE(doms),
				doms, prop, svc_list);

	return rc;
}

#define TMP_RANKS_ARRAY_SIZE	32
void
ds_mgmt_hdlr_pool_create(crt_rpc_t *rpc_req)
{
	struct mgmt_pool_create_in	*pc_in;
	struct mgmt_pool_create_out	*pc_out;
	crt_rpc_t			*tc_req;
	crt_opcode_t			opc;
	struct mgmt_tgt_create_in	*tc_in;
	struct mgmt_tgt_create_out	*tc_out;
	d_rank_t			*tc_out_ranks;
	uuid_t				*tc_out_uuids;
	crt_group_t			*grp = NULL;
	char				id[DAOS_UUID_STR_SIZE];
	d_rank_list_t		*rank_list;
	d_rank_list_t		tmp_rank_list = {0};
	d_rank_t			ranks_array[TMP_RANKS_ARRAY_SIZE];
	unsigned int			ranks_size;
	uuid_t				*tgt_uuids = NULL;
	unsigned int			i;
	int				topo;
	int				rc;

	pc_in = crt_req_get(rpc_req);
	D_ASSERT(pc_in != NULL);
	pc_out = crt_reply_get(rpc_req);
	D_ASSERT(pc_out != NULL);

	if (pc_in->pc_tgts) {
		daos_rank_list_sort(pc_in->pc_tgts);
		rank_list = pc_in->pc_tgts;
		ranks_size = pc_in->pc_tgts->rl_nr;
	} else {
		rc = crt_group_size(NULL, &ranks_size);
		D_ASSERT(rc == 0);

		tmp_rank_list.rl_nr = ranks_size;
		if (ranks_size > TMP_RANKS_ARRAY_SIZE) {
			d_rank_t *ranks;

			D_ALLOC_ARRAY(ranks, ranks_size);
			if (ranks == NULL)
				D_GOTO(free, rc = -DER_NOMEM);
			tmp_rank_list.rl_ranks = ranks;
		} else {
			tmp_rank_list.rl_ranks = ranks_array;
		}

		for (i = 0; i < ranks_size; i++)
			tmp_rank_list.rl_ranks[i] = i;

		rank_list = &tmp_rank_list;
	}

	/* Collective RPC to all of targets of the pool */
	uuid_unparse_lower(pc_in->pc_pool_uuid, id);
	rc = dss_group_create(id, rank_list, &grp);
	if (rc != 0)
		D_GOTO(free, rc);

	topo = crt_tree_topo(CRT_TREE_KNOMIAL, 4);
	opc = DAOS_RPC_OPCODE(MGMT_TGT_CREATE, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_corpc_req_create(dss_get_module_info()->dmi_ctx, grp, NULL,
				  opc, NULL, NULL, 0, topo, &tc_req);
	if (rc)
		D_GOTO(free, rc);

	tc_in = crt_req_get(tc_req);
	D_ASSERT(tc_in != NULL);
	uuid_copy(tc_in->tc_pool_uuid, pc_in->pc_pool_uuid);

	/* the pc_in->pc_tgt_dev will be freed when the MGMT_POOL_CREATE
	 * finishes, it is after TGT_CREATE RPC handling so it is safe
	 * to directly use it here.
	 */
	tc_in->tc_tgt_dev = pc_in->pc_tgt_dev;
	tc_in->tc_scm_size = pc_in->pc_scm_size;
	tc_in->tc_nvme_size = pc_in->pc_nvme_size;
	rc = dss_rpc_send(tc_req);
	if (rc != 0) {
		crt_req_decref(tc_req);
		D_GOTO(free, rc);
	}

	tc_out = crt_reply_get(tc_req);
	rc = tc_out->tc_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to update pool map on %d targets\n",
			DP_UUID(tc_in->tc_pool_uuid), rc);
		crt_req_decref(tc_req);
		D_GOTO(tgt_pool_create_fail, rc);
	}

	D_DEBUG(DB_MGMT, DF_UUID" create %zu tgts pool\n",
		DP_UUID(pc_in->pc_pool_uuid), tc_out->tc_tgt_uuids.ca_count);

	/** Gather target uuids ranks from collective RPC to start pool svc. */
	D_ALLOC_ARRAY(tgt_uuids, ranks_size);
	if (tgt_uuids == NULL)
		D_GOTO(free, rc = -DER_NOMEM);
	tc_out_ranks = tc_out->tc_ranks.ca_arrays;
	tc_out_uuids = tc_out->tc_tgt_uuids.ca_arrays;
	for (i = 0; i < tc_out->tc_tgt_uuids.ca_count; i++) {
		int	idx;
		bool	found;

		found = daos_rank_list_find(rank_list, tc_out_ranks[i], &idx);
		D_ASSERT(found);

		/** copy returned target UUID */
		uuid_copy(tgt_uuids[idx], tc_out_uuids[i]);

		D_DEBUG(DB_TRACE, "fill ranks %d idx %d "DF_UUID"\n",
			tc_out_ranks[i], idx, DP_UUID(tc_out_uuids[i]));
	}

	crt_req_decref(tc_req);
	/* Since the pool_svc will create another group,
	 * let's destroy this group
	 **/
	dss_group_destroy(grp);
	grp = NULL;

	/** allocate service rank list */
	D_ALLOC_PTR(pc_out->pc_svc);
	if (pc_out->pc_svc == NULL)
		D_GOTO(tgt_pool_create_fail, rc = -DER_NOMEM);

	D_ALLOC_ARRAY(pc_out->pc_svc->rl_ranks,
		pc_in->pc_svc_nr);
	if (pc_out->pc_svc->rl_ranks == NULL)
		D_GOTO(tgt_pool_create_fail, rc = -DER_NOMEM);
	pc_out->pc_svc->rl_nr = pc_in->pc_svc_nr;

	rc = ds_mgmt_pool_svc_create(pc_in->pc_pool_uuid, pc_in->pc_uid,
				     pc_in->pc_gid, pc_in->pc_mode,
				     ranks_size, tgt_uuids, pc_in->pc_grp,
				     rank_list, pc_in->pc_prop, pc_out->pc_svc);
	if (rc)
		D_ERROR("create pool "DF_UUID" svc failed: rc %d\n",
			DP_UUID(pc_in->pc_pool_uuid), rc);

tgt_pool_create_fail:
	if (rc)
		ds_mgmt_tgt_pool_destroy(pc_in->pc_pool_uuid, grp);
free:
	if (tmp_rank_list.rl_ranks != NULL &&
	    tmp_rank_list.rl_ranks != ranks_array)
		D_FREE(tmp_rank_list.rl_ranks);

	if (tgt_uuids != NULL)
		D_FREE(tgt_uuids);

	if (grp != NULL)
		dss_group_destroy(grp);

	pc_out->pc_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send failed, rc: %d "
			"(pc_tgt_dev: %s).\n", rc, pc_in->pc_tgt_dev);
}

void
ds_mgmt_hdlr_pool_destroy(crt_rpc_t *rpc_req)
{
	struct mgmt_pool_destroy_in	*pd_in;
	struct mgmt_pool_destroy_out	*pd_out;
	int				rc;

	pd_in = crt_req_get(rpc_req);
	pd_out = crt_reply_get(rpc_req);
	D_ASSERT(pd_in != NULL && pd_out != NULL);

	/* TODO check metadata about the pool's existence?
	 *      and check active pool connection for "force"
	 */
	D_DEBUG(DB_MGMT, "Destroying pool "DF_UUID"\n",
		DP_UUID(pd_in->pd_pool_uuid));

	rc = ds_pool_svc_destroy(pd_in->pd_pool_uuid);
	if (rc != 0) {
		D_ERROR("Failed to destroy pool service "DF_UUID": %d\n",
			DP_UUID(pd_in->pd_pool_uuid), rc);
		D_GOTO(out, rc);
	}

	rc = ds_mgmt_tgt_pool_destroy(pd_in->pd_pool_uuid, NULL);
	if (rc == 0)
		D_DEBUG(DB_MGMT, "Destroying pool "DF_UUID" succeed.\n",
			DP_UUID(pd_in->pd_pool_uuid));
	else
		D_ERROR("Destroying pool "DF_UUID" failed, rc: %d.\n",
			DP_UUID(pd_in->pd_pool_uuid), rc);
out:
	pd_out->pd_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send failed, rc: %d.\n", rc);
}
