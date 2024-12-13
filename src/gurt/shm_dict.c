/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include <gurt/common.h>
#include <gurt/shm_dict.h>
#include <gurt/shm_utils.h>

/* the address of shared memory region */
extern struct d_shm_alloc *d_shm_head;

/* the attribute set for rwlock located inside shared memory */
extern pthread_rwlockattr_t d_shm_rwlock_attr;

/* the attribute set for mutex located inside shared memory */
extern pthread_mutexattr_t  d_shm_mutex_attr;

int
shm_ht_create(const char name[], int bits, int n_lock, struct d_shm_ht_head **ht_head)
{
	int i;
	struct d_shm_ht_head *ht_head_loc;
	long int *off_next;
	long int offset;
	pthread_mutex_t *p_locks;
	int len_name;
	int n_bucket;

	*ht_head = NULL;
	len_name = strnlen(name, MAX_HT_NAME_LEN);
	if (len_name >= MAX_HT_NAME_LEN) {
		printf("hash table name is longer than %d bytes.\n", MAX_HT_NAME_LEN - 1);
		return EINVAL;
	}

	n_bucket = 1 << bits;
	shm_mutex_lock(&(d_shm_head->ht_lock));

	/* loop over existing hash tables to check whether it exists or not */
	if (d_shm_head->off_ht_head != INVALID_OFFSET) {
		offset = d_shm_head->off_ht_head;
		while (offset > 0) {
			ht_head_loc = (struct d_shm_ht_head *)((char *)d_shm_head + offset);
			if ((strncmp(name, ht_head_loc->ht_name, MAX_HT_NAME_LEN) == 0) &&
			    (ht_head_loc->n_bucket == n_bucket) &&
			    (ht_head_loc->n_lock == n_lock)) {
			    *ht_head = ht_head_loc;
			    break;
			}
			if (ht_head_loc->next == INVALID_OFFSET) {
				*ht_head = NULL;
				break;
			}
			offset = ht_head_loc->next;
		}
	}

	if (*ht_head) {
		shm_mutex_unlock(&(d_shm_head->ht_lock));
		return 0;
	}

	/* This hash table does not exist, then create it. */
	*ht_head = shm_alloc(sizeof(struct d_shm_ht_head) + (sizeof(pthread_mutex_t) * n_lock) +
			     (sizeof(long int) * n_bucket));
	if (*ht_head == NULL)
		return ENOMEM;
	ht_head_loc = *ht_head;

	memcpy(ht_head_loc->ht_name, name, len_name + 1);
	ht_head_loc->n_bucket = n_bucket;
	ht_head_loc->n_lock = n_lock;

	p_locks = (pthread_mutex_t *)((char *)ht_head_loc + sizeof(struct d_shm_ht_head));
	for (i = 0; i < n_lock; i++) {
		if(pthread_mutex_init(&(p_locks[i]), &d_shm_mutex_attr) != 0) {
			perror("pthread_mutex_init");
			return errno;
		}
	}
	off_next = (long int *)((char *)ht_head_loc + sizeof(struct d_shm_ht_head) +
		(sizeof(pthread_mutex_t) * n_lock));
	for (i = 0; i < n_bucket; i++)
		off_next[i] = INVALID_OFFSET;
	/* insert the new hash table as the first one */
	ht_head_loc->next = d_shm_head->off_ht_head;
	d_shm_head->off_ht_head = (long int)((char *)ht_head_loc - (char *)d_shm_head);

	shm_mutex_unlock(&(d_shm_head->ht_lock));
	return 0;
}

int
shm_ht_rec_delete(struct d_shm_ht_head *ht_head, const char *key, const int ksize)
{
	unsigned int hash;
	unsigned int idx;
	unsigned int idx_lock;
	pthread_mutex_t *p_ht_lock;
	long int off_next;
	long int *p_off_list;
	struct shm_ht_rec *rec;
	struct shm_ht_rec *rec_prev = NULL;
	struct shm_ht_rec *rec_next = NULL;

	hash = d_hash_string_u32(key, ksize);
	idx = hash & (ht_head->n_bucket - 1);
	idx_lock = (unsigned int)(idx * ht_head->n_lock * 1.0f / ht_head->n_bucket);
	p_ht_lock = (pthread_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));
	p_off_list = (long int *)((char *)p_ht_lock + sizeof(pthread_mutex_t) * ht_head->n_lock);
	shm_mutex_lock(&(p_ht_lock[idx_lock]));
	if (p_off_list[idx] < 0) {
		/* empty bucket */
		shm_mutex_unlock(&(p_ht_lock[idx_lock]));
		return 0;
	}

	/* loop over all records in this bucket to find the key */
	off_next = p_off_list[idx];
	while (off_next) {
		rec = (struct shm_ht_rec *)((char *)d_shm_head + off_next);
		if (ksize == rec->len_key) {
			if (memcmp(key, (char *)rec + sizeof(struct shm_ht_rec), ksize) == 0) {
				/* found the record for the key, then remove it from the link
				 * list.
				 */
				if (rec->prev != INVALID_OFFSET) {
					rec_prev = (struct shm_ht_rec *)((char *)d_shm_head + rec->prev);
					rec_prev->next = rec->next;
				}
				if (rec->next != INVALID_OFFSET) {
					rec_next = (struct shm_ht_rec *)((char *)d_shm_head + rec->next);
					rec_next->prev = rec->prev;
				}
				shm_mutex_unlock(&(p_ht_lock[idx_lock]));
				shm_free(rec);

				return 1;
			}
		}
		off_next = rec->next;
	}
	shm_mutex_unlock(&(p_ht_lock[idx_lock]));

	return 0;
}

int
shm_ht_rec_delete_at(struct d_shm_ht_head *ht_head, struct shm_ht_rec *link)
{
	int idx_lock = link->idx_lock;
	struct shm_ht_rec *rec_prev = NULL;
	struct shm_ht_rec *rec_next = NULL;
	pthread_mutex_t *p_ht_lock;

	assert(link != NULL);
	idx_lock = link->idx_lock;
	p_ht_lock = (pthread_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));

	shm_mutex_lock(&(p_ht_lock[idx_lock]));
	if (link->prev != INVALID_OFFSET) {
		rec_prev = (struct shm_ht_rec *)((char *)d_shm_head + link->prev);
		rec_prev->next = link->next;
	}
	if (link->next != INVALID_OFFSET) {
		rec_next = (struct shm_ht_rec *)((char *)d_shm_head + link->next);
		rec_next->prev = link->prev;
	}
	shm_mutex_unlock(&(p_ht_lock[idx_lock]));

	shm_free(link);

	return 0;
}

void
shm_ht_rec_decref(struct d_shm_ht_head *ht_head, struct shm_ht_rec *link)
{
	int idx_lock = link->idx_lock;
	pthread_mutex_t *p_ht_lock;

	// Use atomic OP instead????
	p_ht_lock = (pthread_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));
	shm_mutex_lock(&(p_ht_lock[idx_lock]));
	link->ref_count--;
	shm_mutex_unlock(&(p_ht_lock[idx_lock]));
}

void
shm_ht_rec_addref(struct d_shm_ht_head *ht_head, struct shm_ht_rec *link)
{
	int idx_lock = link->idx_lock;
	pthread_mutex_t *p_ht_lock;

	// Use atomic OP instead????
	p_ht_lock = (pthread_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));
	shm_mutex_lock(&(p_ht_lock[idx_lock]));
	link->ref_count++;
	shm_mutex_unlock(&(p_ht_lock[idx_lock]));
}

int
shm_ht_destroy(struct d_shm_ht_head *ht_head, int force)
{
	int i;
	int n_bucket = ht_head->n_bucket;
	int n_lock = ht_head->n_lock;
	pthread_mutex_t *p_ht_lock;
	long int off_next;
	long int *p_off_list;
	struct shm_ht_rec *rec;
	struct d_shm_ht_head *ht_head_prev;
	struct d_shm_ht_head *ht_head_next;

	/* free record in buckets of hash table */
	for (i = 0; i < n_bucket; i++) {
		p_ht_lock = (pthread_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));
		p_off_list = (long int *)((char *)p_ht_lock + sizeof(pthread_mutex_t) * n_lock);
		shm_mutex_lock(&(p_ht_lock[i]));

		off_next = p_off_list[i];
		while (off_next != INVALID_OFFSET) {
			rec = (struct shm_ht_rec *)((char *)d_shm_head + off_next);
			p_off_list[i] = rec->next;
			shm_free(rec);
			off_next = p_off_list[i];
		}

		shm_mutex_unlock(&(p_ht_lock[i]));
	}

	/* remove the hash table from link list */
	if (ht_head->prev != INVALID_OFFSET) {
		ht_head_prev = (struct d_shm_ht_head *)((char *)d_shm_head + ht_head->prev);
		ht_head_prev->next = ht_head->next;
	} else {
		/* this is the first hash table */
		d_shm_head->off_ht_head = ht_head->next;
	}

	if (ht_head->next != INVALID_OFFSET) {
		ht_head_next = (struct d_shm_ht_head *)((char *)d_shm_head + ht_head->next);
		ht_head_next->prev = ht_head->prev;
	}

	/* free hash table buckets and locks */
	shm_free(ht_head);

	return 0;
}

int
get_ht_with_name(const char *name, struct d_shm_ht_head **ht_head)
{
	long int offset;
	struct d_shm_ht_head *head;

	if (ht_head == NULL)
		return EINVAL;

	*ht_head = NULL;
	/* no hash table in shared memory region at all */
	if (d_shm_head->off_ht_head < 0)
		return 0;

	offset = d_shm_head->off_ht_head;
	while (offset > 0) {
		head = (struct d_shm_ht_head *)((char *)d_shm_head + offset);
		if (strncmp(name, head->ht_name, MAX_HT_NAME_LEN) == 0) {
			*ht_head = head;
			return 0;
		}
		if (head->next < 0)
			/* reaching the end of link list and hash table with target name not found */
			return 0;
		offset = head->next;
	}

	return 0;
}

void *
shm_ht_rec_find(struct d_shm_ht_head *ht_head, const char *key, const int len_key, struct shm_ht_rec
		**link)
{
	unsigned int hash;
	unsigned int idx;
	unsigned int idx_lock;
	pthread_mutex_t *p_ht_lock;
	long int off_next;
	long int *p_off_list;
	struct shm_ht_rec *rec;
	char *value = NULL;

	if (link)
		*link = NULL;
	hash = d_hash_string_u32(key, len_key);
	idx = hash & (ht_head->n_bucket - 1);
	idx_lock = (unsigned int)(idx * ht_head->n_lock * 1.0f / ht_head->n_bucket);
	p_ht_lock = (pthread_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));
	p_off_list = (long int *)((char *)p_ht_lock + sizeof(pthread_mutex_t) * ht_head->n_lock);
	shm_mutex_lock(&(p_ht_lock[idx_lock]));
	if (p_off_list[idx] < 0) {
		shm_mutex_unlock(&(p_ht_lock[idx_lock]));
		return NULL;
	}
	off_next = p_off_list[idx];
	while (off_next) {
		rec = (struct shm_ht_rec *)((char *)d_shm_head + off_next);
		if (len_key == rec->len_key) {
			if (memcmp(key, (char *)rec + sizeof(struct shm_ht_rec), len_key) == 0) {
				value = (char *)rec + sizeof(struct shm_ht_rec) + len_key + rec->len_padding;
				D_ASSERT(((uint64_t)value & (SHM_MEM_ALIGN - 1)) == 0);
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
	pthread_mutex_t   *p_ht_lock;
	long int           off_next;
	long int          *p_off_list;
	struct shm_ht_rec *rec = NULL;
	struct shm_ht_rec *new_rec;
	char              *value = NULL;

	if (link)
		*link = NULL;
	hash = d_hash_string_u32(key, len_key);
	idx = hash & (ht_head->n_bucket - 1);
	idx_lock = (unsigned int)(idx * ht_head->n_lock * 1.0f / ht_head->n_bucket);
	p_ht_lock = (pthread_mutex_t *)((char *)ht_head + sizeof(struct d_shm_ht_head));
	p_off_list = (long int *)((char *)p_ht_lock + sizeof(pthread_mutex_t) * ht_head->n_lock);
	shm_mutex_lock(&(p_ht_lock[idx_lock]));
	if (p_off_list[idx] != INVALID_OFFSET) {
		off_next = p_off_list[idx];
		while (off_next != INVALID_OFFSET) {
			rec = (struct shm_ht_rec *)((char *)d_shm_head + off_next);
			if (len_key == rec->len_key) {
				if (memcmp(key, (char *)rec + sizeof(struct shm_ht_rec), len_key) == 0) {
					/* found the key, then return value */
					value = (char *)rec + sizeof(struct shm_ht_rec) + len_key +
					       rec->len_padding;
					shm_mutex_unlock(&(p_ht_lock[idx_lock]));
					D_ASSERT(((uint64_t)value & (SHM_MEM_ALIGN - 1)) == 0);
					if (link)
						*link = rec;
					return value;
				}
			}
			off_next = rec->next;
		}
	}
	/* record is not found. Insert it at the very beginning of the link list. */
	new_rec = (struct shm_ht_rec *)shm_memalign(SHM_MEM_ALIGN, sizeof(struct shm_ht_rec) +
						    len_key + len_value);
	if (new_rec == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	new_rec->len_key = len_key;
	new_rec->len_padding = (len_key & (SHM_MEM_ALIGN - 1)) ?
			       (SHM_MEM_ALIGN - (len_key & (SHM_MEM_ALIGN - 1))) : 0;
	new_rec->len_value = len_value;
	new_rec->next = INVALID_OFFSET;
	memcpy((char *)new_rec + sizeof(struct shm_ht_rec), key, len_key);
	value = (char *)new_rec + sizeof(struct shm_ht_rec) + len_key + new_rec->len_padding;
	D_ASSERT(((uint64_t)value & (SHM_MEM_ALIGN - 1)) == 0);

	if ((strcmp(val, KEY_VALUE_PTHREAD_LOCK) == 0) && (len_value == sizeof(pthread_mutex_t))) {
		/* value holds a pthread mutex lock */
		if (pthread_mutex_init((pthread_mutex_t *)value, &d_shm_mutex_attr) != 0) {
			perror("pthread_mutex_init");
			return NULL;
		}
	} else if ((strcmp(val, KEY_VALUE_PTHREAD_RWLOCK) == 0) && (len_value == sizeof(pthread_rwlock_t))) {
		/* value holds a pthread read-write mutex lock */
		if (pthread_rwlock_init((pthread_rwlock_t *)value, &d_shm_rwlock_attr) != 0) {
			perror("pthread_rwlock_init");
			return NULL;
		}
	} else {
		/* set value */
		memcpy(value, val, len_value);
	}

	if (rec == NULL) {
		/* bucket is empty */
		p_off_list[idx] = (long int)((char *)new_rec - (char *)d_shm_head);
	} else {
		/* rec-> pre = INVALID_OFFSET for the first record */
		new_rec->prev = INVALID_OFFSET;
		new_rec->next = p_off_list[idx];
		p_off_list[idx] = (long int)((char *)new_rec - (char *)d_shm_head);
	}
	new_rec->idx_lock = idx_lock;

	shm_mutex_unlock(&(p_ht_lock[idx_lock]));
	if (link)
		*link = rec;
	return value;
}
