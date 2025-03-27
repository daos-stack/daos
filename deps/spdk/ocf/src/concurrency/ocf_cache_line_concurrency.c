/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf_concurrency.h"
#include "../ocf_priv.h"
#include "../ocf_request.h"
#include "../utils/utils_alock.h"
#include "../utils/utils_cache_line.h"

static bool ocf_cl_lock_line_needs_lock(struct ocf_alock *alock,
		struct ocf_request *req, unsigned index)
{
	/* Remapped cachelines are assigned cacheline lock individually
	 * during eviction
	 */
	return req->map[index].status != LOOKUP_MISS &&
			req->map[index].status != LOOKUP_REMAPPED;
}

static bool ocf_cl_lock_line_is_acting(struct ocf_alock *alock,
		struct ocf_request *req, unsigned index)
{
	return req->map[index].status != LOOKUP_MISS;
}

static ocf_cache_line_t ocf_cl_lock_line_get_entry(
		struct ocf_alock *alock, struct ocf_request *req,
		unsigned index)
{
	return req->map[index].coll_idx;
}

static int ocf_cl_lock_line_fast(struct ocf_alock *alock,
		struct ocf_request *req, int rw)
{
	int32_t i;
	ocf_cache_line_t entry;
	int ret = OCF_LOCK_ACQUIRED;

	for (i = 0; i < req->core_line_count; i++) {
		if (!ocf_cl_lock_line_needs_lock(alock, req, i)) {
			/* nothing to lock */
			continue;
		}

		entry = ocf_cl_lock_line_get_entry(alock, req, i);
		ENV_BUG_ON(ocf_alock_is_index_locked(alock, req, i));

		if (rw == OCF_WRITE) {
			if (ocf_alock_trylock_entry_wr(alock, entry)) {
				/* cache entry locked */
				ocf_alock_mark_index_locked(alock, req, i, true);
			} else {
				/* Not possible to lock all cachelines */
				ret = OCF_LOCK_NOT_ACQUIRED;
				break;
			}
		} else {
			if (ocf_alock_trylock_entry_rd_idle(alock, entry)) {
				/* cache entry locked */
				ocf_alock_mark_index_locked(alock, req, i, true);
			} else {
				/* Not possible to lock all cachelines */
				ret = OCF_LOCK_NOT_ACQUIRED;
				break;
			}
		}
	}

	/* Check if request is locked */
	if (ret == OCF_LOCK_NOT_ACQUIRED) {
		/* Request is not locked, discard acquired locks */
		for (; i >= 0; i--) {
			if (!ocf_cl_lock_line_needs_lock(alock, req, i))
				continue;

			entry = ocf_cl_lock_line_get_entry(alock, req, i);

			if (ocf_alock_is_index_locked(alock, req, i)) {

				if (rw == OCF_WRITE) {
					ocf_alock_unlock_one_wr(alock, entry);
				} else {
					ocf_alock_unlock_one_rd(alock, entry);
				}
				ocf_alock_mark_index_locked(alock, req, i, false);
			}
		}
	}

	return ret;
}

static int ocf_cl_lock_line_slow(struct ocf_alock *alock,
		struct ocf_request *req, int rw, ocf_req_async_lock_cb cmpl)
{
	int32_t i;
	ocf_cache_line_t entry;
	int ret = 0;

	for (i = 0; i < req->core_line_count; i++) {

		if (!ocf_cl_lock_line_needs_lock(alock, req, i)) {
			/* nothing to lock */
			env_atomic_dec(&req->lock_remaining);
			continue;
		}

		entry = ocf_cl_lock_line_get_entry(alock, req, i);
		ENV_BUG_ON(ocf_alock_is_index_locked(alock, req, i));


		if (rw == OCF_WRITE) {
			if (!ocf_alock_lock_one_wr(alock, entry, cmpl, req, i)) {
				/* lock not acquired and not added to wait list */
				ret = -OCF_ERR_NO_MEM;
				goto err;
			}
		} else {
			if (!ocf_alock_lock_one_rd(alock, entry, cmpl, req, i)) {
				/* lock not acquired and not added to wait list */
				ret = -OCF_ERR_NO_MEM;
				goto err;
			}
		}
	}

	return ret;

err:
	for (; i >= 0; i--) {
		if (!ocf_cl_lock_line_needs_lock(alock, req, i))
			continue;

		entry = ocf_cl_lock_line_get_entry(alock, req, i);
		ocf_alock_waitlist_remove_entry(alock, req, i, entry, rw);
	}

	return ret;
}

static struct ocf_alock_lock_cbs ocf_cline_conc_cbs = {
		.lock_entries_fast = ocf_cl_lock_line_fast,
		.lock_entries_slow = ocf_cl_lock_line_slow
};

bool ocf_cache_line_try_lock_rd(struct ocf_alock *alock,
		ocf_cache_line_t line)
{
	return ocf_alock_trylock_one_rd(alock, line);
}

void ocf_cache_line_unlock_rd(struct ocf_alock *alock, ocf_cache_line_t line)
{
	ocf_alock_unlock_one_rd(alock, line);
}

bool ocf_cache_line_try_lock_wr(struct ocf_alock *alock,
		ocf_cache_line_t line)
{
	return ocf_alock_trylock_entry_wr(alock, line);
}

void ocf_cache_line_unlock_wr(struct ocf_alock *alock,
		ocf_cache_line_t line)
{
	ocf_alock_unlock_one_wr(alock, line);
}

int ocf_req_async_lock_rd(struct ocf_alock *alock,
		struct ocf_request *req, ocf_req_async_lock_cb cmpl)
{
	return ocf_alock_lock_rd(alock, req, cmpl);
}

int ocf_req_async_lock_wr(struct ocf_alock *alock,
		struct ocf_request *req, ocf_req_async_lock_cb cmpl)
{
	return ocf_alock_lock_wr(alock, req, cmpl);
}

void ocf_req_unlock_rd(struct ocf_alock *alock, struct ocf_request *req)
{
	int32_t i;
	ocf_cache_line_t entry;

	for (i = 0; i < req->core_line_count; i++) {
		if (!ocf_cl_lock_line_is_acting(alock, req, i))
			continue;

		if (!ocf_alock_is_index_locked(alock, req, i))
			continue;

		entry = ocf_cl_lock_line_get_entry(alock, req, i);

		ocf_alock_unlock_one_rd(alock, entry);
		ocf_alock_mark_index_locked(alock, req, i, false);
	}
}

void ocf_req_unlock_wr(struct ocf_alock *alock, struct ocf_request *req)
{
	int32_t i;
	ocf_cache_line_t entry;

	for (i = 0; i < req->core_line_count; i++) {
		if (!ocf_cl_lock_line_is_acting(alock, req, i))
			continue;

		if (!ocf_alock_is_index_locked(alock, req, i))
			continue;

		entry = ocf_cl_lock_line_get_entry(alock, req, i);

		ocf_alock_unlock_one_wr(alock, entry);
		ocf_alock_mark_index_locked(alock, req, i, false);
	}
}

void ocf_req_unlock(struct ocf_alock *alock, struct ocf_request *req)
{
	if (req->alock_rw == OCF_WRITE)
		ocf_req_unlock_wr(alock, req);
	else
		ocf_req_unlock_rd(alock, req);
}

bool ocf_cache_line_are_waiters(struct ocf_alock *alock,
		ocf_cache_line_t line)
{
	return !ocf_alock_waitlist_is_empty(alock, line);
}

uint32_t ocf_cache_line_concurrency_suspended_no(struct ocf_alock *alock)
{
	return ocf_alock_waitlist_count(alock);
}

#define ALLOCATOR_NAME_FMT "ocf_%s_cl_conc"
#define ALLOCATOR_NAME_MAX (sizeof(ALLOCATOR_NAME_FMT) + OCF_CACHE_NAME_SIZE)

int ocf_cache_line_concurrency_init(struct ocf_alock **self,
		unsigned num_clines, ocf_cache_t cache)
{
	char name[ALLOCATOR_NAME_MAX];
	int ret;

	ret = snprintf(name, sizeof(name), ALLOCATOR_NAME_FMT,
			ocf_cache_get_name(cache));
	if (ret < 0)
		return ret;
	if (ret >= ALLOCATOR_NAME_MAX)
		return -ENOSPC;

	return ocf_alock_init(self, num_clines, name, &ocf_cline_conc_cbs, cache);
}

void ocf_cache_line_concurrency_deinit(struct ocf_alock **self)
{
	ocf_alock_deinit(self);
}

size_t ocf_cache_line_concurrency_size_of(ocf_cache_t cache)
{
	return ocf_alock_size(cache->device->collision_table_entries);
}
