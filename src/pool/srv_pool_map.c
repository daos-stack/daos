/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 */

#include <daos/pool_map.h>
#include "rpc.h"

#define DF_TARGET "Target[%d] (rank %u idx %u)"
#define DP_TARGET(t) t->ta_comp.co_id, t->ta_comp.co_rank, t->ta_comp.co_index

/*
 * Updates a single target by the operation given in opc
 * If something changed, *version is incremented
 * Returns 0 on success or if there's nothing to do. -DER_BUSY if the operation
 * is valid but needs to wait for rebuild to finish, -DER_INVAL if the state
 * transition is invalid
 */
int
update_one_tgt(struct pool_map *map, struct pool_target *target,
	       struct pool_domain *dom, int opc, bool evict_rank,
	       uint32_t *version, bool print_changes)
{
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
			D_INFO("Skip exclude down "DF_TARGET"\n",
			       DP_TARGET(target));
			break;
		case PO_COMP_ST_UP:
		case PO_COMP_ST_UPIN:
		case PO_COMP_ST_DRAIN:
			D_DEBUG(DF_DSMS, "change "DF_TARGET" to DOWN %p\n",
				DP_TARGET(target), map);
			target->ta_comp.co_status = PO_COMP_ST_DOWN;
			target->ta_comp.co_fseq = ++(*version);
			if (print_changes)
				D_PRINT(DF_TARGET " is down.\n",
					DP_TARGET(target));
			else
				D_INFO(DF_TARGET " is down.\n",
				       DP_TARGET(target));
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
			D_ERROR("Can't drain new "DF_TARGET"\n",
				DP_TARGET(target));
			return -DER_BUSY;
		case PO_COMP_ST_UP:
			D_ERROR("Can't drain reint "DF_TARGET"\n",
				DP_TARGET(target));
			return -DER_BUSY;
		case PO_COMP_ST_UPIN:
			D_DEBUG(DF_DSMS, "change "DF_TARGET" to DRAIN %p\n",
				DP_TARGET(target), map);
			target->ta_comp.co_status = PO_COMP_ST_DRAIN;
			target->ta_comp.co_fseq = ++(*version);
			if (print_changes)
				D_PRINT(DF_TARGET " is draining.\n",
					DP_TARGET(target));
			else
				D_INFO(DF_TARGET " is draining.\n",
				       DP_TARGET(target));
			break;
		}
		break;

	case POOL_REINT:
		switch (target->ta_comp.co_status) {
		case PO_COMP_ST_NEW:
			/* Nothing to do, already added */
			D_INFO("Can't reint new "DF_TARGET"\n",
			       DP_TARGET(target));
			return -DER_BUSY;
		case PO_COMP_ST_UP:
		case PO_COMP_ST_UPIN:
			/* Nothing to do, already added */
			D_INFO("Skip reint up "DF_TARGET"\n",
			       DP_TARGET(target));
			break;
		case PO_COMP_ST_DOWN:
		case PO_COMP_ST_DRAIN:
			D_ERROR("Can't reint rebuilding "DF_TARGET"\n",
				DP_TARGET(target));
			return -DER_BUSY;
		case PO_COMP_ST_DOWNOUT:
			D_DEBUG(DF_DSMS, "change "DF_TARGET" to UP %p\n",
				DP_TARGET(target), map);
			target->ta_comp.co_status = PO_COMP_ST_UP;
			++(*version);
			if (print_changes)
				D_PRINT(DF_TARGET " start reintegration.\n",
					DP_TARGET(target));
			else
				D_INFO(DF_TARGET " start reintegration.\n",
				       DP_TARGET(target));
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
			D_INFO("Skip ADD_IN UPIN "DF_TARGET"\n",
			       DP_TARGET(target));
			break;
		case PO_COMP_ST_DOWN:
		case PO_COMP_ST_DRAIN:
		case PO_COMP_ST_DOWNOUT:
			D_ERROR("Can't ADD_IN non-up "DF_TARGET"\n",
				DP_TARGET(target));
			return -DER_INVAL;
		case PO_COMP_ST_UP:
		case PO_COMP_ST_NEW:
			D_DEBUG(DF_DSMS, "change "DF_TARGET" to UPIN %p\n",
				DP_TARGET(target), map);
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
			D_INFO("Skip EXCLUDE_OUT DOWNOUT "DF_TARGET"\n",
			       DP_TARGET(target));
			break;
		case PO_COMP_ST_UP:
		case PO_COMP_ST_UPIN:
		case PO_COMP_ST_NEW:
			D_ERROR("Can't EXCLUDE_OUT non-down "DF_TARGET"\n",
				DP_TARGET(target));
			return -DER_INVAL;
		case PO_COMP_ST_DRAIN:
		case PO_COMP_ST_DOWN:
			D_DEBUG(DF_DSMS, "change "DF_TARGET" to DOWNOUT %p\n",
				DP_TARGET(target), map);
			target->ta_comp.co_status = PO_COMP_ST_DOWNOUT;
			(*version)++;
			if (print_changes)
				D_PRINT(DF_TARGET" is excluded.\n",
					DP_TARGET(target));
			else
				D_INFO(DF_TARGET" is excluded.\n",
				       DP_TARGET(target));
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
			int opc, bool evict_rank, uint32_t *tgt_map_ver,
			bool print_changes)
{
	uint32_t	version;
	int		i;
	int		rc;

	D_ASSERT(tgts != NULL);

	version = pool_map_get_version(map);
	if (tgt_map_ver != NULL)
		*tgt_map_ver = version;

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
				    &version, print_changes);
		if (rc != 0)
			return rc;

		if (tgt_map_ver != NULL && *tgt_map_ver < version)
			*tgt_map_ver = version;

		if (evict_rank &&
		    !(dom->do_comp.co_status & (PO_COMP_ST_DOWN |
						PO_COMP_ST_DOWNOUT)) &&
		    pool_map_node_status_match(dom, PO_COMP_ST_DOWN |
						    PO_COMP_ST_DOWNOUT)) {
			if (opc == POOL_EXCLUDE)
				dom->do_comp.co_status = PO_COMP_ST_DOWN;
			else if (opc == POOL_EXCLUDE_OUT)
				dom->do_comp.co_status = PO_COMP_ST_DOWNOUT;
			else
				D_ASSERTF(false, "evict rank by %d\n", opc);
			dom->do_comp.co_fseq = target->ta_comp.co_fseq;
			D_DEBUG(DF_DSMS, "change rank %u to DOWN\n",
				dom->do_comp.co_rank);
			version++;
		}
	}

	/* If no target is being changed, let's reset the tgt_map_ver to 0,
	 * so related ULT like rebuild/reintegrate/drain will not be scheduled.
	 */
	if (tgt_map_ver != NULL && *tgt_map_ver == pool_map_get_version(map))
		*tgt_map_ver = 0;

	/* Set the version only if actual changes have been made. */
	if (version > pool_map_get_version(map)) {
		D_DEBUG(DF_DSMS, "generating map %p version %u:\n",
			map, version);
		rc = pool_map_set_version(map, version);
		D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	}

	return 0;

}
