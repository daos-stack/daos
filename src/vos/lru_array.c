/**
 * (C) Copyright 2020 Intel Corporation.
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
 * LRU array implementation
 * vos/lru_array.c
 *
 * Author: Jeff Olivier <jeffrey.v.olivier@intel.com>
 */
#define D_LOGFAC DD_FAC(vos)
#include "lru_array.h"

static void
evict_cb(struct lru_array *array, struct lru_sub *sub, struct lru_entry *entry,
	 uint32_t idx)
{
	uint32_t	real_idx;

	if (array->la_cbs.lru_on_evict == NULL) {
		/** By default, reset the entry */
		memset(entry->le_payload, 0, array->la_record_size);
		return;
	}

	real_idx = (sub->ls_array_idx << array->la_array_shift) + idx;

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

	real_idx = (sub->ls_array_idx << array->la_array_shift) + idx;

	array->la_cbs.lru_on_init(entry->le_payload, real_idx, array->la_arg);
}

static void
fini_cb(struct lru_array *array, struct lru_sub *sub, struct lru_entry *entry,
	uint32_t idx)
{
	uint32_t	real_idx;

	if (array->la_cbs.lru_on_fini == NULL)
		return;

	real_idx = (sub->ls_array_idx << array->la_array_shift) + idx;

	array->la_cbs.lru_on_fini(entry->le_payload, real_idx, array->la_arg);
}

static int
array_alloc_one(struct lru_array *array, struct lru_sub *sub)
{
	struct lru_entry	*entry;
	char			*payload;
	size_t			 rec_size;
	uint32_t		 nr_ents = array->la_idx_mask + 1;
	uint32_t		 prev_idx = nr_ents - 1;
	uint32_t		 idx;

	rec_size = sizeof(*entry) + array->la_record_size;
	D_ALLOC(sub->ls_table, rec_size * nr_ents);
	if (sub->ls_table == NULL)
		return -DER_NOMEM;

	/** Add newly allocated ones to head of list */
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
		payload += array->la_record_size;
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

	*idx = (sub->ls_array_idx << array->la_array_shift) + tree_idx;

	return true;
}

static inline void
manual_find_free(struct lru_array *array, struct lru_entry **entryp,
		 uint32_t *idx, uint64_t key)
{
	struct lru_sub	*sub = NULL;
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
			return;
		}
	}

	/** No free entries */
	if (d_list_empty(&array->la_unused_sub))
		return; /* No free sub arrays either */

	sub = d_list_entry(&array->la_unused_sub, struct lru_sub, ls_link);
	rc = array_alloc_one(array, sub);
	if (rc != 0)
		return;

	D_ASSERT(sub_find_free(array, sub, entryp, idx, key));
}

void
lrua_find_free(struct lru_array *array, struct lru_entry **entryp,
	       uint32_t *idx, uint64_t key)
{
	struct lru_sub		*sub;
	struct lru_entry	*entry;

	*entryp = NULL;

	if (array->la_flags & LRU_FLAG_EVICT_MANUAL) {
		manual_find_free(array, entryp, idx, key);
		return;
	}

	sub = &array->la_sub[0];
	if (sub_find_free(array, sub, entryp, idx, key))
		return;

	entry = &sub->ls_table[sub->ls_lru];
	/** Key should not be 0, otherwise, it should be in free list */
	D_ASSERT(entry->le_key != 0);

	evict_cb(array, sub, entry, sub->ls_lru);

	*idx = (sub->ls_array_idx << array->la_array_shift) + sub->ls_lru;
	entry->le_key = key;
	sub->ls_lru = entry->le_next_idx;

	*entryp = entry;
}

void
lrua_evictx(struct lru_array *array, uint32_t idx, uint64_t key)
{
	struct lru_entry	*entry;
	struct lru_sub		*sub;
	uint32_t		 sub_idx;
	uint32_t		 ent_idx;

	D_ASSERT(array != NULL);
	D_ASSERT(key != 0);

	if (idx >= array->la_count)
		return;

	sub_idx = (idx & array->la_array_mask) >> array->la_array_shift;
	ent_idx = idx & array->la_idx_mask;

	sub = &array->la_sub[sub_idx];

	entry = &sub->ls_table[ent_idx];
	if (key != entry->le_key)
		return;

	evict_cb(array, sub, entry, ent_idx);

	entry->le_key = 0;

	/** Remove from active list */
	lrua_remove_entry(sub, &sub->ls_lru, entry, ent_idx);

	if (sub->ls_free == LRU_NO_IDX &&
	    array->la_flags & LRU_FLAG_EVICT_MANUAL) {
		/** Add the entry back to the free list */
		d_list_add_tail(&array->la_sub[idx].ls_link,
				&array->la_free_sub);
	}

	/** Insert in free list */
	lrua_insert(sub, &sub->ls_free, entry, ent_idx,
		    (array->la_flags & LRU_FLAG_REUSE_UNIQUE) != 0);
}

int
lrua_array_alloc(struct lru_array **arrayp, uint32_t nr_ent, uint32_t nr_arrays,
		 uint16_t record_size, uint32_t flags,
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

	aligned_size = (record_size + 7) & ~7;

	*arrayp = NULL;

	D_ALLOC(array, sizeof(*array) +
		(sizeof(array->la_sub[0]) * nr_arrays));
	if (array == NULL)
		return -DER_NOMEM;

	array->la_count = nr_ent;
	array->la_idx_mask = (nr_ent / nr_arrays) - 1;
	array->la_array_mask = (nr_ent - 1) & ~array->la_idx_mask;
	array->la_array_shift = 1;
	while ((1 << array->la_array_shift) < array->la_idx_mask)
		array->la_array_shift++;
	array->la_record_size = aligned_size;
	array->la_flags = flags;
	array->la_arg = arg;
	if (cbs != NULL)
		array->la_cbs = *cbs;

	/** Only allocate one sub array, add the rest to free list */
	D_INIT_LIST_HEAD(&array->la_free_sub);
	D_INIT_LIST_HEAD(&array->la_unused_sub);
	array->la_sub[0].ls_array_idx = 0;
	for (idx = 1; idx < nr_arrays; idx++) {
		array->la_sub[idx].ls_array_idx = idx;
		d_list_add_tail(&array->la_sub[idx].ls_link,
				&array->la_unused_sub);
	}

	rc = array_alloc_one(array, &array->la_sub[0]);
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

	if (array == NULL)
		return;

	while ((sub = d_list_pop_entry(&array->la_sub[0].ls_link,
				       struct lru_sub, ls_link)) != NULL)
		array_free_one(array, sub);

	D_FREE(array);
}
