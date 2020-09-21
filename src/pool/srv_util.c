/**
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
 * ds_pool: Pool Server Utilities
 */
#define D_LOGFAC	DD_FAC(pool)

#include <daos_srv/pool.h>
#include <daos_srv/bio.h>
#include <daos_srv/smd.h>

#include <daos/pool_map.h>
#include "rpc.h"
#include "srv_internal.h"

static inline int
map_ranks_include(enum map_ranks_class class, int status)
{
	switch (class) {
	case MAP_RANKS_UP:
		return status == PO_COMP_ST_UP ||
		       status == PO_COMP_ST_UPIN ||
		       status == PO_COMP_ST_NEW;
	case MAP_RANKS_DOWN:
		return status == PO_COMP_ST_DOWN ||
		       status == PO_COMP_ST_DOWNOUT ||
		       status == PO_COMP_ST_DRAIN;
	default:
		D_ASSERTF(0, "%d\n", class);
	}

	return 0;
}

/* Build a rank list of targets with certain status. */
int
map_ranks_init(const struct pool_map *map, enum map_ranks_class class,
	       d_rank_list_t *ranks)
{
	struct pool_domain     *domains = NULL;
	int			nnodes;
	int			n = 0;
	int			i;
	d_rank_t	       *rs;

	nnodes = pool_map_find_nodes((struct pool_map *)map,
				      PO_COMP_ID_ALL, &domains);
	if (nnodes == 0) {
		D_ERROR("no nodes in pool map\n");
		return -DER_IO;
	}

	for (i = 0; i < nnodes; i++) {
		if (map_ranks_include(class, domains[i].do_comp.co_status))
			n++;
	}

	if (n == 0) {
		memset(ranks, 0, sizeof(*ranks));
		return 0;
	}

	D_ALLOC_ARRAY(rs, n);
	if (rs == NULL)
		return -DER_NOMEM;

	ranks->rl_nr = n;
	ranks->rl_ranks = rs;

	n = 0;
	for (i = 0; i < nnodes; i++) {
		if (map_ranks_include(class, domains[i].do_comp.co_status)) {
			D_ASSERT(n < ranks->rl_nr);
			ranks->rl_ranks[n] = domains[i].do_comp.co_rank;
			n++;
		}
	}
	D_ASSERTF(n == ranks->rl_nr, "%d != %u\n", n, ranks->rl_nr);

	return 0;
}

void
map_ranks_fini(d_rank_list_t *ranks)
{
	if (ranks->rl_ranks != NULL) {
		D_ASSERT(ranks->rl_nr != 0);
		D_FREE(ranks->rl_ranks);
	} else {
		D_ASSERT(ranks->rl_nr == 0);
	}
}

static int
map_ranks_merge(d_rank_list_t *src_ranks, d_rank_list_t *ranks_merge)
{
	d_rank_t	*rs;
	int		*indexes;
	int		num = 0;
	int		src_num;
	int		i;
	int		j;
	int		rc = 0;

	if (ranks_merge == NULL || src_ranks == NULL)
		return 0;

	src_num = src_ranks->rl_nr;
	D_ALLOC_ARRAY(indexes, ranks_merge->rl_nr);
	if (indexes == NULL)
		return -DER_NOMEM;

	for (i = 0; i < ranks_merge->rl_nr; i++) {
		bool included = false;

		for (j = 0; j < src_num; j++) {
			if (src_ranks->rl_ranks[j] ==
			    ranks_merge->rl_ranks[i]) {
				included = true;
				break;
			}
		}

		if (!included) {
			indexes[num] = i;
			num++;
		}
	}

	if (num == 0)
		D_GOTO(free, rc = 0);

	D_ALLOC_ARRAY(rs, (num + src_ranks->rl_nr));
	if (rs == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	for (i = 0; i < src_num; i++)
		rs[i] = src_ranks->rl_ranks[i];

	for (i = src_num, j = 0; i < src_num + num; i++, j++) {
		int idx = indexes[j];

		rs[i] = ranks_merge->rl_ranks[idx];
	}

	map_ranks_fini(src_ranks);

	src_ranks->rl_nr = num + src_num;
	src_ranks->rl_ranks = rs;

free:
	D_FREE(indexes);
	return rc;
}

int
ds_pool_bcast_create(crt_context_t ctx, struct ds_pool *pool,
		     enum daos_module_id module, crt_opcode_t opcode,
		     crt_rpc_t **rpc, crt_bulk_t bulk_hdl,
		     d_rank_list_t *excluded_list)
{
	d_rank_list_t	excluded;
	crt_opcode_t		opc;
	int			rc;

	ABT_rwlock_rdlock(pool->sp_lock);
	rc = map_ranks_init(pool->sp_map, MAP_RANKS_DOWN, &excluded);
	ABT_rwlock_unlock(pool->sp_lock);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create rank list: %d\n",
			DP_UUID(pool->sp_uuid), rc);
		return rc;
	}

	if (excluded_list != NULL)
		map_ranks_merge(&excluded, excluded_list);

	opc = DAOS_RPC_OPCODE(opcode, module, 1);
	rc = crt_corpc_req_create(ctx, pool->sp_group,
			  excluded.rl_nr == 0 ? NULL : &excluded,
			  opc, bulk_hdl/* co_bulk_hdl */, NULL /* priv */,
			  0 /* flags */, crt_tree_topo(CRT_TREE_KNOMIAL, 32),
			  rpc);

	map_ranks_fini(&excluded);
	return rc;
}

/*
 * Updates a single target by the operation given in opc
 * If something changed, *version is incremented
 * Returns 0 on success or if there's nothing to do. -DER_BUSY if the operation
 * is valid but needs to wait for rebuild to finish, -DER_INVAL if the state
 * transition is invalid
 */
static int
update_one_tgt(struct pool_map *map, struct pool_target *target,
	       struct pool_domain *dom, int opc, bool evict_rank,
	       uint32_t *version) {
	int rc;

	D_ASSERTF(target->ta_comp.co_status == PO_COMP_ST_UP ||
		  target->ta_comp.co_status == PO_COMP_ST_NEW ||
		  target->ta_comp.co_status == PO_COMP_ST_UPIN ||
		  target->ta_comp.co_status == PO_COMP_ST_DOWN ||
		  target->ta_comp.co_status == PO_COMP_ST_DRAIN ||
		  target->ta_comp.co_status == PO_COMP_ST_DOWNOUT,
		  "%u\n", target->ta_comp.co_status);

	switch (opc) {
	case POOL_EXCLUDE:
		switch (target->ta_comp.co_status) {
		case PO_COMP_ST_DOWN:
		case PO_COMP_ST_DOWNOUT:
			/* Nothing to do, already excluded */
			D_INFO("Skip exclude down target (rank %u idx %u)\n",
			       target->ta_comp.co_rank,
				target->ta_comp.co_index);
			break;
		case PO_COMP_ST_UP:
		case PO_COMP_ST_UPIN:
		case PO_COMP_ST_DRAIN:
			D_DEBUG(DF_DSMS, "change target %u/%u to DOWN %p\n",
				target->ta_comp.co_rank,
				target->ta_comp.co_index, map);
			target->ta_comp.co_status = PO_COMP_ST_DOWN;
			target->ta_comp.co_fseq = ++(*version);

			D_PRINT("Target (rank %u idx %u) is down.\n",
				target->ta_comp.co_rank,
				target->ta_comp.co_index);
			if (evict_rank && pool_map_node_status_match(dom,
				PO_COMP_ST_DOWN | PO_COMP_ST_DOWNOUT)) {
				D_DEBUG(DF_DSMS, "change rank %u to DOWN\n",
					dom->do_comp.co_rank);
				dom->do_comp.co_status = PO_COMP_ST_DOWN;
				dom->do_comp.co_fseq = target->ta_comp.co_fseq;
			}
			break;
		case PO_COMP_ST_NEW:
			/*
			 * TODO: Add some handling for what happens when
			 * addition fails. Probably need to remove these
			 * targets from the pool map, rather than setting them
			 * to a different state
			 */
			return -DER_NOSYS;
		}
		break;

	case POOL_DRAIN:
		switch (target->ta_comp.co_status) {
		case PO_COMP_ST_DOWN:
		case PO_COMP_ST_DRAIN:
		case PO_COMP_ST_DOWNOUT:
			/* Nothing to do, already excluded / draining */
			D_INFO("Skip drain down target (rank %u idx %u)\n",
			       target->ta_comp.co_rank,
				target->ta_comp.co_index);
			break;
		case PO_COMP_ST_NEW:
			D_ERROR("Can't drain new target (rank %u idx %u)\n",
				target->ta_comp.co_rank,
				target->ta_comp.co_index);
			return -DER_BUSY;
		case PO_COMP_ST_UP:
			D_ERROR("Can't drain reint target (rank %u idx %u)\n",
				target->ta_comp.co_rank,
				target->ta_comp.co_index);
			return -DER_BUSY;
		case PO_COMP_ST_UPIN:
			D_DEBUG(DF_DSMS, "change target %u/%u to DRAIN %p\n",
				target->ta_comp.co_rank,
				target->ta_comp.co_index, map);
			target->ta_comp.co_status = PO_COMP_ST_DRAIN;
			target->ta_comp.co_fseq = ++(*version);

			D_PRINT("Target (rank %u idx %u) is draining.\n",
				target->ta_comp.co_rank,
				target->ta_comp.co_index);
			break;
		}
		break;

	case POOL_REINT:
		switch (target->ta_comp.co_status) {
		case PO_COMP_ST_NEW:
			/* Nothing to do, already added */
			D_INFO("Can't reint new target (rank %u idx %u)\n",
			       target->ta_comp.co_rank,
				target->ta_comp.co_index);
			return -DER_BUSY;
		case PO_COMP_ST_UP:
		case PO_COMP_ST_UPIN:
			/* Nothing to do, already added */
			D_INFO("Skip reint up target (rank %u idx %u)\n",
			       target->ta_comp.co_rank,
				target->ta_comp.co_index);
			break;
		case PO_COMP_ST_DOWN:
		case PO_COMP_ST_DRAIN:
			D_ERROR("Can't reint rebuilding tgt (rank %u idx %u)\n",
				target->ta_comp.co_rank,
				target->ta_comp.co_index);
			return -DER_BUSY;
		case PO_COMP_ST_DOWNOUT:
			D_DEBUG(DF_DSMS, "change target %u/%u to UP %p\n",
				target->ta_comp.co_rank,
				target->ta_comp.co_index, map);
			target->ta_comp.co_status = PO_COMP_ST_UP;
			target->ta_comp.co_fseq = ++(*version);

			D_PRINT("Target (rank %u idx %u) start reintegration\n",
				target->ta_comp.co_rank,
				target->ta_comp.co_index);
			D_DEBUG(DF_DSMS, "change rank %u to UP\n",
				dom->do_comp.co_rank);
			dom->do_comp.co_status = PO_COMP_ST_UP;
			break;
		}
		break;

	case POOL_ADD_IN:
		switch (target->ta_comp.co_status) {
		case PO_COMP_ST_UPIN:
			/* Nothing to do, already UPIN */
			D_INFO("Skip ADD_IN UPIN target (rank %u idx %u)\n",
			       target->ta_comp.co_rank,
			       target->ta_comp.co_index);
			break;
		case PO_COMP_ST_DOWN:
		case PO_COMP_ST_DRAIN:
		case PO_COMP_ST_DOWNOUT:
			D_ERROR("Can't ADD_IN non-up target (rank %u idx %u)\n",
				target->ta_comp.co_rank,
				target->ta_comp.co_index);
			return -DER_INVAL;
		case PO_COMP_ST_UP:
		case PO_COMP_ST_NEW:
			D_DEBUG(DF_DSMS, "change target %u/%u to UPIN %p\n",
				target->ta_comp.co_rank,
				target->ta_comp.co_index, map);
			/*
			 * Need to update this target AND all of its parents
			 * domains from NEW -> UPIN
			 */
			rc = pool_map_activate_new_target(map,
					target->ta_comp.co_id);
			D_ASSERT(rc != 0); /* This target must be findable */
			(*version)++;
			break;
		}
		break;

	case POOL_EXCLUDE_OUT:
		switch (target->ta_comp.co_status) {
		case PO_COMP_ST_DOWNOUT:
			/* Nothing to do, already DOWNOUT */
			D_INFO("Skip EXCLUDEOUT DOWNOUT tgt (rank %u idx %u)\n",
			       target->ta_comp.co_rank,
				target->ta_comp.co_index);
			break;
		case PO_COMP_ST_UP:
		case PO_COMP_ST_UPIN:
		case PO_COMP_ST_NEW:
			D_ERROR("Can't EXCLOUT non-down tgt (rank %u idx %u)\n",
				target->ta_comp.co_rank,
				target->ta_comp.co_index);
			return -DER_INVAL;
		case PO_COMP_ST_DRAIN:
		case PO_COMP_ST_DOWN:
			D_DEBUG(DF_DSMS, "change target %u/%u to DOWNOUT %p\n",
				target->ta_comp.co_rank,
				target->ta_comp.co_index, map);
			target->ta_comp.co_status = PO_COMP_ST_DOWNOUT;
			(*version)++;
			D_PRINT("Target (rank %u idx %u) is excluded.\n",
				target->ta_comp.co_rank,
				target->ta_comp.co_index);

			if (evict_rank && pool_map_node_status_match(dom,
						PO_COMP_ST_DOWNOUT)) {
				D_DEBUG(DF_DSMS, "change rank %u to DOWNOUT\n",
					dom->do_comp.co_rank);
				dom->do_comp.co_status = PO_COMP_ST_DOWNOUT;
			}
		}
		break;
	default:
		D_ERROR("Invalid pool target operation: %d\n", opc);
		D_ASSERT(0);
	}

	return DER_SUCCESS;
}

/*
 * Update "tgts" in "map". A new map version is generated only if actual
 * changes have been made.
 */
int
ds_pool_map_tgts_update(struct pool_map *map, struct pool_target_id_list *tgts,
			int opc, bool evict_rank)
{
	uint32_t	version;
	int		i;
	int		rc;

	D_ASSERT(tgts != NULL);

	version = pool_map_get_version(map);
	for (i = 0; i < tgts->pti_number; i++) {
		struct pool_target	*target = NULL;
		struct pool_domain	*dom = NULL;

		rc = pool_map_find_target(map, tgts->pti_ids[i].pti_id,
					  &target);
		if (rc <= 0) {
			D_DEBUG(DF_DSMS, "not find target %u in map %p\n",
				tgts->pti_ids[i].pti_id, map);
			continue;
		}

		dom = pool_map_find_node_by_rank(map, target->ta_comp.co_rank);
		if (dom == NULL) {
			D_DEBUG(DF_DSMS, "not find rank %u in map %p\n",
				target->ta_comp.co_rank, map);
			continue;
		}

		rc = update_one_tgt(map, target, dom, opc, evict_rank,
				    &version);
		if (rc != 0)
			return rc;
	}

	/* Set the version only if actual changes have been made. */
	if (version > pool_map_get_version(map)) {
		D_DEBUG(DF_DSMS, "generating map %p version %u:\n",
			map, version);
		rc = pool_map_set_version(map, version);
		D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	}

	return 0;
}

#define SWAP_RANKS(ranks, i, j)					\
	do {							\
		d_rank_t r = ranks->rl_ranks[i];		\
								\
		ranks->rl_ranks[i] = ranks->rl_ranks[j];	\
		ranks->rl_ranks[j] = r;				\
	} while (0)

/*
 * Find failed ranks in `replicas` and copy to a new list `failed`
 * Replace the failed ranks with ranks which are up and running.
 * `alt` points to the subset of `replicas` containing replacements.
 */
int
ds_pool_check_failed_replicas(struct pool_map *map, d_rank_list_t *replicas,
			      d_rank_list_t *failed, d_rank_list_t *alt)
{
	struct pool_domain	*nodes = NULL;
	int			 nnodes;
	int			 nfailed;
	int			 nreplaced;
	int			 idx;
	int			 i;
	int			 rc;

	nnodes = pool_map_find_nodes(map, PO_COMP_ID_ALL, &nodes);
	if (nnodes == 0) {
		D_ERROR("no nodes in pool map\n");
		return -DER_IO;
	}

	/**
	 * Move all ranks in the list of replicas which are marked as DOWN
	 * in the pool map to the end of the list.
	 **/
	for (i = 0, nfailed = 0; i < nnodes; i++) {
		if (!map_ranks_include(MAP_RANKS_DOWN,
				       nodes[i].do_comp.co_status))
			continue;
		if (!daos_rank_list_find(replicas,
					 nodes[i].do_comp.co_rank, &idx))
			continue;
		if (idx < replicas->rl_nr - (nfailed + 1))
			SWAP_RANKS(replicas, idx,
				   replicas->rl_nr - (nfailed + 1));
		++nfailed;
	}

	if (nfailed == 0) {
		memset(failed, 0, sizeof(*failed));
		memset(alt, 0, sizeof(*alt));
		return 0;
	}

	/** Make `alt` point to failed subset towards the end **/
	alt->rl_nr = nfailed;
	alt->rl_ranks = replicas->rl_ranks + (replicas->rl_nr - nfailed);

	/** Copy failed ranks to make room for replacements **/
	memset(failed, 0, sizeof(*failed));
	rc = daos_rank_list_copy(failed, alt);
	if (rc != 0)
		return rc;

	/**
	 * For replacements, search all ranks which are marked as UP
	 * in the pool map and not present in the list of replicas.
	 **/
	for (i = 0, nreplaced = 0; i < nnodes && nreplaced < nfailed; i++) {
		if (nodes[i].do_comp.co_rank == 0 /* Skip rank 0 */)
			continue;
		if (!map_ranks_include(MAP_RANKS_UP,
				       nodes[i].do_comp.co_status))
			continue;
		if (daos_rank_list_find(replicas,
					nodes[i].do_comp.co_rank, &idx))
			continue;
		alt->rl_ranks[nreplaced++] = nodes[i].do_comp.co_rank;
	}

	if (nreplaced < nfailed) {
		D_WARN("Not enough ranks available; Failed %d, Replacements %d",
			nfailed, nreplaced);
		alt->rl_nr = nreplaced;
		replicas->rl_nr -= (nfailed - nreplaced);
	}
	return 0;
}

/** The caller are responsible for freeing the ranks */
int ds_pool_get_ranks(const uuid_t pool_uuid, int status,
		      d_rank_list_t *ranks)
{
	struct ds_pool	*pool;
	int		rc;

	pool = ds_pool_lookup(pool_uuid);
	if (pool == NULL)
		return 0;

	/* This may not be the pool leader node, so down targets
	 * may not be updated, then the following collective RPC
	 * might be timeout. XXX
	 */
	ABT_rwlock_rdlock(pool->sp_lock);
	if (pool->sp_map == NULL) {
		rc = 0;
		goto out_lock;
	}
	rc = map_ranks_init(pool->sp_map, status, ranks);
out_lock:
	ABT_rwlock_unlock(pool->sp_lock);
	if (rc != 0)
		D_ERROR(DF_UUID": failed to create rank list: %d\n",
			DP_UUID(pool->sp_uuid), rc);

	ds_pool_put(pool);
	return rc;
}

/* Get failed target index on the current node */
int ds_pool_get_failed_tgt_idx(const uuid_t pool_uuid, int **failed_tgts,
			       unsigned int *failed_tgts_cnt)
{
	struct ds_pool		*pool;
	struct pool_target	**tgts = NULL;
	d_rank_t		myrank;
	int			rc;

	*failed_tgts_cnt = 0;
	pool = ds_pool_lookup(pool_uuid);
	if (pool == NULL || pool->sp_map == NULL)
		D_GOTO(output, rc = 0);

	/* Check if we need excluded the failure targets, NB:
	 * since the ranks in the pool map are ranks of primary
	 * group, so we have to use primary group here.
	 */
	rc = crt_group_rank(NULL, &myrank);
	if (rc) {
		D_ERROR("Can not get rank "DF_RC"\n", DP_RC(rc));
		D_GOTO(output, rc);
	}

	rc = pool_map_find_failed_tgts_by_rank(pool->sp_map, &tgts,
					       failed_tgts_cnt, myrank);
	if (rc) {
		D_ERROR("get failed tgts "DF_RC"\n", DP_RC(rc));
		D_GOTO(output, rc);
	}

	if (*failed_tgts_cnt != 0) {
		int i;

		D_ALLOC(*failed_tgts, *failed_tgts_cnt * sizeof(int));
		if (*failed_tgts == NULL) {
			D_FREE(tgts);
			*failed_tgts_cnt = 0;
			D_GOTO(output, rc = -DER_NOMEM);
		}
		for (i = 0; i < *failed_tgts_cnt; i++)
			(*failed_tgts)[i] = tgts[i]->ta_comp.co_index;

		D_FREE(tgts);
	}

output:
	if (pool)
		ds_pool_put(pool);
	return rc;
}

/* See nvme_faulty_reaction() for return values */
static int
check_pool_targets(uuid_t pool_id, int *tgt_ids, int tgt_cnt, d_rank_t *pl_rank)
{
	struct ds_pool		*pool;
	struct pool_target	*target = NULL;
	d_rank_t		 rank = dss_self_rank();
	int			 nr_downout, nr_down;
	int			 i, nr, rc = 0;

	/* Get pool map to check the target status */
	pool = ds_pool_lookup(pool_id);
	/*
	 * FIXME: Not supporting offline faulty reaction so far.
	 *
	 * The pool cache & pool map IV implementation will be improved
	 * later to not rely on pool connect or rebuild later, then we can
	 * setup pool cache/IV by a local pool open.
	 */
	if (pool == NULL) {
		D_DEBUG(DB_MGMT, DF_UUID": Pool cache not found\n",
			DP_UUID(pool_id));
		return -DER_NOSYS;
	}

	nr_downout = nr_down = 0;
	ABT_rwlock_rdlock(pool->sp_lock);
	for (i = 0; i < tgt_cnt; i++) {
		nr = pool_map_find_target_by_rank_idx(pool->sp_map, rank,
						      tgt_ids[i], &target);
		if (nr != 1) {
			D_ERROR(DF_UUID": Failed to get rank:%u, idx:%d\n",
				DP_UUID(pool_id), rank, tgt_ids[i]);
			rc = -DER_NONEXIST;
			break;
		}

		D_ASSERT(target != NULL);
		switch (target->ta_comp.co_status) {
		case PO_COMP_ST_DOWNOUT:
			nr_downout++;
			break;
		case PO_COMP_ST_DOWN:
			nr_down++;
			break;
		default:
			break;
		}
	}
	D_ASSERT(nr_downout + nr_down <= tgt_cnt);

	if (pool->sp_iv_ns != NULL)
		*pl_rank = pool->sp_iv_ns->iv_master_rank;
	else
		*pl_rank = -1;

	ABT_rwlock_unlock(pool->sp_lock);
	ds_pool_put(pool);

	if (rc)
		return rc;
	else if (nr_downout == tgt_cnt)
		return 0;
	else if (nr_downout + nr_down == tgt_cnt)
		return 1;
	else
		return (*pl_rank == -1) ? -DER_NOSYS : 2;
}

struct exclude_targets_arg {
	uuid_t		 eta_pool_id;
	d_rank_t	 eta_pl_rank;
	d_rank_t	*eta_ranks;
	int		*eta_tgts;
	int		 eta_nr;
};

static void
free_exclude_targets_arg(struct exclude_targets_arg *eta)
{
	D_ASSERT(eta != NULL);
	if (eta->eta_ranks != NULL)
		D_FREE(eta->eta_ranks);
	if (eta->eta_tgts != NULL)
		D_FREE(eta->eta_tgts);
	D_FREE(eta);
}

static struct exclude_targets_arg *
alloc_exclude_targets_arg(uuid_t pool_id, int *tgt_ids, int tgt_cnt,
			  d_rank_t pl_rank)
{
	struct exclude_targets_arg	*eta;
	d_rank_t			 rank;
	int				 i;

	D_ASSERT(tgt_cnt > 0);
	D_ASSERT(tgt_ids != NULL);

	D_ALLOC_PTR(eta);
	if (eta == NULL)
		return NULL;

	D_ALLOC_ARRAY(eta->eta_ranks, tgt_cnt);
	if (eta->eta_ranks == NULL)
		goto free;

	D_ALLOC_ARRAY(eta->eta_tgts, tgt_cnt);
	if (eta->eta_tgts == NULL)
		goto free;

	uuid_copy(eta->eta_pool_id, pool_id);
	eta->eta_pl_rank = pl_rank;
	eta->eta_nr = tgt_cnt;
	rank = dss_self_rank();
	for (i = 0; i < eta->eta_nr; i++) {
		eta->eta_ranks[i] = rank;
		eta->eta_tgts[i] = tgt_ids[i];
	}

	return eta;
free:
	free_exclude_targets_arg(eta);
	return NULL;
}

static void
exclude_targets_ult(void *arg)
{
	struct exclude_targets_arg	*eta = arg;
	struct d_tgt_list		 tgt_list;
	d_rank_list_t			 svc;
	int				 rc;

	svc.rl_ranks = &eta->eta_pl_rank;
	svc.rl_nr = 1;

	tgt_list.tl_nr = eta->eta_nr;
	tgt_list.tl_ranks = eta->eta_ranks;
	tgt_list.tl_tgts = eta->eta_tgts;

	rc = dsc_pool_tgt_exclude(eta->eta_pool_id, NULL /* grp */, &svc,
				  &tgt_list);
	if (rc)
		D_ERROR(DF_UUID": Exclude targets failed. %d\n",
			DP_UUID(eta->eta_pool_id), rc);

	free_exclude_targets_arg(eta);
}

/*
 * The NVMe faulty reaction is called from bio_nvme_poll() which is on
 * progress (hardware poll) ULT, and it will call into client stack to
 * exclude pool targets, blocking calls could be made in this code path,
 * so we have to perform the faulty reactions asynchronously in a new ULT
 * to avoid blocking the hardware poll.
 */
static int
exclude_pool_targets(uuid_t pool_id, int *tgt_ids, int tgt_cnt,
		     d_rank_t pl_rank)
{
	struct exclude_targets_arg	*eta;
	int				 rc;

	eta = alloc_exclude_targets_arg(pool_id, tgt_ids, tgt_cnt, pl_rank);
	if (eta == NULL)
		return -DER_NOMEM;

	rc = dss_ult_create(exclude_targets_ult, eta, DSS_ULT_MISC,
			    DSS_TGT_SELF, 0, NULL);
	if (rc) {
		D_ERROR(DF_UUID": Failed to start excluding ULT. %d\n",
			DP_UUID(pool_id), rc);
		free_exclude_targets_arg(eta);
	}

	return rc;
}

static int
nvme_faulty_reaction(int *tgt_ids, int tgt_cnt)
{
	struct smd_pool_info	*pool_info, *tmp;
	d_list_t		 pool_list;
	d_rank_t		 pl_rank;
	int			 pool_cnt, ret, rc;

	D_ASSERT(tgt_cnt > 0);
	D_ASSERT(tgt_ids != NULL);

	D_INIT_LIST_HEAD(&pool_list);
	rc = smd_pool_list(&pool_list, &pool_cnt);
	if (rc) {
		D_ERROR("Failed to list pools: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	d_list_for_each_entry_safe(pool_info, tmp, &pool_list, spi_link) {
		ret = check_pool_targets(pool_info->spi_id, tgt_ids, tgt_cnt,
					 &pl_rank);
		switch (ret) {
		case 0:
			/*
			 * All affected targets are in DOWN_OUT, it's safe to
			 * transit NVMe state to BIO_BS_STATE_TEARDOWN now.
			 */
			D_DEBUG(DB_MGMT, DF_UUID": Targets are excluded out.\n",
				DP_UUID(pool_info->spi_id));
			break;
		case 1:
			/*
			 * All affected targets are in DOWN, it's safe to
			 * transit NVMe state to BIO_BS_STATE_TEARDOWN now.
			 */
			D_DEBUG(DB_MGMT, DF_UUID": Targets are in excluding.\n",
				DP_UUID(pool_info->spi_id));
			break;
		case 2:
			/*
			 * Some affected targets are still in UP or UPIN, need
			 * to send exclude RPC.
			 */
			D_DEBUG(DB_MGMT, DF_UUID": Trigger targets exclude.\n",
				DP_UUID(pool_info->spi_id));
			rc = exclude_pool_targets(pool_info->spi_id, tgt_ids,
						  tgt_cnt, pl_rank);
			if (rc == 0)
				rc = 1;
			break;
		default:
			/* Errors */
			D_ERROR(DF_UUID": Check targets status failed: %d\n",
				DP_UUID(pool_info->spi_id), ret);
			if (rc >= 0)
				rc = ret;
			break;
		}

		d_list_del(&pool_info->spi_link);
		smd_free_pool_info(pool_info);
	}

	D_DEBUG(DB_MGMT, "Faulty reaction done. tgt_cnt:%d, rc:%d\n",
		tgt_cnt, rc);
	return rc;
}

static int
nvme_bio_error(int media_err_type, int tgt_id)
{
	int rc;

	rc = notify_bio_error(media_err_type, tgt_id);

	return rc;
}

struct bio_reaction_ops nvme_reaction_ops = {
	.faulty_reaction	= nvme_faulty_reaction,
	.reint_reaction		= NULL,
	.ioerr_reaction		= nvme_bio_error,
};
