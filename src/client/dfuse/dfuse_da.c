/**
 * (C) Copyright 2017-2019 Intel Corporation.
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

#include "dfuse_log.h"
#include <gurt/common.h>

#include "dfuse_da.h"

static void
debug_dump(struct dfuse_da_type *type)
{
	DFUSE_TRA_INFO(type, "DescAlloc type %p '%s'", type, type->reg.name);
	DFUSE_TRA_DEBUG(type, "size %d offset %d",
			type->reg.size, type->reg.offset);
	DFUSE_TRA_DEBUG(type, "Count: free %d pending %d total %d",
			type->free_count, type->pending_count, type->count);
	DFUSE_TRA_DEBUG(type, "Calls: init %d reset %d release %d",
			type->init_count, type->reset_count,
			type->release_count);
	DFUSE_TRA_DEBUG(type, "OP: init %d reset %d", type->op_init,
			type->op_reset);
	DFUSE_TRA_DEBUG(type, "No restock: current %d hwm %d", type->no_restock,
			type->no_restock_hwm);
}

/* Create an object da */
int
dfuse_da_init(struct dfuse_da *da, void *arg)
{
	int rc;

	D_INIT_LIST_HEAD(&da->list);

	rc = D_MUTEX_INIT(&da->lock, NULL);
	if (rc != -DER_SUCCESS)
		return rc;

	DFUSE_TRA_UP(da, arg, "dfuse_da");
	DFUSE_TRA_DEBUG(da, "Creating a da");

	da->init = true;
	da->arg = arg;
	return -DER_SUCCESS;
}

/* Destroy an object da */
void
dfuse_da_destroy(struct dfuse_da *da)
{
	struct dfuse_da_type *type;
	int rc;
	bool in_use;

	if (!da->init)
		return;

	d_list_for_each_entry(type, &da->list, type_list) {
		debug_dump(type);
	}

	in_use = dfuse_da_reclaim(da);
	if (in_use)
		DFUSE_TRA_WARNING(da, "Allocator has active objects");

	while ((type = d_list_pop_entry(&da->list,
					struct dfuse_da_type,
					type_list))) {
		if (type->count != 0)
			DFUSE_TRA_WARNING(type,
					  "Freeing type with active objects");
		rc = pthread_mutex_destroy(&type->lock);
		if (rc != 0)
			DFUSE_TRA_ERROR(type,
					"Failed to destroy lock %d %s",
					rc, strerror(rc));
		DFUSE_TRA_DOWN(type);
		D_FREE(type);
	}
	rc = pthread_mutex_destroy(&da->lock);
	if (rc != 0)
		DFUSE_TRA_ERROR(da,
				"Failed to destroy lock %d %s",
				rc, strerror(rc));
	DFUSE_TRA_DOWN(da);
}

/* Helper function for migrating objects from pending list to free list.
 *
 * Migrates objects from the pending list to the free list.  Keeps going
 * until either there are count objects on the free list or there are
 * no more pending objects;
 * This function should be called with the type lock held.
 */
static int
restock(struct dfuse_da_type *type, int count)
{
	d_list_t *entry, *enext;
	int reset_calls = 0;

	if (type->free_count >= count)
		return 0;

	if (type->reg.max_free_desc != 0 &&
	    type->free_count >= type->reg.max_free_desc) {
		DFUSE_TRA_DEBUG(type, "free_count %d, max_free_desc %d, "
				"cannot append.",
				type->free_count, type->reg.max_free_desc);
		return 0;
	}

	d_list_for_each_safe(entry, enext, &type->pending_list) {
		void *ptr = (void *)entry - type->reg.offset;
		bool rcb = true;

		DFUSE_TRA_DEBUG(type, "Resetting %p", ptr);

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
			DFUSE_TRA_INFO(ptr, "entry %p failed reset", ptr);
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
dfuse_da_reclaim(struct dfuse_da *da)
{
	struct dfuse_da_type *type;
	int active_descriptors = false;

	D_MUTEX_LOCK(&da->lock);
	d_list_for_each_entry(type, &da->list, type_list) {
		d_list_t *entry, *enext;

		DFUSE_TRA_DEBUG(type, "Resetting type");

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
		DFUSE_TRA_DEBUG(type, "%d in use", type->count);
		if (type->count) {
			DFUSE_TRA_INFO(type,
				       "Active descriptors (%d) of type '%s'",
				       type->count,
				       type->reg.name);
			active_descriptors = true;
		}
		D_MUTEX_UNLOCK(&type->lock);
	}
	D_MUTEX_UNLOCK(&da->lock);
	return active_descriptors;
}

/* Create a single new object
 *
 * Returns a pointer to the object or NULL if allocation fails.
 */
static void *
create(struct dfuse_da_type *type)
{
	void *ptr;

	D_ALLOC(ptr, type->reg.size);
	if (!ptr)
		return NULL;

	type->init_count++;
	if (type->reg.init)
		type->reg.init(ptr, type->da->arg);

	if (type->reg.reset) {
		if (!type->reg.reset(ptr)) {
			DFUSE_TRA_INFO(type, "entry %p failed reset", ptr);
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
create_many(struct dfuse_da_type *type)
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

/* Register a da type */
struct dfuse_da_type *
dfuse_da_register(struct dfuse_da *da, struct dfuse_da_reg *reg)
{
	struct dfuse_da_type *type;
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

	DFUSE_TRA_UP(type, da, reg->name);

	D_INIT_LIST_HEAD(&type->free_list);
	D_INIT_LIST_HEAD(&type->pending_list);
	type->da = da;

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
		DFUSE_TRA_DOWN(type);
		D_MUTEX_DESTROY(&type->lock);
		D_FREE(type);
		return NULL;
	}

	D_MUTEX_LOCK(&da->lock);
	d_list_add_tail(&type->type_list, &da->list);
	D_MUTEX_UNLOCK(&da->lock);

	return type;
}

/* Acquire a new object.
 *
 * This is to be considered on the critical path so should be as lightweight
 * as posslble.
 */
void *
dfuse_da_acquire(struct dfuse_da_type *type)
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
		DFUSE_TRA_DEBUG(type, "Using %p", ptr);
	else if (at_limit)
		DFUSE_TRA_INFO(type, "Descriptor limit hit");
	else
		DFUSE_TRA_WARNING(type, "Failed to allocate for type");
	return ptr;
}

/* Release an object ready for reuse
 *
 * This is sometimes on the critical path, sometimes not so assume that
 * for all cases it is.
 *
 */
void
dfuse_da_release(struct dfuse_da_type *type, void *ptr)
{
	d_list_t *entry = ptr + type->reg.offset;

	DFUSE_TRA_DOWN(ptr);
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
dfuse_da_restock(struct dfuse_da_type *type)
{
	DFUSE_TRA_DEBUG(type, "Count (%d/%d/%d)", type->pending_count,
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
