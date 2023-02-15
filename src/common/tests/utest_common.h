/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <daos/mem.h>

#define UTEST_POOL_NAME_MAX	255

struct utest_context;

/** Create pmem context for unit testing.  This creates a pool and a root
 *  object of specified sizes.  Note that the root object should be
 *  retrieved with utest_utx2root* rather than pmemobj_root as the real
 *  root type is internal to the helper library.
 *
 *  \param	name[IN]	The path of the pool
 *  \param	pool_size[IN]	The size of the pool in bytes
 *  \param	root_size[IN]	The size of the root object
 *  \param	store[IN]	umem store associated with pool
 *  \param	utx[OUT]
 *
 *  \return 0 on success, error otherwise
 */
int utest_pmem_create(const char *name, size_t pool_size, size_t root_size,
		      struct umem_store *store, struct utest_context **utx);

/** Create vmem context for unit testing.  This allocates a context and
 *  a root object of the specified size.  This isn't wholly necessary but
 *  ensures that we have a common interface whether we are using vmem or
 *  pmem.
 *
 *  \param	root_size[IN]	The size of the root object
 *  \param	utx[OUT]
 *
 *  \return 0 on success, error otherwise
 */
int utest_vmem_create(size_t root_size, struct utest_context **utx);

/** Destroy a context and free any associated resources (e.g. memory,
 *  pmemobj pool, files)
 *
 *  \param	utx[IN]	The context to destroy
 *
 *  \return 0 on success, error otherwise
 */
int utest_utx_destroy(struct utest_context *utx);

/** Retrieve a pointer to the root object in a context
 *
 *  \param	utx[IN]	The context to query
 *
 *  \return	Pointer to root
 */
void *utest_utx2root(struct utest_context *utx);

/** Retrieve an umoff pointer to the root object in a context
 *
 *  \param	utx[IN]	The context to query
 *
 *  \return	umoff pointer to root
 */
umem_off_t utest_utx2rootoff(struct utest_context *utx);

/** Initialization callback for object allocation API.  If the context
 *  is a pmem context, this will be invoked inside a transaction.  The
 *  pointer will already be added to the transaction but it is up to the
 *  user to add any additional memory modified.
 *
 *  \param	ptr[IN, OUT]	The pointer to initialize
 *  \param	size[IN]	The size of the allocation
 *  \param	cb_arg[IN]	Argument passed in by user
 */
typedef void (*utest_init_cb)(void *ptr, size_t size, const void *cb_arg);

/** Allocate an object and, optionally, initialize it.  If the context
 *  is a pmem context, this will be done in a transaction.
 *
 *  \param	utx[IN]		The context
 *  \param	off[OUT]	The allocated offset
 *  \param	size[IN]	The size to allocate
 *  \param	cb[IN]		Optional initialization callback
 *  \param	cb_arg[IN]	Optional argument to pass to initialization
 *
 *  \return 0 on success, error otherwise
 */
int utest_alloc(struct utest_context *utx, umem_off_t *umoff, size_t size,
		utest_init_cb cb, const void *cb_arg);

/** Free an object
 *
 *  \param	utx[IN]		The context
 *  \param	umoff[IN]	The allocated offset to free
 *
 *  \return 0 on success, error otherwise
 */
int utest_free(struct utest_context *utx, umem_off_t umoff);

/** Get the umem_instance for a context
 *
 *  \param	utx[IN]		The context
 *
 *  \return umem_instance
 */
struct umem_instance *utest_utx2umm(struct utest_context *utx);

/** Get the umem_attr for a context
 *
 *  \param	utx[IN]		The context
 *
 *  \return The umem_attr
 */
struct umem_attr *utest_utx2uma(struct utest_context *utx);

/** Helper macro to convert an offset to an direct pointer
 *  \param	utx[IN]		The context
 *  \param	offset[IN]	The offset from start of pool
 *
 *  \return The pointer to the memory
 */
#define utest_off2ptr(utx, offset)					\
	umem_off2ptr(utest_utx2umm(utx), offset)


/** Start a transaction, if applicable
 *  \param	utx[IN]		The context
 *
 *  \return 0 on success
 */
int utest_tx_begin(struct utest_context *utx);

/** Commit or abort and finish a transaction, if applicable
 *  \param	utx[IN]		The context
 *
 *  \return 0 on success
 */
int utest_tx_end(struct utest_context *utx, int rc);


/** Get the SCM used space information.
 *  \param	utx[IN]		utest_context
 *	\param	used_space[OUT]		SCM current allocated space
 *
 *  \return 0 on success
 */
int utest_get_scm_used_space(struct utest_context *utx,
		daos_size_t *used_space);

/** Sync the SCM usage memory status
 *  \param	utx[IN]	utest_context
 *
 *  \return 0 on success
 */
int utest_sync_mem_status(struct utest_context *utx);

/** Check whether SCM usage decrease
 *  \param	utx[IN]	utest_context
 *
 *  \return 0 on success
 */
int utest_check_mem_decrease(struct utest_context *utx);

/** Check whether SCM usage increase
 *  \param	utx[IN]	utest_context
 *
 *  \return 0 on success
 */
int utest_check_mem_increase(struct utest_context *utx);

/** Check initial SCM usage with current value
 *  \param	utx[IN]	utest_context
 *
 *  \return 0 on success
 */
int utest_check_mem_initial_status(struct utest_context *utx);

