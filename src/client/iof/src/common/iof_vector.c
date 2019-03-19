/* Copyright (C) 2017-2018 Intel Corporation
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
 *
 * This implements a simple, thread-safe, random access vector of fixed size
 * entries.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <gurt/common.h> /* container_of */
#include "iof_atomic.h"
#include "iof_obj_pool.h"
#include "iof_vector.h"

#define CAS(valuep, old, new) \
	atomic_compare_exchange(valuep, old, new)

union ptr_lock {
	void *ptr;
	ATOMIC uintptr_t value;
};

/* Acquires a lock on the pointer and returns the pointer. */
static inline void *acquire_ptr_lock(union ptr_lock *lock)
{
	uintptr_t new_value;
	uintptr_t old_value;

	for (;;) {
		new_value = lock->value;

		if (new_value & 1) {
			sched_yield();
			continue;
		}

		old_value = new_value;
		new_value |= 1;
		if (CAS(&lock->value, old_value, new_value))
			break;
	}

	return (void *)old_value;
}

static inline void release_ptr_lock(union ptr_lock *lock)
{
	atomic_store_release(&lock->value, lock->value & ~((uintptr_t)1));
}

/* Caller must hold lock.  A valid aligned pointer value
 * releases the lock
 */
static inline void set_ptr_value(union ptr_lock *lock, void *new_value)
{
	atomic_store_release(&lock->value, (uintptr_t)new_value);
}

struct entry {
	ATOMIC int refcount;       /* vector entries that reference data */
	union {
		uint64_t align[0]; /* Align to 8 bytes */
		char data[0];      /* Actual user data */
	};
};

#define MAGIC 0xd3f211dc

struct vector {
	union ptr_lock *data;        /* entries in vector */
	obj_pool_t pool;             /* Pool of free entries */
	pthread_rwlock_t lock;       /* reader/writer lock for vector */
	int magic;                   /* Magic number for sanity */
	unsigned int entry_size;     /* Size of entries in vector */
	unsigned int num_entries;    /* Current number of allocated entries */
	unsigned int max_entries;    /* limit on size of vector */
};

_Static_assert(sizeof(struct vector) <= sizeof(vector_t),
	       "vector_t must be large enough to contain struct vector");

#define MIN_SIZE 1024
#define ALLOC_SIZE_SHIFT 9 /* 512 */
#define ALLOC_SIZE (1 << ALLOC_SIZE_SHIFT)
#define get_new_size(index) \
	(((index + ALLOC_SIZE) >> ALLOC_SIZE_SHIFT) << ALLOC_SIZE_SHIFT)

/* Assumes new_index is in bounds of vector but not yet allocated */
static int expand_vector(struct vector *vector, unsigned int new_index)
{
	unsigned int num_entries = get_new_size(new_index);
	unsigned int new_entries;
	void *data;

	if (num_entries < MIN_SIZE)
		num_entries = MIN_SIZE;

	if (num_entries > vector->max_entries)
		num_entries = vector->max_entries;

	D_REALLOC(data,
		  vector->data,
		  num_entries * sizeof(union ptr_lock));
	if (!data)
		return -DER_NOMEM;
	vector->data = data;

	/* Now fill in the data from the old size onward */
	data = &vector->data[vector->num_entries];
	new_entries = num_entries - vector->num_entries;
	memset(data, 0, new_entries * sizeof(union ptr_lock));
	vector->num_entries = num_entries;

	return -DER_SUCCESS;
}

/* Assumes caller holds pthread_rwlock.   Same lock will be held on exit */
static int expand_if_needed(struct vector *vector, unsigned int index)
{
	int rc = -DER_SUCCESS;

	if (index >= vector->num_entries) {
		/* Entry "present" but not allocated. */
		D_RWLOCK_UNLOCK(&vector->lock);

		/* expand the vector */
		D_RWLOCK_WRLOCK(&vector->lock);
		if (index >= vector->num_entries)
			rc = expand_vector(vector, index);
		D_RWLOCK_UNLOCK(&vector->lock);

		/* reacquire reader lock and continue */
		D_RWLOCK_RDLOCK(&vector->lock);
	}

	return rc;
}

int vector_init(vector_t *vector, int sizeof_entry, int max_entries)
{
	struct vector *realv = (struct vector *)vector;
	int rc;

	if (vector == NULL || max_entries <= 0 || sizeof_entry <= 0) {
		if (vector != NULL)
			realv->magic = 0;
		return -DER_INVAL;
	}

	realv->magic = 0;
	realv->max_entries = max_entries;
	realv->entry_size = sizeof_entry;
	realv->data = NULL;
	realv->num_entries = 0;
	/* TODO: Improve cleanup of the error paths in this function */
	rc = pthread_rwlock_init(&realv->lock, NULL);
	if (rc != 0)
		return -DER_INVAL;
	rc = obj_pool_initialize(&realv->pool,
				 sizeof(struct entry) + sizeof_entry);
	if (rc != -DER_SUCCESS)
		return -DER_NOMEM;
	rc = expand_vector(realv, 0);

	if (rc != -DER_SUCCESS)
		return rc; /* error logged in expand_vector */

	realv->magic = MAGIC;

	return -DER_SUCCESS;
}

int vector_destroy(vector_t *vector)
{
	struct vector *realv = (struct vector *)vector;
	int rc;

	if (vector == NULL)
		return -DER_INVAL;

	if (realv->magic != MAGIC)
		return -DER_UNINIT;

	realv->magic = 0;

	rc = pthread_rwlock_destroy(&realv->lock);
	obj_pool_destroy(&realv->pool);
	D_FREE(realv->data);

	if (rc == 0)
		return -DER_SUCCESS;
	else
		return -DER_INVAL;
}

int vector_get_(vector_t *vector, unsigned int index, void **ptr)
{
	struct vector *realv = (struct vector *)vector;
	struct entry *entry;
	int rc = -DER_SUCCESS;

	if (ptr == NULL)
		return -DER_INVAL;

	*ptr = NULL;

	if (vector == NULL)
		return -DER_INVAL;

	if (realv->magic != MAGIC)
		return -DER_UNINIT;

	if (index >= realv->max_entries)
		return -DER_INVAL;

	D_RWLOCK_RDLOCK(&realv->lock);
	if (index >= realv->num_entries) {
		/* Entry "present" but not allocated. */
		D_RWLOCK_UNLOCK(&realv->lock);
		return -DER_NONEXIST;
	}

	entry = (struct entry *)acquire_ptr_lock(&realv->data[index]);
	if (entry != NULL) {
		atomic_fetch_add(&entry->refcount, 1);
		*ptr = &entry->data[0];
	} else {
		rc = -DER_NONEXIST;
	}

	release_ptr_lock(&realv->data[index]);

	D_RWLOCK_UNLOCK(&realv->lock);

	return rc;
}

int vector_dup_(vector_t *vector, unsigned int src_idx, unsigned int dst_idx,
		void **ptr)
{
	struct vector *realv = (struct vector *)vector;
	struct entry *entry;
	struct entry *tmp;
	int rc = -DER_SUCCESS;

	if (ptr == NULL)
		return -DER_INVAL;

	*ptr = NULL;

	if (vector == NULL)
		return -DER_INVAL;

	if (realv->magic != MAGIC)
		return -DER_UNINIT;

	if (src_idx >= realv->max_entries || dst_idx >= realv->max_entries)
		return -DER_INVAL;

	D_RWLOCK_RDLOCK(&realv->lock);
	if (src_idx >= realv->num_entries) {
		/* source entry "present" but not allocated. */
		D_RWLOCK_UNLOCK(&realv->lock);
		return -DER_NONEXIST;
	}
	rc = expand_if_needed(realv, dst_idx);
	if (rc != -DER_SUCCESS) {
		D_RWLOCK_UNLOCK(&realv->lock);
		return rc;
	}

	entry = (struct entry *)acquire_ptr_lock(&realv->data[src_idx]);
	if (entry != NULL)
		atomic_fetch_add(&entry->refcount, 2); /* dst_idx + user */
	release_ptr_lock(&realv->data[src_idx]);

	tmp = (struct entry *)acquire_ptr_lock(&realv->data[dst_idx]);
	if (tmp != NULL) {
		/* We will replace the existing entry */
		if (atomic_fetch_sub(&tmp->refcount, 1) == 1)
			obj_pool_put(&realv->pool, tmp);
	}

	if (entry != NULL)
		*ptr = &entry->data[0];

	/* Releases the ptr_lock */
	set_ptr_value(&realv->data[dst_idx], entry);

	D_RWLOCK_UNLOCK(&realv->lock);

	return rc;
}

int vector_decref(vector_t *vector, void *ptr)
{
	struct vector *realv = (struct vector *)vector;
	struct entry *entry;
	int old_value;

	if (vector == NULL || ptr == NULL)
		return -DER_INVAL;

	if (realv->magic != MAGIC)
		return -DER_UNINIT;

	entry = container_of(ptr, struct entry, data);

	old_value = atomic_fetch_sub(&entry->refcount, 1);

	if (old_value == 1)
		obj_pool_put(&realv->pool, entry);

	return -DER_SUCCESS;
}

int vector_set_(vector_t *vector, unsigned int index, void *ptr, size_t size)
{
	struct vector *realv = (struct vector *)vector;
	struct entry *entry;
	int rc = -DER_SUCCESS;

	if (vector == NULL || ptr == NULL)
		return -DER_INVAL;

	if (realv->magic != MAGIC)
		return -DER_UNINIT;

	if (size != realv->entry_size || index >= realv->max_entries)
		return -DER_INVAL;

	D_RWLOCK_RDLOCK(&realv->lock);
	rc = expand_if_needed(realv, index);

	if (rc != -DER_SUCCESS) {
		D_RWLOCK_UNLOCK(&realv->lock);
		return rc;
	}

	entry = (struct entry *)acquire_ptr_lock(&realv->data[index]);
	if (entry != NULL) {
		/* We will replace the existing entry */
		rc = atomic_fetch_sub(&entry->refcount, 1);
		if (rc == 1)
			obj_pool_put(&realv->pool, entry);
	}

	rc = obj_pool_get_(&realv->pool, (void **)&entry,
			   sizeof(*entry) + realv->entry_size);
	if (rc != -DER_SUCCESS) {
		rc = -DER_NOMEM;
		entry = NULL;
		goto release;
	}

	entry->refcount = 1; /* Vector will have a reference */
	memcpy(&entry->data[0], ptr, size);
release:

	/* Releases the ptr_lock */
	set_ptr_value(&realv->data[index], entry);

	D_RWLOCK_UNLOCK(&realv->lock);

	return rc;
}

int vector_remove_(vector_t *vector, unsigned int index, void **ptr)
{
	struct vector *realv = (struct vector *)vector;
	struct entry *entry;
	int rc = -DER_SUCCESS;

	if (ptr != NULL)
		*ptr = NULL;

	if (vector == NULL)
		return -DER_INVAL;

	if (realv->magic != MAGIC)
		return -DER_UNINIT;

	if (index >= realv->max_entries)
		return -DER_INVAL;

	D_RWLOCK_RDLOCK(&realv->lock);
	if (index >= realv->num_entries) {
		D_RWLOCK_UNLOCK(&realv->lock);
		return -DER_NONEXIST;
	}

	entry = (struct entry *)acquire_ptr_lock(&realv->data[index]);
	if (entry != NULL) {
		/* keep the reference if returning the entry */
		if (ptr == NULL) {
			if (atomic_fetch_sub(&entry->refcount, 1) == 1)
				obj_pool_put(&realv->pool, entry);
		} else {
			*ptr = &entry->data[0];
		}
	} else {
		rc = -DER_NONEXIST;
	}

	/* releases ptr lock */
	set_ptr_value(&realv->data[index], NULL);

	D_RWLOCK_UNLOCK(&realv->lock);

	return rc;
}
