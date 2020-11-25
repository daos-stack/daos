/**
 *
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
 * src/placement/jump_map.c
 */
#define D_LOGFAC        DD_FAC(placement)

#include "pl_map.h"
#include <inttypes.h>
#include <daos/pool_map.h>
#include <isa-l.h>


/*
 * These ops determine whether extra information is calculated during
 * placement.
 *
 * PL_PLACE_EXTENDED calculates an extended layout for use when there
 * is a reintegration operation currently ongoing.
 *
 * PL_REINT calculates the post-reintegration layout for use during
 * reintegration, it treats the UP status targets as UP_IN.
 *
 * Currently the other OP types calculate a normal layout without extra info.
 */
enum PL_OP_TYPE {
	PL_PLACE,
	PL_PLACE_EXTENDED,
	PL_REBUILD,
	PL_REINT,
	PL_ADD,
};

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
	struct pl_map		jmp_map;
	/* Total size of domain type specified during map creation */
	unsigned int		jmp_domain_nr;
	/* The dom that will contain no colocated shards */
	pool_comp_type_t	min_redundant_dom;
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
 * This functions determines whether the object layout should be extended or
 * not based on the operation performed and the target status.
 *
 * \param[in]	op	The operation being performed
 * \param[in]	status	The component status.
 *
 * \return		True if the layout should be extended,
 *			False otherwise.
 */
static inline bool
can_extend(enum PL_OP_TYPE op, enum pool_comp_state state)
{
	if (op != PL_PLACE_EXTENDED)
		return false;
	if (state != PO_COMP_ST_UP && state != PO_COMP_ST_DRAIN)
		return false;
	return true;
}

/**
 * This functions finds the pairwise differences in the two layouts provided
 * and appends them into the d_list provided. The function appends the targets
 * from the "new" layout and not those from the "original" layout.
 *
 * \param[in]	jmap		A pointer to the jump map used to retrieve a
 *				reference to the pool map target.
 * \param[in]	original	The original layout calculated not including any
 *				recent pool map changes, like reintegration.
 * \param[in]	new		The new layout that contains changes in layout
 *				that occurred due to pool status changes.
 * \param[out]	diff		The d_list that contains the differences that
 *				were calculated.
 */
static inline void
layout_find_diff(struct pl_jump_map *jmap, struct pl_obj_layout *original,
		 struct pl_obj_layout *new, d_list_t *diff)
{
	int index;

	/* We assume they are the same size */
	D_ASSERT(original->ol_nr == new->ol_nr);

	for (index = 0; index < original->ol_nr; ++index) {
		uint32_t original_target = original->ol_shards[index].po_target;
		uint32_t reint_tgt = new->ol_shards[index].po_target;
		struct pool_target *temp_tgt;

		if (reint_tgt != original_target) {
			pool_map_find_target(jmap->jmp_map.pl_poolmap,
					     reint_tgt, &temp_tgt);
			remap_alloc_one(diff, index, temp_tgt, true);
		}
	}
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
		D_ERROR("Can not find obj class, invalid oid="DF_OID"\n",
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
jump_map_has_next_spare(struct pl_jump_map *jmap, struct jm_obj_placement *jmop,
		uint32_t spares_left, enum PL_OP_TYPE op,
		enum pool_comp_state state)
{
	D_ASSERTF(jmop->jmop_grp_size <= jmap->jmp_domain_nr,
		  "grp_size: %u > domain_nr: %u\n",
		  jmop->jmop_grp_size, jmap->jmp_domain_nr);

	if ((jmop->jmop_grp_size == jmap->jmp_domain_nr &&
	    jmop->jmop_grp_size > 1))
		return false;

	if (spares_left == 0)
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
pl_map2jmap(struct pl_map *map)
{
	return container_of(map, struct pl_jump_map, jmp_map);
}

static inline uint32_t
get_num_domains(struct pool_domain *curr_dom, enum PL_OP_TYPE op_type)
{
	struct pool_domain *next_dom;
	struct pool_target *next_target;
	uint32_t num_dom;
	uint8_t status;

	if (curr_dom->do_children == NULL)
		num_dom = curr_dom->do_target_nr;
	else
		num_dom = curr_dom->do_child_nr;

	if (op_type == PL_ADD)
		return num_dom;

	if (curr_dom->do_children != NULL) {
		next_dom = &curr_dom->do_children[num_dom - 1];
		status = next_dom->do_comp.co_status;

		while (num_dom - 1 > 0 && status == PO_COMP_ST_NEW) {
			num_dom--;
			next_dom = &curr_dom->do_children[num_dom - 1];
			status = next_dom->do_comp.co_status;
		}
	} else {
		next_target = &curr_dom->do_targets[num_dom - 1];
		status = next_target->ta_comp.co_status;

		while (num_dom - 1 > 0 && status == PO_COMP_ST_NEW) {
			num_dom--;
			next_target = &curr_dom->do_targets[num_dom - 1];
			status = next_target->ta_comp.co_status;
		}
	}

	return num_dom;
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
	   uint64_t obj_key, uint8_t *dom_used, uint8_t *tgts_used,
	   int shard_num, enum PL_OP_TYPE op_type)
{
	int                     range_set;
	uint8_t                 found_target = 0;
	uint32_t                selected_dom;
	struct pool_domain      *root_pos;

	obj_key = crc(obj_key, shard_num);
	root_pos = curr_dom;

	do {
		uint32_t        num_doms;

		/* Retrieve number of nodes in this domain */
		num_doms = get_num_domains(curr_dom, op_type);

		/* If choosing target (lowest fault domain level) */
		if (curr_dom->do_children == NULL) {

			uint32_t        fail_num = 0;
			uint32_t        dom_id;
			uint32_t        start_tgt;
			uint32_t        end_tgt;

			start_tgt = curr_dom->do_targets[0].ta_comp.co_id;
			end_tgt = start_tgt + (num_doms - 1);

			range_set = isset_range(tgts_used, start_tgt, end_tgt);
			if (range_set)
				clrbit_range(tgts_used, start_tgt, end_tgt);

			do {
				/*
				 * Must crc key because jump consistent hash
				 * requires an even distribution or it will
				 * not work
				 */
				obj_key = crc(obj_key, fail_num++);

				/* Get target for shard */
				selected_dom = jump_consistent_hash(obj_key,
								    num_doms);

				/* Retrieve actual target using index */
				*target = &curr_dom->do_targets[selected_dom];

				/* Get target id to check if target used */
				dom_id = (*target)->ta_comp.co_id;

			} while (isset(tgts_used, dom_id));
			setbit(tgts_used, dom_id);

			/* Found target (which may be available or not) */
			found_target = 1;
		} else {
			uint32_t        fail_num = 0;
			uint64_t        start_dom;
			uint64_t        end_dom;
			uint64_t        key;

			key = obj_key;

			/*
			 * If all of the nodes in this domain have been used for
			 * shards but we still have shards to place mark all
			 * nodes as unused in bookkeeping array so duplicates
			 * can be chosen
			 */
			start_dom = (curr_dom->do_children) - root_pos;
			end_dom = start_dom + (num_doms - 1);

			range_set = isset_range(dom_used, start_dom, end_dom);
			if (range_set)
				clrbit_range(dom_used, start_dom, end_dom);

			/*
			 * Keep choosing new domains until one that has
			 * not been used is found
			 */
			do {
				selected_dom = jump_consistent_hash(key,
								    num_doms);
				key = crc(key, fail_num++);
			} while (isset(dom_used, start_dom + selected_dom));

			/* Mark this domain as used */
			setbit(dom_used, start_dom + selected_dom);

			curr_dom = &(curr_dom->do_children[selected_dom]);
			obj_key = crc(obj_key, curr_dom->do_comp.co_id);
		}
	} while (!found_target);
}


uint32_t
count_available_spares(struct pl_jump_map *jmap, struct pl_obj_layout *layout,
		uint32_t failed_in_layout)
{
	uint32_t unusable_tgts;
	uint32_t num_targets;

	num_targets =  pool_map_find_target(jmap->jmp_map.pl_poolmap,
			PO_COMP_ID_ALL, NULL);

	/* we might not have any valid targets left at all */
	unusable_tgts = layout->ol_nr;

	if (unusable_tgts >= num_targets)
		return 0;

	return num_targets - unusable_tgts;
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
		 d_list_t *remap_list, enum PL_OP_TYPE op_type,
		 uint8_t *tgts_used, uint8_t *dom_used,
		 uint32_t failed_in_layout, d_list_t *extend_list)
{
	struct failed_shard     *f_shard;
	struct pl_obj_shard     *l_shard;
	struct pool_target      *spare_tgt;
	struct pool_domain      *root;
	d_list_t                *current;
	daos_obj_id_t           oid;
	bool                    spare_avail = true;
	bool			for_reint;
	uint64_t                key;
	uint32_t		spares_left;
	int                     rc;


	remap_dump(remap_list, md, "remap:");

	for_reint = (op_type == PL_REINT);
	current = remap_list->next;
	spare_tgt = NULL;
	oid = md->omd_id;
	key = oid.hi ^ oid.lo;
	spares_left = count_available_spares(jmap, layout, failed_in_layout);

	rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap, PO_COMP_TP_ROOT,
				  PO_COMP_ID_ALL, &root);
	D_ASSERT(rc == 1);
	while (current != remap_list) {
		uint64_t rebuild_key;
		uint32_t shard_id;

		f_shard = d_list_entry(current, struct failed_shard, fs_list);

		shard_id = f_shard->fs_shard_idx;
		l_shard = &layout->ol_shards[f_shard->fs_shard_idx];

		spare_avail = jump_map_has_next_spare(jmap, jmop, spares_left,
				op_type, f_shard->fs_status);
		if (spare_avail) {
			rebuild_key = crc(key, f_shard->fs_shard_idx);
			get_target(root, &spare_tgt, crc(key, rebuild_key),
				   dom_used, tgts_used, shard_id, op_type);
			spares_left--;
			if (can_extend(op_type, spare_tgt->ta_comp.co_status)) {
				rc = remap_alloc_one(extend_list, shard_id,
						spare_tgt, true);
				if (rc)
					return rc;
			}
		}

		determine_valid_spares(spare_tgt, md, spare_avail,
				&current, remap_list, for_reint, f_shard,
				l_shard);
	}

	if (op_type == PL_PLACE_EXTENDED) {
		rc = pl_map_extend(layout, extend_list);
		if (rc != 0)
			return rc;
	}
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
		  enum PL_OP_TYPE op_type, struct daos_obj_md *md)
{
	struct pool_target      *target;
	struct pool_domain      *root;
	daos_obj_id_t           oid;
	d_list_t		extend_list;
	uint8_t                 *dom_used;
	uint8_t                 *tgts_used;
	uint32_t                dom_used_length;
	uint64_t                key;
	uint32_t		fail_tgt_cnt;
	bool			for_reint;
	enum pool_comp_state	state;
	int i, j, k, rc;

	/* Set the pool map version */
	layout->ol_ver = pl_map_version(&(jmap->jmp_map));

	j = 0;
	k = 0;
	fail_tgt_cnt = 0;
	oid = md->omd_id;
	key = oid.hi ^ oid.lo;
	target = NULL;
	for_reint = (op_type == PL_REINT);

	rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap, PO_COMP_TP_ROOT,
				  PO_COMP_ID_ALL, &root);
	if (rc == 0) {
		D_ERROR("Could not find root node in pool map.");
		return -DER_NONEXIST;
	}

	dom_used_length = (struct pool_domain *)(root->do_targets) - (root) + 1;

	D_ALLOC_ARRAY(dom_used, (dom_used_length / 8) + 1);
	D_ALLOC_ARRAY(tgts_used, (root->do_target_nr / 8) + 1);
	D_INIT_LIST_HEAD(&extend_list);

	if (dom_used == NULL || tgts_used == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/**
	 * If the object class is a special class then the first shard must be
	 * hand picked because there is no other way to specify a starting
	 * location.
	 */
	if (daos_obj_is_srank(oid)) {
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
		setbit(tgts_used, target->ta_comp.co_id);

		if (pool_target_unavail(target, for_reint)) {
			fail_tgt_cnt++;
			state = target->ta_comp.co_status;
			rc = remap_alloc_one(remap_list, 0, target, false);
			if (rc)
				D_GOTO(out, rc);
			if (can_extend(op_type, state)) {
				rc = remap_alloc_one(&extend_list, k, target,
						     true);
				if (rc != 0)
					D_GOTO(out, rc);
			}
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

			get_target(root, &target, key, dom_used, tgts_used, k,
				   op_type);

			tgt_id = target->ta_comp.co_id;
			fseq = target->ta_comp.co_fseq;

			layout->ol_shards[k].po_target = tgt_id;
			layout->ol_shards[k].po_shard = k;
			layout->ol_shards[k].po_fseq = fseq;

			/** If target is failed queue it for remap*/
			if (pool_target_unavail(target, for_reint)) {
				fail_tgt_cnt++;
				state = target->ta_comp.co_status;
				rc = remap_alloc_one(remap_list, k, target,
						false);
				if (rc)
					D_GOTO(out, rc);

				if (can_extend(op_type, state)) {
					remap_alloc_one(&extend_list, k,
							target, true);
				}

			}
		}

		j = 0;
	}

	rc = 0;
	if (fail_tgt_cnt > 0)
		rc = obj_remap_shards(jmap, md, layout, jmop, remap_list,
				op_type, tgts_used, dom_used, fail_tgt_cnt,
				&extend_list);
out:
	if (rc) {
		D_ERROR("jump_map_obj_layout_fill failed, rc "DF_RC"\n",
			DP_RC(rc));
		remap_list_free_all(remap_list);
	}
	if (dom_used)
		D_FREE(dom_used);
	if (tgts_used)
		D_FREE(tgts_used);

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

	jmap = pl_map2jmap(map);

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
	struct pl_jump_map      *jmap;
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

	jmap->min_redundant_dom = mia->ia_jump_map.domain;
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
	struct pl_jump_map	*jmap;
	struct pl_obj_layout	*layout;
	struct pl_obj_layout	*add_layout;
	struct jm_obj_placement	jmop;
	struct pool_domain	*root;
	d_list_t		remap_list;
	d_list_t		add_list;
	daos_obj_id_t		oid;
	int			rc;

	jmap = pl_map2jmap(map);
	oid = md->omd_id;

	rc = jm_obj_placement_get(jmap, md, shard_md, &jmop);
	if (rc) {
		D_ERROR("jm_obj_placement_get failed, rc "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	/* Allocate space to hold the layout */
	rc = pl_obj_layout_alloc(jmop.jmop_grp_size, jmop.jmop_grp_nr,
				 &layout);
	if (rc != 0) {
		D_ERROR("pl_obj_layout_alloc failed, rc "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	D_INIT_LIST_HEAD(&remap_list);
	rc = get_object_layout(jmap, layout, &jmop, &remap_list,
				PL_PLACE_EXTENDED, md);
	assert(rc == 0);
	/* Needed to check if domains are being added to pool map */
	rc = pool_map_find_domain(jmap->jmp_map.pl_poolmap, PO_COMP_TP_ROOT,
				  PO_COMP_ID_ALL, &root);
	D_ASSERT(rc == 1);

	if (is_pool_adding(root)) {
		/* Allocate space to hold the layout */
		rc = pl_obj_layout_alloc(jmop.jmop_grp_size, jmop.jmop_grp_nr,
					 &add_layout);

		remap_list_free_all(&remap_list);
		D_INIT_LIST_HEAD(&remap_list);

		rc = get_object_layout(jmap, add_layout, &jmop, &remap_list,
				       PL_ADD, md);
		assert(rc == 0);
		D_INIT_LIST_HEAD(&add_list);
		layout_find_diff(jmap, layout, add_layout, &add_list);

		if (!d_list_empty(&add_list)) {

			rc = pl_map_extend(layout, &add_list);
			if (rc != 0)
				return rc;
		}
	}

	if (rc < 0) {
		D_ERROR("Could not generate placement layout, rc "DF_RC"\n",
			DP_RC(rc));
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
 *
 * \return                      The number of shards that need to be rebuilt on
 *                              another target, Or 0 if none need to be rebuilt.
 */
static int
jump_map_obj_find_rebuild(struct pl_map *map, struct daos_obj_md *md,
			  struct daos_obj_shard_md *shard_md,
			  uint32_t rebuild_ver, uint32_t *tgt_id,
			  uint32_t *shard_idx, unsigned int array_size)
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

	jmap = pl_map2jmap(map);
	oid = md->omd_id;

	rc = jm_obj_placement_get(jmap, md, shard_md, &jmop);
	if (rc) {
		D_ERROR("jm_obj_placement_get failed, rc "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	/* Allocate space to hold the layout */
	rc = pl_obj_layout_alloc(jmop.jmop_grp_size, jmop.jmop_grp_nr,
				 &layout);
	if (rc) {
		D_ERROR("pl_obj_layout_alloc failed, rc "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	D_INIT_LIST_HEAD(&remap_list);
	rc = get_object_layout(jmap, layout, &jmop, &remap_list, PL_REBUILD,
				md);

	if (rc < 0) {
		D_ERROR("Could not generate placement layout, rc "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

	obj_layout_dump(oid, layout);

	rc = remap_list_fill(map, md, shard_md, rebuild_ver, tgt_id, shard_idx,
			     array_size, &idx, layout, &remap_list, false);

out:
	remap_list_free_all(&remap_list);
	pl_obj_layout_free(layout);
	return rc < 0 ? rc : idx;
}

static int
jump_map_obj_find_reint(struct pl_map *map, struct daos_obj_md *md,
			struct daos_obj_shard_md *shard_md,
			uint32_t reint_ver, uint32_t *tgt_rank,
			uint32_t *shard_id, unsigned int array_size)
{
	struct pl_jump_map              *jmap;
	struct pl_obj_layout            *layout;
	struct pl_obj_layout            *reint_layout;
	d_list_t                        remap_list;
	d_list_t                        reint_list;
	struct jm_obj_placement         jop;
	int                             rc;

	int idx = 0;

	D_DEBUG(DB_PL, "Finding Rebuild\n");

	/* Caller should guarantee the pl_map is up-to-date */
	if (pl_map_version(map) < reint_ver) {
		D_ERROR("pl_map version(%u) < rebuild version(%u)\n",
			pl_map_version(map), reint_ver);
		return -DER_INVAL;
	}

	jmap = pl_map2jmap(map);

	rc = jm_obj_placement_get(jmap, md, shard_md, &jop);
	if (rc) {
		D_ERROR("jm_obj_placement_get failed, rc %d.\n", rc);
		return rc;
	}

	/* Allocate space to hold the layout */
	rc = pl_obj_layout_alloc(jop.jmop_grp_size, jop.jmop_grp_nr,
			&layout);
	if (rc)
		return 0;

	rc = pl_obj_layout_alloc(jop.jmop_grp_size, jop.jmop_grp_nr,
			&reint_layout);
	if (rc)
		goto out;

	D_INIT_LIST_HEAD(&remap_list);
	D_INIT_LIST_HEAD(&reint_list);

	/* Get original placement */
	rc = get_object_layout(jmap, layout, &jop, &remap_list, PL_PLACE, md);
	if (rc)
		goto out;

	/* Clear list for next placement operation. */
	remap_list_free_all(&remap_list);
	D_INIT_LIST_HEAD(&remap_list);

	/* Get placement after reintegration. */
	rc = get_object_layout(jmap, reint_layout, &jop, &remap_list, PL_REINT,
			       md);
	if (rc)
		goto out;

	layout_find_diff(jmap, layout, reint_layout, &reint_list);

	rc = remap_list_fill(map, md, shard_md, reint_ver, tgt_rank, shard_id,
			     array_size, &idx, reint_layout, &reint_list,
			     false);
out:
	remap_list_free_all(&reint_list);
	remap_list_free_all(&remap_list);

	if (layout != NULL)
		pl_obj_layout_free(layout);
	if (reint_layout != NULL)
		pl_obj_layout_free(reint_layout);

	return rc < 0 ? rc : idx;
}

static int
jump_map_obj_find_addition(struct pl_map *map, struct daos_obj_md *md,
			   struct daos_obj_shard_md *shard_md,
			   uint32_t reint_ver, uint32_t *tgt_rank,
			   uint32_t *shard_id, unsigned int array_size)
{
	struct pl_jump_map              *jmap;
	struct pl_obj_layout            *layout;
	struct pl_obj_layout            *add_layout;
	d_list_t                        remap_list;
	d_list_t                        add_list;
	struct jm_obj_placement         jop;
	int                             rc;

	int idx = 0;

	D_DEBUG(DB_PL, "Finding new layout for server addition\n");

	/* Caller should guarantee the pl_map is up-to-date */
	if (pl_map_version(map) < reint_ver) {
		D_ERROR("pl_map version(%u) < rebuild version(%u)\n",
			pl_map_version(map), reint_ver);
		return -DER_INVAL;
	}

	jmap = pl_map2jmap(map);

	rc = jm_obj_placement_get(jmap, md, shard_md, &jop);
	if (rc) {
		D_ERROR("jm_obj_placement_get failed, rc %d.\n", rc);
		return rc;
	}

	/* Allocate space to hold the layout */
	rc = pl_obj_layout_alloc(jop.jmop_grp_size, jop.jmop_grp_nr, &layout);
	if (rc)
		return rc;

	D_INIT_LIST_HEAD(&remap_list);
	D_INIT_LIST_HEAD(&add_list);

	rc = pl_obj_layout_alloc(jop.jmop_grp_size, jop.jmop_grp_nr,
				 &add_layout);
	if (rc)
		goto out;

	/* Get original placement */
	rc = get_object_layout(jmap, layout, &jop, &remap_list, PL_PLACE, md);
	if (rc)
		goto out;

	/* Clear list for next placement operation. */
	remap_list_free_all(&remap_list);
	D_INIT_LIST_HEAD(&remap_list);

	/* Get placement after server addition. */
	rc = get_object_layout(jmap, add_layout, &jop, &remap_list, PL_ADD,
			       md);
	if (rc)
		goto out;

	layout_find_diff(jmap, layout, add_layout, &add_list);

	rc = remap_list_fill(map, md, shard_md, reint_ver, tgt_rank, shard_id,
			     array_size, &idx, add_layout, &add_list, true);
out:
	remap_list_free_all(&add_list);
	remap_list_free_all(&remap_list);

	if (layout != NULL)
		pl_obj_layout_free(layout);
	if (add_layout != NULL)
		pl_obj_layout_free(add_layout);

	return rc < 0 ? rc : idx;
}

/** API for generic placement map functionality */
struct pl_map_ops       jump_map_ops = {
	.o_create               = jump_map_create,
	.o_destroy              = jump_map_destroy,
	.o_print                = jump_map_print,
	.o_obj_place            = jump_map_obj_place,
	.o_obj_find_rebuild     = jump_map_obj_find_rebuild,
	.o_obj_find_reint       = jump_map_obj_find_reint,
	.o_obj_find_addition      = jump_map_obj_find_addition,
};
