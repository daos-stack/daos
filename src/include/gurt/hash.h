/* Copyright (C) 2016-2018 Intel Corporation
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

#ifndef __GURT_HASH_H__
#define __GURT_HASH_H__

#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>

#include <gurt/list.h>
#include <gurt/common.h> /* for d_uuid */

/**
 * Hash table keeps and prints extra debugging information
 */
#define D_HASH_DEBUG	0

struct d_hash_table;

#if defined(__cplusplus)
extern "C" {
#endif

/******************************************************************************
 * Generic Hash Table APIs / data structures
 ******************************************************************************/

typedef struct {
	/**
	 * Compare @key with the key of the record @rlink
	 * This member function is mandatory.
	 *
	 * \return	true	The key of the record equals to @key.
	 *		false	Not match
	 */
	bool	 (*hop_key_cmp)(struct d_hash_table *htable, d_list_t *rlink,
				const void *key, unsigned int ksize);
	/**
	 * Optional, generate a key for the record @rlink.
	 *
	 * This function is called before inserting a record w/o key into a
	 * hash table.
	 *
	 * \param rlink	[IN]	The link chain of the record to generate key.
	 * \param arg	[IN]	Input arguments for the key generating.
	 */
	void	 (*hop_key_init)(struct d_hash_table *htable,
				 d_list_t *rlink, void *arg);
	/**
	 * Optional, return the key of record @rlink to @key_pp, and size of
	 * the key as the returned value.
	 *
	 * \param rlink  [IN]	The link chain of the record being queried.
	 * \param key_pp [OUT]	The returned key.
	 *
	 * \return		size of the key.
	 */
	int	 (*hop_key_get)(struct d_hash_table *htable, d_list_t *rlink,
				void **key_pp);
	/**
	 * Optional, hash @key to a 32-bit value.
	 * DJB2 hash is used when this function is abscent.
	 */
	uint32_t (*hop_key_hash)(struct d_hash_table *htable, const void *key,
				 unsigned int ksize);
	/**
	 * Optional, increase refcount on the record @rlink
	 * If this function is provided, it will be called for successfully
	 * inserted record.
	 *
	 * \param rlink	[IN]	The record being referenced.
	 */
	void	 (*hop_rec_addref)(struct d_hash_table *htable,
				   d_list_t *rlink);
	/**
	 * Optional, release refcount on the record @rlink
	 *
	 * If this function is provided, it is called while deleting a record
	 * from the hash table.
	 *
	 * If hop_rec_free() is provided, this function can return true when
	 * the refcount reaches zero, in this case, hop_free() will be called.
	 * If the record should not be automatically freed by the hash table
	 * despite of refcount, then this function should never return true.
	 *
	 * \param rlink	[IN]	The rlink being released.
	 *
	 * \return	False	Do nothing
	 *		True	Only if refcount is zero and the hash item
	 *			can be freed. If this function can return
	 *			true, then hop_rec_free() should be defined.
	 */
	bool	 (*hop_rec_decref)(struct d_hash_table *htable,
				   d_list_t *rlink);
	/**
	 * Optional, free the record @rlink
	 * It is called if hop_decref() returns zero.
	 *
	 * \param rlink	[IN]	The record being freed.
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
 * Note: Please be careful while using rwlock and refcount at the same time,
 * see d_hash_feats for the details.
 *
 * \param feats		[IN]	Feature bits, see D_HASH_FT_*
 * \param bits		[IN]	power2(bits) is the size of hash table
 * \param priv		[IN]	Private data for the hash table
 * \param hops		[IN]	Customized member functions
 * \param htable_pp	[OUT]	The newly created hash table
 */
int  d_hash_table_create(uint32_t feats, unsigned int bits,
			  void *priv, d_hash_table_ops_t *hops,
			  struct d_hash_table **htable_pp);

/**
 * Initialise an inplace hash table.
 *
 * Does not allocate the htable pointer itself
 *
 * Note: Please be careful while using rwlock and refcount at the same time,
 * see d_hash_feats for the details.
 *
 * \param feats		[IN]	Feature bits, see D_HASH_FT_*
 * \param bits		[IN]	power2(bits) is the size of hash table
 * \param priv		[IN]	Private data for the hash table
 * \param hops		[IN]	Customized member functions
 * \param htable	[IN]	Hash table to be initialised
 */
int  d_hash_table_create_inplace(uint32_t feats, unsigned int bits,
				 void *priv, d_hash_table_ops_t *hops,
				 struct d_hash_table *htable);

typedef int (*d_hash_traverse_cb_t)(d_list_t *rlink, void *arg);

/**
 * Traverse a hash table, call the traverse callback function on every item.
 * Break once the callback returns non-zero.
 *
 * \param htable	[IN]	The hash table to be finalised.
 * \param cb		[IN]	Traverse callback, will be called on every item
 *				in the hash table.
 *				\see d_hash_traverse_cb_t.
 * \param arg		[IN]	Arguments for the callback.
 *
 * \return			zero on success, negative value if error.
 */
int d_hash_table_traverse(struct d_hash_table *htable,
			  d_hash_traverse_cb_t cb, void *arg);

/**
 * Destroy a hash table.
 *
 * \param htable	[IN]	The hash table to be destroyed.
 * \param force		[IN]	True:
 *				Destroy the hash table even it is not empty,
 *				all pending items will be deleted.
 *				False:
 *				Destroy the hash table only if it is empty,
 *				otherwise returns error
 */
int  d_hash_table_destroy(struct d_hash_table *htable, bool force);

/**
 * Finalise a hash table, reset all struct members.
 *
 * Note this does NOT free htable itself - only the members it contains.
 *
 * \param htable	[IN]	The hash table to be finalised.
 * \param force		[IN]	True:
 *				Finalise the hash table even it is not empty,
 *				all pending items will be deleted.
 *				False:
 *				Finalise the hash table only if it is empty,
 *				otherwise returns error
 */
int  d_hash_table_destroy_inplace(struct d_hash_table *htable, bool force);

/**
 * lookup @key in the hash table, the found chain rlink is returned on
 * success.
 *
 * \param htable	[IN]	Pointer to the hash table
 * \param key		[IN]	The key to search
 * \param ksize		[IN]	Size of the key
 */
d_list_t *d_hash_rec_find(struct d_hash_table *htable, const void *key,
			  unsigned int ksize);

/**
 * Lookup @key in the hash table, if there is a matched record, it should be
 * returned, otherwise @rlink will be inserted into the hash table. In the
 * later case, the returned link chain is the input @rlink.
 *
 * \param htable	[IN]	Pointer to the hash table
 * \param key		[IN]	The key to be inserted
 * \param ksize		[IN]	Size of the key
 * \param rlink		[IN]	The link chain of the record being inserted
 */
d_list_t *d_hash_rec_find_insert(struct d_hash_table *htable,
				 const void *key, unsigned int ksize,
				 d_list_t *rlink);

/**
 * Insert a new key and its record chain @rlink into the hash table. The hash
 * table holds a refcount on the successfully inserted record, it releases the
 * refcount while deleting the record.
 *
 * If @exclusive is true, it can succeed only if the key is unique, otherwise
 * this function returns error.
 *
 * \param htable	[IN]	Pointer to the hash table
 * \param key		[IN]	The key to be inserted
 * \param ksize		[IN]	Size of the key
 * \param rlink		[IN]	The link chain of the record being inserted
 * \param exclusive	[IN]	The key has to be unique if it is true.
 */
int  d_hash_rec_insert(struct d_hash_table *htable, const void *key,
		       unsigned int ksize, d_list_t *rlink,
		       bool exclusive);

/**
 * Insert an anonymous record (w/o key) into the hash table.
 * This function calls hop_key_init() to generate a key for the new rlink
 * under the protection of the hash table lock.
 *
 * \param htable	[IN]	Pointer to the hash table
 * \param rlink		[IN]	The link chain of the hash record
 * \param arg		[IN]	Arguments for key generating
 */
int  d_hash_rec_insert_anonym(struct d_hash_table *htable, d_list_t *rlink,
			       void *arg);

/**
 * Delete the record identified by @key from the hash table.
 *
 * \param htable	[IN]	Pointer to the hash table
 * \param key		[IN]	The key of the record being deleted
 * \param ksize		[IN]	Size of the key
 *
 * return		True	Item with @key has been deleted
 *			False	Can't find the record by @key
 */
bool d_hash_rec_delete(struct d_hash_table *htable, const void *key,
		       unsigned int ksize);

/**
 * Delete the record linked by the chain @rlink.
 * This record will be freed if hop_rec_free() is defined and the hash table
 * holds the last refcount.
 *
 * \param htable	[IN]	Pointer to the hash table
 * \param rlink		[IN]	The link chain of the record
 *
 * return		True	Successfully deleted the record
 *			False	The record has already been unlinked from the
 *				hash table
 */
bool d_hash_rec_delete_at(struct d_hash_table *htable, d_list_t *rlink);

/**
 * Increase the refcount of the record.
 *
 * \param htable	[IN]	Pointer to the hash table
 * \param rlink		[IN]	The link chain of the record
 */
void d_hash_rec_addref(struct d_hash_table *htable, d_list_t *rlink);

/**
 * Decrease the refcount of the record.
 * The record will be freed if hop_decref() returns true and the EPHEMERAL bit
 * is set.
 *
 * \param htable	[IN]	Pointer to the hash table
 * \param rlink		[IN]	Chain rlink of the hash record
 */
void d_hash_rec_decref(struct d_hash_table *htable, d_list_t *rlink);

/**
 * Decrease the refcount of the record by count.
 * The record will be freed if hop_decref() returns true.
 *
 * \param htable	[IN]	Pointer to the hash table
 * \param int		[IN]	Number of references to drop
 * \param rlink		[IN]	Chain rlink of the hash record
 *
 * \return		0	Success
 *			-DER_INVAL Not enough references were held.
 */
int d_hash_rec_ndecref(struct d_hash_table *htable, int count,
		       d_list_t *rlink);

/**
 * Check if the link chain has already been unlinked from the hash table.
 *
 * \return	True	Yes
 *		False	No
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
 * \param htable	[IN]	Pointer to the hash table
 *
 * \return		rlink	Pointer to first element in hash table
 *			NULL	Hash table is empty or error occurred
 */
d_list_t *d_hash_rec_first(struct d_hash_table *htable);

/**
 * If debugging is enabled, prints stats about the hash table
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
#define D_HTYPE_BITS		3
#define D_HTYPE_MASK		((1ULL << D_HTYPE_BITS) - 1)

/**
 * The handle type, uses the least significant 3-bits in the 64-bits hhash key.
 * The bit 0 is only used for D_HYTPE_PTR (pointer type), all other types MUST
 * with bit 0 set as 1.
 */
enum {
	D_HTYPE_PTR		= 0, /* pointer type handle */
	D_HTYPE_EQ		= 1, /* event queue */
	D_HTYPE_POOL		= 3, /* pool */
	D_HTYPE_CO		= 5, /* container */
	D_HTYPE_OBJ		= 7, /* object */
	/* Needs to enlarge D_HTYPE_BITS to add more types */
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
 * If /a type is D_HTYPE_PTR, user MUST ensure the bit 0 of /a hlink pointer is
 * zero. Assuming zero value of bit 0 of the pointer is reasonable portable. It
 * is with undefined result if bit 0 of /a hlink pointer is 1 for D_HTYPE_PTR
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
};

struct d_ulink {
	struct d_rlink		 ul_link;
	struct d_uuid		 ul_uuid;
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
			 struct d_ulink *hlink);
struct d_ulink *d_uhash_link_lookup(struct d_hash_table *uhtab,
				    struct d_uuid *key);

#if defined(__cplusplus)
}
#endif

#endif /*__GURT_HASH_H__*/
