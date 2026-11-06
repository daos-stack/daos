/**
 * (C) Copyright 2021-2024 Intel Corporation.
 * (C) Copyright 2025 Google LLC
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 */
#include <daos/pool_map.h>
#include "rpc.h"

#define DF_MAP                 DF_UUID ": map=%p"
#define DP_MAP(pool_uuid, map) DP_UUID(pool_uuid), map

static void
update_tgt_up_to_upin(uuid_t pool_uuid, struct pool_map *map, struct pool_target *target,
		      bool print_changes, uint32_t *version)
{
	D_DEBUG(DB_MD, DF_MAP ": change " DF_TARGET " to UPIN\n", DP_MAP(pool_uuid, map),
		DP_TARGET(target));
	target->ta_comp.co_flags = 0;
	target->ta_comp.co_in_ver = ++(*version);
	target->ta_comp.co_status = PO_COMP_ST_UPIN;
	if (print_changes)
		D_PRINT(DF_MAP ": " DF_TARGET " is reintegrated.\n", DP_MAP(pool_uuid, map),
			DP_TARGET(target));
	D_INFO(DF_MAP ": " DF_TARGET " is reintegrated.\n", DP_MAP(pool_uuid, map),
	       DP_TARGET(target));
}

static void
update_tgt_down_drain_to_downout(uuid_t pool_uuid, struct pool_map *map, struct pool_target *target,
				 bool print_changes, uint32_t *version)
{
	if (target->ta_comp.co_status == PO_COMP_ST_DOWN)
		target->ta_comp.co_flags = PO_COMPF_DOWN2OUT;

	D_DEBUG(DB_MD, DF_MAP ": change " DF_TARGET " to DOWNOUT %p fseq %u\n",
		DP_MAP(pool_uuid, map), DP_TARGET(target), map, target->ta_comp.co_fseq);
	target->ta_comp.co_status = PO_COMP_ST_DOWNOUT;
	target->ta_comp.co_out_ver = ++(*version);

	if (print_changes)
		D_PRINT(DF_MAP ": " DF_TARGET " is excluded\n", DP_MAP(pool_uuid, map),
			DP_TARGET(target));
	D_INFO(DF_MAP ": " DF_TARGET " is excluded\n", DP_MAP(pool_uuid, map), DP_TARGET(target));
}

/*
 * Revert a DOWNOUT->UP reintegration back to DOWNOUT. co_fseq must be kept unchanged, as it
 * is the placement remap order key, and changing it would change the layout of all objects
 * that have shards remapped from this target. co_out_ver records the version of the status
 * change, so the reintegration task can find it is stale, see pool_map_tgts_changed_after().
 * This also applies to a creation-time DOWNOUT target, co_out_ver is not used by it.
 */
static void
update_tgt_up_revert_to_downout(uuid_t pool_uuid, struct pool_map *map, struct pool_target *target,
				uint32_t *version)
{
	D_ASSERT(pool_comp_is_downout2up(&target->ta_comp));
	D_DEBUG(DB_MD, DF_MAP ": change " DF_TARGET " to DOWNOUT fseq %u\n", DP_MAP(pool_uuid, map),
		DP_TARGET(target), target->ta_comp.co_fseq);
	target->ta_comp.co_status  = PO_COMP_ST_DOWNOUT;
	target->ta_comp.co_out_ver = ++(*version);
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
update_one_tgt(uuid_t pool_uuid, struct pool_map *map, struct pool_target *target, int opc,
	       uint32_t *version, bool print_changes)
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
			D_INFO(DF_MAP ": Skip exclude down " DF_TARGET "\n", DP_MAP(pool_uuid, map),
			       DP_TARGET(target));
			break;
		case PO_COMP_ST_DRAIN:
			/*
			 * A draining target fails, its data has to be rebuilt from the other
			 * replicas instead. It is a new failure, give it a new co_fseq the same as
			 * an UPIN target, so the drain is stale, and its Fail_reclaim deletes the
			 * data drained to the spares, the rebuild of the failure may select other
			 * spares then. The rebuilds of the components changed after the drain start
			 * are queued after the drain, so remapping it after them is safe.
			 */
			target->ta_comp.co_status = PO_COMP_ST_DOWN;
			target->ta_comp.co_fseq   = ++(*version);
			D_DEBUG(DB_MD, DF_MAP ": change " DF_TARGET " to DOWN fseq %u\n",
				DP_MAP(pool_uuid, map), DP_TARGET(target), target->ta_comp.co_fseq);
			if (print_changes)
				D_PRINT(DF_MAP ": " DF_TARGET " is down during draining.\n",
					DP_MAP(pool_uuid, map), DP_TARGET(target));
			D_INFO(DF_MAP ": " DF_TARGET " is down during draining.\n",
			       DP_MAP(pool_uuid, map), DP_TARGET(target));
			rc = 1;
			break;
		case PO_COMP_ST_UP:
			/*
			 * Excluding a DOWNOUT->UP reintegrating target, it only aborts the
			 * reintegration, so revert it to DOWNOUT and keep its co_fseq.
			 */
			if (pool_comp_is_downout2up(&target->ta_comp)) {
				update_tgt_up_revert_to_downout(pool_uuid, map, target, version);
				if (print_changes)
					D_PRINT(DF_MAP ": " DF_TARGET " is excluded during "
						       "reintegration, reverted.\n",
						DP_MAP(pool_uuid, map), DP_TARGET(target));
				D_INFO(DF_MAP ": " DF_TARGET " is excluded during reintegration, "
					      "reverted.\n",
				       DP_MAP(pool_uuid, map), DP_TARGET(target));
				rc = 1;
				break;
			}
			/*
			 * Excluding a target that is being extended (or reintegrated after it
			 * was excluded while being extended), it has never been a member of any
			 * layout, see pool_comp_is_unplaced(). Keep its co_fseq, so placement
			 * still skips it and the existing layouts are not changed, and its data
			 * is still on the original targets. co_out_ver records the version of
			 * the status change, the extension is stale then.
			 */
			if (pool_comp_is_unplaced(&target->ta_comp)) {
				D_DEBUG(DB_MD, DF_MAP ": change unplaced " DF_TARGET " to DOWN\n",
					DP_MAP(pool_uuid, map), DP_TARGET(target));
				target->ta_comp.co_status  = PO_COMP_ST_DOWN;
				target->ta_comp.co_out_ver = ++(*version);
				if (print_changes)
					D_PRINT(DF_MAP ": " DF_TARGET " is down during "
						       "extension.\n",
						DP_MAP(pool_uuid, map), DP_TARGET(target));
				D_INFO(DF_MAP ": " DF_TARGET " is down during extension.\n",
				       DP_MAP(pool_uuid, map), DP_TARGET(target));
				rc = 1;
				break;
			}
			/* fallthrough */
		case PO_COMP_ST_UPIN:
			D_DEBUG(DB_MD, DF_MAP ": change " DF_TARGET " to DOWN\n",
				DP_MAP(pool_uuid, map), DP_TARGET(target));
			target->ta_comp.co_status = PO_COMP_ST_DOWN;
			target->ta_comp.co_fseq = ++(*version);
			if (print_changes)
				D_PRINT(DF_MAP ": " DF_TARGET " is down.\n", DP_MAP(pool_uuid, map),
					DP_TARGET(target));
			D_INFO(DF_MAP ": " DF_TARGET " is down.\n", DP_MAP(pool_uuid, map),
			       DP_TARGET(target));
			rc = 1;
			break;
		case PO_COMP_ST_NEW:
			/* Nothing to do, it has no data and is not used by placement */
			D_INFO(DF_MAP ": Skip exclude new " DF_TARGET "\n", DP_MAP(pool_uuid, map),
			       DP_TARGET(target));
			break;
		}
		break;
	case MAP_DRAIN:
		switch (target->ta_comp.co_status) {
		case PO_COMP_ST_DOWN:
		case PO_COMP_ST_DRAIN:
		case PO_COMP_ST_DOWNOUT:
			/* Nothing to do, already excluded / draining */
			D_INFO(DF_MAP ": Skip drain down " DF_TARGET "\n", DP_MAP(pool_uuid, map),
			       DP_TARGET(target));
			break;
		case PO_COMP_ST_NEW:
			D_ERROR(DF_MAP ": Can't drain new " DF_TARGET "\n", DP_MAP(pool_uuid, map),
				DP_TARGET(target));
			return -DER_BUSY;
		case PO_COMP_ST_UP:
			D_ERROR(DF_MAP ": Can't drain reint " DF_TARGET "\n",
				DP_MAP(pool_uuid, map), DP_TARGET(target));
			return -DER_BUSY;
		case PO_COMP_ST_UPIN:
			D_DEBUG(DB_MD, DF_MAP ": change " DF_TARGET " to DRAIN\n",
				DP_MAP(pool_uuid, map), DP_TARGET(target));
			target->ta_comp.co_status = PO_COMP_ST_DRAIN;
			target->ta_comp.co_fseq = ++(*version);
			if (print_changes)
				D_PRINT(DF_MAP ": " DF_TARGET " is draining.\n",
					DP_MAP(pool_uuid, map), DP_TARGET(target));
			D_INFO(DF_MAP ": " DF_TARGET " is draining.\n", DP_MAP(pool_uuid, map),
			       DP_TARGET(target));
			rc = 1;
			break;
		}
		break;
	case MAP_REINT:
		switch (target->ta_comp.co_status) {
		case PO_COMP_ST_NEW:
			/* Nothing to do, already added */
			D_INFO(DF_MAP ": Can't reint new " DF_TARGET "\n", DP_MAP(pool_uuid, map),
			       DP_TARGET(target));
			return -DER_BUSY;
		case PO_COMP_ST_UP:
		case PO_COMP_ST_UPIN:
			/* Nothing to do, already added */
			D_INFO(DF_MAP ": Skip reint up " DF_TARGET "\n", DP_MAP(pool_uuid, map),
			       DP_TARGET(target));
			break;
		case PO_COMP_ST_DRAIN:
			D_ERROR(DF_MAP ": Can't reint rebuilding " DF_TARGET "\n",
				DP_MAP(pool_uuid, map), DP_TARGET(target));
			return -DER_BUSY;
		case PO_COMP_ST_DOWN:
			target->ta_comp.co_flags |= PO_COMPF_DOWN2UP;
		case PO_COMP_ST_DOWNOUT:
			D_DEBUG(DB_MD, DF_MAP ": change " DF_TARGET " to UP\n",
				DP_MAP(pool_uuid, map), DP_TARGET(target));
			target->ta_comp.co_status = PO_COMP_ST_UP;
			target->ta_comp.co_in_ver = ++(*version);
			if (print_changes)
				D_PRINT(DF_MAP ": " DF_TARGET " start reintegration.\n",
					DP_MAP(pool_uuid, map), DP_TARGET(target));
			D_INFO(DF_MAP ": " DF_TARGET " start reintegration.\n",
			       DP_MAP(pool_uuid, map), DP_TARGET(target));
			rc = 1;
			break;
		}
		break;
	case MAP_EXTEND:
		switch (target->ta_comp.co_status) {
		case PO_COMP_ST_NEW:
			target->ta_comp.co_status = PO_COMP_ST_UP;
			target->ta_comp.co_in_ver = ++(*version);
			D_DEBUG(DB_MD, DF_MAP ": change " DF_TARGET " to UP\n",
				DP_MAP(pool_uuid, map), DP_TARGET(target));
			if (print_changes)
				D_PRINT(DF_MAP ": " DF_TARGET " is being extended.\n",
					DP_MAP(pool_uuid, map), DP_TARGET(target));
			D_INFO(DF_MAP ": " DF_TARGET " is being extended.\n",
			       DP_MAP(pool_uuid, map), DP_TARGET(target));
			rc = 1;
			break;
		case PO_COMP_ST_UP:
		case PO_COMP_ST_UPIN:
			/* Nothing to do, already added */
			D_INFO(DF_MAP ": Skip extend " DF_TARGET "\n", DP_MAP(pool_uuid, map),
			       DP_TARGET(target));
			break;
		case PO_COMP_ST_DOWN:
		case PO_COMP_ST_DRAIN:
		case PO_COMP_ST_DOWNOUT:
			D_ERROR(DF_MAP ": Can't extend " DF_TARGET "\n", DP_MAP(pool_uuid, map),
				DP_TARGET(target));
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
			/* Nothing to do */
			D_INFO(DF_MAP ": Skip ADD_IN " DF_TARGET "\n", DP_MAP(pool_uuid, map),
			       DP_TARGET(target));
			break;
		case PO_COMP_ST_UP:
			update_tgt_up_to_upin(pool_uuid, map, target, print_changes, version);
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
			/* Nothing to do */
			D_INFO(DF_MAP ": Skip EXCLUDE_OUT " DF_TARGET "\n", DP_MAP(pool_uuid, map),
			       DP_TARGET(target));
			break;
		case PO_COMP_ST_DOWN:
		case PO_COMP_ST_DRAIN:
			update_tgt_down_drain_to_downout(pool_uuid, map, target, print_changes,
							 version);
			rc = 1;
			break;
		}
		break;
	case MAP_FINISH_REBUILD:
		switch (target->ta_comp.co_status) {
		case PO_COMP_ST_UPIN:
		case PO_COMP_ST_DOWNOUT:
		case PO_COMP_ST_NEW:
			/* Nothing to do */
			D_INFO(DF_MAP ": Skip FINISH_REBUILD " DF_TARGET "\n",
			       DP_MAP(pool_uuid, map), DP_TARGET(target));
			break;
		case PO_COMP_ST_DOWN:
		case PO_COMP_ST_DRAIN:
			update_tgt_down_drain_to_downout(pool_uuid, map, target, print_changes,
							 version);
			rc = 1;
			break;
		case PO_COMP_ST_UP:
			update_tgt_up_to_upin(pool_uuid, map, target, print_changes, version);
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
			/* Nothing to do. DOWN can not be revert. */
			D_INFO(DF_MAP ": Skip REVERT_REBUILD " DF_TARGET "\n",
			       DP_MAP(pool_uuid, map), DP_TARGET(target));
			break;
		case PO_COMP_ST_DRAIN: /* revert DRAIN to UPIN */
			target->ta_comp.co_status = PO_COMP_ST_UPIN;
			target->ta_comp.co_fseq = 0;
			++(*version);
			rc = 1;
			break;
		case PO_COMP_ST_UP:
			/*
			 * A UP target can come from three prior states, distinguished by the
			 * relationship between co_fseq and co_ver (never touched by MAP_REINT
			 * or MAP_EXTEND, so this is stable):
			 *
			 *   co_fseq <  co_ver : fresh MAP_EXTEND (NEW->UP). fseq was
			 *                       initialized to 1 in gen_pool_buf() and co_ver
			 *                       is the extend map version, which is > 1.
			 *                       Revert to NEW. If DOWN2UP/DOWN2OUT is set, it
			 *                       was excluded while being extended and is now
			 *                       reintegrated, revert to DOWN/DOWNOUT, see
			 *                       pool_comp_is_unplaced().
			 *   co_fseq == co_ver : baseline creation-DOWNOUT reintegration
			 *                       (see pool_comp_is_creation_downout()).
			 *                       Revert to DOWNOUT, preserving the invariant.
			 *   co_fseq >  co_ver : real UPIN->DOWN/DRAIN->DOWNOUT->UP or
			 *                       UPIN->DOWN->UP (DOWN2UP). MAP_EXCLUDE bumped
			 *                       co_fseq strictly above co_ver. Revert to
			 *                       DOWN with a new co_fseq if DOWN2UP flag is
			 *                       set, same as MAP_EXCLUDE, otherwise DOWNOUT.
			 *
			 * Do NOT rely on co_in_ver here: both MAP_EXTEND and MAP_REINT bump
			 * it on the UP transition, so it cannot distinguish these cases.
			 */
			if (target->ta_comp.co_fseq < target->ta_comp.co_ver &&
			    (target->ta_comp.co_flags & (PO_COMPF_DOWN2UP | PO_COMPF_DOWN2OUT))) {
				/*
				 * Reintegrating a target that was excluded while being extended,
				 * see pool_comp_is_unplaced(), back to DOWN/DOWNOUT with its
				 * co_fseq kept, so placement still skips it.
				 */
				target->ta_comp.co_status =
				    (target->ta_comp.co_flags & PO_COMPF_DOWN2UP)
					? PO_COMP_ST_DOWN
					: PO_COMP_ST_DOWNOUT;
				D_DEBUG(DB_MD, DF_MAP ": revert unplaced " DF_TARGET " to %s\n",
					DP_MAP(pool_uuid, map), DP_TARGET(target),
					pool_map_status2name(target->ta_comp.co_status));
				target->ta_comp.co_out_ver = ++(*version);
			} else if (target->ta_comp.co_fseq < target->ta_comp.co_ver) {
				D_DEBUG(DB_MD, DF_MAP ": change " DF_TARGET " to NEW\n",
					DP_MAP(pool_uuid, map), DP_TARGET(target));
				target->ta_comp.co_status = PO_COMP_ST_NEW;
				target->ta_comp.co_in_ver = 0;
				++(*version);
			} else if (target->ta_comp.co_fseq == target->ta_comp.co_ver) {
				D_DEBUG(DB_MD,
					DF_MAP ": revert baseline reint " DF_TARGET " to DOWNOUT\n",
					DP_MAP(pool_uuid, map), DP_TARGET(target));
				target->ta_comp.co_status = PO_COMP_ST_DOWNOUT;
				/*
				 * co_fseq == co_ver is kept, so subsequent reint attempts behave
				 * the same as the very first attempt. co_out_ver records the
				 * version of the status change, as MAP_EXCLUDE does, so the
				 * rebuilds that may have migrated data to it can find they are
				 * stale, see pool_map_tgts_dropped_after(). It is not used by the
				 * RF check, a creation-time DOWNOUT is not a failure.
				 */
				target->ta_comp.co_out_ver = ++(*version);
			} else {
				if (target->ta_comp.co_flags & PO_COMPF_DOWN2UP) {
					/*
					 * It is a failure again. While it was UP, the other
					 * rebuilds may have migrated data to it, and the other
					 * DOWN targets failed after its old co_fseq may have
					 * been rebuilt and turned into DOWNOUT. Renew co_fseq,
					 * so it is remapped after them, and its own rebuild
					 * covers the data on it.
					 */
					target->ta_comp.co_status = PO_COMP_ST_DOWN;
					target->ta_comp.co_fseq   = *version + 1;
					D_DEBUG(DB_MD,
						DF_MAP ": change " DF_TARGET " to DOWN fseq %u\n",
						DP_MAP(pool_uuid, map), DP_TARGET(target),
						target->ta_comp.co_fseq);
				} else {
					D_DEBUG(DB_MD,
						DF_MAP ": change " DF_TARGET
						" to DOWNOUT fseq %u\n",
						DP_MAP(pool_uuid, map), DP_TARGET(target),
						target->ta_comp.co_fseq);
					target->ta_comp.co_status = PO_COMP_ST_DOWNOUT;
				}
				target->ta_comp.co_out_ver = ++(*version);
			}
			if (print_changes)
				D_PRINT(DF_MAP ": " DF_TARGET " is reverted.\n",
					DP_MAP(pool_uuid, map), DP_TARGET(target));
			D_INFO(DF_MAP ": " DF_TARGET " is reverted.\n", DP_MAP(pool_uuid, map),
			       DP_TARGET(target));
			rc = 1;
			break;
		}
		break;
	default:
		D_ERROR(DF_MAP ": Invalid pool target operation: %d\n", DP_MAP(pool_uuid, map),
			opc);
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
						    *version, &updated, false);
		break;
	case MAP_EXTEND:
		if (dom->do_comp.co_status == PO_COMP_ST_NEW)
			update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id, PO_COMP_ST_UP,
						    *version, &updated, false);
		break;
	case MAP_EXCLUDE:
		/* Only change the dom status if it is from SWIM eviction */
		if (!exclude_rank ||
		    (dom->do_comp.co_status & (PO_COMP_ST_DOWN | PO_COMP_ST_DOWNOUT)) ||
		    !pool_map_node_status_match(dom, PO_COMP_ST_DOWN | PO_COMP_ST_DOWNOUT))
			break;
		/*
		 * Excluding a DOWNOUT->UP reintegrating domain whose targets are all reverted to
		 * DOWNOUT, revert it to DOWNOUT and keep its fseq, same as MAP_REVERT_REBUILD.
		 */
		if (pool_comp_is_downout2up(&dom->do_comp) &&
		    pool_map_node_status_match(dom, PO_COMP_ST_DOWNOUT))
			update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id, PO_COMP_ST_DOWNOUT,
						    *version, &updated, true);
		/*
		 * Excluding a domain that is being extended, keep its fseq, so placement still
		 * skips it, see pool_comp_is_unplaced().
		 */
		else if (pool_comp_is_unplaced(&dom->do_comp))
			update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id, PO_COMP_ST_DOWN,
						    *version, &updated, true);
		else
			update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id, PO_COMP_ST_DOWN,
						    *version, &updated, false);
		break;
	case MAP_FINISH_REBUILD:
		if (dom->do_comp.co_status == PO_COMP_ST_UP)
			update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id, PO_COMP_ST_UPIN,
						    *version, &updated, false);
		else if (dom->do_comp.co_status == PO_COMP_ST_DOWN && exclude_rank)
			update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id, PO_COMP_ST_DOWNOUT,
						    *version, &updated, false);
		break;
	case MAP_REVERT_REBUILD:
		if (dom->do_comp.co_status == PO_COMP_ST_UP) {
			/*
			 * See update_one_tgt() MAP_REVERT_REBUILD PO_COMP_ST_UP for the tri-
			 * partite discriminator on (co_fseq, co_ver).
			 */
			if (dom->do_comp.co_fseq < dom->do_comp.co_ver &&
			    (dom->do_comp.co_flags & PO_COMPF_DOWN2UP))
				update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id,
							    PO_COMP_ST_DOWN, *version, &updated,
							    true);
			else if (dom->do_comp.co_fseq < dom->do_comp.co_ver &&
				 (dom->do_comp.co_flags & PO_COMPF_DOWN2OUT))
				update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id,
							    PO_COMP_ST_DOWNOUT, *version, &updated,
							    true);
			else if (dom->do_comp.co_fseq < dom->do_comp.co_ver)
				update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id, PO_COMP_ST_NEW,
							    *version, &updated, true);
			else if (dom->do_comp.co_fseq == dom->do_comp.co_ver)
				update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id,
							    PO_COMP_ST_DOWNOUT, *version, &updated,
							    true);
			/* a new failure, renew its fseq, see update_one_tgt() */
			else if (dom->do_comp.co_flags & PO_COMPF_DOWN2UP)
				update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id,
							    PO_COMP_ST_DOWN, *version, &updated,
							    false);
			else
				update_dom_status_by_tgt_id(map, tgt->ta_comp.co_id,
							    PO_COMP_ST_DOWNOUT, *version, &updated,
							    true);
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
 * changes have been made. The pool_uuid parameter may be NULL.
 */
int
ds_pool_map_tgts_update(uuid_t pool_uuid, struct pool_map *map, struct pool_target_id_list *tgts,
			int opc, bool exclude_rank, uint32_t *tgt_map_ver, bool print_changes)
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
			D_ERROR(DF_MAP ": Got request to change nonexistent target %u\n",
				DP_MAP(pool_uuid, map), tgts->pti_ids[i].pti_id);
			return -DER_NONEXIST;
		}

		dom = pool_map_find_dom_by_rank(map, target->ta_comp.co_rank);
		if (dom == NULL) {
			D_ERROR(DF_MAP ": Got request to change nonexistent rank %u\n",
				DP_MAP(pool_uuid, map), target->ta_comp.co_rank);
			return -DER_NONEXIST;
		}

		rc = update_one_tgt(pool_uuid, map, target, opc, &version, print_changes);
		if (rc < 0)
			return rc;

		/* if the rank status does not need to change */
		if (rc == 0 && !exclude_rank) {
			D_DEBUG(DB_MD, DF_MAP ": skip target " DF_TARGET "\n",
				DP_MAP(pool_uuid, map), DP_TARGET(target));
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
		D_DEBUG(DB_MD, DF_MAP ": generating version %u:\n", DP_MAP(pool_uuid, map),
			version);
		rc = pool_map_set_version(map, version);
		D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	}

	return 0;
}
