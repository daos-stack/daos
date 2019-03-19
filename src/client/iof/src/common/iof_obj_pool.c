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
 * A simple, efficient pool for allocating objects of equal size
 */
#include <pthread.h>
#include <gurt/common.h> /* container_of */
#include <gurt/list.h>
#include "iof_obj_pool.h"

/* A hack to assert that the sizeof obj_pool_t is large enough */
struct tpv_data {
	struct obj_pool *pool;
	d_list_t free_entries;
	d_list_t allocated_blocks;
	d_list_t link;
};

struct pool_entry {
	union {
		d_list_t link; /* Free list link */
		char data[0];    /* Data */
	};
};

struct obj_pool {
	pthread_key_t key;         /* key to threadprivate data */
	pthread_mutex_t lock;      /* lock thread events */
	d_list_t free_entries;     /* entries put in pool by dead thread */
	d_list_t allocated_blocks; /* blocks allocated by dead thread */
	d_list_t tpv_list;         /* Threadprivate data */
	size_t obj_size;           /* size of objects in pool */
	size_t padded_size;        /* real size of objects in pool */
	size_t block_size;         /* allocation size */
	int magic;                 /* magic number for sanity */
};

#define PAD8(size) ((size + 7) & ~7)

_Static_assert(sizeof(obj_pool_t) >= sizeof(struct obj_pool),
	       "obj_pool_t must be large enough to contain struct obj_pool");

#define MAGIC 0x345342aa

/* On thread death, save the free entries globally */
static void save_free_entries(void *tpv_data)
{
	struct tpv_data *tpv = (struct tpv_data *)tpv_data;
	struct obj_pool *pool = tpv->pool;

	if (d_list_empty(&tpv->free_entries))
		return;

	D_MUTEX_LOCK(&pool->lock);
	d_list_splice(&tpv->free_entries, &pool->free_entries);
	d_list_splice(&tpv->allocated_blocks, &pool->allocated_blocks);
	d_list_del(&tpv->link);
	pthread_setspecific(pool->key, NULL);
	D_FREE(tpv);
	D_MUTEX_UNLOCK(&pool->lock);
}

#define BLOCK_SIZE 16384

/* Initialize an object pool
 * \param pool[out] Pool to initialize
 * \param obj_size[in] Size of objects in pool
 */
int obj_pool_initialize(obj_pool_t *pool, size_t obj_size)
{
	struct obj_pool *real_pool = (struct obj_pool *)pool;
	int rc;

	if (pool == NULL || obj_size == 0)
		return -DER_INVAL;

	if (obj_size > MAX_POOL_OBJ_SIZE)
		return -DER_OVERFLOW;

	rc = pthread_key_create(&real_pool->key, save_free_entries);
	if (rc != 0)
		return -DER_NOMEM;

	rc = D_MUTEX_INIT(&real_pool->lock, NULL);
	if (rc != -DER_SUCCESS) {
		pthread_key_delete(real_pool->key);
		return rc;
	}

	D_INIT_LIST_HEAD(&real_pool->free_entries);
	D_INIT_LIST_HEAD(&real_pool->allocated_blocks);
	D_INIT_LIST_HEAD(&real_pool->tpv_list);

	real_pool->obj_size = obj_size;
	obj_size = sizeof(struct pool_entry) > obj_size ?
		sizeof(struct pool_entry) : obj_size;
	real_pool->padded_size = PAD8(obj_size);
	real_pool->block_size = (BLOCK_SIZE / real_pool->padded_size) *
				real_pool->padded_size;
	real_pool->magic = MAGIC;

	return -DER_SUCCESS;
}

/* Destroy a pool and all objects in pool */
int obj_pool_destroy(obj_pool_t *pool)
{
	struct pool_entry *block;
	struct tpv_data *tpv;
	struct obj_pool *real_pool = (struct obj_pool *)pool;

	if (pool == NULL)
		return -DER_INVAL;

	if (real_pool->magic != MAGIC)
		return -DER_UNINIT;

	real_pool->magic = 0;

	pthread_key_delete(real_pool->key);

	while ((block = d_list_pop_entry(&real_pool->allocated_blocks,
					 struct pool_entry,
					 link))) {
		D_FREE(block);
	}

	while ((tpv = d_list_pop_entry(&real_pool->tpv_list,
				       struct tpv_data,
				       link))) {
		while ((block = d_list_pop_entry(&tpv->allocated_blocks,
						 struct pool_entry,
						 link))) {
			D_FREE(block);
		}
		D_FREE(tpv);
	}

	pthread_mutex_destroy(&real_pool->lock);

	return -DER_SUCCESS;
}

static int get_tpv(struct obj_pool *pool, struct tpv_data **tpv)
{
	struct tpv_data *tpv_data = pthread_getspecific(pool->key);

	if (tpv_data == NULL) {
		D_ALLOC_PTR(tpv_data);
		if (tpv_data == NULL)
			return -DER_NOMEM;

		D_INIT_LIST_HEAD(&tpv_data->free_entries);
		D_INIT_LIST_HEAD(&tpv_data->allocated_blocks);
		tpv_data->pool = pool;

		D_MUTEX_LOCK(&pool->lock);
		d_list_add(&tpv_data->link, &pool->tpv_list);
		/* Steal entries left by a dead thread */
		if (!d_list_empty(&pool->free_entries))
			d_list_splice_init(&pool->free_entries,
					   &tpv_data->free_entries);
		D_MUTEX_UNLOCK(&pool->lock);

		pthread_setspecific(pool->key, tpv_data);
	}

	*tpv = tpv_data;

	return -DER_SUCCESS;
}

static int get_new_entry(struct pool_entry **entry, struct obj_pool *pool)
{
	char *block;
	char *cursor;
	struct tpv_data *tpv_data;
	int rc;

	rc = get_tpv(pool, &tpv_data);

	if (rc != -DER_SUCCESS) {
		*entry = NULL;
		return rc;
	}

	*entry = d_list_pop_entry(&tpv_data->free_entries,
				  struct pool_entry,
				  link);
	if (*entry) {
		goto zero;
	}

	/* Ok, no entries, let's allocate some and put them in our tpv */
	D_ALLOC(block, pool->block_size);
	if (block == NULL) {
		*entry = NULL;
		return -DER_NOMEM;
	}

	/* First entry is reserved for the allocation list */
	/* Give second entry to user */
	*entry = (struct pool_entry *)(block + pool->padded_size);
	/* Put the rest in the tpv free list */
	for (cursor = block + (pool->padded_size * 2);
	     cursor != (block + pool->block_size);
	     cursor += pool->padded_size) {
		d_list_add((d_list_t *)cursor, &tpv_data->free_entries);
	}

	d_list_add((d_list_t *)block, &tpv_data->allocated_blocks);
zero:
	memset(*entry, 0, pool->padded_size);

	return -DER_SUCCESS;
}

int obj_pool_get_(obj_pool_t *pool, void **item, size_t size)
{
	struct obj_pool *real_pool = (struct obj_pool *)pool;
	struct pool_entry *entry;
	int rc;

	if (pool == NULL || item == NULL)
		return -DER_INVAL;

	*item = NULL;
	if (real_pool->magic != MAGIC)
		return -DER_UNINIT;

	if (real_pool->obj_size != size)
		return -DER_INVAL;

	rc = get_new_entry(&entry, real_pool);
	if (rc == -DER_SUCCESS)
		*item = &entry->data[0];

	return rc;
}

int obj_pool_put(obj_pool_t *pool, void *item)
{
	struct obj_pool *real_pool = (struct obj_pool *)pool;
	struct pool_entry *entry;
	struct tpv_data *tpv_data;
	int rc;

	if (pool == NULL || item == NULL)
		return -DER_INVAL;

	if (real_pool->magic != MAGIC)
		return -DER_UNINIT;

	entry = container_of(item, struct pool_entry, data);

	rc = get_tpv(real_pool, &tpv_data);

	if (rc != -DER_SUCCESS) {
		D_MUTEX_LOCK(&real_pool->lock);
		d_list_add(&entry->link, &real_pool->free_entries);
		D_MUTEX_UNLOCK(&real_pool->lock);
		return rc;
	}

	d_list_add(&entry->link, &tpv_data->free_entries);

	return -DER_SUCCESS;
}
