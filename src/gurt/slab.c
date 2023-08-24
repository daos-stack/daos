/**
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdlib.h>
#include <string.h>

#include <gurt/debug.h>
#include <gurt/common.h>

#include "gurt/slab.h"

static void
debug_dump(struct d_slab_type *type)
{
	D_TRACE_INFO(type, "DescAlloc type %p '%s'\n", type, type->st_reg.sr_name);
	D_TRACE_DEBUG(DB_ANY, type, "size %d offset %d", type->st_reg.sr_size,
		      type->st_reg.sr_offset);
	D_TRACE_DEBUG(DB_ANY, type, "Count: free %d pending %d total %d", type->st_free_count,
		      type->st_pending_count, type->st_count);
	D_TRACE_DEBUG(DB_ANY, type, "Calls: init %d reset %d release %d", type->st_init_count,
		      type->st_reset_count, type->st_release_count);
	D_TRACE_DEBUG(DB_ANY, type, "OP: init %d reset %d", type->st_op_init, type->st_op_reset);
	D_TRACE_DEBUG(DB_ANY, type, "No restock: current %d hwm %d", type->st_no_restock,
		      type->st_no_restock_hwm);
}

/* Create a data slab manager */
int
d_slab_init(struct d_slab *slab, void *arg)
{
	int rc;

	D_INIT_LIST_HEAD(&slab->slab_list);

	rc = D_MUTEX_INIT(&slab->slab_lock, NULL);
	if (rc != -DER_SUCCESS)
		return rc;

	D_TRACE_UP(DB_ANY, slab, arg, "slab");
	D_TRACE_DEBUG(DB_ANY, slab, "Creating a data slab manager");

	slab->slab_init = true;
	return -DER_SUCCESS;
}

/* Destroy a data slab manager */
void
d_slab_destroy(struct d_slab *slab)
{
	struct d_slab_type *type;
	int                 rc;
	bool                in_use;

	if (!slab->slab_init)
		return;

	d_list_for_each_entry(type, &slab->slab_list, st_type_list) {
		debug_dump(type);
	}

	in_use = d_slab_reclaim(slab);
	if (in_use)
		D_TRACE_WARN(slab, "Allocator has active objects\n");

	while ((type = d_list_pop_entry(&slab->slab_list, struct d_slab_type, st_type_list))) {
		if (type->st_count != 0)
			D_TRACE_WARN(type, "Freeing type with active objects\n");
		rc = pthread_mutex_destroy(&type->st_lock);
		if (rc != 0)
			D_TRACE_ERROR(type, "Failed to destroy lock %d %s\n", rc, strerror(rc));
		D_FREE(type);
	}
	rc = pthread_mutex_destroy(&slab->slab_lock);
	if (rc != 0)
		D_TRACE_ERROR(slab, "Failed to destroy lock %d %s\n", rc, strerror(rc));
	D_TRACE_DOWN(DB_ANY, slab);
}

/* Helper function for migrating objects from pending list to free list.
 *
 * Migrates objects from the pending list to the free list.  Keeps going
 * until either there are count objects on the free list or there are
 * no more pending objects;
 * This function should be called with the type lock held.
 */
static int
restock(struct d_slab_type *type, int count)
{
	d_list_t *entry, *enext;
	int       reset_calls = 0;

	if (type->st_free_count >= count)
		return 0;

	if (type->st_reg.sr_max_free_desc != 0 &&
	    type->st_free_count >= type->st_reg.sr_max_free_desc) {
		D_TRACE_DEBUG(DB_ANY, type, "free_count %d, max_free_desc %d, cannot append.",
			      type->st_free_count, type->st_reg.sr_max_free_desc);
		return 0;
	}

	d_list_for_each_safe(entry, enext, &type->st_pending_list) {
		void *ptr = (void *)entry - type->st_reg.sr_offset;
		bool  rcb = true;

		D_TRACE_DEBUG(DB_ANY, type, "Resetting %p", ptr);

		d_list_del(entry);
		type->st_pending_count--;

		if (type->st_reg.sr_reset) {
			type->st_reset_count++;
			reset_calls++;
			rcb = type->st_reg.sr_reset(ptr);
		}
		if (rcb) {
			d_list_add(entry, &type->st_free_list);
			type->st_free_count++;
		} else {
			D_TRACE_INFO(ptr, "entry %p failed reset\n", ptr);
			type->st_count--;
			D_FREE(ptr);
		}

		if (type->st_free_count == count)
			return reset_calls;

		if (type->st_reg.sr_max_free_desc != 0 &&
		    type->st_free_count >= type->st_reg.sr_max_free_desc)
			return reset_calls;
	}
	return reset_calls;
}

/* Reclaim any memory possible across all types
 *
 * Returns true of there are any descriptors in use.
 */

bool
d_slab_reclaim(struct d_slab *slab)
{
	struct d_slab_type *type;
	int                 active_descriptors = false;

	D_MUTEX_LOCK(&slab->slab_lock);
	d_list_for_each_entry(type, &slab->slab_list, st_type_list) {
		d_list_t *entry, *enext;

		D_TRACE_DEBUG(DB_ANY, type, "Resetting type");

		D_MUTEX_LOCK(&type->st_lock);

		/* Reclaim any pending objects.  Count here just needs to be
		 * larger than pending_count + free_count however simply
		 * using count is adequate as is guaranteed to be larger.
		 */
		restock(type, type->st_count);

		d_list_for_each_safe(entry, enext, &type->st_free_list) {
			void *ptr = (void *)entry - type->st_reg.sr_offset;

			if (type->st_reg.sr_release) {
				type->st_reg.sr_release(ptr);
				type->st_release_count++;
			}

			d_list_del(entry);
			D_FREE(ptr);
			type->st_free_count--;
			type->st_count--;
		}
		D_TRACE_DEBUG(DB_ANY, type, "%d in use", type->st_count);
		if (type->st_count) {
			D_TRACE_INFO(type, "Active descriptors (%d) of type '%s'\n", type->st_count,
				     type->st_reg.sr_name);
			active_descriptors = true;
		}
		D_MUTEX_UNLOCK(&type->st_lock);
	}
	D_MUTEX_UNLOCK(&slab->slab_lock);
	return active_descriptors;
}

/* Create a single new object
 *
 * Returns a pointer to the object or NULL if allocation fails.
 */
static void *
create(struct d_slab_type *type)
{
	void *ptr;

	D_ALLOC(ptr, type->st_reg.sr_size);
	if (!ptr)
		return NULL;

	type->st_init_count++;
	if (type->st_reg.sr_init)
		type->st_reg.sr_init(ptr, type->st_arg);

	if (type->st_reg.sr_reset) {
		if (!type->st_reg.sr_reset(ptr)) {
			D_TRACE_INFO(type, "entry %p failed reset\n", ptr);
			D_FREE(ptr);
			return NULL;
		}
	}
	type->st_count++;

	return ptr;
}

/* Populate the free list
 *
 * Create objects and add them to the free list.  Creates one more object
 * than is needed to ensure that if the HWM of no-restock calls is reached
 * there will be no on-path allocations.
 */
static void
create_many(struct d_slab_type *type)
{
	while (type->st_free_count < (type->st_no_restock_hwm + 1)) {
		void     *ptr;
		d_list_t *entry;

		if (type->st_reg.sr_max_free_desc != 0 &&
		    type->st_free_count >= type->st_reg.sr_max_free_desc)
			break;

		ptr   = create(type);
		entry = ptr + type->st_reg.sr_offset;

		if (!ptr)
			return;

		d_list_add_tail(entry, &type->st_free_list);
		type->st_free_count++;
	}
}

/* Register a data type */
int
d_slab_register(struct d_slab *slab, struct d_slab_reg *reg, void *arg, struct d_slab_type **_type)
{
	struct d_slab_type *type;
	int                 rc;

	if (!reg->sr_name)
		return -DER_INVAL;

	D_ALLOC_PTR(type);
	if (!type)
		return -DER_NOMEM;

	rc = D_MUTEX_INIT(&type->st_lock, NULL);
	if (rc != -DER_SUCCESS) {
		D_FREE(type);
		return rc;
	}

	D_TRACE_UP(DB_ANY, type, slab, reg->sr_name);

	D_INIT_LIST_HEAD(&type->st_free_list);
	D_INIT_LIST_HEAD(&type->st_pending_list);
	type->st_slab = slab;

	type->st_count = 0;
	type->st_reg   = *reg;
	type->st_arg   = arg;

	create_many(type);
	create_many(type);

	if (type->st_free_count == 0) {
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
		D_MUTEX_DESTROY(&type->st_lock);
		D_FREE(type);
		return -DER_INVAL;
	}

	D_MUTEX_LOCK(&slab->slab_lock);
	d_list_add_tail(&type->st_type_list, &slab->slab_list);
	D_MUTEX_UNLOCK(&slab->slab_lock);

	*_type = type;
	return -DER_SUCCESS;
}

/* Acquire a new object.
 *
 * This is to be considered on the critical path so should be as lightweight
 * as posslble.
 */
void *
d_slab_acquire(struct d_slab_type *type)
{
	void     *ptr = NULL;
	d_list_t *entry;
	bool      at_limit = false;

	D_MUTEX_LOCK(&type->st_lock);

	type->st_no_restock++;

	if (type->st_free_count == 0) {
		int count = restock(type, 1);

		type->st_op_reset += count;
	}

	if (!d_list_empty(&type->st_free_list)) {
		entry = type->st_free_list.next;
		d_list_del(entry);
		entry->next = NULL;
		entry->prev = NULL;
		type->st_free_count--;
		ptr = (void *)entry - type->st_reg.sr_offset;
	} else {
		if (!type->st_reg.sr_max_desc || type->st_count < type->st_reg.sr_max_desc) {
			type->st_op_init++;
			ptr = create(type);
		} else {
			at_limit = true;
		}
	}

	D_MUTEX_UNLOCK(&type->st_lock);

	if (ptr)
		D_TRACE_DEBUG(DB_ANY, type, "Using %p", ptr);
	else if (at_limit)
		D_TRACE_INFO(type, "Descriptor limit hit\n");
	else
		D_TRACE_WARN(type, "Failed to allocate for type\n");
	return ptr;
}

/* Release an object ready for reuse
 *
 * This is sometimes on the critical path, sometimes not so assume that
 * for all cases it is.
 *
 */
void
d_slab_release(struct d_slab_type *type, void *ptr)
{
	d_list_t *entry = ptr + type->st_reg.sr_offset;

	D_MUTEX_LOCK(&type->st_lock);
	type->st_pending_count++;
	d_list_add_tail(entry, &type->st_pending_list);
	D_MUTEX_UNLOCK(&type->st_lock);
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
d_slab_restock(struct d_slab_type *type)
{
	D_TRACE_DEBUG(DB_ANY, type, "Count (%d/%d/%d)", type->st_pending_count, type->st_free_count,
		      type->st_count);

	D_MUTEX_LOCK(&type->st_lock);

	/* Update restock hwm metrics */
	if (type->st_no_restock > type->st_no_restock_hwm)
		type->st_no_restock_hwm = type->st_no_restock;
	type->st_no_restock = 0;

	/* Move from pending to free list */
	restock(type, type->st_no_restock_hwm + 1);

	if (!type->st_reg.sr_max_desc)
		create_many(type);

	D_MUTEX_UNLOCK(&type->st_lock);
}
