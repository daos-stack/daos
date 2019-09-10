/**
 *
 * (C) Copyright 2016-2019 Intel Corporation.
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
 * src/placement/jump_map.c
 */
#define D_LOGFAC        DD_FAC(placement)

#include "pl_map.h"
#include <inttypes.h>
#include <daos/pool_map.h>
#include <isa-l.h>

/**
 * Contains information related to object layout size.
 */
struct jm_obj_placement {
	unsigned int    jmop_grp_size;
	unsigned int    jmop_grp_nr;
};

/**
 * jump_map Placement map structure used to place objects.
 * The map is returned as a struct pl_map and then converted back into a
 * pl_jump_map once passed from the caller into the object placement
 * functions.
 */
struct pl_jump_map {
	/** placement map interface */
	struct pl_map   jmp_map;
	/* Total size of domain type specified during map creation*/
	unsigned int    jmp_domain_nr;
};

struct down_shard {
	d_list_t        ds_list;
	struct pool_target  *target_location;
};

/**
 * Jump Consistent Hash Algorithm that provides a bucket location
 * for the given key. This algorithm hashes a minimal (1/n) number
 * of keys to a new bucket when extending the number of buckets.
 *
 * \param[in]   key             A unique key representing the object that
 *                              will be placed in the bucket.
 * \param[in]   num_buckets     The total number of buckets the hashing
 *                              algorithm can choose from.
 *
 * \return                      Returns an index ranging from 0 to
 *                              num_buckets representing the bucket
 *                              the given key hashes to.
 */
static inline uint32_t
jump_consistent_hash(uint64_t key, uint32_t num_buckets)
{
	int64_t z = -1;
	int64_t y = 0;

	while (y < num_buckets) {
		z = y;
		key = key * 2862933555777941757ULL + 1;
		y = (z + 1) * ((double)(1LL << 31) /
			       ((double)((key >> 33) + 1)));
	}
	return z;
}

/**
 * This is useful for jump_map placement to pseudorandomly permute input keys
 * that are similar to each other. This dramatically improves the even-ness of
 * the distribution of output placements.
 */
static inline uint64_t
crc(uint64_t data, uint32_t init_val)
{
	return crc64_ecma_refl(init_val, (uint8_t *)&data, sizeof(data));
}

/**
 * This function gets the replication and size requirements and then
 * stores those requirements into a obj_placement  struct for usage during
 * layout creation.
 *
 * \param[in] jmap      The placement map for jump_map Placement, used for
 *                      retrieving domain requirements for layout.
 * \param[in] md        Object metadata used for retrieve information
 *                      about the object class.
 * \param[in] shard_md  Shard metadata used for determining the group number
 * \param[out] jmop     Stores layout requirements for use later in placement.
 *
 * \return              Return code, 0 for success and an error code if the
 *                      layout requirements could not be determined / satisfied.
 */
static int
jm_obj_placement_get(struct pl_jump_map *jmap, struct daos_obj_md *md,
		     struct daos_obj_shard_md *shard_md,
		     struct jm_obj_placement *jmop)
{
	struct daos_oclass_attr *oc_attr;
	struct pool_domain      *root;
	daos_obj_id_t           oid;
	int                     rc;

	/* Get the Object ID and the Object class */
	oid = md->omd_id;
	oc_attr = daos_oclass_attr_find(oid);

	if (oc_attr == NULL) {
		D_ERROR("Can not find obj class, invlaid oid="DF_OID"\n",
			DP_OID(oid));
		return -DER_INVAL;
	}

	rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap, PO_COMP_TP_ROOT,
				  PO_COMP_ID_ALL, &root);
	D_ASSERT(rc == 1);

	rc = op_get_grp_size(jmap->jmp_domain_nr, &jmop->jmop_grp_size, oid);
	if (rc)
		return rc;

	if (shard_md == NULL) {
		unsigned int grp_max = root->do_target_nr / jmop->jmop_grp_size;

		if (grp_max == 0)
			grp_max = 1;

		jmop->jmop_grp_nr = daos_oclass_grp_nr(oc_attr, md);
		if (jmop->jmop_grp_nr > grp_max)
			jmop->jmop_grp_nr = grp_max;
	} else {
		jmop->jmop_grp_nr = 1;
	}

	D_ASSERT(jmop->jmop_grp_nr > 0);
	D_ASSERT(jmop->jmop_grp_size > 0);

	D_DEBUG(DB_PL,
		"obj="DF_OID"/ grp_size=%u grp_nr=%d\n",
		DP_OID(oid), jmop->jmop_grp_size, jmop->jmop_grp_nr);

	return 0;
}

/**
 * Given a @jmop and target determine if there exists a spare target
 * that satisfies the layout requirements. This will return false if
 * there are no available domains of type jmp_domain_nr left.
 *
 * \param[in] jmap      The currently used placement map.
 * \param[in] jmop      Struct containing layout group size and number.
 *
 * \return              True if there exists a spare, false otherwise.
 */
static bool
jump_map_remap_next_spare(struct pl_jump_map *jmap,
			  struct jm_obj_placement *jmop)
{
	D_ASSERTF(jmop->jmop_grp_size <= jmap->jmp_domain_nr,
		  "grp_size: %u > domain_nr: %u\n",
		  jmop->jmop_grp_size, jmap->jmp_domain_nr);

	if (jmop->jmop_grp_size == jmap->jmp_domain_nr &&
	    jmop->jmop_grp_size > 1)
		return false;

	return true;
}

/**
 * This function converts a generic pl_map pointer into the
 * proper placement map. This function assumes that the original
 * map was allocated as a pl_jump_map with a pl_map as it's
 * first member.
 *
 * \param[in]   *map    A pointer to a pl_map which is the first
 *                      member of the specific placement map we're
 *                      converting to.
 *
 * \return      A pointer to the full pl_jump_map used for
 *              object placement, rebuild, and reintegration
 */
static inline struct pl_jump_map *
pl_map2mplmap(struct pl_map *map)
{
	return container_of(map, struct pl_jump_map, jmp_map);
}

/**
 * A helper function to add new targets that have already been used
 * to the list of used targets.
 *
 * \param[in]   ds_list         The list that this target will be
 *                              be added to.
 * \param[in]   target          The target to be added to the list.
 *
 * return       0 if there was no error or a negative error code
 *              otherwise.
 */
static int
add_ds_shard(d_list_t *ds_list, struct pool_target *target)
{
	struct down_shard *ds_new;

	D_ALLOC_PTR(ds_new);
	if (ds_new == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&ds_new->ds_list);
	ds_new->target_location = target;

	d_list_add(&ds_new->ds_list, ds_list);

	return 0;
}

/**
 * This function initializes the bit map that is used to determine if a target
 * that is down was previously selected as a fallback target. This is used to
 * differentiate between targets that were fallback targets but have since
 * become unavailable, and targets that were already used as fallback targets
 * in this layout.
 *
 * \param[in]	down_targets	List of targets that are either down, or
 *				already exist in the layout. Both cannot be
 *				used as fallback targets.
 * \param[out]	selected_dom	The top level domain being examined for
 *				fallback target selection/
 * \param[in]	used_tgts	The bitmap that this function populates.
 * \param[in]	skipped_targets The number of skipped targets, used to keep
 *				track of when we have tried all targets in
 *				this domain.
 *
 * return		An error code, 0 if successful, or less than 0
 *			denoting an error occurred.
 */
static int
set_used_targets(d_list_t *down_targets, struct pool_domain *selected_dom,
		 uint8_t **used_tgts, uint32_t *skipped_targets)
{

	struct          pool_target *start_pos;
	struct          pool_target *end_pos;
	uint32_t        nums_targets;
	uint32_t        num_bytes;
	struct down_shard   *curr_down_tgt;

	/* To find rebuild target we examine all targets */
	nums_targets = selected_dom->do_target_nr;
	num_bytes = (nums_targets / 8) + 1;

	D_ALLOC_ARRAY(*used_tgts, num_bytes);
	if (used_tgts == NULL)
		return -DER_NOMEM;

	start_pos = &(selected_dom->do_targets[0]);
	end_pos = start_pos + nums_targets;
	/*
	 * Add the initial layouts targets to the bitmap of checked
	 * targets.
	 */


	d_list_for_each_entry(curr_down_tgt, down_targets, ds_list) {
		struct pool_target *position = curr_down_tgt->target_location;

		if (start_pos <= position && position < end_pos) {
			setbit(*used_tgts, position - start_pos);
			(*skipped_targets)++;
		}
	}

	return 0;
}

/**
 * This function recursively chooses a single target to be used in the
 * object shard layout. This function is called for every shard that needs a
 * placement location.
 *
 * \param[in]   curr_dom        The current domain that is being used to
 *                              determine the target location for this shard.
 *                              initially the root node of the pool map.
 * \param[out]  target          This variable is used when returning the
 *                              selected target for this shard.
 * \param[in]   obj_key         a unique key generated using the object ID.
 *                              This is used in jump consistent hash.
 * \param[in]   dom_used        This is a contiguous array that contains
 *                              information on whether or not an internal node
 *                              (non-target) in a domain has been used.
 * \param[in]   used_targets    A list of the targets that have been used. We
 *                              iterate through this when selecting the next
 *                              target in a placement to determine if that
 *                              location is valid.
 * \param[in]   shard_num       the current shard number. This is used when
 *                              selecting a target to determine if repeated
 *                              targets are allowed in the case that there
 *                              are more shards than targets
 *
 * \return      an int value indicating if the returned target is available (0)
 *              or failed (1)
 */
static void
get_target(struct pool_domain *curr_dom, struct pool_target **target,
	   uint64_t obj_key, uint8_t *dom_used, struct pl_obj_layout *layout,
	   int shard_num)
{
	uint8_t                 found_target = 0;
	uint8_t                 top = 0;
	uint32_t                fail_num = 0;
	uint32_t                selected_dom;
	uint32_t                tgt_id;
	struct pool_domain      *root_pos;

	root_pos = curr_dom;

	do {
		uint32_t        num_doms;
		uint64_t        key;

		/* Retrieve number of nodes in this domain */
		if (curr_dom->do_children == NULL)
			num_doms = curr_dom->do_target_nr;
		else
			num_doms = curr_dom->do_child_nr;

		key = obj_key;
		/* If choosing target in lowest fault domain level */

		if (curr_dom->do_children == NULL) {
			uint32_t dom_id;
			uint32_t        i;

			do {
				/*
				 * Must crc key because jump consistent hash
				 * requires an even distribution or it will
				 * not work
				 */
				key = crc(key, fail_num++);

				/* Get target for shard */
				selected_dom = jump_consistent_hash(key,
								    num_doms);

				/* Retrieve actual target using index */
				*target = &curr_dom->do_targets[selected_dom];

				/* Get target id to check if target used */
				dom_id = (*target)->ta_comp.co_id;

				/*
				 * Check to see if this target is valid to use.
				 * You can reuse targets as long as there are
				 * fewer targets than shards and all targets
				 * have already been used
				 */

				for (i = 0; i < layout->ol_nr; ++i) {

					tgt_id = layout->ol_shards[i].po_target;

					if (tgt_id == dom_id)
						break;
				}
			} while (i < shard_num);

			/* Found target (which may be available or not) */
			found_target = 1;
		} else {
			int             range_set;
			uint64_t        child_pos;

			child_pos = (curr_dom->do_children) - root_pos;

			/*
			 * If all of the nodes in this domain have been used for
			 * shards but we still have shards to place mark all
			 * nodes as unused in bookkeeping array so duplicates
			 * can be chosen
			 */
			range_set = isset_range(dom_used, child_pos, child_pos
						+ num_doms - 1);
			if (range_set  && curr_dom->do_children != NULL) {
				clrbit_range(dom_used, child_pos,
					     child_pos + (num_doms - 1));
			}

			/*
			 * Keep choosing new domains until one that has
			 * not been used is found
			 */
			do {

				selected_dom = jump_consistent_hash(key,
								    num_doms);
				key = crc(key, fail_num++);
			} while (isset(dom_used, selected_dom + child_pos));
			/* Mark this domain as used */
			setbit(dom_used, (selected_dom + child_pos));

			top++;
			curr_dom = &(curr_dom->do_children[selected_dom]);
			obj_key = crc(obj_key, top);
		}
	} while (!found_target);

}

/**
 * This function recursively chooses a single target to be used in the
 * object shard layout. This function is called for every shard that needs a
 * placement location.
 *
 * \param[in]   pmap            The pool map associated with this placement
 *                              map. This is used to directly access the
 *                              targets in the pool.
 * \param[out]  target          Holds the value of the chosen spare target.
 *                              for the shard being rebuilt.
 * \param[in]   key             A unique key generated using the object ID.
 *                              This is the same key used during initial
 *                              placement.
 *                              This is used in jump consistent hash.
 * \param[in]   dom_used        This is a contiguous array that contains
 *                              information on whether or not an internal node
 *                              (non-target) in a domain has been used.
 * \param[in]   layout          This is the current layout for the object.
 *                              This is needed for guaranteeing that we don't
 *                              reuse a target already in the layout.
 * \param[in]   md              Object metadata used used to compare object
 *                              version with fail sequence.
 *
 * \return                      Returns an error code if an error occurred,
 *                              otherwise 0.
 */
static int
get_rebuild_target(struct pool_map *pmap, struct pool_target **target,
		   uint64_t key, uint8_t *dom_used, d_list_t *down_targets,
		   struct pl_obj_layout *layout, struct daos_obj_md *md)
{
	uint8_t                 *used_tgts = NULL;
	uint32_t                selected_dom;
	uint32_t                fail_num = 0xFFc5;
	uint32_t                try = 0;
	uint32_t                num_doms;
	struct pool_domain      *target_selection;
	struct pool_domain      *root;
	int rc = 0;

	rc = pool_map_find_domain(pmap, PO_COMP_TP_ROOT, PO_COMP_ID_ALL, &root);
	if (rc == 0) {
		D_ERROR("Could not find root node in pool map.");
		rc = -DER_NONEXIST;
		return rc;
	}

	while (1) {

		uint8_t range_set;
		uint32_t skipped_targets;
		uint64_t child_pos;

		skipped_targets = 0;
		num_doms = root->do_child_nr;
		child_pos = (root->do_children) - root;

		range_set = isset_range(dom_used, child_pos, child_pos
					+ num_doms - 1);

		if (range_set  && root->do_children != NULL) {
			clrbit_range(dom_used, child_pos,
				     child_pos + (num_doms - 1));
		}

		/*
		 * Choose domains using jump consistent hash until we find a
		 * suitable domains that has not already been used.
		 */
		do {
			key = crc(key, fail_num++);
			selected_dom = jump_consistent_hash(key, num_doms);
			target_selection = &(root->do_children[selected_dom]);
		} while (isset(dom_used, (selected_dom + child_pos)));

		/* To find rebuild target we examine all targets */
		num_doms = target_selection->do_target_nr;

		rc = set_used_targets(down_targets, target_selection,
				      &used_tgts, &skipped_targets);
		if (rc)
			return rc;

		/*
		 * Attempt to choose a fallback target from all targets found
		 * in this top level domain.
		 */
		do {
			key = crc(key, try++);

			selected_dom = jump_consistent_hash(key, num_doms);
			*target = &(target_selection->do_targets[selected_dom]);

			/*
			 * keep track of what targets have been tried
			 * in case all targets in domain have failed
			 */
			if (isclr(used_tgts, selected_dom))
				skipped_targets++;

		} while ((isset(used_tgts, selected_dom)) &&
			 skipped_targets < num_doms);

		if (skipped_targets == num_doms)
			D_DEBUG(DB_PL, "Skipped all targets in domain, "
				"no valid slections.\n");

		D_FREE(used_tgts);

		/* Use the last examined target if it's not unavailable */
		if (!pool_target_unavail(*target) ||
		    (*target)->ta_comp.co_fseq > md->omd_ver) {
			rc = add_ds_shard(down_targets, *target);
			return rc;
		}
	}
	/* Should not reach this point */
	D_ERROR("Unexpectedly reached end of placement loop without result");
	D_ASSERT(0);
	return DER_INVAL;
}

/**
* Try to remap all the failed shards in the @remap_list to proper
* targets. The new target id will be updated in the @layout if the
* remap succeeds, otherwise, corresponding shard and target id in
* @layout will be cleared as -1.
*
* \paramp[in]   jmap            The placement map being used for placement.
* \paramp[in]   md              Object Metadata.
* \paramp[in]   layout          The original layout which contains some shards
*                               on failed targets.
* \paramp[in]   jmop            Structure containing information related to
*                               layout characteristics.
* \paramp[in]   remap_list      List containing shards to be remapped sorted
*                               by failure sequence.
* \paramp[in]   dom_used        Bookkeeping array used to keep track of which
*                               domain components have already been used.
*
* \return       return an error code signifying whether the shards were
*               successfully remapped properly.
*/
static int
obj_remap_shards(struct pl_jump_map *jmap, struct daos_obj_md *md,
		 struct pl_obj_layout *layout, struct jm_obj_placement *jmop,
		 d_list_t *remap_list, d_list_t *used_targets_list,
		 uint8_t *dom_used)
{
	struct failed_shard     *f_shard;
	struct pl_obj_shard     *l_shard;
	struct pool_target      *spare_tgt;
	d_list_t                *current;
	bool                    spare_avail = true;
	daos_obj_id_t           oid;
	uint64_t                key;


	remap_dump(remap_list, md, "before remap:");

	current = remap_list->next;
	spare_tgt = NULL;
	oid = md->omd_id;
	key = oid.lo;

	while (current != remap_list) {
		uint64_t rebuild_key;

		f_shard = d_list_entry(current, struct failed_shard,
				       fs_list);
		l_shard = &layout->ol_shards[f_shard->fs_shard_idx];

		spare_avail = jump_map_remap_next_spare(jmap, jmop);

		rebuild_key = (f_shard->fs_shard_idx * 10) + f_shard->fs_fseq;

		if (spare_avail)
			get_rebuild_target(jmap->jmp_map.pl_poolmap, &spare_tgt,
					   crc(key, rebuild_key), dom_used,
					   used_targets_list, layout, md);

		determine_valid_spares(spare_tgt, md, spare_avail, &current,
				       remap_list, f_shard, l_shard);

	}

	remap_dump(remap_list, md, "after remap:");
	return 0;
}

static int
jump_map_obj_spec_place_get(struct pl_jump_map *jmap, daos_obj_id_t oid,
			    struct pool_target **target, uint8_t *dom_used,
			    uint32_t dom_bytes)
{
	struct pool_target      *tgts;
	struct pool_domain      *current_dom;
	struct pool_domain      *root;
	unsigned int            pos;
	int                     rc;

	tgts = pool_map_targets(jmap->jmp_map.pl_poolmap);

	rc = spec_place_rank_get(&pos, oid, (jmap->jmp_map.pl_poolmap));
	if (rc)
		return rc;

	*target = &(tgts[pos]);


	rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap, PO_COMP_TP_ROOT,
				  PO_COMP_ID_ALL, &root);
	D_ASSERT(rc == 1);
	current_dom = root;

	/* Update collision map to account for this shard. */
	while (current_dom->do_children != NULL) {
		int index;
		uint64_t child_pos;

		child_pos = (current_dom->do_children) - root;

		for (index = 0; index < current_dom->do_child_nr; ++index) {
			struct pool_domain *temp_dom;
			int num_children;
			int last;
			struct pool_target *start;
			struct pool_target *end;

			temp_dom = &(current_dom->do_children[index]);
			num_children = temp_dom->do_target_nr;
			last = num_children - 1;

			start = &(temp_dom->do_targets[0]);
			end = &(temp_dom->do_targets[last]);

			if ((start <= (*target)) && ((*target) <= end)) {
				current_dom = temp_dom;
				setbit(dom_used, (index + child_pos));
				break;
			}
		}
	}

	return 0;
}

/**
 * This function handles getting the initial layout for the object as well as
 * determining if there are targets that are unavailable.
 *
 * \param[in]   jmap            The placement map used for this placement.
 * \param[in]   jmop            The layout group size and count.
 * \param[in]   md              Object metadata.
 * \param[out]  layout          This will contain the layout for the object
 * \param[out]  remap_list      This will contain the targets that need to
 *                              be rebuilt and in the case of rebuild, may be
 *                              returned during the rebuild process.
 *
 * \return                      An error code determining if the function
 *                              succeeded (0) or failed.
 */
static int
get_object_layout(struct pl_jump_map *jmap, struct pl_obj_layout *layout,
		  struct jm_obj_placement *jmop, d_list_t *remap_list,
		  struct daos_obj_md *md)
{
	struct pool_target      *target;
	struct pool_domain      *root;
	daos_obj_id_t           oid;
	d_list_t                used_targets_list;
	uint8_t                 *dom_used;
	uint8_t                *used_targets;
	uint32_t                dom_used_length;
	uint64_t                key;
	int i, j, k, rc;

	/* Set the pool map version */
	layout->ol_ver = pl_map_version(&(jmap->jmp_map));

	j = 0;
	k = 0;
	oid = md->omd_id;
	key = oid.lo;

	rc = 0;
	target = NULL;


	rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap, PO_COMP_TP_ROOT,
				  PO_COMP_ID_ALL, &root);
	if (rc == 0) {
		D_ERROR("Could not find root node in pool map.");
		return -DER_NONEXIST;
	}

	dom_used_length = (struct pool_domain *)(root->do_targets) - (root) + 1;

	D_ALLOC_ARRAY(dom_used, dom_used_length);
	D_ALLOC_ARRAY(used_targets, ((layout->ol_nr) / 8) + 1);
	D_INIT_LIST_HEAD(&used_targets_list);

	if (dom_used == NULL || used_targets == NULL)
		D_GOTO(out, rc);

	/**
	 * If the object class is a special class then the first shard must be
	 * hand picked because there is no other way to specify a starting
	 * location.
	 */
	if (daos_obj_id2class(oid) == DAOS_OC_R3S_SPEC_RANK ||
	    daos_obj_id2class(oid) == DAOS_OC_R1S_SPEC_RANK ||
	    daos_obj_id2class(oid) == DAOS_OC_R2S_SPEC_RANK) {

		rc = jump_map_obj_spec_place_get(jmap, oid, &target, dom_used,
						 dom_used_length);

		if (rc) {
			D_ERROR("special oid "DF_OID" failed: rc %d\n",
				DP_OID(oid), rc);
			D_GOTO(out, rc);

		}

		layout->ol_shards[0].po_target = target->ta_comp.co_id;
		layout->ol_shards[0].po_shard = 0;
		layout->ol_shards[0].po_fseq = target->ta_comp.co_fseq;

		if (pool_target_unavail(target)) {
			rc = remap_alloc_one(remap_list, 0, target);
			if (rc)
				D_GOTO(out, rc);
		}

		/** skip the first shard because it's been
		 * determined by Obj class
		 */
		j = 1;
		k = 1;
	}


	for (i = 0; i < jmop->jmop_grp_nr; i++) {
		for (; j < jmop->jmop_grp_size; j++, k++) {
			uint32_t tgt_id;
			uint32_t fseq;

			get_target(root, &target, crc(key, k), dom_used,
				   layout, k);

			tgt_id = target->ta_comp.co_id;
			fseq = target->ta_comp.co_fseq;

			layout->ol_shards[k].po_target = tgt_id;
			layout->ol_shards[k].po_shard = k;
			layout->ol_shards[k].po_fseq = fseq;

			add_ds_shard(&used_targets_list, target);

			/** If target is failed queue it for remap*/
			if (pool_target_unavail(target)) {
				rc = remap_alloc_one(remap_list, k, target);
				if (rc)
					D_GOTO(out, rc);
			}
		}
		j = 0;
	}

	rc = obj_remap_shards(jmap, md, layout, jmop, remap_list,
			      &used_targets_list, dom_used);
out:
	if (rc) {
		D_ERROR("jump_map_obj_layout_fill failed, rc %d.\n", rc);
		remap_list_free_all(remap_list);
	}

	if (used_targets)
		D_FREE(used_targets);
	if (dom_used)
		D_FREE(dom_used);

	return rc;
}

/**
 * Frees the placement map
 *
 * \param[in]   map     The placement map to be freed
 */
static void
jump_map_destroy(struct pl_map *map)
{
	struct pl_jump_map   *jmap;

	jmap = pl_map2mplmap(map);

	if (jmap->jmp_map.pl_poolmap)
		pool_map_decref(jmap->jmp_map.pl_poolmap);

	D_FREE(jmap);
}

/**
 * This function allocates and initializes the placement map.
 *
 * \param[in]   poolmap The pool map to be used when calculating object
 *                      placement.
 * \param[in]   mia     placement map initialization values. Part of the
 *                      placement map API but currently not used in this map.
 * \param[in]   mapp    The placement map interface that will be passed out
 *                      and used when placing objects.
 *
 * \return              The error status of the function.
 */
static int
jump_map_create(struct pool_map *poolmap, struct pl_map_init_attr *mia,
		struct pl_map **mapp)
{
	struct pool_domain      *root;
	struct pl_jump_map   *jmap;
	struct pool_domain      *doms;
	int                     rc;

	D_ALLOC_PTR(jmap);
	if (jmap == NULL)
		return -DER_NOMEM;

	pool_map_addref(poolmap);
	jmap->jmp_map.pl_poolmap = poolmap;

	rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap, PO_COMP_TP_ROOT,
				  PO_COMP_ID_ALL, &root);
	if (rc == 0) {
		D_ERROR("Could not find root node in pool map.");
		rc = -DER_NONEXIST;
		goto ERR;
	}

	rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap,
			mia->ia_jump_map.domain, PO_COMP_ID_ALL, &doms);
	if (rc <= 0) {
		rc = (rc == 0) ? -DER_INVAL : rc;
		goto ERR;
	}

	jmap->jmp_domain_nr = rc;
	*mapp = &jmap->jmp_map;

	return DER_SUCCESS;

ERR:
	jump_map_destroy(&jmap->jmp_map);
	return rc;
}



static void
jump_map_print(struct pl_map *map)
{
	/** Currently nothing to print */
}

/**
 * Determines the locations that a given object shard should be located.
 *
 * \param[in]   map             A reference to the placement map being used to
 *                              place the object shard.
 * \param[in]   md              The object metadata which contains data about
 *                              the object being placed such as the object ID.
 * \param[in]   shard_md        Shard metadata.
 * \param[out]  layout_pp       The layout generated for the object. Contains
 *                              references to the targets in the pool map where
 *                              the shards will be placed.
 *
 * \return                      An integer value containing the error
 *                              code or 0 if the function returned
 *                              successfully.
 */
static int
jump_map_obj_place(struct pl_map *map, struct daos_obj_md *md,
		   struct daos_obj_shard_md *shard_md,
		   struct pl_obj_layout **layout_pp)
{
	struct pl_jump_map           *jmap;
	struct pl_obj_layout            *layout;
	struct jm_obj_placement    jmop;
	d_list_t                        remap_list;
	daos_obj_id_t                   oid;
	int                             rc;

	jmap = pl_map2mplmap(map);
	oid = md->omd_id;

	rc = jm_obj_placement_get(jmap, md, shard_md, &jmop);
	if (rc) {
		D_ERROR("jm_obj_placement_get failed, rc %d.\n", rc);
		return rc;
	}

	/* Allocate space to hold the layout */
	rc = pl_obj_layout_alloc(jmop.jmop_grp_nr * jmop.jmop_grp_size,
				 &layout);
	if (rc != 0) {
		D_ERROR("pl_obj_layout_alloc failed, rc %d.\n", rc);
		return rc;
	}
	layout->ol_grp_nr = jmop.jmop_grp_nr;
	layout->ol_grp_size = jmop.jmop_grp_size;

	/* Get root node of pool map */
	D_INIT_LIST_HEAD(&remap_list);
	rc = get_object_layout(jmap, layout, &jmop, &remap_list, md);
	if (rc < 0) {
		D_ERROR("Could not generate placement layout, rc %d.\n", rc);
		pl_obj_layout_free(layout);
		remap_list_free_all(&remap_list);
		return rc;
	}

	*layout_pp = layout;
	obj_layout_dump(oid, layout);

	remap_list_free_all(&remap_list);

	return DER_SUCCESS;
}

/**
 *
 * \param[in]   map             The placement map to be used to generate the
 *                              placement layouts and to calculate rebuild
 *                              targets.
 * \param[in]   md              Metadata describing the object.
 * \param[in]   shard_md        Metadata describing how the shards.
 * \param[in]   rebuild_ver     Current Rebuild version
 * \param[out]   tgt_rank       The rank of the targets that need to be rebuild
 *                              will be stored in this array to be passed out
 *                              (this is allocated by the caller)
 * \param[out]   shard_id       The shard IDs of the shards that need to be
 *                              rebuilt (This is allocated by the caller)
 * \param[in]   array_size      The max size of the passed in arrays to store
 *                              info about the shards that need to be rebuilt.
 * \param[in]   myrank          The rank of the server. Only servers who are
 *                              the leader for a particular failed shard will
 *                              initiate a rebuild for it.
 *
 * \return                      The number of shards that need to be rebuilt on
 *                              another target, Or 0 if none need to be rebuilt.
 */
static int
jump_map_obj_find_rebuild(struct pl_map *map, struct daos_obj_md *md,
			  struct daos_obj_shard_md *shard_md,
			  uint32_t rebuild_ver, uint32_t *tgt_id,
			  uint32_t *shard_idx, unsigned int array_size,
			  int myrank)
{
	struct pl_jump_map              *jmap;
	struct pl_obj_layout            *layout;
	d_list_t                        remap_list;
	struct jm_obj_placement         jmop;
	daos_obj_id_t                   oid;
	int                             rc;

	int idx = 0;

	D_DEBUG(DB_PL, "Finding Rebuild\n");

	/* Caller should guarantee the pl_map is up-to-date */
	if (pl_map_version(map) < rebuild_ver) {
		D_ERROR("pl_map version(%u) < rebuild version(%u)\n",
			pl_map_version(map), rebuild_ver);
		return -DER_INVAL;
	}

	jmap = pl_map2mplmap(map);
	oid = md->omd_id;

	rc = jm_obj_placement_get(jmap, md, shard_md, &jmop);
	if (rc) {
		D_ERROR("jm_obj_placement_get failed, rc %d.\n", rc);
		return rc;
	}

	if (jmop.jmop_grp_size == 1) {
		D_DEBUG(DB_PL, "Not replicated object "DF_OID"\n",
			DP_OID(md->omd_id));
		return 0;
	}

	/* Allocate space to hold the layout */
	rc = pl_obj_layout_alloc(jmop.jmop_grp_size * jmop.jmop_grp_nr,
				 &layout);
	if (rc) {
		D_ERROR("pl_obj_layout_alloc failed, rc %d.\n", rc);
		return rc;
	}
	layout->ol_grp_nr = jmop.jmop_grp_nr;
	layout->ol_grp_size = jmop.jmop_grp_size;

	D_INIT_LIST_HEAD(&remap_list);
	rc = get_object_layout(jmap, layout, &jmop, &remap_list, md);

	if (rc < 0) {
		D_ERROR("Could not generate placement layout, rc %d.\n", rc);
		goto out;
	}

	obj_layout_dump(oid, layout);

	rc = remap_list_fill(map, md, shard_md, rebuild_ver, tgt_id, shard_idx,
			     array_size, myrank, &idx, layout, &remap_list);

out:
	remap_list_free_all(&remap_list);
	pl_obj_layout_free(layout);
	return rc < 0 ? rc : idx;
}

static int
jump_map_obj_find_reint(struct pl_map *map, struct daos_obj_md *md,
			struct daos_obj_shard_md *shard_md,
			struct pl_target_grp *tgp_reint, uint32_t *tgt_reint)
{
	D_ERROR("Unsupported\n");
	return -DER_NOSYS;
}

/** API for generic placement map functionality */
struct pl_map_ops       jump_map_ops = {
	.o_create               = jump_map_create,
	.o_destroy              = jump_map_destroy,
	.o_print                = jump_map_print,
	.o_obj_place            = jump_map_obj_place,
	.o_obj_find_rebuild     = jump_map_obj_find_rebuild,
	.o_obj_find_reint       = jump_map_obj_find_reint,
};
