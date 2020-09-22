/*
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * \file
 *
 * ds_mgmt: Pool Methods
 */

#define D_LOGFAC	DD_FAC(mgmt)

#include <daos_srv/pool.h>
#include <daos/rpc.h>

#include "srv_internal.h"

/**
 * Destroy the pool on the specified ranks.
 * If filter_invert == false: destroy on all ranks EXCEPT those in filter_ranks.
 * If filter_invert == true:  destroy on all ranks specified in filter_ranks.
 */
static int
ds_mgmt_tgt_pool_destroy_ranks(uuid_t pool_uuid,
			       d_rank_list_t *filter_ranks, bool filter_invert)
{
	crt_rpc_t			*td_req;
	struct mgmt_tgt_destroy_in	*td_in;
	struct mgmt_tgt_destroy_out	*td_out;
	unsigned int			opc;
	int				topo;
	uint32_t			flags;
	int				rc;

	/* Collective RPC to destroy the pool on all of targets */
	flags = filter_invert ? CRT_RPC_FLAG_FILTER_INVERT : 0;
	topo = crt_tree_topo(CRT_TREE_KNOMIAL, 4);
	opc = DAOS_RPC_OPCODE(MGMT_TGT_DESTROY, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_corpc_req_create(dss_get_module_info()->dmi_ctx, NULL,
				  filter_ranks, opc, NULL, NULL, flags, topo,
				  &td_req);
	if (rc)
		D_GOTO(fini_ranks, rc);

	td_in = crt_req_get(td_req);
	D_ASSERT(td_in != NULL);
	uuid_copy(td_in->td_pool_uuid, pool_uuid);

	rc = dss_rpc_send(td_req);
	if (rc == 0 && DAOS_FAIL_CHECK(DAOS_POOL_DESTROY_FAIL_CORPC))
		rc = -DER_TIMEDOUT;
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	td_out = crt_reply_get(td_req);
	rc = td_out->td_rc;
	if (rc != 0)
		D_ERROR(DF_UUID": failed to destroy pool targets "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
out_rpc:
	crt_req_decref(td_req);

fini_ranks:
	return rc;
}

/**
 * Destroy the pool on every DOWN rank
 */
static int
ds_mgmt_tgt_pool_destroy(uuid_t pool_uuid)
{
	d_rank_list_t			excluded = { 0 };
	int				rc;

	rc = ds_pool_get_ranks(pool_uuid, MAP_RANKS_DOWN, &excluded);
	if (rc)
		return rc;

	rc = ds_mgmt_tgt_pool_destroy_ranks(pool_uuid, &excluded, false);
	if (rc)
		D_GOTO(fini_ranks, rc);
fini_ranks:
	map_ranks_fini(&excluded);
	return rc;
}

static int
ds_mgmt_tgt_pool_create_ranks(uuid_t pool_uuid, char *tgt_dev,
			      d_rank_list_t *rank_list, size_t scm_size,
			      size_t nvme_size, uuid_t **tgt_uuids)
{
	crt_rpc_t			*tc_req;
	crt_opcode_t			opc;
	struct mgmt_tgt_create_in	*tc_in;
	struct mgmt_tgt_create_out	*tc_out;
	d_rank_t			*tc_out_ranks;
	uuid_t				*tc_out_uuids;
	unsigned int			i;
	int				topo;
	int				rc;
	int				rc_cleanup;

	/* Collective RPC to all of targets of the pool */
	topo = crt_tree_topo(CRT_TREE_KNOMIAL, 4);
	opc = DAOS_RPC_OPCODE(MGMT_TGT_CREATE, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_corpc_req_create(dss_get_module_info()->dmi_ctx, NULL,
				  rank_list, opc, NULL, NULL,
				  CRT_RPC_FLAG_FILTER_INVERT, topo, &tc_req);
	if (rc) {
		D_ERROR(DF_UUID": corpc_req_create failed: rc="DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		return rc;
	}

	tc_in = crt_req_get(tc_req);
	D_ASSERT(tc_in != NULL);
	uuid_copy(tc_in->tc_pool_uuid, pool_uuid);
	tc_in->tc_tgt_dev = tgt_dev;
	tc_in->tc_scm_size = scm_size;
	tc_in->tc_nvme_size = nvme_size;
	rc = dss_rpc_send(tc_req);
	if (rc == 0 && DAOS_FAIL_CHECK(DAOS_POOL_CREATE_FAIL_CORPC))
		rc = -DER_TIMEDOUT;
	if (rc != 0) {
		D_ERROR(DF_UUID": dss_rpc_send MGMT_TGT_CREATE: rc="DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		D_GOTO(decref, rc);
	}

	tc_out = crt_reply_get(tc_req);
	rc = tc_out->tc_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to update pool map on targets: rc="
			DF_RC"\n",
			DP_UUID(tc_in->tc_pool_uuid), DP_RC(rc));
		D_GOTO(decref, rc);
	}

	D_DEBUG(DB_MGMT, DF_UUID" create %zu tgts pool\n",
		DP_UUID(pool_uuid), tc_out->tc_tgt_uuids.ca_count);

	/* Abort early if the caller doesn't need the new pool target UUIDs */
	if (tgt_uuids == NULL)
		D_GOTO(decref, rc = DER_SUCCESS);

	/* Gather target uuids ranks from collective RPC to start pool svc. */
	D_ALLOC_ARRAY(*tgt_uuids, rank_list->rl_nr);
	if (*tgt_uuids == NULL) {
		rc = -DER_NOMEM;
		D_GOTO(decref, rc);
	}
	tc_out_ranks = tc_out->tc_ranks.ca_arrays;
	tc_out_uuids = tc_out->tc_tgt_uuids.ca_arrays;
	for (i = 0; i < tc_out->tc_tgt_uuids.ca_count; i++) {
		int	idx;
		bool	found;

		found = daos_rank_list_find(rank_list, tc_out_ranks[i], &idx);
		D_ASSERT(found);

		/* copy returned target UUID */
		uuid_copy((*tgt_uuids)[idx], tc_out_uuids[i]);

		D_DEBUG(DB_TRACE, "fill ranks %d idx %d "DF_UUID"\n",
			tc_out_ranks[i], idx, DP_UUID(tc_out_uuids[i]));
	}

	rc = DER_SUCCESS;

decref:
	crt_req_decref(tc_req);
	if (rc) {
		rc_cleanup = ds_mgmt_tgt_pool_destroy_ranks(pool_uuid,
							    rank_list, true);
		if (rc_cleanup)
			D_ERROR(DF_UUID": failed to clean up failed pool: "
				DF_RC"\n", DP_UUID(pool_uuid), DP_RC(rc));
	}

	return rc;
}

static int
ds_mgmt_pool_svc_create(uuid_t pool_uuid,
			int ntargets, uuid_t target_uuids[],
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
	rc = ds_pool_svc_create(pool_uuid, ranks->rl_nr,
				target_uuids, group, ranks, ARRAY_SIZE(doms),
				doms, prop, svc_list);

	return rc;
}

int
ds_mgmt_create_pool(uuid_t pool_uuid, const char *group, char *tgt_dev,
		    d_rank_list_t *targets, size_t scm_size, size_t nvme_size,
		    daos_prop_t *prop, uint32_t svc_nr, d_rank_list_t **svcp)
{
	uuid_t				*tgt_uuids = NULL;
	d_rank_list_t			*filtered_targets = NULL;
	d_rank_list_t			*pg_ranks = NULL;
	uint32_t			pg_size;
	int				rc;
	int				rc_cleanup;

	/* Sanity check targets versus cart's current primary group members.
	 * If any targets not in PG, flag error before MGMT_TGT_ corpcs fail.
	 */
	rc = crt_group_size(NULL, &pg_size);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	pg_ranks = d_rank_list_alloc(pg_size);
	if (pg_ranks == NULL) {
		rc = -DER_NOMEM;
		D_GOTO(out, rc);
	}
	rc = d_rank_list_dup(&filtered_targets, targets);
	if (rc) {
		rc = -DER_NOMEM;
		D_GOTO(out, rc);
	}
	/* Remove any targets not found in pg_ranks */
	d_rank_list_filter(pg_ranks, filtered_targets, false /* exclude */);
	if (!d_rank_list_identical(filtered_targets, targets)) {
		D_ERROR("some ranks not found in cart primary group\n");
		D_GOTO(out, rc = -DER_OOG);
	}

	rc = ds_mgmt_tgt_pool_create_ranks(pool_uuid, tgt_dev, targets,
					   scm_size, nvme_size, &tgt_uuids);
	if (rc != 0) {
		D_ERROR("creating pool "DF_UUID" on ranks failed: rc "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	/** allocate service rank list */
	*svcp = d_rank_list_alloc(svc_nr);
	if (*svcp == NULL) {
		rc = -DER_NOMEM;
		goto out_uuids;
	}

	rc = ds_mgmt_pool_svc_create(pool_uuid, targets->rl_nr, tgt_uuids,
				     group, targets, prop, *svcp);
	if (rc) {
		D_ERROR("create pool "DF_UUID" svc failed: rc "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out_svcp;
	}

out_svcp:
	if (rc) {
		d_rank_list_free(*svcp);
		*svcp = NULL;

		rc_cleanup = ds_mgmt_tgt_pool_destroy_ranks(pool_uuid,
							    targets, true);
		if (rc_cleanup)
			D_ERROR(DF_UUID": failed to clean up failed pool: "
				DF_RC"\n", DP_UUID(pool_uuid), DP_RC(rc));
	}
out_uuids:
	D_FREE(tgt_uuids);
out:
	d_rank_list_free(filtered_targets);
	d_rank_list_free(pg_ranks);
	D_DEBUG(DB_MGMT, "create pool "DF_UUID": "DF_RC"\n", DP_UUID(pool_uuid),
		DP_RC(rc));
	return rc;
}

int
ds_mgmt_destroy_pool(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		     const char *group, uint32_t force)
{
	int		rc;

	D_DEBUG(DB_MGMT, "Destroying pool "DF_UUID"\n", DP_UUID(pool_uuid));

	if (svc_ranks == NULL) {
		D_ERROR("svc_ranks was NULL\n");
		return -DER_INVAL;
	}

	/* Check active pool connections, evict only if force */
	rc = ds_pool_svc_check_evict(pool_uuid, svc_ranks, force);
	if (rc != 0) {
		D_ERROR("Failed to check/evict pool handles "DF_UUID" rc: %d\n",
			DP_UUID(pool_uuid), rc);
		goto out;
	}

	rc = ds_pool_svc_destroy(pool_uuid);
	if (rc != 0) {
		D_ERROR("Failed to destroy pool service "DF_UUID": "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out;
	}

	rc = ds_mgmt_tgt_pool_destroy(pool_uuid);
	if (rc != 0) {
		D_ERROR("Destroying pool "DF_UUID" failed, rc: "DF_RC".\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out;
	}

	D_DEBUG(DB_MGMT, "Destroying pool "DF_UUID" succeed.\n",
		DP_UUID(pool_uuid));
out:
	return rc;
}

int
ds_mgmt_pool_extend(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		    d_rank_list_t *rank_list,
		    char *tgt_dev,  size_t scm_size, size_t nvme_size)
{
	d_rank_list_t			*unique_add_ranks = NULL;
	uuid_t				*tgt_uuids = NULL;
	int				doms[rank_list->rl_nr];
	int				ntargets;
	int				i;
	int				rc;

	D_DEBUG(DB_MGMT, "extend pool "DF_UUID"\n", DP_UUID(pool_uuid));

	rc = d_rank_list_dup_sort_uniq(&unique_add_ranks, rank_list);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = ds_mgmt_tgt_pool_create_ranks(pool_uuid, tgt_dev, rank_list,
					   scm_size, nvme_size, &tgt_uuids);
	if (rc != 0) {
		D_ERROR("creating pool on ranks "DF_UUID" failed: rc "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* TODO: Need to make pool service aware of new rank UUIDs */

	ntargets = rank_list->rl_nr;
	for (i = 0; i < ntargets; ++i)
		doms[i] = 1;

	rc = ds_pool_extend(pool_uuid, ntargets, tgt_uuids, rank_list,
			    ARRAY_SIZE(doms), doms, svc_ranks);

out:
	if (unique_add_ranks != NULL)
		d_rank_list_free(unique_add_ranks);

	return rc;
}

int
ds_mgmt_evict_pool(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		   const char *group)
{
	int		 rc;

	D_DEBUG(DB_MGMT, "evict pool "DF_UUID"\n", DP_UUID(pool_uuid));

	/* Evict active pool connections if they exist*/
	rc = ds_pool_svc_check_evict(pool_uuid, svc_ranks, true);
	if (rc != 0) {
		D_ERROR("Failed to evict pool handles"DF_UUID" rc: %d\n",
			DP_UUID(pool_uuid), rc);
		goto out;
	}

	D_DEBUG(DB_MGMT, "evicting pool connections "DF_UUID" succeed.\n",
		DP_UUID(pool_uuid));
out:
	return rc;
}

int
ds_mgmt_pool_target_update_state(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
				 uint32_t rank,
				 struct pool_target_id_list *target_list,
				 pool_comp_state_t state)
{
	int			rc;

	if (state == PO_COMP_ST_UP) {
		/* When doing reintegration, need to make sure the pool is
		 * created and started on the target rank
		 */

		d_rank_list_t reint_ranks;

		/* Just one list element - so reference it directly, rather
		 * than allocating an actual list array and populating it
		 */
		reint_ranks.rl_nr = 1;
		reint_ranks.rl_ranks = &rank;

		/* TODO: The size information and "pmem" type need to be
		 * determined automatically, perhaps by querying the pool leader
		 * This works for now because these parameters are ignored if
		 * the pool already exists on the destination node. This is
		 * just used to ensure the pool is started.
		 *
		 * Fixing this will add the ability to reintegrate with a new
		 * node, rather than only the previously failed node.
		 *
		 * This is tracked in DAOS-5041
		 */
		rc = ds_mgmt_tgt_pool_create_ranks(pool_uuid, "pmem",
						   &reint_ranks, 0, 0, NULL);
		if (rc != 0) {
			D_ERROR("creating pool on ranks "DF_UUID" failed: rc "
				DF_RC"\n", DP_UUID(pool_uuid), DP_RC(rc));
			return rc;
		}
	}

	rc = ds_pool_target_update_state(pool_uuid, svc_ranks, rank,
					 target_list, state);

	return rc;
}

/* Get container list from the pool service for the specified pool */
int
ds_mgmt_pool_list_cont(uuid_t uuid, d_rank_list_t *svc_ranks,
		       struct daos_pool_cont_info **containers,
		       uint64_t *ncontainers)
{
	D_DEBUG(DB_MGMT, "Getting container list for pool "DF_UUID"\n",
		DP_UUID(uuid));

	/* call pool service function to issue CaRT RPC to the pool service */
	return ds_pool_svc_list_cont(uuid, svc_ranks, containers, ncontainers);
}

/**
 * Calls into the pool svc to query a pool by UUID.
 *
 * \param[in]		pool_uuid	UUID of the pool
 * \param[in][out]	pool_info	Query results
 *
 * \return		0		Success
 *			-DER_INVAL	Invalid inputs
 *			Negative value	Other error
 */
int
ds_mgmt_pool_query(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		   daos_pool_info_t *pool_info)
{
	if (pool_info == NULL) {
		D_ERROR("pool_info was NULL\n");
		return -DER_INVAL;
	}

	D_DEBUG(DB_MGMT, "Querying pool "DF_UUID"\n", DP_UUID(pool_uuid));

	return ds_pool_svc_query(pool_uuid, svc_ranks, pool_info);
}

static int
get_access_props(uuid_t pool_uuid, d_rank_list_t *ranks, daos_prop_t **prop)
{
	static const size_t	ACCESS_PROPS_LEN = 3;
	static const uint32_t	ACCESS_PROPS[] = {DAOS_PROP_PO_ACL,
						  DAOS_PROP_PO_OWNER,
						  DAOS_PROP_PO_OWNER_GROUP};
	size_t			i;
	int			rc;
	daos_prop_t		*new_prop;

	new_prop = daos_prop_alloc(ACCESS_PROPS_LEN);
	for (i = 0; i < ACCESS_PROPS_LEN; i++)
		new_prop->dpp_entries[i].dpe_type = ACCESS_PROPS[i];

	rc = ds_pool_svc_get_prop(pool_uuid, ranks, new_prop);
	if (rc != 0) {
		daos_prop_free(new_prop);
		return rc;
	}

	*prop = new_prop;
	return 0;
}

int
ds_mgmt_pool_get_acl(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		     daos_prop_t **access_prop)
{
	D_DEBUG(DB_MGMT, "Getting ACL for pool "DF_UUID"\n",
		DP_UUID(pool_uuid));

	return get_access_props(pool_uuid, svc_ranks, access_prop);
}

int
ds_mgmt_pool_overwrite_acl(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			   struct daos_acl *acl,
			   daos_prop_t **result)
{
	int			rc;
	daos_prop_t		*prop;

	D_DEBUG(DB_MGMT, "Overwriting ACL for pool "DF_UUID"\n",
		DP_UUID(pool_uuid));

	prop = daos_prop_alloc(1);
	if (prop == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	prop->dpp_entries[0].dpe_type = DAOS_PROP_PO_ACL;
	prop->dpp_entries[0].dpe_val_ptr = daos_acl_dup(acl);

	rc = ds_pool_svc_set_prop(pool_uuid, svc_ranks, prop);
	if (rc != 0)
		goto out_prop;

	rc = get_access_props(pool_uuid, svc_ranks, result);
	if (rc != 0)
		goto out_prop;

out_prop:
	daos_prop_free(prop);
out:
	return rc;
}

int
ds_mgmt_pool_update_acl(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			struct daos_acl *acl, daos_prop_t **result)
{
	int			rc;

	D_DEBUG(DB_MGMT, "Updating ACL for pool "DF_UUID"\n",
		DP_UUID(pool_uuid));

	rc = ds_pool_svc_update_acl(pool_uuid, svc_ranks, acl);
	if (rc != 0)
		goto out;

	rc = get_access_props(pool_uuid, svc_ranks, result);
	if (rc != 0)
		goto out;

out:
	return rc;
}

int
ds_mgmt_pool_delete_acl(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			const char *principal, daos_prop_t **result)
{
	int				rc;
	enum daos_acl_principal_type	type;
	char				*name = NULL;

	D_DEBUG(DB_MGMT, "Deleting ACL entry for pool "DF_UUID"\n",
		DP_UUID(pool_uuid));

	rc = daos_acl_principal_from_str(principal, &type, &name);
	if (rc != 0)
		goto out;

	rc = ds_pool_svc_delete_acl(pool_uuid, svc_ranks, type, name);
	if (rc != 0)
		goto out_name;

	rc = get_access_props(pool_uuid, svc_ranks, result);
	if (rc != 0)
		goto out_name;

out_name:
	D_FREE(name);
out:
	return rc;
}

int
ds_mgmt_pool_set_prop(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		      daos_prop_t *prop, daos_prop_t **result)
{
	int              rc;
	size_t           i;
	daos_prop_t	*res_prop;

	if (prop == NULL || prop->dpp_entries == NULL || prop->dpp_nr < 1) {
		D_ERROR("invalid property\n");
		rc = -DER_INVAL;
		goto out;
	}

	D_DEBUG(DB_MGMT, "Setting property for pool "DF_UUID"\n",
		DP_UUID(pool_uuid));

	rc = ds_pool_svc_set_prop(pool_uuid, svc_ranks, prop);
	if (rc != 0)
		goto out;

	res_prop = daos_prop_alloc(prop->dpp_nr);
	for (i = 0; i < prop->dpp_nr; i++)
		res_prop->dpp_entries[i].dpe_type =
			prop->dpp_entries[i].dpe_type;

	rc = ds_pool_svc_get_prop(pool_uuid, svc_ranks, res_prop);
	if (rc != 0) {
		daos_prop_free(res_prop);
		goto out;
	}

	*result = res_prop;

out:
	return rc;
}
