/*
 * Copyright(c) 2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf_concurrency.h"
#include "../metadata/metadata_io.h"
#include "../ocf_priv.h"
#include "../ocf_request.h"
#include "../utils/utils_alock.h"
#include "../utils/utils_cache_line.h"

struct ocf_mio_alock
{
	unsigned first_page;
	unsigned num_pages;
};

static ocf_cache_line_t ocf_mio_lock_get_entry(
		struct ocf_alock *alock, struct ocf_request *req,
		unsigned index)
{
	struct ocf_mio_alock *mio_alock = (void*)alock + ocf_alock_obj_size();
	struct metadata_io_request *m_req = (struct metadata_io_request *)req;
	unsigned page = m_req->page + index;

	ENV_BUG_ON(page < mio_alock->first_page);
	ENV_BUG_ON(page >= mio_alock->first_page + mio_alock->num_pages);

	return page - mio_alock->first_page;
}

static int ocf_mio_lock_fast(struct ocf_alock *alock,
		struct ocf_request *req, int rw)
{
	ocf_cache_line_t entry;
	int ret = OCF_LOCK_ACQUIRED;
	int32_t i;
	ENV_BUG_ON(rw != OCF_WRITE);

	for (i = 0; i < req->core_line_count; i++) {
		entry = ocf_mio_lock_get_entry(alock, req, i);
		ENV_BUG_ON(ocf_alock_is_index_locked(alock, req, i));

		if (ocf_alock_trylock_entry_wr(alock, entry)) {
			/* cache entry locked */
			ocf_alock_mark_index_locked(alock, req, i, true);
		} else {
			/* Not possible to lock all cachelines */
			ret = OCF_LOCK_NOT_ACQUIRED;
			break;
		}
	}

	/* Check if request is locked */
	if (ret == OCF_LOCK_NOT_ACQUIRED) {
		/* Request is not locked, discard acquired locks */
		for (; i >= 0; i--) {
			entry = ocf_mio_lock_get_entry(alock, req, i);

			if (ocf_alock_is_index_locked(alock, req, i)) {
				ocf_alock_unlock_one_wr(alock, entry);
				ocf_alock_mark_index_locked(alock, req, i, false);
			}
		}
	}

	return ret;
}

static int ocf_mio_lock_slow(struct ocf_alock *alock,
		struct ocf_request *req, int rw, ocf_req_async_lock_cb cmpl)
{
	int32_t i;
	ocf_cache_line_t entry;
	int ret = 0;
	ENV_BUG_ON(rw != OCF_WRITE);

	for (i = 0; i < req->core_line_count; i++) {
		entry = ocf_mio_lock_get_entry(alock, req, i);
		ENV_BUG_ON(ocf_alock_is_index_locked(alock, req, i));

		if (!ocf_alock_lock_one_wr(alock, entry, cmpl, req, i)) {
			/* lock not acquired and not added to wait list */
			ret = -OCF_ERR_NO_MEM;
			goto err;
		}
	}

	return ret;

err:
	for (; i >= 0; i--) {
		entry = ocf_mio_lock_get_entry(alock, req, i);
		ocf_alock_waitlist_remove_entry(alock, req, i, entry, OCF_WRITE);
	}

	return ret;
}

static struct ocf_alock_lock_cbs ocf_mio_conc_cbs = {
		.lock_entries_fast = ocf_mio_lock_fast,
		.lock_entries_slow = ocf_mio_lock_slow
};

int ocf_mio_async_lock(struct ocf_alock *alock,
		struct metadata_io_request *m_req,
		ocf_req_async_lock_cb cmpl)
{
	return ocf_alock_lock_wr(alock, &m_req->req, cmpl);
}

void ocf_mio_async_unlock(struct ocf_alock *alock,
		struct metadata_io_request *m_req)
{
	ocf_cache_line_t entry;
	struct ocf_request *req = &m_req->req;
	int i;

	for (i = 0; i < req->core_line_count; i++) {
		if (!ocf_alock_is_index_locked(alock, req, i))
			continue;

		entry = ocf_mio_lock_get_entry(alock, req, i);

		ocf_alock_unlock_one_wr(alock, entry);
		ocf_alock_mark_index_locked(alock, req, i, false);
	}

	m_req->alock_status = 0;
}


#define ALLOCATOR_NAME_FMT "ocf_%s_mio_conc"
#define ALLOCATOR_NAME_MAX (sizeof(ALLOCATOR_NAME_FMT) + OCF_CACHE_NAME_SIZE)

int ocf_mio_concurrency_init(struct ocf_alock **self,
		unsigned first_page, unsigned num_pages,
		ocf_cache_t cache)
{
	struct ocf_alock *alock;
	struct ocf_mio_alock *mio_alock;
	size_t base_size = ocf_alock_obj_size();
	char name[ALLOCATOR_NAME_MAX];
	int ret;

	ret = snprintf(name, sizeof(name), ALLOCATOR_NAME_FMT,
			ocf_cache_get_name(cache));
	if (ret < 0)
		return ret;
	if (ret >= ALLOCATOR_NAME_MAX)
		return -OCF_ERR_NO_MEM;

	alock = env_vzalloc(base_size + sizeof(struct ocf_mio_alock));
	if (!alock)
		return -OCF_ERR_NO_MEM;

	ret = ocf_alock_init_inplace(alock, num_pages, name, &ocf_mio_conc_cbs, cache);
	if (ret) {
		env_free(alock);
		return ret;
	}

	mio_alock = (void*)alock + base_size;
	mio_alock->first_page = first_page;
	mio_alock->num_pages = num_pages;

	*self = alock;
	return 0;
}

void ocf_mio_concurrency_deinit(struct ocf_alock **self)
{
	ocf_alock_deinit(self);
}
