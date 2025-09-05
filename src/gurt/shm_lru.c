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
lru_create_node(shm_lru_cache_t *cache, shm_lru_cache_var_t *subcache, void *key, uint32_t key_size,
		void *data, uint32_t data_size, shm_lru_node_t **new_node)
{
	shm_lru_node_t *node_list;
	shm_lru_node_t *node;
	char           *buf_key  = NULL;
	char           *buf_data = NULL;
	long int        idx_node;

	*new_node = NULL;

	/* make sure LRU is not full */
	if (subcache->first_av < 0)
		return EBUSY;

	node_list          = (shm_lru_node_t *)((long int)cache + subcache->off_nodelist);
	idx_node           = subcache->first_av;
	node               = &(node_list[idx_node]);
	subcache->first_av = node->off_hnext;

	if ((key_size > LRU_ALLOC_SIZE_THRESHOLD) || (cache->key_size == 0)) {
		/* key is too long or has unknown size, dynamically allocate
		 * space for key
		 */
		buf_key = shm_alloc(key_size);
		if (buf_key == NULL)
			return ENOMEM;
		node->key = (long int)buf_key - (long int)cache;
		memcpy(buf_key, key, key_size);
	} else {
		/* use preallocated buffer for key */
		node->key = subcache->off_keylist + idx_node * (long int)key_size;
		memcpy((char *)cache + node->key, key, key_size);
	}
	node->key_size = key_size;

	if ((data_size > LRU_ALLOC_SIZE_THRESHOLD) || (cache->data_size == 0)) {
		/* data are too long or has unknown size, dynamically allocate
		 * space for data
		 */
		buf_data = shm_alloc(data_size);
		if (buf_data == NULL) {
			if (buf_key)
				shm_free(buf_key);
			return ENOMEM;
		}
		node->data = (long int)buf_data - (long int)cache;
		memcpy(buf_data, data, data_size);
	} else {
		/* use preallocated buffer for data */
		node->data = subcache->off_datalist + idx_node * (long int)cache->data_size;
		memcpy((char *)cache + node->data, data, data_size);
	}
	node->data_size = data_size;

	atomic_store(&node->ref_count, 0);
	node->off_prev  = 0;
	node->off_next  = 0;
	node->off_hnext = 0;
	*new_node       = node;

	return 0;
}

/* Move node to head (most recently used) */
static void
lru_move_to_head(shm_lru_cache_t *cache, shm_lru_cache_var_t *subcache, shm_lru_node_t *node)
{
	shm_lru_node_t *node_head;
	shm_lru_node_t *node_prev;
	shm_lru_node_t *node_next;

	if ((long int)node == ((long int)cache + (long int)subcache->off_head))
		/* this node is most recently used already */
		return;

	/* detach node */
	if (node->off_prev > 0) {
		node_prev = (shm_lru_node_t *)((long int)cache + (long int)node->off_prev);
		node_prev->off_next = node->off_next;
	}

	if (node->off_next > 0) {
		node_next = (shm_lru_node_t *)((long int)cache + (long int)node->off_next);
		node_next->off_prev = node->off_prev;
	}

	if ((long int)node == ((long int)cache + (long int)subcache->off_tail))
		subcache->off_tail = node->off_prev;

	/* move to front */
	node->off_prev = 0;
	node->off_next = subcache->off_head;
	if (subcache->off_head > 0) {
		node_head = (shm_lru_node_t *)((long int)cache + (long int)subcache->off_head);
		node_head->off_prev = (int)((long int)node - (long int)cache);
	}
	subcache->off_head = (int)((long int)node - (long int)cache);

	if (!subcache->off_tail)
		subcache->off_tail = (int)((long int)node - (long int)cache);
}

/* Remove the least recently used node from tail with zero reference count */
static int
lru_remove_near_tail(shm_lru_cache_t *cache, shm_lru_cache_var_t *subcache)
{
	shm_lru_node_t *node;
	shm_lru_node_t *node_tail;
	shm_lru_node_t *node_prev;
	shm_lru_node_t *node_next;
	shm_lru_node_t *node_hnext;
	int            *off_hash = (int *)((long int)cache + (long int)subcache->off_hashbuckets);
	int             offset;
	int             off_node_to_remove;
	int             first_av_saved;

	if (!subcache->off_tail)
		return 0;

	node_tail = (shm_lru_node_t *)((long int)cache + (long int)subcache->off_tail);
	node      = node_tail;

	/* only the node with zero reference count can be removed!!! */
	while (atomic_load(&node->ref_count) > 0) {
		/* need to find the first node with zero reference count starting from the tail to
		 * head
		 */
		if (node->off_prev == 0)
			return EBUSY;
		else
			node = (shm_lru_node_t *)((long int)cache + (long int)node->off_prev);
	}

	/* node will be removed. Free dynamically allocated key/data space */
	if (node->key_size > LRU_ALLOC_SIZE_THRESHOLD || cache->key_size == 0)
		shm_free((char *)cache + node->key);
	if (node->data_size > LRU_ALLOC_SIZE_THRESHOLD || cache->data_size == 0)
		shm_free((char *)cache + node->data);

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
				node->off_hnext       = 0;
				break;
			}
			offset = node_hnext->off_hnext;
		}
	}

	first_av_saved = subcache->first_av;
	subcache->first_av =
	    (off_node_to_remove - (long int)subcache->off_nodelist) / sizeof(shm_lru_node_t);

	/* remove from list */
	if (node->off_prev > 0) {
		node_prev = (shm_lru_node_t *)((long int)cache + (long int)node->off_prev);
		node_prev->off_next = node->off_next;
	}
	if (node->off_next > 0) {
		node_next = (shm_lru_node_t *)((long int)cache + (long int)node->off_next);
		node_next->off_prev = node->off_prev;
	}

	if (node == node_tail)
		subcache->off_tail = node->off_prev;

	if (off_node_to_remove == subcache->off_head)
		subcache->off_head = node->off_next;

	node->off_hnext = first_av_saved;
	subcache->size--;
	if (subcache->size == 0)
		/* cache is empty */
		subcache->off_head = 0;
	return 0;
}

static inline int
key_cmp(shm_lru_cache_t *cache, shm_lru_node_t *node, void *key, uint32_t key_size)
{
	if (node->key_size != key_size)
		/* key size does not match */
		return 1;

	return memcmp((char *)cache + node->key, key, key_size);
}

int
shm_lru_get(shm_lru_cache_t *cache, void *key, uint32_t key_size, shm_lru_node_t **node_found,
	    void **val)
{
	uint64_t             hash         = d_hash_murmur64(key, key_size, 0);
	uint32_t             idx_subcache = (uint32_t)((hash >> 32) % cache->n_subcache);
	shm_lru_cache_var_t *cache_list =
	    (shm_lru_cache_var_t *)((long int)cache + cache->off_cache_list);
	int *off_hash =
	    (int *)((long int)cache + (long int)cache_list[idx_subcache].off_hashbuckets);
	uint32_t        index  = (uint32_t)(hash % cache->capacity);
	int             offset = off_hash[index];
	shm_lru_node_t *node;

	shm_mutex_lock(&cache_list[idx_subcache].lock);

	*node_found = NULL;
	if (cache->key_size != 0)
		D_ASSERT(cache->key_size == key_size);

	while (offset) {
		node = (shm_lru_node_t *)((long int)cache + (long int)offset);
		if (key_cmp(cache, node, key, key_size) == 0) {
			lru_move_to_head(cache, &cache_list[idx_subcache], node);
			*val = ((void *)cache + (long int)node->data);
			atomic_fetch_add(&node->ref_count, 1);
			*node_found = node;
			shm_mutex_unlock(&cache_list[idx_subcache].lock);
			return 0;
		}
		if (node->off_hnext == 0) {
			shm_mutex_unlock(&cache_list[idx_subcache].lock);
			return ENOENT;
		}
		offset = node->off_hnext;
	}
	shm_mutex_unlock(&cache_list[idx_subcache].lock);

	return ENOENT;
}

/* Put key-data into cache */
int
shm_lru_put(shm_lru_cache_t *cache, void *key, uint32_t key_size, void *data, uint32_t data_size)
{
	int                  rc;
	uint64_t             hash         = d_hash_murmur64(key, key_size, 12345);
	uint32_t             idx_subcache = (uint32_t)((hash >> 32) % cache->n_subcache);
	uint32_t             index        = (uint32_t)(hash % cache->capacity);
	shm_lru_cache_var_t *cache_list =
	    (shm_lru_cache_var_t *)((long int)cache + cache->off_cache_list);
	int *off_hash =
	    (int *)((long int)cache + (long int)cache_list[idx_subcache].off_hashbuckets);
	int             offset   = off_hash[index];
	char           *buf_data = NULL;
	shm_lru_node_t *node;
	shm_lru_node_t *node_head;
	shm_lru_node_t *node_new;

	shm_mutex_lock(&cache_list[idx_subcache].lock);
	while (offset) {
		node = (shm_lru_node_t *)((long int)cache + offset);
		if (key_cmp(cache, node, key, key_size) == 0) {
			/* key exists in cache */

			if (node->data_size == data_size) {
				/* data size does not change */
				memcpy((char *)cache + node->data, data, data_size);
			} else {
				/* new data size is different from the old one,
				 * free the data buffer previously allocated
				 */
				if (node->data_size > LRU_ALLOC_SIZE_THRESHOLD ||
				    cache->data_size == 0)
					shm_free((char *)cache + node->data);

				if ((data_size > LRU_ALLOC_SIZE_THRESHOLD) ||
				    (cache->data_size == 0)) {
					/* data are too long or has unknown size,
					 * dynamically allocate space for data
					 */
					buf_data   = shm_alloc(data_size);
					node->data = (long int)buf_data - (long int)cache;
					memcpy(buf_data, data, data_size);
				} else {
					/* use preallocated buffer for data */
					memcpy((char *)cache + node->data, data, data_size);
				}
			}

			lru_move_to_head(cache, &cache_list[idx_subcache], node);
			shm_mutex_unlock(&cache_list[idx_subcache].lock);
			return 0;
		}
		offset = (long int)node->off_hnext;
	}

	/* Not found, remove one node near tail and create new node */
	if (cache_list[idx_subcache].size >= cache->capacity) {
		rc = lru_remove_near_tail(cache, &cache_list[idx_subcache]);
		if (rc) {
			shm_mutex_unlock(&cache_list[idx_subcache].lock);
			return rc;
		}
	}

	rc = lru_create_node(cache, &cache_list[idx_subcache], key, key_size, data, data_size,
			     &node_new);
	if (rc)
		return rc;
	node_new->idx_bucket = index;
	node_new->off_hnext  = off_hash[index];
	off_hash[index]      = (long int)node_new - (long int)cache;

	/* Insert at LRU head */
	node_new->off_next = cache_list[idx_subcache].off_head;
	if (cache_list[idx_subcache].off_head) {
		node_head           = (shm_lru_node_t *)((long int)cache +
                                               (long int)cache_list[idx_subcache].off_head);
		node_head->off_prev = (long int)node_new - (long int)cache;
	}
	cache_list[idx_subcache].off_head = (long int)node_new - (long int)cache;

	if (!cache_list[idx_subcache].off_tail)
		cache_list[idx_subcache].off_tail = (long int)node_new - (long int)cache;

	cache_list[idx_subcache].size++;
	if (cache_list[idx_subcache].size == cache->capacity)
		cache_list[idx_subcache].first_av = -1;

	shm_mutex_unlock(&cache_list[idx_subcache].lock);
	return 0;
}

void
shm_lru_node_dec_ref(shm_lru_node_t *node)
{
	atomic_fetch_add(&node->ref_count, -1);
}

int
shm_lru_create_cache(uint32_t n_subcache, uint32_t capacity, uint32_t key_size, uint32_t data_size,
		     shm_lru_cache_t **lru_cache)
{
	int                  rc;
	uint32_t             i;
	uint32_t             j;
	uint32_t             capacity_per_subcache;
	size_t               size_key_buf;
	size_t               size_data_buf;
	size_t               size_per_subcache;
	shm_lru_node_t      *node_list;
	shm_lru_cache_var_t *cache_list;
	long int             off;

	if (capacity % n_subcache || n_subcache < 1) {
		DS_ERROR(EINVAL, "capacity needs to be a multiplier of subcache number");
		return EINVAL;
	}
	capacity_per_subcache = capacity / n_subcache;
	/* the space pre-allocated for the array of key of all records if keys have a fixed size */
	size_key_buf =
	    (key_size > 0 && data_size <= LRU_ALLOC_SIZE_THRESHOLD) ? (key_size * capacity) : 0;

	/* the space pre-allocated for the array of data of all records if data have a fixed size */
	size_data_buf =
	    (data_size > 0 && data_size <= LRU_ALLOC_SIZE_THRESHOLD) ? (data_size * capacity) : 0;

	shm_lru_cache_t *cache = (shm_lru_cache_t *)shm_alloc(
	    sizeof(shm_lru_cache_t) + sizeof(shm_lru_cache_var_t) * n_subcache +
	    sizeof(int) * capacity + sizeof(shm_lru_node_t) * capacity + size_key_buf +
	    size_data_buf);
	if (cache == NULL)
		return ENOMEM;
	size_per_subcache = (sizeof(int) * capacity + sizeof(shm_lru_node_t) * capacity +
			     size_key_buf + size_data_buf) /
			    n_subcache;

	memset(cache, 0,
	       sizeof(shm_lru_cache_t) + sizeof(shm_lru_cache_var_t) * n_subcache +
		   sizeof(int) * capacity + sizeof(shm_lru_node_t) * capacity + size_key_buf +
		   size_data_buf);
	cache->n_subcache     = n_subcache;
	cache->capacity       = capacity_per_subcache;
	cache->key_size       = key_size;
	cache->data_size      = data_size;
	off                   = sizeof(shm_lru_cache_t);
	cache->off_cache_list = off;
	cache_list            = (shm_lru_cache_var_t *)((long int)cache + cache->off_cache_list);
	memset(cache_list, 0, sizeof(shm_lru_cache_var_t) * n_subcache);

	for (i = 0; i < n_subcache; i++) {
		cache_list[i].size     = 0;
		cache_list[i].first_av = 0;
		cache_list[i].off_hashbuckets =
		    off + (sizeof(shm_lru_cache_var_t) * n_subcache) + (i * size_per_subcache);
		cache_list[i].off_nodelist =
		    cache_list[i].off_hashbuckets + sizeof(int) * capacity_per_subcache;
		cache_list[i].off_keylist =
		    cache_list[i].off_nodelist + sizeof(shm_lru_node_t) * capacity_per_subcache;
		cache_list[i].off_datalist =
		    cache_list[i].off_keylist + (size_key_buf / n_subcache);

		/* form a linked list for all free nodes. first_av points to the head node. */
		node_list = (shm_lru_node_t *)((long int)cache + cache_list[i].off_nodelist);
		for (j = 0; j < (capacity_per_subcache - 1); j++)
			node_list[j].off_hnext = j + 1;

		/* invalid node index */
		node_list[j].off_hnext = -1;
		if (lru_cache)
			*lru_cache = cache;

		rc = shm_mutex_init(&cache_list[i].lock);
		if (rc)
			return rc;
	}

	return 0;
}

static void
lru_free_dymaic_buff(shm_lru_cache_t *cache)
{
	int                  i;
	int                  offset;
	shm_lru_node_t      *node;
	shm_lru_cache_var_t *cache_list =
	    (shm_lru_cache_var_t *)((long int)cache + cache->off_cache_list);

	if (cache->key_size > 0 && cache->key_size <= LRU_ALLOC_SIZE_THRESHOLD &&
	    cache->data_size > 0 && cache->data_size <= LRU_ALLOC_SIZE_THRESHOLD)
		return;

	for (i = 0; i < cache->n_subcache; i++) {
		offset = cache_list[i].off_head;
		/* need to loop over all LRU nodes to free key & data buffer */
		while (offset) {
			node = (shm_lru_node_t *)((long int)cache + (long int)offset);
			if (node->key_size > LRU_ALLOC_SIZE_THRESHOLD || cache->key_size == 0)
				shm_free((char *)cache + node->key);
			if (node->data_size > LRU_ALLOC_SIZE_THRESHOLD || cache->data_size == 0)
				shm_free((char *)cache + node->data);
			offset = node->off_next;
		}
	}
}

/* Free all nodes and destroy cache */
void
shm_lru_destroy_cache(shm_lru_cache_t *cache)
{
	lru_free_dymaic_buff(cache);
	if (cache)
		shm_free(cache);
}
