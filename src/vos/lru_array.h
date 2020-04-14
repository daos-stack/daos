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
	uint32_t	*le_record_idx;
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
	uint32_t		la_record_size;
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
	       uint32_t *idx, bool evict_lru);

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
lrua_insert_entry(struct lru_entry *entries, struct lru_entry *entry,
		  uint32_t idx, uint32_t prev_idx, uint32_t next_idx)
{
	struct lru_entry	*prev;
	struct lru_entry	*next;

	prev = &entries[prev_idx];
	next = &entries[next_idx];
	next->le_prev_idx = idx;
	prev->le_next_idx = idx;
	entry->le_prev_idx = prev_idx;
	entry->le_next_idx = next_idx;
}

/** Internal API: Make the entry the mru */
static inline void
lrua_move_to_mru(struct lru_array *array, struct lru_entry *entry, uint32_t idx)
{
	if (array->la_mru == idx) {
		/** Already the mru */
		return;
	}

	if (array->la_lru == idx)
		array->la_lru = entry->le_next_idx;

	/** First remove */
	lrua_remove_entry(&array->la_table[0], entry);

	/** Now add */
	lrua_insert_entry(&array->la_table[0], entry, idx,
			  array->la_mru, array->la_lru);

	array->la_mru = idx;
}

/** Internal API to lookup entry from index */
static inline struct lru_entry *
lrua_lookup_idx(struct lru_array *array, const uint32_t *idx)
{
	struct lru_entry	*entry;
	uint32_t		 tindex = *idx;

	if (tindex >= array->la_count)
		return NULL;

	entry = &array->la_table[tindex];
	if (entry->le_record_idx == idx) {
		lrua_move_to_mru(array, entry, tindex);
		return entry;
	}

	return NULL;
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
	struct lru_entry	*entry;

	D_ASSERT(array != NULL);

	*entryp = NULL;

	entry = lrua_lookup_idx(array, idx);
	if (entry == NULL)
		return false;

	*entryp = entry->le_payload;
	return true;
}

/** Allocate a new entry lru array.   Lookup should be called first and this
 * should only be called if it returns false.  This will modify idx.  If
 * called within a transaction and the value needs to persist, the old value
 * should be logged before calling this function.
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
	struct lru_entry	*new_entry;

	D_ASSERT(array != NULL);

	lrua_evict_lru(array, &new_entry, idx, evict_lru);

	if (new_entry == NULL)
		return NULL;

	return new_entry->le_payload;
}

/** If an entry is still in the array, evict it and invoke eviction callback.
 *  Move the evicted entry to the LRU and mark it as already evicted.
 *
 * \param	array[in]	Address of the LRU array.
 * \param	idx[in]		Address of the entry index.
 */
void
lrua_evict(struct lru_array *array, uint32_t *idx);

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
		 uint32_t record_size, const struct lru_callbacks *cbs,
		 void *arg);


/** Free an LRU array
 *
 * \param	array[in]	Pointer to LRU array
 */
void
lrua_array_free(struct lru_array *array);

#endif /* __LRU_ARRAY__ */
