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
 * This file is part of cart, it implements the hash table functions.
 */
#define D_LOGFAC	DD_FAC(mem)

#include <pthread.h>
#include <gurt/common.h>
#include <gurt/list.h>
#include <gurt/hash.h>

enum d_hash_lru {
	D_HASH_LRU_TAIL = -1,
	D_HASH_LRU_NONE =  0,
	D_HASH_LRU_HEAD =  1,
};

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
static __thread int  thread_uuid_str_buf_idx;

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
	int	i;

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

/**
 * Lock the hash table
 *
 * Note: if hash table is using rwlock, it only takes read lock for
 * reference-only operations and caller should protect refcount.
 * see D_HASH_FT_RWLOCK for the details.
 */
static inline void
ch_bucket_lock(struct d_hash_table *htable, uint32_t idx, bool read_only)
{
	union d_hash_lock *lock;

	if (htable->ht_feats & D_HASH_FT_NOLOCK)
		return;

	lock = (htable->ht_feats & D_HASH_FT_GLOCK)
		? &htable->ht_lock : &htable->ht_locks[idx];
	if (htable->ht_feats & D_HASH_FT_MUTEX) {
		D_MUTEX_LOCK(&lock->mutex);
	} else if (htable->ht_feats & D_HASH_FT_RWLOCK) {
		if (read_only)
			D_RWLOCK_RDLOCK(&lock->rwlock);
		else
			D_RWLOCK_WRLOCK(&lock->rwlock);
	} else {
		D_SPIN_LOCK(&lock->spin);
	}
}

/** unlock the hash table */
static inline void
ch_bucket_unlock(struct d_hash_table *htable, uint32_t idx, bool read_only)
{
	union d_hash_lock *lock;

	if (htable->ht_feats & D_HASH_FT_NOLOCK)
		return;

	lock = (htable->ht_feats & D_HASH_FT_GLOCK)
		? &htable->ht_lock : &htable->ht_locks[idx];
	if (htable->ht_feats & D_HASH_FT_MUTEX)
		D_MUTEX_UNLOCK(&lock->mutex);
	else if (htable->ht_feats & D_HASH_FT_RWLOCK)
		D_RWLOCK_UNLOCK(&lock->rwlock);
	else
		D_SPIN_UNLOCK(&lock->spin);
}

/**
 * wrappers for member functions.
 */

static inline bool
ch_key_cmp(struct d_hash_table *htable, d_list_t *link,
	   const void *key, unsigned int ksize)
{
	return htable->ht_ops->hop_key_cmp(htable, link, key, ksize);
}

static inline void
ch_key_init(struct d_hash_table *htable, d_list_t *link, void *arg)
{
	htable->ht_ops->hop_key_init(htable, link, arg);
}

/**
 * Convert key to hash bucket id.
 *
 * It calls DJB2 hash if no customized hash function is provided.
 */
static inline uint32_t
ch_key_hash(struct d_hash_table *htable, const void *key, unsigned int ksize)
{
	uint32_t idx;

	if (htable->ht_ops->hop_key_hash)
		idx = htable->ht_ops->hop_key_hash(htable, key, ksize);
	else
		idx = d_hash_string_u32((const char *)key, ksize);

	return idx & ((1U << htable->ht_bits) - 1);
}

static inline uint32_t
ch_rec_hash(struct d_hash_table *htable, d_list_t *link)
{
	uint32_t idx = 0;

	if (htable->ht_ops->hop_rec_hash)
		idx = htable->ht_ops->hop_rec_hash(htable, link);
	else
		D_ASSERT(htable->ht_feats &
			 (D_HASH_FT_NOLOCK | D_HASH_FT_GLOCK));

	return idx & ((1U << htable->ht_bits) - 1);
}

static inline void
ch_rec_addref(struct d_hash_table *htable, d_list_t *link)
{
	if (htable->ht_ops->hop_rec_addref)
		htable->ht_ops->hop_rec_addref(htable, link);
}

static inline bool
ch_rec_decref(struct d_hash_table *htable, d_list_t *link)
{
	return htable->ht_ops->hop_rec_decref ?
	       htable->ht_ops->hop_rec_decref(htable, link) : false;
}

static inline void
ch_rec_free(struct d_hash_table *htable, d_list_t *link)
{
	if (htable->ht_ops->hop_rec_free)
		htable->ht_ops->hop_rec_free(htable, link);
}

static inline void
ch_rec_insert(struct d_hash_table *htable, struct d_hash_bucket *bucket,
	      d_list_t *link)
{
	d_list_add(link, &bucket->hb_head);
#if D_HASH_DEBUG
	htable->ht_nr++;
	if (htable->ht_nr > htable->ht_nr_max)
		htable->ht_nr_max = htable->ht_nr;

	if (htable->ht_ops->hop_rec_hash) {
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

/**
 * Insert the record into the hash table and take refcount on it if
 * "ephemeral" is not set.
 */
static inline void
ch_rec_insert_addref(struct d_hash_table *htable, struct d_hash_bucket *bucket,
		     d_list_t *link)
{
	if (!(htable->ht_feats & D_HASH_FT_EPHEMERAL))
		ch_rec_addref(htable, link);

	ch_rec_insert(htable, bucket, link);
}

static inline void
ch_rec_delete(struct d_hash_table *htable, d_list_t *link)
{
	d_list_del_init(link);
#if D_HASH_DEBUG
	htable->ht_nr--;
	if (htable->ht_ops->hop_rec_hash) {
		struct d_hash_bucket *bucket;

		bucket = &htable->ht_buckets[ch_rec_hash(htable, link)];
		bucket->hb_dep--;
	}
#endif
}

/**
 * Delete the record from the hash table, it also releases refcount of it if
 * "ephemeral" is not set.
 */
static inline bool
ch_rec_del_decref(struct d_hash_table *htable, d_list_t *link)
{
	bool zombie = false;

	ch_rec_delete(htable, link);
	if (!(htable->ht_feats & D_HASH_FT_EPHEMERAL))
		zombie = ch_rec_decref(htable, link);

	return zombie;
}

static inline d_list_t *
ch_rec_find(struct d_hash_table *htable, struct d_hash_bucket *bucket,
	    const void *key, unsigned int ksize, enum d_hash_lru lru)
{
	d_list_t *link;
	bool lru_enabled = (htable->ht_feats & D_HASH_FT_LRU) &&
			   (lru != D_HASH_LRU_NONE);

	d_list_for_each(link, &bucket->hb_head) {
		if (ch_key_cmp(htable, link, key, ksize)) {
			if (lru_enabled) {
				if (lru == D_HASH_LRU_HEAD &&
				    link != bucket->hb_head.next)
					d_list_move(link, &bucket->hb_head);
				else if (lru == D_HASH_LRU_TAIL &&
					 link != bucket->hb_head.prev)
					d_list_move_tail(link,
							 &bucket->hb_head);
			}
			return link;
		}
	}

	return NULL;
}

bool
d_hash_rec_unlinked(d_list_t *link)
{
	return d_list_empty(link);
}

d_list_t *
d_hash_rec_find(struct d_hash_table *htable, const void *key,
		unsigned int ksize)
{
	struct d_hash_bucket	*bucket;
	d_list_t		*link;
	uint32_t		 idx;
	bool			 is_lru = (htable->ht_feats & D_HASH_FT_LRU);

	D_ASSERT(key != NULL && ksize != 0);
	idx = ch_key_hash(htable, key, ksize);
	bucket = &htable->ht_buckets[idx];

	ch_bucket_lock(htable, idx, !is_lru);

	link = ch_rec_find(htable, bucket, key, ksize, D_HASH_LRU_HEAD);
	if (link != NULL)
		ch_rec_addref(htable, link);

	ch_bucket_unlock(htable, idx, !is_lru);
	return link;
}

int
d_hash_rec_insert(struct d_hash_table *htable, const void *key,
		  unsigned int ksize, d_list_t *link, bool exclusive)
{
	struct d_hash_bucket	*bucket;
	d_list_t		*tmp;
	uint32_t		 idx;
	int			 rc = 0;

	D_ASSERT(key != NULL && ksize != 0);
	idx = ch_key_hash(htable, key, ksize);
	bucket = &htable->ht_buckets[idx];

	ch_bucket_lock(htable, idx, false);

	if (exclusive) {
		tmp = ch_rec_find(htable, bucket, key, ksize, D_HASH_LRU_NONE);
		if (tmp)
			D_GOTO(out_unlock, rc = -DER_EXIST);
	}
	ch_rec_insert_addref(htable, bucket, link);

out_unlock:
	ch_bucket_unlock(htable, idx, false);
	return rc;
}

d_list_t *
d_hash_rec_find_insert(struct d_hash_table *htable, const void *key,
		       unsigned int ksize, d_list_t *link)
{
	struct d_hash_bucket	*bucket;
	d_list_t		*tmp;
	uint32_t		 idx;

	D_ASSERT(key != NULL && ksize != 0);
	idx = ch_key_hash(htable, key, ksize);
	bucket = &htable->ht_buckets[idx];

	ch_bucket_lock(htable, idx, false);

	tmp = ch_rec_find(htable, bucket, key, ksize, D_HASH_LRU_HEAD);
	if (tmp) {
		ch_rec_addref(htable, tmp);
		link = tmp;
		D_GOTO(out_unlock, 0);
	}
	ch_rec_insert_addref(htable, bucket, link);

out_unlock:
	ch_bucket_unlock(htable, idx, false);
	return link;
}

int
d_hash_rec_insert_anonym(struct d_hash_table *htable, d_list_t *link,
			 void *arg)
{
	struct d_hash_bucket	*bucket;
	uint32_t	 idx;
	uint32_t	 nr = 1U << htable->ht_bits;
	bool		 need_lock = !(htable->ht_feats & D_HASH_FT_NOLOCK);

	if (htable->ht_ops->hop_key_init == NULL)
		return -DER_INVAL;

	if (need_lock) {
		/* Lock all buckets because of unknown key yet */
		for (idx = 0; idx < nr; idx++) {
			ch_bucket_lock(htable, idx, false);
			if (htable->ht_feats & D_HASH_FT_GLOCK)
				break;
		}
	}

	/* has no key, hash table should have provided key generator */
	ch_key_init(htable, link, arg);

	idx = ch_rec_hash(htable, link);
	bucket = &htable->ht_buckets[idx];
	ch_rec_insert_addref(htable, bucket, link);

	if (need_lock) {
		for (idx = 0; idx < nr; idx++) {
			ch_bucket_unlock(htable, idx, false);
			if (htable->ht_feats & D_HASH_FT_GLOCK)
				break;
		}
	}
	return 0;
}

bool
d_hash_rec_delete(struct d_hash_table *htable, const void *key,
		  unsigned int ksize)
{
	struct d_hash_bucket	*bucket;
	d_list_t		*link;
	uint32_t		 idx;
	bool			 deleted = false;
	bool			 zombie  = false;

	D_ASSERT(key != NULL && ksize != 0);
	idx = ch_key_hash(htable, key, ksize);
	bucket = &htable->ht_buckets[idx];

	ch_bucket_lock(htable, idx, false);

	link = ch_rec_find(htable, bucket, key, ksize, D_HASH_LRU_NONE);
	if (link != NULL) {
		zombie  = ch_rec_del_decref(htable, link);
		deleted = true;
	}

	ch_bucket_unlock(htable, idx, false);

	if (zombie)
		ch_rec_free(htable, link);
	return deleted;
}

bool
d_hash_rec_delete_at(struct d_hash_table *htable, d_list_t *link)
{
	uint32_t idx = 0;
	bool	 deleted = false;
	bool	 zombie  = false;
	bool	 need_lock = !(htable->ht_feats & D_HASH_FT_NOLOCK);

	if (need_lock) {
		idx = ch_rec_hash(htable, link);
		ch_bucket_lock(htable, idx, false);
	}

	if (!d_list_empty(link)) {
		zombie  = ch_rec_del_decref(htable, link);
		deleted = true;
	}

	if (need_lock)
		ch_bucket_unlock(htable, idx, false);

	if (zombie)
		ch_rec_free(htable, link);
	return deleted;
}

bool
d_hash_rec_evict(struct d_hash_table *htable, const void *key,
		 unsigned int ksize)
{
	struct d_hash_bucket	*bucket;
	d_list_t		*link;
	uint32_t		 idx;

	if (!(htable->ht_feats & D_HASH_FT_LRU))
		return false;

	D_ASSERT(key != NULL && ksize != 0);
	idx = ch_key_hash(htable, key, ksize);
	bucket = &htable->ht_buckets[idx];

	ch_bucket_lock(htable, idx, false);

	link = ch_rec_find(htable, bucket, key, ksize, D_HASH_LRU_TAIL);

	ch_bucket_unlock(htable, idx, false);
	return link != NULL;
}

bool
d_hash_rec_evict_at(struct d_hash_table *htable, d_list_t *link)
{
	struct d_hash_bucket	*bucket;
	uint32_t		 idx;
	bool			 evicted = false;

	if (!(htable->ht_feats & D_HASH_FT_LRU))
		return false;

	idx = ch_rec_hash(htable, link);
	bucket = &htable->ht_buckets[idx];

	ch_bucket_lock(htable, idx, false);

	if (link != bucket->hb_head.prev) {
		d_list_move_tail(link, &bucket->hb_head);
		evicted = true;
	}

	ch_bucket_unlock(htable, idx, false);
	return evicted;
}

void
d_hash_rec_addref(struct d_hash_table *htable, d_list_t *link)
{
	uint32_t idx = 0;
	bool	 need_lock = !(htable->ht_feats & D_HASH_FT_NOLOCK);

	if (need_lock) {
		idx = ch_rec_hash(htable, link);
		ch_bucket_lock(htable, idx, true);
	}

	ch_rec_addref(htable, link);

	if (need_lock)
		ch_bucket_unlock(htable, idx, true);
}

void
d_hash_rec_decref(struct d_hash_table *htable, d_list_t *link)
{
	uint32_t idx = 0;
	bool	 need_lock = !(htable->ht_feats & D_HASH_FT_NOLOCK);
	bool	 ephemeral = (htable->ht_feats & D_HASH_FT_EPHEMERAL);
	bool	 zombie;

	if (need_lock) {
		idx = ch_rec_hash(htable, link);
		ch_bucket_lock(htable, idx, !ephemeral);
	}

	zombie = ch_rec_decref(htable, link);
	if (zombie && ephemeral && !d_list_empty(link))
		ch_rec_delete(htable, link);

	D_ASSERT(!zombie || d_list_empty(link));

	if (need_lock)
		ch_bucket_unlock(htable, idx, !ephemeral);

	if (zombie)
		ch_rec_free(htable, link);
}

int
d_hash_rec_ndecref(struct d_hash_table *htable, int count, d_list_t *link)
{
	uint32_t idx = 0;
	bool	 need_lock = !(htable->ht_feats & D_HASH_FT_NOLOCK);
	bool	 ephemeral = (htable->ht_feats & D_HASH_FT_EPHEMERAL);
	bool	 zombie = false;
	int	 rc = 0;

	if (need_lock) {
		idx = ch_rec_hash(htable, link);
		ch_bucket_lock(htable, idx, !ephemeral);
	}

	if (htable->ht_ops->hop_rec_ndecref) {
		rc = htable->ht_ops->hop_rec_ndecref(htable, link, count);
		if (rc >= 1) {
			zombie = true;
			rc = 0;
		}
	} else {
		do {
			zombie = ch_rec_decref(htable, link);
		} while (--count && !zombie);

		if (count != 0)
			rc = -DER_INVAL;
	}

	if (rc == 0) {
		if (zombie && ephemeral && !d_list_empty(link))
			ch_rec_delete(htable, link);

		D_ASSERT(!zombie || d_list_empty(link));
	}

	if (need_lock)
		ch_bucket_unlock(htable, idx, !ephemeral);

	if (zombie)
		ch_rec_free(htable, link);
	return rc;
}

/* Find an entry in the hash table.
 *
 * As d_hash_table_traverse() does not support removal from the callback
 * function save a pointer in *arg and return 1 to terminate the traverse.
 * This way we can iterate over the entries in the hash table and delete every
 * one.
 */
static inline int
d_hash_find_single(d_list_t *link, void *arg)
{
	d_list_t **p = arg;

	*p = link;
	return 1;
}

d_list_t *
d_hash_rec_first(struct d_hash_table *htable)
{
	d_list_t	*link = NULL;
	int		 rc;

	rc = d_hash_table_traverse(htable, d_hash_find_single, &link);
	if (rc < 0)
		return NULL;

	return link;
}

int
d_hash_table_create_inplace(uint32_t feats, uint32_t bits, void *priv,
			    d_hash_table_ops_t *hops,
			    struct d_hash_table *htable)
{
	uint32_t nr = (1 << bits);
	uint32_t i;
	int	 rc = 0;

	D_ASSERT(hops != NULL);
	D_ASSERT(hops->hop_key_cmp != NULL);

	htable->ht_feats = feats;
	htable->ht_bits	 = bits;
	htable->ht_ops	 = hops;
	htable->ht_priv	 = priv;

	if (hops->hop_rec_hash == NULL && !(feats & D_HASH_FT_NOLOCK)) {
		htable->ht_feats |= D_HASH_FT_GLOCK;
		D_WARN("The d_hash_table_ops_t->hop_rec_hash() callback is "
			"not provided!\nTherefore the whole hash table locking "
			"will be used for backward compatibility.\n");
	}

	D_ALLOC_ARRAY(htable->ht_buckets, nr);
	if (htable->ht_buckets == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < nr; i++)
		D_INIT_LIST_HEAD(&htable->ht_buckets[i].hb_head);

	if (htable->ht_feats & D_HASH_FT_NOLOCK)
		D_GOTO(out, rc = 0);

	if (htable->ht_feats & D_HASH_FT_GLOCK) {
		if (htable->ht_feats & D_HASH_FT_MUTEX)
			rc = D_MUTEX_INIT(&htable->ht_lock.mutex, NULL);
		else if (htable->ht_feats & D_HASH_FT_RWLOCK)
			rc = D_RWLOCK_INIT(&htable->ht_lock.rwlock, NULL);
		else
			rc = D_SPIN_INIT(&htable->ht_lock.spin,
					 PTHREAD_PROCESS_PRIVATE);
		if (rc)
			D_GOTO(free_buckets, rc);
	} else {
		D_ALLOC_ARRAY(htable->ht_locks, nr);
		if (htable->ht_locks == NULL)
			D_GOTO(free_buckets, rc = -DER_NOMEM);

		for (i = 0; i < nr; i++) {
			if (htable->ht_feats & D_HASH_FT_MUTEX)
				rc = D_MUTEX_INIT(&htable->ht_locks[i].mutex,
						  NULL);
			else if (htable->ht_feats & D_HASH_FT_RWLOCK)
				rc = D_RWLOCK_INIT(&htable->ht_locks[i].rwlock,
						   NULL);
			else
				rc = D_SPIN_INIT(&htable->ht_locks[i].spin,
						 PTHREAD_PROCESS_PRIVATE);
			if (rc)
				D_GOTO(free_locks, rc);
		}
	}

	D_GOTO(out, rc = 0);

free_locks:
	while (i > 0) {
		i--; /* last successful */
		if (htable->ht_feats & D_HASH_FT_MUTEX)
			D_MUTEX_DESTROY(&htable->ht_locks[i].mutex);
		else if (htable->ht_feats & D_HASH_FT_RWLOCK)
			D_RWLOCK_DESTROY(&htable->ht_locks[i].rwlock);
		else
			D_SPIN_DESTROY(&htable->ht_locks[i].spin);
	}
	D_FREE(htable->ht_locks);
free_buckets:
	D_FREE(htable->ht_buckets);
out:
	return rc;
}

int
d_hash_table_create(uint32_t feats, uint32_t bits, void *priv,
		    d_hash_table_ops_t *hops,
		    struct d_hash_table **htable_pp)
{
	struct d_hash_table	*htable;
	int			 rc;

	D_ALLOC_PTR(htable);
	if (htable == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = d_hash_table_create_inplace(feats, bits, priv, hops, htable);
	if (rc)
		D_FREE(htable);
out:
	*htable_pp = htable;
	return rc;
}

int
d_hash_table_traverse(struct d_hash_table *htable, d_hash_traverse_cb_t cb,
		      void *arg)
{
	struct d_hash_bucket	*bucket;
	d_list_t		*link;
	uint32_t		 nr = 1U << htable->ht_bits;
	uint32_t		 idx;
	int			 rc = 0;

	if (htable->ht_buckets == NULL) {
		D_ERROR("d_hash_table %p not initialized (NULL buckets).\n",
			htable);
		D_GOTO(out, rc = -DER_UNINIT);
	}
	if (cb == NULL) {
		D_ERROR("invalid parameter, NULL cb.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	for (idx = 0; idx < nr && !rc; idx++) {
		bucket = &htable->ht_buckets[idx];
		ch_bucket_lock(htable, idx, true);
		d_list_for_each(link, &bucket->hb_head) {
			rc = cb(link, arg);
			if (rc)
				break;
		}
		ch_bucket_unlock(htable, idx, true);
	}
out:
	return rc;
}

static bool
d_hash_table_is_empty(struct d_hash_table *htable)
{
	uint32_t	 nr = 1U << htable->ht_bits;
	uint32_t	 idx;
	bool		 is_empty = true;

	if (htable->ht_buckets == NULL) {
		D_ERROR("d_hash_table %p not initialized (NULL buckets).\n",
			htable);
		D_GOTO(out, 0);
	}

	for (idx = 0; idx < nr && is_empty; idx++) {
		ch_bucket_lock(htable, idx, true);
		is_empty = d_list_empty(&htable->ht_buckets[idx].hb_head);
		ch_bucket_unlock(htable, idx, true);
	}

out:
	return is_empty;
}

int
d_hash_table_destroy_inplace(struct d_hash_table *htable, bool force)
{
	struct d_hash_bucket	*bucket;
	uint32_t		 nr = 1U << htable->ht_bits;
	uint32_t		 i;
	int			 rc = 0;

	for (i = 0; i < nr; i++) {
		bucket = &htable->ht_buckets[i];
		while (!d_list_empty(&bucket->hb_head)) {
			if (!force) {
				D_DEBUG(DB_TRACE, "Warning, non-empty hash\n");
				D_GOTO(out, rc = -DER_BUSY);
			}
			d_hash_rec_delete_at(htable, bucket->hb_head.next);
		}
	}

	if (htable->ht_feats & D_HASH_FT_NOLOCK)
		D_GOTO(free_buckets, rc = 0);

	if (htable->ht_feats & D_HASH_FT_GLOCK) {
		if (htable->ht_feats & D_HASH_FT_MUTEX)
			D_MUTEX_DESTROY(&htable->ht_lock.mutex);
		else if (htable->ht_feats & D_HASH_FT_RWLOCK)
			D_RWLOCK_DESTROY(&htable->ht_lock.rwlock);
		else
			D_SPIN_DESTROY(&htable->ht_lock.spin);
	} else {
		for (i = 0; i < nr; i++) {
			if (htable->ht_feats & D_HASH_FT_MUTEX)
				D_MUTEX_DESTROY(&htable->ht_locks[i].mutex);
			else if (htable->ht_feats & D_HASH_FT_RWLOCK)
				D_RWLOCK_DESTROY(&htable->ht_locks[i].rwlock);
			else
				D_SPIN_DESTROY(&htable->ht_locks[i].spin);
		}
		D_FREE(htable->ht_locks);
	}

free_buckets:
	D_FREE(htable->ht_buckets);
	memset(htable, 0, sizeof(*htable));
out:
	return rc;
}

int
d_hash_table_destroy(struct d_hash_table *htable, bool force)
{
	int rc = d_hash_table_destroy_inplace(htable, force);

	if (!rc)
		D_FREE(htable);
	return rc;
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
	uint64_t		ch_cookie;
	struct d_hash_table	ch_htable;
	/* server-side uses D_HTYPE_PTR handle */
	bool			ch_ptrtype;
};

static inline struct d_rlink*
link2rlink(d_list_t *link)
{
	D_ASSERT(link != NULL);
	return container_of(link, struct d_rlink, rl_link);
}

static void
rl_op_addref(struct d_rlink *rlink)
{
	rlink->rl_ref++;
}

static bool
rl_op_decref(struct d_rlink *rlink)
{
	D_ASSERT(rlink->rl_ref > 0);
	rlink->rl_ref--;
	return rlink->rl_ref == 0;
}

static void
rl_op_init(struct d_rlink *rlink)
{
	D_INIT_LIST_HEAD(&rlink->rl_link);
	rlink->rl_initialized	= 1;
	rlink->rl_ref		= 1; /* for caller */
}

static bool
rl_op_empty(struct d_rlink *rlink)
{
	bool is_unlinked;

	if (!rlink->rl_initialized)
		return true;

	is_unlinked = d_hash_rec_unlinked(&rlink->rl_link);
	D_ASSERT(rlink->rl_ref != 0 || is_unlinked);
	return is_unlinked;
}

static inline struct d_hlink *
link2hlink(d_list_t *link)
{
	struct d_rlink	*rlink = link2rlink(link);

	return container_of(rlink, struct d_hlink, hl_link);
}

static void
hh_op_key_init(struct d_hash_table *htable, d_list_t *link, void *arg)
{
	struct d_hhash	*hhash;
	struct d_hlink	*hlink = link2hlink(link);
	int		 type  = *(int *)arg;

	hhash = container_of(htable, struct d_hhash, ch_htable);
	hlink->hl_key = ((hhash->ch_cookie++) << D_HTYPE_BITS)
			| (type & D_HTYPE_MASK);
}

static uint32_t
hh_op_key_hash(struct d_hash_table *htable, const void *key, unsigned int ksize)
{
	D_ASSERT(ksize == sizeof(uint64_t));

	return (uint32_t)(*(const uint64_t *)key >> D_HTYPE_BITS);
}

static bool
hh_op_key_cmp(struct d_hash_table *htable, d_list_t *link,
	      const void *key, unsigned int ksize)
{
	struct d_hlink	*hlink = link2hlink(link);

	D_ASSERT(ksize == sizeof(uint64_t));

	return hlink->hl_key == *(uint64_t *)key;
}

static uint32_t
hh_op_rec_hash(struct d_hash_table *htable, d_list_t *link)
{
	struct d_hlink	*hlink = link2hlink(link);

	return (uint32_t)(hlink->hl_key >> D_HTYPE_BITS);
}

static void
hh_op_rec_addref(struct d_hash_table *htable, d_list_t *link)
{
	rl_op_addref(link2rlink(link));
}

static bool
hh_op_rec_decref(struct d_hash_table *htable, d_list_t *link)
{
	return rl_op_decref(link2rlink(link));
}

static void
hh_op_rec_free(struct d_hash_table *htable, d_list_t *link)
{
	struct d_hlink	*hlink = link2hlink(link);

	if (hlink->hl_ops != NULL && hlink->hl_ops->hop_free != NULL)
		hlink->hl_ops->hop_free(hlink);
}

static d_hash_table_ops_t hh_ops = {
	.hop_key_init		= hh_op_key_init,
	.hop_key_hash		= hh_op_key_hash,
	.hop_key_cmp		= hh_op_key_cmp,
	.hop_rec_hash		= hh_op_rec_hash,
	.hop_rec_addref		= hh_op_rec_addref,
	.hop_rec_decref		= hh_op_rec_decref,
	.hop_rec_free		= hh_op_rec_free,
};

int
d_hhash_create(uint32_t feats, uint32_t bits, struct d_hhash **hhash_pp)
{
	struct d_hhash	*hhash;
	int		 rc = 0;

	D_ALLOC_PTR(hhash);
	if (hhash == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = d_hash_table_create_inplace(feats, bits, NULL, &hh_ops,
					 &hhash->ch_htable);
	if (rc) {
		D_FREE(hhash);
		D_GOTO(out, rc);
	}

	hhash->ch_cookie  = 1ULL;
	hhash->ch_ptrtype = false;
out:
	*hhash_pp = hhash;
	return rc;
}

void
d_hhash_destroy(struct d_hhash *hhash)
{
	d_hash_table_debug(&hhash->ch_htable);
	d_hash_table_destroy_inplace(&hhash->ch_htable, true);
	D_FREE(hhash);
}

int
d_hhash_set_ptrtype(struct d_hhash *hhash)
{
	if (!d_hash_table_is_empty(&hhash->ch_htable) &&
	    hhash->ch_ptrtype == false) {
		D_ERROR("d_hash_table %p not empty with non-ptr objects.\n",
			&hhash->ch_htable);
		return -DER_ALREADY;
	}

	hhash->ch_ptrtype = true;
	return 0;
}

bool
d_hhash_is_ptrtype(struct d_hhash *hhash)
{
	return hhash->ch_ptrtype;
}

void
d_hhash_hlink_init(struct d_hlink *hlink, struct d_hlink_ops *hl_ops)
{
	hlink->hl_ops = hl_ops;
	rl_op_init(&hlink->hl_link);
}

bool
d_uhash_link_empty(struct d_ulink *ulink)
{
	return rl_op_empty(&ulink->ul_link);
}

void
d_hhash_link_insert(struct d_hhash *hhash, struct d_hlink *hlink, int type)
{
	bool need_lock = !(hhash->ch_htable.ht_feats & D_HASH_FT_NOLOCK);

	D_ASSERT(hlink->hl_link.rl_initialized);

	/* check if handle type fits in allocated bits */
	D_ASSERTF(type < (1 << D_HTYPE_BITS),
		  "Type (%d) does not fit in D_HTYPE_BITS (%d)\n",
		  type, D_HTYPE_BITS);

	if (d_hhash_is_ptrtype(hhash)) {
		uint64_t ptr_key = (uintptr_t)hlink;
		uint32_t nr = 1U << hhash->ch_htable.ht_bits;
		uint32_t idx = 0;

		D_ASSERTF(type == D_HTYPE_PTR, "direct/ptr-based htable can "
			  "only contain D_HTYPE_PTR type entries");
		D_ASSERTF(d_hhash_key_isptr(ptr_key), "hlink ptr %p is invalid "
			  "D_HTYPE_PTR type", hlink);

		if (need_lock) {
			/* Lock all buckets to emulate proper hlink lock */
			for (idx = 0; idx < nr; idx++) {
				ch_bucket_lock(&hhash->ch_htable, idx, false);
				if (hhash->ch_htable.ht_feats & D_HASH_FT_GLOCK)
					break;
			}
		}

		ch_rec_addref(&hhash->ch_htable, &hlink->hl_link.rl_link);
		hlink->hl_key = ptr_key;

		if (need_lock) {
			for (idx = 0; idx < nr; idx++) {
				ch_bucket_unlock(&hhash->ch_htable, idx, false);
				if (hhash->ch_htable.ht_feats & D_HASH_FT_GLOCK)
					break;
			}
		}
	} else {
		D_ASSERTF(type != D_HTYPE_PTR, "PTR type key being inserted "
			  "in a non ptr-based htable.\n");

		d_hash_rec_insert_anonym(&hhash->ch_htable,
			&hlink->hl_link.rl_link, (void *)&type);
	}
}

static inline struct d_hlink*
d_hlink_find(struct d_hash_table *htable, const void *key, unsigned int ksize)
{
	d_list_t *link = d_hash_rec_find(htable, key, ksize);

	return link ? link2hlink(link) : NULL;
}

bool
d_hhash_key_isptr(uint64_t key)
{
	return ((key & 0x1) == 0);
}

struct d_hlink *
d_hhash_link_lookup(struct d_hhash *hhash, uint64_t key)
{
	if (d_hhash_key_isptr(key)) {
		struct d_hlink *hlink = (struct d_hlink *)key;

		if (!d_hhash_is_ptrtype(hhash)) {
			D_ERROR("invalid PTR type key being lookup in a "
				"non ptr-based htable.\n");
			return NULL;
		}
		if (hlink->hl_key != key) {
			D_ERROR("invalid PTR type key.\n");
			return NULL;
		}

		d_hash_rec_addref(&hhash->ch_htable, &hlink->hl_link.rl_link);
		return hlink;
	} else {
		return d_hlink_find(&hhash->ch_htable, (void *)&key,
				    sizeof(key));
	}
}

bool
d_hhash_link_delete(struct d_hhash *hhash, struct d_hlink *hlink)
{
	if (d_hhash_key_isptr(hlink->hl_key)) {
		if (!d_hhash_is_ptrtype(hhash)) {
			D_ERROR("invalid PTR type key being lookup in a "
				"non ptr-based htable.\n");
			return false;
		}

		d_hhash_link_putref(hhash, hlink);
		return true;
	} else {
		return d_hash_rec_delete_at(&hhash->ch_htable,
					    &hlink->hl_link.rl_link);
	}
}

void
d_hhash_link_getref(struct d_hhash *hhash, struct d_hlink *hlink)
{
	d_hash_rec_addref(&hhash->ch_htable, &hlink->hl_link.rl_link);
}

void
d_hhash_link_putref(struct d_hhash *hhash, struct d_hlink *hlink)
{
	d_hash_rec_decref(&hhash->ch_htable, &hlink->hl_link.rl_link);
}

bool
d_hhash_link_empty(struct d_hlink *hlink)
{
	return rl_op_empty(&hlink->hl_link);
}

void
d_hhash_link_key(struct d_hlink *hlink, uint64_t *key)
{
	*key = hlink->hl_key;
}

int
d_hhash_key_type(uint64_t key)
{
	return d_hhash_key_isptr(key) ? D_HTYPE_PTR : key & D_HTYPE_MASK;
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

static inline struct d_ulink *
link2ulink(d_list_t *link)
{
	struct d_rlink	*rlink = link2rlink(link);

	return container_of(rlink, struct d_ulink, ul_link);
}

static void
uh_op_rec_addref(struct d_hash_table *htable, d_list_t *link)
{
	struct d_ulink		*ulink	= link2ulink(link);

	rl_op_addref(&ulink->ul_link);
}

static bool
uh_op_rec_decref(struct d_hash_table *htable, d_list_t *link)
{
	struct d_ulink		*ulink	= link2ulink(link);

	return rl_op_decref(&ulink->ul_link);
}

static uint32_t
uh_op_key_hash(struct d_hash_table *htable, const void *key, unsigned int ksize)
{
	struct d_uhash_bundle	*uhbund	= (struct d_uhash_bundle *)key;
	struct d_uuid		*lkey	= uhbund->key;
	uint32_t		*retp	= (uint32_t *)lkey->uuid;

	D_ASSERT(ksize == sizeof(struct d_uhash_bundle));
	D_DEBUG(DB_TRACE, "uuid_key: "CF_UUID"\n", CP_UUID(lkey->uuid));

	return *retp;
}

static uint32_t
uh_op_rec_hash(struct d_hash_table *htable, d_list_t *link)
{
	struct d_ulink		*ulink	= link2ulink(link);
	uint32_t		*retp	= (uint32_t *)ulink->ul_uuid.uuid;

	return *retp;
}

static bool
uh_op_key_cmp(struct d_hash_table *htable, d_list_t *link, const void *key,
	      unsigned int ksize)
{
	struct d_ulink		*ulink	= link2ulink(link);
	struct d_uhash_bundle	*uhbund	= (struct d_uhash_bundle *)key;
	struct d_uuid		*lkey	= (struct d_uuid *)uhbund->key;
	bool			 res;

	D_ASSERT(ksize == sizeof(struct d_uhash_bundle));
	D_DEBUG(DB_TRACE, "Link key, Key:"CF_UUID","CF_UUID"\n",
		CP_UUID(lkey->uuid),
		CP_UUID(ulink->ul_uuid.uuid));

	res = ((uuid_compare(ulink->ul_uuid.uuid, lkey->uuid)) == 0);
	if (res && ulink->ul_ops != NULL && ulink->ul_ops->uop_cmp != NULL)
		res = ulink->ul_ops->uop_cmp(ulink, uhbund->cmp_args);

	return res;
}

static void
uh_op_rec_free(struct d_hash_table *htable, d_list_t *link)
{
	struct d_ulink	*ulink = link2ulink(link);

	if (ulink->ul_ops != NULL && ulink->ul_ops->uop_free != NULL)
		ulink->ul_ops->uop_free(ulink);
}

static d_hash_table_ops_t uh_ops = {
	.hop_key_hash		= uh_op_key_hash,
	.hop_key_cmp		= uh_op_key_cmp,
	.hop_rec_hash		= uh_op_rec_hash,
	.hop_rec_addref		= uh_op_rec_addref,
	.hop_rec_decref		= uh_op_rec_decref,
	.hop_rec_free		= uh_op_rec_free,
};

int
d_uhash_create(uint32_t feats, uint32_t bits, struct d_hash_table **htable_pp)
{
	return d_hash_table_create(feats, bits, NULL, &uh_ops, htable_pp);
}

void
d_uhash_destroy(struct d_hash_table *htable)
{
	d_hash_table_debug(htable);
	d_hash_table_destroy(htable, true);
}

void
d_uhash_ulink_init(struct d_ulink *ulink, struct d_ulink_ops *ul_ops)
{
	ulink->ul_ops = ul_ops;
	rl_op_init(&ulink->ul_link);
}

static inline struct d_ulink*
d_ulink_find(struct d_hash_table *htable, void *key, unsigned int ksize)
{
	d_list_t *link = d_hash_rec_find(htable, key, ksize);

	return link ? link2ulink(link) : NULL;
}

struct d_ulink*
d_uhash_link_lookup(struct d_hash_table *htable, struct d_uuid *key,
		    void *cmp_args)
{
	struct d_uhash_bundle uhbund;

	uhbund.key	= key;
	uhbund.cmp_args	= cmp_args;

	return d_ulink_find(htable, (void *)&uhbund, sizeof(uhbund));
}

void
d_uhash_link_addref(struct d_hash_table *htable, struct d_ulink *ulink)
{
	d_hash_rec_addref(htable, &ulink->ul_link.rl_link);
}

void
d_uhash_link_putref(struct d_hash_table *htable, struct d_ulink *ulink)
{
	d_hash_rec_decref(htable, &ulink->ul_link.rl_link);
}

int
d_uhash_link_insert(struct d_hash_table *htable, struct d_uuid *key,
		    void *cmp_args, struct d_ulink *ulink)
{
	struct d_uhash_bundle	uhbund;
	int			rc = 0;

	D_ASSERT(ulink->ul_link.rl_initialized);

	uuid_copy(ulink->ul_uuid.uuid, key->uuid);
	uhbund.key	= key;
	uhbund.cmp_args = cmp_args;

	rc = d_hash_rec_insert(htable, (void *)&uhbund, sizeof(uhbund),
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
d_uhash_link_delete(struct d_hash_table *htable, struct d_ulink *ulink)
{
	d_hash_rec_delete_at(htable, &ulink->ul_link.rl_link);
}
