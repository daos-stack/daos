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
 * Generic struct for allocating LRU entries in an array
 * common/lru_array.h
 *
 * Author: Jeff Olivier <jeffrey.v.olivier@intel.com>
 */

#ifndef __LRU_ARRAY__
#define __LRU_ARRAY__

#include <daos/common.h>

struct lru_callbacks {
	/** Called when an entry is going to be evicted from cache */
	void	(*lru_on_evict)(void *entry, uint32_t idx, void *arg);
	/** Called on initialization of an entry */
	void	(*lru_on_init)(void *entry, uint32_t idx, void *arg);
	/** Called on finalization of an entry */
	void	(*lru_on_fini)(void *entry, uint32_t idx, void *arg);
};

struct lru_entry {
	/** The pointer to the index is unique identifier for the entry */
	uint64_t	 le_key;
	/** Pointer to this entry */
	void		*le_payload;
	/** Next index in LRU array */
	uint32_t	 le_next_idx;
	/** Previous index in LRU array */
	uint32_t	 le_prev_idx;
};

struct lru_array {
	/** Least recently accessed index */
	uint32_t		la_lru;
	/** Most recently accessed index */
	uint32_t		la_mru;
	/** Number of indices */
	uint32_t		la_count;
	/** record size */
	uint16_t		la_record_size;
	/** eviction count */
	uint16_t		la_evicting;
	/** Allocated payload entries */
	void			*la_payload;
	/** Callbacks for implementation */
	struct lru_callbacks	 la_cbs;
	/** User callback argument passed on init */
	void			*la_arg;
	/** Entries in the array */
	struct lru_entry	 la_table[0];
};

/** Internal API: Evict the LRU, move it to MRU, invoke eviction callback,
 *  and return the index
 */
void
lrua_evict_lru(struct lru_array *array, struct lru_entry **entry,
	       uint32_t *idx, uint64_t key, bool evict_lru);

/** Internal API: Remove an entry from the lru list */
static inline void
lrua_remove_entry(struct lru_entry *entries, struct lru_entry *entry)
{
	struct lru_entry	*prev = &entries[entry->le_prev_idx];
	struct lru_entry	*next = &entries[entry->le_next_idx];

	/** This is an internal API used to remove an entry that will be
	 *  immediately added back.  So we should never get down to a
	 *  single entry in practice.
	 */
	D_ASSERT(prev != next);
	prev->le_next_idx = entry->le_next_idx;
	next->le_prev_idx = entry->le_prev_idx;
}

/** Internal API: Insert an entry in the lru list */
static inline void
lrua_insert_mru(struct lru_array *array, struct lru_entry *entry,
		uint32_t idx)
{
	struct lru_entry	*entries = &array->la_table[0];
	struct lru_entry	*prev;
	struct lru_entry	*next;

	prev = &entries[array->la_mru];
	next = &entries[array->la_lru];
	D_ASSERT(prev != next);
	next->le_prev_idx = idx;
	prev->le_next_idx = idx;
	entry->le_prev_idx = array->la_mru;
	entry->le_next_idx = array->la_lru;
}

/** Internal API: Make the entry the mru */
static inline void
lrua_move_to_mru(struct lru_array *array, struct lru_entry *entry, uint32_t idx)
{
	if (array->la_mru == idx) {
		/** Already the mru */
		return;
	}

	if (array->la_lru == idx) {
		/** Ordering doesn't change in circular list so just update
		 *  the lru and mru idx
		 */
		array->la_lru = entry->le_next_idx;
		goto set_mru;
	}

	/** First remove */
	lrua_remove_entry(&array->la_table[0], entry);

	/** Insert between MRU and LRU */
	lrua_insert_mru(array, entry, idx);

set_mru:
	array->la_mru = idx;
}

/** Internal API to lookup entry from index */
static inline struct lru_entry *
lrua_lookup_idx(struct lru_array *array, uint32_t idx, uint64_t key)
{
	struct lru_entry	*entry;

	if (idx >= array->la_count)
		return NULL;

	entry = &array->la_table[idx];
	if (entry->le_key == key) {
		if (!array->la_evicting) {
			/** Only make mru if we are not evicting it */
			lrua_move_to_mru(array, entry, idx);
		}
		return entry;
	}

	return NULL;
}

/** Lookup an entry in the lru array with alternative key.
 *
 * \param	array[in]	The lru array
 * \param	idx[in]		The index of the entry
 * \param	idx[in]		Unique identifier
 * \param	entryp[in,out]	Valid only if function returns true.
 *
 * \return true if the entry is in the array and set \p entryp accordingly
 */
static inline bool
lrua_lookupx(struct lru_array *array, uint32_t idx, uint64_t key,
	     void **entryp)
{
	struct lru_entry	*entry;

	D_ASSERT(array != NULL);
	D_ASSERT(key != 0);

	*entryp = NULL;

	entry = lrua_lookup_idx(array, idx, key);
	if (entry == NULL)
		return false;

	*entryp = entry->le_payload;
	return true;
}

/** Lookup an entry in the lru array.
 *
 * \param	array[in]	The lru array
 * \param	idx[in,out]	Address of the record index.
 * \param	entryp[in,out]	Valid only if function returns true.
 *
 * \return true if the entry is in the array and set \p entryp accordingly
 */
static inline bool
lrua_lookup(struct lru_array *array, const uint32_t *idx,
	    void **entryp)
{
	return lrua_lookupx(array, *idx, (uint64_t)idx, entryp);
}

/** Allocate a new entry lru array with alternate key specifier.
 *  This should only be called if lookup would return false.  This will
 *  modify idx.  If called within a transaction and the value needs to
 *  persist, the old value should be logged before calling this function.
 *
 * \param	array[in]	The LRU array
 * \param	idx[in,out]	Index address in, allocated index out
 * \param	key[in]		Unique identifier of entry
 * \param	evict_lru[in]	True if LRU should be evicted
 *
 * \return	Returns a pointer to the entry or NULL if evict_lru is false
 *		and the entry at the LRU is allocated
 */
static inline void *
lrua_allocx(struct lru_array *array, uint32_t *idx, uint64_t key,
	    bool evict_lru)
{
	struct lru_entry	*new_entry;

	D_ASSERT(array != NULL);
	D_ASSERT(key != 0);

	lrua_evict_lru(array, &new_entry, idx, key, evict_lru);

	if (new_entry == NULL)
		return NULL;

	return new_entry->le_payload;
}

/** Allocate a new entry lru array.   This should only be called if lookup
 *  would return false.  This will modify idx.  If called within a
 *  transaction and the value needs to persist, the old value should be
 *  logged before calling this function.
 *
 * \param	array[in]	The LRU array
 * \param	idx[in,out]	Address of the entry index.
 * \param	evict_lru[in]	True if LRU should be evicted
 *
 * \return	Returns a pointer to the entry or NULL if evict_lru is false
 *		and the entry at the LRU is allocated
 */
static inline void *
lrua_alloc(struct lru_array *array, uint32_t *idx, bool evict_lru)
{
	return lrua_allocx(array, idx, (uint64_t)idx, evict_lru);
}

/** Allocate an entry in place.  Used for recreating an old array.
 *
 * \param	array[in]	The LRU array
 * \param	idx[in]		Index of entry.
 * \param	key[in]		Address of the entry index.
 *
 * \return	Returns a pointer to the entry or NULL on error
 */
static inline void *
lrua_allocx_inplace(struct lru_array *array, uint32_t idx, uint64_t key)
{
	struct lru_entry	*entry;

	D_ASSERT(array != NULL);
	D_ASSERT(key != 0);
	if (idx >= array->la_count) {
		D_ERROR("Index %d is out of range\n", idx);
		return NULL;
	}

	entry = &array->la_table[idx];
	if (entry->le_key != key && entry->le_key != 0) {
		D_ERROR("Cannot allocated idx %d in place\n", idx);
		return NULL;
	}

	entry->le_key = key;
	lrua_move_to_mru(array, entry, idx);

	return entry->le_payload;

}

/** If an entry is still in the array, evict it and invoke eviction callback.
 *  Move the evicted entry to the LRU and mark it as already evicted.
 *
 * \param	array[in]	Address of the LRU array.
 * \param	idx[in]		Index of the entry
 * \param	key[in]		Unique identifier
 */
void
lrua_evictx(struct lru_array *array, uint32_t idx, uint64_t key);

/** If an entry is still in the array, evict it and invoke eviction callback.
 *  Move the evicted entry to the LRU and mark it as already evicted.
 *
 * \param	array[in]	Address of the LRU array.
 * \param	idx[in]		Address of the entry index.
 */
static inline void
lrua_evict(struct lru_array *array, uint32_t *idx)
{
	lrua_evictx(array, *idx, (uint64_t)idx);
}

/** Allocate an LRU array
 *
 * \param	array[in,out]	Pointer to LRU array
 * \param	nr_ent[in]	Number of records in array
 * \param	rec_size[in]	Size of each record
 * \param	cbs[in]		Optional callbacks
 * \param	arg[in]		Optional argument passed to all callbacks
 *
 * \return	-DER_NOMEM	Not enough memory available
 *		0		Success
 */
int
lrua_array_alloc(struct lru_array **array, uint32_t nr_ent,
		 uint16_t record_size, const struct lru_callbacks *cbs,
		 void *arg);

/** Free an LRU array
 *
 * \param	array[in]	Pointer to LRU array
 */
void
lrua_array_free(struct lru_array *array);

#endif /* __LRU_ARRAY__ */
