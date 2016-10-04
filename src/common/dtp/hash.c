/**
 * (C) Copyright 2016 Intel Corporation.
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
/*
 * This file is part of DAOSM
 *
 * daos_event/hash.c
 *
 * Author: Liang Zhen  <liang.zhen@intel.com>
 */

#include <pthread.h>
#include <daos/common.h>
#include <daos/list.h>
#include <daos/hash.h>

uint64_t
daos_hash_mix64(uint64_t key)
{
	key = (~key) + (key << 21);
	key = key ^ (key >> 24);
	key = (key + (key << 3)) + (key << 8);
	key = key ^ (key >> 14);
	key = (key + (key << 2)) + (key << 4);
	key = key ^ (key >> 28);
	key = key + (key << 31);

	return key;
}

/** Robert Jenkins' 96 bit Mix Function */
uint32_t
daos_hash_mix96(uint32_t a, uint32_t b, uint32_t c)
{
	a = a - b;
	a = a - c;
	a = a ^ (c >> 13);
	b = b - c;
	b = b - a;
	b = b ^ (a << 8);
	c = c - a;
	c = c - b;
	c = c ^ (b >> 13);
	a = a - b;
	a = a - c;
	a = a ^ (c >> 12);
	b = b - c;
	b = b - a;
	b = b ^ (a << 16);
	c = c - a;
	c = c - b;
	c = c ^ (b >> 5);
	a = a - b;
	a = a - c;
	a = a ^ (c >> 3);
	b = b - c;
	b = b - a;
	b = b ^ (a << 10);
	c = c - a;
	c = c - b;
	c = c ^ (b >> 15);

	return c;
}

/** consistent hash search */
unsigned int
daos_chash_srch_u64(uint64_t *hashes, unsigned int nhashes, uint64_t value)
{
	int	high = nhashes - 1;
	int	low = 0;
	int     i;

	for (i = high / 2; high - low > 1; i = (low + high) / 2) {
		if (value >= hashes[i])
			low = i;
		else /* value < hashes[i] */
			high = i;
	}
	return value >= hashes[high] ? high : low;
}

/* The djb2 string hash function, hash a string to a uint32_t value */
uint32_t
daos_hash_string_u32(const char *string, unsigned int len)
{
	uint32_t result = 5381;
	const unsigned char *p;

	p = (const unsigned char *) string;

	for (; len > 0; len--) {
		result = (result << 5) + result + *p;
		++p;
	}
	return result;
}

/**
 * Murmur hash
 * see https://sites.google.com/site/murmurhash
 */
#define MUR_PRIME	0xc6a4a7935bd1e995
#define MUR_ROTATE	47

uint64_t
daos_hash_murmur64(const unsigned char *key, unsigned int key_len,
		   unsigned int seed)
{
	const uint64_t	*addr	= (const uint64_t *)key;
	int		 loop	= key_len >> 3; /* divided by sizeof uint64 */
	int		 rest	= key_len & ((1 << 3) - 1);
	int		 i;
	uint64_t	 mur;

	mur = seed ^ (key_len * MUR_PRIME);
	for (i = 0; i < loop; i++) {
		uint64_t k = addr[i];

		k *= MUR_PRIME;
		k ^= k >> MUR_ROTATE;
		k *= MUR_PRIME;

		mur ^= k;
		mur *= MUR_PRIME;
	}

	key = (const unsigned char *)&addr[i];

	switch (rest) {
	case 7:	mur ^= (uint64_t)key[6] << 48;
	case 6:	mur ^= (uint64_t)key[5] << 40;
	case 5:	mur ^= (uint64_t)key[4] << 32;
	case 4:	mur ^= (uint64_t)key[3] << 24;
	case 3:	mur ^= (uint64_t)key[2] << 16;
	case 2:	mur ^= (uint64_t)key[1] << 8;
	case 1:	mur ^= (uint64_t)key[0];
		mur *= MUR_PRIME;
	};

	mur ^= mur >> MUR_ROTATE;
	mur *= MUR_PRIME;
	mur ^= mur >> MUR_ROTATE;

	return mur;
}

/**
 * Hash tables.
 */

static void
dh_lock_init(struct dhash_table *htable)
{
	if (htable->ht_feats & DHASH_FT_NOLOCK)
		return;

	if (htable->ht_feats & DHASH_FT_RWLOCK)
		pthread_rwlock_init(&htable->ht_rwlock, NULL);
	else
		pthread_mutex_init(&htable->ht_lock, NULL);
}

static void
dh_lock_fini(struct dhash_table *htable)
{
	if (htable->ht_feats & DHASH_FT_NOLOCK)
		return;

	if (htable->ht_feats & DHASH_FT_RWLOCK)
		pthread_rwlock_destroy(&htable->ht_rwlock);
	else
		pthread_mutex_destroy(&htable->ht_lock);
}

/** lock the hash table */
static void
dh_lock(struct dhash_table *htable, bool read_only)
{
	/* NB: if hash table is using rwlock, it only takes read lock for
	 * reference-only operations and caller should protect refcount.
	 * see DHASH_FT_RWLOCK for the details.
	 */

	if (htable->ht_feats & DHASH_FT_NOLOCK)
		return;

	if (htable->ht_feats & DHASH_FT_RWLOCK) {
		if (read_only)
			pthread_rwlock_rdlock(&htable->ht_rwlock);
		else
			pthread_rwlock_wrlock(&htable->ht_rwlock);

	} else {
		pthread_mutex_lock(&htable->ht_lock);
	}
}

/** unlock the hash table */
static void
dh_unlock(struct dhash_table *htable, bool read_only)
{
	if (htable->ht_feats & DHASH_FT_NOLOCK)
		return;

	if (htable->ht_feats & DHASH_FT_RWLOCK)
		pthread_rwlock_unlock(&htable->ht_rwlock);
	else
		pthread_mutex_unlock(&htable->ht_lock);
}

/**
 * wrappers for member functions.
 */

/**
 * Convert key to hash bucket id.
 *
 * It calls DJB2 hash if no customized hash function is provided.
 */
static unsigned int
dh_key_hash(struct dhash_table *htable, const void *key, unsigned int ksize)
{
	unsigned int idx;

	if (htable->ht_ops->hop_key_hash)
		idx = htable->ht_ops->hop_key_hash(htable, key, ksize);
	else
		idx = daos_hash_string_u32((const char *)key, ksize);

	return idx & ((1U << htable->ht_bits) - 1);
}

static void
dh_key_init(struct dhash_table *htable, daos_list_t *rlink, void *args)
{
	D_ASSERT(htable->ht_ops->hop_key_init);
	htable->ht_ops->hop_key_init(htable, rlink, args);
}

static bool
dh_key_cmp(struct dhash_table *htable, daos_list_t *rlink,
	   const void *key, unsigned int ksize)
{
	D_ASSERT(htable->ht_ops->hop_key_cmp);
	return htable->ht_ops->hop_key_cmp(htable, rlink, key, ksize);
}

static unsigned int
dh_key_get(struct dhash_table *htable, daos_list_t *rlink, void **key_pp)
{
	D_ASSERT(htable->ht_ops->hop_key_get);
	return htable->ht_ops->hop_key_get(htable, rlink, key_pp);
}

static void
dh_rec_insert(struct dhash_table *htable, unsigned idx, daos_list_t *rlink)
{
	struct dhash_bucket *bucket = &htable->ht_buckets[idx];

	daos_list_add(rlink, &bucket->hb_head);
#if DHASH_DEBUG
	htable->ht_nr++;
	if (htable->ht_nr > htable->ht_nr_max)
		htable->ht_nr_max = htable->ht_nr;

	if (htable->ht_ops->hop_key_get) {
		bucket->hb_dep++;
		if (bucket->hb_dep > htable->ht_dep_max) {
			htable->ht_dep_max = bucket->hb_dep;
			D_DEBUG(DF_MISC, "Max depth %d/%d/%d\n",
				htable->ht_dep_max, htable->ht_nr,
				htable->ht_nr_max);
		}
	}
#endif
}

static void
dh_rec_delete(struct dhash_table *htable, daos_list_t *rlink)
{
	daos_list_del_init(rlink);
#if DHASH_DEBUG
	htable->ht_nr--;
	if (htable->ht_ops->hop_key_get) {
		struct dhash_bucket *bucket;
		void		    *key;
		unsigned int	     size;

		size = htable->ht_ops->hop_key_get(htable, rlink, &key);
		bucket = &htable->ht_buckets[dh_key_hash(htable, key, size)];
		bucket->hb_dep--;
	}
#endif
}

static daos_list_t *
dh_rec_find(struct dhash_table *htable, unsigned idx, const void *key,
	     unsigned int ksize)
{
	struct dhash_bucket *bucket = &htable->ht_buckets[idx];
	daos_list_t	    *rlink;

	daos_list_for_each(rlink, &bucket->hb_head) {
		if (dh_key_cmp(htable, rlink, key, ksize))
			return rlink;
	}
	return NULL;
}

static void
dh_rec_addref(struct dhash_table *htable, daos_list_t *rlink)
{
	if (htable->ht_ops->hop_rec_addref)
		htable->ht_ops->hop_rec_addref(htable, rlink);
}

static bool
dh_rec_decref(struct dhash_table *htable, daos_list_t *rlink)
{
	return htable->ht_ops->hop_rec_decref ?
	       htable->ht_ops->hop_rec_decref(htable, rlink) : false;
}

static void
dh_rec_free(struct dhash_table *htable, daos_list_t *rlink)
{
	if (htable->ht_ops->hop_rec_free)
		htable->ht_ops->hop_rec_free(htable, rlink);
}

/**
 * lookup @key in the hash table, the found chain rlink is returned on
 * success.
 *
 * \param htable	[IN]	Pointer to the hash table
 * \param key		[IN]	The key to search
 * \param ksize		[IN]	Size of the key
 */
daos_list_t *
dhash_rec_find(struct dhash_table *htable, const void *key, unsigned int ksize)
{
	daos_list_t	*rlink;
	int		 idx;

	D_ASSERT(key != NULL);

	idx = dh_key_hash(htable, key, ksize);
	dh_lock(htable, true);

	rlink = dh_rec_find(htable, idx, key, ksize);
	if (rlink != NULL)
		dh_rec_addref(htable, rlink);

	dh_unlock(htable, true);
	return rlink;
}

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
int
dhash_rec_insert(struct dhash_table *htable, const void *key,
		 unsigned int ksize, daos_list_t *rlink, bool exclusive)
{
	int	idx;
	int	rc = 0;

	D_ASSERT(key != NULL && ksize != 0);

	idx = dh_key_hash(htable, key, ksize);
	dh_lock(htable, false);

	if (exclusive && dh_rec_find(htable, idx, key, ksize))
		D_GOTO(out, rc = -DER_EXIST);

	dh_rec_addref(htable, rlink);
	dh_rec_insert(htable, idx, rlink);
 out:
	dh_unlock(htable, false);
	return 0;
}

/**
 * Insert an anonymous record (w/o key) into the hash table.
 * This function calls hop_key_init() to generate a key for the new rlink
 * under the protection of the hash table lock.
 *
 * \param htable	[IN]	Pointer to the hash table
 * \param rlink		[IN]	The link chain of the hash record
 * \param args		[IN]	Arguments for key generating
 */
int
dhash_rec_insert_anonym(struct dhash_table *htable, daos_list_t *rlink,
			void *args)
{
	void	*key;
	int	 idx;
	int	 ksize;

	if (htable->ht_ops->hop_key_init == NULL ||
	    htable->ht_ops->hop_key_get == NULL)
		return -DER_NO_PERM;

	dh_lock(htable, false);
	/* has no key, hash table should have provided key generator */
	dh_key_init(htable, rlink, args);

	ksize = dh_key_get(htable, rlink, &key);
	idx = dh_key_hash(htable, key, ksize);

	dh_rec_addref(htable, rlink);
	dh_rec_insert(htable, idx, rlink);

	dh_unlock(htable, false);
	return 0;
}

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
bool
dhash_rec_delete(struct dhash_table *htable, const void *key,
		 unsigned int ksize)
{
	daos_list_t	*rlink;
	int		 idx;
	bool		 deleted = false;
	bool		 zombie  = false;

	D_ASSERT(key != NULL);

	idx = dh_key_hash(htable, key, ksize);
	dh_lock(htable, false);

	rlink = dh_rec_find(htable, idx, key, ksize);
	if (rlink != NULL) {
		dh_rec_delete(htable, rlink);
		zombie = dh_rec_decref(htable, rlink);
		deleted = true;
	}

	dh_unlock(htable, false);
	if (zombie)
		dh_rec_free(htable, rlink);

	return deleted;
}

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
bool
dhash_rec_delete_at(struct dhash_table *htable, daos_list_t *rlink)
{
	bool	deleted = false;
	bool	zombie  = false;

	dh_lock(htable, false);

	if (!daos_list_empty(rlink)) {
		dh_rec_delete(htable, rlink);
		zombie = dh_rec_decref(htable, rlink);
		deleted = true;
	}
	dh_unlock(htable, false);

	if (zombie)
		dh_rec_free(htable, rlink);

	return deleted;
}

/**
 * Increase the refcount of the record.
 *
 * \param htable	[IN]	Pointer to the hash table
 * \param rlink		[IN]	The link chain of the record
 */
void
dhash_rec_addref(struct dhash_table *htable, daos_list_t *rlink)
{
	dh_lock(htable, true);
	dh_rec_addref(htable, rlink);
	dh_unlock(htable, true);
}

/**
 * Decrease the refcount of the record.
 * The record will be freed if hop_decref() returns true.
 *
 * \param htable	[IN]	Pointer to the hash table
 * \param rlink		[IN]	Chain rlink of the hash record
 */
void
dhash_rec_decref(struct dhash_table *htable, daos_list_t *rlink)
{
	bool zombie;

	dh_lock(htable, true);

	zombie = dh_rec_decref(htable, rlink);
	D_ASSERT(!zombie || daos_list_empty(rlink));

	dh_unlock(htable, true);
	if (zombie)
		dh_rec_free(htable, rlink);
}

/**
 * The link chain has already been unlinked from the hash table or not.
 *
 * \return	True	Yes
 *		False	No
 */
bool
dhash_rec_unlinked(daos_list_t *rlink)
{
	return daos_list_empty(rlink);
}

/**
 * Initialise an inplace hash table.
 *
 * NB: Please be careful while using rwlock and refcount at the same time,
 * see dhash_feats for the details.
 *
 * \param feats		[IN]	Feature bits, see DHASH_FT_*
 * \param bits		[IN]	power2(bits) is the size of hash table
 * \param priv		[IN]	Private data for the hash table
 * \param hops		[IN]	Customized member functions
 * \param htable	[IN]	Hash table to be initialised
 */
int
dhash_table_create_inplace(uint32_t feats, unsigned int bits, void *priv,
			   dhash_table_ops_t *hops, struct dhash_table *htable)
{
	struct dhash_bucket *buckets;
	int		     nr = (1 << bits);
	int		     i;

	D_ASSERT(hops != NULL);
	D_ASSERT(hops->hop_key_cmp != NULL);

	htable->ht_feats = feats;
	htable->ht_bits	 = bits;
	htable->ht_ops	 = hops;
	htable->ht_priv	 = priv;

	D_ALLOC(buckets, sizeof(*buckets) * nr);
	if (buckets == NULL)
		return -DER_NOMEM;

	for (i = 0; i < nr; i++)
		DAOS_INIT_LIST_HEAD(&buckets[i].hb_head);

	htable->ht_buckets = buckets;
	dh_lock_init(htable);

	return 0;
}

/**
 * Create a new hash table.
 *
 * NB: Please be careful while using rwlock and refcount at the same time,
 * see dhash_feats for the details.
 *
 * \param feats		[IN]	Feature bits, see DHASH_FT_*
 * \param bits		[IN]	power2(bits) is the size of hash table
 * \param priv		[IN]	Private data for the hash table
 * \param hops		[IN]	Customized member functions
 * \param htable_pp	[OUT]	The newly created hash table
 */
int
dhash_table_create(uint32_t feats, unsigned int bits, void *priv,
		   dhash_table_ops_t *hops, struct dhash_table **htable_pp)
{
	struct dhash_table *htable;
	int		    rc;

	D_ALLOC_PTR(htable);
	if (htable == NULL)
		return -DER_NOMEM;

	rc = dhash_table_create_inplace(feats, bits, priv, hops, htable);
	if (rc != 0)
		goto failed;

	*htable_pp = htable;
	return 0;
 failed:
	D_FREE_PTR(htable);
	return rc;
}

/**
 * Traverse a hash table, call the traverse callback function on every item.
 * Break once the callback returns non-zero.
 *
 * \param htable	[IN]	The hash table to be finalised.
 * \param cb		[IN]	Traverse callback, will be called on every item
 *				in the hash table.
 *				\see dhash_traverse_cb_t.
 * \param args		[IN]	Arguments for the callback.
 *
 * \return			zero on success, negative value if error.
 */
int
dhash_table_traverse(struct dhash_table *htable, dhash_traverse_cb_t cb,
		     void *args)
{
	struct dhash_bucket *buckets = htable->ht_buckets;
	daos_list_t	    *rlink;

	int		     nr;
	int		     i;
	int		     rc = 0;

	if (buckets == NULL) {
		D_ERROR("dhash_table %p un-initialized (NULL buckets).\n",
			htable);
		D_GOTO(out, rc = -DER_UNINIT);
	}
	if (cb == NULL) {
		D_ERROR("invalid parameter, NULL cb.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	dh_lock(htable, true);

	nr = 1U << htable->ht_bits;
	for (i = 0; i < nr; i++) {
		daos_list_for_each(rlink, &buckets[i].hb_head) {
			rc = cb(rlink, args);
			if (rc != 0)
				D_GOTO(unlock, rc);
		}
	}

unlock:
	dh_unlock(htable, true);
out:
	return rc;
}

/**
 * Finalise a hash table, reset all struct members.
 *
 * \param htable	[IN]	The hash table to be finalised.
 * \param force		[IN]	True:
 *				Finalise the hash table even it is not empty,
 *				all pending items will be deleted.
 *				False:
 *				Finalise the hash table only if it is empty,
 *				otherwise returns error
 */
int
dhash_table_destroy_inplace(struct dhash_table *htable, bool force)
{
	struct dhash_bucket *buckets = htable->ht_buckets;
	int		     nr;
	int		     i;

	if (buckets == NULL)
		goto out;

	nr = 1U << htable->ht_bits;
	for (i = 0; i < nr; i++) {
		while (!daos_list_empty(&buckets[i].hb_head)) {
			if (!force) {
				D_DEBUG(DF_MISC, "Warning, non-empty hash\n");
				return -DER_BUSY;
			}
			dhash_rec_delete_at(htable, buckets[i].hb_head.next);
		}
	}
	D_FREE(buckets, sizeof(*buckets) * nr);
	dh_lock_fini(htable);
 out:
	memset(htable, 0, sizeof(*htable));
	return 0;
}

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
int
dhash_table_destroy(struct dhash_table *htable, bool force)
{
	dhash_table_destroy_inplace(htable, force);
	D_FREE_PTR(htable);
	return 0;
}

/**
 * Print stats of the hash table.
 */
void
dhash_table_debug(struct dhash_table *htable)
{
#if DHASH_DEBUG
	D_DEBUG(DF_MISC, "max nr: %d, cur nr: %d, max_dep: %d\n",
		htable->ht_nr_max, htable->ht_nr, htable->ht_dep_max);
#endif
}

/**
 * daos handle hash table: the first user of dhash_table
 */

struct daos_hhash {
	uint64_t                dh_cookie;
	struct dhash_table	dh_htable;
};

static struct daos_rlink*
link2rlink(daos_list_t *link)
{
	D_ASSERT(link != NULL);
	return container_of(link, struct daos_rlink, rl_link);
}

static void
rlink_op_addref(struct daos_rlink *rlink)
{
	rlink->rl_ref++;
}

static bool
rlink_op_decref(struct daos_rlink *rlink)
{
	D_ASSERT(rlink->rl_ref > 0);
	rlink->rl_ref--;

	return rlink->rl_ref == 0;
}

static void
rlink_op_init(struct daos_rlink *rlink)
{
	DAOS_INIT_LIST_HEAD(&rlink->rl_link);
	rlink->rl_initialized	= 1;
	rlink->rl_ref		= 1; /* for caller */
}


static bool
rlink_op_empty(struct daos_rlink *rlink)
{
	if (!rlink->rl_initialized)
		return 1;
	D_ASSERT(rlink->rl_ref != 0 || dhash_rec_unlinked(&rlink->rl_link));
	return dhash_rec_unlinked(&rlink->rl_link);
}

static struct daos_hlink *
hh_link2ptr(daos_list_t *link)
{
	struct daos_rlink	*rlink;

	rlink = link2rlink(link);
	return	container_of(rlink, struct daos_hlink, hl_link);
}

static void
hh_op_key_init(struct dhash_table *hhtab, daos_list_t *rlink, void *args)
{
	struct daos_hhash *dht;
	struct daos_hlink *hlink = hh_link2ptr(rlink);
	int		   type = *(int *)args;

	dht = container_of(hhtab, struct daos_hhash, dh_htable);
	hlink->hl_key = ((dht->dh_cookie++) << DAOS_HTYPE_BITS) | type;
}

static int
hh_key_type(const void *key)
{
	uint64_t	cookie;

	D_ASSERT(key != NULL);
	cookie = *(uint64_t *)key;

	return cookie & DAOS_HTYPE_MASK;
}

static int
hh_op_key_get(struct dhash_table *hhtab, daos_list_t *rlink, void **key_pp)
{
	struct daos_hlink *hlink = hh_link2ptr(rlink);

	*key_pp = (void *)&hlink->hl_key;
	return sizeof(hlink->hl_key);
}

static uint32_t
hh_op_key_hash(struct dhash_table *hhtab, const void *key, unsigned int ksize)
{
	D_ASSERT(ksize == sizeof(uint64_t));

	return (unsigned int)(*(const uint64_t *)key >> DAOS_HTYPE_BITS);
}

static bool
hh_op_key_cmp(struct dhash_table *hhtab, daos_list_t *link,
	  const void *key, unsigned int ksize)
{
	struct daos_hlink *hlink = hh_link2ptr(link);

	D_ASSERT(ksize == sizeof(uint64_t));
	return hlink->hl_key == *(uint64_t *)key;
}

static void
hh_op_rec_addref(struct dhash_table *hhtab, daos_list_t *link)
{
	rlink_op_addref(link2rlink(link));
}

static bool
hh_op_rec_decref(struct dhash_table *hhtab, daos_list_t *link)
{
	return rlink_op_decref(link2rlink(link));
}

static void
hh_op_rec_free(struct dhash_table *hhtab, daos_list_t *link)
{
	struct daos_hlink *hlink = hh_link2ptr(link);

	if (hlink->hl_ops != NULL &&
	    hlink->hl_ops->hop_free != NULL)
		hlink->hl_ops->hop_free(hlink);
}

static dhash_table_ops_t hh_ops = {
	.hop_key_init		= hh_op_key_init,
	.hop_key_get		= hh_op_key_get,
	.hop_key_hash		= hh_op_key_hash,
	.hop_key_cmp		= hh_op_key_cmp,
	.hop_rec_addref		= hh_op_rec_addref,
	.hop_rec_decref		= hh_op_rec_decref,
	.hop_rec_free		= hh_op_rec_free,
};

int
daos_hhash_create(unsigned int bits, struct daos_hhash **htable_pp)
{
	struct daos_hhash *hhtab;
	int		   rc;

	D_ALLOC_PTR(hhtab);
	if (hhtab == NULL)
		return -DER_NOMEM;

	rc = dhash_table_create_inplace(0, bits, NULL, &hh_ops,
					&hhtab->dh_htable);
	if (rc != 0)
		goto failed;

	hhtab->dh_cookie = 1;
	*htable_pp = hhtab;
	return 0;
 failed:
	D_FREE_PTR(hhtab);
	return -DER_NOMEM;
}

void
daos_hhash_destroy(struct daos_hhash *hhtab)
{
	dhash_table_debug(&hhtab->dh_htable);
	dhash_table_destroy_inplace(&hhtab->dh_htable, true);
	D_FREE_PTR(hhtab);
}

void
daos_hhash_hlink_init(struct daos_hlink *hlink, struct daos_hlink_ops *ops)
{
	hlink->hl_ops = ops;
	rlink_op_init(&hlink->hl_link);
}

bool
daos_uhash_link_empty(struct daos_ulink *ulink)
{
	return rlink_op_empty(&ulink->ul_link);
}

void
daos_hhash_link_insert(struct daos_hhash *hhtab, struct daos_hlink *hlink,
		       int type)
{
	D_ASSERT(hlink->hl_link.rl_initialized);
	dhash_rec_insert_anonym(&hhtab->dh_htable, &hlink->hl_link.rl_link,
				(void *)&type);
}

static inline struct daos_hlink*
daos_hlink_find(struct dhash_table *htable, void *key, size_t size)
{
	daos_list_t		*link;

	link = dhash_rec_find(htable, key, size);
	return link == NULL ? NULL : hh_link2ptr(link);
}

struct daos_hlink *
daos_hhash_link_lookup(struct daos_hhash *hhtab, uint64_t key)
{
	return daos_hlink_find(&hhtab->dh_htable, (void *)&key, sizeof(key));
}

bool
daos_hhash_link_delete(struct daos_hhash *hhtab, struct daos_hlink *hlink)
{
	return dhash_rec_delete_at(&hhtab->dh_htable, &hlink->hl_link.rl_link);
}

void
daos_hhash_link_putref(struct daos_hhash *hhtab, struct daos_hlink *hlink)
{
	dhash_rec_decref(&hhtab->dh_htable, &hlink->hl_link.rl_link);
}

bool
daos_hhash_link_empty(struct daos_hlink *hlink)
{
	return rlink_op_empty(&hlink->hl_link);
}

void
daos_hhash_link_key(struct daos_hlink *hlink, uint64_t *key)
{
	*key = hlink->hl_key;
}

int daos_hhash_key_type(uint64_t key)
{
	return hh_key_type(&key);
}

/**
 * daos uuid hash table
 * Key UUID, val: generic ptr
 */

static struct daos_ulink *
uh_link2ptr(daos_list_t *link)
{
	struct daos_rlink	*rlink;

	rlink = link2rlink(link);
	return container_of(rlink, struct daos_ulink, ul_link);
}

static unsigned int
uh_op_key_hash(struct dhash_table *uhtab, const void *key, unsigned int ksize)
{
	struct daos_uuid *lkey = (struct daos_uuid *)key;

	D_ASSERT(ksize == sizeof(struct daos_uuid));
	D_DEBUG(DF_MISC, "uuid_key: "DF_UUID"\n", DP_UUID(lkey->uuid));

	return (unsigned int)(daos_hash_string_u32((const char *)lkey->uuid,
						   sizeof(uuid_t)));
}

static bool
uh_op_key_cmp(struct dhash_table *uhtab, daos_list_t *link, const void *key,
	      unsigned int ksize)
{
	struct daos_ulink *ulink = uh_link2ptr(link);
	struct daos_uuid  *lkey = (struct daos_uuid *)key;

	D_ASSERT(ksize == sizeof(struct daos_uuid));
	D_DEBUG(DF_MISC, "Link key, Key:"DF_UUID","DF_UUID"\n",
		DP_UUID(lkey->uuid),
		DP_UUID(ulink->ul_uuid.uuid));

	return !uuid_compare(ulink->ul_uuid.uuid, lkey->uuid);
}

static void
uh_op_rec_free(struct dhash_table *hhtab, daos_list_t *link)
{
	struct daos_ulink *ulink = uh_link2ptr(link);

	if (ulink->ul_ops != NULL &&
	    ulink->ul_ops->uop_free != NULL)
		ulink->ul_ops->uop_free(ulink);
}



static dhash_table_ops_t uh_ops = {
	.hop_key_hash	= uh_op_key_hash,
	.hop_key_cmp	= uh_op_key_cmp,
	.hop_rec_addref	= hh_op_rec_addref, /* Reuse hh_op_add/decref */
	.hop_rec_decref = hh_op_rec_decref,
	.hop_rec_free	= uh_op_rec_free,
};

int
daos_uhash_create(int feats, unsigned int bits, struct dhash_table **htable_pp)
{
	struct dhash_table	*uhtab;
	int			rc;

	rc = dhash_table_create(feats, bits, NULL, &uh_ops, &uhtab);
	if (rc != 0)
		goto failed;

	*htable_pp = uhtab;
	return 0;

failed:
	return -DER_NOMEM;
}

void
daos_uhash_destroy(struct dhash_table *uhtab)
{
	dhash_table_debug(uhtab);
	dhash_table_destroy(uhtab, true);
}

void
daos_uhash_ulink_init(struct daos_ulink *ulink, struct daos_ulink_ops *ops)
{
	ulink->ul_ops = ops;
	rlink_op_init(&ulink->ul_link);
}

static inline struct daos_ulink*
daos_ulink_find(struct dhash_table *htable, void *key, size_t size)
{
	daos_list_t		*link;

	link = dhash_rec_find(htable, key, size);
	return link == NULL ? NULL : uh_link2ptr(link);
}

struct daos_ulink*
daos_uhash_link_lookup(struct dhash_table *uhtab, struct daos_uuid *key)
{
	return daos_ulink_find(uhtab, (void *)key, sizeof(struct daos_uuid));
}

void
daos_uhash_link_addref(struct dhash_table *uhtab, struct daos_ulink *ulink)
{
	dhash_rec_addref(uhtab, &ulink->ul_link.rl_link);
}

void
daos_uhash_link_putref(struct dhash_table *uhtab, struct daos_ulink *ulink)
{
	dhash_rec_decref(uhtab, &ulink->ul_link.rl_link);
}

int
daos_uhash_link_insert(struct dhash_table *uhtab, struct daos_uuid *key,
		       struct daos_ulink *ulink)
{
	int	rc = 0;

	D_ASSERT(ulink->ul_link.rl_initialized);

	uuid_copy(ulink->ul_uuid.uuid, key->uuid);
	rc = dhash_rec_insert(uhtab, (void *)key, sizeof(struct daos_uuid),
			      &ulink->ul_link.rl_link, true);

	if (rc)
		D_ERROR("Error Inserting handle in UUID in-memory hash\n");

	return rc;
}

bool
daos_uhash_link_last_ref(struct daos_ulink *ulink)
{
	return ulink->ul_link.rl_ref == 1;
}

void
daos_uhash_link_delete(struct dhash_table *uhtab, struct daos_ulink *ulink)
{
	dhash_rec_delete_at(uhtab, &ulink->ul_link.rl_link);
}
