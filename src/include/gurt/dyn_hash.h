/*
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * \file
 *
 * Generic Dynamically Extended Hash Table APIs & data structures
 */

#ifndef __GURT_DYNHASH_H__
#define __GURT_DYNHASH_H__

#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>

#include <gurt/list.h>
#include <gurt/types.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef void *dh_item_t;
struct d_hash_table;

#define DYN_HASH_DEBUG 0

/** @addtogroup GURT
 * @{
 */
/******************************************************************************
 * Generic Hash Table APIs / data structures
 ******************************************************************************/
struct dh_bucket;
typedef struct dh_vector {
	/** actual vector size (bytes) */
	size_t		size;
	/** number of active bucket pointers */
	uint32_t	counter;
	/** set of buckect pointers */
	void		**data;
} dh_vector_t;

struct dyn_hash {
	/** SIP hash right shift for vector index calculation */
	uint8_t			ht_shift;
	/** total number of hash records */
	uint32_t		ht_records;
	/** vector (bucket pointer) */
	dh_vector_t		ht_vector;
	/** customized member functions */
	d_hash_table_ops_t	ht_ops;
	/** virtual internal global write lock function */
	void			(*ht_write_lock)(struct dyn_hash *htable);
	/** virtual internal global read lock function */
	void			(*ht_read_lock)(struct dyn_hash *htable);
	/** virtual internal global unlock function */
	void			(*ht_rw_unlock)(struct dyn_hash *htable);
	/** virtual internal bucket lock function */
	void			(*bucket_lock)(struct dh_bucket *bucket);
	/** virtual internal bucket unlock function */
	void			(*bucket_unlock)(struct dh_bucket *bucket);
	/** hash table magic signature */
	uint32_t		ht_magic;
	/** basic hash table reference */
	struct d_hash_table	*gtable;
#if DYN_HASH_DEBUG
	/** number of vector splits
	 * (updated only if DYN_HASH_FT_SHRING not set)
	 */
	uint32_t		ht_vsplits;
	/** accumulated vector spit time (usec)
	 * (updated only if DYN_HASH_FT_SHRING not set)
	 */
	uint32_t		ht_vsplit_delay;
	/** maximum number of hash records */
	uint32_t		ht_nr_max;
#endif
};

/**
 * Create a new hash table.
 *
 * \note Please be careful while using rwlock and refcount at the same time,
 * see \ref d_hash_feats for the details.
 *
 * \param[in] feats		Feature bits, see DYN_HASH_FT_*
 * \param[in] bits		power2 (bits) for number of bucket mutexes
 *                      (ignored if DYN_HASH_FT_GLOCK is set)
 * \param[in] hops		Customized member functions
 * \param[out] htable_pp	The newly created hash table
 *
 * \return			0 on success, negative value on error
 */
int dyn_hash_create(uint32_t feats, uint32_t bits, void *priv,
		    d_hash_table_ops_t *hops, struct d_hash_table **htable_pp);

/**
 * Initialize an inplace hash table.
 *
 * Does not allocate the htable pointer itself
 *
 * \note Please be careful while using rwlock and refcount at the same time,
 * see \ref d_hash_feats for the details.
 *
 * \param[in] feats		Feature bits, see DYN_HASH_FT_*
 * \param[in] bits		power2 (bits) for number of bucket mutexes
 * \param[in] hops		Customized member functions
 * \param[in] htable	Hash table to be initialized
 *
 * \return			0 on success, negative value on error
 */
int dyn_hash_table_create_inplace(uint32_t feats, uint32_t bits, void *priv,
				  d_hash_table_ops_t *hops,
				  struct d_hash_table *htable);

/**
 * Traverse a hash table, call the traverse callback function on every item.
 * Break once the callback returns non-zero.
 *
 * \param[in] htable	The hash table to be finalized.
 * \param[in] cb		Traverse callback, will be called on every item
 *						in the hash table.
 *						See \a d_hash_traverse_cb_t.
 * \param[in] arg			Arguments for the callback.
 *
 * \return			zero on success, negative value if error.
 */
int dyn_hash_table_traverse(struct d_hash_table *htable,
			    d_hash_traverse_cb_t cb, void *arg);

/**
 * Destroy a hash table.
 *
 * \param[in] htable	The hash table to be destroyed.
 * \param[in] force		true:
 *				Destroy the hash table even it is not empty,
 *				all pending items will be deleted.
 *				false:
 *				Destroy the hash table only if it is empty,
 *				otherwise returns error
 *
 * \return			zero on success, negative value if error.
 */
int dyn_hash_table_destroy(struct d_hash_table *htable, bool force);

/**
 * Finalize a hash table, reset all struct members.
 *
 * Note this does NOT free htable itself - only the members it contains.
 *
 * \param[in] htable		The hash table to be finalized.
 * \param[in] force		true:
 *				Finalize the hash table even it is not empty,
 *				all pending items will be deleted.
 *				false:
 *				Finalize the hash table only if it is empty,
 *				otherwise returns error
 *
 * \return			zero on success, negative value if error.
 */
int dyn_hash_table_destroy_inplace(struct d_hash_table *htable, bool force);

/**
 * lookup \p key in the hash table, the found item is returned on
 * success.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] key		The key to search
 * \param[in] ksize		Size of the ke
 * \param[in] siphash   Previously generated SIP hash or 0 if unknown
 *
 * \return			found item
 */
dh_item_t dyn_hash_rec_find(struct d_hash_table *htable, const void *key,
			    unsigned int ksize, uint64_t siphash);

/**
 * Lookup \p key in the hash table, if there is a matched record, it should be
 * returned, otherwise the item will be inserted into the hash table. In the
 * later case, the returned is the is the input item.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] key		The key to be inserted
 * \param[in] ksize		Size of the key
 * \param[in] item		The item being inserted
 * \param[in] siphash   Previously generated SIP hash or 0 if unknown
 *
 * \return			matched record
 */
dh_item_t dyn_hash_rec_find_insert(struct d_hash_table *htable, const void *key,
				   unsigned int ksize, dh_item_t item,
				   uint64_t siphash);

/**
 * Insert a new key and its record into the hash table. The hash
 * table holds a refcount on the successfully inserted record, it releases the
 * refcount while deleting the record.
 *
 * If \p exclusive is true, it can succeed only if the key is unique, otherwise
 * this function returns error.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] key		The key to be inserted
 * \param[in] ksize		Size of the key
 * \param[in] item		The item being inserted
  * \param[in] exclusive		The key has to be unique if it is true.
 *
 * \return			0 on success, negative value on error
 */
int dyn_hash_rec_insert(struct d_hash_table *htable, const void *key,
			unsigned int ksize, dh_item_t item,  bool exclusive);

/**
 * Delete the record identified by \p key from the hash table.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] key		The key of the record being deleted
 * \param[in] ksize		Size of the key
 * \param[in] siphash		Previously generated SIP hash or 0 if unknown
 *
 * \retval			true	Item with \p key has been deleted
 * \retval			false	Can't find the record by \p key
 */
bool dyn_hash_rec_delete(struct d_hash_table *htable, const void *key,
			 unsigned int ksize, uint64_t siphash);

/**
 * Delete the record.
 * This record will be freed if hop_rec_free() is defined and the hash table
 * holds the last refcount.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] item		The link chain of the record
 *
 * \retval			true	Successfully deleted the record
 * \retval			false	The record has already been unlinked
 *					from the hash table
 */
bool dyn_hash_rec_delete_at(struct d_hash_table *htable, dh_item_t item);

/**
 * Evict the record identified by \p key from the hash table.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] key		The key of the record being evicted
 * \param[in] ksize		Size of the key
 *
 * \retval			true	Item with \p key has been evicted
 * \retval			false	Can't find the record by \p key
 */
bool dyn_hash_rec_evict(struct d_hash_table *htable, const void *key,
			unsigned int ksize);

/**
 * Evict the record.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] item		The record
 * \param[in] siphash   Previously generated SIP hash or 0 if unknown
 *
 * \retval			true	Item has been evicted
 * \retval			false	Not LRU feature
 */
bool dyn_hash_rec_evict_at(struct d_hash_table *htable, dh_item_t item,
			   uint64_t siphash);

/**
 * Increase the refcount of the record.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] item		The record
 */
void dyn_hash_rec_addref(struct d_hash_table *htable, dh_item_t item);

/**
 * Decrease the refcount of the record.
 * The record will be freed if hop_decref() returns true and the EPHEMERAL bit
 * is set.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] item		The record
 */
void dyn_hash_rec_decref(struct d_hash_table *htable, dh_item_t item);

/**
 * Decrease the refcount of the record by count.
 * The record will be freed if hop_decref() returns true.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] count		Number of references to drop
 * \param[in] item		The hash record
 *
 * \retval			0		Success
 * \retval			-DER_INVAL	Not enough references were held.
 */
int dyn_hash_rec_ndecref(struct d_hash_table *htable, int count,
			 dh_item_t item);


/**
 * Return the first entry in a hash table.
 *
 * Note this does not take a reference on the returned entry and has no ordering
 * semantics.  It's main use is for draining a hash table before calling
 * destroy()
 *
 * \param[in] htable		Pointer to the hash table
 *
 * \retval			item	Pointer to first element in hash table
 * \retval			NULL	Hash table is empty or error occurred
 */
dh_item_t dyn_hash_rec_first(struct d_hash_table *htable);

/**
 * If debugging is enabled, prints stats about the hash table
 *
 * \param[in] htable		Pointer to the hash table
 */
void dyn_hash_table_debug(struct d_hash_table *htable);

#if defined(__cplusplus)
}
#endif

/** @}
 */
#endif /*__GURT_DYNHASH_H__ */
