/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2015-2024, Intel Corporation */

/*
 * dav_flags.h -- Interfaces exported by DAOS internal Allocator for VOS (DAV)
 */

#ifndef __DAOS_COMMON_DAV_V2_H
#define __DAOS_COMMON_DAV_V2_H 1

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include "../dav/dav.h"

typedef struct dav_obj dav_obj_t;
struct umem_store;

/**
 * Create and initialize a DAV object and return its handle.
 *
 * \param[in]	path	Path of the vos file.
 *
 * \param[in]	flags	additional flags (Future).
 *
 * \param[in]	sz	size of the file/heap.
 *
 * \param[in]	mode	permission to use while creating the file.
 *
 * \param[in]	store	backing umem store.
 *
 * \return		Returns the pointer to the object handle. Upon failure,
 *			it returns NULL with errno set appropriately.
 */
dav_obj_t *
dav_obj_create_v2(const char *path, int flags, size_t sz, mode_t mode, struct umem_store *store);

/**
 * Open and initialize a DAV object and return its handle.
 *
 * \param[in]	path	Path of the vos file.
 *
 * \param[in]	flags	additional flags (Future).
 *
 * \param[in]	store	backing umem store.
 *
 * \return		Returns the pointer to the object handle. Upon failure,
 *			it returns NULL with errno set appropriately.
 */
dav_obj_t *
dav_obj_open_v2(const char *path, int flags, struct umem_store *store);

/**
 * Close the DAV object
 *
 * \param[in]	hdl	DAV handle
 */
void
dav_obj_close_v2(dav_obj_t *hdl);

/**
 * Return the pointer to the base of the heap.
 *
 * \param[in]	hdl	DAV handle
 *
 * \return		Returns the pointer to the base of the heap pointed to
 *			by hdl.
 */
void *
dav_get_base_ptr_v2(dav_obj_t *hdl);

typedef int (*dav_constr)(dav_obj_t *pop, void *ptr, void *arg);

/*
 * Allocates a new object from the pool and calls a constructor function before
 * returning. It is guaranteed that allocated object is either properly
 * initialized, or if it's interrupted before the constructor completes, the
 * memory reserved for the object is automatically reclaimed.
 */
int
dav_alloc_v2(dav_obj_t *pop, uint64_t *offp, size_t size, uint64_t type_num, uint64_t flags,
	   dav_constr constructor, void *arg);

/**
 * Frees the memory at specified offset within the DAV object pointed to by hdl.
 *
 * \param[in]	hdl	DAV handle.
 *
 * \param[in]	off	offset to the memory location. off should correspond
 *			to the offset returned by previous call to dav_malloc().
 */
void
dav_free_v2(dav_obj_t *pop, uint64_t off);

/*
 * DAV version of memcpy. Data copied is made persistent in blob.
 */
void *
dav_memcpy_persist_v2(dav_obj_t *pop, void *dest, const void *src, size_t len);

/*
 * If called for the first time on a newly created dav heap, the root object
 * of given size is allocated.  Otherwise, it returns the existing root object.
 * In such case, the size must be not less than the actual root object size
 * stored in the pool.  If it's larger, the root object is automatically
 * resized.
 *
 * This function is currently *not* thread-safe.
 */
uint64_t
dav_root_v2(dav_obj_t *pop, size_t size);

/*
 * Starts a new transaction in the current thread.
 * If called within an open transaction, starts a nested transaction.
 *
 * If successful, transaction stage changes to TX_STAGE_WORK and function
 * returns zero. Otherwise, stage changes to TX_STAGE_ONABORT and an error
 * number is returned.
 */
int
dav_tx_begin_v2(dav_obj_t *pop, jmp_buf env, ...);

/*
 * Aborts current transaction
 *
 * Causes transition to TX_STAGE_ONABORT.
 *
 * This function must be called during TX_STAGE_WORK.
 */
void
dav_tx_abort_v2(int errnum);

/*
 * Commits current transaction
 *
 * This function must be called during TX_STAGE_WORK.
 */
void
dav_tx_commit_v2(void);

/*
 * Cleanups current transaction. Must always be called after dav_tx_begin,
 * even if starting the transaction failed.
 *
 * If called during TX_STAGE_NONE, has no effect.
 *
 * Always causes transition to TX_STAGE_NONE.
 *
 * If transaction was successful, returns 0. Otherwise returns error code set
 * by dav_tx_abort.
 *
 * This function must *not* be called during TX_STAGE_WORK.
 */
int
dav_tx_end_v2(void *data);

/*
 * Returns the current stage of the transaction.
 */
enum dav_tx_stage
dav_tx_stage_v2(void);

/*
 * Returns last transaction error code.
 */
int
dav_tx_errno_v2(void);

/*
 * Transactionally allocates a new object.
 *
 * If successful, returns offset of the object in the heap.
 * Otherwise, stage changes to TX_STAGE_ONABORT and an zero is returned.
 * 'Flags' is a bitmask of the following values:
 *  - POBJ_XALLOC_ZERO - zero the allocated object
 *  - POBJ_XALLOC_NO_FLUSH - skip flush on commit
 *  - POBJ_XALLOC_NO_ABORT - if the function does not end successfully,
 *  - DAV_CLASS_ID(id)	   - id of allocation class to use.
 *  - DAV_EZONE_ID(id)	   - id of zone to use.
 *  do not abort the transaction and return the error number.
 *
 * This function must be called during TX_STAGE_WORK.
 */
uint64_t
dav_tx_alloc_v2(size_t size, uint64_t type_num, uint64_t flags);

/*
 * Transactionally frees an existing object.
 *
 * If successful, returns zero.
 * Otherwise, stage changes to TX_STAGE_ONABORT and an error number is returned.
 *
 * This function must be called during TX_STAGE_WORK.
 */
int
dav_tx_free_v2(uint64_t off);

/*
 * Takes a "snapshot" of the memory block of given size and located at given
 * offset 'off' in the object 'oid' and saves it in the undo log.
 * The application is then free to directly modify the object in that memory
 * range. In case of failure or abort, all the changes within this range will
 * be rolled-back automatically.
 *
 * If successful, returns zero.
 * Otherwise, stage changes to TX_STAGE_ONABORT and an error number is returned.
 *
 * This function must be called during TX_STAGE_WORK.
 */
int
dav_tx_add_range_v2(uint64_t off, size_t size);

/*
 * Takes a "snapshot" of the given memory region and saves it in the undo log.
 * The application is then free to directly modify the object in that memory
 * range. In case of failure or abort, all the changes within this range will
 * be rolled-back automatically. The supplied block of memory has to be within
 * the given pool.
 *
 * If successful, returns zero.
 * Otherwise, stage changes to TX_STAGE_ONABORT and an error number is returned.
 *
 * This function must be called during TX_STAGE_WORK.
 */
int
dav_tx_add_range_direct_v2(const void *ptr, size_t size);

/*
 * Behaves exactly the same as dav_tx_add_range when 'flags' equals 0.
 * 'Flags' is a bitmask of the following values:
 *  - POBJ_XADD_NO_FLUSH - skips flush on commit
 *  - POBJ_XADD_NO_SNAPSHOT - added range will not be snapshotted
 *  - POBJ_XADD_ASSUME_INITIALIZED - added range is assumed to be initialized
 *  - POBJ_XADD_NO_ABORT - if the function does not end successfully,
 *  do not abort the transaction and return the error number.
 */
int
dav_tx_xadd_range_v2(uint64_t off, size_t size, uint64_t flags);

/*
 * Behaves exactly the same as dav_tx_add_range_direct when 'flags' equals
 * 0. 'Flags' is a bitmask of the following values:
 *  - POBJ_XADD_NO_FLUSH - skips flush on commit
 *  - POBJ_XADD_NO_SNAPSHOT - added range will not be snapshotted
 *  - POBJ_XADD_ASSUME_INITIALIZED - added range is assumed to be initialized
 *  - POBJ_XADD_NO_ABORT - if the function does not end successfully,
 *  do not abort the transaction and return the error number.
 */
int
dav_tx_xadd_range_direct_v2(const void *ptr, size_t size, uint64_t flags);

#define DAV_ACTION_XRESERVE_VALID_FLAGS						\
	(DAV_XALLOC_CLASS_MASK | DAV_XALLOC_EZONE_MASK | DAV_XALLOC_ZERO)

struct dav_action;
uint64_t
dav_reserve_v2(dav_obj_t *pop, struct dav_action *act, size_t size, uint64_t type_num,
	     uint64_t flags);
void
dav_defer_free_v2(dav_obj_t *pop, uint64_t off, struct dav_action *act);
void
dav_cancel_v2(dav_obj_t *pop, struct dav_action *actv, size_t actvcnt);
int
dav_tx_publish_v2(struct dav_action *actv, size_t actvcnt);

struct dav_alloc_class_desc;
/*
 * Registers an allocation class handle with the DAV object.
 */
int
dav_class_register_v2(dav_obj_t *pop, struct dav_alloc_class_desc *p);

struct dav_heap_stats;
/*
 * Returns the heap allocation statistics associated  with the
 * DAV object.
 */
int
dav_get_heap_stats_v2(dav_obj_t *pop, struct dav_heap_stats *st);

struct dav_heap_mb_stats {
	uint64_t dhms_allocated;
	uint64_t dhms_maxsz;
};

/**
 * Returns the usage statistics of a memory bucket. Note that usage
 * stats for evictable MBs will be approximate values if they are not
 * yet loaded on to the umem cache.
 *
 * \param[in]           pop             pool handle
 * \param[in]           mb_id           memory bucket id
 * \param[out]          st              mb stats
 *
 * \return   0, success
 *         < 0, error and errno is set to appropriate value.
 */
int
dav_get_heap_mb_stats_v2(dav_obj_t *pop, uint32_t mb_id, struct dav_heap_mb_stats *st);

/**
 * Allot an evictable memory bucket for tasks like new object creation
 *
 * \param[in]           pop             pool handle
 * \param[in]           flags           zone selection criteria.
 *
 * \return id > 0, mbid of evictable memory bucket.
 *         id = 0, no evictable memory bucket is available
 *                 use non-evictable memory bucket.
 */
uint32_t
dav_allot_mb_evictable_v2(dav_obj_t *pop, int flags);

/*
 * Return the page size for dav_v2.
 */
size_t
dav_obj_pgsz_v2();

/** Force GC to reclaim freeblocks and mark empty non-evictable
 *  memory buckets as unused, thus allowing more umem_cache
 *  for non-evictable memory buckets.
 *
 * \param[in]           pop             pool handle
 *
 * \return  0, success
 *        < 0, error and errno is set to appropriate value.
 */
int
dav_force_gc_v2(dav_obj_t *pop);
#endif /* __DAOS_COMMON_DAV_V2_H */
