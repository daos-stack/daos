/**
 * (C) Copyright 2020-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

#define OBJ_MISS_SIZE (1 << 16)
#define DKEY_MISS_SIZE (1 << 16)
#define AKEY_MISS_SIZE (1 << 16)

#define TS_TRACE(action, entry, idx, type)				\
	D_DEBUG(DB_TRACE, "%s %s at idx %d(%p), read.hi="DF_U64		\
		" read.lo="DF_U64"\n", action, type_strs[type], idx,	\
		(entry)->te_record_ptr, (entry)->te_ts.tp_ts_rh,	\
		(entry)->te_ts.tp_ts_rl)

/** The entry is being evicted either because there is no space in the cache or
 *  the item it represents has been removed.  In either case, update the
 *  corresponding negative entry.
 */
static bool
ts_update_on_evict(struct vos_ts_table *ts_table, struct vos_ts_entry *entry)
{
	struct vos_wts_cache	*wcache;
	struct vos_wts_cache	*dest;

	if (entry->te_record_ptr == NULL)
		return false;

	wcache = &entry->te_w_cache;

	if (entry->te_negative == NULL) {
		/* No negative entry.  This is likely the container level, so
		 * just update the global entries
		 */
		dest = &ts_table->tt_w_cache;
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
		goto update_w_cache;
	}

	dest = &entry->te_negative->te_w_cache;
	vos_ts_rl_update(entry->te_negative, entry->te_ts.tp_ts_rl,
			 &entry->te_ts.tp_tx_rl);
	vos_ts_rh_update(entry->te_negative, entry->te_ts.tp_ts_rh,
			 &entry->te_ts.tp_tx_rh);
update_w_cache:
	vos_ts_update_wcache(dest, wcache->wc_ts_w[0]);
	vos_ts_update_wcache(dest, wcache->wc_ts_w[1]);

	return true;
}

static void evict_entry(void *payload, uint32_t idx, void *arg)
{
	struct vos_ts_info	*info = arg;
	struct vos_ts_entry	*entry = payload;

	if (ts_update_on_evict(info->ti_table, entry)) {
		TS_TRACE("Evicted", entry, idx, info->ti_type);
		entry->te_record_ptr = NULL;
	}
}

static void init_entry(void *payload, uint32_t idx, void *arg)
{
	struct vos_ts_info	*info = arg;
	struct vos_ts_entry	*entry = payload;

	entry->te_info = info;
}

static const struct lru_callbacks lru_cbs = {
	.lru_on_evict = evict_entry,
	.lru_on_init = init_entry,
};

int
vos_ts_table_alloc(struct vos_ts_table **ts_tablep)
{
	struct vos_ts_entry	*entry;
	struct vos_ts_table	*ts_table;
	struct vos_ts_info	*info;
	struct vos_ts_entry	*miss_cursor;
	int			 rc;
	uint32_t		 i;
	int			 j;
	uint32_t		 miss_size;

	*ts_tablep = NULL;

	D_ALLOC_PTR(ts_table);
	if (ts_table == NULL)
		return -DER_NOMEM;

	D_ALLOC_ARRAY(ts_table->tt_misses,
		      OBJ_MISS_SIZE + DKEY_MISS_SIZE + AKEY_MISS_SIZE);
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
		case VOS_TS_TYPE_OBJ:
			miss_size = OBJ_MISS_SIZE;
			break;
		case VOS_TS_TYPE_DKEY:
			miss_size = DKEY_MISS_SIZE;
			break;
		case VOS_TS_TYPE_AKEY:
			miss_size = AKEY_MISS_SIZE;
			break;
		case VOS_TS_TYPE_CONT:
		default:
			miss_size = 0;
			break;
		}
		if (miss_size) {
			info->ti_cache_mask = miss_size - 1;
			info->ti_misses = miss_cursor;
			miss_cursor += miss_size;
			/* Negative entries are global.  Each object/key chain
			 * will hash to an index.   There will be some false
			 * sharing but it should be fairly minimal.  Start each
			 * negative entry with global settings.
			 */
			for (j = 0; j < miss_size; j++) {
				entry = &info->ti_misses[j];
				entry->te_info = info;
				vos_ts_copy(&entry->te_ts.tp_ts_rl,
					    &entry->te_ts.tp_tx_rl,
					    ts_table->tt_ts_rl,
					    &ts_table->tt_tx_rl);
				vos_ts_copy(&entry->te_ts.tp_ts_rh,
					    &entry->te_ts.tp_tx_rh,
					    ts_table->tt_ts_rh,
					    &ts_table->tt_tx_rh);
			}
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
vos_ts_evict_lru(struct vos_ts_table *ts_table, struct vos_ts_entry **entryp,
		 uint32_t *idx, uint32_t hash_idx, uint32_t type)
{
	struct vos_ts_entry	*entry;
	struct vos_ts_entry	*neg_entry = NULL;
	struct vos_ts_info	*info = &ts_table->tt_type_info[type];
	int			 rc;

	rc = lrua_alloc(ts_table->tt_type_info[type].ti_array, idx, &entry);
	D_ASSERT(rc == 0); /** autoeviction and no allocation */

	if (info->ti_cache_mask)
		neg_entry = &info->ti_misses[hash_idx];

	entry->te_negative = neg_entry;

	if (neg_entry == NULL) {
		/** Use global timestamps for the type to initialize it */
		vos_ts_copy(&entry->te_ts.tp_ts_rl, &entry->te_ts.tp_tx_rl,
			    ts_table->tt_ts_rl, &ts_table->tt_tx_rl);
		vos_ts_copy(&entry->te_ts.tp_ts_rh, &entry->te_ts.tp_tx_rh,
			    ts_table->tt_ts_rh, &ts_table->tt_tx_rh);
		entry->te_w_cache = ts_table->tt_w_cache;
	} else {
		vos_ts_copy(&entry->te_ts.tp_ts_rh,
			    &entry->te_ts.tp_tx_rh,
			    neg_entry->te_ts.tp_ts_rh,
			    &neg_entry->te_ts.tp_tx_rh);
		vos_ts_copy(&entry->te_ts.tp_ts_rl,
			    &entry->te_ts.tp_tx_rl,
			    neg_entry->te_ts.tp_ts_rl,
			    &neg_entry->te_ts.tp_tx_rl);
		entry->te_w_cache = neg_entry->te_w_cache;
	}

	/** Set the lower bounds for the entry */
	entry->te_record_ptr = idx;
	TS_TRACE("Allocated", entry, *idx, type);

	D_ASSERT(type == info->ti_type);

	*entryp = entry;
}

int
vos_ts_set_allocate(struct vos_ts_set **ts_set, uint64_t flags,
		    uint16_t cflags, uint32_t akey_nr,
		    const struct dtx_handle *dth, bool standalone)
{
	const struct dtx_id	*tx_id = NULL;
	uint32_t		 size;
	uint64_t		 array_size;
	uint64_t		 cond_mask = VOS_COND_FETCH_MASK |
					     VOS_COND_UPDATE_MASK |
					     VOS_OF_COND_PER_AKEY;

	vos_kh_clear(standalone);

	*ts_set = NULL;
	if (!dtx_is_valid_handle(dth)) {
		if ((flags & cond_mask) == 0)
			return 0;
	} else {
		tx_id = &dth->dth_xid;
	}

	size = VOS_TS_TYPE_AKEY + akey_nr;
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
	struct vos_ts_table	*ts_table;
	struct vos_ts_info	*info;
	uint32_t		 hash_idx;
	int			 i;

	if (!vos_ts_in_tx(ts_set))
		return;

	ts_table = vos_ts_table_get(false);

	for (i = 0; i < ts_set->ts_init_count; i++) {
		set_entry = &ts_set->ts_entries[i];
		entry = set_entry->se_entry;
		info = entry->te_info;

		D_ASSERT(entry != NULL);
		if (entry->te_negative != NULL || info->ti_misses == NULL)
			continue;

		D_ASSERT(i != 0); /** no negative lookup on container */
		D_ASSERT(set_entry->se_create_idx != NULL);

		hash_idx = entry - info->ti_misses;
		vos_ts_evict_lru(ts_table, &entry, set_entry->se_create_idx,
				 hash_idx, info->ti_type);
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
	bool			 conflict;

	D_ASSERT(ts_set != NULL);

	se = &ts_set->ts_entries[idx];
	entry = se->se_entry;

	if (ts_set->ts_wr_level > ts_set->ts_max_type)
		write_level = ts_set->ts_max_type;
	else
		write_level = ts_set->ts_wr_level;

	if (se->se_etype > write_level)
		return false; /** Check is redundant */

	/** NB: If there is a negative entry, we should also check it.  Otherwise, we can miss
	 *  timestamp updates associated with conditional operations where the tree exists but
	 *  we don't load it
	 */
	if (se->se_etype < write_level) {
		/* check the low time */
		conflict = vos_ts_check_conflict(entry->te_ts.tp_ts_rl, &entry->te_ts.tp_tx_rl,
						 write_time, &ts_set->ts_tx_id);

		if (conflict || entry->te_negative == NULL)
			return conflict;

		return vos_ts_check_conflict(entry->te_negative->te_ts.tp_ts_rl,
					     &entry->te_negative->te_ts.tp_tx_rl,
					     write_time, &ts_set->ts_tx_id);
	}

	/* check the high time */
	conflict = vos_ts_check_conflict(entry->te_ts.tp_ts_rh, &entry->te_ts.tp_tx_rh, write_time,
					 &ts_set->ts_tx_id);

	if (conflict || entry->te_negative == NULL)
		return conflict;

	return vos_ts_check_conflict(entry->te_negative->te_ts.tp_ts_rh,
				     &entry->te_negative->te_ts.tp_tx_rh, write_time,
				     &ts_set->ts_tx_id);
}
