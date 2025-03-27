/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "../ocf_cache_priv.h"
#include "../ocf_ctx_priv.h"
#include "engine_bf.h"
#include "engine_inv.h"
#include "engine_common.h"
#include "cache_engine.h"
#include "../ocf_request.h"
#include "../utils/utils_io.h"
#include "../concurrency/ocf_concurrency.h"

#define OCF_ENGINE_DEBUG_IO_NAME "bf"
#include "engine_debug.h"

/* Decrements and checks if queue may be unblocked again */
static inline void backfill_queue_dec_unblock(struct ocf_cache *cache)
{
	env_atomic_dec(&cache->pending_read_misses_list_count);

	if (!env_atomic_read(&cache->pending_read_misses_list_blocked))
		return;

	if (env_atomic_read(&cache->pending_read_misses_list_count)
			< cache->backfill.queue_unblock_size)
		env_atomic_set(&cache->pending_read_misses_list_blocked, 0);
}

static inline void backfill_queue_inc_block(struct ocf_cache *cache)
{
	if (env_atomic_inc_return(&cache->pending_read_misses_list_count)
			>= cache->backfill.max_queue_size)
		env_atomic_set(&cache->pending_read_misses_list_blocked, 1);
}

static void _ocf_backfill_complete(struct ocf_request *req, int error)
{
	struct ocf_cache *cache = req->cache;

	if (error)
		req->error = error;

	if (req->error)
		inc_fallback_pt_error_counter(req->cache);

	/* Handle callback-caller race to let only one of the two complete the
	 * request. Also, complete original request only if this is the last
	 * sub-request to complete
	 */
	if (env_atomic_dec_return(&req->req_remaining))
		return;

	/* We must free the pages we have allocated */
	ctx_data_secure_erase(cache->owner, req->data);
	ctx_data_munlock(cache->owner, req->data);
	ctx_data_free(cache->owner, req->data);
	req->data = NULL;

	if (req->error) {
		ocf_core_stats_cache_error_update(req->core, OCF_WRITE);
		ocf_engine_invalidate(req);
	} else {
		ocf_req_unlock(ocf_cache_line_concurrency(cache), req);

		/* put the request at the last point of the completion path */
		ocf_req_put(req);
	}
}

static int _ocf_backfill_do(struct ocf_request *req)
{
	unsigned int reqs_to_issue;

	backfill_queue_dec_unblock(req->cache);

	reqs_to_issue = ocf_engine_io_count(req);

	/* There will be #reqs_to_issue completions */
	env_atomic_set(&req->req_remaining, reqs_to_issue);

	req->data = req->cp_data;

	ocf_submit_cache_reqs(req->cache, req, OCF_WRITE, 0, req->byte_length,
				reqs_to_issue, _ocf_backfill_complete);

	return 0;
}

static const struct ocf_io_if _io_if_backfill = {
	.read = _ocf_backfill_do,
	.write = _ocf_backfill_do,
};

void ocf_engine_backfill(struct ocf_request *req)
{
	backfill_queue_inc_block(req->cache);
	ocf_engine_push_req_front_if(req, &_io_if_backfill, true);
}
