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
/**
 * This file is part of cart, it implements the hash table functions.
 */
#define D_LOGFAC	DD_FAC(mem)

#include <pthread.h>
#include <gurt/common.h>
#include <gurt/list.h>
#include <gurt/hash.h>

/******************************************************************************
 * Hash functions / supporting routines
 ******************************************************************************/

/*
 * Each thread has CF_UUID_MAX number of thread-local buffers for UUID strings.
 * Each debug message can have at most this many CP_UUIDs.
 *
 * CF_UUID prints the first eight characters of the string representation.
 */
#define CF_UUID_MAX	8
#define CF_UUID		"%.8s"

#define D_UUID_STR_SIZE 37	/* 36 + 1 for '\0' */

static __thread char thread_uuid_str_buf[CF_UUID_MAX][D_UUID_STR_SIZE];
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
d_hash_mix64(uint64_t key)
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
d_hash_mix96(uint32_t a, uint32_t b, uint32_t c)
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
d_hash_srch_u64(uint64_t *hashes, unsigned int nhashes, uint64_t value)
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
d_hash_string_u32(const char *string, unsigned int len)
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
d_hash_murmur64(const unsigned char *key, unsigned int key_len,
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

/******************************************************************************
 * Generic Hash Table functions / data structures
 ******************************************************************************/

static int
ch_lock_init(struct d_hash_table *htable)
{
	if (htable->ht_feats & D_HASH_FT_NOLOCK)
		return 0;

	if (htable->ht_feats & D_HASH_FT_RWLOCK)
		return D_RWLOCK_INIT(&htable->ht_rwlock, NULL);
	else
		return D_MUTEX_INIT(&htable->ht_lock, NULL);
}

static void
ch_lock_fini(struct d_hash_table *htable)
{
	if (htable->ht_feats & D_HASH_FT_NOLOCK)
		return;

	if (htable->ht_feats & D_HASH_FT_RWLOCK)
		D_RWLOCK_DESTROY(&htable->ht_rwlock);
	else
		D_MUTEX_DESTROY(&htable->ht_lock);
}

/**
 * Lock the hash table
 *
 * Note: if hash table is using rwlock, it only takes read lock for
 * reference-only operations and caller should protect refcount.
 * see D_HASH_FT_RWLOCK for the details.
 */
static void
ch_lock(struct d_hash_table *htable, bool read_only)
{

	if (htable->ht_feats & D_HASH_FT_NOLOCK)
		return;

	if (htable->ht_feats & D_HASH_FT_RWLOCK) {
		if (read_only)
			D_RWLOCK_RDLOCK(&htable->ht_rwlock);
		else
			D_RWLOCK_WRLOCK(&htable->ht_rwlock);

	} else {
		D_MUTEX_LOCK(&htable->ht_lock);
	}
}

/** unlock the hash table */
static void
ch_unlock(struct d_hash_table *htable, bool read_only)
{
	if (htable->ht_feats & D_HASH_FT_NOLOCK)
		return;

	if (htable->ht_feats & D_HASH_FT_RWLOCK)
		D_RWLOCK_UNLOCK(&htable->ht_rwlock);
	else
		D_MUTEX_UNLOCK(&htable->ht_lock);
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
ch_key_hash(struct d_hash_table *htable, const void *key, unsigned int ksize)
{
	unsigned int idx;

	if (htable->ht_ops->hop_key_hash)
		idx = htable->ht_ops->hop_key_hash(htable, key, ksize);
	else
		idx = d_hash_string_u32((const char *)key, ksize);

	return idx & ((1U << htable->ht_bits) - 1);
}

static void
ch_key_init(struct d_hash_table *htable, d_list_t *rlink, void *arg)
{
	D_ASSERT(htable->ht_ops->hop_key_init);
	htable->ht_ops->hop_key_init(htable, rlink, arg);
}

static bool
ch_key_cmp(struct d_hash_table *htable, d_list_t *rlink,
	   const void *key, unsigned int ksize)
{
	D_ASSERT(htable->ht_ops->hop_key_cmp);
	return htable->ht_ops->hop_key_cmp(htable, rlink, key, ksize);
}

static unsigned int
ch_key_get(struct d_hash_table *htable, d_list_t *rlink, void **key_pp)
{
	D_ASSERT(htable->ht_ops->hop_key_get);
	return htable->ht_ops->hop_key_get(htable, rlink, key_pp);
}

static void
ch_rec_insert(struct d_hash_table *htable, unsigned idx, d_list_t *rlink)
{
	struct d_hash_bucket *bucket = &htable->ht_buckets[idx];

	d_list_add(rlink, &bucket->hb_head);
#if D_HASH_DEBUG
	htable->ht_nr++;
	if (htable->ht_nr > htable->ht_nr_max)
		htable->ht_nr_max = htable->ht_nr;

	if (htable->ht_ops->hop_key_get) {
		bucket->hb_dep++;
		if (bucket->hb_dep > htable->ht_dep_max) {
			htable->ht_dep_max = bucket->hb_dep;
			D_DEBUG(DB_TRACE, "Max depth %d/%d/%d\n",
				htable->ht_dep_max, htable->ht_nr,
				htable->ht_nr_max);
		}
	}
#endif
}

static void
ch_rec_delete(struct d_hash_table *htable, d_list_t *rlink)
{
	d_list_del_init(rlink);
#if D_HASH_DEBUG
	htable->ht_nr--;
	if (htable->ht_ops->hop_key_get) {
		struct d_hash_bucket *bucket;
		void		    *key;
		unsigned int	     size;

		size = htable->ht_ops->hop_key_get(htable, rlink, &key);
		bucket = &htable->ht_buckets[ch_key_hash(htable, key, size)];
		bucket->hb_dep--;
	}
#endif
}

static d_list_t *
ch_rec_find(struct d_hash_table *htable, unsigned idx, const void *key,
	    unsigned int ksize)
{
	struct d_hash_bucket	*bucket = &htable->ht_buckets[idx];
	d_list_t		*rlink;

	d_list_for_each(rlink, &bucket->hb_head) {
		if (ch_key_cmp(htable, rlink, key, ksize))
			return rlink;
	}
	return NULL;
}

static void
ch_rec_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	if (htable->ht_ops->hop_rec_addref)
		htable->ht_ops->hop_rec_addref(htable, rlink);
}

static bool
ch_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	return htable->ht_ops->hop_rec_decref ?
	       htable->ht_ops->hop_rec_decref(htable, rlink) : false;
}

static void
ch_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	if (htable->ht_ops->hop_rec_free)
		htable->ht_ops->hop_rec_free(htable, rlink);
}

/**
 * Insert the record into the hash table and take refcount on it if
 * "ephemeral" is not set.
 */
static void
ch_rec_insert_addref(struct d_hash_table *htable, int bucket_idx,
		     d_list_t *rlink)
{
	if (!(htable->ht_feats & D_HASH_FT_EPHEMERAL))
		ch_rec_addref(htable, rlink);

	ch_rec_insert(htable, bucket_idx, rlink);
}

/**
 * Delete the record from the hash table, it also releases refcount of it if
 * "ephemeral" is not set.
 */
static bool
ch_rec_del_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	bool zombie = false;

	ch_rec_delete(htable, rlink);
	if (!(htable->ht_feats & D_HASH_FT_EPHEMERAL))
		zombie = ch_rec_decref(htable, rlink);

	return zombie;
}

d_list_t *
d_hash_rec_find(struct d_hash_table *htable, const void *key,
		unsigned int ksize)
{
	d_list_t	*rlink;
	int		 idx;

	D_ASSERT(key != NULL);

	idx = ch_key_hash(htable, key, ksize);
	ch_lock(htable, true);

	rlink = ch_rec_find(htable, idx, key, ksize);
	if (rlink != NULL)
		ch_rec_addref(htable, rlink);

	ch_unlock(htable, true);
	return rlink;
}

int
d_hash_rec_insert(struct d_hash_table *htable, const void *key,
		  unsigned int ksize, d_list_t *rlink, bool exclusive)
{
	int	idx;
	int	rc = 0;

	D_ASSERT(key != NULL && ksize != 0);
	idx = ch_key_hash(htable, key, ksize);

	ch_lock(htable, false);
	if (exclusive) {
		d_list_t *tmp;

		tmp = ch_rec_find(htable, idx, key, ksize);
		if (tmp)
			D_GOTO(out, rc = -DER_EXIST);
	}
	ch_rec_insert_addref(htable, idx, rlink);
out:
	ch_unlock(htable, false);
	return rc;
}

d_list_t *
d_hash_rec_find_insert(struct d_hash_table *htable, const void *key,
		       unsigned int ksize, d_list_t *rlink)
{
	d_list_t *tmp;
	int	  idx;

	D_ASSERT(key != NULL && ksize != 0);
	idx = ch_key_hash(htable, key, ksize);

	ch_lock(htable, false);
	tmp = ch_rec_find(htable, idx, key, ksize);
	if (tmp) {
		ch_rec_addref(htable, tmp);
		rlink = tmp;
		D_GOTO(out, 0);
	}
	ch_rec_insert_addref(htable, idx, rlink);
out:
	ch_unlock(htable, false);
	return rlink;
}

int
d_hash_rec_insert_anonym(struct d_hash_table *htable, d_list_t *rlink,
			 void *arg)
{
	void	*key;
	int	 idx;
	int	 ksize;

	if (htable->ht_ops->hop_key_init == NULL ||
	    htable->ht_ops->hop_key_get == NULL)
		return -DER_NO_PERM;

	ch_lock(htable, false);
	/* has no key, hash table should have provided key generator */
	ch_key_init(htable, rlink, arg);

	ksize = ch_key_get(htable, rlink, &key);
	idx = ch_key_hash(htable, key, ksize);
	ch_rec_insert_addref(htable, idx, rlink);

	ch_unlock(htable, false);
	return 0;
}

bool
d_hash_rec_delete(struct d_hash_table *htable, const void *key,
		  unsigned int ksize)
{
	d_list_t	*rlink;
	int		 idx;
	bool		 deleted = false;
	bool		 zombie  = false;

	D_ASSERT(key != NULL);

	idx = ch_key_hash(htable, key, ksize);
	ch_lock(htable, false);

	rlink = ch_rec_find(htable, idx, key, ksize);
	if (rlink != NULL) {
		zombie = ch_rec_del_decref(htable, rlink);
		deleted = true;
	}

	ch_unlock(htable, false);
	if (zombie)
		ch_rec_free(htable, rlink);

	return deleted;
}

bool
d_hash_rec_delete_at(struct d_hash_table *htable, d_list_t *rlink)
{
	bool	deleted = false;
	bool	zombie  = false;

	ch_lock(htable, false);

	if (!d_list_empty(rlink)) {
		zombie = ch_rec_del_decref(htable, rlink);
		deleted = true;
	}
	ch_unlock(htable, false);

	if (zombie)
		ch_rec_free(htable, rlink);

	return deleted;
}

void
d_hash_rec_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	ch_lock(htable, true);
	ch_rec_addref(htable, rlink);
	ch_unlock(htable, true);
}

void
d_hash_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	bool ephemeral = (htable->ht_feats & D_HASH_FT_EPHEMERAL);
	bool zombie;

	ch_lock(htable, !ephemeral);
	zombie = ch_rec_decref(htable, rlink);

	if (zombie && ephemeral && !d_list_empty(rlink))
		ch_rec_delete(htable, rlink);

	D_ASSERT(!zombie || d_list_empty(rlink));

	ch_unlock(htable, !ephemeral);
	if (zombie)
		ch_rec_free(htable, rlink);
}

int
d_hash_rec_ndecref(struct d_hash_table *htable, int count, d_list_t *rlink)
{
	bool ephemeral = (htable->ht_feats & D_HASH_FT_EPHEMERAL);
	bool zombie;
	int rc = 0;

	ch_lock(htable, !ephemeral);
	do {
		zombie = ch_rec_decref(htable, rlink);
	} while (--count && !zombie);

	if (count != 0)
		rc = -DER_INVAL;

	if (zombie && ephemeral && !d_list_empty(rlink))
		ch_rec_delete(htable, rlink);

	D_ASSERT(!zombie || d_list_empty(rlink));

	ch_unlock(htable, !ephemeral);
	if (zombie)
		ch_rec_free(htable, rlink);

	return rc;
}

bool
d_hash_rec_unlinked(d_list_t *rlink)
{
	return d_list_empty(rlink);
}

/* Find an entry in the hash table.
 *
 * As d_hash_table_traverse() does not support removal from the callback
 * function save a pointer in *arg and return 1 to terminate the traverse.
 * This way we can iterate over the entries in the hash table and delete every
 * one.
 */
static int
d_hash_find_single(d_list_t *rlink, void *arg)
{
	d_list_t **p = arg;

	*p = rlink;
	return 1;
}

d_list_t *
d_hash_rec_first(struct d_hash_table *htable)
{
	d_list_t *rlink = NULL;
	int rc;

	rc = d_hash_table_traverse(htable, d_hash_find_single, &rlink);
	if (rc < 0)
		return NULL;

	return rlink;
}

int
d_hash_table_create_inplace(uint32_t feats, unsigned int bits, void *priv,
			    d_hash_table_ops_t *hops,
			    struct d_hash_table *htable)
{
	struct d_hash_bucket	*buckets;
	int			nr = (1 << bits);
	int			i;
	int			rc;
	D_ASSERT(hops != NULL);
	D_ASSERT(hops->hop_key_cmp != NULL);

	htable->ht_feats = feats;
	htable->ht_bits	 = bits;
	htable->ht_ops	 = hops;
	htable->ht_priv	 = priv;

	D_ALLOC_ARRAY(buckets, nr);
	if (buckets == NULL)
		return -DER_NOMEM;

	for (i = 0; i < nr; i++)
		D_INIT_LIST_HEAD(&buckets[i].hb_head);

	htable->ht_buckets = buckets;
	rc = ch_lock_init(htable);
	if (rc != 0) {
		D_FREE(buckets);
		return rc;
	}

	return 0;
}

int
d_hash_table_create(uint32_t feats, unsigned int bits, void *priv,
		    d_hash_table_ops_t *hops,
		    struct d_hash_table **htable_pp)
{
	struct d_hash_table *htable;
	int		    rc;

	D_ALLOC_PTR(htable);
	if (htable == NULL)
		return -DER_NOMEM;

	rc = d_hash_table_create_inplace(feats, bits, priv, hops, htable);
	if (rc != 0)
		goto failed;

	*htable_pp = htable;
	return 0;
 failed:
	D_FREE_PTR(htable);
	return rc;
}

int
d_hash_table_traverse(struct d_hash_table *htable, d_hash_traverse_cb_t cb,
		      void *arg)
{
	struct d_hash_bucket	*buckets = htable->ht_buckets;
	d_list_t		*rlink;

	int			 nr;
	int			 i;
	int			 rc = 0;

	if (buckets == NULL) {
		D_ERROR("d_hash_table %p not initialized (NULL buckets).\n",
			htable);
		D_GOTO(out, rc = -DER_UNINIT);
	}
	if (cb == NULL) {
		D_ERROR("invalid parameter, NULL cb.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	ch_lock(htable, true);

	nr = 1U << htable->ht_bits;
	for (i = 0; i < nr; i++) {
		d_list_for_each(rlink, &buckets[i].hb_head) {
			rc = cb(rlink, arg);
			if (rc != 0)
				D_GOTO(unlock, 0);
		}
	}

unlock:
	ch_unlock(htable, true);
out:
	return rc;
}

static bool
d_hash_table_is_empty(struct d_hash_table *htable)
{
	struct d_hash_bucket    *buckets = htable->ht_buckets;
	int			 nr;
	int			 i;
	bool			 res = true;

	if (buckets == NULL) {
		D_ERROR("d_hash_table %p not initialized (NULL buckets).\n",
			htable);
		return true;
	}

	ch_lock(htable, true);

	nr = 1U << htable->ht_bits;
	for (i = 0; i < nr; i++)
		if (!d_list_empty(&buckets[i].hb_head)) {
			res = false;
			break;
		}

	ch_unlock(htable, true);

	return res;
}

int
d_hash_table_destroy_inplace(struct d_hash_table *htable, bool force)
{
	struct d_hash_bucket	*buckets = htable->ht_buckets;
	int			 nr;
	int			 i;

	if (buckets == NULL)
		goto out;

	nr = 1U << htable->ht_bits;
	for (i = 0; i < nr; i++) {
		while (!d_list_empty(&buckets[i].hb_head)) {
			if (!force) {
				D_DEBUG(DB_TRACE, "Warning, non-empty hash\n");
				return -DER_BUSY;
			}
			d_hash_rec_delete_at(htable, buckets[i].hb_head.next);
		}
	}
	D_FREE(buckets);
	ch_lock_fini(htable);
 out:
	memset(htable, 0, sizeof(*htable));
	return 0;
}

int
d_hash_table_destroy(struct d_hash_table *htable, bool force)
{
	int rc = d_hash_table_destroy_inplace(htable, force);

	if (rc != 0)
		return rc;

	D_FREE_PTR(htable);
	return 0;
}

void
d_hash_table_debug(struct d_hash_table *htable)
{
#if D_HASH_DEBUG
	D_DEBUG(DB_TRACE, "max nr: %d, cur nr: %d, max_dep: %d\n",
		htable->ht_nr_max, htable->ht_nr, htable->ht_dep_max);
#endif
}

/******************************************************************************
 * DAOS Handle Hash Table Wrapper
 *
 * Note: These functions are not thread-safe because reference counting
 * operations are not internally lock-protected. The user must add their own
 * locking.
 ******************************************************************************/

struct d_hhash {
	uint64_t                ch_cookie;
	struct d_hash_table	ch_htable;
	/* server-side uses D_HTYPE_PTR handle */
	bool			ch_ptrtype;
};

static struct d_rlink*
link2rlink(d_list_t *link)
{
	D_ASSERT(link != NULL);
	return container_of(link, struct d_rlink, rl_link);
}

static void
rlink_op_addref(struct d_rlink *rlink)
{
	rlink->rl_ref++;
}

static bool
rlink_op_decref(struct d_rlink *rlink)
{
	D_ASSERT(rlink->rl_ref > 0);
	rlink->rl_ref--;

	return rlink->rl_ref == 0;
}

static void
rlink_op_init(struct d_rlink *rlink)
{
	D_INIT_LIST_HEAD(&rlink->rl_link);
	rlink->rl_initialized	= 1;
	rlink->rl_ref		= 1; /* for caller */
}


static bool
rlink_op_empty(struct d_rlink *rlink)
{
	if (!rlink->rl_initialized)
		return 1;
	D_ASSERT(rlink->rl_ref != 0 || d_hash_rec_unlinked(&rlink->rl_link));
	return d_hash_rec_unlinked(&rlink->rl_link);
}

static struct d_hlink *
hh_link2ptr(d_list_t *link)
{
	struct d_rlink	*rlink;

	rlink = link2rlink(link);
	return	container_of(rlink, struct d_hlink, hl_link);
}

static void
hh_op_key_init(struct d_hash_table *hhtab, d_list_t *rlink, void *arg)
{
	struct d_hhash *dht;
	struct d_hlink *hlink = hh_link2ptr(rlink);
	int		   type = *(int *)arg;

	dht = container_of(hhtab, struct d_hhash, ch_htable);
	hlink->hl_key = ((dht->ch_cookie++) << D_HTYPE_BITS) | type;
}

static int
hh_op_key_get(struct d_hash_table *hhtab, d_list_t *rlink, void **key_pp)
{
	struct d_hlink *hlink = hh_link2ptr(rlink);

	*key_pp = (void *)&hlink->hl_key;
	return sizeof(hlink->hl_key);
}

static uint32_t
hh_op_key_hash(struct d_hash_table *hhtab, const void *key, unsigned int ksize)
{
	D_ASSERT(ksize == sizeof(uint64_t));

	return (unsigned int)(*(const uint64_t *)key >> D_HTYPE_BITS);
}

static bool
hh_op_key_cmp(struct d_hash_table *hhtab, d_list_t *link,
	  const void *key, unsigned int ksize)
{
	struct d_hlink *hlink = hh_link2ptr(link);

	D_ASSERT(ksize == sizeof(uint64_t));
	return hlink->hl_key == *(uint64_t *)key;
}

static void
hh_op_rec_addref(struct d_hash_table *hhtab, d_list_t *link)
{
	rlink_op_addref(link2rlink(link));
}

static bool
hh_op_rec_decref(struct d_hash_table *hhtab, d_list_t *link)
{
	return rlink_op_decref(link2rlink(link));
}

static void
hh_op_rec_free(struct d_hash_table *hhtab, d_list_t *link)
{
	struct d_hlink *hlink = hh_link2ptr(link);

	if (hlink->hl_ops != NULL &&
	    hlink->hl_ops->hop_free != NULL)
		hlink->hl_ops->hop_free(hlink);
}

static d_hash_table_ops_t hh_ops = {
	.hop_key_init		= hh_op_key_init,
	.hop_key_get		= hh_op_key_get,
	.hop_key_hash		= hh_op_key_hash,
	.hop_key_cmp		= hh_op_key_cmp,
	.hop_rec_addref		= hh_op_rec_addref,
	.hop_rec_decref		= hh_op_rec_decref,
	.hop_rec_free		= hh_op_rec_free,
};

int
d_hhash_create(unsigned int bits, struct d_hhash **htable_pp)
{
	struct d_hhash		*hhtab;
	int			 rc;

	D_ALLOC_PTR(hhtab);
	if (hhtab == NULL)
		return -DER_NOMEM;

	rc = d_hash_table_create_inplace(0, bits, NULL, &hh_ops,
					 &hhtab->ch_htable);
	if (rc != 0)
		goto failed;

	hhtab->ch_cookie = 1;
	hhtab->ch_ptrtype = false;
	*htable_pp = hhtab;
	return 0;
 failed:
	D_FREE_PTR(hhtab);
	return rc;
}

void
d_hhash_destroy(struct d_hhash *hhtab)
{
	d_hash_table_debug(&hhtab->ch_htable);
	d_hash_table_destroy_inplace(&hhtab->ch_htable, true);
	D_FREE_PTR(hhtab);
}

int
d_hhash_set_ptrtype(struct d_hhash *hhtab)
{
	if (!d_hash_table_is_empty(&hhtab->ch_htable) &&
	    hhtab->ch_ptrtype == false) {
		D_ERROR("d_hash_table %p not empty with non-ptr objects.\n",
			&hhtab->ch_htable);
		return -DER_ALREADY;
	}

	hhtab->ch_ptrtype = true;
	return 0;
}

bool
d_hhash_is_ptrtype(struct d_hhash *hhtab)
{
	return hhtab->ch_ptrtype;
}

void
d_hhash_hlink_init(struct d_hlink *hlink, struct d_hlink_ops *ops)
{
	hlink->hl_ops = ops;
	rlink_op_init(&hlink->hl_link);
}

bool
d_uhash_link_empty(struct d_ulink *ulink)
{
	return rlink_op_empty(&ulink->ul_link);
}

void
d_hhash_link_insert(struct d_hhash *hhtab, struct d_hlink *hlink, int type)
{
	D_ASSERT(hlink->hl_link.rl_initialized);

	/* check if handle type fits in allocated bits */
	D_ASSERTF(type < (1 << D_HTYPE_BITS),
		  "Type (%d) does not fit in D_HTYPE_BITS (%d)\n",
		  type, D_HTYPE_BITS);

	if (d_hhash_is_ptrtype(hhtab)) {
		uint64_t ptr_key = (uintptr_t)hlink;

		D_ASSERTF(type == D_HTYPE_PTR, "direct/ptr-based htable can "
			  "only contain D_HTYPE_PTR type entries");
		D_ASSERTF(d_hhash_key_isptr(ptr_key), "hlink ptr %p is invalid "
			  "D_HTYPE_PTR type", hlink);
		ch_lock(&hhtab->ch_htable, false);
		ch_rec_addref(&hhtab->ch_htable, &hlink->hl_link.rl_link);
		hlink->hl_key = ptr_key;
		ch_unlock(&hhtab->ch_htable, false);

	} else {
		D_ASSERTF(type != D_HTYPE_PTR, "PTR type key being inserted "
			  "in a non ptr-based htable.\n");
		d_hash_rec_insert_anonym(&hhtab->ch_htable,
			&hlink->hl_link.rl_link, (void *)&type);
	}
}

static inline struct d_hlink*
d_hlink_find(struct d_hash_table *htable, void *key, size_t size)
{
	d_list_t		*link;

	link = d_hash_rec_find(htable, key, size);
	return link == NULL ? NULL : hh_link2ptr(link);
}

bool
d_hhash_key_isptr(uint64_t key)
{
	return ((key & 0x1) == 0);
}

struct d_hlink *
d_hhash_link_lookup(struct d_hhash *hhtab, uint64_t key)
{
	if (d_hhash_key_isptr(key)) {
		struct d_hlink *hlink;

		if (!d_hhash_is_ptrtype(hhtab)) {
			D_ERROR("invalid PTR type key being lookup in a "
				"non ptr-based htable.\n");
			return NULL;
		}
		hlink = (struct d_hlink *)key;
		if (hlink->hl_key != key) {
			D_ERROR("invalid PTR type key.\n");
			return NULL;
		}

		ch_lock(&hhtab->ch_htable, true);
		ch_rec_addref(&hhtab->ch_htable, &hlink->hl_link.rl_link);
		ch_unlock(&hhtab->ch_htable, true);

		return hlink;
	} else {
		return d_hlink_find(&hhtab->ch_htable, (void *)&key,
				    sizeof(key));
	}
}

bool
d_hhash_link_delete(struct d_hhash *hhtab, struct d_hlink *hlink)
{
	if (d_hhash_key_isptr(hlink->hl_key)) {
		if (!d_hhash_is_ptrtype(hhtab)) {
			D_ERROR("invalid PTR type key being lookup in a "
				"non ptr-based htable.\n");
			return false;
		}
		d_hhash_link_putref(hhtab, hlink);
		return true;
	} else {
		return d_hash_rec_delete_at(&hhtab->ch_htable,
					    &hlink->hl_link.rl_link);
	}
}

void
d_hhash_link_getref(struct d_hhash *hhtab, struct d_hlink *hlink)
{
	d_hash_rec_addref(&hhtab->ch_htable, &hlink->hl_link.rl_link);
}

void
d_hhash_link_putref(struct d_hhash *hhtab, struct d_hlink *hlink)
{
	d_hash_rec_decref(&hhtab->ch_htable, &hlink->hl_link.rl_link);
}

bool
d_hhash_link_empty(struct d_hlink *hlink)
{
	return rlink_op_empty(&hlink->hl_link);
}

void
d_hhash_link_key(struct d_hlink *hlink, uint64_t *key)
{
	*key = hlink->hl_key;
}

int d_hhash_key_type(uint64_t key)
{
	if (d_hhash_key_isptr(key))
		return D_HTYPE_PTR;
	else
		return key & D_HTYPE_MASK;
}

/******************************************************************************
 * UUID Hash Table Wrapper
 * Key: UUID
 * Value: generic pointer
 *
 * Note: These functions are not thread-safe because reference counting
 * operations are not internally lock-protected. The user must add their own
 * locking.
 ******************************************************************************/

struct d_uhash_bundle {
	struct d_uuid	*key;
	/* additional args for comparison function */
	void		*cmp_args;
};

static struct d_ulink *
uh_link2ptr(d_list_t *link)
{
	struct d_rlink	*rlink;

	rlink = link2rlink(link);
	return container_of(rlink, struct d_ulink, ul_link);
}

static unsigned int
uh_op_key_hash(struct d_hash_table *uhtab, const void *key, unsigned int ksize)
{
	struct d_uhash_bundle	*uhbund	= (struct d_uhash_bundle *)key;
	struct d_uuid		*lkey	= uhbund->key;

	D_ASSERT(ksize == sizeof(struct d_uhash_bundle));
	D_DEBUG(DB_TRACE, "uuid_key: "CF_UUID"\n", CP_UUID(lkey->uuid));

	return (unsigned int)(d_hash_string_u32((const char *)lkey->uuid,
						   sizeof(uuid_t)));
}

static bool
uh_op_key_cmp(struct d_hash_table *uhtab, d_list_t *link, const void *key,
	      unsigned int ksize)
{
	struct d_ulink		*ulink	= uh_link2ptr(link);
	struct d_uhash_bundle	*uhbund	= (struct d_uhash_bundle *)key;
	struct d_uuid		*lkey	= (struct d_uuid *)uhbund->key;
	bool			res	= true;

	D_ASSERT(ksize == sizeof(struct d_uhash_bundle));
	D_DEBUG(DB_TRACE, "Link key, Key:"CF_UUID","CF_UUID"\n",
		CP_UUID(lkey->uuid),
		CP_UUID(ulink->ul_uuid.uuid));

	res = ((uuid_compare(ulink->ul_uuid.uuid, lkey->uuid)) == 0);
	if (res && ulink->ul_ops != NULL &&
	    ulink->ul_ops->uop_cmp != NULL)
		res = ulink->ul_ops->uop_cmp(ulink, uhbund->cmp_args);

	return res;
}

static void
uh_op_rec_free(struct d_hash_table *hhtab, d_list_t *link)
{
	struct d_ulink *ulink = uh_link2ptr(link);

	if (ulink->ul_ops != NULL &&
	    ulink->ul_ops->uop_free != NULL)
		ulink->ul_ops->uop_free(ulink);
}



static d_hash_table_ops_t uh_ops = {
	.hop_key_hash	= uh_op_key_hash,
	.hop_key_cmp	= uh_op_key_cmp,
	.hop_rec_addref	= hh_op_rec_addref, /* Reuse hh_op_add/decref */
	.hop_rec_decref = hh_op_rec_decref,
	.hop_rec_free	= uh_op_rec_free,
};

int
d_uhash_create(int feats, unsigned int bits, struct d_hash_table **htable_pp)
{
	struct d_hash_table	*uhtab;
	int			rc;

	rc = d_hash_table_create(feats, bits, NULL, &uh_ops, &uhtab);
	if (rc != 0)
		goto failed;

	*htable_pp = uhtab;
	return 0;

failed:
	return -DER_NOMEM;
}

void
d_uhash_destroy(struct d_hash_table *uhtab)
{
	d_hash_table_debug(uhtab);
	d_hash_table_destroy(uhtab, true);
}

void
d_uhash_ulink_init(struct d_ulink *ulink, struct d_ulink_ops *ops)
{
	ulink->ul_ops = ops;
	rlink_op_init(&ulink->ul_link);
}

static inline struct d_ulink*
d_ulink_find(struct d_hash_table *htable, void *key, size_t size)
{
	d_list_t		*link;

	link = d_hash_rec_find(htable, key, size);
	return link == NULL ? NULL : uh_link2ptr(link);
}

struct d_ulink*
d_uhash_link_lookup(struct d_hash_table *uhtab, struct d_uuid *key,
		    void *cmp_args)
{
	struct d_uhash_bundle	uhbund;

	uhbund.key	= key;
	uhbund.cmp_args	= cmp_args;
	return d_ulink_find(uhtab, (void *)(&uhbund),
			    sizeof(struct d_uhash_bundle));
}

void
d_uhash_link_addref(struct d_hash_table *uhtab, struct d_ulink *ulink)
{
	d_hash_rec_addref(uhtab, &ulink->ul_link.rl_link);
}

void
d_uhash_link_putref(struct d_hash_table *uhtab, struct d_ulink *ulink)
{
	d_hash_rec_decref(uhtab, &ulink->ul_link.rl_link);
}

int
d_uhash_link_insert(struct d_hash_table *uhtab, struct d_uuid *key,
		    void *cmp_args, struct d_ulink *ulink)
{
	int			rc = 0;
	struct d_uhash_bundle	uhbund;

	D_ASSERT(ulink->ul_link.rl_initialized);

	uuid_copy(ulink->ul_uuid.uuid, key->uuid);
	uhbund.key	= key;
	uhbund.cmp_args = cmp_args;

	rc = d_hash_rec_insert(uhtab, (void *)(&uhbund),
			       sizeof(struct d_uhash_bundle),
			       &ulink->ul_link.rl_link, true);

	if (rc)
		D_ERROR("Error Inserting handle in UUID in-memory hash\n");

	return rc;
}

bool
d_uhash_link_last_ref(struct d_ulink *ulink)
{
	return ulink->ul_link.rl_ref == 1;
}

void
d_uhash_link_delete(struct d_hash_table *uhtab, struct d_ulink *ulink)
{
	d_hash_rec_delete_at(uhtab, &ulink->ul_link.rl_link);
}
