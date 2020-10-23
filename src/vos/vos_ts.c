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

#define DEFINE_TS_STR(type, desc, count)	desc,

/** Strings corresponding to timestamp types */
static const char * const type_strs[] = {
	D_FOREACH_TS_TYPE(DEFINE_TS_STR)
};

#define DEFINE_TS_COUNT(type, desc, count)	count,
static const uint32_t type_counts[] = {
	D_FOREACH_TS_TYPE(DEFINE_TS_COUNT)
};

#define OBJ_MISS_SIZE (1 << 9)
#define DKEY_MISS_SIZE (1 << 5)
#define AKEY_MISS_SIZE (1 << 4)

#define TS_TRACE(action, entry, idx, type)				\
	D_DEBUG(DB_TRACE, "%s %s at idx %d(%p), read.hi="DF_U64		\
		" read.lo="DF_U64"\n", action, type_strs[type], idx,	\
		(entry)->te_record_ptr, (entry)->te_ts.tp_ts_rh,	\
		(entry)->te_ts.tp_ts_rl)

/** This probably needs more thought */
static bool
ts_update_on_evict(struct vos_ts_table *ts_table, struct vos_ts_entry *entry)
{
	struct vos_ts_entry	*parent = NULL;
	struct vos_ts_entry	*other = NULL;
	struct vos_ts_info	*info = entry->te_info;
	struct vos_ts_info	*parent_info;
	struct vos_ts_info	*neg_info = NULL;
	uint32_t		*idx;

	if (entry->te_record_ptr == NULL)
		return false;

	if (entry->te_parent_ptr != NULL) {
		if (info->ti_type & 1) { /* negative entry */
			parent_info = info - 1;
		} else {
			parent_info = info - VOS_TS_PER_LEVEL;
			neg_info = info - 1;
		}
		lrua_lookup(parent_info->ti_array, entry->te_parent_ptr,
			    &parent);
		if (neg_info == NULL) {
			other = parent;
		} else if (parent != NULL) {
			idx = &parent->te_miss_idx[entry->te_hash_idx];
			lrua_lookup(neg_info->ti_array, idx, &other);
		}
	}

	if (other == NULL) {
		if (entry->te_ts.tp_ts_rl > ts_table->tt_ts_rl) {
			vos_ts_copy(&ts_table->tt_ts_rl, &ts_table->tt_tx_rl,
				    entry->te_ts.tp_ts_rl,
				    &entry->te_ts.tp_tx_rl);
		}
		if (entry->te_ts.tp_ts_rh > ts_table->tt_ts_rh) {
			vos_ts_copy(&ts_table->tt_ts_rh, &ts_table->tt_tx_rh,
				    entry->te_ts.tp_ts_rh,
				    &entry->te_ts.tp_tx_rh);
		}
		return true;
	}

	vos_ts_rl_update(other, entry->te_ts.tp_ts_rl, &entry->te_ts.tp_tx_rl);
	vos_ts_rh_update(other, entry->te_ts.tp_ts_rh, &entry->te_ts.tp_tx_rh);

	return true;
}

static inline void
evict_children(struct vos_ts_info *info, struct vos_ts_entry *entry)
{
	int			 i;
	uint32_t		*idx;
	uint32_t		 cache_num;

	info = entry->te_info;

	if ((info->ti_type == VOS_TS_TYPE_AKEY) || (info->ti_type & 1) != 0)
		return;

	cache_num = info->ti_cache_mask + 1;
	info++;
	for (i = 0; i < cache_num; i++) {
		/* Also evict the children, if present */
		idx = &entry->te_miss_idx[i];
		lrua_evict(info->ti_array, idx);
	}
}


static void evict_entry(void *payload, uint32_t idx, void *arg)
{
	struct vos_ts_info	*info = arg;
	struct vos_ts_entry	*entry = payload;

	evict_children(info, entry);

	if (ts_update_on_evict(info->ti_table, entry)) {
		TS_TRACE("Evicted", entry, idx, info->ti_type);
		entry->te_record_ptr = NULL;
	}
}

static void init_entry(void *payload, uint32_t idx, void *arg)
{
	struct vos_ts_info	*info = arg;
	struct vos_ts_entry	*entry = payload;
	uint32_t		 count;

	entry->te_info = info;
	if (info->ti_misses) {
		D_ASSERT((info->ti_type & 1) == 0 &&
			 info->ti_type != VOS_TS_TYPE_AKEY &&
			 info->ti_cache_mask != 0);
		count = info->ti_cache_mask + 1;
		entry->te_miss_idx = &info->ti_misses[idx * count];
	}
}

static const struct lru_callbacks lru_cbs = {
	.lru_on_evict = evict_entry,
	.lru_on_init = init_entry,
};

int
vos_ts_table_alloc(struct vos_ts_table **ts_tablep)
{
	struct vos_ts_table	*ts_table;
	struct vos_ts_info	*info;
	int			 rc;
	uint32_t		 i;
	uint32_t		 miss_size;
	uint32_t		*miss_cursor;

	*ts_tablep = NULL;

	D_ALLOC_PTR(ts_table);
	if (ts_table == NULL)
		return -DER_NOMEM;

	D_ALLOC_ARRAY(ts_table->tt_misses,
		      (type_counts[VOS_TS_TYPE_CONT] * OBJ_MISS_SIZE) +
		      (type_counts[VOS_TS_TYPE_OBJ] * DKEY_MISS_SIZE) +
		      (type_counts[VOS_TS_TYPE_DKEY] * AKEY_MISS_SIZE));
	if (ts_table->tt_misses == NULL) {
		rc = -DER_NOMEM;
		goto free_table;
	}

	ts_table->tt_ts_rl = vos_start_epoch;
	ts_table->tt_ts_rh = vos_start_epoch;
	uuid_clear(ts_table->tt_tx_rl.dti_uuid);
	uuid_clear(ts_table->tt_tx_rh.dti_uuid);
	miss_cursor = ts_table->tt_misses;
	for (i = 0; i < VOS_TS_TYPE_COUNT; i++) {
		info = &ts_table->tt_type_info[i];

		info->ti_type = i;
		info->ti_count = type_counts[i];
		info->ti_table = ts_table;
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
		if (miss_size) {
			info->ti_cache_mask = miss_size - 1;
			info->ti_misses = miss_cursor;
			miss_cursor += info->ti_count * miss_size;
		}

		rc = lrua_array_alloc(&info->ti_array, info->ti_count, 1,
				      sizeof(struct vos_ts_entry), 0, &lru_cbs,
				      info);
		if (rc != 0)
			goto cleanup;
	}

	*ts_tablep = ts_table;

	return 0;

cleanup:
	for (i = 0; i < VOS_TS_TYPE_COUNT; i++)
		lrua_array_free(ts_table->tt_type_info[i].ti_array);
	D_FREE(ts_table->tt_misses);
free_table:
	D_FREE(ts_table);

	return rc;
}

void
vos_ts_table_free(struct vos_ts_table **ts_tablep)
{
	struct vos_ts_table	*ts_table = *ts_tablep;
	int			 i;

	for (i = 0; i < VOS_TS_TYPE_COUNT; i++)
		lrua_array_free(ts_table->tt_type_info[i].ti_array);

	D_FREE(ts_table->tt_misses);
	D_FREE(ts_table);

	*ts_tablep = NULL;
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
	int			 rc;

	rc = lrua_alloc(ts_table->tt_type_info[type].ti_array, idx, &entry);
	D_ASSERT(rc == 0); /** autoeviction and no allocation */

	if (parent == NULL) {
		/** Use global timestamps for the type to initialize it */
		vos_ts_copy(&entry->te_ts.tp_ts_rl, &entry->te_ts.tp_tx_rl,
			    ts_table->tt_ts_rl, &ts_table->tt_tx_rl);
		vos_ts_copy(&entry->te_ts.tp_ts_rh, &entry->te_ts.tp_tx_rh,
			    ts_table->tt_ts_rh, &ts_table->tt_tx_rh);
		entry->te_parent_ptr = NULL;
	} else {
		entry->te_parent_ptr = parent->te_record_ptr;
		if ((type & 1) == 0) { /* positive entry */
			neg_idx = &parent->te_miss_idx[hash_idx];
			lrua_lookup(parent->te_info->ti_array, neg_idx,
				    &ts_source);
		}
		if (ts_source == NULL)
			ts_source = parent;

		if ((type & 1) != 0) {
			/* This is a new negative entry. The low timestamp of
			 * the parent should cover this case so copy it to
			 * both timestamps.
			 */
			vos_ts_copy(&entry->te_ts.tp_ts_rh,
				    &entry->te_ts.tp_tx_rh,
				    ts_source->te_ts.tp_ts_rl,
				    &ts_source->te_ts.tp_tx_rl);
		} else {
			/** It is either copying from a negative entry
			 * or its parent was evicted so copy both timestamps
			 */
			vos_ts_copy(&entry->te_ts.tp_ts_rh,
				    &entry->te_ts.tp_tx_rh,
				    ts_source->te_ts.tp_ts_rh,
				    &ts_source->te_ts.tp_tx_rh);
		}
		/** Low timestamp is always copied as is */
		vos_ts_copy(&entry->te_ts.tp_ts_rl, &entry->te_ts.tp_tx_rl,
			    ts_source->te_ts.tp_ts_rl,
			    &ts_source->te_ts.tp_tx_rl);
	}

	/** Set the lower bounds for the entry */
	entry->te_hash_idx = hash_idx;
	entry->te_record_ptr = idx;
	TS_TRACE("Allocated", entry, *idx, type);

	D_ASSERT(type == info->ti_type);

	*entryp = entry;
}

int
vos_ts_set_allocate(struct vos_ts_set **ts_set, uint64_t flags,
		    uint16_t cflags, uint32_t akey_nr,
		    const struct dtx_id *tx_id)
{
	uint32_t	size;
	uint64_t	array_size;
	uint64_t	cond_mask = VOS_COND_FETCH_MASK | VOS_COND_UPDATE_MASK |
		VOS_OF_COND_PER_AKEY;

	*ts_set = NULL;
	if (tx_id == NULL && (flags & cond_mask) == 0)
		return 0;

	size = VOS_TS_TYPE_AKEY / VOS_TS_PER_LEVEL + akey_nr;
	array_size = size * sizeof((*ts_set)->ts_entries[0]);

	D_ALLOC(*ts_set, sizeof(**ts_set) + array_size);
	if (*ts_set == NULL)
		return -DER_NOMEM;

	(*ts_set)->ts_flags = flags;
	(*ts_set)->ts_set_size = size;
	if (tx_id != NULL) {
		(*ts_set)->ts_in_tx = true;
		uuid_copy((*ts_set)->ts_tx_id.dti_uuid, tx_id->dti_uuid);
		(*ts_set)->ts_tx_id.dti_hlc = tx_id->dti_hlc;
	} /* ts_in_tx is false by default */
	vos_ts_set_append_cflags(*ts_set, cflags);

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

	if (!vos_ts_in_tx(ts_set))
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
				 info->ti_type + VOS_TS_PER_LEVEL);
		set_entry->se_entry = entry;
	}
}

static inline bool
vos_ts_check_conflict(daos_epoch_t read_time, const struct dtx_id *read_id,
		      daos_epoch_t write_time, const struct dtx_id *write_id)

{
	if (write_time > read_time)
		return false;

	if (write_time != read_time)
		return true;

	if (read_id->dti_hlc != write_id->dti_hlc)
		return true;

	return uuid_compare(read_id->dti_uuid, write_id->dti_uuid) != 0;
}

bool
vos_ts_check_read_conflict(struct vos_ts_set *ts_set, int idx,
			   daos_epoch_t write_time)
{
	struct vos_ts_set_entry	*se;
	struct vos_ts_entry	*entry;
	int			 write_level;

	D_ASSERT(ts_set != NULL);

	se = &ts_set->ts_entries[idx];
	entry = se->se_entry;

	if (ts_set->ts_wr_level > ts_set->ts_max_type)
		write_level = ts_set->ts_max_type;
	else
		write_level = ts_set->ts_wr_level;

	if (se->se_etype > write_level)
		return false; /** Check is redundant */

	if (se->se_etype < write_level) {
		/* check the low time */
		return vos_ts_check_conflict(entry->te_ts.tp_ts_rl,
					     &entry->te_ts.tp_tx_rl,
					     write_time, &ts_set->ts_tx_id);
	}

	/* check the high time */
	return vos_ts_check_conflict(entry->te_ts.tp_ts_rh,
				     &entry->te_ts.tp_tx_rh,
				     write_time, &ts_set->ts_tx_id);
}
