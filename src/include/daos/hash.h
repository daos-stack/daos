/**
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * GPL HEADER END
 */
#ifndef __DAOS_HASH_H__
#define __DAOS_HASH_H__

#include <daos/list.h>

#define DHASH_DEBUG	0

struct dhash_table;

typedef struct {
	/**
	 * Optional, generate a key for the record @rlink.
	 *
	 * This function is called before inserting a record w/o key into a
	 * hash table.
	 *
	 * \param rlink	[IN]	The link chain of the record to generate key.
	 * \param args	[IN]	Input arguments for the key generating.
	 */
	void	 (*hop_key_init)(struct dhash_table *htable,
				 daos_list_t *rlink, void *args);
	/**
	 * Optional, return the key of record @rlink to @key_pp, and size of
	 * the key as the returned value.
	 *
	 * \param rlink	[IN]	The link chain of the record being queried.
	 * \param key_pp [OUT]	The returned key.
	 *
	 * \return		size of the key.
	 */
	int	 (*hop_key_get)(struct dhash_table *htable, daos_list_t *rlink,
				void **key_pp);
	/**
	 * Optional, hash @key to a 32-bit value.
	 * DJB2 hash is used when this function is abscent.
	 */
	uint32_t (*hop_key_hash)(struct dhash_table *htable, const void *key,
				 unsigned int ksize);
	/**
	 * Compare @key with the key of the record @rlink
	 * This member function is mandatory.
	 *
	 * \return	true	The key of the record equals to @key.
	 *		false	Not match
	 */
	bool	 (*hop_key_cmp)(struct dhash_table *htable, daos_list_t *rlink,
				const void *key, unsigned int ksize);
	/**
	 * Optional, increase refcount on the record @rlink
	 * If this function is provided, it will be called for successfully
	 * inserted record.
	 *
	 * \param rlink	[IN]	The record being referenced.
	 */
	void	 (*hop_rec_addref)(struct dhash_table *htable,
				    daos_list_t *rlink);
	/**
	 * Optional, release refcount on the record @rlink
	 *
	 * If this function is provided, it is called while deleting an record
	 * from the hash table.
	 *
	 * If hop_free() is also provided, this function can return true when
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
	bool	 (*hop_rec_decref)(struct dhash_table *htable,
				    daos_list_t *rlink);
	/**
	 * Optional, free the record @rlink
	 * It is called if hop_decref() returns zero.
	 *
	 * \param rlink	[IN]	The record being freed.
	 */
	void	 (*hop_rec_free)(struct dhash_table *htable,
				  daos_list_t *rlink);
} dhash_table_ops_t;

enum dhash_feats {
	/**
	 * By default, the hash table is protected by pthread_mutex.
	 */

	/**
	 * The hash table has no lock, it means the hash table is protected
	 * by external lock, or only accessed by a single thread.
	 */
	DHASH_FT_NOLOCK		= (1 << 0),
	/**
	 * It is a read-mostly hash table, so it is protected by RW lock.
	 *
	 * NB: If caller sets this flag and also provides hop_addref/decref,
	 * then he should guarantee refcount changes are atomic or protected
	 * within hop_addref/decref, because RW lock can't protect refcount.
	 */
	DHASH_FT_RWLOCK		= (1 << 1),
};

int  dhash_table_create(uint32_t feats, unsigned int bits,
			void *priv, dhash_table_ops_t *hops,
			struct dhash_table **htable_pp);
int  dhash_table_create_inplace(uint32_t feats, unsigned int bits,
				void *priv, dhash_table_ops_t *hops,
				struct dhash_table *htable);
int  dhash_table_destroy(struct dhash_table *htable, bool force);
int  dhash_table_destroy_inplace(struct dhash_table *htable, bool force);
void dhash_table_debug(struct dhash_table *htable);

daos_list_t *dhash_rec_find(struct dhash_table *htable, const void *key,
			    unsigned int ksize);
int  dhash_rec_insert(struct dhash_table *htable, const void *key,
		     unsigned int ksize, daos_list_t *rlink,
		     bool exclusive);
int  dhash_rec_insert_anonym(struct dhash_table *htable, daos_list_t *rlink,
			      void *args);
bool dhash_rec_delete(struct dhash_table *htable, const void *key,
		      unsigned int ksize);
bool dhash_rec_delete_at(struct dhash_table *htable, daos_list_t *rlink);
void dhash_rec_addref(struct dhash_table *htable, daos_list_t *rlink);
void dhash_rec_decref(struct dhash_table *htable, daos_list_t *rlink);
bool dhash_rec_unlinked(daos_list_t *rlink);

#define DAOS_HHASH_BITS		16
#define DAOS_HTYPE_BITS		3
#define DAOS_HTYPE_MASK		((1ULL << DAOS_HTYPE_BITS) - 1)

enum {
	DAOS_HTYPE_EQ		= 0,
	DAOS_HTYPE_VOS_POOL	= 1,
	DAOS_HTYPE_VOS_CO	= 2,
	/* More to be added */
};

struct daos_hlink;

struct daos_hlink_ops {
	/** free callback */
	void	(*hop_free)(struct daos_hlink *hlink);
};

struct daos_hlink {
	daos_list_t		hl_link;
	uint64_t		hl_key;
	unsigned int		hl_ref;
	unsigned int		hl_initialized:1;
	struct daos_hlink_ops	*hl_ops;
};

struct daos_hhash {
	pthread_mutex_t         dh_lock;
	unsigned int		dh_lock_init:1;
	unsigned int            dh_bits;
	unsigned int		dh_pid;
	uint64_t                dh_cookie;
	daos_list_t              *dh_hash;
};

int daos_hhash_create(unsigned int bits, struct daos_hhash **hhash);
void daos_hhash_destroy(struct daos_hhash *hh);
void daos_hhash_hlink_init(struct daos_hlink *hlink,
			   struct daos_hlink_ops *ops);
void daos_hhash_link_insert(struct daos_hhash *hhash,
			    struct daos_hlink *hlink, int type);
int daos_hhash_link_insert_key(struct daos_hhash *hhash,
			       uint64_t key, struct daos_hlink *hlink);
struct daos_hlink *daos_hhash_link_lookup(struct daos_hhash *hhash,
					  uint64_t key);
void daos_hhash_link_putref_locked(struct daos_hlink *hlink);
void daos_hhash_link_putref(struct daos_hhash *hhash,
			    struct daos_hlink *hlink);
int daos_hhash_link_delete(struct daos_hhash *hhash,
			   struct daos_hlink *hlink);
int daos_hhash_link_empty(struct daos_hlink *hlink);
void daos_hhash_link_key(struct daos_hlink *hlink, uint64_t *key);

#endif /*__DAOS_HASH_H__*/
