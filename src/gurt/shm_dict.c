/**
 * (C) Copyright 2024-2025 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include "shm_internal.h"
#include <gurt/shm_dict.h>

/* the address of shared memory region */
extern struct d_shm_hdr *d_shm_head;

#define INVALID_HT_ID (0)
#define NUSER_MASK (0xFFFF000000000000L)
#define HT_ID_MASK (0xFFFFFFFFFFFFL)
#define NUSER_INC  (0x1000000000000L)
#define GET_NUSER(x) (((x) & 0xFFFF000000000000L) >> 48)
#define GET_HTID(x) ((x) & HT_ID_MASK)

static __inline__ unsigned long long
rdtsc(void)
{
	unsigned hi, lo;

	__asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
	return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

int
shm_ht_create(const char name[], int bits, int n_lock, struct d_shm_ht_loc *shm_ht_loc)
{
	int                   i;
	struct d_shm_ht_head *ht_head_tmp;
	long int             *off_next;
	long int              offset;
	d_shm_mutex_t        *p_locks;
	int                   len_name;
	int                   n_bucket;
	int                   rc;
	uint64_t              ht_id;

	shm_ht_loc->ht_head = NULL;
	len_name = strnlen(name, MAX_HT_NAME_LEN);
	if (len_name >= MAX_HT_NAME_LEN) {
		DS_ERROR(EINVAL, "hash table name is longer than %d bytes", MAX_HT_NAME_LEN - 1);
		return EINVAL;
	}

	/* force the number of buckets be power of 2 */
	n_bucket = 1 << bits;
	if (n_bucket < n_lock) {
		DS_ERROR(EINVAL, "number of buckets is smaller than number of locks");
		return EINVAL;
	}
	if (n_bucket % n_lock) {
		DS_ERROR(EINVAL, "number of buckets is not a multiplier of number of locks");
		return EINVAL;
	}

	shm_mutex_lock(&(d_shm_head->ht_lock), NULL);

	/* loop over existing hash tables to check whether it exists or not */
	if (d_shm_head->off_ht_head != INVALID_OFFSET) {
		offset = d_shm_head->off_ht_head;
		while (offset != INVALID_OFFSET) {
			ht_head_tmp = (struct d_shm_ht_head *)((char *)d_shm_head + offset);
			if ((strncmp(name, ht_head_tmp->ht_name, MAX_HT_NAME_LEN) == 0)) {
				/* found existing hash table with given name */
				if ((ht_head_tmp->n_bucket == n_bucket) &&
				    (ht_head_tmp->n_lock == n_lock)) {
					shm_ht_loc->ht_head = ht_head_tmp;
					shm_ht_loc->ht_id = GET_HTID(ht_head_tmp->nuser_htid);
					shm_mutex_unlock(&(d_shm_head->ht_lock));
					return 0;
				} else {
					DS_ERROR(EINVAL,
						 "hash table with different parameters exists");
					rc = EINVAL;
					goto err;
				}
			}

			if (ht_head_tmp->next == INVALID_OFFSET) {
				/* reaching the end of the link list of existing hash tables */
				shm_ht_loc->ht_head = NULL;
				break;
			}
			offset = ht_head_tmp->next;
		}
	}

	/* This hash table does not exist, then create it. */
	shm_ht_loc->ht_head = shm_alloc(sizeof(struct d_shm_ht_head) +
					(sizeof(d_shm_mutex_t) * n_lock) +
					(sizeof(long int) * n_bucket));
	if (shm_ht_loc->ht_head == NULL) {
		rc = ENOMEM;
		goto err;
	}
	ht_head_tmp = shm_ht_loc->ht_head;

	atomic_store_relaxed(&(ht_head_tmp->nuser_htid), INVALID_HT_ID);
	memcpy(ht_head_tmp->ht_name, name, len_name + 1);
	ht_head_tmp->n_bucket = n_bucket;
	ht_head_tmp->n_lock   = n_lock;

	p_locks = (d_shm_mutex_t *)((char *)ht_head_tmp + sizeof(struct d_shm_ht_head));
	for (i = 0; i < n_lock; i++) {
		if (shm_mutex_init(&(p_locks[i])) != 0) {
			DS_ERROR(errno, "shm_mutex_init() failed");
			rc = errno;
			goto err;
		}
	}
	off_next = (long int *)((char *)ht_head_tmp + sizeof(struct d_shm_ht_head) +
				(sizeof(d_shm_mutex_t) * n_lock));
	for (i = 0; i < n_bucket; i++)
		off_next[i] = INVALID_OFFSET;
	/* insert the new hash table header as the first one of the link list */
	ht_head_tmp->prev       = INVALID_OFFSET;
	ht_head_tmp->next       = d_shm_head->off_ht_head;
	d_shm_head->off_ht_head = (long int)((char *)ht_head_tmp - (char *)d_shm_head);

	ht_id = INVALID_HT_ID;
	while (ht_id == INVALID_HT_ID) {
		#if defined(__x86_64__)
		/* a fast way to generate a random number with time stampe */
		ht_id = rdtsc();
		#else
		ht_id = ((long int)rand()) << 32;
		ht_id |= rand();
		#endif
		/* ht id only takes lower 48 bits */
		ht_id &= HT_ID_MASK;
	}
	/* the upper 16 bits are for nuser which is zero at this moment */
	atomic_store_relaxed(&(ht_head_tmp->nuser_htid), ht_id);
	shm_ht_loc->ht_id  = ht_id;

	shm_mutex_unlock(&(d_shm_head->ht_lock));
	return 0;

err:
	shm_mutex_unlock(&(d_shm_head->ht_lock));
	return rc;
}

/* atomically check ht id and update (inc / dec) number of ht users */
static bool
shm_ht_update_nuser(struct d_shm_ht_loc *ht_loc, int64_t change)
{
	int64_t old_nuser_htid, old_nuser, saved_ht_id, *addr_old_nuser_htid;
	int64_t new_nuser_htid, ht_id;

	saved_ht_id         = ht_loc->ht_id;
	addr_old_nuser_htid = (int64_t *)&(ht_loc->ht_head->nuser_htid);
	old_nuser_htid      = atomic_load_explicit(addr_old_nuser_htid, memory_order_relaxed);
	ht_id               = GET_HTID(old_nuser_htid);
	if (saved_ht_id != ht_id)
		/* hash table is not valid any more */
		return false;

	old_nuser = GET_NUSER(old_nuser_htid);
	/* sanity check */
	if (old_nuser < 0)
		DS_WARN(EINVAL, "negative number of hash table user");

	new_nuser_htid = old_nuser_htid + change;
	while (1) {
		if (atomic_compare_exchange_weak(addr_old_nuser_htid, &old_nuser_htid,
						 new_nuser_htid)) {
			return true;
		} else {
			/* failed to exchange */
			ht_id = GET_HTID(old_nuser_htid);
			if (saved_ht_id != ht_id) {
				/* hash table is not valid any more */
				return false;
			} else {
				/* hash table is still valid, but nuser was updated by another user */
				if (old_nuser < 0)
					DS_WARN(EINVAL, "negative number of hash table user");
				new_nuser_htid = old_nuser_htid + change;
			}
		}
	}
}

static void
shm_ht_invalidate_htid(struct d_shm_ht_loc *ht_loc)
{
	int64_t old_nuser_htid, old_nuser, saved_ht_id, *addr_old_nuser_htid;
	int64_t new_nuser_htid, ht_id;

	saved_ht_id         = ht_loc->ht_id;
	addr_old_nuser_htid = (int64_t *)&(ht_loc->ht_head->nuser_htid);
	old_nuser_htid      = atomic_load_explicit(addr_old_nuser_htid, memory_order_relaxed);
	ht_id               = GET_HTID(old_nuser_htid);
	if (saved_ht_id == INVALID_HT_ID) {
		/* hash table was invalidated already */
		return;
	} else if (saved_ht_id != ht_id) {
		DS_WARN(EINVAL, "inconsistent hash table id");
		return;
	}

	old_nuser = GET_NUSER(old_nuser_htid);
	/* sanity check */
	if (old_nuser < 0)
		DS_WARN(EINVAL, "negative number of hash table user");

	new_nuser_htid = (old_nuser_htid & NUSER_MASK) + INVALID_HT_ID;
	while (1) {
		if (atomic_compare_exchange_weak(addr_old_nuser_htid, &old_nuser_htid,
						 new_nuser_htid)) {
			return;
		} else {
			/* failed to exchange */
			ht_id = GET_HTID(old_nuser_htid);
			if (saved_ht_id != ht_id) {
				/* not supposed to be here! */
				D_ASSERT(0);
				return;
			} else {
				/* nuser was updated by another user */
				if (old_nuser < 0)
					DS_WARN(EINVAL, "negative number of hash table user");
				new_nuser_htid = (old_nuser_htid & NUSER_MASK) + INVALID_HT_ID;
			}
		}
	}
}

static bool
shm_ht_inc_nuser(struct d_shm_ht_loc *ht_loc)
{
	return shm_ht_update_nuser(ht_loc, NUSER_INC);
}

static bool
shm_ht_dec_nuser(struct d_shm_ht_loc *ht_loc)
{
	return shm_ht_update_nuser(ht_loc, -NUSER_INC);
}

bool
shm_ht_is_usable(struct d_shm_ht_loc *shm_ht_loc)
{
	int64_t nuser_htid;

	nuser_htid = atomic_load_explicit(&(shm_ht_loc->ht_head->nuser_htid), memory_order_relaxed);
	return (GET_HTID(nuser_htid) == shm_ht_loc->ht_id);
}

bool
shm_ht_rec_delete(struct d_shm_ht_loc *shm_ht_loc, const char *key, const int ksize)
{
	unsigned int          hash;
	unsigned int          idx;
	unsigned int          idx_lock;
	d_shm_mutex_t        *p_ht_lock;
	long int              off_next;
	long int             *p_off_list;
	struct shm_ht_rec    *rec;
	struct shm_ht_rec    *rec_prev = NULL;
	struct shm_ht_rec    *rec_next = NULL;
	struct d_shm_ht_head *ht_head;
	bool                  update_nuser;

	ht_head    = shm_ht_loc->ht_head;
	hash       = d_hash_string_u32(key, ksize);
	idx        = hash & (ht_head->n_bucket - 1);
	idx_lock   = idx % ht_head->n_lock;
	p_ht_lock  = (d_shm_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));
	p_off_list = (long int *)((char *)p_ht_lock + sizeof(d_shm_mutex_t) * ht_head->n_lock);
	update_nuser = shm_ht_inc_nuser(shm_ht_loc);
	if (!update_nuser)
		/* ht is NOT valid */
		return false;

	shm_mutex_lock(&(p_ht_lock[idx_lock]), NULL);
	if (p_off_list[idx] == INVALID_OFFSET) {
		/* empty bucket */
		shm_mutex_unlock(&(p_ht_lock[idx_lock]));
		shm_ht_dec_nuser(shm_ht_loc);
		return false;
	}

	/* loop over all records in this bucket to find the key */
	off_next = p_off_list[idx];
	/* record is not empty */
	while (off_next != INVALID_OFFSET) {
		rec = (struct shm_ht_rec *)((char *)d_shm_head + off_next);
		if (ksize == rec->len_key) {
			if (memcmp(key, (char *)rec + sizeof(struct shm_ht_rec), ksize) == 0) {
				/* found the record for the key, then remove it from the link
				 * list.
				 */
				if (rec->prev != INVALID_OFFSET) {
					rec_prev =
					    (struct shm_ht_rec *)((char *)d_shm_head + rec->prev);
					rec_prev->next = rec->next;
				} else {
					/* This is the first record in this bucket */
					p_off_list[idx] = rec->next;
				}

				/* next record in this bucket exists */
				if (rec->next != INVALID_OFFSET) {
					rec_next =
					    (struct shm_ht_rec *)((char *)d_shm_head + rec->next);
					rec_next->prev = rec->prev;
				}
				shm_mutex_unlock(&(p_ht_lock[idx_lock]));
				shm_free(rec);
				shm_ht_dec_nuser(shm_ht_loc);

				return true;
			}
		}
		off_next = rec->next;
	}
	shm_mutex_unlock(&(p_ht_lock[idx_lock]));
	shm_ht_dec_nuser(shm_ht_loc);

	return false;
}

bool
shm_ht_rec_delete_at(struct d_shm_ht_loc *shm_ht_loc, struct shm_ht_rec *link)
{
	unsigned int          idx_lock;
	long int             *p_off_list;
	struct shm_ht_rec    *rec_prev = NULL;
	struct shm_ht_rec    *rec_next = NULL;
	d_shm_mutex_t        *p_ht_lock;
	struct d_shm_ht_head *ht_head;
	bool                  update_nuser;

	if (link == NULL)
		return false;

	ht_head   = shm_ht_loc->ht_head;
	idx_lock  = link->idx % ht_head->n_lock;
	p_ht_lock = (d_shm_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));

	update_nuser = shm_ht_inc_nuser(shm_ht_loc);
	if (!update_nuser)
		/* ht is NOT valid */
		return false;

	shm_mutex_lock(&(p_ht_lock[idx_lock]), NULL);
	if (link->prev != INVALID_OFFSET) {
		rec_prev       = (struct shm_ht_rec *)((char *)d_shm_head + link->prev);
		rec_prev->next = link->next;
	} else {
		/* This is the first record in this bucket */
		p_off_list = (long int *)((char *)ht_head + sizeof(struct d_shm_ht_head) +
					  sizeof(d_shm_mutex_t) * ht_head->n_lock);
		p_off_list[link->idx] = INVALID_OFFSET;
	}
	if (link->next != INVALID_OFFSET) {
		rec_next       = (struct shm_ht_rec *)((char *)d_shm_head + link->next);
		rec_next->prev = link->prev;
	}
	shm_mutex_unlock(&(p_ht_lock[idx_lock]));

	shm_free(link);
	shm_ht_dec_nuser(shm_ht_loc);

	return true;
}

void
shm_ht_destroy(struct d_shm_ht_loc *shm_ht_loc)
{
	int                   i;
	int                   n_bucket;
	int                   n_lock;
	int                   nuser;
	d_shm_mutex_t        *p_ht_lock;
	long int              off_next;
	long int             *p_off_list;
	struct shm_ht_rec    *rec;
	struct d_shm_ht_head *ht_head;
	struct d_shm_ht_head *ht_head_prev;
	struct d_shm_ht_head *ht_head_next;

	ht_head = shm_ht_loc->ht_head;
	shm_mutex_lock(&(d_shm_head->ht_lock), NULL);

	if (GET_HTID(ht_head->nuser_htid) == INVALID_HT_ID) {
		shm_mutex_unlock(&(d_shm_head->ht_lock));
		DS_WARN(EINVAL, "the hash table was freed already");
		return;
	} else if (GET_HTID(ht_head->nuser_htid) != shm_ht_loc->ht_id) {
		shm_mutex_unlock(&(d_shm_head->ht_lock));
		DS_WARN(EINVAL, "ht_id does not match the hash table");
		return;
	}

	/* invalid ht id to alert other process do not use this ht any more */
	shm_ht_invalidate_htid(shm_ht_loc);

	n_lock     = ht_head->n_lock;
	p_ht_lock  = (d_shm_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));
	p_off_list = (long int *)((char *)p_ht_lock + sizeof(d_shm_mutex_t) * n_lock);
	/* acquire all locks before make any changes */
	for (i = 0; i < n_lock; i++) {
		shm_mutex_lock(&(p_ht_lock[i]), NULL);
	}

	n_bucket = ht_head->n_bucket;
	/* free record in buckets of hash table */
	for (i = 0; i < n_bucket; i++) {
		/* free records in one bucket */
		off_next = p_off_list[i];
		while (off_next != INVALID_OFFSET) {
			rec           = (struct shm_ht_rec *)((char *)d_shm_head + off_next);
			p_off_list[i] = rec->next;
			rec->prev     = INVALID_OFFSET;
			rec->next     = INVALID_OFFSET;
			shm_free(rec);
			off_next      = p_off_list[i];
		}
	}

	/* remove the hash table from link list */
	if (ht_head->prev != INVALID_OFFSET) {
		ht_head_prev       = (struct d_shm_ht_head *)((char *)d_shm_head + ht_head->prev);
		ht_head_prev->next = ht_head->next;
	} else {
		/* this is the first hash table */
		d_shm_head->off_ht_head = ht_head->next;
	}

	if (ht_head->next != INVALID_OFFSET) {
		ht_head_next       = (struct d_shm_ht_head *)((char *)d_shm_head + ht_head->next);
		ht_head_next->prev = ht_head->prev;
	}

	/* unlock all locks */
	for (i = 0; i < n_lock; i++) {
		shm_mutex_unlock(&(p_ht_lock[i]));
	}

	shm_mutex_unlock(&(d_shm_head->ht_lock));

	nuser = GET_NUSER(atomic_load_explicit(&(ht_head->nuser_htid), memory_order_relaxed));
	if (nuser == 0)
		/* free hash table buckets and locks */
		shm_free(ht_head);

	/* If a thread is accessing this hash table or the thread crashed during accessing this
	 * hash table, nuser will be non-zero. The memory of this hash table head is leaked then.
	 * It is still better than possible data corruption. We do not create/destroy hash tables
	 * anyway. It should not be an issue.
	 */
}

int
get_shm_ht_with_name(const char *name, struct d_shm_ht_loc *shm_ht_loc)
{
	long int              offset;
	struct d_shm_ht_head *head;

	if (shm_ht_loc == NULL)
		return EINVAL;

	shm_ht_loc->ht_head = NULL;
	shm_mutex_lock(&(d_shm_head->ht_lock), NULL);

	/* no hash table in shared memory region at all */
	if (d_shm_head->off_ht_head == INVALID_OFFSET)
		goto err;

	offset = d_shm_head->off_ht_head;
	while (offset != INVALID_OFFSET) {
		head = (struct d_shm_ht_head *)((char *)d_shm_head + offset);
		if (strncmp(name, head->ht_name, MAX_HT_NAME_LEN) == 0) {
			/* found the hash table with given name */
			if (GET_HTID(head->nuser_htid) == INVALID_HT_ID) {
				/* ht exists but not ready for use */
				shm_mutex_unlock(&(d_shm_head->ht_lock));
				return ENOENT;
			}
			shm_ht_loc->ht_head = head;
			shm_ht_loc->ht_id   = GET_HTID(head->nuser_htid);
			shm_mutex_unlock(&(d_shm_head->ht_lock));
			return 0;
		}
		if (head->next == INVALID_OFFSET)
			/* reaching the end of link list and hash table with target name not found
			 */
			goto err;
		offset = head->next;
	}

err:
	shm_mutex_unlock(&(d_shm_head->ht_lock));
	return ENOENT;
}

void *
shm_ht_rec_find(struct d_shm_ht_loc *shm_ht_loc, const char *key, const int len_key,
		struct shm_ht_rec **link)
{
	unsigned int          hash;
	unsigned int          idx;
	unsigned int          idx_lock;
	d_shm_mutex_t        *p_ht_lock;
	long int              off_next;
	long int             *p_off_list;
	struct shm_ht_rec    *rec;
	char                 *value = NULL;
	struct d_shm_ht_head *ht_head;
	bool                  update_nuser;

	if (link)
		*link = NULL;

	ht_head = shm_ht_loc->ht_head;
	hash       = d_hash_string_u32(key, len_key);
	idx        = hash & (ht_head->n_bucket - 1);
	idx_lock   = idx % ht_head->n_lock;
	p_ht_lock  = (d_shm_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));
	p_off_list = (long int *)((char *)p_ht_lock + sizeof(d_shm_mutex_t) * ht_head->n_lock);

	update_nuser = shm_ht_inc_nuser(shm_ht_loc);
	if (!update_nuser) {
		/* ht is NOT valid */
		errno = EFAULT;
		return NULL;
	}

	shm_mutex_lock(&(p_ht_lock[idx_lock]), NULL);

	if (p_off_list[idx] == INVALID_OFFSET) {
		/* bucket is empty */
		shm_mutex_unlock(&(p_ht_lock[idx_lock]));
		shm_ht_dec_nuser(shm_ht_loc);
		return NULL;
	}
	off_next = p_off_list[idx];
	while (off_next != INVALID_OFFSET) {
		rec = (struct shm_ht_rec *)((char *)d_shm_head + off_next);
		if (len_key == rec->len_key) {
			if (memcmp(key, (char *)rec + sizeof(struct shm_ht_rec), len_key) == 0) {
				/* found the record */
				value = (char *)rec + sizeof(struct shm_ht_rec) + len_key +
					rec->len_padding;
				shm_mutex_unlock(&(p_ht_lock[idx_lock]));
				shm_ht_dec_nuser(shm_ht_loc);
				if (link)
					*link = rec;
				return value;
			}
		}
		off_next = rec->next;
	}

	shm_mutex_unlock(&(p_ht_lock[idx_lock]));
	shm_ht_dec_nuser(shm_ht_loc);
	return NULL;
}

void *
shm_ht_rec_find_insert(struct d_shm_ht_loc *shm_ht_loc, const char *key, const int len_key,
		       const char *val, const int len_value, struct shm_ht_rec **link)
{
	unsigned int          hash;
	unsigned int          idx;
	unsigned int          idx_lock;
	d_shm_mutex_t        *p_ht_lock;
	long int              off_next;
	long int             *p_off_list;
	struct shm_ht_rec    *rec      = NULL;
	struct shm_ht_rec    *rec_next = NULL;
	char                 *value    = NULL;
	int                   err_save;
	int                   rc;
	struct d_shm_ht_head *ht_head;
	bool                  update_nuser;

	if (link)
		*link = NULL;

	ht_head = shm_ht_loc->ht_head;
	hash       = d_hash_string_u32(key, len_key);
	idx        = hash & (ht_head->n_bucket - 1);
	idx_lock   = idx % ht_head->n_lock;
	p_ht_lock  = (d_shm_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));
	p_off_list = (long int *)((char *)p_ht_lock + sizeof(d_shm_mutex_t) * ht_head->n_lock);

	update_nuser = shm_ht_inc_nuser(shm_ht_loc);
	if (!update_nuser) {
		/* ht is NOT valid */
		errno = EFAULT;
		return NULL;
	}

	shm_mutex_lock(&(p_ht_lock[idx_lock]), NULL);

	if (p_off_list[idx] != INVALID_OFFSET) {
		/* bucket is not empty */
		off_next = p_off_list[idx];
		while (off_next != INVALID_OFFSET) {
			rec = (struct shm_ht_rec *)((char *)d_shm_head + off_next);
			if (len_key == rec->len_key) {
				if (memcmp(key, (char *)rec + sizeof(struct shm_ht_rec), len_key) ==
				    0) {
					/* found the key, then return value */
					value = (char *)rec + sizeof(struct shm_ht_rec) + len_key +
						rec->len_padding;
					shm_mutex_unlock(&(p_ht_lock[idx_lock]));
					if (link)
						*link = rec;
					shm_ht_dec_nuser(shm_ht_loc);
					return value;
				}
			}
			off_next = rec->next;
		}
	}
	/* record is not found. Insert it at the very beginning of the link list. */
	rec = (struct shm_ht_rec *)shm_memalign(SHM_MEM_ALIGN,
						sizeof(struct shm_ht_rec) + len_key + len_value);
	if (rec == NULL) {
		err_save = ENOMEM;
		goto err;
	}
	rec->len_key     = len_key;
	/* add padding space to make sure value is aligned by SHM_MEM_ALIGN */
	rec->len_padding =
	    (len_key & (SHM_MEM_ALIGN - 1)) ? (SHM_MEM_ALIGN - (len_key & (SHM_MEM_ALIGN - 1))) : 0;
	rec->len_value   = len_value;
	rec->next        = INVALID_OFFSET;
	memcpy((char *)rec + sizeof(struct shm_ht_rec), key, len_key);
	value = (char *)rec + sizeof(struct shm_ht_rec) + len_key + rec->len_padding;

	if (strcmp(val, INIT_KEY_VALUE_MUTEX) == 0) {
		/* value holds a pthread mutex lock */
		rc = shm_mutex_init((d_shm_mutex_t *)value);
		if (rc != 0) {
			DS_ERROR(rc, "shm_mutex_init() failed");
			err_save = rc;
			goto err;
		}
	} else {
		/* set value */
		memcpy(value, val, len_value);
	}

	rec->idx        = idx;
	/* put the new record at the head of the link list */
	rec->prev       = INVALID_OFFSET;
	rec->next       = p_off_list[idx];
	p_off_list[idx] = (long int)((char *)rec - (char *)d_shm_head);
	if (rec->next != INVALID_OFFSET) {
		/* the next record exist */
		rec_next       = (struct shm_ht_rec *)((char *)d_shm_head + rec->next);
		rec_next->prev = p_off_list[idx];
	}

	shm_mutex_unlock(&(p_ht_lock[idx_lock]));
	if (link)
		*link = rec;
	shm_ht_dec_nuser(shm_ht_loc);
	return value;

err:
	shm_mutex_unlock(&(p_ht_lock[idx_lock]));
	shm_ht_dec_nuser(shm_ht_loc);
	shm_free(rec);
	errno = err_save;
	return NULL;
}
