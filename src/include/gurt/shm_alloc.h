/**
 * (C) Copyright 2024-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_SHM_ALLOC_H__
#define __DAOS_SHM_ALLOC_H__

/* default value for invalid offset pointer */
#define INVALID_OFFSET (-1L)

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
shm_fini(void);

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

/**
 * query the base address of shared memory region
 */
void *
shm_base(void);

#endif
