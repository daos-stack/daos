/**
 * (C) Copyright 2024-2025 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_SHM_ALLOC_INTERNAL_H__
#define __DAOS_SHM_ALLOC_INTERNAL_H__

#include <stdatomic.h>
#include <gurt/atomic.h>
#include <gurt/common.h>
#include <gurt/shm_utils.h>
#include <gurt/shm_tlsf.h>

/* the magic value stored at the header of share memory region */
#define DSM_MAGIC        (0x13577531)

/**
 * the number of shared memory allocators. Use multiple allocators to alleviate lock contentions
 * since the allocator currently used is not thread safe.
 */
#define N_SHM_FIXED_POOL (8)

/* the size of each shm pool */
#define SHM_POOL_SIZE    (256 * 1024 * 1024L)

/* the total size of shared memory that will be allocated */
#define SHM_SIZE_TOTAL   (SHM_POOL_SIZE * N_SHM_FIXED_POOL)

/**
 * the threshold value to determine whether requesting large memory. The ways to pick memory
 * allocator are different for large and small memory blocks.
 */
#define LARGE_MEM        (64 * 1024)

#define DEFAULT_CACHE_DENTRY_CAPACITY (256 * 1024)

/* Head of shared memory region */
struct d_shm_hdr {
	/* magic not equal DSM_MAGIC means shared memory is not initialized yet */
	int              magic;
	/* the version number. Reserved for future usage. It will be bumped up when a shared memory
	 * region is added/removed.
	 */
	int              version;
	d_shm_mutex_t    g_lock;
	int              num_pool;
	/* the count of how many processes are mapping the shared memory region */
	_Atomic int      ref_count;
	/* global counter used for round robin picking memory allocator for large memory request */
	_Atomic uint64_t large_mem_count;
	/* array of offset of fixed (non-freeable) memory pool */
	off_t            off_fixed_pool[N_SHM_FIXED_POOL];

	/* the lock needed when a hash table to be created or destroyed */
	d_shm_mutex_t    ht_lock;
	/* the offset to the first hash table head */
	long int         off_ht_head;

	/* the offset to LRU directory entry cache */
	long int         off_lru_cache_dentry;

	/* the total size of shared memory region */
	uint64_t         size;
	/* size of each shared memory allocator's pool */
	uint64_t         shm_pool_size;
	/* the number of physical cores on current node */
	uint32_t         num_core;
	uint32_t         pad;
	/* reserved for future usage */
	char             reserved[256];
};

/* store the starting and ending addresses of a pool in current process */
struct shm_pool_local {
	/* the beginning of the pool in shared memory */
	char *addr_s;
	/* the end of the pool in shared memory */
	char *addr_e;
	bool  freeable;
};

/* the address of shared memory region */
extern struct d_shm_hdr *d_shm_head;

/* the total size of shared memory that will be allocated */
#define SHM_SIZE_REQ (SHM_POOL_SIZE * N_SHM_FIXED_POOL + sizeof(struct d_shm_hdr))

/* Node of entry in LRU cache */
struct shm_lru_node {
	/* key size*/
	uint32_t    key_size;
	/* data size*/
	uint32_t    data_size;
	/* store the offset to key */
	long int    off_key;
	/* store the offset to data */
	long int    off_data;
	/* the reference count of this record */
	_Atomic int ref_count;
	/* off_prev and off_next are used in doubly linked list for LRU */
	int         off_prev;
	int         off_next;
	/* offset to the next node in hash chain in each bucket for allocated node. point to next
	 * available node for free nodes
	 */
	int         off_hnext;
	/* the index of hash bucket this record is in */
	uint32_t    idx_bucket;
	/* the index of sub-cache this record is in */
	uint32_t    idx_subcache;
};

/* This implementation of shm LRU is mainly optimized for performance by using pre-allocated buffer
 * when possible and fine grained lock
 */

typedef struct {
	/* number of nodes allocated in this shard */
	uint32_t      size;
	/* Most recently used node */
	int           off_head;
	/* Least recently used node */
	int           off_tail;
	/* First available/free node */
	int           first_av;
	/* the offset to the array of offset of hash buckets */
	long int      off_hashbuckets;
	/* the offset to the array of preallocated array of nodes */
	long int      off_nodelist;
	/* the offset to the array of preallocated array of keys */
	long int      off_keylist;
	/* the offset to the array of preallocated array of data */
	long int      off_datalist;

	d_shm_mutex_t lock;
	char          pad[8];
} shm_lru_cache_var_t;

/* LRU Cache structure */
struct shm_lru_cache {
	/* the number of sub-cache to use */
	uint32_t n_subcache;
	/* max number of nodes to hold in each shard */
	uint32_t capacity_per_subcache;
	/* the size of key. zero means key size is variable */
	uint32_t key_size;
	/* the size of data. zero means data size is variable */
	uint32_t data_size;
	/* 0 - dynamically allocate buffer for key, 1 - use pre-allocated buffer for key */
	uint32_t prealloc_key;
	/* 0 - dynamically allocate buffer for data, 1 - use pre-allocated buffer for data */
	uint32_t prealloc_data;

	/* number of bytes per sub-cache */
	size_t   size_per_subcache;
};

/*
 * data layout of LRU cache:
 * 1) LRU Cache header (struct shm_lru_cache)
 * 2) 1st sub-cache header (struct shm_lru_cache_var_t)
 * 3) 1st sub-cache data (bucket, nodes, key, data)
 * 4) 2nd sub-cache header (struct shm_lru_cache_var_t)
 * 5) 2nd sub-cache data (bucket, nodes, key, data)
 * 6) ...
 */

#endif
