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

	if (cache->prealloc_key == 0) {
		/* key is too long or has unknown size, dynamically allocate
		 * space for key
		 */
		buf_key = shm_alloc(key_size);
		if (buf_key == NULL)
			return ENOMEM;
		node->off_key = (long int)buf_key - (long int)cache;
		memcpy(buf_key, key, key_size);
	} else {
		/* use preallocated buffer for key */
		node->off_key = subcache->off_keylist + idx_node * (long int)cache->key_size;
		memcpy((char *)cache + node->off_key, key, key_size);
	}
	node->key_size = key_size;

	if (cache->prealloc_data == 0) {
		/* data are too long or has unknown size, dynamically allocate
		 * space for data
		 */
		buf_data = shm_alloc(data_size);
		if (buf_data == NULL) {
			if (buf_key)
				shm_free(buf_key);
			return ENOMEM;
		}
		node->off_data = (long int)buf_data - (long int)cache;
		memcpy(buf_data, data, data_size);
	} else {
		/* make sure data size not longer than buffer */
		D_ASSERT(data_size <= cache->data_size);
		/* use preallocated buffer for data */
		node->off_data = subcache->off_datalist + idx_node * (long int)cache->data_size;
		memcpy((char *)cache + node->off_data, data, data_size);
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
	int            *off_bucket = (int *)((long int)cache + (long int)subcache->off_hashbuckets);
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
	if (cache->prealloc_key == 0)
		shm_free((char *)cache + node->off_key);
	if (cache->prealloc_data == 0)
		shm_free((char *)cache + node->off_data);

	/* update the linked list in the hash bucket */
	off_node_to_remove = (long int)node - (long int)cache;
	if (off_bucket[node->idx_bucket] == off_node_to_remove) {
		/* the head node of this bucket */
		off_bucket[node->idx_bucket] = node->off_hnext;
	} else {
		/* loop the chain of nodes in this bucket */
		offset = off_bucket[node->idx_bucket];
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

	return memcmp((char *)cache + node->off_key, key, key_size);
}

int
shm_lru_get(shm_lru_cache_t *cache, void *key, uint32_t key_size, shm_lru_node_t **node_found,
	    void **val)
{
	uint64_t             hash;
	uint32_t             idx_subcache;
	shm_lru_cache_var_t *sub_cache;
	int                 *off_bucket;
	uint32_t             index;
	int                  offset;
	shm_lru_node_t      *node;

	if (cache == NULL || key == NULL || node_found == NULL || val == NULL)
		return EINVAL;

	hash         = d_hash_murmur64(key, key_size, 0);
	idx_subcache = (cache->n_subcache == 1) ? (0) : (uint32_t)(hash % cache->n_subcache);
	sub_cache    = (shm_lru_cache_var_t *)((long int)cache + sizeof(shm_lru_cache_t) +
                                            idx_subcache * cache->size_per_subcache);
	off_bucket   = (int *)((long int)cache + (long int)sub_cache->off_hashbuckets);
	index        = (uint32_t)(hash % cache->capacity_per_subcache);
	offset       = off_bucket[index];

	shm_mutex_lock(&sub_cache->lock);

	*node_found = NULL;
	if (cache->key_size != 0)
		D_ASSERT(cache->key_size == key_size);

	while (offset) {
		node = (shm_lru_node_t *)((long int)cache + (long int)offset);
		if (key_cmp(cache, node, key, key_size) == 0) {
			lru_move_to_head(cache, sub_cache, node);
			*val = ((void *)cache + (long int)node->off_data);
			atomic_fetch_add(&node->ref_count, 1);
			*node_found = node;
			shm_mutex_unlock(&sub_cache->lock);
			return 0;
		}
		if (node->off_hnext == 0) {
			shm_mutex_unlock(&sub_cache->lock);
			return ENOENT;
		}
		offset = node->off_hnext;
	}
	shm_mutex_unlock(&sub_cache->lock);

	return ENOENT;
}

/* Put key-data into cache */
int
shm_lru_put(shm_lru_cache_t *cache, void *key, uint32_t key_size, void *data, uint32_t data_size)
{
	int                  rc;
	uint64_t             hash;
	uint32_t             idx_subcache;
	uint32_t             index;
	shm_lru_cache_var_t *sub_cache;
	int                 *off_bucket;
	int                  offset;
	char                *buf_to_free = NULL;
	char                *buf_data    = NULL;
	shm_lru_node_t      *node;
	shm_lru_node_t      *node_head;
	shm_lru_node_t      *node_new;

	if (cache == NULL || key == NULL || data == NULL)
		return EINVAL;

	hash         = d_hash_murmur64(key, key_size, 0);
	idx_subcache = (cache->n_subcache == 1) ? (0) : (uint32_t)(hash % cache->n_subcache);
	index        = (uint32_t)(hash % cache->capacity_per_subcache);
	sub_cache    = (shm_lru_cache_var_t *)((long int)cache + sizeof(shm_lru_cache_t) +
                                            idx_subcache * cache->size_per_subcache);
	off_bucket   = (int *)((long int)cache + (long int)sub_cache->off_hashbuckets);
	offset       = off_bucket[index];

	shm_mutex_lock(&sub_cache->lock);
	while (offset) {
		node = (shm_lru_node_t *)((long int)cache + offset);
		if (key_cmp(cache, node, key, key_size) == 0) {
			/* key exists in cache */
			if (node->data_size == data_size) {
				/* data size does not change */
				memcpy((char *)cache + node->off_data, data, data_size);
			} else {
				if (cache->prealloc_data == 0) {
					/* save the data buffer pointer which will be freed later */
					buf_to_free = (char *)cache + node->off_data;

					/* data are too long or have unknown size,
					 * dynamically allocate space for data
					 */
					buf_data = shm_alloc(data_size);
					if (buf_data == NULL) {
						shm_mutex_unlock(&sub_cache->lock);
						/* the old data buffer is kept valid */
						return ENOMEM;
					}
					node->off_data = (long int)buf_data - (long int)cache;
					memcpy(buf_data, data, data_size);

					/* new data size is different from the old one,
					 * free the data buffer previously allocated
					 */
					if (buf_to_free)
						shm_free(buf_to_free);
				} else {
					/* make sure data size not longer than buffer */
					D_ASSERT(data_size <= cache->data_size);
					/* use preallocated buffer for data */
					memcpy((char *)cache + node->off_data, data, data_size);
				}

				/* update data size */
				node->data_size = data_size;
			}

			lru_move_to_head(cache, sub_cache, node);
			shm_mutex_unlock(&sub_cache->lock);
			return 0;
		}
		offset = (long int)node->off_hnext;
	}

	/* Not found, remove one node near tail and create new node */
	if (sub_cache->size >= cache->capacity_per_subcache) {
		rc = lru_remove_near_tail(cache, sub_cache);
		if (rc) {
			shm_mutex_unlock(&sub_cache->lock);
			return rc;
		}
	}
	D_ASSERT(sub_cache->size < cache->capacity_per_subcache);

	rc = lru_create_node(cache, sub_cache, key, key_size, data, data_size, &node_new);
	if (rc)
		return rc;
	node_new->idx_bucket = index;
	node_new->off_hnext  = off_bucket[index];
	off_bucket[index]    = (long int)node_new - (long int)cache;

	/* Insert at LRU head */
	node_new->off_next = sub_cache->off_head;
	if (sub_cache->off_head) {
		node_head = (shm_lru_node_t *)((long int)cache + (long int)sub_cache->off_head);
		node_head->off_prev = (long int)node_new - (long int)cache;
	}
	sub_cache->off_head = (long int)node_new - (long int)cache;

	if (!sub_cache->off_tail)
		sub_cache->off_tail = (long int)node_new - (long int)cache;

	sub_cache->size++;
	if (sub_cache->size == cache->capacity_per_subcache)
		sub_cache->first_av = -1;

	shm_mutex_unlock(&sub_cache->lock);
	return 0;
}

void
shm_lru_node_dec_ref(shm_lru_node_t *node)
{
	D_ASSERT(node != NULL);

	atomic_fetch_add(&node->ref_count, -1);
}

int
shm_lru_create_cache(bool auto_partition, uint32_t capacity, uint32_t key_size, uint32_t data_size,
		     shm_lru_cache_t **lru_cache)
{
	int                  rc;
	uint32_t             i;
	uint32_t             j;
	uint32_t             n_subcache;
	uint32_t             capacity_per_subcache;
	uint32_t             prealloc_key;
	uint32_t             prealloc_data;
	size_t               size_key_buf;
	size_t               size_data_buf;
	size_t               size_per_subcache;
	size_t               size_tot;
	shm_lru_node_t      *node_list;
	shm_lru_cache_var_t *sub_cache;
	shm_lru_cache_t     *cache;
	long int             num_cores;

	if (lru_cache == NULL)
		return EINVAL;

	if (auto_partition) {
		/* query the number of cores on current machine */
		num_cores             = d_shm_head->num_core;
		n_subcache            = (uint32_t)num_cores;
		capacity_per_subcache = capacity / n_subcache;
		if (capacity % n_subcache)
			capacity_per_subcache++;
		capacity = capacity_per_subcache * n_subcache;
	} else {
		n_subcache            = 1;
		capacity_per_subcache = capacity;
	}

	/* the space pre-allocated for the array of key of all records if keys have a fixed size */
	prealloc_key = (key_size > 0 && data_size <= LRU_ALLOC_SIZE_THRESHOLD) ? 1 : 0;
	size_key_buf = (prealloc_key == 1) ? (key_size * capacity_per_subcache) : 0;

	/* the space pre-allocated for the array of data of all records if data have a fixed size */
	prealloc_data = (data_size > 0 && data_size <= LRU_ALLOC_SIZE_THRESHOLD) ? 1 : 0;
	size_data_buf = (prealloc_data == 1) ? (data_size * capacity_per_subcache) : 0;

	/**
	 * sub-cache header (sizeof(shm_lru_cache_var_t)),
	 * buckets (sizeof(int) * capacity_per_subcache),
	 * entries (sizeof(shm_lru_node_t) * capacity_per_subcache),
	 * key buffer (size_key_buf),
	 * data buffer (size_data_buf)
	 */
	size_per_subcache = sizeof(shm_lru_cache_var_t) + sizeof(int) * capacity_per_subcache +
			    sizeof(shm_lru_node_t) * capacity_per_subcache + size_key_buf +
			    size_data_buf;

	/* cache header, all sub-cache header and data */
	size_tot = sizeof(shm_lru_cache_t) + size_per_subcache * n_subcache;
	cache    = (shm_lru_cache_t *)shm_alloc(size_tot);
	if (cache == NULL)
		return ENOMEM;
	memset(cache, 0, size_tot);

	cache->n_subcache            = n_subcache;
	cache->capacity_per_subcache = capacity_per_subcache;
	cache->key_size              = key_size;
	cache->data_size             = data_size;
	cache->prealloc_key          = prealloc_key;
	cache->prealloc_data         = prealloc_data;
	cache->size_per_subcache     = size_per_subcache;

	for (i = 0; i < n_subcache; i++) {
		/* the header of individual sub-cache */
		sub_cache = (shm_lru_cache_var_t *)((long int)cache + sizeof(shm_lru_cache_t) +
						    i * size_per_subcache);

		/* size, off_head, off_tail, first_av were set zero by memset() above */

		/* bucket list after sub-cache header */
		sub_cache->off_hashbuckets =
		    (long int)sub_cache + sizeof(shm_lru_cache_var_t) - (long int)cache;
		/* cache entries */
		sub_cache->off_nodelist =
		    sub_cache->off_hashbuckets + sizeof(int) * capacity_per_subcache;
		/* buffer for keys if pre-allocated */
		sub_cache->off_keylist =
		    sub_cache->off_nodelist + sizeof(shm_lru_node_t) * capacity_per_subcache;
		/* buffer for data if pre-allocated */
		sub_cache->off_datalist = sub_cache->off_keylist + size_key_buf;

		/* form a linked list for all free nodes. first_av points to the head node. */
		node_list = (shm_lru_node_t *)((long int)cache + sub_cache->off_nodelist);
		for (j = 0; j < (capacity_per_subcache - 1); j++)
			node_list[j].off_hnext = j + 1;

		/* set invalid node index */
		node_list[j].off_hnext = -1;

		rc = shm_mutex_init(&sub_cache->lock);
		if (rc)
			goto err;
	}
	*lru_cache = cache;

	return 0;

err:
	shm_free(cache);
	return rc;
}

static void
lru_free_dynamic_buff(shm_lru_cache_t *cache)
{
	int                  i;
	int                  offset;
	shm_lru_node_t      *node;
	shm_lru_cache_var_t *sub_cache;

	if (cache->prealloc_key == 1 && cache->prealloc_data == 1)
		return;

	for (i = 0; i < cache->n_subcache; i++) {
		sub_cache = (shm_lru_cache_var_t *)((long int)cache + sizeof(shm_lru_cache_t) +
						    i * cache->size_per_subcache);
		offset    = sub_cache->off_head;
		/* need to loop over all LRU nodes to free key & data buffer */
		while (offset) {
			node = (shm_lru_node_t *)((long int)cache + (long int)offset);
			if (cache->prealloc_key == 0)
				shm_free((char *)cache + node->off_key);
			if (cache->prealloc_data == 0)
				shm_free((char *)cache + node->off_data);
			offset = node->off_next;
		}
	}
}

/* Free all nodes and destroy cache */
void
shm_lru_destroy_cache(shm_lru_cache_t *cache)
{
	D_ASSERT(cache != NULL);

	lru_free_dynamic_buff(cache);
	shm_free(cache);
}
