/**
 * (C) Copyright 2020-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Record timestamp table
 * vos/vos_ts.h
 *
 * Author: Jeff Olivier <jeffrey.v.olivier@intel.com>
 */

#ifndef __VOS_TS__
#define __VOS_TS__

#include <daos/dtx.h>
#include "lru_array.h"
#include "vos_tls.h"

struct vos_ts_table;
struct vos_ts_entry;

struct vos_ts_info {
	/** The LRU array */
	struct lru_array	*ti_array;
	/** Back pointer to table */
	struct vos_ts_table	*ti_table;
	/** Negative entries for this type */
	struct vos_ts_entry	*ti_misses;
	/** Type identifier */
	uint32_t		ti_type;
	/** Mask for negative entry cache */
	uint32_t		ti_cache_mask;
	/** Number of entries in cache for type (for testing) */
	uint32_t		ti_count;
};

struct vos_ts_pair {
	/** Low read time or read time for the object/key */
	daos_epoch_t	tp_ts_rl;
	/** High read time or read time for the object/key */
	daos_epoch_t	tp_ts_rh;
	/** Low read tx */
	struct dtx_id	tp_tx_rl;
	/** High read tx */
	struct dtx_id	tp_tx_rh;
};

struct vos_wts_cache {
	/** Highest two write timestamps. */
	daos_epoch_t	wc_ts_w[2];
	/** Index of highest timestamp in wc_ts_w */
	uint32_t	wc_w_high;
};

struct vos_ts_entry {
	struct vos_ts_info	*te_info;
	/** Key for current occupant */
	uint32_t		*te_record_ptr;
	/** Corresponding negative entry, if applicable */
	struct vos_ts_entry	*te_negative;
	/** The timestamps for the entry */
	struct vos_ts_pair	 te_ts;
	/** Write timestamps for epoch bound check */
	struct vos_wts_cache	 te_w_cache;
};

/** Check/update flags for a ts set entry */
enum {
	/** Mark operation as CONT read */
	VOS_TS_READ_CONT	= (1 << 0),
	/** Mark operation as OBJ read */
	VOS_TS_READ_OBJ		= (1 << 1),
	/** Mark operation as DKEY read */
	VOS_TS_READ_DKEY	= (1 << 2),
	/** Mark operation as AKEY read */
	VOS_TS_READ_AKEY	= (1 << 3),
	/** Read mask */
	VOS_TS_READ_MASK	= (VOS_TS_READ_CONT | VOS_TS_READ_OBJ |
				   VOS_TS_READ_DKEY | VOS_TS_READ_AKEY),
	/** Mark operation as OBJ write */
	VOS_TS_WRITE_OBJ	= (1 << 4),
	/** Mark operation as DKEY write */
	VOS_TS_WRITE_DKEY	= (1 << 5),
	/** Mark operation as AKEY write */
	VOS_TS_WRITE_AKEY	= (1 << 6),
	/** Write mask */
	VOS_TS_WRITE_MASK	= (VOS_TS_WRITE_DKEY | VOS_TS_WRITE_AKEY |
				   VOS_TS_WRITE_OBJ),
};

struct vos_ts_set_entry {
	/** Pointer to the entry at this level */
	struct vos_ts_entry	*se_entry;
	/** pointer to newly created index */
	uint32_t		*se_create_idx;
	/** The expected type of this entry. */
	uint32_t		 se_etype;
};

/** Structure looking up and caching operation flags */
struct vos_ts_set {
	/** Operation flags */
	uint64_t		 ts_flags;
	/** type of next entry */
	uint32_t		 ts_etype;
	/** true if inside a transaction */
	bool			 ts_in_tx;
	/** The Check/update flags for the set */
	uint16_t		 ts_cflags;
	/** Write level for the set */
	uint16_t		 ts_wr_level;
	/** Read level for the set */
	uint16_t		 ts_rd_level;
	/** Max type */
	uint16_t		 ts_max_type;
	/** Transaction that owns the set */
	struct dtx_id		 ts_tx_id;
	/** size of the set */
	uint32_t		 ts_set_size;
	/** Number of initialized entries */
	uint32_t		 ts_init_count;
	/** timestamp entries */
	struct vos_ts_set_entry	 ts_entries[0];
};

/** Timestamp types (should all be powers of 2) */
#define D_FOREACH_TS_TYPE(ACTION)					\
	ACTION(VOS_TS_TYPE_CONT,	"container",	1024)		\
	ACTION(VOS_TS_TYPE_OBJ,		"object",	32 * 1024)	\
	ACTION(VOS_TS_TYPE_DKEY,	"dkey",		128 * 1024)	\
	ACTION(VOS_TS_TYPE_AKEY,	"akey",		512 * 1024)

#define DEFINE_TS_TYPE(type, desc, count)	type,

enum {
	D_FOREACH_TS_TYPE(DEFINE_TS_TYPE)
	/** Number of timestamp types */
	VOS_TS_TYPE_COUNT,
};

struct vos_ts_table {
	/** Global read low timestamp for type */
	daos_epoch_t		tt_ts_rl;
	/** Global read high timestamp for type */
	daos_epoch_t		tt_ts_rh;
	/** Global write timestamps */
	struct vos_wts_cache	tt_w_cache;
	/** Transaction id associated with global read low timestamp */
	struct dtx_id		tt_tx_rl;
	/** Transaction id associated with global read high timestamp */
	struct dtx_id		tt_tx_rh;
	/** Negative entry cache */
	struct vos_ts_entry	*tt_misses;
	/** Timestamp table pointers for a type */
	struct vos_ts_info	tt_type_info[VOS_TS_TYPE_COUNT];
};

/** Internal API: Use the parent entry to get the type info and hash offset for
 *  the current object/key.
 */
static inline void
vos_ts_set_get_info(struct vos_ts_table *ts_table, struct vos_ts_set *ts_set,
		    struct vos_ts_info **info, uint64_t *hash_offset)
{
	struct vos_ts_set_entry	*set_entry;
	struct vos_ts_entry	*parent = NULL;
	uint32_t		 parent_set_idx;

	D_ASSERT(hash_offset != NULL && info != NULL);
	D_ASSERT(ts_set->ts_set_size != ts_set->ts_init_count);

	*hash_offset = 0;

	if (ts_set->ts_init_count == 0) {
		*info = &ts_table->tt_type_info[0];
		return;
	}

	/* if current entry is one of many akeys, backoff to last dkey */
	parent_set_idx = MIN(ts_set->ts_init_count - 1, VOS_TS_TYPE_DKEY);
	set_entry = &ts_set->ts_entries[parent_set_idx];
	parent = set_entry->se_entry;

	*info = parent->te_info + 1;

	if ((*info)->ti_type <= VOS_TS_TYPE_OBJ)
		return; /** Container has no negative entries at present. */

	/** Return the index of the negative entry */
	if (parent->te_negative == NULL) {
		*hash_offset = parent - parent->te_info->ti_misses;
		return;
	}

	*hash_offset = parent->te_negative - parent->te_info->ti_misses;
}

/** Returns true of we are inside a transaction and the
 *  timestamp set is valid.
 *
 * \param[in]	ts_set	The timestamp set
 */
static inline bool
vos_ts_in_tx(const struct vos_ts_set *ts_set)
{
	if (ts_set == NULL || !ts_set->ts_in_tx)
		return false;

	return true;
}

/** Reset the index in the set so an entry can be replaced
 *
 * \param[in]	ts_set		The timestamp set
 * \param[in]	type		Type of entry
 * \param[in]	akey_idx	Set to 0 if not akey, otherwise idx of akey
 */
static inline void
vos_ts_set_reset(struct vos_ts_set *ts_set, uint32_t type, uint32_t akey_nr)
{
	uint32_t	idx;

	if (!vos_ts_in_tx(ts_set))
		return;

	D_ASSERT((type == VOS_TS_TYPE_AKEY) || (akey_nr == 0));
	idx = type + akey_nr;
	D_ASSERT(idx <= ts_set->ts_init_count);
	ts_set->ts_init_count = idx;
}

static inline bool
vos_ts_lookup_internal(struct vos_ts_set *ts_set, uint32_t type, uint32_t *idx,
		       struct vos_ts_entry **entryp)
{
	struct vos_ts_table	*ts_table = vos_ts_table_get(false);
	struct vos_ts_info	*info = &ts_table->tt_type_info[type];
	void			*entry;
	struct vos_ts_set_entry	 set_entry = {0};
	bool found;

	found = lrua_lookup(info->ti_array, idx, &entry);
	if (found) {
		D_ASSERT(ts_set->ts_set_size != ts_set->ts_init_count);
		set_entry.se_entry = entry;
		ts_set->ts_entries[ts_set->ts_init_count++] = set_entry;
		*entryp = entry;
		return true;
	}

	return false;
}

/** Lookup an entry in the timestamp cache and save it to the set.
 *
 * \param[in]		ts_set	The timestamp set
 * \param[in,out]	idx	Address of the entry index.
 * \param[in]		reset	Remove the last entry in the set before checking
 * \param[in]		entryp	Valid only if function returns true.  Will be
 *				NULL if ts_set is NULL.
 *
 * \return true if the timestamp set is NULL or the entry is found in cache
 */
static inline bool
vos_ts_lookup(struct vos_ts_set *ts_set, uint32_t *idx, bool reset,
	      struct vos_ts_entry **entryp)
{
	uint32_t		 type;

	*entryp = NULL;

	if (!vos_ts_in_tx(ts_set))
		return true;

	if (reset)
		ts_set->ts_init_count--;

	type = MIN(ts_set->ts_init_count, VOS_TS_TYPE_AKEY);

	return vos_ts_lookup_internal(ts_set, type, idx, entryp);
}

/** Internal function to evict LRU and initialize an entry */
void
vos_ts_evict_lru(struct vos_ts_table *ts_table, struct vos_ts_entry **new_entry,
		 uint32_t *idx, uint32_t hash_idx, uint32_t new_type);

/** Internal function to calculate index of negative entry */
static uint32_t
vos_ts_get_hash_idx(struct vos_ts_info *info, uint64_t hash,
		    uint64_t parent_idx)
{
	return (hash + (parent_idx * 17)) & info->ti_cache_mask;
}

/** Allocate a new entry in the set.   Lookup should be called first and this
 * should only be called if it returns false.
 *
 * \param[in]	ts_set	The timestamp set
 * \param[in]	idx	Address of the entry index.
 * \param[in]	hash	Hash to identify the item
 *
 * \return	Returns a pointer to the entry or NULL if ts_set is not
 *		allocated or we detected a duplicate akey.
 */
static inline struct vos_ts_entry *
vos_ts_alloc(struct vos_ts_set *ts_set, uint32_t *idx, uint64_t hash)
{
	uint64_t		 hash_offset = 0;
	struct vos_ts_table	*ts_table;
	struct vos_ts_info	*info = NULL;
	struct vos_ts_set_entry	 set_entry = {0};
	struct vos_ts_entry	*new_entry;
	uint32_t		 hash_idx;

	if (!vos_ts_in_tx(ts_set))
		return NULL;

	ts_table = vos_ts_table_get(false);

	vos_ts_set_get_info(ts_table, ts_set, &info, &hash_offset);

	/** By combining the parent entry offset, we avoid using the same
	 *  index for every key with the same value.
	 */
	hash_idx = vos_ts_get_hash_idx(info, hash, hash_offset);

	vos_ts_evict_lru(ts_table, &new_entry, idx, hash_idx, info->ti_type);

	set_entry.se_entry = new_entry;

	/** No need to save allocation hash for non-negative entry */
	ts_set->ts_entries[ts_set->ts_init_count++] = set_entry;
	return new_entry;
}

/** Get the last entry in the set
 *
 * \param[in]	ts_set	The timestamp set
 *
 * \return Returns the last entry added to the set or NULL
 */
static inline struct vos_ts_entry *
vos_ts_set_get_entry(struct vos_ts_set *ts_set)
{
	struct vos_ts_set_entry	*entry;

	if (!vos_ts_in_tx(ts_set) || ts_set->ts_init_count == 0)
		return NULL;

	entry = &ts_set->ts_entries[ts_set->ts_init_count - 1];
	return entry->se_entry;
}

/** When a subtree doesn't exist, we need a negative entry.  The entry in this
 *  case is identified by a hash.  This looks up the negative entry and
 *  allocates it if necessary.  Resets te_create_idx to NULL.
 *
 * \param[in]	ts_set	The timestamp set
 * \param[in]	hash	The hash of the missing subtree entry
 * \param[in]	reset	Remove the last entry in the set before checking
 *
 * \return	The entry for negative lookups on the subtree
 */
static inline struct vos_ts_entry *
vos_ts_get_negative(struct vos_ts_set *ts_set, uint64_t hash, bool reset)
{
	struct vos_ts_info	*info;
	struct vos_ts_table	*ts_table;
	struct vos_ts_set_entry	 set_entry = {0};
	uint64_t		 hash_offset;
	uint64_t		 hash_idx;

	if (!vos_ts_in_tx(ts_set))
		return NULL;

	if (reset)
		ts_set->ts_init_count--;

	ts_table = vos_ts_table_get(false);

	vos_ts_set_get_info(ts_table, ts_set, &info, &hash_offset);

	hash_idx = vos_ts_get_hash_idx(info, hash, hash_offset);

	set_entry.se_entry = &info->ti_misses[hash_idx];

	ts_set->ts_entries[ts_set->ts_init_count++] = set_entry;

	return set_entry.se_entry;
}

/** Do an uncertainty check on the entry.  Return true if there
 *  is a write within the epoch uncertainty bound or if it
 *  can't be determined that the epoch is safe (e.g. a cache miss).
 *
 *  There are the following cases for an uncertainty check
 *  1. The access timestamp is earlier than both.  In such
 *     case, we have a cache miss and can't determine whether
 *     there is uncertainty so we must reject the access.
 *  2. The access is later than the first and the bound is
 *     less than or equal to the high time.  No conflict in
 *     this case because the write is outside the undertainty
 *     bound.
 *  3. The access is later than the first but the bound is
 *     greater than the high timestamp.  We must reject the
 *     access because there is an uncertain write.
 *  4. The access is later than both timestamps.  No conflict
 *     in this case.
 *
 *  \param[in]	ts_set	The timestamp set
 *  \param[in]	epoch	The epoch of the update
 *  \param[in]	bound	The uncertainty bound
 */
static inline bool
vos_ts_wcheck(struct vos_ts_set *ts_set, daos_epoch_t epoch,
	      daos_epoch_t bound)
{
	struct vos_wts_cache	*wcache;
	struct vos_ts_set_entry	*se;
	uint32_t		 high_idx;
	daos_epoch_t		 high;
	daos_epoch_t		 second;

	if (!vos_ts_in_tx(ts_set) || ts_set->ts_init_count == 0 ||
	    bound <= epoch)
		return false;

	se = &ts_set->ts_entries[ts_set->ts_init_count - 1];

	if (se->se_entry == NULL)
		return false;

	wcache = &se->se_entry->te_w_cache;
	high_idx = wcache->wc_w_high;
	high = wcache->wc_ts_w[high_idx];
	if (epoch >= high) /* Case #4, the access is newer than any write */
		return false;

	second = wcache->wc_ts_w[1 - high_idx];
	if (epoch < second) /* Case #1, Cache miss, not enough history */
		return true;

	/* We know at this point that second <= epoch so we need to determine
	 * only if the high time is inside the uncertainty bound.
	 */
	if (bound >= high) /* Case #3, Uncertain write conflict */
		return true;

	/* Case #2, No write conflict, all writes outside the bound */
	return false;
}

/** Set the type of the next entry.  This gets set automatically
 *  by default in vos_ts_set_add to child type of entry being
 *  inserted so only required when this isn't suitable
 *
 *  \param[in]	ts_set	The timestamp set
 *  \param[in]	type	The type of the next insertion
 */
static inline void
vos_ts_set_type(struct vos_ts_set *ts_set, uint32_t type)
{
	if (!vos_ts_in_tx(ts_set))
		return;

	ts_set->ts_etype = type;
}

static inline int
vos_ts_set_add(struct vos_ts_set *ts_set, uint32_t *idx, const void *rec,
	       size_t rec_size)
{
	struct vos_ts_set_entry	*se;
	struct vos_ts_entry	*entry;
	uint64_t		 hash = 0;
	uint32_t		 expected_type;

	if (!vos_ts_in_tx(ts_set))
		return 0;

	if (idx == NULL)
		goto calc_hash;

	if (ts_set->ts_flags & VOS_OF_PUNCH_PROPAGATE)
		return 0; /* Set already populated */

	if (ts_set->ts_init_count == ts_set->ts_set_size)
		return -DER_BUSY; /** No more room in the set */

	if (vos_ts_lookup(ts_set, idx, false, &entry)) {
		vos_kh_clear(false);
		expected_type = entry->te_info->ti_type;
		D_ASSERT(expected_type == ts_set->ts_etype);
		goto set_params;
	}

calc_hash:
	if (ts_set->ts_etype > VOS_TS_TYPE_CONT) {
		/* sysdb pool should not come here */
		if (ts_set->ts_etype != VOS_TS_TYPE_OBJ) {
			hash = vos_hash_get(rec, rec_size, false);
		} else {
			daos_unit_oid_t *oid = (daos_unit_oid_t *)rec;

			/*
			 * Test shows using d_hash_murmur64() for oid
			 * is easy to conflict.
			 */
			hash = oid->id_pub.lo ^ oid->id_pub.hi;
		}
	}

	if (idx != NULL) {
		entry = vos_ts_alloc(ts_set, idx, hash);
		if (entry == NULL)
			return -DER_NO_PERM;
		expected_type = entry->te_info->ti_type;
		D_ASSERT(expected_type == ts_set->ts_etype);
	} else {
		entry = vos_ts_get_negative(ts_set, hash, false);
		D_ASSERT(entry != NULL);
		expected_type = entry->te_info->ti_type;
	}

set_params:
	D_ASSERT(ts_set->ts_init_count >= 1);
	se = &ts_set->ts_entries[ts_set->ts_init_count - 1];
	se->se_etype = ts_set->ts_etype;
	if (se->se_etype > ts_set->ts_max_type)
		ts_set->ts_max_type = se->se_etype;
	if (expected_type != VOS_TS_TYPE_AKEY)
		ts_set->ts_etype = expected_type + 1;
	se->se_entry = entry;
	se->se_create_idx = NULL;

	return 0;
}

/** Get the specified entry in the set
 *
 * \param[in]	ts_set		The timestamp set
 * \param[in]	type		The type of entry
 * \param[in]	akey_idx	0 or index of the akey
 *
 * \return Returns the last entry added to the set or NULL
 */
static inline struct vos_ts_entry *
vos_ts_set_get_entry_type(struct vos_ts_set *ts_set, uint32_t type,
			  int akey_idx)
{
	struct vos_ts_set_entry	*entry;
	uint32_t		 idx = (type / 2) + akey_idx;

	D_ASSERT(akey_idx == 0 || type == VOS_TS_TYPE_AKEY);

	if (!vos_ts_in_tx(ts_set) || idx >= ts_set->ts_init_count)
		return NULL;

	entry = &ts_set->ts_entries[idx];
	return entry->se_entry;
}

/** Set the index of the associated positive entry in the last entry
 *  in the set.
 *
 *  \param[in]	ts_set	The timestamp set
 *  \param[in]	idx	Pointer to the index that will be used
 *			when allocating the positive entry
 */
static inline void
vos_ts_set_mark_entry(struct vos_ts_set *ts_set, uint32_t *idx)
{
	struct vos_ts_set_entry	*entry;

	if (!vos_ts_in_tx(ts_set) || *idx >= ts_set->ts_init_count)
		return;

	entry = &ts_set->ts_entries[ts_set->ts_init_count - 1];

	/** Should be a negative entry */
	D_ASSERT(entry->se_entry->te_negative == NULL);
	entry->se_create_idx = idx;
}

/** If an entry is still in the thread local timestamp cache, evict it and
 *  update global timestamps for the type.  Move the evicted entry to the LRU
 *  and mark it as already evicted.
 *
 * \param[in]	idx	Address of the entry index.
 * \param[in]	type	Type of the object
 */
static inline void
vos_ts_evict(uint32_t *idx, uint32_t type, bool standalone)
{
	struct vos_ts_table	*ts_table = vos_ts_table_get(standalone);

	if (ts_table == NULL)
		return;

	lrua_evict(ts_table->tt_type_info[type].ti_array, idx);
}

static inline bool
vos_ts_peek_entry(uint32_t *idx, uint32_t type, struct vos_ts_entry **entryp,
		  bool standalone)
{
	struct vos_ts_table	*ts_table = vos_ts_table_get(standalone);
	struct vos_ts_info      *info;

	if (ts_table == NULL)
		return false;

	info = &ts_table->tt_type_info[type];

	return lrua_peek(info->ti_array, idx, entryp);
}

/** Allocate thread local timestamp cache.   Set the initial global times
 *
 * \param[in,out]	ts_table	Thread local table pointer
 *
 * \return		-DER_NOMEM	Not enough memory available
 *			0		Success
 */
int
vos_ts_table_alloc(struct vos_ts_table **ts_table);


/** Free the thread local timestamp cache and reset pointer to NULL
 *
 * \param[in,out]	ts_table	Thread local table pointer
 */
void
vos_ts_table_free(struct vos_ts_table **ts_table);

/** Allocate a timestamp set
 *
 * \param[in,out]	ts_set	Pointer to set
 * \param[in]		flags	Operations flags
 * \param[in]		cflags	Check/update flags
 * \param[in]		akey_nr	Number of akeys in operation
 * \param[in]		dth	Optional transaction handle
 * \param[in]		standalone use standalone tls
 *
 * \return	0 on success, error otherwise.
 */
int
vos_ts_set_allocate(struct vos_ts_set **ts_set, uint64_t flags,
		    uint16_t cflags, uint32_t akey_nr,
		    const struct dtx_handle *dth, bool standalone);

/** Upgrade any negative entries in the set now that the associated
 *  update/punch has committed
 *
 *  \param[in]	ts_set	Pointer to set
 */
void
vos_ts_set_upgrade(struct vos_ts_set *ts_set);

/** Free an allocated timestamp set
 *
 * Implemented as a macro to improve logging.
 *
 * \param[in]	ts_set	Set to free
 */

#define vos_ts_set_free(ts_set) D_FREE(ts_set)

/** Internal API to copy timestamp */
static inline void
vos_ts_copy(daos_epoch_t *dest_epc, struct dtx_id *dest_id,
	    daos_epoch_t src_epc, const struct dtx_id *src_id)
{
	*dest_epc = src_epc;
	daos_dti_copy(dest_id, src_id);
}

/** Internal API to update low read timestamp and tx id */
static inline void
vos_ts_rl_update(struct vos_ts_entry *entry, daos_epoch_t read_time,
		 const struct dtx_id *tx_id)
{
	if (entry == NULL || read_time < entry->te_ts.tp_ts_rl)
		return;

	vos_ts_copy(&entry->te_ts.tp_ts_rl, &entry->te_ts.tp_tx_rl,
		    read_time, tx_id);
}

/** Internal API to update high read timestamp and tx id */
static inline void
vos_ts_rh_update(struct vos_ts_entry *entry, daos_epoch_t read_time,
		 const struct dtx_id *tx_id)
{
	if (entry == NULL || read_time < entry->te_ts.tp_ts_rh)
		return;

	vos_ts_copy(&entry->te_ts.tp_ts_rh, &entry->te_ts.tp_tx_rh,
		    read_time, tx_id);
}

/** Internal API to check read conflict of a given entry */
bool
vos_ts_check_read_conflict(struct vos_ts_set *ts_set, int idx,
			   daos_epoch_t write_time);

/** Checks the set for read/write conflicts
 *
 * \param[in]	ts_set		The timestamp read set
 * \param[in]	write_time	The time of the update
 *
 * \return	true		Conflict
 *		false		No conflict (or no timestamp set)
 */
static inline int
vos_ts_set_check_conflict(struct vos_ts_set *ts_set, daos_epoch_t write_time)
{
	int			 i;

	if (!vos_ts_in_tx(ts_set))
		return false;

	if ((ts_set->ts_cflags & VOS_TS_WRITE_MASK) == 0)
		return false;

	for (i = 0; i < ts_set->ts_init_count; i++) {
		/** Will check the appropriate read timestamp based on the type
		 *  of the entry at index i.
		 */
		if (vos_ts_check_read_conflict(ts_set, i, write_time))
			return true;
	}

	return false;
}

/** Append VOS_OF flags to timestamp set
 *  \param[in] ts_set		The timestamp set
 *  \param[in] flags		VOS_OF flag(s) to add to set
 */
static inline void
vos_ts_set_append_vflags(struct vos_ts_set *ts_set, uint64_t flags)
{
	if (!vos_ts_in_tx(ts_set))
		return;

	ts_set->ts_flags |= flags;
}

/** Append check/update flags to timestamp set
 *  \param[in] ts_set		The timestamp set
 *  \param[in] flags		flags
 */
static inline void
vos_ts_set_append_cflags(struct vos_ts_set *ts_set, uint16_t flags)
{
	if (!vos_ts_in_tx(ts_set))
		return;

	ts_set->ts_cflags |= flags;

	if (ts_set->ts_cflags & VOS_TS_WRITE_OBJ)
		ts_set->ts_wr_level = VOS_TS_TYPE_OBJ;
	else if (ts_set->ts_cflags & VOS_TS_WRITE_DKEY)
		ts_set->ts_wr_level = VOS_TS_TYPE_DKEY;
	else if (ts_set->ts_cflags & VOS_TS_WRITE_AKEY)
		ts_set->ts_wr_level = VOS_TS_TYPE_AKEY;

	if (ts_set->ts_cflags & VOS_TS_READ_CONT)
		ts_set->ts_rd_level = VOS_TS_TYPE_CONT;
	else if (ts_set->ts_cflags & VOS_TS_READ_OBJ)
		ts_set->ts_rd_level = VOS_TS_TYPE_OBJ;
	else if (ts_set->ts_cflags & VOS_TS_READ_DKEY)
		ts_set->ts_rd_level = VOS_TS_TYPE_DKEY;
	else if (ts_set->ts_cflags & VOS_TS_READ_AKEY)
		ts_set->ts_rd_level = VOS_TS_TYPE_AKEY;
}

/** Update the read timestamps for the set after a successful operation
 *
 *  \param[in]	ts_set		The timestamp set
 *  \param[in]	read_time	The new read timestamp
 */
static inline void
vos_ts_set_update(struct vos_ts_set *ts_set, daos_epoch_t read_time)
{
	struct vos_ts_set_entry	*se;
	int			 i;
	uint16_t		 read_level;

	if (!vos_ts_in_tx(ts_set))
		return;

	if (DAOS_FAIL_CHECK(DAOS_DTX_NO_READ_TS))
		return;

	if ((ts_set->ts_cflags & VOS_TS_READ_MASK) == 0)
		return;

	if (ts_set->ts_max_type < ts_set->ts_rd_level)
		read_level = ts_set->ts_max_type;
	else
		read_level = ts_set->ts_rd_level;

	for (i = 0; i < ts_set->ts_init_count; i++) {
		se = &ts_set->ts_entries[i];

		if (se->se_etype > read_level)
			continue; /** We would have updated the high
				   *  timestamp at a higher level
				   */

		if (se->se_etype == read_level)
			vos_ts_rl_update(se->se_entry, read_time,
					 &ts_set->ts_tx_id);
		vos_ts_rh_update(se->se_entry, read_time,
				 &ts_set->ts_tx_id);
	}
}

static inline void
vos_ts_update_wcache(struct vos_wts_cache *wcache, daos_epoch_t write_time)
{
	daos_epoch_t		*high;
	daos_epoch_t		*second;

	/** We store only the highest two timestamps so workout which timestamp
	 *  should be replaced, if any and replace it
	 */
	high = &wcache->wc_ts_w[wcache->wc_w_high];
	second = &wcache->wc_ts_w[1 - wcache->wc_w_high];

	if (write_time <= *second || write_time == *high)
		return;

	/** We know it's not older than both timestamps and is unique, so
	 *  check for which one to replace.  If the high needs to be
	 *  replaced, we simply move the index of the high and still
	 *  replace the second one.
	 */
	if (write_time > *high)
		wcache->wc_w_high = 1 - wcache->wc_w_high;
	*second = write_time;
}

/** Update the write timestamps for the set after a successful operation
 *
 *  \param[in]	ts_set		The timestamp set
 *  \param[in]	write_time	The new write timestamp
 */
static inline void
vos_ts_set_wupdate(struct vos_ts_set *ts_set, daos_epoch_t write_time)
{
	struct vos_ts_set_entry	*se;
	int			 i;

	if (!vos_ts_in_tx(ts_set))
		return;

	for (i = 0; i < ts_set->ts_init_count; i++) {
		se = &ts_set->ts_entries[i];
		if (se->se_entry == NULL)
			continue;

		vos_ts_update_wcache(&se->se_entry->te_w_cache, write_time);
	}
}

/** Save the current state of the set
 *
 * \param[in]	ts_set	The timestamp set
 * \param[out]	save	Target to save state to
 */
static inline void
vos_ts_set_save(struct vos_ts_set *ts_set, struct vos_ts_set *save)
{
	if (ts_set == NULL)
		return;

	*save = *ts_set;
}

/** Restore previously saved state of the set
 *
 * \param[in]	ts_set	The timestamp set
 * \param[in]	restore	The saved state
 */
static inline void
vos_ts_set_restore(struct vos_ts_set *ts_set, const struct vos_ts_set *restore)
{
	if (ts_set == NULL)
		return;

	*ts_set = *restore;
}
#endif /* __VOS_TS__ */
