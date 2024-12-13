/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_SHM_ALLOC_H__
#define __DAOS_SHM_ALLOC_H__

#include <stdint.h>
#include <stdatomic.h>
#include <gurt/shm_tlsf.h>

/* default value for invalid offset pointer */
#define INVALID_OFFSET  (-1L)

/* the magic value stored at the header of share memory region */
#define DSM_MAGIC       (0x13577531)

/* the fixed address for shared memory in all processes. We will phase this out later. */
#define FIXED_SHM_ADDR  ((void *)0x600000000000)

/**
 * the number of shared memory allocators. Use multiple allocators to minimize lock contentions
 * since the allocator currently used is not thread safe.
 */
#define N_SHM_POOL    (8)

/* the size of each shm pool */
#define SHM_POOL_SIZE (1024*1024*1024L)

/* the total size of shared memory that will be allocated */
#define SHM_SIZE_TOTAL  (SHM_POOL_SIZE * N_SHM_POOL)

/**
 * the threshold value to determine whether requesting large memory. The ways to pick memory
 * allocator are different for large and small memory blocks.
 */
#define LARGE_MEM       (64 * 1024)

/* the address of shared memory region */
extern struct d_shm_alloc *d_shm_head;

/* Local info about a shm buffer. Each process has its own copy. */
struct d_shm_alloc {
	/* magic not equal DSM_MAGIC means shared memory is not initialized yet */
	int              magic;
	pthread_mutex_t  g_lock;
	/* the count of how many processes are mapping the shared memory region */
	_Atomic int      ref_count;
	/* global counter used for round robin picking memory allocator for large memory request */
	_Atomic uint64_t large_mem_count;
	/* array of pointors to memory allocators */
	tlsf_t           tlsf[N_SHM_POOL];
	/* lock for accessing one individual memory allocator */
	pthread_mutex_t  mem_lock[N_SHM_POOL];

	/* the lock needed when a hash table to be created or destroyed */
	pthread_mutex_t  ht_lock;
	/* the offset to the first hash table head */
	long int         off_ht_head;

	/* the total size of shared memory region */
	uint64_t         size;
	/* reserved for future usage */
	char             reserved[256];
};

/* the total size of shared memory that will be allocated */
#define SHM_SIZE_REQ  (SHM_POOL_SIZE * N_SHM_POOL + sizeof(struct d_shm_alloc))


/**
 * Initialize shared memory region in current process
 *
 * \return			zero for success. return error code otherwise.
 */
int
shm_init(void);

/**
 * Unmap and decrease the reference count of shared memory. Shared memory should not be referenced
 * after shm_dec_ref() is called.
 */
void
shm_dec_ref(void);

/**
 * Increase the reference of shared memory.
 */
void
shm_inc_ref(void);

/**
 * Allocate memory from shared memory region
 *
 * \param[in] size		size of memory block requested
 *
 * \return			buffer address
 */
void *
shm_alloc(size_t size);

/**
 * Remove shared memory file under /dev/shm/ when tests finish
 */
void
shm_destroy(void);

/**
 * Allocate memory from shared memory region with alignment
 *
 * \param[in] align		size of alignment
 * \param[in] size		size of memory block requested
 *
 * \return			buffer address
 */
void *
shm_memalign(size_t align, size_t size);

/**
 * Free a memory block which was allocated from shared memory region
 *
 * \param[in] ptr		memory block address
 *
 */
void
shm_free(void *ptr);

/**
 * Query whether shared memory region is initialized properly or not
 *
 * \return			True/False
 */
bool
shm_inited(void);

#endif
