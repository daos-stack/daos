/**
 * (C) Copyright 2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_SHM_ALLOC_INTERNAL_H__
#define __DAOS_SHM_ALLOC_INTERNAL_H__

#include <stdatomic.h>
#include <gurt/shm_utils.h>
#include <gurt/shm_tlsf.h>

/* the magic value stored at the header of share memory region */
#define DSM_MAGIC      (0x13577531)

/* the fixed address for shared memory in all processes. We will phase this out later. */
#define FIXED_SHM_ADDR ((void *)0x600000000000)

/**
 * the number of shared memory allocators. Use multiple allocators to alleviate lock contentions
 * since the allocator currently used is not thread safe.
 */
#define N_SHM_POOL     (8)

/* the size of each shm pool */
#define SHM_POOL_SIZE  (1024*1024*1024L)

/* the total size of shared memory that will be allocated */
#define SHM_SIZE_TOTAL (SHM_POOL_SIZE * N_SHM_POOL)

/**
 * the threshold value to determine whether requesting large memory. The ways to pick memory
 * allocator are different for large and small memory blocks.
 */
#define LARGE_MEM      (64 * 1024)

/* Head of shared memory region */
struct d_shm_hdr {
	/* magic not equal DSM_MAGIC means shared memory is not initialized yet */
	int              magic;
	d_shm_mutex_t    g_lock;
	/* the count of how many processes are mapping the shared memory region */
	_Atomic int      ref_count;
	/* global counter used for round robin picking memory allocator for large memory request */
	_Atomic uint64_t large_mem_count;
	/* array of pointors to memory allocators */
	tlsf_t           tlsf[N_SHM_POOL];
	/* lock for accessing one individual memory allocator */
	d_shm_mutex_t    mem_lock[N_SHM_POOL];

	/* the lock needed when a hash table to be created or destroyed */
	d_shm_mutex_t    ht_lock;
	/* the offset to the first hash table head */
	long int         off_ht_head;

	/* the total size of shared memory region */
	uint64_t         size;
	/* size of each shared memory allocator's pool */
	uint64_t         shm_pool_size;
	/* reserved for future usage */
	char             reserved[256];
};

/* the address of shared memory region */
extern struct d_shm_hdr *d_shm_head;

/* the total size of shared memory that will be allocated */
#define SHM_SIZE_REQ   (SHM_POOL_SIZE * N_SHM_POOL + sizeof(struct d_shm_hdr))

#endif
