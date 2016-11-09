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
 * ds_pool: Pool Server Utilities
 */

#include <daos_srv/pool.h>

#include <daos/pool_map.h>
#include "srv_internal.h"

enum map_ranks_class {
	MAP_RANKS_UP,
	MAP_RANKS_DOWN
};

static inline int
map_ranks_include(enum map_ranks_class class, int status)
{
	switch (class) {
	case MAP_RANKS_UP:
		return status == PO_COMP_ST_UP || status == PO_COMP_ST_UPIN;
	case MAP_RANKS_DOWN:
		return status == PO_COMP_ST_DOWN ||
		       status == PO_COMP_ST_DOWNOUT;
	default:
		D_ASSERTF(0, "%d\n", class);
	}
}

/* Build a rank list of targets with certain status. */
static int
map_ranks_init(const struct pool_map *map, enum map_ranks_class class,
	       daos_rank_list_t *ranks)
{
	struct pool_target     *targets;
	int			ntargets;
	int			n = 0;
	int			i;
	daos_rank_t	       *rs;

	ntargets = pool_map_find_target((struct pool_map *)map, PO_COMP_ID_ALL,
					&targets);
	if (ntargets == 0) {
		D_ERROR("no targets in pool map\n");
		return -DER_IO;
	}

	for (i = 0; i < ntargets; i++)
		if (map_ranks_include(class, targets[i].ta_comp.co_status))
			n++;

	if (n == 0) {
		memset(ranks, 0, sizeof(*ranks));
		return 0;
	}

	D_ALLOC(rs, sizeof(*rs) * n);
	if (rs == NULL)
		return -DER_NOMEM;

	ranks->rl_nr.num = n;
	ranks->rl_nr.num_out = 0;
	ranks->rl_ranks = rs;

	n = 0;
	for (i = 0; i < ntargets; i++) {
		if (map_ranks_include(class, targets[i].ta_comp.co_status)) {
			D_ASSERT(n < ranks->rl_nr.num);
			ranks->rl_ranks[n] = targets[i].ta_comp.co_rank;
			n++;
		}
	}
	D_ASSERTF(n == ranks->rl_nr.num, "%d != %u\n", n, ranks->rl_nr.num);

	return 0;
}

static void
map_ranks_fini(daos_rank_list_t *ranks)
{
	if (ranks->rl_ranks != NULL) {
		D_ASSERT(ranks->rl_nr.num != 0);
		D_FREE(ranks->rl_ranks,
		       sizeof(*ranks->rl_ranks) * ranks->rl_nr.num);
	} else {
		D_ASSERT(ranks->rl_nr.num == 0);
	}
}

static int
group_create_cb(crt_group_t *grp, void *priv, int status)
{
	ABT_eventual *eventual = priv;

	if (status != 0) {
		D_ERROR("failed to create pool group: %d\n", status);
		grp = NULL;
	}
	ABT_eventual_set(*eventual, &grp, sizeof(grp));
	return 0;
}

int
ds_pool_group_create(const uuid_t pool_uuid, const struct pool_map *map,
		     crt_group_t **group)
{
	char			id[DAOS_UUID_STR_SIZE];
	daos_rank_list_t		ranks;
	ABT_eventual		eventual;
	crt_group_t	      **g;
	int			rc;

	D_DEBUG(DF_DSMS, DF_UUID"\n", DP_UUID(pool_uuid));

	uuid_unparse_lower(pool_uuid, id);

	rc = map_ranks_init(map, MAP_RANKS_UP, &ranks);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create rank list: %d\n",
			DP_UUID(pool_uuid), rc);
		D_GOTO(out, rc);
	}

	if (ranks.rl_nr.num == 0) {
		D_ERROR(DF_UUID": failed to find any up targets\n",
			DP_UUID(pool_uuid));
		D_GOTO(out_ranks, rc = -DER_IO);
	}

	rc = ABT_eventual_create(sizeof(*g), &eventual);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_ranks, rc = dss_abterr2der(rc));

	/* "!populate_now" is not implemented yet. */
	rc = crt_group_create(id, &ranks, true /* populate_now */,
			      group_create_cb, &eventual);
	if (rc != 0)
		D_GOTO(out_eventual, rc);

	rc = ABT_eventual_wait(eventual, (void **)&g);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_eventual, rc = dss_abterr2der(rc));

	if (*g == NULL)
		D_GOTO(out_eventual, rc = -DER_IO);

	*group = *g;
	rc = 0;
out_eventual:
	ABT_eventual_free(&eventual);
out_ranks:
	map_ranks_fini(&ranks);
out:
	return rc;
}

static int
group_destroy_cb(void *args, int status)
{
	ABT_eventual *eventual = args;

	ABT_eventual_set(*eventual, &status, sizeof(status));
	return 0;
}

int
ds_pool_group_destroy(crt_group_t *group)
{
	ABT_eventual	eventual;
	int	       *status;
	int		rc;

	D_DEBUG(DF_DSMS, "%s\n", group->cg_grpid);

	rc = ABT_eventual_create(sizeof(*status), &eventual);
	if (rc != ABT_SUCCESS)
		D_GOTO(out, rc = dss_abterr2der(rc));

	rc = crt_group_destroy(group, group_destroy_cb, &eventual);
	if (rc != 0)
		D_GOTO(out_eventual, rc);

	rc = ABT_eventual_wait(eventual, (void **)&status);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_eventual, rc = dss_abterr2der(rc));

	if (*status != 0)
		D_GOTO(out_eventual, rc = *status);

	rc = 0;
out_eventual:
	ABT_eventual_free(&eventual);
out:
	return rc;
}

int
ds_pool_bcast_create(crt_context_t ctx, struct ds_pool *pool,
		     enum daos_module_id module, crt_opcode_t opcode,
		     crt_rpc_t **rpc)
{
	daos_rank_list_t	excluded;
	crt_opcode_t	opc;
	int		rc;

	opc = DAOS_RPC_OPCODE(opcode, module, 1);

	ABT_rwlock_rdlock(pool->sp_lock);
	rc = map_ranks_init(pool->sp_map, MAP_RANKS_DOWN, &excluded);
	ABT_rwlock_unlock(pool->sp_lock);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create rank list: %d\n",
			DP_UUID(pool->sp_uuid), rc);
		return rc;
	}

	rc = crt_corpc_req_create(ctx, pool->sp_group,
				  excluded.rl_nr.num == 0 ? NULL : &excluded,
				  opc, NULL /* co_bulk_hdl */, NULL /* priv */,
				  0 /* flags */, 0 /* tree_topo */, rpc);

	map_ranks_fini(&excluded);
	return rc;
}

/*
 * Exclude "tgts" in "map". A new map version is generated only if actual
 * changes have been made. If "tgts_failed" is not NULL, then targets whose
 * ranks cannot be found in map are added to "tgts_failed", whose rank buffer
 * must be at least as large that of "tgts".
 */
void
ds_pool_map_exclude_targets(struct pool_map *map, daos_rank_list_t *tgts,
			    daos_rank_list_t *tgts_failed)
{
	uint32_t	version;
	int		i;
	int		nchanges = 0;
	int		rc;

	if (tgts_failed != NULL) {
		D_ASSERT(tgts_failed->rl_nr.num >= tgts->rl_nr.num);
		tgts_failed->rl_nr.num_out = 0;
	}

	version = pool_map_get_version(map) + 1;

	for (i = 0; i < tgts->rl_nr.num; i++) {
		struct pool_target *target;

		target = pool_map_find_target_by_rank(map, tgts->rl_ranks[i]);
		if (target == NULL) {
			D_DEBUG(DF_DSMS, "failed to find rank %u in map %p\n",
				tgts->rl_ranks[i], map);
			if (tgts_failed != NULL) {
				int j = tgts_failed->rl_nr.num_out;

				tgts_failed->rl_ranks[j] = tgts->rl_ranks[i];
				tgts_failed->rl_nr.num_out++;
			}
			continue;
		}

		D_ASSERTF(target->ta_comp.co_status == PO_COMP_ST_UP ||
			  target->ta_comp.co_status == PO_COMP_ST_UPIN ||
			  target->ta_comp.co_status == PO_COMP_ST_DOWN ||
			  target->ta_comp.co_status == PO_COMP_ST_DOWNOUT,
			  "%u\n", target->ta_comp.co_status);
		if (target->ta_comp.co_status != PO_COMP_ST_DOWN &&
		    target->ta_comp.co_status != PO_COMP_ST_DOWNOUT) {
			D_DEBUG(DF_DSMS, "changing rank %u to DOWN in map %p\n",
				target->ta_comp.co_rank, map);
			target->ta_comp.co_status = PO_COMP_ST_DOWN;
			target->ta_comp.co_fseq = version;
			nchanges++;
		}
	}

	/* Set the version only if actual changes have been made. */
	if (nchanges > 0) {
		D_DEBUG(DF_DSMS, "generating map %p version %u: nchanges=%d\n",
			map, version, nchanges);
		rc = pool_map_set_version(map, version);
		D_ASSERTF(rc == 0, "%d\n", rc);
	}
}
