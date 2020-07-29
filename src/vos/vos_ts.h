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
 * vos/vos_ts.h
 *
 * Author: Jeff Olivier <jeffrey.v.olivier@intel.com>
 */

#ifndef __VOS_TS__
#define __VOS_TS__

#include <lru_array.h>
#include <vos_tls.h>

struct vos_ts_table;

struct vos_ts_info {
	/** The LRU array */
	struct lru_array	*ti_array;
	/** Back pointer to table */
	struct vos_ts_table	*ti_table;
	/** Miss indexes for the type */
	uint32_t		*ti_misses;
	/** Type identifier */
	uint32_t		ti_type;
	/** mask for hash of negative entries */
	uint32_t		ti_cache_mask;
	/** Number of entries in cache for type (for testing) */
	uint32_t		ti_count;
};

struct vos_ts_entry {
	struct vos_ts_info	*te_info;
	/** Key for current occupant */
	uint32_t		*te_record_ptr;
	/** Uniquely identifies the parent record */
	uint32_t		*te_parent_ptr;
	/** negative entry cache */
	uint32_t		*te_miss_idx;
	/** Low read time or read time for the object/key */
	daos_epoch_t		 te_ts_rl;
	/** Max read time for subtrees */
	daos_epoch_t		 te_ts_rh;
	/** uuid's of transactions.  These can potentially be changed
	 *  to 16 bits and save some space here.  But for now, stick
	 *  with the full id.
	 */
	/** Low read tx */
	uuid_t			 te_tx_rl;
	/** high read tx */
	uuid_t			 te_tx_rh;
	/** Hash index in parent */
	uint32_t		 te_hash_idx;
};

struct vos_ts_set_entry {
	/** Pointer to the entry at this level */
	struct vos_ts_entry	*se_entry;
	/** pointer to newly created index */
	uint32_t		*se_create_idx;
	/** Cache of calculated hash for obj/key */
	uint64_t		 se_hash;
};

/** Structure looking up and caching operation flags */
struct vos_ts_set {
	/** Operation flags */
	uint64_t		 ts_flags;
	/** Transaction that owns the set */
	uuid_t			 ts_tx_id;
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
	ACTION(VOS_TS_TYPE_OBJ_MISS,	"object miss",	16 * 1024)	\
	ACTION(VOS_TS_TYPE_OBJ,		"object",	32 * 1024)	\
	ACTION(VOS_TS_TYPE_DKEY_MISS,	"dkey miss",	64 * 1024)	\
	ACTION(VOS_TS_TYPE_DKEY,	"dkey",		128 * 1024)	\
	ACTION(VOS_TS_TYPE_AKEY_MISS,	"akey miss",	256 * 1024)	\
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
	/** Transaciton id associated with global read low timestamp */
	uuid_t			tt_tx_rl;
	/** Transaciton id associated with global read high timestamp */
	uuid_t			tt_tx_rh;
	/** Miss index table */
	uint32_t		*tt_misses;
	/** Timestamp table pointers for a type */
	struct vos_ts_info	tt_type_info[VOS_TS_TYPE_COUNT];
};

/** Internal API: Grab the parent entry from the set */
static inline struct vos_ts_entry *
ts_set_get_parent(struct vos_ts_set *ts_set)
{
	struct vos_ts_set_entry	*set_entry;
	struct vos_ts_entry	*parent = NULL;
	uint32_t		 parent_set_idx;

	D_ASSERT(ts_set->ts_set_size != ts_set->ts_init_count);
	if (ts_set->ts_init_count > 0) {
		/** 2 is dkey index in case there are multiple akeys */
		parent_set_idx = MIN(ts_set->ts_init_count - 1, 2);
		set_entry = &ts_set->ts_entries[parent_set_idx];
		parent = set_entry->se_entry;
	}

	return parent;

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

	if (ts_set == NULL)
		return;

	D_ASSERT((type == VOS_TS_TYPE_AKEY) || (akey_nr == 0));
	D_ASSERT((type & 1) == 0);
	idx = type / 2 + akey_nr;
	D_ASSERT(idx <= ts_set->ts_init_count);
	ts_set->ts_init_count = idx;
}

static inline bool
vos_ts_lookup_internal(struct vos_ts_set *ts_set, uint32_t type, uint32_t *idx,
		       struct vos_ts_entry **entryp)
{
	struct vos_ts_table	*ts_table = vos_ts_table_get();
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

	if (ts_set == NULL)
		return true;

	if (reset)
		ts_set->ts_init_count--;

	type = MIN(ts_set->ts_init_count * 2, VOS_TS_TYPE_AKEY);

	return vos_ts_lookup_internal(ts_set, type, idx, entryp);
}

/** Internal function to evict LRU and initialize an entry */
void
vos_ts_evict_lru(struct vos_ts_table *ts_table, struct vos_ts_entry *parent,
		 struct vos_ts_entry **new_entry, uint32_t *idx,
		 uint32_t hash_idx, uint32_t new_type);

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
	struct vos_ts_entry	*parent;
	struct vos_ts_table	*ts_table;
	struct vos_ts_info	*info;
	struct vos_ts_set_entry	 set_entry = {0};
	struct vos_ts_entry	*new_entry;
	uint32_t		 hash_idx;
	uint32_t		 new_type = 0;

	if (ts_set == NULL)
		return NULL;


	ts_table = vos_ts_table_get();

	parent = ts_set_get_parent(ts_set);

	if (parent == NULL) {
		hash_idx = 0;
		info = &ts_table->tt_type_info[0];
	} else {
		info = parent->te_info;
		if (info->ti_type & 1) {
			/** this can happen if it's a duplicate key.  Return
			 * NULL in this case
			 */
			return NULL;
		}
		hash_idx = hash & info->ti_cache_mask;
		new_type = info->ti_type + 2;
	}

	D_ASSERT((info->ti_type & 1) == 0);
	D_ASSERT(info->ti_type != VOS_TS_TYPE_AKEY);

	vos_ts_evict_lru(ts_table, parent, &new_entry, idx, hash_idx,
			 new_type);

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

	if (ts_set == NULL || ts_set->ts_init_count == 0)
		return NULL;

	entry = &ts_set->ts_entries[ts_set->ts_init_count - 1];
	return entry->se_entry;
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

	if (ts_set == NULL || idx >= ts_set->ts_init_count)
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

	if (ts_set == NULL || ts_set->ts_init_count == 0)
		return;

	entry = &ts_set->ts_entries[ts_set->ts_init_count - 1];

	/** Should be a negative entry */
	D_ASSERT(entry->se_entry->te_info->ti_type & 1);
	entry->se_create_idx = idx;
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
	struct vos_ts_entry	*parent;
	struct vos_ts_info	*info;
	struct vos_ts_entry	*neg_entry;
	struct vos_ts_table	*ts_table;
	struct vos_ts_set_entry	 set_entry = {0};
	uint32_t		 idx;

	if (ts_set == NULL)
		return NULL;

	if (reset)
		ts_set->ts_init_count--;

	parent = ts_set_get_parent(ts_set);

	D_ASSERT(parent != NULL);

	info = parent->te_info;
	if (info->ti_type & 1) {
		/** Parent is a negative entry, just reuse it
		 *  for child entry
		 */
		neg_entry = parent;
		goto add_to_set;
	}

	ts_table = vos_ts_table_get();

	idx = hash & info->ti_cache_mask;
	if (vos_ts_lookup_internal(ts_set, info->ti_type + 1,
				   &parent->te_miss_idx[idx], &neg_entry)) {
		D_ASSERT(idx == neg_entry->te_hash_idx);
		goto out;
	}

	vos_ts_evict_lru(ts_table, parent, &neg_entry,
			 &parent->te_miss_idx[idx], idx, info->ti_type + 1);
	D_ASSERT(idx == neg_entry->te_hash_idx);
add_to_set:
	set_entry.se_entry = neg_entry;
	ts_set->ts_entries[ts_set->ts_init_count++] = set_entry;
out:
	ts_set->ts_entries[ts_set->ts_init_count-1].se_hash = hash;

	return neg_entry;
}

/** If an entry is still in the thread local timestamp cache, evict it and
 *  update global timestamps for the type.  Move the evicted entry to the LRU
 *  and mark it as already evicted.
 *
 * \param[in]	idx	Address of the entry index.
 * \param[in]	type	Type of the object
 */
static inline void
vos_ts_evict(uint32_t *idx, uint32_t type)
{
	struct vos_ts_table	*ts_table = vos_ts_table_get();

	lrua_evict(ts_table->tt_type_info[type].ti_array, idx);
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
 * \param[in]		akey_nr	Number of akeys in operation
 * \param[in]		tx_id	Optional transaction id
 *
 * \return	0 on success, error otherwise.
 */
int
vos_ts_set_allocate(struct vos_ts_set **ts_set, uint64_t flags,
		    uint32_t akey_nr, uuid_t *tx_id);

/** Upgrade any negative entries in the set now that the associated
 *  update/punch has committed
 *
 *  \param[in]	ts_set	Pointer to set
 */
void
vos_ts_set_upgrade(struct vos_ts_set *ts_set);

/** Free an allocated timestamp set
 *
 * \param[in]	ts_set	Set to free
 */
static inline void
vos_ts_set_free(struct vos_ts_set *ts_set)
{
	D_FREE(ts_set);
}

/** Update the low timestamp if the new read is newer
 *
 * \param[in]	entry		The timestamp entry
 * \param[in]	read_time	The new read timestamp
 * \param[in]	tx_id		The uuid of the new read
 */
static inline void
vos_ts_rl_update(struct vos_ts_entry *entry, daos_epoch_t read_time,
		 const uuid_t tx_id)
{
	if (entry == NULL || read_time < entry->te_ts_rl)
		return;

	entry->te_ts_rl = read_time;
	uuid_copy(entry->te_tx_rl, tx_id);
}

/** Update the low timestamp if the new read is newer
 *
 * \param[in]	entry		The timestamp entry
 * \param[in]	read_time	The new read timestamp
 * \param[in]	tx_id		The uuid of the new read
 */
static inline void
vos_ts_rh_update(struct vos_ts_entry *entry, daos_epoch_t read_time,
		 const uuid_t tx_id)
{
	if (entry == NULL || read_time < entry->te_ts_rh)
		return;

	entry->te_ts_rh = read_time;
	uuid_copy(entry->te_tx_rh, tx_id);
}

/** Check the read low timestamp at current entry.
 *
 * \param[in]	ts_set		The timestamp set
 * \param[in]	write_time	The write time
 *
 * \return	true	Conflict
 *		false	No conflict (or no timestamp set)
 */
static inline bool
vos_ts_check_rl_conflict(struct vos_ts_set *ts_set, daos_epoch_t write_time)
{
	struct vos_ts_entry	*entry;

	entry = vos_ts_set_get_entry(ts_set);
	if (entry == NULL || write_time > entry->te_ts_rl)
		return false;

	if (write_time != entry->te_ts_rl)
		return true;

	return uuid_compare(ts_set->ts_tx_id, entry->te_tx_rl) != 0;
}

/** Check the read high timestamp at current entry.
 *
 * \param[in]	ts_set		The timestamp set
 * \param[in]	write_time	The write time
 *
 * \return	true	Conflict
 *		false	No conflict (or no timestamp set)
 */
static inline bool
vos_ts_check_rh_conflict(struct vos_ts_set *ts_set, daos_epoch_t write_time)
{
	struct vos_ts_entry	*entry;

	entry = vos_ts_set_get_entry(ts_set);
	if (entry == NULL || write_time > entry->te_ts_rh)
		return false;

	if (write_time != entry->te_ts_rh)
		return true;

	return uuid_compare(ts_set->ts_tx_id, entry->te_tx_rh) != 0;
}

#endif /* __VOS_TS__ */
