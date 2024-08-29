/**
 * (C) Copyright 2021-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 */
#include <daos/pool_map.h>
#include "rpc.h"

static void
update_tgt_up_to_upin(struct pool_map *map, struct pool_target *target, bool print_changes,
		      uint32_t *version)
{
	D_DEBUG(DB_MD, "change "DF_TARGET" to UPIN %p\n", DP_TARGET(target), map);
	target->ta_comp.co_flags = 0;
	target->ta_comp.co_in_ver = ++(*version);
	target->ta_comp.co_status = PO_COMP_ST_UPIN;
	if (print_changes)
		D_PRINT(DF_TARGET " is reintegrated.\n", DP_TARGET(target));
	else
		D_INFO(DF_TARGET " is reintegrated.\n", DP_TARGET(target));
}

static void
update_tgt_down_drain_to_downout(struct pool_map *map, struct pool_target *target,
				 bool print_changes, uint32_t *version)
{
	if (target->ta_comp.co_status == PO_COMP_ST_DOWN)
		target->ta_comp.co_flags = PO_COMPF_DOWN2OUT;

	D_DEBUG(DB_MD, "change "DF_TARGET" to DOWNOUT %p fseq %u\n",
		DP_TARGET(target), map, target->ta_comp.co_fseq);
	target->ta_comp.co_status = PO_COMP_ST_DOWNOUT;
	target->ta_comp.co_out_ver = ++(*version);

	if (print_changes)
		D_PRINT(DF_TARGET" is excluded\n", DP_TARGET(target));
	else
		D_INFO(DF_TARGET" is excluded\n", DP_TARGET(target));
}

/*
 * Updates a single target by the operation given in opc
 * If something changed, *version is incremented
 *
 * 1: if the target status is updated successfully.
 * 0: if there's nothing to do.
 * < 0: failure happened.
 */
static int
update_one_tgt(struct pool_map *map, struct pool_target *target,
	       int opc, uint32_t *version, bool print_changes)
{
	int rc = 0;

	D_ASSERTF(target->ta_comp.co_status == PO_COMP_ST_UP ||
		  target->ta_comp.co_status == PO_COMP_ST_NEW ||
		  target->ta_comp.co_status == PO_COMP_ST_UPIN ||
		  target->ta_comp.co_status == PO_COMP_ST_DOWN ||
		  target->ta_comp.co_status == PO_COMP_ST_DRAIN ||
		  target->ta_comp.co_status == PO_COMP_ST_DOWNOUT,
		  "%u\n", target->ta_comp.co_status);

	switch (opc) {
	case MAP_EXCLUDE:
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
			D_DEBUG(DB_MD, "change "DF_TARGET" to DOWN %p\n",
				DP_TARGET(target), map);
			target->ta_comp.co_status = PO_COMP_ST_DOWN;
			target->ta_comp.co_fseq = ++(*version);
			if (print_changes)
				D_PRINT(DF_TARGET " is down.\n",
					DP_TARGET(target));
			else
				D_INFO(DF_TARGET " is down.\n",
				       DP_TARGET(target));
			rc = 1;
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
	case MAP_DRAIN:
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
			D_DEBUG(DB_MD, "change "DF_TARGET" to DRAIN %p\n",
				DP_TARGET(target), map);
			target->ta_comp.co_status = PO_COMP_ST_DRAIN;
			target->ta_comp.co_fseq = ++(*version);
			if (print_changes)
				D_PRINT(DF_TARGET " is draining.\n",
					DP_TARGET(target));
			else
				D_INFO(DF_TARGET " is draining.\n",
				       DP_TARGET(target));
			rc = 1;
			break;
		}
		break;
	case MAP_REINT:
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
		case PO_COMP_ST_DRAIN:
			D_ERROR("Can't reint rebuilding "DF_TARGET"\n",
				DP_TARGET(target));
			return -DER_BUSY;
		case PO_COMP_ST_DOWN:
			target->ta_comp.co_flags |= PO_COMPF_DOWN2UP;
		case PO_COMP_ST_DOWNOUT:
			D_DEBUG(DB_MD, "change "DF_TARGET" to UP %p\n",
				DP_TARGET(target), map);
			target->ta_comp.co_status = PO_COMP_ST_UP;
			target->ta_comp.co_in_ver = ++(*version);
			if (print_changes)
				D_PRINT(DF_TARGET " start reintegration.\n",
					DP_TARGET(target));
			else
				D_INFO(DF_TARGET " start reintegration.\n",
				       DP_TARGET(target));
			rc = 1;
			break;
		}
		break;
	case MAP_EXTEND:
		switch (target->ta_comp.co_status) {
		case PO_COMP_ST_NEW:
			target->ta_comp.co_status = PO_COMP_ST_UP;
			target->ta_comp.co_in_ver = ++(*version);
			D_DEBUG(DB_MD, "change "DF_TARGET" to UP %p\n",
				DP_TARGET(target), map);
			if (print_changes)
				D_PRINT(DF_TARGET " is being extended.\n",
					DP_TARGET(target));
			else
				D_INFO(DF_TARGET " is being extended.\n",
				       DP_TARGET(target));
			rc = 1;
			break;
		case PO_COMP_ST_UP:
		case PO_COMP_ST_UPIN:
			/* Nothing to do, already added */
			D_INFO("Skip extend "DF_TARGET"\n", DP_TARGET(target));
			break;
		case PO_COMP_ST_DOWN:
		case PO_COMP_ST_DRAIN:
		case PO_COMP_ST_DOWNOUT:
			D_ERROR("Can't extend "DF_TARGET"\n", DP_TARGET(target));
			return -DER_BUSY;
		}
		break;
	case MAP_ADD_IN:
		switch (target->ta_comp.co_status) {
		case PO_COMP_ST_UPIN:
		case PO_COMP_ST_DOWNOUT:
		case PO_COMP_ST_DOWN:
		case PO_COMP_ST_DRAIN:
		case PO_COMP_ST_NEW:
			/* Nothing to do, already UPIN */
			D_INFO("Skip ADD_IN UPIN "DF_TARGET"\n",
			       DP_TARGET(target));
			break;
		case PO_COMP_ST_UP:
			update_tgt_up_to_upin(map, target, print_changes, version);
			rc = 1;
			break;
		}
		break;
	case MAP_EXCLUDE_OUT:
		switch (target->ta_comp.co_status) {
		case PO_COMP_ST_UPIN:
		case PO_COMP_ST_DOWNOUT:
		case PO_COMP_ST_NEW:
		case PO_COMP_ST_UP:
			/* Nothing to do, already UPIN */
			D_INFO("Skip ADD_IN UPIN "DF_TARGET"\n",
			       DP_TARGET(target));
			break;
		case PO_COMP_ST_DOWN:
		case PO_COMP_ST_DRAIN:
			update_tgt_down_drain_to_downout(map, target, print_changes, version);
			rc = 1;
			break;
		}
		break;
	case MAP_FINISH_REBUILD:
		switch (target->ta_comp.co_status) {
		case PO_COMP_ST_UPIN:
		case PO_COMP_ST_DOWNOUT:
		case PO_COMP_ST_NEW:
			/* Nothing to do, already UPIN */
			D_INFO("Skip ADD_IN UPIN "DF_TARGET"\n",
			       DP_TARGET(target));
			break;
		case PO_COMP_ST_DOWN:
		case PO_COMP_ST_DRAIN:
			update_tgt_down_drain_to_downout(map, target, print_changes, version);
			rc = 1;
			break;
		case PO_COMP_ST_UP:
			update_tgt_up_to_upin(map, target, print_changes, version);
			rc = 1;
			break;
		}
		break;
	case MAP_REVERT_REBUILD:
		switch (target->ta_comp.co_status) {
		case PO_COMP_ST_UPIN:
		case PO_COMP_ST_DOWNOUT:
		case PO_COMP_ST_DOWN:
		case PO_COMP_ST_NEW:
			/* Nothing to do, already UPIN and DOWNOUT. DOWN can not be revert. */
			D_INFO("Skip ADD_IN UPIN DOWN "DF_TARGET"\n",
			       DP_TARGET(target));
			break;
		case PO_COMP_ST_DRAIN: /* revert DRAIN to UPIN */
			target->ta_comp.co_status = PO_COMP_ST_UPIN;
			target->ta_comp.co_fseq = 0;
			++(*version);
			rc = 1;
			break;
		case PO_COMP_ST_UP:
			if (target->ta_comp.co_fseq == 1) {
				D_DEBUG(DB_MD, "change "DF_TARGET" to NEW %p\n",
					DP_TARGET(target), map);
				target->ta_comp.co_status = PO_COMP_ST_NEW;
				target->ta_comp.co_in_ver = 0;
				++(*version);
			} else {
				D_DEBUG(DB_MD, "change "DF_TARGET" to DOWNOUT %p fseq %u\n",
					DP_TARGET(target), map, target->ta_comp.co_fseq);
				if (target->ta_comp.co_flags & PO_COMPF_DOWN2UP)
					target->ta_comp.co_status = PO_COMP_ST_DOWN;
				else
					target->ta_comp.co_status = PO_COMP_ST_DOWNOUT;
				target->ta_comp.co_out_ver = ++(*version);
			}
			if (print_changes)
				D_PRINT(DF_TARGET" is excluded.\n", DP_TARGET(target));
			else
				D_INFO(DF_TARGET" is excluded.\n", DP_TARGET(target));
			rc = 1;
			break;
		}
		break;
	default:
		D_ERROR("Invalid pool target operation: %d\n", opc);
		D_ASSERT(0);
	}

	return rc;
}

static void
update_one_dom(struct pool_map *map, struct pool_domain *dom, struct pool_target *tgt,
	       int opc, bool exclude_rank, uint32_t *version)
{
	bool updated = false;

	switch(opc) {
	case MAP_REINT:
		if (dom->do_comp.co_status == PO_COMP_ST_DOWNOUT ||
		    dom->do_comp.co_status == PO_COMP_ST_DOWN)
			update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id, PO_COMP_ST_UP,
						    *version, &updated);
		break;
	case MAP_EXTEND:
		if (dom->do_comp.co_status == PO_COMP_ST_NEW)
			update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id, PO_COMP_ST_UP,
						    *version, &updated);
		break;
	case MAP_EXCLUDE:
		/* Only change the dom status if it is from SWIM eviction */
		if (exclude_rank &&
		    !(dom->do_comp.co_status & (PO_COMP_ST_DOWN | PO_COMP_ST_DOWNOUT)) &&
		    pool_map_node_status_match(dom, PO_COMP_ST_DOWN | PO_COMP_ST_DOWNOUT))
			update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id, PO_COMP_ST_DOWN,
						    *version, &updated);
		break;
	case MAP_FINISH_REBUILD:
		if (dom->do_comp.co_status == PO_COMP_ST_UP)
			update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id, PO_COMP_ST_UPIN,
						    *version, &updated);
		else if (dom->do_comp.co_status == PO_COMP_ST_DOWN && exclude_rank)
			update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id, PO_COMP_ST_DOWNOUT,
						    *version, &updated);
		break;
	case MAP_REVERT_REBUILD:
		if (dom->do_comp.co_status == PO_COMP_ST_UP) {
			if (dom->do_comp.co_fseq == 1)
				update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id, PO_COMP_ST_NEW,
							    *version, &updated);
			else if (dom->do_comp.co_flags == PO_COMPF_DOWN2UP)
				update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id,
							    PO_COMP_ST_DOWN, *version, &updated);
			else
				update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id,
							    PO_COMP_ST_DOWNOUT, *version, &updated);
		}
		break;
	default:
		break;
	}

	if (updated)
		(*version)++;
}

/*
 * Update "tgts" in "map". A new map version is generated only if actual
 * changes have been made.
 */
int
ds_pool_map_tgts_update(struct pool_map *map, struct pool_target_id_list *tgts,
			int opc, bool exclude_rank, uint32_t *tgt_map_ver,
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
			D_ERROR("Got request to change nonexistent target %u"
				" in map %p\n",
				tgts->pti_ids[i].pti_id, map);
			return -DER_NONEXIST;
		}

		dom = pool_map_find_dom_by_rank(map, target->ta_comp.co_rank);
		if (dom == NULL) {
			D_ERROR("Got request to change nonexistent rank %u"
				" in map %p\n",
				target->ta_comp.co_rank, map);
			return -DER_NONEXIST;
		}

		rc = update_one_tgt(map, target, opc, &version, print_changes);
		if (rc < 0)
			return rc;

		/* if the target status does not need to change */
		if (rc == 0 && !exclude_rank) {
			D_DEBUG(DB_MD, "skip target "DF_TARGET"\n", DP_TARGET(target));
			continue;
		}

		update_one_dom(map, dom, target, opc, exclude_rank, &version);
		if (tgt_map_ver != NULL && *tgt_map_ver < version)
			*tgt_map_ver = version;
	}

	/* If no target is being changed, let's reset the tgt_map_ver to 0,
	 * so related ULT like rebuild/reintegrate/drain will not be scheduled.
	 */
	if (tgt_map_ver != NULL && *tgt_map_ver == pool_map_get_version(map))
		*tgt_map_ver = 0;

	/* Set the version only if actual changes have been made. */
	if (version > pool_map_get_version(map)) {
		D_DEBUG(DB_MD, "generating map %p version %u:\n",
			map, version);
		rc = pool_map_set_version(map, version);
		D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	}

	return 0;
}
