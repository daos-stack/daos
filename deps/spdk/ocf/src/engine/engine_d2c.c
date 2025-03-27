/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "ocf/ocf.h"
#include "../ocf_cache_priv.h"
#include "engine_d2c.h"
#include "engine_common.h"
#include "cache_engine.h"
#include "../ocf_request.h"
#include "../utils/utils_io.h"
#include "../metadata/metadata.h"

#define OCF_ENGINE_DEBUG_IO_NAME "d2c"
#include "engine_debug.h"

static void _ocf_d2c_completion(struct ocf_request *req, int error)
{
	req->error = error;

	OCF_DEBUG_RQ(req, "Completion");

	if (req->error) {
		req->info.core_error = 1;
		ocf_core_stats_core_error_update(req->core, req->rw);
	}

	/* Complete request */
	req->complete(req, req->error);

	/* Release OCF request */
	ocf_req_put(req);
}

int ocf_io_d2c(struct ocf_request *req)
{
	ocf_core_t core = req->core;

	OCF_DEBUG_TRACE(req->cache);

	ocf_io_start(&req->ioi.io);

	/* Get OCF request - increase reference counter */
	ocf_req_get(req);

	ocf_submit_volume_req(&core->volume, req, _ocf_d2c_completion);

	ocf_engine_update_block_stats(req);

	ocf_core_stats_request_pt_update(req->core, req->part_id, req->rw,
			req->info.hit_no, req->core_line_count);

	/* Put OCF request - decrease reference counter */
	ocf_req_put(req);

	return 0;

}
