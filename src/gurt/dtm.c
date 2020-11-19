/**
 * (C) Copyright 2017-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#include <stdlib.h>
#include <string.h>

#include <gurt/debug.h>
#include <gurt/common.h>

#include "gurt/dtm.h"

static void
debug_dump(struct d_dtm_type *type)
{
	D_TRACE_INFO(type, "DescAlloc type %p '%s'", type,
		     type->dt_reg.dr_name);
	D_TRACE_DEBUG(DB_ANY, type, "size %d offset %d", type->dt_reg.dr_size,
		      type->dt_reg.dr_offset);
	D_TRACE_DEBUG(DB_ANY, type, "Count: free %d pending %d total %d",
		      type->dt_free_count, type->dt_pending_count,
		      type->dt_count);
	D_TRACE_DEBUG(DB_ANY, type, "Calls: init %d reset %d release %d",
		      type->dt_init_count, type->dt_reset_count,
		      type->dt_release_count);
	D_TRACE_DEBUG(DB_ANY, type, "OP: init %d reset %d", type->dt_op_init,
		      type->dt_op_reset);
	D_TRACE_DEBUG(DB_ANY, type, "No restock: current %d hwm %d",
		      type->dt_no_restock, type->dt_no_restock_hwm);
}

/* Create a data type manager */
int
d_dtm_init(struct d_dtm *dtm, void *arg)
{
	int rc;

	D_INIT_LIST_HEAD(&dtm->dtm_list);

	rc = D_MUTEX_INIT(&dtm->dtm_lock, NULL);
	if (rc != -DER_SUCCESS)
		return rc;

	D_TRACE_UP(DB_ANY, dtm, arg, "dtm");
	D_TRACE_DEBUG(DB_ANY, dtm, "Creating a data type manager");

	dtm->dtm_init = true;
	dtm->dtm_arg = arg;
	return -DER_SUCCESS;
}

/* Destroy a data type manager */
void
d_dtm_destroy(struct d_dtm *dtm)
{
	struct d_dtm_type *type;
	int rc;
	bool in_use;

	if (!dtm->dtm_init)
		return;

	d_list_for_each_entry(type, &dtm->dtm_list, dt_type_list) {
		debug_dump(type);
	}

	in_use = d_dtm_reclaim(dtm);
	if (in_use)
		D_TRACE_WARN(dtm, "Allocator has active objects");

	while ((type = d_list_pop_entry(&dtm->dtm_list,
					struct d_dtm_type,
					dt_type_list))) {
		if (type->dt_count != 0)
			D_TRACE_WARN(type, "Freeing type with active objects");
		rc = pthread_mutex_destroy(&type->dt_lock);
		if (rc != 0)
			D_TRACE_ERROR(type, "Failed to destroy lock %d %s",
				      rc, strerror(rc));
		D_FREE(type);
	}
	rc = pthread_mutex_destroy(&dtm->dtm_lock);
	if (rc != 0)
		D_TRACE_ERROR(dtm, "Failed to destroy lock %d %s",
			      rc, strerror(rc));
	D_TRACE_DOWN(DB_ANY, dtm);
}

/* Helper function for migrating objects from pending list to free list.
 *
 * Migrates objects from the pending list to the free list.  Keeps going
 * until either there are count objects on the free list or there are
 * no more pending objects;
 * This function should be called with the type lock held.
 */
static int
restock(struct d_dtm_type *type, int count)
{
	d_list_t *entry, *enext;
	int reset_calls = 0;

	if (type->dt_free_count >= count)
		return 0;

	if (type->dt_reg.dr_max_free_desc != 0 &&
	    type->dt_free_count >= type->dt_reg.dr_max_free_desc) {
		D_TRACE_DEBUG(DB_ANY, type, "free_count %d, max_free_desc %d, "
			      "cannot append.", type->dt_free_count,
			      type->dt_reg.dr_max_free_desc);
		return 0;
	}

	d_list_for_each_safe(entry, enext, &type->dt_pending_list) {
		void *ptr = (void *)entry - type->dt_reg.dr_offset;
		bool rcb = true;

		D_TRACE_DEBUG(DB_ANY, type, "Resetting %p", ptr);

		d_list_del(entry);
		type->dt_pending_count--;

		if (type->dt_reg.dr_reset) {
			type->dt_reset_count++;
			reset_calls++;
			rcb = type->dt_reg.dr_reset(ptr);
		}
		if (rcb) {
			d_list_add(entry, &type->dt_free_list);
			type->dt_free_count++;
		} else {
			D_TRACE_INFO(ptr, "entry %p failed reset", ptr);
			type->dt_count--;
			D_FREE(ptr);
		}

		if (type->dt_free_count == count)
			return reset_calls;

		if (type->dt_reg.dr_max_free_desc != 0 &&
		    type->dt_free_count >= type->dt_reg.dr_max_free_desc)
			return reset_calls;
	}
	return reset_calls;
}

/* Reclaim any memory possible across all types
 *
 * Returns true of there are any descriptors in use.
 */

bool
d_dtm_reclaim(struct d_dtm *dtm)
{
	struct d_dtm_type *type;
	int active_descriptors = false;

	D_MUTEX_LOCK(&dtm->dtm_lock);
	d_list_for_each_entry(type, &dtm->dtm_list, dt_type_list) {
		d_list_t *entry, *enext;

		D_TRACE_DEBUG(DB_ANY, type, "Resetting type");

		D_MUTEX_LOCK(&type->dt_lock);

		/* Reclaim any pending objects.  Count here just needs to be
		 * larger than pending_count + free_count however simply
		 * using count is adequate as is guaranteed to be larger.
		 */
		restock(type, type->dt_count);

		d_list_for_each_safe(entry, enext, &type->dt_free_list) {
			void *ptr = (void *)entry - type->dt_reg.dr_offset;

			if (type->dt_reg.dr_release) {
				type->dt_reg.dr_release(ptr);
				type->dt_release_count++;
			}

			d_list_del(entry);
			D_FREE(ptr);
			type->dt_free_count--;
			type->dt_count--;
		}
		D_TRACE_DEBUG(DB_ANY, type, "%d in use", type->dt_count);
		if (type->dt_count) {
			D_TRACE_INFO(type,
				     "Active descriptors (%d) of type '%s'",
				     type->dt_count, type->dt_reg.dr_name);
			active_descriptors = true;
		}
		D_MUTEX_UNLOCK(&type->dt_lock);
	}
	D_MUTEX_UNLOCK(&dtm->dtm_lock);
	return active_descriptors;
}

/* Create a single new object
 *
 * Returns a pointer to the object or NULL if allocation fails.
 */
static void *
create(struct d_dtm_type *type)
{
	void *ptr;

	D_ALLOC(ptr, type->dt_reg.dr_size);
	if (!ptr)
		return NULL;

	type->dt_init_count++;
	if (type->dt_reg.dr_init)
		type->dt_reg.dr_init(ptr, type->dt_dtm->dtm_arg);

	if (type->dt_reg.dr_reset) {
		if (!type->dt_reg.dr_reset(ptr)) {
			D_TRACE_INFO(type, "entry %p failed reset", ptr);
			D_FREE(ptr);
			return NULL;
		}
	}
	type->dt_count++;

	return ptr;
}

/* Populate the free list
 *
 * Create objects and add them to the free list.  Creates one more object
 * than is needed to ensure that if the HWM of no-restock calls is reached
 * there will be no on-path allocations.
 */
static void
create_many(struct d_dtm_type *type)
{
	while (type->dt_free_count < (type->dt_no_restock_hwm + 1)) {
		void *ptr;
		d_list_t *entry;

		if (type->dt_reg.dr_max_free_desc != 0 &&
		    type->dt_free_count >= type->dt_reg.dr_max_free_desc)
			break;

		ptr = create(type);
		entry = ptr + type->dt_reg.dr_offset;

		if (!ptr)
			return;

		d_list_add_tail(entry, &type->dt_free_list);
		type->dt_free_count++;
	}
}

/* Register a data type */
struct d_dtm_type *
d_dtm_register(struct d_dtm *dtm, struct d_dtm_reg *reg)
{
	struct d_dtm_type *type;
	int rc;

	if (!reg->dr_name)
		return NULL;

	D_ALLOC_PTR(type);
	if (!type)
		return NULL;

	rc = D_MUTEX_INIT(&type->dt_lock, NULL);
	if (rc != -DER_SUCCESS) {
		D_FREE(type);
		return NULL;
	}

	D_TRACE_UP(DB_ANY, type, dtm, reg->dr_name);

	D_INIT_LIST_HEAD(&type->dt_free_list);
	D_INIT_LIST_HEAD(&type->dt_pending_list);
	type->dt_dtm = dtm;

	type->dt_count = 0;
	type->dt_reg = *reg;

	create_many(type);

	if (type->dt_free_count == 0) {
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
		D_MUTEX_DESTROY(&type->dt_lock);
		D_FREE(type);
		return NULL;
	}

	D_MUTEX_LOCK(&dtm->dtm_lock);
	d_list_add_tail(&type->dt_type_list, &dtm->dtm_list);
	D_MUTEX_UNLOCK(&dtm->dtm_lock);

	return type;
}

/* Acquire a new object.
 *
 * This is to be considered on the critical path so should be as lightweight
 * as posslble.
 */
void *
d_dtm_acquire(struct d_dtm_type *type)
{
	void		*ptr = NULL;
	d_list_t	*entry;
	bool		at_limit = false;

	D_MUTEX_LOCK(&type->dt_lock);

	type->dt_no_restock++;

	if (type->dt_free_count == 0) {
		int count = restock(type, 1);

		type->dt_op_reset += count;
	}

	if (!d_list_empty(&type->dt_free_list)) {
		entry = type->dt_free_list.next;
		d_list_del(entry);
		entry->next = NULL;
		entry->prev = NULL;
		type->dt_free_count--;
		ptr = (void *)entry - type->dt_reg.dr_offset;
	} else {
		if (!type->dt_reg.dr_max_desc ||
		    type->dt_count < type->dt_reg.dr_max_desc) {
			type->dt_op_init++;
			ptr = create(type);
		} else {
			at_limit = true;
		}
	}

	D_MUTEX_UNLOCK(&type->dt_lock);

	if (ptr)
		D_TRACE_DEBUG(DB_ANY, type, "Using %p", ptr);
	else if (at_limit)
		D_TRACE_INFO(type, "Descriptor limit hit");
	else
		D_TRACE_WARN(type, "Failed to allocate for type");
	return ptr;
}

/* Release an object ready for reuse
 *
 * This is sometimes on the critical path, sometimes not so assume that
 * for all cases it is.
 *
 */
void
d_dtm_release(struct d_dtm_type *type, void *ptr)
{
	d_list_t *entry = ptr + type->dt_reg.dr_offset;

	D_TRACE_DOWN(DB_ANY, ptr);
	D_MUTEX_LOCK(&type->dt_lock);
	type->dt_pending_count++;
	d_list_add_tail(entry, &type->dt_pending_list);
	D_MUTEX_UNLOCK(&type->dt_lock);
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
d_dtm_restock(struct d_dtm_type *type)
{
	D_TRACE_DEBUG(DB_ANY, type, "Count (%d/%d/%d)", type->dt_pending_count,
		      type->dt_free_count, type->dt_count);

	D_MUTEX_LOCK(&type->dt_lock);

	/* Update restock hwm metrics */
	if (type->dt_no_restock > type->dt_no_restock_hwm)
		type->dt_no_restock_hwm = type->dt_no_restock;
	type->dt_no_restock = 0;

	/* Move from pending to free list */
	restock(type, type->dt_no_restock_hwm + 1);

	if (!type->dt_reg.dr_max_desc)
		create_many(type);

	D_MUTEX_UNLOCK(&type->dt_lock);
}
