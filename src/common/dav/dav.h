/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2015-2022, Intel Corporation */

/*
 * dav_flags.h -- Interfaces exported by DAOS internal Allocator for VOS (DAV)
 */

#ifndef __DAOS_COMMON_DAV_H
#define __DAOS_COMMON_DAV_H 1

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

/*
 * allocation functions flags
 */
#define DAV_FLAG_ZERO			(((uint64_t)1) << 0)
#define DAV_FLAG_NO_FLUSH		(((uint64_t)1) << 1)
#define DAV_FLAG_NO_SNAPSHOT		(((uint64_t)1) << 2)
#define DAV_FLAG_ASSUME_INITIALIZED	(((uint64_t)1) << 3)
#define DAV_FLAG_TX_NO_ABORT		(((uint64_t)1) << 4)

#define DAV_CLASS_ID(id)		(((uint64_t)(id)) << 48)
#define DAV_ARENA_ID(id)		(((uint64_t)(id)) << 32)

#define DAV_XALLOC_CLASS_MASK		((((uint64_t)1 << 16) - 1) << 48)
#define DAV_XALLOC_ARENA_MASK		((((uint64_t)1 << 16) - 1) << 32)
#define DAV_XALLOC_ZERO			DAV_FLAG_ZERO
#define DAV_XALLOC_NO_FLUSH		DAV_FLAG_NO_FLUSH
#define DAV_XALLOC_NO_ABORT		DAV_FLAG_TX_NO_ABORT

#define DAV_TX_XALLOC_VALID_FLAGS	(DAV_XALLOC_ZERO |\
					DAV_XALLOC_NO_FLUSH |\
					DAV_XALLOC_ARENA_MASK |\
					DAV_XALLOC_CLASS_MASK |\
					DAV_XALLOC_NO_ABORT)

#define DAV_XADD_NO_FLUSH		DAV_FLAG_NO_FLUSH
#define DAV_XADD_NO_SNAPSHOT		DAV_FLAG_NO_SNAPSHOT
#define DAV_XADD_ASSUME_INITIALIZED	DAV_FLAG_ASSUME_INITIALIZED
#define DAV_XADD_NO_ABORT		DAV_FLAG_TX_NO_ABORT
#define DAV_XADD_VALID_FLAGS		(DAV_XADD_NO_FLUSH |\
					DAV_XADD_NO_SNAPSHOT |\
					DAV_XADD_ASSUME_INITIALIZED |\
					DAV_XADD_NO_ABORT)

/*
 * WAL Redo hints.
 */
#define DAV_XADD_WAL_CPTR		(((uint64_t)1) << 5)

#define DAV_XLOCK_NO_ABORT	DAV_FLAG_TX_NO_ABORT
#define DAV_XLOCK_VALID_FLAGS	(DAV_XLOCK_NO_ABORT)

#define DAV_XFREE_NO_ABORT	DAV_FLAG_TX_NO_ABORT
#define DAV_XFREE_VALID_FLAGS	(DAV_XFREE_NO_ABORT)

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
dav_obj_create(const char *path, int flags, size_t sz, mode_t mode,
	       struct umem_store *store);

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
dav_obj_open(const char *path, int flags, struct umem_store *store);

/**
 * Close the DAV object
 *
 * \param[in]	hdl	DAV handle
 */
void
dav_obj_close(dav_obj_t *hdl);

/**
 * Return the pointer to the base of the heap.
 *
 * \param[in]	hdl	DAV handle
 *
 * \return		Returns the pointer to the base of the heap pointed to
 *			by hdl.
 */
void *
dav_get_base_ptr(dav_obj_t *hdl);

typedef int (*dav_constr)(dav_obj_t *pop, void *ptr, void *arg);

/*
 * Allocates a new object from the pool and calls a constructor function before
 * returning. It is guaranteed that allocated object is either properly
 * initialized, or if it's interrupted before the constructor completes, the
 * memory reserved for the object is automatically reclaimed.
 */
int dav_alloc(dav_obj_t *pop, uint64_t *offp, size_t size,
	      uint64_t type_num, dav_constr constructor, void *arg);

/**
 * Frees the memory at specified offset within the DAV object pointed to by hdl.
 *
 * \param[in]	hdl	DAV handle.
 *
 * \param[in]	off	offset to the memory location. off should correspond
 *			to the offset returned by previous call to dav_malloc().
 */
void
dav_free(dav_obj_t *pop, uint64_t off);

/*
 * DAV version of memcpy. Data copied is made persistent in blob.
 */
void *dav_memcpy_persist(dav_obj_t *pop, void *dest, const void *src,
			 size_t len);
/*
 * DAV version of memcpy with deferred commit to blob.
 */
void *dav_memcpy_persist_relaxed(dav_obj_t *pop, void *dest, const void *src,
				 size_t len);

/*
 * If called for the first time on a newly created dav heap, the root object
 * of given size is allocated.  Otherwise, it returns the existing root object.
 * In such case, the size must be not less than the actual root object size
 * stored in the pool.  If it's larger, the root object is automatically
 * resized.
 *
 * This function is currently *not* thread-safe.
 */
uint64_t dav_root(dav_obj_t *pop, size_t size);


/*
 * Transactions
 *
 * Stages are changed only by the dav_tx_* functions, each transition
 * to the TX_STAGE_ONABORT is followed by a longjmp to the jmp_buf provided in
 * the dav_tx_begin function.
 */
enum dav_tx_stage {
	DAV_TX_STAGE_NONE,	/* no transaction in this thread */
	DAV_TX_STAGE_WORK,	/* transaction in progress */
	DAV_TX_STAGE_ONCOMMIT,	/* successfully committed */
	DAV_TX_STAGE_ONABORT,	/* tx_begin failed or transaction aborted */
	DAV_TX_STAGE_FINALLY,	/* always called */

	DAV_MAX_TX_STAGE
};

typedef void (*dav_tx_callback)(dav_obj_t *pop, enum dav_tx_stage stage,
	       void *);

enum dav_tx_param {
	DAV_TX_PARAM_NONE,
	DAV_TX_PARAM_UNUSED1,	/* For parity with libpmemobj */
	DAV_TX_PARAM_UNUSED2,	/* For parity with libpmemobj */
	DAV_TX_PARAM_CB,	/* dav_tx_callback cb, void *arg */
};

/*
 * Starts a new transaction in the current thread.
 * If called within an open transaction, starts a nested transaction.
 *
 * If successful, transaction stage changes to TX_STAGE_WORK and function
 * returns zero. Otherwise, stage changes to TX_STAGE_ONABORT and an error
 * number is returned.
 */
int dav_tx_begin(dav_obj_t *pop, jmp_buf env, ...);

/*
 * Aborts current transaction
 *
 * Causes transition to TX_STAGE_ONABORT.
 *
 * This function must be called during TX_STAGE_WORK.
 */
void dav_tx_abort(int errnum);

/*
 * Commits current transaction
 *
 * This function must be called during TX_STAGE_WORK.
 */
void dav_tx_commit(void);

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
int dav_tx_end(void *data);

/*
 * Returns the current stage of the transaction.
 */
enum dav_tx_stage dav_tx_stage(void);

/*
 * Returns last transaction error code.
 */
int dav_tx_errno(void);

/*
 * Transactionally allocates a new object.
 *
 * If successful, returns PMEMoid.
 * Otherwise, stage changes to TX_STAGE_ONABORT and an OID_NULL is returned.
 *
 * This function must be called during TX_STAGE_WORK.
 */
uint64_t dav_tx_alloc(size_t size, uint64_t type_num);

/*
 * Transactionally allocates a new object.
 *
 * If successful, returns PMEMoid.
 * Otherwise, stage changes to TX_STAGE_ONABORT and an OID_NULL is returned.
 * 'Flags' is a bitmask of the following values:
 *  - POBJ_XALLOC_ZERO - zero the allocated object
 *  - POBJ_XALLOC_NO_FLUSH - skip flush on commit
 *  - POBJ_XALLOC_NO_ABORT - if the function does not end successfully,
 *  do not abort the transaction and return the error number.
 *
 * This function must be called during TX_STAGE_WORK.
 */
uint64_t dav_tx_xalloc(size_t size, uint64_t type_num, uint64_t flags);

/*
 * Transactionally allocates new zeroed object.
 *
 * If successful, returns PMEMoid.
 * Otherwise, stage changes to TX_STAGE_ONABORT and an OID_NULL is returned.
 *
 * This function must be called during TX_STAGE_WORK.
 */
uint64_t dav_tx_zalloc(size_t size, uint64_t type_num);

/*
 * Transactionally frees an existing object.
 *
 * If successful, returns zero.
 * Otherwise, stage changes to TX_STAGE_ONABORT and an error number is returned.
 *
 * This function must be called during TX_STAGE_WORK.
 */
int dav_tx_free(uint64_t off);

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
int dav_tx_add_range(uint64_t off, size_t size);

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
int dav_tx_add_range_direct(const void *ptr, size_t size);

/*
 * Behaves exactly the same as dav_tx_add_range when 'flags' equals 0.
 * 'Flags' is a bitmask of the following values:
 *  - POBJ_XADD_NO_FLUSH - skips flush on commit
 *  - POBJ_XADD_NO_SNAPSHOT - added range will not be snapshotted
 *  - POBJ_XADD_ASSUME_INITIALIZED - added range is assumed to be initialized
 *  - POBJ_XADD_NO_ABORT - if the function does not end successfully,
 *  do not abort the transaction and return the error number.
 */
int dav_tx_xadd_range(uint64_t off, size_t size, uint64_t flags);

/*
 * Behaves exactly the same as dav_tx_add_range_direct when 'flags' equals
 * 0. 'Flags' is a bitmask of the following values:
 *  - POBJ_XADD_NO_FLUSH - skips flush on commit
 *  - POBJ_XADD_NO_SNAPSHOT - added range will not be snapshotted
 *  - POBJ_XADD_ASSUME_INITIALIZED - added range is assumed to be initialized
 *  - POBJ_XADD_NO_ABORT - if the function does not end successfully,
 *  do not abort the transaction and return the error number.
 */
int dav_tx_xadd_range_direct(const void *ptr, size_t size, uint64_t flags);

/*
 * Converts the offset to a pointer in the context of heap associated with
 * current transaction.
 */
void *dav_tx_off2ptr(uint64_t off);

enum dav_action_type {
	/* a heap action (e.g., alloc) */
	DAV_ACTION_TYPE_HEAP,
	/* a single memory operation (e.g., value set)  */
	DAV_ACTION_TYPE_MEM,

	DAV_MAX_ACTION_TYPE
};

struct dav_action_heap {
	/* offset to the element being freed/allocated */
	uint64_t offset;
	/* usable size of the element being allocated */
	uint64_t usable_size;
};

struct dav_action {
	/*
	 * These fields are internal for the implementation and are not
	 * guaranteed to be stable across different versions of the API.
	 * Use with caution.
	 *
	 * This structure should NEVER be stored on persistent memory!
	 */
	enum dav_action_type type;
	uint32_t data[3];
	union {
		struct dav_action_heap heap;
		uint64_t data2[14];
	};
};

uint64_t dav_reserve(dav_obj_t *pop, struct dav_action *act, size_t size, uint64_t type_num);
void dav_defer_free(dav_obj_t *pop, uint64_t off, struct dav_action *act);
int dav_publish(dav_obj_t *pop, struct dav_action *actv, size_t actvcnt);
void dav_cancel(dav_obj_t *pop, struct dav_action *actv, size_t actvcnt);
int dav_tx_publish(struct dav_action *actv, size_t actvcnt);

/*
 * Allocation class interface
 *
 * When requesting an object from the allocator, the first step is to determine
 * which allocation class best approximates the size of the object.
 * Once found, the appropriate free list, called bucket, for that
 * class is selected in a fashion that minimizes contention between threads.
 * Depending on the requested size and the allocation class, it might happen
 * that the object size (including required metadata) would be bigger than the
 * allocation class size - called unit size. In those situations, the object is
 * constructed from two or more units (up to 64).
 *
 * If the requested number of units cannot be retrieved from the selected
 * bucket, the thread reaches out to the global, shared, heap which manages
 * memory in 256 kilobyte chunks and gives it out in a best-fit fashion. This
 * operation must be performed under an exclusive lock.
 * Once the thread is in the possession of a chunk, the lock is dropped, and the
 * memory is split into units that repopulate the bucket.
 *
 * These are the CTL entry points that control allocation classes:
 * - heap.alloc_class.[class_id].desc
 *	Creates/retrieves allocation class information
 *
 * It's VERY important to remember that the allocation classes are a RUNTIME
 * property of the allocator - they are NOT stored persistently in the pool.
 * It's recommended to always create custom allocation classes immediately after
 * creating or opening the pool, before any use.
 * If there are existing objects created using a class that is no longer stored
 * in the runtime state of the allocator, they can be normally freed, but
 * allocating equivalent objects will be done using the allocation class that
 * is currently defined for that size.
 */

/*
 * Persistent allocation header
 */
enum dav_header_type {
	/*
	 * 64-byte header used up until the version 1.3 of the library,
	 * functionally equivalent to the compact header.
	 * It's not recommended to create any new classes with this header.
	 */
	DAV_HEADER_LEGACY,
	/*
	 * 16-byte header used by the default allocation classes. All library
	 * metadata is by default allocated using this header.
	 * Supports type numbers and variably sized allocations.
	 */
	DAV_HEADER_COMPACT,
	/*
	 * 0-byte header with metadata stored exclusively in a bitmap. This
	 * ensures that objects are allocated in memory contiguously and
	 * without attached headers.
	 * This can be used to create very small allocation classes, but it
	 * does not support type numbers.
	 * Additionally, allocations with this header can only span a single
	 * unit.
	 * Objects allocated with this header do show up when iterating through
	 * the heap using palloc_first/palloc_next functions, but have a
	 * type_num equal 0.
	 */
	DAV_HEADER_NONE,

	MAX_DAV_HEADER_TYPES
};

/*
 * Description of allocation classes
 */
struct dav_alloc_class_desc {
	/*
	 * The number of bytes in a single unit of allocation. A single
	 * allocation can span up to 64 units (or 1 in the case of no header).
	 * If one creates an allocation class with a certain unit size and
	 * forces it to handle bigger sizes, more than one unit
	 * will be used.
	 * For example, an allocation class with a compact header and 128 bytes
	 * unit size, for a request of 200 bytes will create a memory block
	 * containing 256 bytes that spans two units. The usable size of that
	 * allocation will be 240 bytes: 2 * 128 - 16 (header).
	 */
	size_t unit_size;

	/*
	 * Desired alignment of objects from the allocation class.
	 * If non zero, must be a power of two and an even divisor of unit size.
	 *
	 * All allocation classes have default alignment
	 * of 64. User data alignment is affected by the size of a header. For
	 * compact one this means that the alignment is 48 bytes.
	 *
	 */
	size_t alignment;

	/*
	 * The minimum number of units that must be present in a
	 * single, contiguous, memory block.
	 * Those blocks (internally called runs), are fetched on demand from the
	 * heap. Accessing that global state is a serialization point for the
	 * allocator and thus it is imperative for performance and scalability
	 * that a reasonable amount of memory is fetched in a single call.
	 * Threads generally do not share memory blocks from which they
	 * allocate, but blocks do go back to the global heap if they are no
	 * longer actively used for allocation.
	 */
	unsigned units_per_block;

	/*
	 * The header of allocations that originate from this allocation class.
	 */
	enum dav_header_type header_type;

	/*
	 * The identifier of this allocation class.
	 */
	unsigned class_id;
};

/*
 * Registers an allocation class handle with the DAV object.
 */
int dav_class_register(dav_obj_t *pop, struct dav_alloc_class_desc *p);


struct dav_heap_stats {
	uint64_t curr_allocated;
	uint64_t run_allocated;
	uint64_t run_active;
};
/*
 * Returns the heap allocation statistics associated  with the
 * DAV object.
 */
int dav_get_heap_stats(dav_obj_t *pop, struct dav_heap_stats *st);

struct umem_wal_tx;

uint32_t wal_tx_act_nr(struct umem_wal_tx *tx);
uint32_t wal_tx_payload_len(struct umem_wal_tx *tx);
struct umem_action *wal_tx_act_first(struct umem_wal_tx *tx);
struct umem_action *wal_tx_act_next(struct umem_wal_tx *tx);

#endif /* __DAOS_COMMON_DAV_H */
