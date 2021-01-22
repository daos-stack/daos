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
 * This file is part of cart, it implements the dynamic hash table functions.
 */
#define D_LOGFAC	DD_FAC(mem)
#include <gurt/common.h>
#include <gurt/list.h>
#include <gurt/hash.h>
#include <gurt/dyn_hash.h>

#define DYNHASH_SIPBITS 6
#define DYNHASH_BUCKET	(1 << DYNHASH_SIPBITS)
#define DYNHASH_VECTOR	64
#define DYNHASH_MAGIC   0xab013245

#define _le64toh(x) (x)

typedef struct dh_field {
	uint64_t	siphash;
	void		*record;
} dh_field_t;

typedef struct dh_bucket {
	unsigned char	counter;
	pthread_mutex_t	mtx;
	dh_field_t	field[DYNHASH_BUCKET];
} dh_bucket_t;


#define ROTATE(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define HALF_ROUND(a, b, c, d, s, t) \
	a += b; c += d;                  \
	b = ROTATE(b, s) ^ a;            \
	d = ROTATE(d, t) ^ c;            \
	a = ROTATE(a, 32)

#define DOUBLE_ROUND(v0, v1, v2, v3)         \
	HALF_ROUND(v0, v1, v2, v3, 13, 16);      \
	HALF_ROUND(v2, v1, v0, v3, 17, 21);      \
	HALF_ROUND(v0, v1, v2, v3, 13, 16);      \
	HALF_ROUND(v2, v1, v0, v3, 17, 21)

const char keys[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8,
	   9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf };

/* SipHash computes 64-bit massage authentication from a variable message
 * length and 128-bit secret key. In dynamic hash SipHash is generated for
 * each unique record key as random hash key.
 */
static uint64_t
gen_siphash(const void *src, uint32_t src_sz)
{
	const uint64_t	*_key = (uint64_t *)keys;
	uint64_t	k0 = _le64toh(_key[0]);
	uint64_t	k1 = _le64toh(_key[1]);
	uint64_t	b = (uint64_t)src_sz << 56;
	const uint64_t	*in = (uint64_t *)src;

	uint64_t v0 = k0 ^ 0x736f6d6570736575ULL;
	uint64_t v1 = k1 ^ 0x646f72616e646f6dULL;
	uint64_t v2 = k0 ^ 0x6c7967656e657261ULL;
	uint64_t v3 = k1 ^ 0x7465646279746573ULL;

	while (src_sz >= 8) {
		uint64_t mi = _le64toh(*in);

		in += 1;
		src_sz -= 8;
		v3 ^= mi;
		DOUBLE_ROUND(v0, v1, v2, v3);
		v0 ^= mi;
	}

	uint64_t t = 0;
	uint8_t *pt = (uint8_t *)&t;
	uint8_t *m = (uint8_t *)in;

	switch (src_sz) {
	case 7:
		pt[6] = m[6];
	case 6:
		pt[5] = m[5];
	case 5:
		pt[4] = m[4];
	case 4:
		*((uint32_t *)&pt[0]) = *((uint32_t *)&m[0]);
		break;
	case 3:
		pt[2] = m[2];
	case 2:
		pt[1] = m[1];
	case 1:
		pt[0] = m[0];
		break;
	default:
		break;
	}
	b |= _le64toh(t);

	v3 ^= b;
	DOUBLE_ROUND(v0, v1, v2, v3);
	v0 ^= b;
	v2 ^= 0xff;
	DOUBLE_ROUND(v0, v1, v2, v3);
	DOUBLE_ROUND(v0, v1, v2, v3);
	return (v0 ^ v1) ^ (v2 ^ v3);
}

static inline int
vec_init(dh_vector_t *vec, unsigned char power)
{
	int	rc = 0;

	memset(vec, 0, sizeof(*vec));
	vec->size = (size_t)(1 << power) * sizeof(void *);
	D_ALLOC(vec->data, sizeof(*vec->data) * (1 << power));
	if (vec->data == NULL) {
		/* out of memory */
		rc = -DER_NOMEM;
	}
	return rc;
}

static inline void
vec_destroy(dh_vector_t *vec)
{
	if (vec->data != NULL) {
		/* free RAM */
		D_FREE(vec->data);
	}
}

/*--------Virtual internal functions-----------------*/
static inline void
bucket_lock(dh_bucket_t *bucket)
{
	D_MUTEX_LOCK(&bucket->mtx);
}

static inline void
bucket_unlock(dh_bucket_t *bucket)
{
	D_MUTEX_UNLOCK(&bucket->mtx);
}

static void
no_bucket_lock(dh_bucket_t *bucket)
{
}

static inline void
read_lock(struct dyn_hash *htable)
{
	D_RWLOCK_RDLOCK(&htable->gtable->ht_lock.rwlock);
}

static inline void
write_lock(struct dyn_hash *htable)
{
	D_RWLOCK_WRLOCK(&htable->gtable->ht_lock.rwlock);
}

static inline void
mutex_lock(struct dyn_hash *htable)
{
	D_MUTEX_LOCK(&htable->gtable->ht_lock.mutex);
}

static inline void
spinlock(struct dyn_hash *htable)
{
	D_SPIN_LOCK(&htable->gtable->ht_lock.spin);
}

static inline void
rw_unlock(struct dyn_hash *htable)
{
	D_RWLOCK_UNLOCK(&htable->gtable->ht_lock.rwlock);
}

static inline void
mutex_unlock(struct dyn_hash *htable)
{
	D_MUTEX_UNLOCK(&htable->gtable->ht_lock.mutex);
}

static inline void
spinunlock(struct dyn_hash *htable)
{
	D_SPIN_UNLOCK(&htable->gtable->ht_lock.spin);
}

static void no_global_lock(struct dyn_hash *htable)
{
}

/*-----------Default customized member functions-------*/
static inline bool
def_hop_getkey(dh_item_t item, void **key, unsigned int *ksize)
{
	return false;
}

static inline void
def_hop_siphash_set(dh_item_t item, uint64_t siphash)
{
}

static inline void
def_hop_addref_free(struct d_hash_table *gtable, d_list_t *item)
{
}

static inline bool
def_hop_decref(struct d_hash_table *gtable, d_list_t *item)
{
	return false;
}

static inline int
def_hop_ndecref(struct d_hash_table *gtable, d_list_t *item, int count)
{
	return 0;
}

/*-----End of default customized member functions--*/

static inline void
prepare_insert(dh_bucket_t *bucket, unsigned char index)
{
	dh_field_t *start = &bucket->field[index];
	dh_field_t *dst = &bucket->field[bucket->counter];
	dh_field_t *src = &bucket->field[bucket->counter - 1];

	do {
		*dst-- = *src--;
	} while (src >= start);
}

static inline void
prepare_lookup(dh_bucket_t *bucket, uint64_t siphash,
	       unsigned char *out_first,
	       unsigned char *out_last)
{
	unsigned char first = 0;
	unsigned char last = bucket->counter;
	unsigned char middle;
	unsigned char len = last;

	while (len > 4) {
		middle = last - (len >> 1);
		if (bucket->field[middle].siphash > siphash) {
			last = middle;
			len = len >> 1;
		} else if (bucket->field[middle].siphash < siphash) {
			first = middle;
			len = len >> 1;
		} else {
			first = last = middle;
		}
	}
	if (last != bucket->counter) {
		/* move last */
		last++;
	}
	*out_first = first;
	*out_last = last;
}

static inline unsigned char
find_insert_index(dh_bucket_t *bucket, uint64_t siphash)
{
	unsigned char	idx;
	dh_field_t	*field;
	unsigned char	first;
	unsigned char	last;

	if (bucket->counter == 0) {
		/**/
		return 0;
	}
	prepare_lookup(bucket, siphash, &first, &last);
	for (idx = first; idx < last; idx++) {
		field = &bucket->field[idx];
		if (field->siphash == siphash) {
			/* found it */
			break;
		}
		if (field->siphash > siphash) {
			/* not there */
			break;
		}
	}
	return idx;
}

static inline int
find_exact_match(struct dyn_hash *htable, dh_bucket_t *bucket,
		 uint64_t siphash, const void *key,
			unsigned int ksize)
{
	unsigned char	idx;
	unsigned char	first;
	unsigned char	last;
	int		rc = -DER_NONEXIST;

	prepare_lookup(bucket, siphash, &first, &last);

	for (idx = first; idx < last; idx++) {
		if (bucket->field[idx].siphash == siphash) {
			/* got it */
			break;
		}
	}
	if (idx == last) {
		/* break the search */
		D_GOTO(out, rc);
	}
	while (idx != 0 && bucket->field[idx - 1].siphash == siphash) {
		/**/
		idx--;
	}
	for (; idx < last; idx++) {
		if (htable->ht_ops.hop_key_cmp(htable->gtable,
					       bucket->field[idx].record,
					       key, ksize)) {
			D_GOTO(out, rc = (int)idx);
		}
	}
out:
	return rc;
}

static void
shrink_vector(struct dyn_hash *htable, dh_bucket_t *bucket,
	      uint32_t index)
{

	uint32_t	last_idx;
	uint32_t	first_idx;
	uint32_t	idx;

	if (!(htable->gtable->ht_feats & D_HASH_FT_SHRINK)) {
		/* not configured */
		return;
	}
	last_idx = htable->ht_vector.counter;
	first_idx = 0xffffffff;

	for (idx = 0; idx < htable->ht_vector.counter; idx++) {
		if (htable->ht_vector.data[idx] == bucket) {
			first_idx = idx;
			break;
		}
	}
	D_ASSERT(first_idx != 0xffffffff);
	for (; idx < htable->ht_vector.counter; idx++) {
		if (htable->ht_vector.data[idx] != bucket) {
			last_idx = idx;
			break;
		}
	}
	if (last_idx == htable->ht_vector.counter && first_idx == 0) {
		/**/
		return;
	}
	if (first_idx == 0) {
		for (idx = 0; idx < last_idx; idx++) {
			htable->ht_vector.data[idx] =
			htable->ht_vector.data[last_idx];
		}
	} else {
		for (idx = first_idx; idx < last_idx; idx++) {
			htable->ht_vector.data[idx] =
			htable->ht_vector.data[first_idx - 1];
		}
	}
}

static int
split_bucket(struct dyn_hash *htable, dh_bucket_t *bucket,
	     dh_bucket_t **new_bucket)
{
	dh_bucket_t	*ad_bucket = NULL;
	unsigned char	idx;
	unsigned char	ix;
	int		rc = 0;
	uint32_t	vector_idx;
	uint32_t	bucket_idx;
	uint32_t	counter = 0;
	uint32_t        new_counter = 0;
	bool            b_switch = false;

	D_ASSERT(new_bucket != NULL);
	D_ASSERT(bucket->counter == DYNHASH_BUCKET);
	vector_idx = (uint32_t)(bucket->field[DYNHASH_BUCKET / 2].siphash
			>> htable->ht_shift);

	D_ALLOC(ad_bucket, sizeof(*bucket));
	if (ad_bucket == NULL) {
		/* Just return */
		D_GOTO(out, rc = -DER_NOMEM);
	}
	memset(ad_bucket, 0, sizeof(*ad_bucket));
	rc = D_MUTEX_INIT(&bucket->mtx, NULL);
	if (rc != 0) {
		D_FREE(ad_bucket);
		D_GOTO(out, rc = -DER_NOMEM);
	}
	for (idx = 0, ix = 0; idx < bucket->counter; idx++) {
		bucket_idx = (uint32_t)(bucket->field[idx].siphash >>
		htable->ht_shift);
		if (bucket_idx <= vector_idx) {
			D_ASSERT(!b_switch);
			counter++;
			continue;
		} else {
			b_switch = true;
			ad_bucket->field[ix++] = bucket->field[idx];
			new_counter++;
		}
	}
	if (new_counter == 0) {
		/* Just retry */
		D_GOTO(done, rc = -DER_AGAIN);
	}
	D_ASSERT((counter + new_counter) == bucket->counter);
	bucket->counter = counter;
	ad_bucket->counter = new_counter;
done:
	if (rc != 0) {
		D_MUTEX_DESTROY(&ad_bucket->mtx);
		D_FREE(ad_bucket);
	} else {
		/* set a new bucket */
		*new_bucket = ad_bucket;
	}
out:
	return rc;
}

static int
split_vector(struct dyn_hash *htable)
{
	void		**current_data = htable->ht_vector.data;
	void		**new_data;
	uint32_t	idx;
	uint32_t	ix;
	uint32_t	index;
	int		rc = 0;
	uint32_t	counter;
	void		**src;
	void		**dst;
#if DYN_HASH_DEBUG
	struct timeval	begit;
	struct timeval	end;


	gettimeofday(begit, NULL);
#endif
	counter = htable->ht_vector.counter;
	D_ALLOC(new_data, htable->ht_vector.size * 2);
	if (new_data == NULL) {
		/**/
		D_GOTO(out, rc = -DER_NOMEM);
	}
	/* duplicate twice each vector reference */
	for (idx = 0; idx < (counter / DYNHASH_BUCKET); idx++) {
		dst = &new_data[idx * DYNHASH_BUCKET * 2];
		src = &current_data[idx * DYNHASH_BUCKET];
		for (ix = 0, index = 0; index < DYNHASH_BUCKET;
		     ix++, index++) {
			dst[ix++] = src[index];
			dst[ix] = src[index];
		}
	}
	htable->ht_shift--;

	/* update vector */
	D_FREE(current_data);
	htable->ht_vector.data = new_data;
	htable->ht_vector.counter *= 2;
	htable->ht_vector.size *= 2;
out:
#if DYN_HASH_DEBUG
	gettimeofday(end, NULL);
	do {
		uint64_t start = begin.tv_sec * 1000000 + begin.tv_usec;
		uint64_t stop = end.tv_sec * 1000000 + end.tv_usec;

		htable->ht_vsplits++;
		htable->ht_vsplit_delay += (uint32_t)(stop - start);
	} while (0);
#endif
	return rc;
}

static void
add_record(dh_bucket_t *bucket, uint64_t siphash, dh_item_t item)
{
	unsigned char	idx;

	idx = find_insert_index(bucket, siphash);
	prepare_insert(bucket, idx);
	bucket->field[idx].siphash = siphash;
	bucket->field[idx].record = item;
	bucket->counter++;
}

typedef enum {
	DH_INSERT_INCLUSIVE = 0,
	DH_INSERT_EXCLUSIVE = 1,
	DH_LOOKUP_INSERT    = 3,
} dh_insert_mode_t;

static int
do_insert(struct dyn_hash *htable, const void *key, unsigned int ksize,
	  dh_item_t *item, uint64_t siphash, dh_insert_mode_t mode)
{
	dh_item_t	data = *item;
	uint64_t	sk1;
	uint64_t	sk2;
	uint32_t	index;
	dh_bucket_t	*bucket;
	dh_bucket_t	*prev_bucket;
	dh_bucket_t	*splt_bucket;
	int		rc = 0;
	uint32_t	idx;

	htable->ht_write_lock(htable);
	index = (uint32_t)(siphash >> htable->ht_shift);
	bucket = htable->ht_vector.data[index];
	if (mode == DH_INSERT_EXCLUSIVE || mode == DH_LOOKUP_INSERT) {
		rc = find_exact_match(htable, bucket, siphash,
				      key, ksize);
		if (rc >= 0 && mode == DH_INSERT_EXCLUSIVE) {
			/* insert on exist */
			D_GOTO(unlock, rc = -DER_EXIST);
		}
		if (rc >= 0 && mode == DH_LOOKUP_INSERT) {
			*item = bucket->field[rc].record;
			D_GOTO(unlock, rc);
		}
	}
	rc = 0;
	htable->bucket_lock(bucket);
	if (bucket->counter < DYNHASH_BUCKET) {
		htable->ht_records++;
#if DYN_HASH_DEBUG
		if (htable->ht_nr_max < htable->ht_records) {
			/* save max entries */
			htable->ht_nr_max = htable->ht_records;
		}
#endif
		htable->ht_rw_unlock(htable);
		add_record(bucket, siphash, data);
		if (!(htable->gtable->ht_feats & D_HASH_FT_EPHEMERAL) ||
		    mode == DH_LOOKUP_INSERT) {
			/* update reference count */
			htable->ht_ops.hop_rec_addref(htable->gtable, *item);
		}
		htable->bucket_unlock(bucket);
		D_GOTO(out, rc);
	}
	bucket_unlock(bucket);
	/* bucket is full */
	rc = split_bucket(htable, bucket, &splt_bucket);
	if (rc == -DER_AGAIN) {
		/* bucket split can't update vector;
		 *  vector is full
		 */
		rc = split_vector(htable);
		if (rc == 0) {
			/* force operation repeat */
			rc = -DER_AGAIN;
		}
	}
	if (rc != 0) {
		/* operation failed */
		D_GOTO(unlock, rc);
	}
	sk1 = splt_bucket->field[0].siphash;
	index = (uint32_t)(sk1 >> htable->ht_shift);
	prev_bucket = htable->ht_vector.data[index];
	sk2 = prev_bucket->field[0].siphash;

	/* make sure vector is ready for bucket updates */
	if (index == (uint32_t)(sk2 >> htable->ht_shift)) {
		rc = split_vector(htable);
		if (rc == 0) {
			/* run it again */
			rc = -DER_AGAIN;
		}
		D_GOTO(unlock, rc);
	}
	/* update vector */
	for (idx = index; idx < htable->ht_vector.counter; idx++) {
		if (prev_bucket != htable->ht_vector.data[idx]) {
			/* update vector */
			break;
		}
		htable->ht_vector.data[idx] = splt_bucket;
	}
	rc = -DER_AGAIN;
unlock:
	htable->ht_rw_unlock(htable);
out:
	return rc;
}

/* caller is responsible to lock global lock and bucket */
static bool
do_delete(struct d_hash_table *gtable, const void *key,
	  unsigned int ksize, uint64_t siphash,
	  dh_bucket_t *bucket, uint32_t index)
{
	bool		rc = true;
	int		bucket_idx;
	dh_item_t	item;
	struct dyn_hash *htable = gtable->dyn_hash;
	bool		free_bucket = false;

	if (bucket->counter == 0) {
		htable->ht_rw_unlock(htable);
		htable->bucket_unlock(bucket);
		D_GOTO(out, rc = false);
	}
	bucket_idx = find_exact_match(htable, bucket, siphash,
				      key, ksize);
	if (bucket_idx < 0) {
		htable->ht_rw_unlock(htable);
		htable->bucket_unlock(bucket);
		D_GOTO(out, rc = false);
	}
	item = bucket->field[bucket_idx].record;
	if (bucket->counter == 1 &&
	    gtable->ht_feats & D_HASH_FT_SHRINK) {

		bucket->counter = 0;
		htable->ht_records--;
		if (htable->ht_records != 0) {
			/* makr empty bucket to delete */
			free_bucket = true;
		}
		shrink_vector(htable, bucket, index);
		htable->ht_rw_unlock(htable);
	} else {
		htable->ht_records--;
		htable->ht_rw_unlock(htable);
		while (bucket_idx < (bucket->counter - 1)) {
			bucket->field[bucket_idx] =
			bucket->field[bucket_idx + 1];
			bucket_idx++;
		}
		bucket->counter--;
	}
	if ((htable->gtable->ht_feats & D_HASH_FT_EPHEMERAL) == 0 &&
	    htable->ht_ops.hop_rec_decref(gtable, item) != 0) {
		htable->ht_ops.hop_rec_free(gtable, item);
	}
	htable->bucket_unlock(bucket);
	if (free_bucket) {
		D_MUTEX_DESTROY(&bucket->mtx);
		D_FREE(bucket);
	}
out:
	return rc;
}

/*----------External API-------------------------*/
int
dyn_hash_create(uint32_t feats, uint32_t bits, void *priv,
		d_hash_table_ops_t *hops, struct d_hash_table **htable_pp)
{
	struct d_hash_table     *gtable;
	int			rc;

	D_ALLOC_PTR(gtable);
	if (gtable == NULL) {
		/* out of memory */
		D_GOTO(out, rc = -DER_NOMEM);
	}
	memset(gtable, 0, sizeof(*gtable));
	D_ALLOC_PTR(gtable->dyn_hash);
	if (gtable->dyn_hash == NULL) {
		D_FREE(gtable);
		D_GOTO(out, rc = -DER_NOMEM);
	}
	memset(gtable->dyn_hash, 0, sizeof(*gtable->dyn_hash));
	rc = dyn_hash_table_create_inplace(feats, bits, priv, hops, gtable);
	if (rc) {
		D_FREE(gtable->dyn_hash);
		D_FREE(gtable);
	}
out:
	*htable_pp = gtable;
	return rc;
}

int
dyn_hash_table_create_inplace(uint32_t feats, uint32_t bits, void *priv,
			      d_hash_table_ops_t *hops,
			      struct d_hash_table *gtable)
{
	int		rc = 0;
	uint32_t	idx;
	dh_bucket_t	*bucket;
	struct dyn_hash	*htable = gtable->dyn_hash;

	D_ASSERT(hops != NULL);
	D_ASSERT(hops->hop_key_cmp != NULL);
	D_ASSERT(feats & D_HASH_FT_DYNAMIC);

	gtable->ht_feats = feats;
	gtable->ht_priv = priv;
	htable->gtable = gtable;
	htable->bucket_lock = no_bucket_lock;
	htable->bucket_unlock = no_bucket_lock;
	htable->ht_shift = (sizeof(uint64_t) * 8) - DYNHASH_SIPBITS;

	/* set specified customized functions */
	htable->ht_ops.hop_key_cmp = hops->hop_key_cmp;
	if (hops->hop_key_get != NULL) {
		/* set hop_key_get */
		htable->ht_ops.hop_key_get = hops->hop_key_get;
	} else {
		/* set default */
		htable->ht_ops.hop_key_get = def_hop_getkey;
	}
	if (hops->hop_rec_addref != NULL) {
		/* set hop_rec_addref */
		htable->ht_ops.hop_rec_addref = hops->hop_rec_addref;
	} else {
		/* set default */
		htable->ht_ops.hop_rec_addref = def_hop_addref_free;
	}
	if (hops->hop_rec_decref != NULL) {
		/* set hop_rec_decref */
		htable->ht_ops.hop_rec_decref = hops->hop_rec_decref;
	} else {
		/* set default */
		htable->ht_ops.hop_rec_decref = def_hop_decref;
	}
	if (hops->hop_rec_free != NULL) {
		/* set hop_rec_free */
		htable->ht_ops.hop_rec_free = hops->hop_rec_free;
	} else {
		/* set default */
		htable->ht_ops.hop_rec_free = def_hop_addref_free;
	}
	if (hops->hop_rec_ndecref != NULL) {
		/* set hop_rec_ndecref */
		htable->ht_ops.hop_rec_ndecref = hops->hop_rec_ndecref;
	} else {
		/* set default */
		htable->ht_ops.hop_rec_ndecref = def_hop_ndecref;
	}
	if (hops->hop_siphash_set != NULL) {
		/* set hop_siphash_set*/
		htable->ht_ops.hop_siphash_set = hops->hop_siphash_set;
	} else {
		/* set default */
		htable->ht_ops.hop_siphash_set = def_hop_siphash_set;
	}

	/* set global lock virtual function */
	htable->ht_write_lock = no_global_lock;
	htable->ht_read_lock = no_global_lock;
	if (!(feats & D_HASH_FT_NOLOCK)) {
		htable->bucket_lock = bucket_lock;
		htable->bucket_unlock = bucket_unlock;
		if (feats & D_HASH_FT_MUTEX) {
			rc = D_MUTEX_INIT(&gtable->ht_lock.mutex, NULL);
			if (rc != 0) {
				/* return error */
				D_GOTO(out, rc);
			}
			htable->ht_write_lock = mutex_lock;
			htable->ht_read_lock = mutex_lock;
			htable->ht_rw_unlock = mutex_unlock;
		} else if (feats & D_HASH_FT_RWLOCK) {
			rc = D_RWLOCK_INIT(&gtable->ht_lock.rwlock, NULL);
			if (rc != 0) {
				/* return error */
				D_GOTO(out, rc);
			}
			htable->ht_write_lock = write_lock;
			htable->ht_read_lock = read_lock;
			htable->ht_rw_unlock = rw_unlock;
		} else {
			rc = D_SPIN_INIT(&gtable->ht_lock.spin,
					 PTHREAD_PROCESS_PRIVATE);
			if (rc != 0) {
				/* return error */
				D_GOTO(out, rc);
			}
			htable->ht_write_lock = spinlock;
			htable->ht_read_lock = spinlock;
			htable->ht_rw_unlock = spinunlock;
		}
	}

	/* initialize vector */
	rc = vec_init(&htable->ht_vector, DYNHASH_SIPBITS);
	if (rc != 0) {
		/* return error */
		D_GOTO(out1, rc);
	}
	htable->ht_vector.counter = DYNHASH_BUCKET;

	/* allocate bucket */
	D_ALLOC(bucket, sizeof(*bucket));
	if (bucket  == NULL) {
		/* out of memory */
		D_GOTO(out2, rc = -DER_NOMEM);
	}
	memset(bucket, 0, sizeof(*bucket));
	rc = D_MUTEX_INIT(&bucket->mtx, NULL);
	if (rc != 0) {
		/* fail the function */
		D_GOTO(out2, rc = -DER_NOMEM);
	}

	for (idx = 0; idx < htable->ht_vector.counter; idx++) {
		/* set bucket pointer to vector */
		htable->ht_vector.data[idx] = (void *)bucket;
	}
	htable->ht_magic = DYNHASH_MAGIC;
	D_GOTO(out, rc);

out2:
	vec_destroy(&htable->ht_vector);
out1:
	if (!(feats & D_HASH_FT_NOLOCK)) {
		/* destroy active lock */
		if (feats & D_HASH_FT_MUTEX) {
			/* destroy mutex */
			D_MUTEX_DESTROY(&gtable->ht_lock.mutex);
		} else if (feats & D_HASH_FT_RWLOCK) {
			/* destroy RWLOck */
			D_RWLOCK_DESTROY(&gtable->ht_lock.rwlock);
		} else {
			/* destroy spinlock */
			D_SPIN_DESTROY(&gtable->ht_lock.spin);
		}
	}

out:
	return rc;
}

int
dyn_hash_table_traverse(struct d_hash_table *gtable, d_hash_traverse_cb_t cb,
			void *arg)
{
	int		rc = 0;
	uint32_t	idx;
	uint32_t        ix;
	dh_bucket_t     *bucket;
	dh_bucket_t     *prev = NULL;
	struct dyn_hash *htable = gtable->dyn_hash;

	D_ASSERT(htable->ht_magic == DYNHASH_MAGIC);
	D_ASSERT(gtable->ht_feats & D_HASH_FT_DYNAMIC);

	if (cb == NULL) {
		D_ERROR("invalid parameter, NULL cb.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	htable->ht_read_lock(htable);
	for (idx = 0; idx < htable->ht_vector.counter; idx++) {
		bucket = htable->ht_vector.data[idx];
		if (bucket == prev) {
			/* skip duplicated bucket */
			continue;
		}
		prev = bucket;
		for (ix = 0; ix < bucket->counter; ix++) {
			rc = cb(bucket->field[ix].record, arg);
			if (rc != 0) {
				/* done */
				break;
			}
		}
	}
	htable->ht_rw_unlock(htable);
out:
	return rc;
}

int
dyn_hash_table_destroy(struct d_hash_table *gtable, bool force)
{
	int rc;

	D_ASSERT(gtable->ht_feats & D_HASH_FT_DYNAMIC);
	rc = dyn_hash_table_destroy_inplace(gtable, force);
	if (rc == 0) {
		D_FREE(gtable->dyn_hash);
		D_FREE(gtable);
	}
	return rc;
}

int
dyn_hash_table_destroy_inplace(struct d_hash_table *gtable, bool force)
{
	int		rc = 0;
	uint32_t	idx;
	dh_bucket_t     *bucket;
	dh_bucket_t     *prev = NULL;
	struct dyn_hash *htable = gtable->dyn_hash;

	D_ASSERT(gtable->ht_feats & D_HASH_FT_DYNAMIC);
	D_ASSERT(htable->ht_magic == DYNHASH_MAGIC);
	if (!force && htable->ht_records != 0) {
		D_DEBUG(DB_TRACE, "Warning, non-empty hash\n");
		D_GOTO(out, rc = -DER_BUSY);
	}
	htable->ht_write_lock(htable);
	for (idx = 0; idx < htable->ht_vector.counter; idx++) {
		bucket = htable->ht_vector.data[idx];
		if (bucket == prev) {
			/* skip duplicated bucket */
			continue;
		}
		prev = bucket;
		if (!(gtable->ht_feats & D_HASH_FT_NOLOCK)) {
			/* destroy mutex */
			D_MUTEX_DESTROY(&bucket->mtx);
		}
		D_FREE(bucket);
	}
	vec_destroy(&htable->ht_vector);
	htable->ht_rw_unlock(htable);

	if (!(gtable->ht_feats & D_HASH_FT_NOLOCK)) {
		/**/
		if (gtable->ht_feats & D_HASH_FT_MUTEX) {
			/* destroy ht mutex */
			D_MUTEX_DESTROY(&gtable->ht_lock.mutex);
		} else if (gtable->ht_feats & D_HASH_FT_RWLOCK) {
			/* destroy ht rwlock */
			D_RWLOCK_DESTROY(&gtable->ht_lock.rwlock);
		} else {
			/* destroy ht spinlock */
			D_SPIN_DESTROY(&gtable->ht_lock.spin);
		}
	}

	htable->ht_magic = 0;
out:
	return rc;
}

dh_item_t
dyn_hash_rec_find(struct d_hash_table *gtable, const void *key,
		  unsigned int ksize, uint64_t siphash)
{
	int		rc = 0;
	dh_bucket_t	*bucket;
	dh_item_t	item = NULL;
	bool		sip_update = false;
	uint32_t        index;
	struct dyn_hash *htable = gtable->dyn_hash;

	D_ASSERT(gtable->ht_feats & D_HASH_FT_DYNAMIC);
	D_ASSERT(htable->ht_magic == DYNHASH_MAGIC);
	if (siphash == 0) {
		siphash = gen_siphash(key, ksize);
		sip_update = true;
	}
	htable->ht_read_lock(htable);
	index = (uint32_t)(siphash >> htable->ht_shift);
	bucket = htable->ht_vector.data[index];
	htable->bucket_lock(bucket);
	htable->ht_rw_unlock(htable);

	rc = find_exact_match(htable, bucket, siphash, key, ksize);
	if (rc >= 0) {
		/* get record */
		item = bucket->field[rc].record;
	}
	if (item != NULL) {
		htable->ht_ops.hop_rec_addref(gtable, item);
		if (sip_update) {
			/* call user operation */
			htable->ht_ops.hop_siphash_set(item, siphash);
		}
	}
	htable->bucket_unlock(bucket);
	return item;
}

dh_item_t
dyn_hash_rec_find_insert(struct d_hash_table *gtable, const void *key,
			 unsigned int ksize, dh_item_t item, uint64_t siphash)
{
	int		rc = 0;
	bool		set_siphash = false;
	dh_item_t	tmp = item;
	struct dyn_hash *htable = gtable->dyn_hash;

	D_ASSERT(gtable->ht_feats & D_HASH_FT_DYNAMIC);
	D_ASSERT(htable->ht_magic == DYNHASH_MAGIC);
	if (siphash == 0) {
		siphash = gen_siphash(key, ksize);
		set_siphash = true;
	}
	do {
		rc = do_insert(htable, key, ksize, &tmp, siphash,
			       DH_LOOKUP_INSERT);
	} while (rc == -DER_AGAIN);
	if (rc == 0 && set_siphash) {
		/* call user operation */
		htable->ht_ops.hop_siphash_set(tmp, siphash);
	}
	return tmp;
}

int
dyn_hash_rec_insert(struct d_hash_table *gtable, const void *key,
		    unsigned int ksize, dh_item_t item, bool exclusive)
{
	int		rc = 0;
	uint64_t	siphash;
	struct dyn_hash	*htable = gtable->dyn_hash;

	D_ASSERT(gtable->ht_feats & D_HASH_FT_DYNAMIC);
	D_ASSERT(htable->ht_magic == DYNHASH_MAGIC);

	siphash = gen_siphash(key, ksize);
	do {
		rc = do_insert(htable, key, ksize, &item,
			       siphash, exclusive ? DH_INSERT_EXCLUSIVE :
			       DH_INSERT_INCLUSIVE);
	} while (rc == -DER_AGAIN);

	if (rc == 0) {
		/* set siphash */
		htable->ht_ops.hop_siphash_set(item, siphash);
	}
	return rc;
}

bool
dyn_hash_rec_delete(struct d_hash_table *gtable, const void *key,
		    unsigned int ksize, uint64_t siphash)
{
	uint32_t	index;
	dh_bucket_t	*bucket;
	struct dyn_hash *htable = gtable->dyn_hash;

	D_ASSERT(gtable->ht_feats & D_HASH_FT_DYNAMIC);
	D_ASSERT(htable->ht_magic == DYNHASH_MAGIC);

	if (siphash == 0) {
		/* generate siphash */
		siphash = gen_siphash(key, ksize);
	}
	index = (uint32_t)(siphash >> htable->ht_shift);
	htable->ht_write_lock(htable);
	bucket = htable->ht_vector.data[index];
	htable->bucket_lock(bucket);
	return do_delete(gtable, key, ksize, siphash, bucket, index);
}

bool
dyn_hash_rec_delete_at(struct d_hash_table *gtable, dh_item_t item)
{
	bool		rc = true;
	void		*key;
	unsigned int	ksize;
	uint64_t	siphash;
	struct dyn_hash *htable = gtable->dyn_hash;

	D_ASSERT(gtable->ht_feats & D_HASH_FT_DYNAMIC);
	D_ASSERT(htable->ht_magic == DYNHASH_MAGIC);

	if (!(htable->ht_ops.hop_key_get(item, &key, &ksize))) {
		D_ERROR("Get key function failed\n");
		D_GOTO(out, rc = false);
	}
	siphash = gen_siphash(key, ksize);
	rc = dyn_hash_rec_delete(gtable, (const void *)key, ksize, siphash);
out:
	return rc;
}

bool
dyn_hash_rec_evict(struct d_hash_table *htable, const void *key,
		   unsigned int ksize)
{
	return false;
}

bool
dyn_hash_rec_evict_at(struct d_hash_table *htable, dh_item_t item,
		      uint64_t siphash)
{
	return false;
}

void
dyn_hash_rec_addref(struct d_hash_table *gtable, dh_item_t item)
{
	struct dyn_hash *htable = gtable->dyn_hash;

	D_ASSERT(gtable->ht_feats & D_HASH_FT_DYNAMIC);
	D_ASSERT(htable->ht_magic == DYNHASH_MAGIC);
	htable->ht_write_lock(htable);
	htable->ht_ops.hop_rec_addref(gtable, item);
	htable->ht_rw_unlock(htable);
}

void
dyn_hash_rec_decref(struct d_hash_table *gtable, dh_item_t item)
{
	struct dyn_hash *htable = gtable->dyn_hash;
	bool		zombie;
	bool		ephemeral = (gtable->ht_feats & D_HASH_FT_EPHEMERAL);
	void		*key;
	unsigned int	ksize;
	uint64_t	siphash;
	uint32_t	index;
	dh_bucket_t	*bucket;

	D_ASSERT(gtable->ht_feats & D_HASH_FT_DYNAMIC);
	D_ASSERT(htable->ht_magic == DYNHASH_MAGIC);

	htable->ht_write_lock(htable);
	zombie = htable->ht_ops.hop_rec_decref(gtable, item);
	if (zombie) {
		if (!htable->ht_ops.hop_key_get(item, &key, &ksize)) {
			D_ERROR("Get key function failed\n");
			htable->ht_rw_unlock(htable);
			D_GOTO(out, 0);
		}
		siphash = gen_siphash(key, ksize);
		index = (uint32_t)(siphash >> htable->ht_shift);
		bucket = htable->ht_vector.data[index];
		htable->bucket_lock(bucket);
		do_delete(gtable, key, ksize, siphash, bucket, index);
		if (ephemeral) {
			/* call user operation */
			htable->ht_ops.hop_rec_free(gtable, item);
		}
	} else {
		htable->ht_rw_unlock(htable);
	}
out:
	;
}

int
dyn_hash_rec_ndecref(struct d_hash_table *gtable, int count, dh_item_t item)
{
	bool		zombie = false;
	int		rc = 0;
	struct dyn_hash *htable = gtable->dyn_hash;
	bool		ephemeral = (gtable->ht_feats & D_HASH_FT_EPHEMERAL);
	void		*key;
	unsigned int	ksize;
	uint64_t	siphash;
	uint32_t	index;
	dh_bucket_t	*bucket;

	D_ASSERT(gtable->ht_feats & D_HASH_FT_DYNAMIC);
	D_ASSERT(htable->ht_magic == DYNHASH_MAGIC);

	htable->ht_write_lock(htable);
	if (htable->ht_ops.hop_rec_ndecref != def_hop_ndecref) {
		rc = htable->ht_ops.hop_rec_ndecref(gtable, item, count);
		if (rc >= 1) {
			zombie = true;
			rc = 0;
		}
	} else {
		do {
			zombie = htable->ht_ops.hop_rec_decref(gtable,
				item);
		} while (--count && !zombie);

		if (count != 0) {
			/* hash has no records */
			rc = -DER_INVAL;
		}
	}
	if (rc == 0 && zombie) {
		if (!htable->ht_ops.hop_key_get(item, &key, &ksize)) {
			D_ERROR("Get key function failed\n");
			htable->ht_rw_unlock(htable);
			D_GOTO(out, 0);
		}
		siphash = gen_siphash(key, ksize);
		index = (uint32_t)(siphash >> htable->ht_shift);
		bucket = htable->ht_vector.data[index];
		htable->bucket_lock(bucket);
		do_delete(gtable, key, ksize, siphash, bucket, index);
		if (ephemeral) {
			/* call user operation */
			htable->ht_ops.hop_rec_free(gtable, item);
		}
	} else {
		htable->ht_rw_unlock(htable);
	}
out:
	return rc;
}

dh_item_t
dyn_hash_rec_first(struct d_hash_table *gtable)
{
	dh_bucket_t	*bucket;
	dh_bucket_t	*prev_bucket = 0;
	dh_item_t	item = NULL;
	uint32_t	idx;
	struct dyn_hash *htable = gtable->dyn_hash;

	D_ASSERT(gtable->ht_feats & D_HASH_FT_DYNAMIC);
	D_ASSERT(htable->ht_magic == DYNHASH_MAGIC);

	htable->ht_read_lock(htable);
	for (idx = 0; idx < htable->ht_vector.counter; idx++) {
		bucket = htable->ht_vector.data[idx];
		if (bucket == prev_bucket) {
			/* back to the loop */
			continue;
		}
		prev_bucket = bucket;
		if (bucket->counter != 0) {
			item = bucket->field[0].record;
			break;
		}
	}
	htable->ht_rw_unlock(htable);
	return item;
}

void
dyn_hash_table_debug(struct d_hash_table *gtable)
{
	struct dyn_hash *htable = gtable->dyn_hash;

	D_ASSERT(gtable->ht_feats & D_HASH_FT_DYNAMIC);
	D_ASSERT(htable->ht_magic == DYNHASH_MAGIC);
#if DYN_HASH_DEBUG
	if (htable->ht_feats & DYN_HASH_FT_SHRINK) {
		D_DEBUG(DB_TRACE, "max nr: %u, cur nr: %u, vector_spits: %u,"
			"split_time(usec) %u\n",
			htable->ht_nr_max, htable->ht_records,
			htable->ht_dep_max, htable->ht_vsplit_delay);
	} else {
		D_DEBUG(DB_TRACE, "max nr: %u, cur nr: %u\n",
			htable->ht_nr_max, htable->ht_records);

	}
#endif

}
