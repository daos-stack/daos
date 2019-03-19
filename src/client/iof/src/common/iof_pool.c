/* Copyright (C) 2017-2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * A simple, efficient pool for allocating objects of equal size
 */

#include <stdlib.h>
#include <string.h>

#include <gurt/common.h>

#include "iof_pool.h"
#include "log.h"

static void
debug_dump(struct iof_pool_type *type)
{
	IOF_TRACE_INFO(type, "Pool type %p '%s'", type, type->reg.name);
	IOF_TRACE_DEBUG(type, "size %d offset %d",
			type->reg.size, type->reg.offset);
	IOF_TRACE_DEBUG(type, "Count: free %d pending %d total %d",
			type->free_count, type->pending_count, type->count);
	IOF_TRACE_DEBUG(type, "Calls: init %d reset %d release %d",
			type->init_count, type->reset_count,
			type->release_count);
	IOF_TRACE_DEBUG(type, "OP: init %d reset %d", type->op_init,
			type->op_reset);
	IOF_TRACE_DEBUG(type, "No restock: current %d hwm %d", type->no_restock,
			type->no_restock_hwm);
}

/* Create an object pool */
int
iof_pool_init(struct iof_pool *pool, void *arg)
{
	int rc;

	D_INIT_LIST_HEAD(&pool->list);

	rc = D_MUTEX_INIT(&pool->lock, NULL);
	if (rc != -DER_SUCCESS)
		return rc;

	IOF_TRACE_UP(pool, arg, "iof_pool");
	IOF_TRACE_DEBUG(pool, "Creating a pool");

	pool->init = true;
	pool->arg = arg;
	return -DER_SUCCESS;
}

/* Destroy an object pool */
void
iof_pool_destroy(struct iof_pool *pool)
{
	struct iof_pool_type *type;
	int rc;
	bool in_use;

	if (!pool->init)
		return;

	d_list_for_each_entry(type, &pool->list, type_list) {
		debug_dump(type);
	}

	in_use = iof_pool_reclaim(pool);
	if (in_use)
		IOF_TRACE_WARNING(pool, "Pool has active objects");

	while ((type = d_list_pop_entry(&pool->list,
					struct iof_pool_type,
					type_list))) {
		if (type->count != 0)
			IOF_TRACE_WARNING(type,
					  "Freeing type with active objects");
		rc = pthread_mutex_destroy(&type->lock);
		if (rc != 0)
			IOF_TRACE_ERROR(type,
					"Failed to destroy lock %d %s",
					rc, strerror(rc));
		IOF_TRACE_DOWN(type);
		D_FREE(type);
	}
	rc = pthread_mutex_destroy(&pool->lock);
	if (rc != 0)
		IOF_TRACE_ERROR(pool,
				"Failed to destroy lock %d %s",
				rc, strerror(rc));
	IOF_TRACE_DOWN(pool);
}

/* Helper function for migrating objects from pending list to free list.
 *
 * Migrates objects from the pending list to the free list.  Keeps going
 * until either there are count objects on the free list or there are
 * no more pending objects;
 * This function should be called with the type lock held.
 */
static int
restock(struct iof_pool_type *type, int count)
{
	d_list_t *entry, *enext;
	int reset_calls = 0;

	if (type->free_count >= count)
		return 0;

	if (type->reg.max_free_desc != 0 &&
	    type->free_count >= type->reg.max_free_desc) {
		IOF_TRACE_DEBUG(type, "free_count %d, max_free_desc %d, "
				"cannot append.",
				type->free_count, type->reg.max_free_desc);
		return 0;
	}

	d_list_for_each_safe(entry, enext, &type->pending_list) {
		void *ptr = (void *)entry - type->reg.offset;
		bool rcb = true;

		IOF_TRACE_DEBUG(type, "Resetting %p", ptr);

		d_list_del(entry);
		type->pending_count--;

		if (type->reg.reset) {
			type->reset_count++;
			reset_calls++;
			rcb = type->reg.reset(ptr);
		}
		if (rcb) {
			d_list_add(entry, &type->free_list);
			type->free_count++;
		} else {
			IOF_TRACE_INFO(ptr, "entry %p failed reset", ptr);
			type->count--;
			D_FREE(ptr);
		}

		if (type->free_count == count)
			return reset_calls;

		if (type->reg.max_free_desc != 0 &&
		    type->free_count >= type->reg.max_free_desc)
			return reset_calls;
	}
	return reset_calls;
}

/* Reclaim any memory possible across all types
 *
 * Returns true of there are any descriptors in use.
 */

bool
iof_pool_reclaim(struct iof_pool *pool)
{
	struct iof_pool_type *type;
	int active_descriptors = false;

	D_MUTEX_LOCK(&pool->lock);
	d_list_for_each_entry(type, &pool->list, type_list) {
		d_list_t *entry, *enext;

		IOF_TRACE_DEBUG(type, "Resetting type");

		D_MUTEX_LOCK(&type->lock);

		/* Reclaim any pending objects.  Count here just needs to be
		 * larger than pending_count + free_count however simply
		 * using count is adequate as is guaranteed to be larger.
		 */
		restock(type, type->count);

		d_list_for_each_safe(entry, enext, &type->free_list) {
			void *ptr = (void *)entry - type->reg.offset;

			if (type->reg.release) {
				type->reg.release(ptr);
				type->release_count++;
			}

			d_list_del(entry);
			D_FREE(ptr);
			type->free_count--;
			type->count--;
		}
		IOF_TRACE_DEBUG(type, "%d in use", type->count);
		if (type->count) {
			IOF_TRACE_INFO(type,
				       "Active descriptors (%d) of type '%s'",
				       type->count,
				       type->reg.name);
			active_descriptors = true;
		}
		D_MUTEX_UNLOCK(&type->lock);
	}
	D_MUTEX_UNLOCK(&pool->lock);
	return active_descriptors;
}

/* Create a single new object
 *
 * Returns a pointer to the object or NULL if allocation fails.
 */
static void *
create(struct iof_pool_type *type)
{
	void *ptr;

	D_ALLOC(ptr, type->reg.size);
	if (!ptr)
		return NULL;

	type->init_count++;
	if (type->reg.init)
		type->reg.init(ptr, type->pool->arg);

	if (type->reg.reset) {
		if (!type->reg.reset(ptr)) {
			IOF_TRACE_INFO(type, "entry %p failed reset", ptr);
			D_FREE(ptr);
			return NULL;
		}
	}
	type->count++;

	return ptr;
}

/* Populate the free list
 *
 * Create objects and add them to the free list.  Creates one more object
 * than is needed to ensure that if the HWM of no-restock calls is reached
 * there will be no on-path allocations.
 */
static void
create_many(struct iof_pool_type *type)
{
	while (type->free_count < (type->no_restock_hwm + 1)) {
		void *ptr;
		d_list_t *entry;

		if (type->reg.max_free_desc != 0 &&
		    type->free_count >= type->reg.max_free_desc)
			break;

		ptr = create(type);
		entry = ptr + type->reg.offset;

		if (!ptr)
			return;

		d_list_add_tail(entry, &type->free_list);
		type->free_count++;
	}
}

/* Register a pool type */
struct iof_pool_type *
iof_pool_register(struct iof_pool *pool, struct iof_pool_reg *reg)
{
	struct iof_pool_type *type;
	int rc;

	if (!reg->name)
		return NULL;

	D_ALLOC_PTR(type);
	if (!type)
		return NULL;

	rc = D_MUTEX_INIT(&type->lock, NULL);
	if (rc != -DER_SUCCESS) {
		D_FREE(type);
		return NULL;
	}

	IOF_TRACE_UP(type, pool, reg->name);

	D_INIT_LIST_HEAD(&type->free_list);
	D_INIT_LIST_HEAD(&type->pending_list);
	type->pool = pool;

	type->count = 0;
	type->reg = *reg;

	create_many(type);

	if (type->free_count == 0) {
		/* If create_many() failed to create any descriptors then
		 * return failure, as it either means an early allocation
		 * failure or a wider problem with the type itself.
		 *
		 * This works with the fault injection tests as precisely
		 * one descriptor is created initially, if there were more
		 * and one failed the error would not propagate and the
		 * injected fault would be ignored - failing the specific
		 * test.
		 */
		IOF_TRACE_DOWN(type);
		D_MUTEX_DESTROY(&type->lock);
		D_FREE(type);
		return NULL;
	}

	D_MUTEX_LOCK(&pool->lock);
	d_list_add_tail(&type->type_list, &pool->list);
	D_MUTEX_UNLOCK(&pool->lock);

	return type;
}

/* Acquire a new object.
 *
 * This is to be considered on the critical path so should be as lightweight
 * as posslble.
 */
void *
iof_pool_acquire(struct iof_pool_type *type)
{
	void		*ptr = NULL;
	d_list_t	*entry;
	bool		at_limit = false;

	D_MUTEX_LOCK(&type->lock);

	type->no_restock++;

	if (type->free_count == 0) {
		int count = restock(type, 1);

		type->op_reset += count;
	}

	if (!d_list_empty(&type->free_list)) {
		entry = type->free_list.next;
		d_list_del(entry);
		entry->next = NULL;
		entry->prev = NULL;
		type->free_count--;
		ptr = (void *)entry - type->reg.offset;
	} else {
		if (!type->reg.max_desc || type->count < type->reg.max_desc) {
			type->op_init++;
			ptr = create(type);
		} else {
			at_limit = true;
		}
	}

	D_MUTEX_UNLOCK(&type->lock);

	if (ptr)
		IOF_TRACE_DEBUG(type, "Using %p", ptr);
	else if (at_limit)
		IOF_TRACE_INFO(type, "Descriptor limit hit");
	else
		IOF_TRACE_WARNING(type, "Failed to allocate for type");
	return ptr;
}

/* Release an object ready for reuse
 *
 * This is sometimes on the critical path, sometimes not so assume that
 * for all cases it is.
 *
 */
void
iof_pool_release(struct iof_pool_type *type, void *ptr)
{
	d_list_t *entry = ptr + type->reg.offset;

	IOF_TRACE_DOWN(ptr);
	D_MUTEX_LOCK(&type->lock);
	type->pending_count++;
	d_list_add_tail(entry, &type->pending_list);
	D_MUTEX_UNLOCK(&type->lock);
}

/* Re-stock an object type.
 *
 * This is a function called off the critical path to pre-alloc and recycle
 * objects to be ready for re-use.  In an ideal world this function does
 * all the heavy lifting and acquire/release are very cheap.
 *
 * Ideally this function should be called once for every acquire(), after the
 * object has been used however correctness is maintained even if that is not
 * the case.
 */

void
iof_pool_restock(struct iof_pool_type *type)
{
	IOF_TRACE_DEBUG(type, "Count (%d/%d/%d)", type->pending_count,
			type->free_count, type->count);

	D_MUTEX_LOCK(&type->lock);

	/* Update restock hwm metrics */
	if (type->no_restock > type->no_restock_hwm)
		type->no_restock_hwm = type->no_restock;
	type->no_restock = 0;

	/* Move from pending to free list */
	restock(type, type->no_restock_hwm + 1);

	if (!type->reg.max_desc)
		create_many(type);

	D_MUTEX_UNLOCK(&type->lock);
}
