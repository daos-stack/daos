/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * LRU array implementation
 * vos/lru_array.c
 *
 * Author: Jeff Olivier <jeffrey.v.olivier@intel.com>
 */
#define D_LOGFAC DD_FAC(vos)
#include "lru_array.h"

/** Internal converter for real index to entity index in sub array */
#define ent2idx(array, sub, ent_idx)	\
	(((sub)->ls_array_idx << (array)->la_array_shift) + (ent_idx))

static void
evict_cb(struct lru_array *array, struct lru_sub *sub, struct lru_entry *entry,
	 uint32_t idx)
{
	uint32_t	real_idx;

	if (array->la_cbs.lru_on_evict == NULL) {
		/** By default, reset the entry */
		memset(entry->le_payload, 0, array->la_payload_size);
		return;
	}

	real_idx = ent2idx(array, sub, idx);

	array->la_evicting++;
	array->la_cbs.lru_on_evict(entry->le_payload, real_idx, array->la_arg);
	array->la_evicting--;
}

static void
init_cb(struct lru_array *array, struct lru_sub *sub, struct lru_entry *entry,
	uint32_t idx)
{
	uint32_t	real_idx;

	if (array->la_cbs.lru_on_init == NULL)
		return;

	real_idx = ent2idx(array, sub, idx);

	array->la_cbs.lru_on_init(entry->le_payload, real_idx, array->la_arg);
}

static void
fini_cb(struct lru_array *array, struct lru_sub *sub, struct lru_entry *entry,
	uint32_t idx)
{
	uint32_t	real_idx;

	if (array->la_cbs.lru_on_fini == NULL)
		return;

	real_idx = ent2idx(array, sub, idx);

	array->la_cbs.lru_on_fini(entry->le_payload, real_idx, array->la_arg);
}

int
lrua_array_alloc_one(struct lru_array *array, struct lru_sub *sub)
{
	struct lru_entry	*entry;
	char			*payload;
	size_t			 rec_size;
	uint32_t		 nr_ents = array->la_idx_mask + 1;
	uint32_t		 prev_idx = nr_ents - 1;
	uint32_t		 idx;

	rec_size = sizeof(*entry) + array->la_payload_size;
	D_ALLOC(sub->ls_table, rec_size * nr_ents);
	if (sub->ls_table == NULL)
		return -DER_NOMEM;

	/** Add newly allocated ones to head of list */
	d_list_del(&sub->ls_link);
	d_list_add(&sub->ls_link, &array->la_free_sub);

	payload = sub->ls_payload = &sub->ls_table[nr_ents];
	sub->ls_lru = LRU_NO_IDX;
	sub->ls_free = 0;
	for (idx = 0; idx < nr_ents; idx++) {
		entry = &sub->ls_table[idx];
		entry->le_payload = payload;
		entry->le_prev_idx = prev_idx;
		entry->le_next_idx = (idx + 1) & array->la_idx_mask;
		init_cb(array, sub, entry, idx);
		payload += array->la_payload_size;
		prev_idx = idx;
	}

	return 0;
}

static inline bool
sub_find_free(struct lru_array *array, struct lru_sub *sub,
	      struct lru_entry **entryp, uint32_t *idx, uint64_t key)
{
	struct lru_entry	*entry;
	uint32_t		 tree_idx;

	if (sub->ls_free == LRU_NO_IDX)
		return false;

	tree_idx = sub->ls_free;

	entry = &sub->ls_table[tree_idx];

	/** Remove from free list */
	lrua_remove_entry(sub, &sub->ls_free, entry, tree_idx);

	/** Insert at tail (mru) */
	lrua_insert(sub, &sub->ls_lru, entry, tree_idx, true);

	entry->le_key = key;

	*entryp = entry;

	*idx = ent2idx(array, sub, tree_idx);

	return true;
}

static inline int
manual_find_free(struct lru_array *array, struct lru_entry **entryp,
		 uint32_t *idx, uint64_t key)
{
	struct lru_sub	*sub = NULL;
	bool		 found;
	int		 rc;

	/** First search already allocated lists */
	d_list_for_each_entry(sub, &array->la_free_sub, ls_link) {
		if (sub_find_free(array, sub, entryp, idx, key)) {
			if (sub->ls_free == LRU_NO_IDX) {
				/** Remove the entry from the free sub list so
				 * we stop looking in it.
				 */
				d_list_del(&sub->ls_link);
			}
			return 0;
		}
	}

	/** No free entries */
	if (d_list_empty(&array->la_unused_sub))
		return -DER_BUSY;; /* No free sub arrays either */

	sub = d_list_entry(array->la_unused_sub.next, struct lru_sub, ls_link);
	rc = lrua_array_alloc_one(array, sub);
	if (rc != 0)
		return rc;

	found = sub_find_free(array, sub, entryp, idx, key);
	D_ASSERT(found);

	return 0;
}

int
lrua_find_free(struct lru_array *array, struct lru_entry **entryp,
	       uint32_t *idx, uint64_t key)
{
	struct lru_sub		*sub;
	struct lru_entry	*entry;

	*entryp = NULL;

	if (array->la_flags & LRU_FLAG_EVICT_MANUAL) {
		return manual_find_free(array, entryp, idx, key);
	}

	sub = &array->la_sub[0];
	if (sub_find_free(array, sub, entryp, idx, key))
		return 0;

	entry = &sub->ls_table[sub->ls_lru];
	/** Key should not be 0, otherwise, it should be in free list */
	D_ASSERT(entry->le_key != 0);

	evict_cb(array, sub, entry, sub->ls_lru);

	*idx = ent2idx(array, sub, sub->ls_lru);
	entry->le_key = key;
	sub->ls_lru = entry->le_next_idx;

	*entryp = entry;

	return 0;
}

void
lrua_evictx(struct lru_array *array, uint32_t idx, uint64_t key)
{
	struct lru_entry	*entry;
	struct lru_sub		*sub;
	uint32_t		 ent_idx;

	D_ASSERT(array != NULL);
	D_ASSERT(key != 0);

	if (idx >= array->la_count)
		return;

	sub = lrua_idx2sub(array, idx);
	ent_idx = lrua_idx2ent(array, idx);

	if (sub->ls_table == NULL)
		return;

	entry = &sub->ls_table[ent_idx];
	if (key != entry->le_key)
		return;

	evict_cb(array, sub, entry, ent_idx);

	entry->le_key = 0;

	/** Remove from active list */
	lrua_remove_entry(sub, &sub->ls_lru, entry, ent_idx);

	if (sub->ls_free == LRU_NO_IDX &&
	    (array->la_flags & LRU_FLAG_EVICT_MANUAL)) {
		/** Add the entry back to the free list */
		d_list_add_tail(&sub->ls_link, &array->la_free_sub);
	}

	/** Insert in free list */
	lrua_insert(sub, &sub->ls_free, entry, ent_idx,
		    (array->la_flags & LRU_FLAG_REUSE_UNIQUE) != 0);
}

int
lrua_array_alloc(struct lru_array **arrayp, uint32_t nr_ent, uint32_t nr_arrays,
		 uint16_t payload_size, uint32_t flags,
		 const struct lru_callbacks *cbs, void *arg)
{
	struct lru_array	*array;
	uint32_t		 aligned_size;
	uint32_t		 idx;
	int			 rc;

	D_ASSERT(arrayp != NULL);
	/** The prev != next assertions require the array to have a minimum
	 *  size of 3.   Just assert this precondition.
	 */
	D_ASSERT(nr_ent > 2);

	/** nr_ent and nr_arrays need to be powers of two and nr_arrays
	 *  must be less than nr_ent.  This enables faster lookups by using
	 *  & operations rather than % operations
	 */
	D_ASSERT((nr_ent & (nr_ent - 1)) == 0);
	D_ASSERT((nr_arrays & (nr_arrays - 1)) == 0);
	D_ASSERT(nr_arrays != 0);
	D_ASSERT(nr_ent > nr_arrays);

	if (nr_arrays != 1) {
		/** No good algorithm for auto eviction across multiple
		 *  sub arrays since one lru is maintained per sub array
		 */
		flags |= LRU_FLAG_EVICT_MANUAL;
	}

	aligned_size = (payload_size + 7) & ~7;

	*arrayp = NULL;

	D_ALLOC(array, sizeof(*array) +
		(sizeof(array->la_sub[0]) * nr_arrays));
	if (array == NULL)
		return -DER_NOMEM;

	array->la_count = nr_ent;
	array->la_idx_mask = (nr_ent / nr_arrays) - 1;
	array->la_array_nr = nr_arrays;
	array->la_array_shift = 1;
	while ((1 << array->la_array_shift) < array->la_idx_mask)
		array->la_array_shift++;
	array->la_payload_size = aligned_size;
	array->la_flags = flags;
	array->la_arg = arg;
	if (cbs != NULL)
		array->la_cbs = *cbs;

	/** Only allocate one sub array, add the rest to free list */
	D_INIT_LIST_HEAD(&array->la_free_sub);
	D_INIT_LIST_HEAD(&array->la_unused_sub);
	for (idx = 0; idx < nr_arrays; idx++) {
		array->la_sub[idx].ls_array_idx = idx;
		d_list_add_tail(&array->la_sub[idx].ls_link,
				&array->la_unused_sub);
	}

	rc = lrua_array_alloc_one(array, &array->la_sub[0]);
	if (rc != 0) {
		D_FREE(array);
		return rc;
	}

	*arrayp = array;

	return 0;
}

static void
array_free_one(struct lru_array *array, struct lru_sub *sub)
{
	uint32_t	idx;

	for (idx = 0; idx < array->la_idx_mask + 1; idx++)
		fini_cb(array, sub, &sub->ls_table[idx], idx);

	D_FREE(sub->ls_table);
}

void
lrua_array_free(struct lru_array *array)
{
	struct lru_sub	*sub;
	uint32_t	 i;

	if (array == NULL)
		return;


	for (i = 0; i < array->la_array_nr; i++) {
		sub = &array->la_sub[i];
		if (sub->ls_table != NULL)
			array_free_one(array, sub);
	}

	D_FREE(array);
}

void
lrua_array_aggregate(struct lru_array *array)
{
	struct lru_sub	*sub;
	struct lru_sub	*tmp;

	if ((array->la_flags & LRU_FLAG_EVICT_MANUAL) == 0)
		return; /* Not applicable */

	if (d_list_empty(&array->la_free_sub))
		return; /* Nothing to do */

	/** Grab the 2nd entry (may be head in which case the loop will  be a
	 *  noop).   This leaves some free entries in the array.
	 */
	sub = d_list_entry(array->la_free_sub.next->next, struct lru_sub,
			   ls_link);

	d_list_for_each_entry_safe_from(sub, tmp, &array->la_free_sub,
					ls_link) {
		if (sub->ls_lru != LRU_NO_IDX)
			continue; /** Used entries */
		d_list_del(&sub->ls_link);
		d_list_add_tail(&sub->ls_link, &array->la_unused_sub);
		array_free_one(array, sub);
	}
}
