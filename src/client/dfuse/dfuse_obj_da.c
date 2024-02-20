/**
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <pthread.h>
#include "dfuse_log.h"
#include <gurt/common.h> /* container_of */
#include <gurt/list.h>
#include "dfuse_obj_da.h"

/* A hack to assert that the sizeof obj_da_t is large enough */
struct tpv_data {
	struct obj_da *da;
	d_list_t free_entries;
	d_list_t allocated_blocks;
	d_list_t link;
};

struct da_entry {
	union {
		d_list_t link; /* Free list link */
		char data[0];    /* Data */
	};
};

struct obj_da {
	int magic;                 /* magic number for sanity */
	pthread_key_t key;         /* key to threadprivate data */
	pthread_mutex_t lock;      /* lock thread events */
	d_list_t free_entries;     /* entries put in da by dead thread */
	d_list_t allocated_blocks; /* blocks allocated by dead thread */
	d_list_t tpv_list;         /* Threadprivate data */
	size_t obj_size;           /* size of objects in da */
	size_t padded_size;        /* real size of objects in da */
	size_t block_size;         /* allocation size */
};

#define PAD8(size) ((size + 7) & ~7)

_Static_assert(sizeof(obj_da_t) >= sizeof(struct obj_da),
	       "obj_da_t must be large enough to contain struct obj_da");

#define MAGIC 0x345342aa

/* On thread death, save the free entries globally */
static void
save_free_entries(void *tpv_data)
{
	struct tpv_data *tpv = (struct tpv_data *)tpv_data;
	struct obj_da *da = tpv->da;

	if (d_list_empty(&tpv->free_entries))
		return;

	D_MUTEX_LOCK(&da->lock);
	d_list_splice(&tpv->free_entries, &da->free_entries);
	d_list_splice(&tpv->allocated_blocks, &da->allocated_blocks);
	d_list_del(&tpv->link);
	pthread_setspecific(da->key, NULL);
	D_FREE(tpv);
	D_MUTEX_UNLOCK(&da->lock);
}

#define BLOCK_SIZE 16384

/* Initialize an object da
 * \param da[out] Allocator to initialize
 * \param obj_size[in] Size of objects in da
 */
int
obj_da_initialize(obj_da_t *da, size_t obj_size)
{
	struct obj_da *real_da = (struct obj_da *)da;
	int rc;

	if (da == NULL || obj_size == 0)
		return -DER_INVAL;

	if (obj_size > MAX_POOL_OBJ_SIZE)
		return -DER_OVERFLOW;

	rc = pthread_key_create(&real_da->key, save_free_entries);
	if (rc != 0)
		return -DER_NOMEM;

	rc = D_MUTEX_INIT(&real_da->lock, NULL);
	if (rc != -DER_SUCCESS) {
		pthread_key_delete(real_da->key);
		return rc;
	}

	D_INIT_LIST_HEAD(&real_da->free_entries);
	D_INIT_LIST_HEAD(&real_da->allocated_blocks);
	D_INIT_LIST_HEAD(&real_da->tpv_list);

	real_da->obj_size = obj_size;
	obj_size = sizeof(struct da_entry) > obj_size ?
		sizeof(struct da_entry) : obj_size;
	real_da->padded_size = PAD8(obj_size);
	real_da->block_size = (BLOCK_SIZE / real_da->padded_size) *
				real_da->padded_size;
	real_da->magic = MAGIC;

	return -DER_SUCCESS;
}

/* Destroy a da and all objects in da */
int
obj_da_destroy(obj_da_t *da)
{
	struct da_entry *block;
	struct tpv_data *tpv;
	struct obj_da *real_da = (struct obj_da *)da;
	int		rc;

	if (da == NULL)
		return -DER_INVAL;

	if (real_da->magic != MAGIC)
		return -DER_UNINIT;

	real_da->magic = 0;

	pthread_key_delete(real_da->key);

	while ((block = d_list_pop_entry(&real_da->allocated_blocks,
					 struct da_entry,
					 link))) {
		D_FREE(block);
	}

	while ((tpv = d_list_pop_entry(&real_da->tpv_list,
				       struct tpv_data,
				       link))) {
		while ((block = d_list_pop_entry(&tpv->allocated_blocks,
						 struct da_entry,
						 link))) {
			D_FREE(block);
		}
		D_FREE(tpv);
	}

	rc = pthread_mutex_destroy(&real_da->lock);
	if (rc != 0)
		D_ERROR("Failed to destroy lock %d %s\n", rc, strerror(rc));

	return -DER_SUCCESS;
}

static int
get_tpv(struct obj_da *da, struct tpv_data **tpv)
{
	struct tpv_data *tpv_data = pthread_getspecific(da->key);

	if (tpv_data == NULL) {
		D_ALLOC_PTR(tpv_data);
		if (tpv_data == NULL)
			return -DER_NOMEM;

		D_INIT_LIST_HEAD(&tpv_data->free_entries);
		D_INIT_LIST_HEAD(&tpv_data->allocated_blocks);
		tpv_data->da = da;

		D_MUTEX_LOCK(&da->lock);
		d_list_add(&tpv_data->link, &da->tpv_list);
		/* Steal entries left by a dead thread */
		if (!d_list_empty(&da->free_entries))
			d_list_splice_init(&da->free_entries,
					   &tpv_data->free_entries);
		D_MUTEX_UNLOCK(&da->lock);

		pthread_setspecific(da->key, tpv_data);
	}

	*tpv = tpv_data;

	return -DER_SUCCESS;
}

static int
get_new_entry(struct da_entry **entry, struct obj_da *da)
{
	char *block;
	char *cursor;
	struct tpv_data *tpv_data;
	int rc;

	rc = get_tpv(da, &tpv_data);

	if (rc != -DER_SUCCESS) {
		*entry = NULL;
		return rc;
	}

	*entry = d_list_pop_entry(&tpv_data->free_entries,
				  struct da_entry,
				  link);
	if (*entry)
		goto zero;

	/* Ok, no entries, let's allocate some and put them in our tpv */
	D_ALLOC(block, da->block_size);
	if (block == NULL) {
		*entry = NULL;
		return -DER_NOMEM;
	}

	/* First entry is reserved for the allocation list */
	/* Give second entry to user */
	*entry = (struct da_entry *)(block + da->padded_size);
	/* Put the rest in the tpv free list */
	for (cursor = block + (da->padded_size * 2);
	     cursor != (block + da->block_size);
	     cursor += da->padded_size) {
		d_list_add((d_list_t *)cursor, &tpv_data->free_entries);
	}

	d_list_add((d_list_t *)block, &tpv_data->allocated_blocks);
zero:
	memset(*entry, 0, da->padded_size);

	return -DER_SUCCESS;
}

int
obj_da_get_(obj_da_t *da, void **item, size_t size)
{
	struct obj_da *real_da = (struct obj_da *)da;
	struct da_entry *entry;
	int rc;

	if (da == NULL || item == NULL)
		return -DER_INVAL;

	*item = NULL;
	if (real_da->magic != MAGIC)
		return -DER_UNINIT;

	if (real_da->obj_size != size)
		return -DER_INVAL;

	rc = get_new_entry(&entry, real_da);
	if (rc == -DER_SUCCESS)
		*item = &entry->data[0];

	return rc;
}

int
obj_da_put(obj_da_t *da, void *item)
{
	struct obj_da *real_da = (struct obj_da *)da;
	struct da_entry *entry;
	struct tpv_data *tpv_data;
	int rc;

	if (da == NULL || item == NULL)
		return -DER_INVAL;

	if (real_da->magic != MAGIC)
		return -DER_UNINIT;

	entry = container_of(item, struct da_entry, data);

	rc = get_tpv(real_da, &tpv_data);

	if (rc != -DER_SUCCESS) {
		D_MUTEX_LOCK(&real_da->lock);
		d_list_add(&entry->link, &real_da->free_entries);
		D_MUTEX_UNLOCK(&real_da->lock);
		return rc;
	}

	d_list_add(&entry->link, &tpv_data->free_entries);

	return -DER_SUCCESS;
}
