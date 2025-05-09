/**
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
#include <gurt/shm_utils.h>

/* the address of shared memory region */
extern struct d_shm_hdr *d_shm_head;

/* Create a new LRU node */
static int
lru_create_node(shm_lru_cache_t *cache, void *key, int key_size, void *data, int data_size,
				shm_lru_node_t ** new_node)
{
	shm_lru_node_t *node_list;
	shm_lru_node_t *node;
	char           *buf_key = NULL;
	char           *buf_data = NULL;
	long int        key_mask;
	long int        data_mask;

	*new_node = NULL;

	/* make sure LRU is not full */
	if (cache->first_av < 0)
		return SHM_LRU_NO_SPACE;

	node_list = (shm_lru_node_t *)((long int)cache + cache->off_nodelist);
	node = &(node_list[cache->first_av]);
	cache->first_av = node->off_hnext;

	if (key_size <= sizeof(long int)) {
		/* key is small enough to be stored in-place */
		key_mask = (key_size < 8) ? ((1L << (key_size << 3)) - 1) : (-1L);
		node->key = *((long int *)key) & key_mask;
	} else if ((key_size > LRU_ALLOC_SIZE_THRESHOLD) || (cache->key_size == 0)) {
		/* key is too long or has unknown size (long than sizeof(long int)), dynamically allocate
		 * space for key
		 */
		buf_key = shm_alloc(key_size);
		if (buf_key == NULL)
			return SHM_LRU_OUT_OF_MEM;
		node->key = (long int)buf_key - (long int)cache;
		memcpy(buf_key, key, key_size);
	} else {
		/* use preallocated buffer for key */
		node->key = cache->off_keylist + cache->first_av * (long int)key_size;
		memcpy((char *)cache + node->key, key, key_size);
	}
	node->key_size = key_size;

	if (data_size <= sizeof(long int)) {
		/* data are small enough to be stored in-place */
		data_mask = (data_size < 8) ? ((1L << (data_size << 3)) - 1) : (-1L);
		node->data = *((long int *)data) & data_mask;
	} else if ((data_size > LRU_ALLOC_SIZE_THRESHOLD) || (cache->data_size == 0)) {
		/* data are too long or has unknown size (long than sizeof(long int)), dynamically allocate
		 * space for data
		 */
		buf_data = shm_alloc(data_size);
		if (buf_data == NULL) {
			if (buf_key)
				shm_free(buf_key);
			return SHM_LRU_OUT_OF_MEM;
		}
		node->data = (long int)buf_data - (long int)cache;
		memcpy(buf_data, data, data_size);
	} else {
		/* use preallocated buffer for data */
		node->data = cache->off_datalist + cache->first_av * (long int)data_size;
		memcpy((char *)cache + node->data, data, data_size);
	}
	node->data_size = data_size;

	atomic_store(&node->ref_count, 0);
	node->off_prev = node->off_next = node->off_hnext = 0;
	*new_node      = node;

	return SHM_LRU_SUCCESS;
}

/* Move node to head (most recently used) */
static void
lru_move_to_head(shm_lru_cache_t *cache, shm_lru_node_t *node)
{
	shm_lru_node_t *node_head;
	shm_lru_node_t *node_prev;
	shm_lru_node_t *node_next;

	if ((long int)node == ((long int)cache + (long int)cache->off_head))
		/* this node is most recently used already */
		return;

	/* detach node */
	if (node->off_prev) {
		node_prev = (shm_lru_node_t *)((long int)cache + (long int)node->off_prev);
		node_prev->off_next = node->off_next;
	}

	if (node->off_next) {
		node_next = (shm_lru_node_t *)((long int)cache + (long int)node->off_next);
		node_next->off_prev = node->off_prev;
	}

	if ((long int)node == ((long int)cache + (long int)cache->off_tail))
		cache->off_tail = node->off_prev;

	/* move to front */
	node->off_prev = 0;
	node->off_next = cache->off_head;
	if (cache->off_head) {
		node_head = (shm_lru_node_t *)((long int)cache + (long int)cache->off_head);
		node_head->off_prev = (int)((long int)node - (long int)cache);
	}
	cache->off_head = (int)((long int)node - (long int)cache);

	if (!cache->off_tail)
		cache->off_tail = (int)((long int)node - (long int)cache);
}

/* Remove the least recently used node from tail with zero reference count */
static int
lru_remove_near_tail(shm_lru_cache_t *cache)
{
	shm_lru_node_t *node;
	shm_lru_node_t *node_tail;
	shm_lru_node_t *node_prev;
	shm_lru_node_t *node_next;
	shm_lru_node_t *node_hnext;
	int            *off_hash = (int *)((long int)cache + (long int)cache->off_hashbuckets);
	int             offset;
	int             off_node_to_remove;
	int             first_av_saved;

	if (!cache->off_tail)
		return SHM_LRU_SUCCESS;

	node = (shm_lru_node_t *)((long int)cache + (long int)cache->off_tail);
	node_tail = node;

    /* only the node with zero reference count can be removed!!! */
	while (atomic_load(&node->ref_count) > 0) {
		/* need to find the first node with zero reference count starting from the tail to head */
		if (node->off_prev == 0)
			return SHM_LRU_NO_SPACE;
		else
			node = (shm_lru_node_t *)((long int)cache + (long int)node->off_prev);
	}
	D_ASSERT(atomic_load(&node->ref_count) == 0);

	/* node is the node to be removed */

	/* updata the linked list in the hash bucket */
	off_node_to_remove = (long int)node - (long int)cache;
	if (off_hash[node->idx_bucket] == off_node_to_remove) {
		/* the head node of this bucket */
		off_hash[node->idx_bucket] = node->off_hnext;
	} else {
		/* loop the chain of nodes in this bucket */
		offset = off_hash[node->idx_bucket];
		while (offset) {
			node_hnext = (shm_lru_node_t *)((long int)cache + (long int)offset);
			if (node_hnext->off_hnext == off_node_to_remove) {
				node_hnext->off_hnext = node->off_hnext;
				node->off_hnext = 0;
				break;
			}
			offset = node_hnext->off_hnext;
		}
	}

	first_av_saved = cache->first_av;
	cache->first_av = (off_node_to_remove - (long int)cache->off_nodelist)/sizeof(shm_lru_node_t);

	/* remove from list */
	if (node->off_prev) {
		node_prev = (shm_lru_node_t *)((long int)cache + (long int)node->off_prev);
		node_prev->off_next = node->off_next;
	}
	if (node->off_next) {
		node_next = (shm_lru_node_t *)((long int)cache + (long int)node->off_next);
		node_next->off_prev = node->off_prev;
	}

	if (node == node_tail)
		cache->off_tail = node->off_prev;
	if (off_node_to_remove == cache->off_head)
		cache->off_head = node->off_next;

	node->off_hnext = first_av_saved;
	cache->size--;
	if (cache->size == 0)
		/* cache is empty */
		cache->off_head = 0;
	return SHM_LRU_SUCCESS;
}

static inline int
key_cmp(shm_lru_cache_t *cache, shm_lru_node_t *node, void *key, int key_size)
{
	long int key_mask;

	if (node->key_size != key_size)
		/* key size does not match */
		return 1;

	if (key_size <= sizeof(long int)) {
		/* key is small enough to be stored in-place */
		key_mask = (key_size < 8) ? ((1L << (key_size << 3)) - 1) : (-1L);
		return (((long int)node->key & key_mask) == (*((long int *)key) & key_mask)) ? 0 : 1;
	} else {
		/* the offset to key is stored in key */
		return memcmp((char *)cache + node->key, key, key_size);
	}
}

int
shm_lru_get(shm_lru_cache_t *cache, void *key, int key_size, shm_lru_node_t **node_found, void **val)
{
	int            *off_hash = (int *)((long int)cache + (long int)cache->off_hashbuckets);
	int             index = d_hash_string_u32(key, key_size) % cache->capacity;
	shm_lru_node_t *node;
	int             offset = off_hash[index];
	bool            pre_owner_dead;

	shm_mutex_lock(&cache->lock, &pre_owner_dead);

	*node_found = NULL;
	if (cache->key_size != 0)
		D_ASSERT(cache->key_size == key_size);

	while (offset) {
		node = (shm_lru_node_t *)((long int)cache + (long int)offset);
		if (key_cmp(cache, node, key, key_size) == 0) {
			lru_move_to_head(cache, node);
			*val = (node->data_size <= sizeof(long int)) ? &(node->data) : ((void *)cache +
				   (long int)node->data);
			atomic_fetch_add(&node->ref_count, 1);
			*node_found = node;
			shm_mutex_unlock(&cache->lock);
			return 0;
		}
		if (node->off_hnext == 0) {
			shm_mutex_unlock(&cache->lock);
			return SHM_LRU_REC_NOT_FOUND;
		}
		offset = node->off_hnext;
	}
	shm_mutex_unlock(&cache->lock);
	return SHM_LRU_REC_NOT_FOUND;
}

/* Put key-data into cache */
int
shm_lru_put(shm_lru_cache_t *cache, void *key, int key_size, void *data, int data_size)
{
	int             rc;
	int            *off_hash = (int *)((long int)cache + (long int)cache->off_hashbuckets);
	int             index    = d_hash_string_u32(key, key_size) % cache->capacity;
	int             offset   = off_hash[index];
	char           *buf_data = NULL;
	long int        data_mask;
	shm_lru_node_t *node;
	shm_lru_node_t *node_head;
	shm_lru_node_t *node_new;
	bool            pre_owner_dead;

	if (cache->key_size != 0)
		D_ASSERT(cache->key_size == key_size);
	if (cache->data_size != 0)
		D_ASSERT(cache->data_size == data_size);

	shm_mutex_lock(&cache->lock, &pre_owner_dead);
	while (offset) {
		node = (shm_lru_node_t *)((long int)cache + offset);
		if (key_cmp(cache, node, key, key_size) == 0) {
			/* key exists in cache */

			if (node->data_size == data_size) {
				/* data size does not change */
				if (data_size <= sizeof(long int)) {
					data_mask = (data_size < 8) ? ((1L << (data_size << 3)) - 1) : (-1L);
					/* copy data since data size is smaller than long int */
					node->data = data_mask & *((long int *)data);
				} else {
					memcpy((char *)cache + node->data, data, data_size);
				}
			} else {
				/* new data size is different from the old one */
				/* free the data buffer previously allocated */
				if (node->data_size > LRU_ALLOC_SIZE_THRESHOLD || (cache->key_size == 0 &&
					node->data_size > sizeof(long int)))
					shm_free((char *)cache + node->data);

				if (data_size <= sizeof(long int)) {
					/* data are small enough to be stored in-place */
					data_mask = (data_size < 8) ? ((1L << (data_size << 3)) - 1) : (-1L);
					node->data = *((long int *)data) & data_mask;
				} else if ((data_size > LRU_ALLOC_SIZE_THRESHOLD) || (cache->data_size == 0)) {
					/* data are too long or has unknown size (long than sizeof(long int)),
					 * dynamically allocate space for data
					 */
					buf_data = shm_alloc(data_size);
					if (buf_data == NULL) {
						return SHM_LRU_OUT_OF_MEM;
					}
					node->data = (long int)buf_data - (long int)cache;
					memcpy(buf_data, data, data_size);
				} else {
					/* use preallocated buffer for data */
					memcpy((char *)cache + node->data, data, data_size);
				}
			}

			lru_move_to_head(cache, node);
			shm_mutex_unlock(&cache->lock);
			return SHM_LRU_SUCCESS;
		}
		offset = (long int)node->off_hnext;
    }

    /* Not found, remove one node near tail and create new node */
	if (cache->size >= cache->capacity) {
		rc = lru_remove_near_tail(cache);
		if (rc) {
			shm_mutex_unlock(&cache->lock);
			return rc;
		}
	}

	rc = lru_create_node(cache, key, key_size, data, data_size, &node_new);
	if (rc)
		return rc;
	node_new->idx_bucket = index;
	node_new->off_hnext  = off_hash[index];
	off_hash[index]      = (long int)node_new - (long int)cache;

    /* Insert at LRU head */
	node_new->off_next = cache->off_head;
	if (cache->off_head) {
		node_head = (shm_lru_node_t *)((long int)cache + (long int)cache->off_head);
		node_head->off_prev = (long int)node_new - (long int)cache;
	}
	cache->off_head = (long int)node_new - (long int)cache;

	if (!cache->off_tail)
		cache->off_tail = (long int)node_new - (long int)cache;

	cache->size++;
	if (cache->size == cache->capacity)
		cache->first_av = -1;

	shm_mutex_unlock(&cache->lock);
	return SHM_LRU_SUCCESS;
}

void
shm_lru_node_dec_ref(shm_lru_node_t *node)
{
	atomic_fetch_add(&node->ref_count, -1);
}

int
shm_lru_create_cache(int capacity, int key_size, int data_size, shm_lru_cache_t **lru_cache)
{
	int             i;
	int             rc;
	size_t          size_key_buf;
	size_t          size_data_buf;
	shm_lru_node_t *node_list;

	/* the space pre-allocated for the array of key of all records if keys have a fixed size */
	size_key_buf = (key_size > sizeof(long int) && data_size <= LRU_ALLOC_SIZE_THRESHOLD) ?
				   (key_size * capacity) : 0;

	/* the space pre-allocated for the array of data of all records if data have a fixed size */
	size_data_buf = (data_size > sizeof(long int) && data_size <= LRU_ALLOC_SIZE_THRESHOLD) ?
					(data_size * capacity) : 0;

	shm_lru_cache_t *cache = (shm_lru_cache_t *)shm_alloc(sizeof(shm_lru_cache_t) +
							 sizeof(int)*capacity + sizeof(shm_lru_node_t)*capacity) + size_key_buf
							 + size_data_buf;
	if (cache == NULL)
		return ENOMEM;

	memset(cache, 0, sizeof(shm_lru_cache_t) + sizeof(int)*capacity + sizeof(shm_lru_node_t) *
		   capacity);
	cache->capacity = capacity;
	cache->size     = 0;
	cache->first_av = 0;
	cache->key_size = key_size;
	cache->data_size = data_size;
	cache->off_hashbuckets = sizeof(shm_lru_cache_t);
	cache->off_nodelist = cache->off_hashbuckets + sizeof(int) * capacity;
	cache->off_keylist = cache->off_nodelist + sizeof(shm_lru_node_t) * capacity;
	cache->off_datalist = cache->off_keylist + size_key_buf;

	/* form a linked list for all free nodes. first_av points to the head node. */
	node_list = (shm_lru_node_t *)((long int)cache + cache->off_nodelist);
	for (i = 0; i < (capacity - 1); i++)
		node_list[i].off_hnext = i + 1;

	/* invalid node index */
	node_list[i].off_hnext = -1;
	if (lru_cache)
		*lru_cache = cache;

	rc = shm_mutex_init(&cache->lock);
	if (rc)
		return rc;

	return 0;
}

shm_lru_cache_t *
shm_lru_get_cache(enum SHM_LRU_CACHE_TYPE type)
{
	switch (type)
	{
	case CACHE_DENTRY:
		return (shm_lru_cache_t *)((long int)d_shm_head + d_shm_head->off_lru_cache_dentry);

	case CACHE_DATA:
		return (shm_lru_cache_t *)((long int)d_shm_head + d_shm_head->off_lru_cache_data);

	default:
		return NULL;
	};
}

static void
lru_free_dymaic_buff(shm_lru_cache_t *cache)
{
	int             offset = cache->off_head;
	shm_lru_node_t *node;

	if (cache->key_size > 0 && cache->key_size <= LRU_ALLOC_SIZE_THRESHOLD && cache->data_size > 0
		&& cache->data_size <= LRU_ALLOC_SIZE_THRESHOLD)
		return;

	/* need to loop over all LRU nodes to free key & data buffer */
	while (offset) {
		node = (shm_lru_node_t *)((long int)cache + (long int)offset);
		if (node->key_size > sizeof(long int)) {
			shm_free((char *)cache + node->key);;
		}
		if (node->data_size > sizeof(long int)) {
			shm_free((char *)cache + node->data);;
		}
		offset = node->off_next;
	}
}

/* Free all nodes and destroy cache */
void
shm_lru_destroy_cache(shm_lru_cache_t *cache)
{
	bool pre_owner_dead;

	shm_mutex_lock(&cache->lock, &pre_owner_dead);

	lru_free_dymaic_buff(cache);

	if (cache->size > 0)
		shm_free(cache);
	shm_mutex_unlock(&cache->lock);
}

/* Print current state */
void
printCache(shm_lru_cache_t *cache)
{
	int             offset = cache->off_head;
	shm_lru_node_t *node;

	printf("Cache [MRU -> LRU]: ");
	while (offset) {
		node = (shm_lru_node_t *)((long int)cache + (long int)offset);
		printf("(%ld:%ld) ", node->key, node->data);
		offset = node->off_next;
	}
	printf("\n");
}
