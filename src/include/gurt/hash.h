/* Copyright (C) 2016-2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * \file
 *
 * Generic Hash Table APIs & data structures
 */

#ifndef __GURT_HASH_H__
#define __GURT_HASH_H__

#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>

#include <gurt/list.h>
#include <gurt/types.h> /* for d_uuid */

/**
 * Hash table keeps and prints extra debugging information
 */
#define D_HASH_DEBUG	0

struct d_hash_table;

#if defined(__cplusplus)
extern "C" {
#endif

/** @addtogroup GURT
 * @{
 */
/******************************************************************************
 * Generic Hash Table APIs / data structures
 ******************************************************************************/

typedef struct {
	/**
	 * Compare \p key with the key of the record \p rlink
	 * This member function is mandatory.
	 *
	 * \param[in] htable	hash table
	 * \param[in] rlink	The link chain of the record
	 * \param[in] key	Key to compare
	 * \param[in] ksize	Size of the key
	 *
	 * \retval	true	The key of the record equals to \p key.
	 * \retval	false	No match
	 */
	bool	 (*hop_key_cmp)(struct d_hash_table *htable, d_list_t *rlink,
				const void *key, unsigned int ksize);
	/**
	 * Optional, generate a key for the record \p rlink.
	 *
	 * This function is called before inserting a record w/o key into a
	 * hash table.
	 *
	 * \param[in] htable	hash table
	 * \param[in] rlink	The link chain of the record to generate key.
	 * \param[in] arg	Input arguments for the key generating.
	 */
	void	 (*hop_key_init)(struct d_hash_table *htable,
				 d_list_t *rlink, void *arg);
	/**
	 * Optional, return the key of record \p rlink to \p key_pp, and size of
	 * the key as the returned value.
	 *
	 * \param[in] htable	hash table
	 * \param[in] rlink	The link chain of the record being queried.
	 * \param[out] key_pp	The returned key.
	 *
	 * \return		size of the key.
	 */
	int	 (*hop_key_get)(struct d_hash_table *htable, d_list_t *rlink,
				void **key_pp);
	/**
	 * Optional, hash \p key to a 32-bit value.
	 * DJB2 hash is used when this function is abscent.
	 *
	 * \param[in] htable	hash table
	 * \param[in] key	Key to hash
	 * \param[in] ksize	Key size
	 *
	 * \return		hash of the key
	 */
	uint32_t (*hop_key_hash)(struct d_hash_table *htable, const void *key,
				 unsigned int ksize);
	/**
	 * Optional, increase refcount on the record \p rlink
	 * If this function is provided, it will be called for successfully
	 * inserted record.
	 *
	 * \param[in] htable	hash table
	 * \param[in] rlink	The record being referenced.
	 */
	void	 (*hop_rec_addref)(struct d_hash_table *htable,
				   d_list_t *rlink);
	/**
	 * Optional, release refcount on the record \p rlink
	 *
	 * If this function is provided, it is called while deleting a record
	 * from the hash table.
	 *
	 * If hop_rec_free() is provided, this function can return true when
	 * the refcount reaches zero, in this case, hop_free() will be called.
	 * If the record should not be automatically freed by the hash table
	 * despite of refcount, then this function should never return true.
	 *
	 * \param[in] htable	hash table
	 * \param[in] rlink	The rlink being released.
	 *
	 * \retval	false	Do nothing
	 * \retval	true	Only if refcount is zero and the hash item
	 *			can be freed. If this function can return
	 *			true, then hop_rec_free() should be defined.
	 */
	bool	 (*hop_rec_decref)(struct d_hash_table *htable,
				   d_list_t *rlink);
	/**
	 * Optional, free the record \p rlink
	 * It is called if hop_decref() returns zero.
	 *
	 * \param[in] htable	hash table
	 * \param[in] rlink	The record being freed.
	 */
	void	 (*hop_rec_free)(struct d_hash_table *htable, d_list_t *rlink);
} d_hash_table_ops_t;

enum d_hash_feats {
	/**
	 * By default, the hash table is protected by pthread_mutex.
	 */

	/**
	 * The hash table has no lock, it means the hash table is protected
	 * by external lock, or only accessed by a single thread.
	 */
	D_HASH_FT_NOLOCK		= (1 << 0),

	/**
	 * It is a read-mostly hash table, so it is protected by RW lock.
	 *
	 * Note: If caller sets this flag and also provides hop_addref/decref,
	 * then he should guarantee refcount changes are atomic or protected
	 * within hop_addref/decref, because RW lock can't protect refcount.
	 */
	D_HASH_FT_RWLOCK		= (1 << 1),

	/**
	 * If the EPHEMERAL bit is zero:
	 * - The hash table will take and release references using the
	 *   user-provided hop_rec_addref and hop_rec_decref functions as
	 *   entries are added to and deleted from the hash table.
	 * - Decrementing the last reference on an item without previously
	 *   deleting it will cause an ASSERT - it will not be free'd
	 *
	 * If the EPHEMERAL bit is set:
	 * - The hash table will not call automatically call the addref or
	 *   decref functions when entries are added/removed
	 * - When decref is called and the reference count reaches zero, the
	 *   record will be deleted automatically from the table and free'd
	 *
	 * Note that if addref/decref are not provided this bit has no effect
	 */
	D_HASH_FT_EPHEMERAL		= (1 << 2),
};

struct d_hash_bucket {
	d_list_t		hb_head;
#if D_HASH_DEBUG
	unsigned int		hb_dep;
#endif
};

struct d_hash_table {
	/** different type of locks based on ht_feats */
	union {
		pthread_mutex_t		ht_lock;
		pthread_rwlock_t	ht_rwlock;
	};
	/** bits to generate number of buckets */
	unsigned int		 ht_bits;
	/** feature bits */
	unsigned int		 ht_feats;
#if D_HASH_DEBUG
	/** maximum search depth ever */
	unsigned int		 ht_dep_max;
	/** maximum number of hash records */
	unsigned int		 ht_nr_max;
	/** total number of hash records */
	unsigned int		 ht_nr;
#endif
	/** private data to pass into customized functions */
	void			*ht_priv;
	/** customized member functions */
	d_hash_table_ops_t	*ht_ops;
	/** array of buckets */
	struct d_hash_bucket	*ht_buckets;
};


/**
 * Create a new hash table.
 *
 * \note Please be careful while using rwlock and refcount at the same time,
 * see \ref d_hash_feats for the details.
 *
 * \param[in] feats		Feature bits, see D_HASH_FT_*
 * \param[in] bits		power2(bits) is the size of hash table
 * \param[in] priv		Private data for the hash table
 * \param[in] hops		Customized member functions
 * \param[out] htable_pp	The newly created hash table
 *
 * \return			0 on success, negative value on error
 */
int  d_hash_table_create(uint32_t feats, unsigned int bits,
			  void *priv, d_hash_table_ops_t *hops,
			  struct d_hash_table **htable_pp);

/**
 * Initialise an inplace hash table.
 *
 * Does not allocate the htable pointer itself
 *
 * \note Please be careful while using rwlock and refcount at the same time,
 * see \ref d_hash_feats for the details.
 *
 * \param[in] feats		Feature bits, see D_HASH_FT_*
 * \param[in] bits		power2(bits) is the size of hash table
 * \param[in] priv		Private data for the hash table
 * \param[in] hops		Customized member functions
 * \param[in] htable		Hash table to be initialised
 *
 * \return			0 on success, negative value on error
 */
int  d_hash_table_create_inplace(uint32_t feats, unsigned int bits,
				 void *priv, d_hash_table_ops_t *hops,
				 struct d_hash_table *htable);

typedef int (*d_hash_traverse_cb_t)(d_list_t *rlink, void *arg);

/**
 * Traverse a hash table, call the traverse callback function on every item.
 * Break once the callback returns non-zero.
 *
 * \param[in] htable		The hash table to be finalised.
 * \param[in] cb		Traverse callback, will be called on every item
 *				in the hash table.
 *				See \a d_hash_traverse_cb_t.
 * \param[in] arg			Arguments for the callback.
 *
 * \return			zero on success, negative value if error.
 */
int d_hash_table_traverse(struct d_hash_table *htable,
			  d_hash_traverse_cb_t cb, void *arg);

/**
 * Destroy a hash table.
 *
 * \param[in] htable		The hash table to be destroyed.
 * \param[in] force		true:
 *				Destroy the hash table even it is not empty,
 *				all pending items will be deleted.
 *				false:
 *				Destroy the hash table only if it is empty,
 *				otherwise returns error
 *
 * \return			zero on success, negative value if error.
 */
int  d_hash_table_destroy(struct d_hash_table *htable, bool force);

/**
 * Finalise a hash table, reset all struct members.
 *
 * Note this does NOT free htable itself - only the members it contains.
 *
 * \param[in] htable		The hash table to be finalised.
 * \param[in] force		true:
 *				Finalise the hash table even it is not empty,
 *				all pending items will be deleted.
 *				false:
 *				Finalise the hash table only if it is empty,
 *				otherwise returns error
 *
 * \return			zero on success, negative value if error.
 */
int  d_hash_table_destroy_inplace(struct d_hash_table *htable, bool force);

/**
 * lookup \p key in the hash table, the found chain rlink is returned on
 * success.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] key		The key to search
 * \param[in] ksize		Size of the key
 *
 * \return			found chain rlink
 */
d_list_t *d_hash_rec_find(struct d_hash_table *htable, const void *key,
			  unsigned int ksize);

/**
 * Lookup \p key in the hash table, if there is a matched record, it should be
 * returned, otherwise \p rlink will be inserted into the hash table. In the
 * later case, the returned link chain is the input \p rlink.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] key		The key to be inserted
 * \param[in] ksize		Size of the key
 * \param[in] rlink		The link chain of the record being inserted
 *
 * \return			matched record
 */
d_list_t *d_hash_rec_find_insert(struct d_hash_table *htable,
				 const void *key, unsigned int ksize,
				 d_list_t *rlink);

/**
 * Insert a new key and its record chain \p rlink into the hash table. The hash
 * table holds a refcount on the successfully inserted record, it releases the
 * refcount while deleting the record.
 *
 * If \p exclusive is true, it can succeed only if the key is unique, otherwise
 * this function returns error.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] key		The key to be inserted
 * \param[in] ksize		Size of the key
 * \param[in] rlink		The link chain of the record being inserted
 * \param[in] exclusive		The key has to be unique if it is true.
 *
 * \return			0 on success, negative value on error
 */
int  d_hash_rec_insert(struct d_hash_table *htable, const void *key,
		       unsigned int ksize, d_list_t *rlink,
		       bool exclusive);

/**
 * Insert an anonymous record (w/o key) into the hash table.
 * This function calls hop_key_init() to generate a key for the new rlink
 * under the protection of the hash table lock.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] rlink		The link chain of the hash record
 * \param[in] arg		Arguments for key generating
 *
 * \return			0 on success, negative value on error
 */
int  d_hash_rec_insert_anonym(struct d_hash_table *htable, d_list_t *rlink,
			       void *arg);

/**
 * Delete the record identified by \p key from the hash table.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] key		The key of the record being deleted
 * \param[in] ksize		Size of the key
 *
 * \retval			true	Item with \p key has been deleted
 * \retval			false	Can't find the record by \p key
 */
bool d_hash_rec_delete(struct d_hash_table *htable, const void *key,
		       unsigned int ksize);

/**
 * Delete the record linked by the chain \p rlink.
 * This record will be freed if hop_rec_free() is defined and the hash table
 * holds the last refcount.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] rlink		The link chain of the record
 *
 * \retval			true	Successfully deleted the record
 * \retval			false	The record has already been unlinked
 *					from the hash table
 */
bool d_hash_rec_delete_at(struct d_hash_table *htable, d_list_t *rlink);

/**
 * Increase the refcount of the record.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] rlink		The link chain of the record
 */
void d_hash_rec_addref(struct d_hash_table *htable, d_list_t *rlink);

/**
 * Decrease the refcount of the record.
 * The record will be freed if hop_decref() returns true and the EPHEMERAL bit
 * is set.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] rlink		Chain rlink of the hash record
 */
void d_hash_rec_decref(struct d_hash_table *htable, d_list_t *rlink);

/**
 * Decrease the refcount of the record by count.
 * The record will be freed if hop_decref() returns true.
 *
 * \param[in] htable		Pointer to the hash table
 * \param[in] count		Number of references to drop
 * \param[in] rlink		Chain rlink of the hash record
 *
 * \retval			0		Success
 * \retval			-DER_INVAL	Not enough references were held.
 */
int d_hash_rec_ndecref(struct d_hash_table *htable, int count,
		       d_list_t *rlink);

/**
 * Check if the link chain has already been unlinked from the hash table.
 *
 * \param[in] rlink		The link chain of the record
 *
 * \retval			true	Yes
 * \retval			false	No
 */
bool d_hash_rec_unlinked(d_list_t *rlink);

/**
 * Return the first entry in a hash table.  Do this by traversing the table, and
 * returning the first rlink value provided to the callback.
 * Returns rlink on success, or NULL on error or if the hash table is empty.
 *
 * Note this does not take a reference on the returned entry and has no ordering
 * semantics.  It's main use is for draining a hash table before calling
 * destroy()
 *
 * \param[in] htable		Pointer to the hash table
 *
 * \retval			rlink	Pointer to first element in hash table
 * \retval			NULL	Hash table is empty or error occurred
 */
d_list_t *d_hash_rec_first(struct d_hash_table *htable);

/**
 * If debugging is enabled, prints stats about the hash table
 *
 * \param[in] htable		Pointer to the hash table
 */
void d_hash_table_debug(struct d_hash_table *htable);

/******************************************************************************
 * DAOS Handle Hash Table Wrapper
 *
 * Note: These functions are not thread-safe because reference counting
 * operations are not internally lock-protected. The user must add their own
 * locking.
 *
 ******************************************************************************/

#define D_HHASH_BITS		16
#define D_HTYPE_BITS		4
#define D_HTYPE_MASK		((1ULL << D_HTYPE_BITS) - 1)

/**
 * The handle type, uses the least significant 4-bits in the 64-bits hhash key.
 * The bit 0 is only used for D_HYTPE_PTR (pointer type), all other types MUST
 * set bit 0 to 1.
 */
enum {
	D_HTYPE_PTR		= 0, /**< pointer type handle */
	D_HTYPE_EQ		= 1, /**< event queue */
	D_HTYPE_POOL		= 3, /**< pool */
	D_HTYPE_CO		= 5, /**< container */
	D_HTYPE_OBJ		= 7, /**< object */
	D_HTYPE_ARRAY		= 9, /**< object */
	D_HTYPE_TX		= 11, /**< object */
	/* Must enlarge D_HTYPE_BITS to add more types */
};

struct d_hlink;
struct d_hlink_ops {
	/** free callback */
	void	(*hop_free)(struct d_hlink *rlink);
};

struct d_rlink {
	d_list_t		rl_link;
	unsigned int		rl_ref;
	unsigned int		rl_initialized:1;
};

struct d_hlink {
	struct d_rlink		 hl_link;
	uint64_t		 hl_key;
	struct d_hlink_ops	*hl_ops;
};

struct d_hhash;

int  d_hhash_create(unsigned int bits, struct d_hhash **hhash);
void d_hhash_destroy(struct d_hhash *hh);
void d_hhash_hlink_init(struct d_hlink *hlink, struct d_hlink_ops *ops);
/**
 * Insert to handle hash table.
 * If \a type is D_HTYPE_PTR, user MUST ensure the bit 0 of \a hlink pointer is
 * zero. Assuming zero value of bit 0 of the pointer is reasonable portable. It
 * is with undefined result if bit 0 of \a hlink pointer is 1 for D_HTYPE_PTR
 * type.
 */
void d_hhash_link_insert(struct d_hhash *hhash, struct d_hlink *hlink,
		         int type);
struct d_hlink *d_hhash_link_lookup(struct d_hhash *hhash, uint64_t key);
void d_hhash_link_getref(struct d_hhash *hhash, struct d_hlink *hlink);
void d_hhash_link_putref(struct d_hhash *hhash, struct d_hlink *hlink);
bool d_hhash_link_delete(struct d_hhash *hhash, struct d_hlink *hlink);
bool d_hhash_link_empty(struct d_hlink *hlink);
void d_hhash_link_key(struct d_hlink *hlink, uint64_t *key);
int  d_hhash_key_type(uint64_t key);
bool d_hhash_key_isptr(uint64_t key);
int  d_hhash_set_ptrtype(struct d_hhash *hhtab);
bool d_hhash_is_ptrtype(struct d_hhash *hhtab);

/******************************************************************************
 * UUID Hash Table Wrapper
 * Key: UUID
 * Value: generic pointer
 *
 * Note: These functions are not thread-safe because reference counting
 * operations are not internally lock-protected. The user must add their own
 * locking.
 *
 ******************************************************************************/

struct d_ulink;
struct d_ulink_ops {
	/** free callback */
	void	(*uop_free)(struct d_ulink *ulink);
	/** optional compare callback -- for any supplement comparison */
	bool	(*uop_cmp)(struct d_ulink *ulink, void *cmp_args);
};

struct d_ulink {
	struct d_rlink		 ul_link;
	struct d_uuid		 ul_uuid;
	/** optional agrument for compare callback */
	void			*ul_cmp_args;
	struct d_ulink_ops	*ul_ops;
};

int d_uhash_create(int feats, unsigned int bits, struct d_hash_table **uhtab);
void d_uhash_destroy(struct d_hash_table *uhtab);
void d_uhash_ulink_init(struct d_ulink *ulink, struct d_ulink_ops *rl_ops);
bool d_uhash_link_empty(struct d_ulink *ulink);
bool d_uhash_link_last_ref(struct d_ulink *ulink);
void d_uhash_link_addref(struct d_hash_table *uhtab, struct d_ulink *hlink);
void d_uhash_link_putref(struct d_hash_table *uhtab, struct d_ulink *hlink);
void d_uhash_link_delete(struct d_hash_table *uhtab, struct d_ulink *hlink);
int  d_uhash_link_insert(struct d_hash_table *uhtab, struct d_uuid *key,
			 void *cmp_args, struct d_ulink *hlink);
struct d_ulink *d_uhash_link_lookup(struct d_hash_table *uhtab,
				    struct d_uuid *key, void *cmp_args);

#if defined(__cplusplus)
}
#endif

/** @}
 */
#endif /*__GURT_HASH_H__*/
