/**
 *
 *
 *
 *
 *
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
 * This file is part of DSR
 *
 * src/placement/mapless_jump_map.c
 */
#define D_LOGFAC        DD_FAC(placement)

#include "pl_map.h"
#include <inttypes.h>
#include <daos/pool_map.h>

/**
 * This struct is used to to hold information while
 * finding rebuild targets for shards located on unavailable
 * targets.
 */
struct failed_shard {
	d_list_t        fs_list;
	uint32_t        fs_shard_idx;
	uint32_t        fs_fseq;
	uint32_t        fs_tgt_id;
	uint8_t         fs_status;
};

struct mapless_obj_placement {
	unsigned int	mop_grp_size;
	unsigned int	mop_grp_nr;
};

/**
 * This is the Mapless placement map structure used to place objects.
 * The map is returned as a struct pl_map and then converted back into a
 * pl_mapless_map once passed from the caller into the object placement
 * functions.
 */
struct pl_mapless_map {
	/** placement map interface */
	struct pl_map   mmp_map;
	/** the total length of the array used for bookkeeping */
	uint32_t        dom_used_length;

	unsigned int	mmp_domain_nr;
};

/**
 * This function sets a specific bit in the bitmap
 * It expects the bitmap to be zero indexed from left
 * to right.
 */
static inline void
set_bit(uint8_t *bitmap, uint64_t bit, uint32_t num_bytes)
{
	assert(bit >= 0);
	assert(bit < 8*num_bytes);

	uint64_t offset = bit / 8;
	uint8_t position = bit % 8;

	bitmap[offset] |= (0x80 >> position);
}

/** Returns the bit at a specific position in the bitmap */
static inline uint8_t
get_bit(uint8_t *bitmap, uint64_t bit, uint32_t num_bytes)
{
	assert(bit < 8*num_bytes);
	assert(bit >= 0);

	uint64_t offset = bit / 8;
	uint8_t position = bit % 8;

	return ((bitmap[offset] & (0x80 >> position)) != 0);
}

/**
 * Returns true or false depending on whether all bits given in a range
 * are set or not, the range is inclusive. This is used to in the bookkeeping
 * to determine if all children in a domain have been used.
 *
 * \return	returns 0 if the entire range does not contain set bits and
 *		returns 1 if all bits are set
 */
static inline uint8_t
is_range_set(uint8_t *bitmap, uint64_t start, uint64_t end, uint32_t num_bytes)
{
	assert(start >= 0);
	assert(end < 8*num_bytes);

	uint8_t mask = 0xFF >> (start % 8);

	if (end >> 3 == start >> 3) {
		mask &= (0xFF << (8-((end%8)+1)));
		return (bitmap[(start >> 3)] & mask) == mask;
	}

	if ((bitmap[(start >> 3)] & mask) != mask)
		return 0;

	mask = (0xFF << (8-((end%8)+1)));
	if ((bitmap[(end >> 3)] & mask) != mask)
		return 0;

	if ((end >> 3) > ((start >> 3) + 1)) {
		int i;

		for (i = (start >> 3) + 1; i < (end >> 3); ++i)
			if (bitmap[i] != 0xFF)
				return 0;
	}

	return 1;
}

/**
 * This function clears a continuous range of bits in the bitmap.
 * The range if from start to end inclusive.
 */
static inline void
clear_bitmap_range(uint8_t *bitmap, uint64_t start, uint64_t end,
		uint32_t num_bytes)
{

	assert(start >= 0);
	assert(end < 8*num_bytes);

	uint8_t mask = (0xFF >> (start % 8));

	mask = ~mask;
	if (end >> 3 == start >> 3) {
		mask |= (0xFF >> ((end%8)+1));
		bitmap[(start >> 3)] &= mask;
		return;
	}

	bitmap[(start >> 3)] &= mask;

	mask = (0xFF >> ((end % 8) + 1));
	bitmap[(end >> 3)] &= mask;

	if (end >> 3 > (start >> 3) + 1)
		memset(&(bitmap[(start>>3)+1]), 0, (end>>3) - ((start>>3)+1));
}

/**
 * This function returns the number of non-leaf domains in the pool map
 * It's used to determine the size of the arrays for storing the bookkeeping
 * bitmaps
 *
 * \param[in]	dom	The pool map used for this placement map.
 *
 * \return		The number of non-leaf nodes in the pool, not including
 *			the root.
 */
uint64_t get_dom_cnt(struct pool_domain *dom)
{
	uint64_t count = 0;

	if (dom->do_children != NULL) {
		int i;

		count += dom->do_child_nr;

		for (i = 0; i < dom->do_child_nr; ++i)
			count += get_dom_cnt(&(dom->do_children[i]));
	}

	return count;
}

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
 * Assembly instruction for fast 4-byte CRC computation on Intel X86
 * Copied from BSD-licensed Data Plane Development Kit (DPDK) rte_hash_crc.h
 *
 * \param[in] data              Primary input to hash function
 * \param[in] init_value        Acts like a seed to the CRC, useful to get
 *                              different results with the same data
 */
static inline uint32_t
crc32c_sse42_u32(uint32_t data, uint32_t init_val)
{
	__asm__ volatile(
		"crc32l %[data], %[init_val];"
		: [init_val] "+r"(init_val)
		: [data] "rm"(data));
	return init_val;
}

/**
 * Computes an 8-byte CRC using fast assembly instructions.
 *
 * This is useful for mapless placement to pseudorandomly permute input keys
 * that are similar to each other. This dramatically improves the even-ness of
 * the distribution of output placements.
 *
 * Since CRC is typically a 4-byte operation, this by computes a 4-byte CRC of
 * each half of the input data and concatenates them
 *
 * \param[in] data              Primary input to hash function
 * \param[in] init_value        Acts like a seed to the CRC, useful to get
 *                              different results with the same data
 */
static inline uint64_t
crc(uint64_t data, uint32_t init_val)
{
	return (uint64_t)crc32c_sse42_u32((data & 0xFFFFFFFF), init_val)
		| ((uint64_t)crc32c_sse42_u32(((data >> 32) & 0xFFFFFFFF),
					init_val) << 32);
}

/** add one failed shard into remap list */
static void
remap_add_one(d_list_t *remap_list, struct failed_shard *f_new)
{
	struct failed_shard 	*f_shard;
	d_list_t			*tmp;
	D_DEBUG(DB_PL,"fnew: %u",f_new->fs_shard_idx);

	/* All failed shards are sorted by fseq in ascending order */
	d_list_for_each_prev(tmp, remap_list) {
		f_shard = d_list_entry(tmp, struct failed_shard, fs_list);
		/*
		* Since we can only reuild one target at a time, the
		* target fseq should be assigned uniquely, even if all
		* the targets of the same domain failed at same time.
		*/
		D_DEBUG(DB_PL,"fnew: %u, fshard: %u",f_new->fs_shard_idx,
				f_shard->fs_shard_idx);
		D_ASSERTF(f_new->fs_fseq != f_shard->fs_fseq,
		"same fseq %u!\n", f_new->fs_fseq);

		if (f_new->fs_fseq < f_shard->fs_fseq)
			continue;
		d_list_add(&f_new->fs_list, tmp);
		return;
	}
	d_list_add(&f_new->fs_list, remap_list);
}

/** allocate one failed shard then add it into remap list */
static int
remap_alloc_one(d_list_t *remap_list, unsigned int shard_idx,
		struct pool_target *tgt)
{
	struct failed_shard *f_new;

	D_ALLOC_PTR(f_new);
	if (f_new == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&f_new->fs_list);
	f_new->fs_shard_idx = shard_idx;
	f_new->fs_fseq = tgt->ta_comp.co_fseq;
	f_new->fs_status = tgt->ta_comp.co_status;
	f_new->fs_tgt_id = -1;

	remap_add_one(remap_list, f_new);
	return 0;
}


/** free all elements in the remap list */
static void
mapless_remap_free_all(d_list_t *remap_list)
{
	struct failed_shard *f_shard, *f_tmp;

	d_list_for_each_entry_safe(f_shard, f_tmp, remap_list, fs_list) {
		d_list_del_init(&f_shard->fs_list);
		D_FREE(f_shard);
	}
}

static int
mapless_obj_placement_get(struct pl_mapless_map *mmap, struct daos_obj_md *md,
	struct daos_obj_shard_md *shard_md, struct mapless_obj_placement *mop)
{
	struct daos_oclass_attr *oc_attr;
	struct pool_domain	*root;
	daos_obj_id_t 		oid;

	/* Get the Object ID and the Object class */
	oid = md->omd_id;
	oc_attr = daos_oclass_attr_find(oid);

	if (oc_attr == NULL) {
		D_ERROR("Can not find obj class, invlaid oid="DF_OID"\n",
			DP_OID(oid));
		return -DER_INVAL;
	}

	/* Retrieve group size and count */
	mop->mop_grp_size = daos_oclass_grp_size(oc_attr);

	pool_map_find_domain(mmap->mmp_map.pl_poolmap, PO_COMP_TP_ROOT,
			PO_COMP_ID_ALL, &root);

	if(mop->mop_grp_size == DAOS_OBJ_REPL_MAX)
		mop->mop_grp_size = mmap->mmp_domain_nr;

	if (mop->mop_grp_size > mmap->mmp_domain_nr) {
		D_ERROR("obj="DF_OID": grp size (%u) (%u) is larger than "
			"domain nr (%u)\n", DP_OID(oid), mop->mop_grp_size,
			DAOS_OBJ_REPL_MAX, mmap->mmp_domain_nr);
		return -DER_INVAL;
	}

	D_ASSERT(root->do_target_nr > 0);
	if(shard_md == NULL) {
		unsigned int grp_max = root->do_target_nr / mop->mop_grp_size;

		if (grp_max == 0)
			grp_max = 1;

		mop->mop_grp_nr = daos_oclass_grp_nr(oc_attr, md);

		if(mop->mop_grp_nr > grp_max)
			mop->mop_grp_nr = grp_max;

	} else {
		mop->mop_grp_nr = 1;
	}

	D_ASSERT(mop->mop_grp_nr > 0);
        D_ASSERT(mop->mop_grp_size > 0);

        D_DEBUG(DB_PL,
                "obj="DF_OID"/ grp_size=%u grp_nr=%d\n",
                DP_OID(oid), mop->mop_grp_size, mop->mop_grp_nr);

	return 0;
}

/**
 * Given object placement @rop, calculate the next spare target start
 * from @spare_idx. Return false if no available spare, otherwise, return
 * true and next spare index in @spare_idx.
 */
static bool
mapless_remap_next_spare(struct pl_mapless_map *mmap,
	struct mapless_obj_placement *mop, struct pool_target *target)
{
	D_ASSERTF(mop->mop_grp_size <= mmap->mmp_domain_nr,
		"grp_size: %u > domain_nr: %u\n",
		mop->mop_grp_size, mmap->mmp_domain_nr);

	if (mop->mop_grp_size == mmap->mmp_domain_nr &&
			mop->mop_grp_size > 1)
		return false;

	return true;
}

/**
 * This function converts a generic pl_map pointer into the
 * proper placement map. This function assumes that the original
 * map was allocated as a pl_mapless_map with a pl_map as it's
 * first member.
 *
 * \param[in]   *map    A pointer to a pl_map which is the first
 *                      member of the specific placement map we're
 *                      converting to.
 *
 * \return      A pointer to the full pl_mapless_map used for
 *              object placement, rebuild, and reintegration
 */
static inline struct pl_mapless_map *
pl_map2mplmap(struct pl_map *map)
{
	return container_of(map, struct pl_mapless_map, mmp_map);
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
	   uint64_t obj_key, uint8_t *dom_used, uint32_t *used_targets,
	   int shard_num, uint32_t num_bytes)
{
	uint8_t		found_target = 0;
	uint8_t		top = 0;
	uint32_t	fail_num = 0;
	uint32_t	selected_dom;
	uint64_t	root_pos;

	root_pos = (uint64_t)curr_dom;

	do {
		uint32_t	num_doms;
		uint64_t	key;
		uint64_t	curr_pos;

		/* Retrieve number of nodes in this domain */
		if (curr_dom->do_children == NULL)
			num_doms = curr_dom->do_target_nr;
		else
			num_doms = curr_dom->do_child_nr;

		key = obj_key;
		/* If choosing target in lowest fault domain level */

		curr_pos = (uint64_t)curr_dom - root_pos;
		curr_pos = curr_pos / sizeof(struct pool_domain);

		if (curr_dom->do_children == NULL) {
			uint32_t dom_id;
			uint32_t	i;

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
				for (i = 0; used_targets[i] != 0; ++i) {
					if (used_targets[i] == dom_id + 1)
						break;
				}
			} while (used_targets[i]);

			/* Mark target as used */
			used_targets[i] = dom_id + 1;

			/* Found target (which may be available or not) */
			found_target = 1;
		} else {

			int		range_set;
			uint64_t	child_pos;

			child_pos = (uint64_t)(curr_dom->do_children)
				- root_pos;
			child_pos = child_pos / sizeof(struct pool_domain);

			/*
			 * If all of the nodes have been used for shards but we
			 * still have shards to place mark all nodes as unused
			 * so duplicates can be chosen
			 */

			range_set = is_range_set(dom_used, child_pos, child_pos
					+ num_doms - 1, num_bytes);
			if (range_set  && curr_dom->do_children != NULL) {
				clear_bitmap_range(dom_used, child_pos,
						child_pos + (num_doms - 1),
						num_bytes);
			}

			/*
			 * Keep choosing new domains until one that has
			 * not been used is found
			 */
			do {

				selected_dom = jump_consistent_hash(key,
								    num_doms);
				key = crc(key, fail_num++);
			} while (get_bit(dom_used, selected_dom + child_pos,
						num_bytes)
					== 1);
			/* Mark this domain as used */
			set_bit(dom_used, selected_dom + child_pos, num_bytes);

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
 * \param[in]   root            The top level domain that is being used to
 *                              determine the target location for this shard.
 *                              The root node of the pool map.
 * \param[out]  target		Holds the value of the new target
 *				for the shard being rebuilt.
 * \param[in]   key             A unique key generated using the object ID.
 *                              This is the same key used during initial
 *                              placement.
 *                              This is used in jump consistent hash.
 * \param[in]   dom_used        This is a contiguous array that contains
 *                              information on whether or not an internal node
 *                              (non-target) in a domain has been used.
 * \param[in]   shard_num       the current shard number. This is used when
 *                              selecting a target to determine if repeated
 *                              targets are allowed in the case that there
 *                              are more shards than targets
 * \param[in]	layout		This is the current layout for the object.
 *				This is needed for guaranteeing that we don't
 *				reuse a target already in the layout.
 *
 * \return			Returns an Error code if an error occurred,
 *				otherwise 0.
 */
static int
get_rebuild_target(struct pool_map *pmap, struct pool_target **target,
		   uint64_t key, uint8_t *dom_used,
		   struct pl_obj_layout *layout, struct daos_obj_md *md,
		   uint32_t dom_bytes)
{
	uint8_t                 *used_tgts = NULL;
	uint32_t                selected_dom;
	uint32_t                fail_num = 0xFFc5;
	uint32_t                try = 0;
	struct pool_domain      *target_selection;
	struct pool_domain	*root;
	int rc = 0;

	rc = pool_map_find_domain(pmap, PO_COMP_TP_ROOT, PO_COMP_ID_ALL, &root);

	if (rc == 0) {
		D_ERROR("Could not find root node in pool map.");
		rc = -DER_NONEXIST;
		return rc;
	}

	while (1) {

		uint8_t skiped_targets = 0;
		uint16_t num_bytes = 0;
		uint32_t num_doms;

		num_doms = root->do_child_nr;

		uint64_t child_pos = (uint64_t)(root->do_children)
			- (uint64_t)root;
		child_pos = child_pos / sizeof(struct pool_domain);

		/*
		 * If all of the nodes have been used for shards but we
		 * still have shards to place mark all nodes as unused
		 * so duplicates can be chosen
		 */
		if (is_range_set(dom_used, child_pos, child_pos +
			num_doms - 1, dom_bytes)) {
			clear_bitmap_range(dom_used, child_pos,
					child_pos + (num_doms - 1), dom_bytes);
		}

		/*
		 * Choose domains using jump consistent hash until we find a
		 * suitable domains that has not already been used.
		 */
		do {
			key = crc(key, fail_num++);
			selected_dom = jump_consistent_hash(key, num_doms);
			target_selection = &(root->do_children[selected_dom]);
		} while (get_bit(dom_used, selected_dom + child_pos,
					dom_bytes) == 1);

		/* Mark this domain as used */
		set_bit(dom_used, selected_dom + child_pos, dom_bytes);

		/* To find rebuild target we examine all targets */
		num_doms = target_selection->do_target_nr;

		num_bytes = (num_doms / 8) + 1;

		D_ALLOC_ARRAY(used_tgts, num_bytes);
		if (used_tgts == NULL)
			return -DER_NOMEM;

		/*
		 * Add the initial layouts targets to the bitmap of checked
		 * targets.
		 */
		int i;
		uint64_t start_pos;
		uint64_t end_pos;

		start_pos = (uint64_t)(&((target_selection->do_targets[0])));
		end_pos = start_pos + (num_doms * sizeof(**target));

		for (i = 0; i < layout->ol_nr; ++i) {
			int id = layout->ol_shards[i].po_target;
			uint64_t position;

			pool_map_find_target(pmap, id, target);
			position = (uint64_t) (*target);


			if (position >= start_pos && position < end_pos) {
				set_bit(used_tgts, (position - start_pos)
						/ sizeof(**target), num_bytes);
				skiped_targets++;
			}
		}


		/*
		 * Attempt to choose a fallback target from all targets found
		 * in this top level domain.
		 */
		do {
			key = crc(key, try++);

			selected_dom = jump_consistent_hash(key, num_doms);
			*target = &target_selection->do_targets[selected_dom];

			/*
			 * keep track of what targets have been tried
			 * in case all targets in domain have failed
			 */
			if (get_bit(used_tgts, selected_dom, num_bytes) == 0)
				skiped_targets++;

			if (pool_target_unavail(*target))
				set_bit(used_tgts, selected_dom, num_bytes);

		} while (get_bit(used_tgts, selected_dom, num_bytes) &&
				skiped_targets < num_doms);

		if(skiped_targets == num_doms)
			D_DEBUG(DB_PL, "Skipped all targets in domain, "
					"no valid slections.\n");

		D_FREE(used_tgts);

		/* Use the last examined target if it's not unavailable */
		if (!pool_target_unavail(*target) ||
		   (*target)->ta_comp.co_fseq > md->omd_ver){
			return 0;
		}
	}
	/* Should not reach this point */
	D_ERROR("Unexpectedly reached end of placement loop without result");
	D_ASSERT(0);
	return DER_INVAL;
}

/** dump remap list, for debug only */
static void
mapless_remap_dump(d_list_t *remap_list, struct daos_obj_md *md,
                char *comment)
{
        struct failed_shard *f_shard;

        D_DEBUG(DB_PL, "remap list for "DF_OID", %s, ver %d\n",
                DP_OID(md->omd_id), comment, md->omd_ver);

        d_list_for_each_entry(f_shard, remap_list, fs_list) {
                D_DEBUG(DB_PL, "fseq:%u, shard_idx:%u status:%u rank %d\n",
                        f_shard->fs_fseq, f_shard->fs_shard_idx,
                        f_shard->fs_status, f_shard->fs_tgt_id);
        }
}


/**
* Try to remap all the failed shards in the @remap_list to proper
* targets respectively. The new target id will be updated in the
* @layout if the remap succeed, otherwise, corresponding shard id
* and target id in @layout will be cleared as -1.
*/
static void
obj_remap_shards(struct pl_mapless_map *mmap, struct daos_obj_md *md,
		struct pl_obj_layout *layout, struct mapless_obj_placement *mop,
		d_list_t *remap_list, uint8_t *dom_used, uint64_t key)
{
	struct failed_shard *f_shard, *f_tmp;
	struct pl_obj_shard      *l_shard;
	struct pool_target       *spare_tgt;
	d_list_t                 *current;
	bool                      spare_avail = true;
	int 			fail_count;

	mapless_remap_dump(remap_list, md, "before remap:");
	current = remap_list->next;
	spare_tgt = NULL;
	fail_count = 0;

	while (current != remap_list) {
		f_shard = d_list_entry(current, struct failed_shard,
					fs_list);
		l_shard = &layout->ol_shards[f_shard->fs_shard_idx];

		spare_avail = mapless_remap_next_spare(mmap, mop, spare_tgt);

		if (!spare_avail)
			goto next_fail;

		get_rebuild_target(mmap->mmp_map.pl_poolmap, &spare_tgt,
		   crc(key,f_shard->fs_shard_idx * 10 + fail_count++), dom_used,
		   layout, md, mmap->dom_used_length);

		/* The selected spare target is down as well */
		if (pool_target_unavail(spare_tgt)) {
				D_ASSERTF(spare_tgt->ta_comp.co_fseq !=
					f_shard->fs_fseq, "same fseq %u!\n",
					f_shard->fs_fseq);

			/* If the spare target fseq > the current object pool
			* version, the current failure shard will be handled
			* by the following rebuild.
			*/
			if (spare_tgt->ta_comp.co_fseq > md->omd_ver) {
				D_DEBUG(DB_PL, DF_OID", fseq %d rank %d"
					" ver %d\n", DP_OID(md->omd_id),
					spare_tgt->ta_comp.co_fseq,
					spare_tgt->ta_comp.co_rank,
					md->omd_ver);
				spare_avail = false;
				goto next_fail;
			}

			/*
			* The selected spare is down prior to current failed
			* one, then it can't be a valid spare, let's skip it
			* and try next spare on the ring.
			*/
			if (spare_tgt->ta_comp.co_fseq < f_shard->fs_fseq)
				continue; /* try next spare */

			/*
			* If both failed target and spare target are down, then
			* add the spare target to the fail list for remap, and
			* try next spare on the ring.
			*/
			if (f_shard->fs_status == PO_COMP_ST_DOWN)
				D_ASSERTF(spare_tgt->ta_comp.co_status !=
					PO_COMP_ST_DOWNOUT,
					"down fseq(%u) < downout fseq(%u)\n",
					f_shard->fs_fseq,
					spare_tgt->ta_comp.co_fseq);

			f_shard->fs_fseq = spare_tgt->ta_comp.co_fseq;
			f_shard->fs_status = spare_tgt->ta_comp.co_status;

			current = current->next;
			d_list_del_init(&f_shard->fs_list);
			remap_add_one(remap_list, f_shard);

			/* Continue with the failed shard has minimal fseq */
			if (current == remap_list) {
				current = &f_shard->fs_list;
			} else {
				f_tmp = d_list_entry(current,
						struct failed_shard,
						fs_list);
				if (f_shard->fs_fseq < f_tmp->fs_fseq)
					current = &f_shard->fs_list;
			}
			continue; /* try next spare */
		}
next_fail:
		fail_count = 0;
		if (spare_avail) {
			/* The selected spare target is up and ready */
			l_shard->po_target = spare_tgt->ta_comp.co_id;
			l_shard->po_fseq = f_shard->fs_fseq;

			/*
			* Mark the shard as 'rebuilding' so that read will
			* skip this shard.
			*/
			if (f_shard->fs_status == PO_COMP_ST_DOWN) {
				l_shard->po_rebuilding = 1;
				f_shard->fs_tgt_id = spare_tgt->ta_comp.co_id;
			}
		} else {
			l_shard->po_shard = -1;
			l_shard->po_target = -1;
		}
		current = current->next;
	}

	mapless_remap_dump(remap_list, md, "after remap:");
}

static int
mapless_obj_spec_place_get(struct pl_mapless_map *mmap, daos_obj_id_t oid,
			struct pool_target **target, uint8_t *dom_used,
			uint32_t dom_bytes)
{
	struct pool_target      *tgts;
	unsigned int            tgts_nr;
	d_rank_t                rank;
	int                     tgt;
	unsigned int            pos;
	int rc;

	D_ASSERT(daos_obj_id2class(oid) == DAOS_OC_R3S_SPEC_RANK ||
	daos_obj_id2class(oid) == DAOS_OC_R1S_SPEC_RANK ||
	daos_obj_id2class(oid) == DAOS_OC_R2S_SPEC_RANK);

	/* locate rank in the pool map targets */
	tgts = pool_map_targets(mmap->mmp_map.pl_poolmap);
	tgts_nr = pool_map_target_nr(mmap->mmp_map.pl_poolmap);
	/* locate rank in the pool map targets */
	rank = daos_oclass_sr_get_rank(oid);
	tgt = daos_oclass_st_get_tgt(oid);

	for (pos = 0; pos < tgts_nr; pos++) {
		if (rank == tgts[pos].ta_comp.co_rank &&
		(tgt == tgts[pos].ta_comp.co_index))
		break;
	}
	if (pos == tgts_nr)
		return -DER_INVAL;

	*target = &(tgts[pos]);


	struct pool_domain *current_dom;
	struct pool_domain * root;

	rc = pool_map_find_domain(mmap->mmp_map.pl_poolmap, PO_COMP_TP_ROOT,
			PO_COMP_ID_ALL, &root);
	if (rc == 0) {
		D_ERROR("Could not find root node in pool map.");
		return -DER_NONEXIST;
	}
	current_dom = root;

	while(current_dom->do_children != NULL) {
		struct pool_domain *temp_dom;
		int index;
		uint64_t child_pos;

		child_pos = (current_dom->do_children) - root;

		for(index = 0; index < current_dom->do_child_nr; ++index) {
			temp_dom = &(current_dom->do_children[index]);
			int num_children = temp_dom->do_target_nr;
			int last = num_children-1;
			struct pool_target *start = &(temp_dom->do_targets[0]);
			struct pool_target *end = &(temp_dom->do_targets[last]);

			if((start <= (*target)) && ((*target) <= end)) {
				current_dom = temp_dom;
				set_bit(dom_used, (index + child_pos),
						dom_bytes);
				break;
			}
		}
	}

	return 0;
}

/**
 * This function handles getting the initial layout for the object as well as
 * finding the rebuild targets if any of the targets are unavailable. If this
 * function is called from the rebuild interface then it also returns the list
 * of targets that need to be remapped.
 *
 * \param[in]   root            This is the root of the pool map tree structure
 *                              containing all the fault domain information.
 * \param[out]  layout          This structure will contained the ordered list
 *                              of targets to be used for the object layout.
 * \param[in]   com_map         This is the collision map used during the
 *                              placement process for bookkeeping and
 *                              determining which domains have been used.
 * \param[in]   grp_cnt       This number of redundancy groups for this
 *                              objects layout.
 * \param[in]   grp_size      The size of each redundancy group in the layout.
 *                              This will be multiplied with group_cnt to get
 *                              the total number of targets required for
 *                              placement.
 * \param[in]   dom_map_size    The number of non-leaf nodes in the pool map.
 *                              This value will be used to initialize the array
 *                              that holds the statuses of whether the node has
 *                              been used or not.
 * \param[in]   cnt_map_size    The number of child domains used for a given
 *                              non-leaf node in the pool map. When this equals
 *                              the total number of child nodes we reset the
 *                              status showing the nodes have been used.
 * \param[out]  rebuild_list      The list of nodes that needs to be rebuilt
 *                              during the rebuild process. This is only
 *                              returned when this function is called from the
 *                              rebuild function and will otherwise be NULL.
 *
 * \return                      Returns the number of targets that need to be
 *                              rebuilt. This number will be ignored when called
 *                              from functions other than find rebuild.
 */
static int
get_object_layout(struct pl_mapless_map *mmap, struct pl_obj_layout *layout,
		struct mapless_obj_placement *mop, daos_obj_id_t oid,
		d_list_t *remap_list, struct daos_obj_md *md)
{
	struct pool_target	*target;
	uint8_t			*dom_used;
	uint32_t		*used_targets;
	uint64_t		key;
	struct pool_domain 	*root;
	int i, j, k, rc;

	/* Set the pool map version */
	layout->ol_ver = pl_map_version(&(mmap->mmp_map));

	j = 0;
	k = 0;
	key = oid.lo;

	rc = 0;
	target = NULL;

	D_ALLOC_ARRAY(dom_used, mmap->dom_used_length);
	D_ALLOC_ARRAY(used_targets, layout->ol_nr + 1);

	if (dom_used == NULL)
		return -DER_NOMEM;
	if (used_targets == NULL)
		return -DER_NOMEM;

	if (daos_obj_id2class(oid) == DAOS_OC_R3S_SPEC_RANK ||
			daos_obj_id2class(oid) == DAOS_OC_R1S_SPEC_RANK ||
		    	daos_obj_id2class(oid) == DAOS_OC_R2S_SPEC_RANK) {

		rc = mapless_obj_spec_place_get(mmap, oid, &target, dom_used,
				mmap->dom_used_length);

		if (rc) {
			D_ERROR("special oid "DF_OID" failed: rc %d\n",
				DP_OID(oid), rc);
			return rc;
                }

		layout->ol_shards[0].po_target = target->ta_comp.co_id;
		layout->ol_shards[0].po_shard = 0;
		layout->ol_shards[0].po_fseq = target->ta_comp.co_fseq;

		if (pool_target_unavail(target)) {
			rc = remap_alloc_one(remap_list, 0, target);
			if (rc)
				D_GOTO(out, rc);
		}

		j = 1; k = 1;
		key = oid.lo;

	 }

	rc = pool_map_find_domain(mmap->mmp_map.pl_poolmap, PO_COMP_TP_ROOT,
			PO_COMP_ID_ALL, &root);
	if (rc == 0) {
		D_ERROR("Could not find root node in pool map.");
		rc = -DER_NONEXIST;
		goto out;
	}

	for (i = 0; i < mop->mop_grp_nr; i++) {
		for (; j < mop->mop_grp_size; j++, k++) {
			uint32_t tgt_id;
			uint32_t fseq;

			get_target(root, &target, crc(key, k), dom_used,
				used_targets, k, mmap->dom_used_length);

			tgt_id = target->ta_comp.co_id;
			fseq = target->ta_comp.co_fseq;

			layout->ol_shards[k].po_target = tgt_id;
			layout->ol_shards[k].po_shard = k;
			layout->ol_shards[k].po_fseq = fseq;

			if (pool_target_unavail(target)) {
				rc = remap_alloc_one(remap_list, k, target);
				if (rc)
					D_GOTO(out, rc);
			}
		}
		j = 0;
	}

	obj_remap_shards(mmap, md, layout, mop, remap_list, dom_used, key);
out:
	if(rc) {
		D_ERROR("mapless_obj_layout_fill failed, rc %d.\n", rc);
		mapless_remap_free_all(remap_list);
	}

	D_FREE(used_targets);
	D_FREE(dom_used);
	return rc;
}

/**
 * Frees the placement map and the
 *
 * \param[in]   map     The placement map to be freed
 *
 * \return              void
 */
static void
mapless_jump_map_destroy(struct pl_map *map)
{
	struct pl_mapless_map   *mmap;

	mmap = pl_map2mplmap(map);

	pool_map_decref(mmap->mmp_map.pl_poolmap);

	D_FREE(mmap);
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
mapless_jump_map_create(struct pool_map *poolmap, struct pl_map_init_attr *mia,
			struct pl_map **mapp)
{
	struct pool_domain      *root;
	struct pl_mapless_map   *mmap;
	struct pool_domain	*doms;
	int                     rc;

	D_ALLOC_PTR(mmap);
	if (mmap == NULL)
		return -DER_NOMEM;

	pool_map_addref(poolmap);
	mmap->mmp_map.pl_poolmap = poolmap;
	pool_map_print(poolmap);
	rc = pool_map_find_domain(mmap->mmp_map.pl_poolmap, PO_COMP_TP_ROOT,
			PO_COMP_ID_ALL, &root);
	if (rc == 0) {
		D_ERROR("Could not find root node in pool map.");
		rc = -DER_NONEXIST;
		goto ERR;
	}

	rc = pool_map_find_domain(mmap->mmp_map.pl_poolmap,
			mia->ia_mapless.domain, PO_COMP_ID_ALL, &doms);
        if (rc <= 0) {
                rc = (rc == 0) ? -DER_INVAL : rc;
		goto ERR;
	}

	mmap->mmp_domain_nr = rc;
	mmap->dom_used_length = (get_dom_cnt(root) / 8) + 1;
	*mapp = &mmap->mmp_map;

	return DER_SUCCESS;

ERR:
	mapless_jump_map_destroy(&mmap->mmp_map);
	return rc;
}



static void
mapless_jump_map_print(struct pl_map *map)
{
	/* TODO: Print out the collision map */
}

/**
 * Determines the locations that a given object shard should be located.
 *
 * \param[in]   map             A reference to the placement map being used to
 *                              place the object shard.
 * \param[in]   md              The object metadata which contains data about
 *                              the object being placed such as the object ID.
 * \param[in]   shard_md
 * \param[out]  layout_pp       The layout generated for the object. Contains
 *                              references to the targets in the pool map where
 *                              the shards will be placed.
 *
 * \return                      An integer value containing the error
 *                              message or 0 if the  function returned
 *                              successfully.
 */
static int
mapless_obj_place(struct pl_map *map, struct daos_obj_md *md,
		  struct daos_obj_shard_md *shard_md,
		  struct pl_obj_layout **layout_pp)
{
	struct pl_mapless_map		*mmap;
	struct pl_obj_layout		*layout;
	struct mapless_obj_placement 	mop;
	d_list_t			remap_list;
	daos_obj_id_t           	oid;
	int				rc;

	mmap = pl_map2mplmap(map);
	oid = md->omd_id;

	rc = mapless_obj_placement_get(mmap, md, shard_md, &mop);
	if (rc) {
                D_ERROR("mapless_obj_placement_get failed, rc %d.\n", rc);
                return rc;
        }

	/* Allocate space to hold the layout */
	rc = pl_obj_layout_alloc(mop.mop_grp_nr * mop.mop_grp_size, &layout);
	if (rc != 0) {
		D_ERROR("pl_obj_layout_alloc failed, rc %d.\n", rc);
		return rc;
	}

	/* Get root node of pool map */
	D_INIT_LIST_HEAD(&remap_list);
	rc = get_object_layout(mmap, layout, &mop, oid, &remap_list, md);
	if (rc < 0) {
		D_ERROR("Could not generate placement layout, rc %d.\n", rc);
		pl_obj_layout_free(layout);
		mapless_remap_free_all(&remap_list);
		return rc;
	}

	*layout_pp = layout;
	mapless_remap_free_all(&remap_list);
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
mapless_obj_find_rebuild(struct pl_map *map, struct daos_obj_md *md,
			 struct daos_obj_shard_md *shard_md,
			 uint32_t rebuild_ver, uint32_t *tgt_id,
			 uint32_t *shard_idx, unsigned int array_size,
			 int myrank)
{
	struct pl_mapless_map   	*mmap;
	struct pl_obj_layout    	*layout;
	d_list_t		   	remap_list;
	struct failed_shard  		*f_shard;
	struct pl_obj_shard       	*l_shard;
	struct mapless_obj_placement 	mop;
	daos_obj_id_t           	oid;
	int 				rc;

	int idx = 0;

	/* Caller should guarantee the pl_map is up-to-date */
	if (pl_map_version(map) < rebuild_ver) {
		D_ERROR("pl_map version(%u) < rebuild version(%u)\n",
			pl_map_version(map), rebuild_ver);
		return -DER_INVAL;
	}

	mmap = pl_map2mplmap(map);
	oid = md->omd_id;

	rc = mapless_obj_placement_get(mmap, md, shard_md, &mop);
	if (rc) {
                D_ERROR("mapless_obj_placement_get failed, rc %d.\n", rc);
                return rc;
        }

	if (mop.mop_grp_size == 1) {
                D_DEBUG(DB_PL, "Not replicated object "DF_OID"\n",
                        DP_OID(md->omd_id));
                return 0;
        }

	/* Allocate space to hold the layout */
	rc = pl_obj_layout_alloc(mop.mop_grp_size * mop.mop_grp_nr, &layout);
	if (rc) {
		D_ERROR("pl_obj_layout_alloc failed, rc %d.\n", rc);
		return rc;
	}

	D_INIT_LIST_HEAD(&remap_list);
	rc = get_object_layout(mmap, layout, &mop, oid,  &remap_list, md);

	if (rc < 0) {
		D_ERROR("Could not generate placement layout, rc %d.\n", rc);
		goto out;
	}

	d_list_for_each_entry(f_shard, &remap_list, fs_list) {
		l_shard = &layout->ol_shards[f_shard->fs_shard_idx];

		if (f_shard->fs_fseq > rebuild_ver)
			break;

		if (f_shard->fs_status == PO_COMP_ST_DOWN) {
			/*
			* Target id is used for rw, but rank is used
			* for rebuild, perhaps they should be unified.
			*/
			if (l_shard->po_shard != -1) {
				struct pool_target      *target;
				int                      leader;

				D_ASSERT(f_shard->fs_tgt_id != -1);
				D_ASSERT(idx < array_size);

				/* If the caller does not care about DTX related
				* things (myrank == -1), then fill it directly.
				*/
				if (myrank == -1)
					goto fill;

				leader = pl_select_leader(md->omd_id,
					l_shard->po_shard, layout->ol_nr,
					true, pl_obj_get_shard, layout);

				if (leader < 0) {
					D_WARN("Not sure whether current shard "
						"is leader or not for obj "
						DF_OID" , fseq:%d, status:%d, "
						"ver:%d, shard:%d, rc = %d\n",
						DP_OID(md->omd_id),
						f_shard->fs_fseq,
						f_shard->fs_status, rebuild_ver,
						l_shard->po_shard, leader);
						goto fill;
				}

				rc = pool_map_find_target(map->pl_poolmap,
							leader, &target);
				D_ASSERT(rc == 1);

				if (myrank != target->ta_comp.co_rank) {
					/* The leader shard is not on current
					* server, then current server cannot
					* know whether DTXs for current shard
					* have been re-synced or not. So skip
					* the shard that will be handled by
					* the leader on another server.
					*/
					D_DEBUG(DB_PL, "Current replica (%d)"
						"isn't the leader (%d) for obj "
						DF_OID", fseq:%d, status:%d, "
						"ver:%d, shard:%d, skip it\n",
						myrank, target->ta_comp.co_rank,
						DP_OID(md->omd_id),
						f_shard->fs_fseq,
						f_shard->fs_status,
						rebuild_ver, l_shard->po_shard);
					continue;
				}

fill:
				D_DEBUG(DB_PL, "Current replica (%d) is the "
					"leader for obj "DF_OID", fseq:%d, "
					"ver:%d, shard:%d, to be rebuilt.\n",
					myrank, DP_OID(md->omd_id),
					f_shard->fs_fseq,
					rebuild_ver, l_shard->po_shard);
				tgt_id[idx] = f_shard->fs_tgt_id;
				shard_idx[idx] = l_shard->po_shard;
				idx++;
			}
		} else if (f_shard->fs_tgt_id != -1) {
			rc = -DER_ALREADY;
			D_ERROR(""DF_OID" rebuild is done for "
				"fseq:%d(status:%d)? rbd_ver:%d rc %d\n",
				DP_OID(md->omd_id), f_shard->fs_fseq,
				f_shard->fs_status, rebuild_ver, rc);
		}
	}

out:
	mapless_remap_free_all(&remap_list);
	pl_obj_layout_free(layout);
	return rc < 0 ? rc : idx;
}

static int
mapless_obj_find_reint(struct pl_map *map, struct daos_obj_md *md,
		       struct daos_obj_shard_md *shard_md,
		       struct pl_target_grp *tgp_reint, uint32_t *tgt_reint)
{
	D_ERROR("Unsupported\n");
	return -DER_NOSYS;
}

/** API for generic placement map functionality */
struct pl_map_ops       mapless_map_ops = {
	.o_create               = mapless_jump_map_create,
	.o_destroy              = mapless_jump_map_destroy,
	.o_print                = mapless_jump_map_print,
	.o_obj_place            = mapless_obj_place,
	.o_obj_find_rebuild     = mapless_obj_find_rebuild,
	.o_obj_find_reint       = mapless_obj_find_reint,
};
