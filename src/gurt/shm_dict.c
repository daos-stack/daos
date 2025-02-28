/**
 * (C) Copyright 2024 Intel Corporation.
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
#define NREF_MASK (0xFFFF000000000000L)
#define HT_ID_MASK (0xFFFFFFFFFFFFL)
#define NREF_INC  (0x1000000000000L)
#define GET_NREF(x) (((x) & 0xFFFF000000000000L) >> 48)
#define GET_HTID(x) ((x) & HT_ID_MASK)

#if defined(__x86_64__)
/* efficient way to generate random number with time stamp counter on x86_64 */
static __inline__ unsigned long long
rdtsc(void)
{
	unsigned hi, lo;

	__asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
	return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}
#endif

/**
 * atomically check ht id and update (inc / dec) number of ht reference. This can avoid updating an
 * invalid ht
 */
static int
shm_ht_update_nref(d_shm_ht_loc_t ht_loc, int64_t change)
{
	int64_t old_nref_htid, old_nref, saved_ht_id, *addr_old_nref_htid;
	int64_t new_nref_htid, ht_id;

	saved_ht_id        = ht_loc->ht_id;
	addr_old_nref_htid = (int64_t *)&(ht_loc->ht_head->nref_htid);
	old_nref_htid      = atomic_load_explicit((_Atomic int64_t *)addr_old_nref_htid,
						  memory_order_relaxed);
	ht_id              = GET_HTID(old_nref_htid);
	if (saved_ht_id != ht_id)
		/* hash table is not valid any more */
		return SHM_HT_INVALID_HT;

	old_nref = GET_NREF(old_nref_htid);
	/* sanity check */
	if (old_nref <= 0 && change < 0) {
		DS_ERROR(EINVAL, "negative number of hash table reference");
		return SHM_HT_NEGATIVE_REF;
	}

	new_nref_htid = old_nref_htid + change;
	while (1) {
		if (atomic_compare_exchange_weak((_Atomic int64_t *)addr_old_nref_htid,
						 &old_nref_htid, new_nref_htid)) {
			return SHM_HT_SUCCESS;
		} else {
			/* failed to exchange */
			ht_id = GET_HTID(old_nref_htid);
			if (saved_ht_id != ht_id) {
				/* hash table is not valid any more */
				return SHM_HT_INVALID_HT;
			} else {
				/* hash table is still valid, but nref was updated by other user */
				old_nref = GET_NREF(old_nref_htid);
				if (old_nref <= 0 && change < 0) {
					DS_WARN(EINVAL, "negative number of hash table reference");
					return SHM_HT_NEGATIVE_REF;
				}
				new_nref_htid = old_nref_htid + change;
			}
		}
	}
	/* not supposed to be here! */
	D_ASSERT(0);
	return SHM_HT_SUCCESS;
}

int
shm_ht_num_ref(d_shm_ht_loc_t shm_ht_loc)
{
	int64_t nref_htid;

	nref_htid = atomic_load_explicit(&(shm_ht_loc->ht_head->nref_htid), memory_order_relaxed);
	/* check ht is valid or not */
	return GET_HTID(nref_htid) == shm_ht_loc->ht_id ? (GET_NREF(nref_htid)) : (-1);
}

static int
shm_ht_incref(d_shm_ht_loc_t ht_loc)
{
	return shm_ht_update_nref(ht_loc, NREF_INC);
}

int
shm_ht_decref(d_shm_ht_loc_t ht_loc)
{
	return shm_ht_update_nref(ht_loc, -NREF_INC);
}

int
shm_ht_create(const char name[], int bits, int n_lock, d_shm_ht_loc_t shm_ht_loc)
{
	int              i;
	d_shm_ht_head_t  ht_head_tmp;
	long int        *off_next;
	long int         offset;
	d_shm_mutex_t   *p_locks;
	int              len_name;
	int              n_bucket;
	int              rc;
	uint64_t         ht_id;

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
			ht_head_tmp = (d_shm_ht_head_t)((char *)d_shm_head + offset);
			if ((strncmp(name, ht_head_tmp->ht_name, MAX_HT_NAME_LEN) == 0)) {
				/* found existing hash table with given name */
				if ((ht_head_tmp->n_bucket == n_bucket) &&
				    (ht_head_tmp->n_lock == n_lock)) {
					shm_ht_loc->ht_head = ht_head_tmp;
					shm_ht_loc->ht_id = GET_HTID(ht_head_tmp->nref_htid);
					shm_mutex_unlock(&(d_shm_head->ht_lock));
					/* increase ht reference count */
					return shm_ht_incref(shm_ht_loc);
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

	atomic_store_relaxed(&(ht_head_tmp->nref_htid), INVALID_HT_ID);
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
	/* set ht reference count 1 for this new ht */
	atomic_store_relaxed(&(ht_head_tmp->nref_htid), ht_id + NREF_INC);
	shm_ht_loc->ht_id  = ht_id;

	shm_mutex_unlock(&(d_shm_head->ht_lock));
	return SHM_HT_SUCCESS;

err:
	shm_mutex_unlock(&(d_shm_head->ht_lock));
	return rc;
}

int
shm_ht_open_with_name(const char *name, d_shm_ht_loc_t shm_ht_loc)
{
	long int        offset;
	d_shm_ht_head_t head;

	if (shm_ht_loc == NULL)
		return EINVAL;

	shm_ht_loc->ht_head = NULL;
	shm_mutex_lock(&(d_shm_head->ht_lock), NULL);

	/* no hash table in shared memory region at all */
	if (d_shm_head->off_ht_head == INVALID_OFFSET)
		goto err;

	offset = d_shm_head->off_ht_head;
	while (offset != INVALID_OFFSET) {
		head = (d_shm_ht_head_t)((char *)d_shm_head + offset);
		if (strncmp(name, head->ht_name, MAX_HT_NAME_LEN) == 0) {
			/* found the hash table with given name */
			if (GET_HTID(head->nref_htid) == INVALID_HT_ID) {
				/* ht exists but not ready for use */
				shm_mutex_unlock(&(d_shm_head->ht_lock));
				return ENOENT;
			}
			shm_ht_loc->ht_head = head;
			shm_ht_loc->ht_id   = GET_HTID(head->nref_htid);
			shm_mutex_unlock(&(d_shm_head->ht_lock));
			return shm_ht_incref(shm_ht_loc);
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

/* clear ht id in nref_htid field when destroying a ht */
static int
shm_ht_invalidate_htid(d_shm_ht_loc_t ht_loc, bool force, int *num_ref)
{
	int64_t old_nref_htid, old_nref, saved_ht_id, *addr_old_nref_htid;
	int64_t new_nref_htid, ht_id;

	saved_ht_id        = ht_loc->ht_id;
	addr_old_nref_htid = (int64_t *)&(ht_loc->ht_head->nref_htid);

	if (!force) {
		/* invalidate htid only if reference count is 0 */
		old_nref_htid = saved_ht_id;
		new_nref_htid = 0;
		if (atomic_compare_exchange_weak((_Atomic int64_t *)addr_old_nref_htid,
						 &old_nref_htid, new_nref_htid)) {
			*num_ref = GET_NREF(old_nref_htid);
			return SHM_HT_SUCCESS;
		} else {
			*num_ref = GET_NREF(old_nref_htid);
			if (*num_ref > 0)
				return SHM_HT_BUSY;
			else if (*num_ref < 0)
				return SHM_HT_NEGATIVE_REF;
			else
				return SHM_HT_INVALID_HT;
		}
	}

	old_nref_htid = atomic_load_explicit((_Atomic int64_t *)addr_old_nref_htid,
					     memory_order_relaxed);
	ht_id         = GET_HTID(old_nref_htid);
	if (saved_ht_id == INVALID_HT_ID) {
		/* hash table was invalidated already */
		DS_ERROR(EINVAL, "hash table has been invalidated already");
		return SHM_HT_INVALID_HT;
	} else if (saved_ht_id != ht_id) {
		DS_ERROR(EINVAL, "inconsistent hash table id");
		return SHM_HT_INVALID_HT;
	}

	old_nref = GET_NREF(old_nref_htid);
	/* sanity check */
	if (old_nref < 0) {
		DS_ERROR(EINVAL, "negative number of hash table reference");
		return SHM_HT_NEGATIVE_REF;
	}

	new_nref_htid = (old_nref_htid & NREF_MASK) + INVALID_HT_ID;
	while (1) {
		if (atomic_compare_exchange_weak((_Atomic int64_t *)addr_old_nref_htid,
						 &old_nref_htid, new_nref_htid)) {
			*num_ref = GET_NREF(old_nref_htid);
			return SHM_HT_SUCCESS;
		} else {
			/* failed to exchange */
			ht_id = GET_HTID(old_nref_htid);
			if (saved_ht_id != ht_id) {
				return SHM_HT_INVALID_HT;
			} else {
				/* nref was updated by another user */
				if (old_nref < 0) {
					DS_ERROR(EINVAL, "negative number of hash table reference");
					return SHM_HT_NEGATIVE_REF;
				}
				new_nref_htid = (old_nref_htid & NREF_MASK) + INVALID_HT_ID;
			}
		}
	}
	return SHM_HT_INVALID_HT;
}

bool
shm_ht_is_usable(d_shm_ht_loc_t shm_ht_loc)
{
	int64_t nref_htid;

	nref_htid = atomic_load_explicit(&(shm_ht_loc->ht_head->nref_htid), memory_order_relaxed);
	return (GET_HTID(nref_htid) == shm_ht_loc->ht_id);
}

int
shm_ht_destroy(d_shm_ht_loc_t shm_ht_loc, bool force)
{
	int             rc;
	int             i;
	int             n_bucket;
	int             n_lock;
	int             num_ref;
	int             num_rec_ref;
	d_shm_mutex_t  *p_ht_lock;
	long int        off_next;
	long int       *p_off_list;
	d_shm_ht_rec_t  rec;
	d_shm_ht_head_t ht_head;
	d_shm_ht_head_t ht_head_prev;
	d_shm_ht_head_t ht_head_next;

	if (!shm_ht_is_usable(shm_ht_loc))
		return SHM_HT_INVALID_HT;

	/* invalid ht id to alert other processes do not use this ht any more */
	rc = shm_ht_invalidate_htid(shm_ht_loc, force,  &num_ref);
	if (rc)
		return rc;

	ht_head    = shm_ht_loc->ht_head;
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
			rec         = (d_shm_ht_rec_t)((char *)d_shm_head + off_next);
			num_rec_ref = atomic_load_explicit(&(rec->ref_count), memory_order_relaxed);
			if (num_rec_ref == 0) {
				/* free the record only if ref_count is zero */
				p_off_list[i] = rec->next;
				rec->prev     = INVALID_OFFSET;
				rec->next     = INVALID_OFFSET;
				shm_free(rec);
			}
			off_next = p_off_list[i];
		}
	}

	/* unlock all locks */
	for (i = 0; i < n_lock; i++) {
		shm_mutex_unlock(&(p_ht_lock[i]));
	}

	shm_mutex_lock(&(d_shm_head->ht_lock), NULL);
	/* remove the hash table from link list */
	if (ht_head->prev != INVALID_OFFSET) {
		ht_head_prev       = (d_shm_ht_head_t)((char *)d_shm_head + ht_head->prev);
		ht_head_prev->next = ht_head->next;
	} else {
		/* this is the first hash table */
		d_shm_head->off_ht_head = ht_head->next;
	}

	if (ht_head->next != INVALID_OFFSET) {
		ht_head_next       = (d_shm_ht_head_t)((char *)d_shm_head + ht_head->next);
		ht_head_next->prev = ht_head->prev;
	}
	if (num_ref == 0)
		/* free hash table head only if reference count is zero */
		shm_free(ht_head);

	shm_mutex_unlock(&(d_shm_head->ht_lock));

	/* If a thread crashed during accessing this hash table, nref will be non-zero. The memory
	 * of this hash table head is leaked then. It is still better than possible data corruption
	 * caused by freeing the memory by force. We do not create/destroy hash tables frequently.
	 * It should not be a big issue.
	 */
	return SHM_HT_SUCCESS;
}

inline int
shm_ht_rec_incref(d_shm_ht_rec_t link)
{
	atomic_fetch_add_relaxed(&(link->ref_count), 1);
	return SHM_HT_SUCCESS;
}

int
shm_ht_rec_decref(d_shm_ht_rec_loc_t link_loc)
{
	if (shm_ht_is_usable(&(link_loc->ht_head_loc))) {
		atomic_fetch_add_relaxed(&(link_loc->ht_rec->ref_count), -1);
		return SHM_HT_SUCCESS;
	} else {
		return SHM_HT_INVALID_HT;
	}
}

int
shm_ht_rec_delete(d_shm_ht_loc_t shm_ht_loc, const char *key, const int ksize)
{
	unsigned int    hash;
	unsigned int    idx;
	unsigned int    idx_lock;
	d_shm_mutex_t  *p_ht_lock;
	long int        off_next;
	long int       *p_off_list;
	int             rec_ref_count;
	d_shm_ht_rec_t  rec;
	d_shm_ht_rec_t  rec_prev = NULL;
	d_shm_ht_rec_t  rec_next = NULL;
	d_shm_ht_head_t ht_head;

	if (!shm_ht_is_usable(shm_ht_loc))
		/* immediately return error if ht is not usable */
		return SHM_HT_INVALID_HT;

	ht_head    = shm_ht_loc->ht_head;
	hash       = d_hash_string_u32(key, ksize);
	idx        = hash & (ht_head->n_bucket - 1);
	idx_lock   = idx % ht_head->n_lock;
	p_ht_lock  = (d_shm_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));
	p_off_list = (long int *)((char *)p_ht_lock + sizeof(d_shm_mutex_t) * ht_head->n_lock);

	shm_mutex_lock(&(p_ht_lock[idx_lock]), NULL);
	if (p_off_list[idx] == INVALID_OFFSET) {
		/* empty bucket */
		shm_mutex_unlock(&(p_ht_lock[idx_lock]));
		return SHM_HT_REC_NOT_EXIST;
	}

	/* loop over all records in this bucket to find the key */
	off_next = p_off_list[idx];
	/* record is not empty */
	while (off_next != INVALID_OFFSET) {
		rec = (d_shm_ht_rec_t)((char *)d_shm_head + off_next);
		if (ksize == rec->len_key) {
			if (memcmp(key, (char *)rec + sizeof(struct d_shm_ht_rec), ksize) == 0) {
				/* found the record for the key, then remove it from the link
				 * list.
				 */
				if (rec->prev != INVALID_OFFSET) {
					rec_prev =
					    (d_shm_ht_rec_t)((char *)d_shm_head + rec->prev);
					rec_prev->next = rec->next;
				} else {
					/* This is the first record in this bucket */
					p_off_list[idx] = rec->next;
				}

				/* next record in this bucket exists */
				if (rec->next != INVALID_OFFSET) {
					rec_next =
					    (d_shm_ht_rec_t)((char *)d_shm_head + rec->next);
					rec_next->prev = rec->prev;
				}
				rec_ref_count = atomic_load_explicit(&(rec->ref_count),
								     memory_order_relaxed);
				if (rec_ref_count == 0) {
					/* free record memory only if reference count is zero */
					shm_free(rec);
					shm_mutex_unlock(&(p_ht_lock[idx_lock]));
					return SHM_HT_SUCCESS;
				} else {
					shm_mutex_unlock(&(p_ht_lock[idx_lock]));
					return SHM_HT_REC_BUSY;
				}
			}
		}
		off_next = rec->next;
	}
	shm_mutex_unlock(&(p_ht_lock[idx_lock]));

	return SHM_HT_REC_NOT_EXIST;
}

int
shm_ht_rec_delete_at(d_shm_ht_rec_loc_t link_loc)
{
	unsigned int    idx_lock;
	long int       *p_off_list;
	d_shm_ht_rec_t  link     = link_loc->ht_rec;
	d_shm_ht_rec_t  rec_prev = NULL;
	d_shm_ht_rec_t  rec_next = NULL;
	d_shm_mutex_t  *p_ht_lock;
	d_shm_ht_head_t ht_head;
	d_shm_ht_loc_t  shm_ht_loc;
	int             ref_count;

	if (link == NULL)
		return SHM_HT_REC_INVALID;
	shm_ht_loc = &(link_loc->ht_head_loc);
	if (!shm_ht_is_usable(shm_ht_loc))
		/* immediately return error if ht is not usable */
		return SHM_HT_INVALID_HT;

	ht_head   = shm_ht_loc->ht_head;
	idx_lock  = link->idx % ht_head->n_lock;
	p_ht_lock = (d_shm_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));

	shm_mutex_lock(&(p_ht_lock[idx_lock]), NULL);
	ref_count = atomic_load_explicit(&(link->ref_count), memory_order_relaxed);
	if (ref_count != 0) {
		shm_mutex_unlock(&(p_ht_lock[idx_lock]));
		return SHM_HT_REC_BUSY;
	}

	if (link->prev != INVALID_OFFSET) {
		rec_prev       = (d_shm_ht_rec_t)((char *)d_shm_head + link->prev);
		rec_prev->next = link->next;
	} else {
		/* This is the first record in this bucket */
		p_off_list = (long int *)((char *)ht_head + sizeof(struct d_shm_ht_head) +
					  sizeof(d_shm_mutex_t) * ht_head->n_lock);
		p_off_list[link->idx] = INVALID_OFFSET;
	}
	if (link->next != INVALID_OFFSET) {
		rec_next       = (d_shm_ht_rec_t)((char *)d_shm_head + link->next);
		rec_next->prev = link->prev;
	}
	shm_free(link);
	shm_mutex_unlock(&(p_ht_lock[idx_lock]));

	return SHM_HT_SUCCESS;
}

void *
shm_ht_rec_find(d_shm_ht_loc_t shm_ht_loc, const char *key, const int len_key,
		d_shm_ht_rec_loc_t link_loc, int *err)
{
	unsigned int    hash;
	unsigned int    idx;
	unsigned int    idx_lock;
	d_shm_mutex_t  *p_ht_lock;
	long int        off_next;
	long int       *p_off_list;
	d_shm_ht_rec_t  rec;
	char           *value = NULL;
	d_shm_ht_head_t ht_head;

	*err = SHM_HT_SUCCESS;
	if (!shm_ht_is_usable(shm_ht_loc)) {
		*err = SHM_HT_INVALID_HT;
		/* immediately return error if ht is not usable */
		return NULL;
	}

	if (link_loc) {
		link_loc->ht_head_loc.ht_head = shm_ht_loc->ht_head;
		link_loc->ht_head_loc.ht_id   = shm_ht_loc->ht_id;
		link_loc->ht_rec = NULL;
	}

	ht_head = shm_ht_loc->ht_head;
	hash       = d_hash_string_u32(key, len_key);
	idx        = hash & (ht_head->n_bucket - 1);
	idx_lock   = idx % ht_head->n_lock;
	p_ht_lock  = (d_shm_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));
	p_off_list = (long int *)((char *)p_ht_lock + sizeof(d_shm_mutex_t) * ht_head->n_lock);

	shm_mutex_lock(&(p_ht_lock[idx_lock]), NULL);

	if (p_off_list[idx] == INVALID_OFFSET) {
		/* bucket is empty */
		shm_mutex_unlock(&(p_ht_lock[idx_lock]));
		return NULL;
	}
	off_next = p_off_list[idx];
	while (off_next != INVALID_OFFSET) {
		rec = (d_shm_ht_rec_t)((char *)d_shm_head + off_next);
		if (len_key == rec->len_key) {
			if (memcmp(key, (char *)rec + sizeof(struct d_shm_ht_rec), len_key) == 0) {
				/* found the record */
				value = (char *)rec + sizeof(struct d_shm_ht_rec) + len_key +
					rec->len_padding;
				if (link_loc) {
					link_loc->ht_rec = rec;
					shm_ht_rec_incref(rec);
				}
				shm_mutex_unlock(&(p_ht_lock[idx_lock]));
				return value;
			}
		}
		off_next = rec->next;
	}

	shm_mutex_unlock(&(p_ht_lock[idx_lock]));
	return NULL;
}

void *
shm_ht_rec_find_insert(d_shm_ht_loc_t shm_ht_loc, const char *key, const int len_key,
		       const char *val, const int len_value, d_shm_ht_rec_loc_t link_loc,
		       int *err)
{
	unsigned int    hash;
	unsigned int    idx;
	unsigned int    idx_lock;
	d_shm_mutex_t  *p_ht_lock;
	long int        off_next;
	long int       *p_off_list;
	d_shm_ht_rec_t  rec      = NULL;
	d_shm_ht_rec_t  rec_next = NULL;
	char           *value    = NULL;
	int             rc;
	d_shm_ht_head_t ht_head;

	*err = SHM_HT_SUCCESS;
	if (!shm_ht_is_usable(shm_ht_loc)) {
		/* immediately return error if ht is not usable */
		*err = SHM_HT_INVALID_HT;
		return NULL;
	}

	if (link_loc) {
		link_loc->ht_head_loc.ht_head = shm_ht_loc->ht_head;
		link_loc->ht_head_loc.ht_id   = shm_ht_loc->ht_id;
		link_loc->ht_rec = NULL;
	}

	ht_head    = shm_ht_loc->ht_head;
	hash       = d_hash_string_u32(key, len_key);
	idx        = hash & (ht_head->n_bucket - 1);
	idx_lock   = idx % ht_head->n_lock;
	p_ht_lock  = (d_shm_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));
	p_off_list = (long int *)((char *)p_ht_lock + sizeof(d_shm_mutex_t) * ht_head->n_lock);

	shm_mutex_lock(&(p_ht_lock[idx_lock]), NULL);

	if (p_off_list[idx] != INVALID_OFFSET) {
		/* bucket is not empty */
		off_next = p_off_list[idx];
		while (off_next != INVALID_OFFSET) {
			rec = (d_shm_ht_rec_t)((char *)d_shm_head + off_next);
			if (len_key == rec->len_key) {
				if (memcmp(key, (char *)rec + sizeof(struct d_shm_ht_rec), len_key) ==
				    0) {
					/* found the key, then return value */
					value = (char *)rec + sizeof(struct d_shm_ht_rec) + len_key +
						rec->len_padding;
					if (link_loc) {
						link_loc->ht_rec = rec;
						shm_ht_rec_incref(rec);
					}
					shm_mutex_unlock(&(p_ht_lock[idx_lock]));
					return value;
				}
			}
			off_next = rec->next;
		}
	}
	/* record is not found. Insert it at the very beginning of the link list. */
	rec = (d_shm_ht_rec_t)shm_memalign(SHM_MEM_ALIGN,
						sizeof(struct d_shm_ht_rec) + len_key + len_value);
	if (rec == NULL) {
		*err = ENOMEM;
		goto err;
	}
	rec->len_key     = len_key;
	/* add padding space to make sure value is aligned by SHM_MEM_ALIGN */
	rec->len_padding =
	    (len_key & (SHM_MEM_ALIGN - 1)) ? (SHM_MEM_ALIGN - (len_key & (SHM_MEM_ALIGN - 1))) : 0;
	rec->len_value   = len_value;
	rec->next        = INVALID_OFFSET;
	memcpy((char *)rec + sizeof(struct d_shm_ht_rec), key, len_key);
	value = (char *)rec + sizeof(struct d_shm_ht_rec) + len_key + rec->len_padding;

	if (strcmp(val, INIT_KEY_VALUE_MUTEX) == 0) {
		/* value holds a pthread mutex lock */
		rc = shm_mutex_init((d_shm_mutex_t *)value);
		if (rc != 0) {
			DS_ERROR(rc, "shm_mutex_init() failed");
			*err = rc;
			goto err;
		}
	} else {
		/* set value */
		memcpy(value, val, len_value);
	}

	rec->idx        = idx;
	/* only non-null link_loc increase ref_count */
	atomic_store_relaxed(&(rec->ref_count), link_loc ? 1 : 0);
	/* put the new record at the head of the link list */
	rec->prev       = INVALID_OFFSET;
	rec->next       = p_off_list[idx];
	p_off_list[idx] = (long int)((char *)rec - (char *)d_shm_head);
	if (rec->next != INVALID_OFFSET) {
		/* the next record exist */
		rec_next       = (d_shm_ht_rec_t)((char *)d_shm_head + rec->next);
		rec_next->prev = p_off_list[idx];
	}

	if (link_loc)
		link_loc->ht_rec = rec;
	shm_mutex_unlock(&(p_ht_lock[idx_lock]));
	return value;

err:
	shm_mutex_unlock(&(p_ht_lock[idx_lock]));
	shm_free(rec);
	return NULL;
}

void *
shm_ht_rec_data(d_shm_ht_rec_loc_t rec_loc)
{
	void *data;
	d_shm_ht_rec_t rec;

	rec = rec_loc->ht_rec;
	if (rec == NULL)
		return NULL;
	data = (char *)rec + sizeof(struct d_shm_ht_rec) + rec->len_key + rec->len_padding;
	return data;
}

int
shm_ht_rec_num_ref(d_shm_ht_rec_loc_t rec_loc)
{
	if (rec_loc->ht_rec == NULL)
		/* invalid reference count */
		return (-1);

	return atomic_load_explicit(&(rec_loc->ht_rec->ref_count), memory_order_relaxed);
}
