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
 * Record timestamp table
 * vos/vos_ts.c
 *
 * Author: Jeff Olivier <jeffrey.v.olivier@intel.com>
 */
#define D_LOGFAC DD_FAC(vos)

#include "vos_internal.h"

#define DEFINE_TS_STR(type, desc, count, child_count)	desc, desc "_nochild",

/** Strings corresponding to timestamp types */
static const char * const type_strs[] = {
	D_FOREACH_TS_TYPE(DEFINE_TS_STR)
};

#define DEFINE_TS_COUNT(type, desc, count, child_count)	count, child_count,
static const uint32_t type_counts[] = {
	D_FOREACH_TS_TYPE(DEFINE_TS_COUNT)
};

#define OBJ_MISS_SIZE (1 << 9)
#define DKEY_MISS_SIZE (1 << 5)
#define AKEY_MISS_SIZE (1 << 4)

int
vos_ts_table_alloc(struct vos_ts_table **ts_tablep)
{
	struct vos_ts_table	*ts_table;
	struct vos_ts_info	*info;
	struct vos_ts_entry	*current;
	uint32_t		 sofar = 0;
	uint32_t		 cur_idx;
	uint32_t		 next_idx;
	uint32_t		 prev_idx;
	uint32_t		 i, count, offset;
	uint32_t		 miss_size;
	uint32_t		*misses;
	uint32_t		*miss_cursor;

	*ts_tablep = NULL;

	D_ALLOC_PTR(ts_table);
	if (ts_table == NULL)
		return -DER_NOMEM;

	D_ALLOC_ARRAY(misses, (type_counts[VOS_TS_TYPE_CONT] * OBJ_MISS_SIZE) +
			      (type_counts[VOS_TS_TYPE_OBJ] * DKEY_MISS_SIZE) +
			      (type_counts[VOS_TS_TYPE_DKEY] * AKEY_MISS_SIZE));
	if (misses == NULL) {
		D_FREE(ts_table);
		return -DER_NOMEM;
	}

	ts_table->tt_ts_rl = vos_start_epoch;
	ts_table->tt_ts_rh = vos_start_epoch;
	ts_table->tt_ts_w = vos_start_epoch;
	miss_cursor = misses;
	cur_idx = 0;
	for (i = 0; i < VOS_TS_TYPE_COUNT; i++) {
		info = &ts_table->tt_type_info[i];
		count = type_counts[i];

		if (count == 0) {
			D_ASSERT(i == VOS_TS_TYPE_AKEY);
			count = VOS_TS_SIZE - sofar;
			/** More akeys than missing akeys */
			D_ASSERT(count > type_counts[VOS_TS_TYPE_DKEY_CHILD]);
			/** Make sure it doesn't overflow */
			D_ASSERT(count < VOS_TS_SIZE);
		} else {
			sofar += count;
		}

		info->ti_count = count;
		info->ti_type = i;

		offset = cur_idx;
		switch (i) {
		case VOS_TS_TYPE_CONT:
			miss_size = OBJ_MISS_SIZE;
			break;
		case VOS_TS_TYPE_OBJ:
			miss_size = DKEY_MISS_SIZE;
			break;
		case VOS_TS_TYPE_DKEY:
			miss_size = AKEY_MISS_SIZE;
			break;
		case VOS_TS_TYPE_AKEY:
		default:
			miss_size = 0;
			break;
		}

		if (miss_size != 0)
			info->ti_cache_mask = miss_size - 1;
		info->ti_lru = cur_idx;
		prev_idx = info->ti_mru = offset + count - 1;
		while (cur_idx < (offset + count)) {
			next_idx = offset + ((cur_idx + 1 - offset) % count);
			current = &ts_table->tt_table[cur_idx];
			current->te_info = info;
			current->te_next_idx = next_idx;
			current->te_prev_idx = prev_idx;
			prev_idx = cur_idx;
			cur_idx++;
			if (miss_size == 0)
				continue;
			current->te_miss_idx = miss_cursor;
			miss_cursor += miss_size;
		}
	}

	*ts_tablep = ts_table;

	return 0;
}

void
vos_ts_table_free(struct vos_ts_table **ts_tablep)
{
	struct vos_ts_table	*ts_table = *ts_tablep;

	/** entry 0 points to start of allocated space */
	D_FREE(ts_table->tt_table[0].te_miss_idx);
	D_FREE(ts_table);

	*ts_tablep = NULL;
}

/** This probably needs more thought */
static bool
ts_update_on_evict(struct vos_ts_table *ts_table, struct vos_ts_entry *entry)
{
	struct vos_ts_entry	*parent = NULL;
	struct vos_ts_entry	*other = NULL;
	struct vos_ts_info	*info = entry->te_info;
	uint32_t		*idx;

	if (entry->te_record_ptr == NULL)
		return false;

	if (entry->te_parent_ptr != NULL) {
		parent = vos_ts_lookup_idx(ts_table, entry->te_parent_ptr);
		if (info->ti_type & 1) { /* negative entry */
			other = parent;
		} else if (parent != NULL) {
			idx = &parent->te_miss_idx[entry->te_hash_idx];
			other = vos_ts_lookup_idx(ts_table, idx);
		}
	}

	if (other == NULL) {
		ts_table->tt_ts_rl = MAX(ts_table->tt_ts_rl, entry->te_ts_rl);
		ts_table->tt_ts_rh = MAX(ts_table->tt_ts_rh, entry->te_ts_rh);
		ts_table->tt_ts_w = MAX(ts_table->tt_ts_w, entry->te_ts_w);
		return true;
	}

	other->te_ts_rl = MAX(other->te_ts_rl, entry->te_ts_rl);
	other->te_ts_rh = MAX(other->te_ts_rh, entry->te_ts_rh);
	other->te_ts_w = MAX(other->te_ts_w, entry->te_ts_w);

	return true;
}

#define TS_TRACE(action, entry, idx, type)				\
	D_DEBUG(DB_TRACE, "%s %s at idx %d(%p), read.hi="DF_U64		\
		" read.lo="DF_U64" write="DF_U64"\n", action,		\
		type_strs[type], idx, (entry)->te_record_ptr,		\
		(entry)->te_ts_rh, (entry)->te_ts_rl, (entry)->te_ts_w)

static inline void
evict_one(struct vos_ts_table *ts_table, struct vos_ts_entry *entry,
	  uint32_t idx, struct vos_ts_info *info, bool removed)
{
	if (ts_update_on_evict(ts_table, entry)) {
		TS_TRACE("Evicted", entry, idx, info->ti_type);
		entry->te_record_ptr = NULL;
	}

	if (removed)
		return;

	if (info->ti_mru == idx)
		info->ti_mru = entry->te_prev_idx;

	if (info->ti_lru == idx)
		return;

	/** Remove the entry from it's current location */
	remove_ts_entry(&ts_table->tt_table[0], entry);

	/** insert the entry at the LRU */
	insert_ts_entry(&ts_table->tt_table[0], entry, idx, info->ti_mru,
			info->ti_lru);

	info->ti_lru = idx;
}

static inline void
evict_children(struct vos_ts_table *ts_table, struct vos_ts_info *info,
	       struct vos_ts_entry *entry)
{
	struct vos_ts_entry	*child;
	int			 i;
	uint32_t		 idx;
	uint32_t		 cache_num;

	info = entry->te_info;

	if ((info->ti_type == VOS_TS_TYPE_AKEY) || (info->ti_type & 1) != 0)
		return;

	cache_num = info->ti_cache_mask + 1;
	info++;
	for (i = 0; i < cache_num; i++) {
		/* Also evict the children, if present */
		idx = entry->te_miss_idx[i] & VOS_TS_MASK;
		child = &ts_table->tt_table[idx];
		if (child->te_record_ptr != &entry->te_miss_idx[i])
			continue;

		evict_one(ts_table, child, idx, info, false);
	}
}

void
vos_ts_evict_lru(struct vos_ts_table *ts_table, struct vos_ts_entry *parent,
		 struct vos_ts_entry **entryp, uint32_t *idx, uint32_t hash_idx,
		 uint32_t type)
{
	struct vos_ts_entry	*ts_source = NULL;
	struct vos_ts_entry	*entry;
	struct vos_ts_info	*info = &ts_table->tt_type_info[type];
	uint32_t		*neg_idx;

	/** Ok, grab and evict the LRU */
	*idx = info->ti_lru;
	entry = &ts_table->tt_table[*idx];
	info->ti_lru = entry->te_next_idx;
	info->ti_mru = *idx;

	if (entry->te_record_ptr != NULL) {
		evict_children(ts_table, info, entry);
		evict_one(ts_table, entry, *idx, info, true);
	}

	if (parent == NULL) {
		/** Use global timestamps for the type to initialize it */
		entry->te_ts_rl = ts_table->tt_ts_rl;
		entry->te_ts_rh = ts_table->tt_ts_rh;
		entry->te_ts_w = ts_table->tt_ts_w;
		entry->te_parent_ptr = NULL;
	} else {
		entry->te_parent_ptr = parent->te_record_ptr;
		if ((type & 1) == 0) { /* positive entry */
			neg_idx = &parent->te_miss_idx[hash_idx];
			ts_source = vos_ts_lookup_idx(ts_table, neg_idx);
		}
		if (ts_source == NULL) /* for negative and uncached entries */
			ts_source = parent;

		entry->te_ts_rl = ts_source->te_ts_rl;
		entry->te_ts_rh = ts_source->te_ts_rh;
		entry->te_ts_w = ts_source->te_ts_w;
	}

	/** Set the lower bounds for the entry */
	entry->te_hash_idx = hash_idx;
	entry->te_record_ptr = idx;
	uuid_clear(entry->te_tx_rl);
	uuid_clear(entry->te_tx_rh);
	uuid_clear(entry->te_tx_w);
	TS_TRACE("Allocated", entry, *idx, type);

	D_ASSERT(type == info->ti_type);

	*entryp = entry;
}

void
vos_ts_evict_entry(struct vos_ts_table *ts_table, struct vos_ts_entry *entry,
		   uint32_t idx)
{
	struct vos_ts_info	*info = entry->te_info;

	evict_children(ts_table, info, entry);

	evict_one(ts_table, entry, idx, info, false);
}

int
vos_ts_set_allocate(struct vos_ts_set **ts_set, uint64_t flags,
		    uint32_t akey_nr)
{
	uint32_t	size;
	uint64_t	array_size;

	*ts_set = NULL;
	if ((flags & VOS_OF_USE_TIMESTAMPS) == 0)
		return 0;

	size = 3 + akey_nr;
	array_size = size * sizeof((*ts_set)->ts_entries[0]);

	D_ALLOC(*ts_set, sizeof(**ts_set) + array_size);
	if (*ts_set == NULL)
		return -DER_NOMEM;

	(*ts_set)->ts_flags = flags;
	(*ts_set)->ts_set_size = size;

	return 0;
}

void
vos_ts_set_upgrade(struct vos_ts_set *ts_set)
{
	struct vos_ts_set_entry	*set_entry;
	struct vos_ts_entry	*entry;
	struct vos_ts_entry	*parent;
	struct vos_ts_table	*ts_table;
	struct vos_ts_info	*info;
	uint32_t		 hash_idx;
	int			 i;
	int			 parent_idx;

	if (ts_set == NULL)
		return;

	ts_table = vos_ts_table_get();

	for (i = 0; i < ts_set->ts_init_count; i++) {
		set_entry = &ts_set->ts_entries[i];
		entry = set_entry->se_entry;
		D_ASSERT(entry != NULL);
		if ((entry->te_info->ti_type & 1) == 0)
			continue;

		D_ASSERT(i != 0); /** no negative lookup on container */
		D_ASSERT(set_entry->se_create_idx != NULL);

		parent_idx = MIN(2, i - 1);
		parent = ts_set->ts_entries[parent_idx].se_entry;
		info = parent->te_info;
		hash_idx = set_entry->se_hash & info->ti_cache_mask;
		vos_ts_evict_lru(ts_table, parent, &entry,
				 set_entry->se_create_idx, hash_idx,
				 info->ti_type + 2);
		set_entry->se_entry = entry;
	}
}
