/* Copyright (C) 2016-2017 Intel Corporation
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
 * This file is part of cart, it implements the hash table functions.
 */

#include <pthread.h>
#include <pouch/common.h>
#include <pouch/list.h>
#include <pouch/hash.h>

/*
 * Each thread has CF_UUID_MAX number of thread-local buffers for UUID strings.
 * Each debug message can have at most this many CP_UUIDs.
 *
 * CF_UUID prints the first eight characters of the string representation.
 */
#define CF_UUID_MAX	8
#define CF_UUID		"%.8s"

#define CRT_UUID_STR_SIZE 37	/* 36 + 1 for '\0' */

static __thread char thread_uuid_str_buf[CF_UUID_MAX][CRT_UUID_STR_SIZE];
static __thread int thread_uuid_str_buf_idx;

static char *
CP_UUID(const void *uuid)
{
	char *buf = thread_uuid_str_buf[thread_uuid_str_buf_idx];

	uuid_unparse_lower(uuid, buf);
	thread_uuid_str_buf_idx = (thread_uuid_str_buf_idx + 1) % CF_UUID_MAX;
	return buf;
}

uint64_t
crt_hash_mix64(uint64_t key)
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
crt_hash_mix96(uint32_t a, uint32_t b, uint32_t c)
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
crt_chash_srch_u64(uint64_t *hashes, unsigned int nhashes, uint64_t value)
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
crt_hash_string_u32(const char *string, unsigned int len)
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
crt_hash_murmur64(const unsigned char *key, unsigned int key_len,
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
	case 7:
		mur ^= (uint64_t)key[6] << 48;
	case 6:
		mur ^= (uint64_t)key[5] << 40;
	case 5:
		mur ^= (uint64_t)key[4] << 32;
	case 4:
		mur ^= (uint64_t)key[3] << 24;
	case 3:
		mur ^= (uint64_t)key[2] << 16;
	case 2:
		mur ^= (uint64_t)key[1] << 8;
	case 1:
		mur ^= (uint64_t)key[0];
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
ch_lock_init(struct chash_table *htable)
{
	if (htable->ht_feats & DHASH_FT_NOLOCK)
		return;

	if (htable->ht_feats & DHASH_FT_RWLOCK)
		pthread_rwlock_init(&htable->ht_rwlock, NULL);
	else
		pthread_mutex_init(&htable->ht_lock, NULL);
}

static void
ch_lock_fini(struct chash_table *htable)
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
ch_lock(struct chash_table *htable, bool read_only)
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
ch_unlock(struct chash_table *htable, bool read_only)
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
ch_key_hash(struct chash_table *htable, const void *key, unsigned int ksize)
{
	unsigned int idx;

	if (htable->ht_ops->hop_key_hash)
		idx = htable->ht_ops->hop_key_hash(htable, key, ksize);
	else
		idx = crt_hash_string_u32((const char *)key, ksize);

	return idx & ((1U << htable->ht_bits) - 1);
}

static void
ch_key_init(struct chash_table *htable, crt_list_t *rlink, void *args)
{
	C_ASSERT(htable->ht_ops->hop_key_init);
	htable->ht_ops->hop_key_init(htable, rlink, args);
}

static bool
ch_key_cmp(struct chash_table *htable, crt_list_t *rlink,
	   const void *key, unsigned int ksize)
{
	C_ASSERT(htable->ht_ops->hop_key_cmp);
	return htable->ht_ops->hop_key_cmp(htable, rlink, key, ksize);
}

static unsigned int
ch_key_get(struct chash_table *htable, crt_list_t *rlink, void **key_pp)
{
	C_ASSERT(htable->ht_ops->hop_key_get);
	return htable->ht_ops->hop_key_get(htable, rlink, key_pp);
}

static void
ch_rec_insert(struct chash_table *htable, unsigned idx, crt_list_t *rlink)
{
	struct chash_bucket *bucket = &htable->ht_buckets[idx];

	crt_list_add(rlink, &bucket->hb_head);
#if DHASH_DEBUG
	htable->ht_nr++;
	if (htable->ht_nr > htable->ht_nr_max)
		htable->ht_nr_max = htable->ht_nr;

	if (htable->ht_ops->hop_key_get) {
		bucket->hb_dep++;
		if (bucket->hb_dep > htable->ht_dep_max) {
			htable->ht_dep_max = bucket->hb_dep;
			C_DEBUG("Max depth %d/%d/%d\n", htable->ht_dep_max,
				htable->ht_nr, htable->ht_nr_max);
		}
	}
#endif
}

static void
ch_rec_delete(struct chash_table *htable, crt_list_t *rlink)
{
	crt_list_del_init(rlink);
#if DHASH_DEBUG
	htable->ht_nr--;
	if (htable->ht_ops->hop_key_get) {
		struct chash_bucket *bucket;
		void		    *key;
		unsigned int	     size;

		size = htable->ht_ops->hop_key_get(htable, rlink, &key);
		bucket = &htable->ht_buckets[ch_key_hash(htable, key, size)];
		bucket->hb_dep--;
	}
#endif
}

static crt_list_t *
ch_rec_find(struct chash_table *htable, unsigned idx, const void *key,
	     unsigned int ksize)
{
	struct chash_bucket *bucket = &htable->ht_buckets[idx];
	crt_list_t	    *rlink;

	crt_list_for_each(rlink, &bucket->hb_head) {
		if (ch_key_cmp(htable, rlink, key, ksize))
			return rlink;
	}
	return NULL;
}

static void
ch_rec_addref(struct chash_table *htable, crt_list_t *rlink)
{
	if (htable->ht_ops->hop_rec_addref)
		htable->ht_ops->hop_rec_addref(htable, rlink);
}

static bool
ch_rec_decref(struct chash_table *htable, crt_list_t *rlink)
{
	return htable->ht_ops->hop_rec_decref ?
	       htable->ht_ops->hop_rec_decref(htable, rlink) : false;
}

static void
ch_rec_free(struct chash_table *htable, crt_list_t *rlink)
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
crt_list_t *
chash_rec_find(struct chash_table *htable, const void *key, unsigned int ksize)
{
	crt_list_t	*rlink;
	int		 idx;

	C_ASSERT(key != NULL);

	idx = ch_key_hash(htable, key, ksize);
	ch_lock(htable, true);

	rlink = ch_rec_find(htable, idx, key, ksize);
	if (rlink != NULL)
		ch_rec_addref(htable, rlink);

	ch_unlock(htable, true);
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
chash_rec_insert(struct chash_table *htable, const void *key,
		 unsigned int ksize, crt_list_t *rlink, bool exclusive)
{
	int	idx;
	int	rc = 0;

	C_ASSERT(key != NULL && ksize != 0);

	idx = ch_key_hash(htable, key, ksize);
	ch_lock(htable, false);

	if (exclusive && ch_rec_find(htable, idx, key, ksize))
		C_GOTO(out, rc = -CER_EXIST);

	ch_rec_addref(htable, rlink);
	ch_rec_insert(htable, idx, rlink);
 out:
	ch_unlock(htable, false);
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
chash_rec_insert_anonym(struct chash_table *htable, crt_list_t *rlink,
			void *args)
{
	void	*key;
	int	 idx;
	int	 ksize;

	if (htable->ht_ops->hop_key_init == NULL ||
	    htable->ht_ops->hop_key_get == NULL)
		return -CER_NO_PERM;

	ch_lock(htable, false);
	/* has no key, hash table should have provided key generator */
	ch_key_init(htable, rlink, args);

	ksize = ch_key_get(htable, rlink, &key);
	idx = ch_key_hash(htable, key, ksize);

	ch_rec_addref(htable, rlink);
	ch_rec_insert(htable, idx, rlink);

	ch_unlock(htable, false);
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
chash_rec_delete(struct chash_table *htable, const void *key,
		 unsigned int ksize)
{
	crt_list_t	*rlink;
	int		 idx;
	bool		 deleted = false;
	bool		 zombie  = false;

	C_ASSERT(key != NULL);

	idx = ch_key_hash(htable, key, ksize);
	ch_lock(htable, false);

	rlink = ch_rec_find(htable, idx, key, ksize);
	if (rlink != NULL) {
		ch_rec_delete(htable, rlink);
		zombie = ch_rec_decref(htable, rlink);
		deleted = true;
	}

	ch_unlock(htable, false);
	if (zombie)
		ch_rec_free(htable, rlink);

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
chash_rec_delete_at(struct chash_table *htable, crt_list_t *rlink)
{
	bool	deleted = false;
	bool	zombie  = false;

	ch_lock(htable, false);

	if (!crt_list_empty(rlink)) {
		ch_rec_delete(htable, rlink);
		zombie = ch_rec_decref(htable, rlink);
		deleted = true;
	}
	ch_unlock(htable, false);

	if (zombie)
		ch_rec_free(htable, rlink);

	return deleted;
}

/**
 * Increase the refcount of the record.
 *
 * \param htable	[IN]	Pointer to the hash table
 * \param rlink		[IN]	The link chain of the record
 */
void
chash_rec_addref(struct chash_table *htable, crt_list_t *rlink)
{
	ch_lock(htable, true);
	ch_rec_addref(htable, rlink);
	ch_unlock(htable, true);
}

/**
 * Decrease the refcount of the record.
 * The record will be freed if hop_decref() returns true.
 *
 * \param htable	[IN]	Pointer to the hash table
 * \param rlink		[IN]	Chain rlink of the hash record
 */
void
chash_rec_decref(struct chash_table *htable, crt_list_t *rlink)
{
	bool zombie;

	ch_lock(htable, true);

	zombie = ch_rec_decref(htable, rlink);
	C_ASSERT(!zombie || crt_list_empty(rlink));

	ch_unlock(htable, true);
	if (zombie)
		ch_rec_free(htable, rlink);
}

/**
 * The link chain has already been unlinked from the hash table or not.
 *
 * \return	True	Yes
 *		False	No
 */
bool
chash_rec_unlinked(crt_list_t *rlink)
{
	return crt_list_empty(rlink);
}

/**
 * Initialise an inplace hash table.
 *
 * NB: Please be careful while using rwlock and refcount at the same time,
 * see chash_feats for the details.
 *
 * \param feats		[IN]	Feature bits, see DHASH_FT_*
 * \param bits		[IN]	power2(bits) is the size of hash table
 * \param priv		[IN]	Private data for the hash table
 * \param hops		[IN]	Customized member functions
 * \param htable	[IN]	Hash table to be initialised
 */
int
chash_table_create_inplace(uint32_t feats, unsigned int bits, void *priv,
			   chash_table_ops_t *hops, struct chash_table *htable)
{
	struct chash_bucket *buckets;
	int		     nr = (1 << bits);
	int		     i;

	C_ASSERT(hops != NULL);
	C_ASSERT(hops->hop_key_cmp != NULL);

	htable->ht_feats = feats;
	htable->ht_bits	 = bits;
	htable->ht_ops	 = hops;
	htable->ht_priv	 = priv;

	C_ALLOC(buckets, sizeof(*buckets) * nr);
	if (buckets == NULL)
		return -CER_NOMEM;

	for (i = 0; i < nr; i++)
		CRT_INIT_LIST_HEAD(&buckets[i].hb_head);

	htable->ht_buckets = buckets;
	ch_lock_init(htable);

	return 0;
}

/**
 * Create a new hash table.
 *
 * NB: Please be careful while using rwlock and refcount at the same time,
 * see chash_feats for the details.
 *
 * \param feats		[IN]	Feature bits, see DHASH_FT_*
 * \param bits		[IN]	power2(bits) is the size of hash table
 * \param priv		[IN]	Private data for the hash table
 * \param hops		[IN]	Customized member functions
 * \param htable_pp	[OUT]	The newly created hash table
 */
int
chash_table_create(uint32_t feats, unsigned int bits, void *priv,
		   chash_table_ops_t *hops, struct chash_table **htable_pp)
{
	struct chash_table *htable;
	int		    rc;

	C_ALLOC_PTR(htable);
	if (htable == NULL)
		return -CER_NOMEM;

	rc = chash_table_create_inplace(feats, bits, priv, hops, htable);
	if (rc != 0)
		goto failed;

	*htable_pp = htable;
	return 0;
 failed:
	C_FREE_PTR(htable);
	return rc;
}

/**
 * Traverse a hash table, call the traverse callback function on every item.
 * Break once the callback returns non-zero.
 *
 * \param htable	[IN]	The hash table to be finalised.
 * \param cb		[IN]	Traverse callback, will be called on every item
 *				in the hash table.
 *				\see chash_traverse_cb_t.
 * \param args		[IN]	Arguments for the callback.
 *
 * \return			zero on success, negative value if error.
 */
int
chash_table_traverse(struct chash_table *htable, chash_traverse_cb_t cb,
		     void *args)
{
	struct chash_bucket *buckets = htable->ht_buckets;
	crt_list_t	    *rlink;

	int		     nr;
	int		     i;
	int		     rc = 0;

	if (buckets == NULL) {
		C_ERROR("chash_table %p not initialized (NULL buckets).\n",
			htable);
		C_GOTO(out, rc = -CER_UNINIT);
	}
	if (cb == NULL) {
		C_ERROR("invalid parameter, NULL cb.\n");
		C_GOTO(out, rc = -CER_INVAL);
	}

	ch_lock(htable, true);

	nr = 1U << htable->ht_bits;
	for (i = 0; i < nr; i++) {
		crt_list_for_each(rlink, &buckets[i].hb_head) {
			rc = cb(rlink, args);
			if (rc != 0)
				C_GOTO(unlock, rc);
		}
	}

unlock:
	ch_unlock(htable, true);
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
chash_table_destroy_inplace(struct chash_table *htable, bool force)
{
	struct chash_bucket *buckets = htable->ht_buckets;
	int		     nr;
	int		     i;

	if (buckets == NULL)
		goto out;

	nr = 1U << htable->ht_bits;
	for (i = 0; i < nr; i++) {
		while (!crt_list_empty(&buckets[i].hb_head)) {
			if (!force) {
				C_DEBUG("Warning, non-empty hash\n");
				return -CER_BUSY;
			}
			chash_rec_delete_at(htable, buckets[i].hb_head.next);
		}
	}
	C_FREE(buckets, sizeof(*buckets) * nr);
	ch_lock_fini(htable);
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
chash_table_destroy(struct chash_table *htable, bool force)
{
	chash_table_destroy_inplace(htable, force);
	C_FREE_PTR(htable);
	return 0;
}

/**
 * Print stats of the hash table.
 */
void
chash_table_debug(struct chash_table *htable)
{
#if DHASH_DEBUG
	C_DEBUG("max nr: %d, cur nr: %d, max_dep: %d\n",
		htable->ht_nr_max, htable->ht_nr, htable->ht_dep_max);
#endif
}

/**
 * daos handle hash table: the first user of chash_table
 */

struct crt_hhash {
	uint64_t                ch_cookie;
	struct chash_table	ch_htable;
};

static struct crt_rlink*
link2rlink(crt_list_t *link)
{
	C_ASSERT(link != NULL);
	return container_of(link, struct crt_rlink, rl_link);
}

static void
rlink_op_addref(struct crt_rlink *rlink)
{
	rlink->rl_ref++;
}

static bool
rlink_op_decref(struct crt_rlink *rlink)
{
	C_ASSERT(rlink->rl_ref > 0);
	rlink->rl_ref--;

	return rlink->rl_ref == 0;
}

static void
rlink_op_init(struct crt_rlink *rlink)
{
	CRT_INIT_LIST_HEAD(&rlink->rl_link);
	rlink->rl_initialized	= 1;
	rlink->rl_ref		= 1; /* for caller */
}


static bool
rlink_op_empty(struct crt_rlink *rlink)
{
	if (!rlink->rl_initialized)
		return 1;
	C_ASSERT(rlink->rl_ref != 0 || chash_rec_unlinked(&rlink->rl_link));
	return chash_rec_unlinked(&rlink->rl_link);
}

static struct crt_hlink *
hh_link2ptr(crt_list_t *link)
{
	struct crt_rlink	*rlink;

	rlink = link2rlink(link);
	return	container_of(rlink, struct crt_hlink, hl_link);
}

static void
hh_op_key_init(struct chash_table *hhtab, crt_list_t *rlink, void *args)
{
	struct crt_hhash *dht;
	struct crt_hlink *hlink = hh_link2ptr(rlink);
	int		   type = *(int *)args;

	dht = container_of(hhtab, struct crt_hhash, ch_htable);
	hlink->hl_key = ((dht->ch_cookie++) << CRT_HTYPE_BITS) | type;
}

static int
hh_key_type(const void *key)
{
	uint64_t	cookie;

	C_ASSERT(key != NULL);
	cookie = *(uint64_t *)key;

	return cookie & CRT_HTYPE_MASK;
}

static int
hh_op_key_get(struct chash_table *hhtab, crt_list_t *rlink, void **key_pp)
{
	struct crt_hlink *hlink = hh_link2ptr(rlink);

	*key_pp = (void *)&hlink->hl_key;
	return sizeof(hlink->hl_key);
}

static uint32_t
hh_op_key_hash(struct chash_table *hhtab, const void *key, unsigned int ksize)
{
	C_ASSERT(ksize == sizeof(uint64_t));

	return (unsigned int)(*(const uint64_t *)key >> CRT_HTYPE_BITS);
}

static bool
hh_op_key_cmp(struct chash_table *hhtab, crt_list_t *link,
	  const void *key, unsigned int ksize)
{
	struct crt_hlink *hlink = hh_link2ptr(link);

	C_ASSERT(ksize == sizeof(uint64_t));
	return hlink->hl_key == *(uint64_t *)key;
}

static void
hh_op_rec_addref(struct chash_table *hhtab, crt_list_t *link)
{
	rlink_op_addref(link2rlink(link));
}

static bool
hh_op_rec_decref(struct chash_table *hhtab, crt_list_t *link)
{
	return rlink_op_decref(link2rlink(link));
}

static void
hh_op_rec_free(struct chash_table *hhtab, crt_list_t *link)
{
	struct crt_hlink *hlink = hh_link2ptr(link);

	if (hlink->hl_ops != NULL &&
	    hlink->hl_ops->hop_free != NULL)
		hlink->hl_ops->hop_free(hlink);
}

static chash_table_ops_t hh_ops = {
	.hop_key_init		= hh_op_key_init,
	.hop_key_get		= hh_op_key_get,
	.hop_key_hash		= hh_op_key_hash,
	.hop_key_cmp		= hh_op_key_cmp,
	.hop_rec_addref		= hh_op_rec_addref,
	.hop_rec_decref		= hh_op_rec_decref,
	.hop_rec_free		= hh_op_rec_free,
};

int
crt_hhash_create(unsigned int bits, struct crt_hhash **htable_pp)
{
	struct crt_hhash *hhtab;
	int		   rc;

	C_ALLOC_PTR(hhtab);
	if (hhtab == NULL)
		return -CER_NOMEM;

	rc = chash_table_create_inplace(0, bits, NULL, &hh_ops,
					&hhtab->ch_htable);
	if (rc != 0)
		goto failed;

	hhtab->ch_cookie = 1;
	*htable_pp = hhtab;
	return 0;
 failed:
	C_FREE_PTR(hhtab);
	return -CER_NOMEM;
}

void
crt_hhash_destroy(struct crt_hhash *hhtab)
{
	chash_table_debug(&hhtab->ch_htable);
	chash_table_destroy_inplace(&hhtab->ch_htable, true);
	C_FREE_PTR(hhtab);
}

void
crt_hhash_hlink_init(struct crt_hlink *hlink, struct crt_hlink_ops *ops)
{
	hlink->hl_ops = ops;
	rlink_op_init(&hlink->hl_link);
}

bool
crt_uhash_link_empty(struct crt_ulink *ulink)
{
	return rlink_op_empty(&ulink->ul_link);
}

void
crt_hhash_link_insert(struct crt_hhash *hhtab, struct crt_hlink *hlink,
		       int type)
{
	C_ASSERT(hlink->hl_link.rl_initialized);
	chash_rec_insert_anonym(&hhtab->ch_htable, &hlink->hl_link.rl_link,
				(void *)&type);
}

static inline struct crt_hlink*
crt_hlink_find(struct chash_table *htable, void *key, size_t size)
{
	crt_list_t		*link;

	link = chash_rec_find(htable, key, size);
	return link == NULL ? NULL : hh_link2ptr(link);
}

struct crt_hlink *
crt_hhash_link_lookup(struct crt_hhash *hhtab, uint64_t key)
{
	return crt_hlink_find(&hhtab->ch_htable, (void *)&key, sizeof(key));
}

bool
crt_hhash_link_delete(struct crt_hhash *hhtab, struct crt_hlink *hlink)
{
	return chash_rec_delete_at(&hhtab->ch_htable, &hlink->hl_link.rl_link);
}

void
crt_hhash_link_addref(struct crt_hhash *hhtab, struct crt_hlink *hlink)
{
	chash_rec_addref(&hhtab->ch_htable, &hlink->hl_link.rl_link);
}

void
crt_hhash_link_putref(struct crt_hhash *hhtab, struct crt_hlink *hlink)
{
	chash_rec_decref(&hhtab->ch_htable, &hlink->hl_link.rl_link);
}

bool
crt_hhash_link_empty(struct crt_hlink *hlink)
{
	return rlink_op_empty(&hlink->hl_link);
}

void
crt_hhash_link_key(struct crt_hlink *hlink, uint64_t *key)
{
	*key = hlink->hl_key;
}

int crt_hhash_key_type(uint64_t key)
{
	return hh_key_type(&key);
}

/**
 * daos uuid hash table
 * Key UUID, val: generic ptr
 */

static struct crt_ulink *
uh_link2ptr(crt_list_t *link)
{
	struct crt_rlink	*rlink;

	rlink = link2rlink(link);
	return container_of(rlink, struct crt_ulink, ul_link);
}

static unsigned int
uh_op_key_hash(struct chash_table *uhtab, const void *key, unsigned int ksize)
{
	struct crt_uuid *lkey = (struct crt_uuid *)key;

	C_ASSERT(ksize == sizeof(struct crt_uuid));
	C_DEBUG("uuid_key: "CF_UUID"\n", CP_UUID(lkey->uuid));

	return (unsigned int)(crt_hash_string_u32((const char *)lkey->uuid,
						   sizeof(uuid_t)));
}

static bool
uh_op_key_cmp(struct chash_table *uhtab, crt_list_t *link, const void *key,
	      unsigned int ksize)
{
	struct crt_ulink *ulink = uh_link2ptr(link);
	struct crt_uuid  *lkey = (struct crt_uuid *)key;

	C_ASSERT(ksize == sizeof(struct crt_uuid));
	C_DEBUG("Link key, Key:"CF_UUID","CF_UUID"\n",
		CP_UUID(lkey->uuid),
		CP_UUID(ulink->ul_uuid.uuid));

	return !uuid_compare(ulink->ul_uuid.uuid, lkey->uuid);
}

static void
uh_op_rec_free(struct chash_table *hhtab, crt_list_t *link)
{
	struct crt_ulink *ulink = uh_link2ptr(link);

	if (ulink->ul_ops != NULL &&
	    ulink->ul_ops->uop_free != NULL)
		ulink->ul_ops->uop_free(ulink);
}



static chash_table_ops_t uh_ops = {
	.hop_key_hash	= uh_op_key_hash,
	.hop_key_cmp	= uh_op_key_cmp,
	.hop_rec_addref	= hh_op_rec_addref, /* Reuse hh_op_add/decref */
	.hop_rec_decref = hh_op_rec_decref,
	.hop_rec_free	= uh_op_rec_free,
};

int
crt_uhash_create(int feats, unsigned int bits, struct chash_table **htable_pp)
{
	struct chash_table	*uhtab;
	int			rc;

	rc = chash_table_create(feats, bits, NULL, &uh_ops, &uhtab);
	if (rc != 0)
		goto failed;

	*htable_pp = uhtab;
	return 0;

failed:
	return -CER_NOMEM;
}

void
crt_uhash_destroy(struct chash_table *uhtab)
{
	chash_table_debug(uhtab);
	chash_table_destroy(uhtab, true);
}

void
crt_uhash_ulink_init(struct crt_ulink *ulink, struct crt_ulink_ops *ops)
{
	ulink->ul_ops = ops;
	rlink_op_init(&ulink->ul_link);
}

static inline struct crt_ulink*
crt_ulink_find(struct chash_table *htable, void *key, size_t size)
{
	crt_list_t		*link;

	link = chash_rec_find(htable, key, size);
	return link == NULL ? NULL : uh_link2ptr(link);
}

struct crt_ulink*
crt_uhash_link_lookup(struct chash_table *uhtab, struct crt_uuid *key)
{
	return crt_ulink_find(uhtab, (void *)key, sizeof(struct crt_uuid));
}

void
crt_uhash_link_addref(struct chash_table *uhtab, struct crt_ulink *ulink)
{
	chash_rec_addref(uhtab, &ulink->ul_link.rl_link);
}

void
crt_uhash_link_putref(struct chash_table *uhtab, struct crt_ulink *ulink)
{
	chash_rec_decref(uhtab, &ulink->ul_link.rl_link);
}

int
crt_uhash_link_insert(struct chash_table *uhtab, struct crt_uuid *key,
		       struct crt_ulink *ulink)
{
	int	rc = 0;

	C_ASSERT(ulink->ul_link.rl_initialized);

	uuid_copy(ulink->ul_uuid.uuid, key->uuid);
	rc = chash_rec_insert(uhtab, (void *)key, sizeof(struct crt_uuid),
			      &ulink->ul_link.rl_link, true);

	if (rc)
		C_ERROR("Error Inserting handle in UUID in-memory hash\n");

	return rc;
}

bool
crt_uhash_link_last_ref(struct crt_ulink *ulink)
{
	return ulink->ul_link.rl_ref == 1;
}

void
crt_uhash_link_delete(struct chash_table *uhtab, struct crt_ulink *ulink)
{
	chash_rec_delete_at(uhtab, &ulink->ul_link.rl_link);
}
