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

int
shm_ht_create(const char name[], int bits, int n_lock, struct d_shm_ht_head **ht_head)
{
	int                   i;
	struct d_shm_ht_head *ht_head_loc;
	long int             *off_next;
	long int              offset;
	d_shm_mutex_t        *p_locks;
	int                   len_name;
	int                   n_bucket;
	int                   rc;

	*ht_head = NULL;
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
			ht_head_loc = (struct d_shm_ht_head *)((char *)d_shm_head + offset);
			if ((strncmp(name, ht_head_loc->ht_name, MAX_HT_NAME_LEN) == 0)) {
				/* found existing hash table with given name */
				if ((ht_head_loc->n_bucket == n_bucket) &&
				    (ht_head_loc->n_lock == n_lock)) {
					*ht_head = ht_head_loc;
					shm_mutex_unlock(&(d_shm_head->ht_lock));
					return 0;
				} else {
					DS_ERROR(EINVAL,
						 "hash table with different parameters exists");
					rc = EINVAL;
					goto err;
				}
			}

			if (ht_head_loc->next == INVALID_OFFSET) {
				/* reaching the end of the link list of existing hash tables */
				*ht_head = NULL;
				break;
			}
			offset = ht_head_loc->next;
		}
	}

	/* This hash table does not exist, then create it. */
	*ht_head = shm_alloc(sizeof(struct d_shm_ht_head) + (sizeof(d_shm_mutex_t) * n_lock) +
			     (sizeof(long int) * n_bucket));
	if (*ht_head == NULL) {
		rc = ENOMEM;
		goto err;
	}
	ht_head_loc = *ht_head;

	memcpy(ht_head_loc->ht_name, name, len_name + 1);
	ht_head_loc->n_bucket = n_bucket;
	ht_head_loc->n_lock   = n_lock;

	p_locks = (d_shm_mutex_t *)((char *)ht_head_loc + sizeof(struct d_shm_ht_head));
	for (i = 0; i < n_lock; i++) {
		if (shm_mutex_init(&(p_locks[i])) != 0) {
			DS_ERROR(errno, "shm_mutex_init() failed");
			rc = errno;
			goto err;
		}
	}
	off_next = (long int *)((char *)ht_head_loc + sizeof(struct d_shm_ht_head) +
				(sizeof(d_shm_mutex_t) * n_lock));
	for (i = 0; i < n_bucket; i++)
		off_next[i] = INVALID_OFFSET;
	/* insert the new hash table header as the first one of the link list */
	ht_head_loc->prev       = INVALID_OFFSET;
	ht_head_loc->next       = d_shm_head->off_ht_head;
	d_shm_head->off_ht_head = (long int)((char *)ht_head_loc - (char *)d_shm_head);

	shm_mutex_unlock(&(d_shm_head->ht_lock));
	return 0;

err:
	shm_mutex_unlock(&(d_shm_head->ht_lock));
	return rc;
}

bool
shm_ht_rec_delete(struct d_shm_ht_head *ht_head, const char *key, const int ksize)
{
	unsigned int       hash;
	unsigned int       idx;
	unsigned int       idx_lock;
	d_shm_mutex_t     *p_ht_lock;
	long int           off_next;
	long int          *p_off_list;
	struct shm_ht_rec *rec;
	struct shm_ht_rec *rec_prev = NULL;
	struct shm_ht_rec *rec_next = NULL;

	hash       = d_hash_string_u32(key, ksize);
	idx        = hash & (ht_head->n_bucket - 1);
	idx_lock   = idx % ht_head->n_lock;
	p_ht_lock  = (d_shm_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));
	p_off_list = (long int *)((char *)p_ht_lock + sizeof(d_shm_mutex_t) * ht_head->n_lock);
	shm_mutex_lock(&(p_ht_lock[idx_lock]), NULL);
	if (p_off_list[idx] == INVALID_OFFSET) {
		/* empty bucket */
		shm_mutex_unlock(&(p_ht_lock[idx_lock]));
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

				return true;
			}
		}
		off_next = rec->next;
	}
	shm_mutex_unlock(&(p_ht_lock[idx_lock]));

	return false;
}

bool
shm_ht_rec_delete_at(struct d_shm_ht_head *ht_head, struct shm_ht_rec *link)
{
	unsigned int       idx_lock;
	long int          *p_off_list;
	struct shm_ht_rec *rec_prev = NULL;
	struct shm_ht_rec *rec_next = NULL;
	d_shm_mutex_t     *p_ht_lock;

	if (link == NULL)
		return false;

	idx_lock  = link->idx % ht_head->n_lock;
	p_ht_lock = (d_shm_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));

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

	return true;
}

void
shm_ht_rec_decref(struct shm_ht_rec *link)
{
	atomic_fetch_add_relaxed(&(link->ref_count), -1);
}

void
shm_ht_rec_addref(struct shm_ht_rec *link)
{
	atomic_fetch_add_relaxed(&(link->ref_count), 1);
}

void
shm_ht_destroy(struct d_shm_ht_head *ht_head)
{
	int                   i;
	int                   n_bucket = ht_head->n_bucket;
	int                   n_lock   = ht_head->n_lock;
	d_shm_mutex_t        *p_ht_lock;
	unsigned int          idx_lock;
	long int              off_next;
	long int             *p_off_list;
	struct shm_ht_rec    *rec;
	struct d_shm_ht_head *ht_head_prev;
	struct d_shm_ht_head *ht_head_next;

	shm_mutex_lock(&(d_shm_head->ht_lock), NULL);
	/* free record in buckets of hash table */
	for (i = 0; i < n_bucket; i++) {
		p_ht_lock  = (d_shm_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));
		p_off_list = (long int *)((char *)p_ht_lock + sizeof(d_shm_mutex_t) * n_lock);
		idx_lock   = i % n_lock;
		shm_mutex_lock(&(p_ht_lock[idx_lock]), NULL);

		/* free records in one bucket */
		off_next = p_off_list[i];
		while (off_next != INVALID_OFFSET) {
			rec           = (struct shm_ht_rec *)((char *)d_shm_head + off_next);
			p_off_list[i] = rec->next;
			shm_free(rec);
			off_next = p_off_list[i];
		}

		shm_mutex_unlock(&(p_ht_lock[idx_lock]));
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

	shm_mutex_unlock(&(d_shm_head->ht_lock));
	/* free hash table buckets and locks */
	shm_free(ht_head);
}

int
get_shm_ht_with_name(const char *name, struct d_shm_ht_head **ht_head)
{
	long int              offset;
	struct d_shm_ht_head *head;

	if (ht_head == NULL)
		return EINVAL;

	*ht_head = NULL;
	/* no hash table in shared memory region at all */
	if (d_shm_head->off_ht_head == INVALID_OFFSET)
		return ENOENT;

	offset = d_shm_head->off_ht_head;
	while (offset != INVALID_OFFSET) {
		head = (struct d_shm_ht_head *)((char *)d_shm_head + offset);
		if (strncmp(name, head->ht_name, MAX_HT_NAME_LEN) == 0) {
			/* found the hash table with given name */
			*ht_head = head;
			return 0;
		}
		if (head->next == INVALID_OFFSET)
			/* reaching the end of link list and hash table with target name not found
			 */
			return ENOENT;
		offset = head->next;
	}

	return ENOENT;
}

void *
shm_ht_rec_find(struct d_shm_ht_head *ht_head, const char *key, const int len_key,
		struct shm_ht_rec **link)
{
	unsigned int       hash;
	unsigned int       idx;
	unsigned int       idx_lock;
	d_shm_mutex_t     *p_ht_lock;
	long int           off_next;
	long int          *p_off_list;
	struct shm_ht_rec *rec;
	char              *value = NULL;

	if (link)
		*link = NULL;
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
		rec = (struct shm_ht_rec *)((char *)d_shm_head + off_next);
		if (len_key == rec->len_key) {
			if (memcmp(key, (char *)rec + sizeof(struct shm_ht_rec), len_key) == 0) {
				/* found the record */
				value = (char *)rec + sizeof(struct shm_ht_rec) + len_key +
					rec->len_padding;
				atomic_fetch_add_relaxed(&(rec->ref_count), 1);
				shm_mutex_unlock(&(p_ht_lock[idx_lock]));
				if (link)
					*link = rec;
				return value;
			}
		}
		off_next = rec->next;
	}
	shm_mutex_unlock(&(p_ht_lock[idx_lock]));
	return NULL;
}

void *
shm_ht_rec_find_insert(struct d_shm_ht_head *ht_head, const char *key, const int len_key,
		       const char *val, const int len_value, struct shm_ht_rec **link)
{
	unsigned int       hash;
	unsigned int       idx;
	unsigned int       idx_lock;
	d_shm_mutex_t     *p_ht_lock;
	long int           off_next;
	long int          *p_off_list;
	struct shm_ht_rec *rec      = NULL;
	struct shm_ht_rec *rec_next = NULL;
	char              *value    = NULL;
	int                err_save;
	int                rc;

	if (link)
		*link = NULL;
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
			rec = (struct shm_ht_rec *)((char *)d_shm_head + off_next);
			if (len_key == rec->len_key) {
				if (memcmp(key, (char *)rec + sizeof(struct shm_ht_rec), len_key) ==
				    0) {
					/* found the key, then return value */
					value = (char *)rec + sizeof(struct shm_ht_rec) + len_key +
						rec->len_padding;
					atomic_fetch_add_relaxed(&(rec->ref_count), 1);
					shm_mutex_unlock(&(p_ht_lock[idx_lock]));
					if (link)
						*link = rec;
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
	atomic_store_relaxed(&(rec->ref_count), 1);

	shm_mutex_unlock(&(p_ht_lock[idx_lock]));
	if (link)
		*link = rec;
	return value;

err:
	shm_free(rec);
	errno = err_save;
	return NULL;
}
