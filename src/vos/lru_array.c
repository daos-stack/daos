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
#include "lru_array.h"

static void
evict_cb(struct lru_array *array, struct lru_entry *entry, uint32_t idx)
{
	if (array->la_cbs.lru_on_evict == NULL)
		return;

	array->la_cbs.lru_on_evict(entry->le_payload, idx, array->la_arg);
}

static void
init_cb(struct lru_array *array, struct lru_entry *entry, uint32_t idx)
{
	if (array->la_cbs.lru_on_init == NULL)
		return;

	array->la_cbs.lru_on_init(entry->le_payload, idx, array->la_arg);
}

static void
fini_cb(struct lru_array *array, struct lru_entry *entry, uint32_t idx)
{
	if (array->la_cbs.lru_on_fini == NULL)
		return;

	array->la_cbs.lru_on_fini(entry->le_payload, idx, array->la_arg);
}

void
lrua_evict_lru(struct lru_array *array, struct lru_entry **entryp,
	       uint32_t *idx, bool evict_lru)
{
	struct lru_entry	*entry;

	*entryp = NULL;

	entry = &array->la_table[array->la_lru];

	if (entry->le_record_idx != NULL) {
		if (!evict_lru)
			return; /* Caller has not set eviction flag */

		evict_cb(array, entry, array->la_lru);
	}

	*idx = array->la_lru;
	entry->le_record_idx = idx;
	array->la_lru = entry->le_next_idx;
	array->la_mru = *idx;

	*entryp = entry;
}

void
lrua_evict(struct lru_array *array, uint32_t *idx)
{
	struct lru_entry	*entry;
	uint32_t		 tidx;

	D_ASSERT(array != NULL);
	D_ASSERT(idx != NULL && *idx < array->la_count);
	tidx = *idx;

	entry = &array->la_table[tidx];
	if (idx != entry->le_record_idx)
		return;

	evict_cb(array, entry, tidx);

	entry->le_record_idx = NULL;

	if (array->la_mru == tidx)
		array->la_mru = entry->le_prev_idx;

	if (array->la_lru == tidx)
		return;

	/** Remove from current location */
	lrua_remove_entry(&array->la_table[0], entry);

	/** Add at the LRU */
	lrua_insert_entry(&array->la_table[0], entry, tidx, array->la_mru,
			  array->la_lru);

	array->la_lru = tidx;
}

int
lrua_array_alloc(struct lru_array **arrayp, uint32_t nr_ent,
		 uint32_t record_size, const struct lru_callbacks *cbs,
		 void *arg)
{
	struct lru_array	*array;
	struct lru_entry	*current;
	uint32_t		 aligned_size;
	uint32_t		 cur_idx;
	uint32_t		 next_idx;
	uint32_t		 prev_idx;

	aligned_size = (record_size + 7) & ~7;

	*arrayp = NULL;

	D_ALLOC(array, sizeof(*array) +
		(sizeof(array->la_table[0]) + aligned_size) * nr_ent);
	if (array == NULL)
		return -DER_NOMEM;

	prev_idx = array->la_mru = nr_ent - 1;
	array->la_arg = arg;
	array->la_lru = 0;
	array->la_count = nr_ent;
	array->la_record_size = aligned_size;
	array->la_payload = &array->la_table[nr_ent];
	if (cbs != NULL)
		array->la_cbs = *cbs;
	cur_idx = 0;
	for (cur_idx = 0; cur_idx < nr_ent; cur_idx++) {
		next_idx = (cur_idx + 1) % nr_ent;
		current = &array->la_table[cur_idx];
		current->le_payload = array->la_payload +
			(aligned_size * cur_idx);
		current->le_next_idx = next_idx;
		current->le_prev_idx = prev_idx;
		prev_idx = cur_idx;
		init_cb(array, current, cur_idx);
	}

	*arrayp = array;

	return 0;
}

void
lrua_array_free(struct lru_array *array)
{
	int	i;

	if (array == NULL)
		return;

	for (i = 0; i < array->la_count; i++)
		fini_cb(array, &array->la_table[i], i);

	D_FREE(array);
}
