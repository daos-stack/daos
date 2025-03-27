/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "../ocf_cache_priv.h"
#include "engine_inv.h"
#include "engine_common.h"
#include "cache_engine.h"
#include "../ocf_request.h"
#include "../utils/utils_cache_line.h"
#include "../metadata/metadata.h"
#include "../concurrency/ocf_concurrency.h"

#define OCF_ENGINE_DEBUG_IO_NAME "inv"
#include "engine_debug.h"

static void _ocf_invalidate_req(struct ocf_request *req, int error)
{
	if (error) {
		req->error = error;
		ocf_core_stats_cache_error_update(req->core, OCF_WRITE);
	}

	if (env_atomic_dec_return(&req->req_remaining))
		return;

	OCF_DEBUG_RQ(req, "Completion");

	if (req->error)
		ocf_engine_error(req, true, "Failed to flush metadata to cache");

	ocf_req_unlock_wr(ocf_cache_line_concurrency(req->cache), req);

	/* Put OCF request - decrease reference counter */
	ocf_req_put(req);
}

static int _ocf_invalidate_do(struct ocf_request *req)
{
	struct ocf_cache *cache = req->cache;

	ENV_BUG_ON(env_atomic_read(&req->req_remaining));

	ocf_hb_req_prot_lock_wr(req);
	ocf_purge_map_info(req);
	ocf_hb_req_prot_unlock_wr(req);

	env_atomic_inc(&req->req_remaining);

	if (ocf_volume_is_atomic(&cache->device->volume) &&
			req->info.flush_metadata) {
		/* Metadata flush IO */
		ocf_metadata_flush_do_asynch(cache, req, _ocf_invalidate_req);
	}

	_ocf_invalidate_req(req, 0);

	return 0;
}

static const struct ocf_io_if _io_if_invalidate = {
	.read = _ocf_invalidate_do,
	.write = _ocf_invalidate_do,
};

void ocf_engine_invalidate(struct ocf_request *req)
{
	ocf_engine_push_req_front_if(req, &_io_if_invalidate, true);
}
