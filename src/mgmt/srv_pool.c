/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

/** Destroy the pool on the specified ranks. */
static int
ds_mgmt_tgt_pool_destroy_ranks(uuid_t pool_uuid, d_rank_list_t *filter_ranks)
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
	rc = crt_corpc_req_create(dss_get_module_info()->dmi_ctx, NULL, filter_ranks, opc, NULL,
				  NULL, CRT_RPC_FLAG_FILTER_INVERT, topo, &td_req);
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

static uint32_t
pool_create_rpc_timeout(crt_rpc_t *tc_req, size_t scm_size)
{
	uint32_t	timeout;
	uint32_t	default_timeout;
	size_t		gib;
	int		rc;
	rc = crt_req_get_timeout(tc_req, &default_timeout);
	D_ASSERTF(rc == 0, "crt_req_get_timeout: "DF_RC"\n", DP_RC(rc));

	gib = scm_size / ((size_t)1024 * 1024 * 1024);
	if (gib < 32)
		timeout = 15;
	else if (gib < 64)
		timeout = 30;
	else if (gib < 128)
		timeout = 60;
	else
		timeout = 90;

	return max(timeout, default_timeout);
}

static int
ds_mgmt_tgt_pool_create_ranks(uuid_t pool_uuid, char *tgt_dev, d_rank_list_t *rank_list,
			      size_t scm_size, size_t nvme_size)
{
	crt_rpc_t			*tc_req;
	crt_opcode_t			opc;
	struct mgmt_tgt_create_in	*tc_in;
	struct mgmt_tgt_create_out	*tc_out = NULL;
	int				topo;
	int				rc;
	int				rc_cleanup;
	uint32_t			timeout;

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

	timeout = pool_create_rpc_timeout(tc_req, scm_size);
	crt_req_set_timeout(tc_req, timeout);
	D_DEBUG(DB_MGMT, DF_UUID": pool create RPC timeout: %u\n",
		DP_UUID(pool_uuid), timeout);
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
		D_ERROR(DF_UUID": failed to create targets: rc="DF_RC"\n",
			DP_UUID(tc_in->tc_pool_uuid), DP_RC(rc));
		D_GOTO(decref, rc);
	}

	D_DEBUG(DB_MGMT, DF_UUID" created pool tgts on %zu ranks\n",
		DP_UUID(pool_uuid), tc_out->tc_ranks.ca_count);

decref:
	if (tc_out)
		D_FREE(tc_out->tc_ranks.ca_arrays);

	crt_req_decref(tc_req);
	if (rc) {
		rc_cleanup = ds_mgmt_tgt_pool_destroy_ranks(pool_uuid, rank_list);
		if (rc_cleanup)
			D_ERROR(DF_UUID": failed to clean up failed pool: "
				DF_RC"\n", DP_UUID(pool_uuid), DP_RC(rc));
		else
			D_DEBUG(DB_MGMT, DF_UUID": cleaned up failed create targets\n",
				DP_UUID(pool_uuid));
	}

	return rc;
}

static int
ds_mgmt_pool_svc_create(uuid_t pool_uuid, int ntargets, const char *group, d_rank_list_t *ranks,
			daos_prop_t *prop, d_rank_list_t **svc_list, size_t domains_nr,
			uint32_t *domains)
{
	D_DEBUG(DB_MGMT, DF_UUID": all tgts created, setting up pool "
		"svc\n", DP_UUID(pool_uuid));

	return ds_pool_svc_dist_create(pool_uuid, ranks->rl_nr, group, ranks, domains_nr, domains,
				       prop, svc_list);
}

int
ds_mgmt_create_pool(uuid_t pool_uuid, const char *group, char *tgt_dev,
		    d_rank_list_t *targets, size_t scm_size, size_t nvme_size,
		    daos_prop_t *prop, d_rank_list_t **svcp,
		    int domains_nr, uint32_t *domains)
{
	d_rank_list_t			*pg_ranks = NULL;
	d_rank_list_t			*pg_targets = NULL;
	int				rc;
	int				rc_cleanup;

	/* Sanity check targets versus cart's current primary group members.
	 * If any targets not in PG, flag error before MGMT_TGT_ corpcs fail.
	 */
	rc = crt_group_ranks_get(NULL, &pg_ranks);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));

	rc = d_rank_list_dup(&pg_targets, targets);
	if (rc != 0)
		D_GOTO(out, rc);

	/* The pg_ranks and targets lists should overlap perfectly.
	 * If not, fail early to avoid expensive corpc failures.
	 */
	d_rank_list_filter(pg_ranks, pg_targets, false /* exclude */);
	if (!d_rank_list_identical(pg_targets, targets)) {
		char *pg_str, *tgt_str;

		pg_str = d_rank_list_to_str(pg_ranks);
		if (pg_str == NULL) {
			rc = -DER_NOMEM;
			D_GOTO(out, rc);
		}

		tgt_str = d_rank_list_to_str(targets);
		if (tgt_str == NULL) {
			D_FREE(pg_str);
			rc = -DER_NOMEM;
			D_GOTO(out, rc);
		}

		D_ERROR(DF_UUID": targets (%s) contains ranks not in pg (%s)\n",
			DP_UUID(pool_uuid), tgt_str, pg_str);

		D_FREE(pg_str);
		D_FREE(tgt_str);
		D_GOTO(out, rc = -DER_OOG);
	}

	rc = ds_mgmt_tgt_pool_create_ranks(pool_uuid, tgt_dev, targets,
					   scm_size, nvme_size);
	if (rc != 0) {
		D_ERROR("creating pool "DF_UUID" on ranks failed: rc "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	D_INFO(DF_UUID": creating targets on ranks succeeded\n", DP_UUID(pool_uuid));

	rc = ds_mgmt_pool_svc_create(pool_uuid, targets->rl_nr, group, targets, prop, svcp,
				     domains_nr, domains);
	if (rc) {
		D_ERROR("create pool "DF_UUID" svc failed: rc "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		/*
		 * The ds_mgmt_pool_svc_create call doesn't clean up any
		 * successful PS replica creations upon errors; we clean up
		 * those here together with other pool resources to save one
		 * round of RPCs.
		 */
		rc_cleanup = ds_mgmt_tgt_pool_destroy_ranks(pool_uuid, targets);
		if (rc_cleanup)
			D_ERROR(DF_UUID": failed to clean up failed pool: "DF_RC"\n",
				DP_UUID(pool_uuid), DP_RC(rc_cleanup));
		else
			D_DEBUG(DB_MGMT, DF_UUID": cleaned up failed create targets\n",
				DP_UUID(pool_uuid));
	} else {
		D_INFO(DF_UUID": creating svc succeeded\n", DP_UUID(pool_uuid));
	}

out:
	d_rank_list_free(pg_targets);
	d_rank_list_free(pg_ranks);
	D_DEBUG(DB_MGMT, "create pool "DF_UUID": "DF_RC"\n", DP_UUID(pool_uuid),
		DP_RC(rc));
	return rc;
}

int
ds_mgmt_destroy_pool(uuid_t pool_uuid, d_rank_list_t *ranks)
{
	int rc;

	D_DEBUG(DB_MGMT, "Destroying pool "DF_UUID"\n", DP_UUID(pool_uuid));

	if (ranks == NULL) {
		D_ERROR("ranks was NULL\n");
		return -DER_INVAL;
	}

	rc = ds_mgmt_tgt_pool_destroy_ranks(pool_uuid, ranks);
	if (rc != 0) {
		D_ERROR("Destroying pool " DF_UUID " failed, " DF_RC "\n", DP_UUID(pool_uuid),
			DP_RC(rc));
		goto out;
	}

	D_INFO(DF_UUID": destroy succeeded.\n", DP_UUID(pool_uuid));
out:
	return rc;
}

int
ds_mgmt_pool_extend(uuid_t pool_uuid, d_rank_list_t *svc_ranks, d_rank_list_t *rank_list,
		    char *tgt_dev,  size_t scm_size, size_t nvme_size, size_t domains_nr,
		    uint32_t *domains)
{
	d_rank_list_t		*unique_add_ranks = NULL;
	int			ntargets;
	int			rc;

	D_DEBUG(DB_MGMT, "extend pool "DF_UUID"\n", DP_UUID(pool_uuid));

	rc = d_rank_list_dup_sort_uniq(&unique_add_ranks, rank_list);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = ds_mgmt_tgt_pool_create_ranks(pool_uuid, tgt_dev, unique_add_ranks, scm_size,
					   nvme_size);
	if (rc != 0) {
		D_ERROR("creating pool on ranks "DF_UUID" failed: rc "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* TODO: Need to make pool service aware of new rank UUIDs */

	ntargets = unique_add_ranks->rl_nr;
	rc = ds_pool_extend(pool_uuid, ntargets, unique_add_ranks, domains_nr, domains, svc_ranks);
out:
	d_rank_list_free(unique_add_ranks);
	return rc;
}

int
ds_mgmt_evict_pool(uuid_t pool_uuid, d_rank_list_t *svc_ranks, uuid_t *handles, size_t n_handles,
		   uint32_t destroy, uint32_t force_destroy, char *machine, uint32_t *count)
{
	int		 rc;

	D_DEBUG(DB_MGMT, "evict pool "DF_UUID"\n", DP_UUID(pool_uuid));

	/* Evict active pool connections if they exist*/
	rc = ds_pool_svc_check_evict(pool_uuid, svc_ranks, handles, n_handles,
				     destroy, force_destroy, machine, count);
	if (rc != 0) {
		D_ERROR("Failed to evict pool handles" DF_UUID " rc: " DF_RC "\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out;
	}

	D_INFO(DF_UUID": evict connections succeeded\n", DP_UUID(pool_uuid));
out:
	return rc;
}

int
ds_mgmt_pool_target_update_state(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
				 struct pool_target_addr_list *target_addrs,
				 pool_comp_state_t state, size_t scm_size, size_t nvme_size)
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
		reint_ranks.rl_ranks = &target_addrs->pta_addrs[0].pta_rank;

		rc = ds_mgmt_tgt_pool_create_ranks(pool_uuid, "pmem", &reint_ranks, scm_size,
						   nvme_size);
		if (rc != 0) {
			D_ERROR("creating pool on ranks "DF_UUID" failed: rc "
				DF_RC"\n", DP_UUID(pool_uuid), DP_RC(rc));
			return rc;
		}
	}

	rc = ds_pool_target_update_state(pool_uuid, svc_ranks, target_addrs, state);

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
 * \param[in]		pool_uuid	   UUID of the pool.
 * \param[in]		svc_ranks	   Ranks of pool svc replicas.
 * \param[out]		ranks		   Optional, returned storage ranks in this pool.
 *					   If #pool_info is NULL, engines with disabled targets.
 *					   If #pool_info is passed, engines with enabled or
 *					   disabled targets according to
 *					   #pi_bits (DPI_ENGINES_ENABLED bit).
 *					   Note: ranks may be empty (i.e., *ranks->rl_nr may be 0).
 *					   The caller must free the list with d_rank_list_free().
 * \param[in][out]	pool_info	   Query results
 * \param[in][out]	pool_layout_ver	   Pool global version
 * \param[in][out]	upgrade_layout_ver Latest pool global version this pool might be upgraded
 *
 * \return		0		   Success
 *			-DER_INVAL	   Invalid inputs
 *			Negative value	   Other error
 */
int
ds_mgmt_pool_query(uuid_t pool_uuid, d_rank_list_t *svc_ranks, d_rank_list_t **ranks,
		   daos_pool_info_t *pool_info, uint32_t *pool_layout_ver,
		   uint32_t *upgrade_layout_ver)
{
	uint64_t deadline;

	if (pool_info == NULL) {
		D_ERROR("pool_info was NULL\n");
		return -DER_INVAL;
	}

	D_DEBUG(DB_MGMT, "Querying pool "DF_UUID"\n", DP_UUID(pool_uuid));

	/*
	 * Use a fixed timeout that matches what the control plane uses for the
	 * moment.
	 *
	 * TODO: Pass the deadline from dmg (or daos_server).
	 */
	deadline = daos_getmtime_coarse() + 5 * 60 * 1000;

	return dsc_pool_svc_query(pool_uuid, svc_ranks, deadline, ranks, pool_info, pool_layout_ver,
				  upgrade_layout_ver);
}

/**
 * Calls into the pool svc to query one or more targets of a pool storage engine.
 *
 * \param[in]		pool_uuid	UUID of the pool.
 * \param[in]		svc_ranks	Ranks of pool svc replicas.
 * \param[in]		rank		Rank of the pool storage engine.
 * \param[in]		tgts		Target indices of the engine.
 * \param[out]		infos		State, storage  capacity/usage per target in \a tgts.
 *					Allocated if returning 0. Caller frees with D_FREE().
 *
 * \return		0		Success
 *			-DER_INVAL	Invalid inputs
 *			Negative value	Other error
 */
int
ds_mgmt_pool_query_targets(uuid_t pool_uuid, d_rank_list_t *svc_ranks, d_rank_t rank,
			   d_rank_list_t *tgts, daos_target_info_t **infos)
{
	int			rc = 0;
	uint32_t		i;
	daos_target_info_t	*out_infos = NULL;

	if (infos == NULL) {
		D_ERROR("infos argument was NULL\n");
		return -DER_INVAL;
	}

	D_ALLOC_ARRAY(out_infos, tgts->rl_nr);
	if (out_infos == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < tgts->rl_nr; i++) {
		D_DEBUG(DB_MGMT, "Querying pool "DF_UUID" rank %u tgt %u\n", DP_UUID(pool_uuid),
			rank, tgts->rl_ranks[i]);
		rc = ds_pool_svc_query_target(pool_uuid, svc_ranks, rank, tgts->rl_ranks[i],
					      &out_infos[i]);
		if (rc != 0) {
			D_ERROR(DF_UUID": ds_pool_svc_query_target() failed rank %u tgt %u\n",
				DP_UUID(pool_uuid), rank, tgts->rl_ranks[i]);
			goto out;
		}
	}

out:
	if (rc)
		D_FREE(out_infos);
	else
		*infos = out_infos;

	return rc;
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
	if (new_prop == NULL)
		return -DER_NOMEM;

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
		      daos_prop_t *prop)
{
	int              rc;

	if (prop == NULL || prop->dpp_entries == NULL || prop->dpp_nr < 1) {
		D_ERROR("invalid property list\n");
		rc = -DER_INVAL;
		goto out;
	}

	D_DEBUG(DB_MGMT, "Setting properties for pool "DF_UUID"\n",
		DP_UUID(pool_uuid));

	rc = ds_pool_svc_set_prop(pool_uuid, svc_ranks, prop);

out:
	return rc;
}

int ds_mgmt_pool_upgrade(uuid_t pool_uuid, d_rank_list_t *svc_ranks)
{
	D_DEBUG(DB_MGMT, "Upgrading pool "DF_UUID"\n",
		DP_UUID(pool_uuid));

	return ds_pool_svc_upgrade(pool_uuid, svc_ranks);
}

int
ds_mgmt_pool_get_prop(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		      daos_prop_t *prop)
{
	int              rc;

	if (prop == NULL || prop->dpp_entries == NULL || prop->dpp_nr < 1) {
		D_ERROR("invalid property list\n");
		rc = -DER_INVAL;
		goto out;
	}

	D_DEBUG(DB_MGMT, "Getting properties for pool "DF_UUID"\n",
		DP_UUID(pool_uuid));

	rc = ds_pool_svc_get_prop(pool_uuid, svc_ranks, prop);

out:
	return rc;
}
