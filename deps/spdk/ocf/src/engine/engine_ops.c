/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "ocf/ocf.h"
#include "../ocf_cache_priv.h"
#include "engine_common.h"
#include "cache_engine.h"
#include "engine_ops.h"
#include "../ocf_request.h"
#include "../utils/utils_io.h"

#define OCF_ENGINE_DEBUG_IO_NAME "ops"
#include "engine_debug.h"

static void _ocf_engine_ops_complete(struct ocf_request *req, int error)
{
	if (error)
		req->error |= error;

	if (env_atomic_dec_return(&req->req_remaining))
		return;

	OCF_DEBUG_RQ(req, "Completion");

	if (req->error) {
		/* An error occured */
		ocf_engine_error(req, false, "Core operation failure");
	}

	/* Complete requests - both to cache and to core*/
	req->complete(req, req->error);

	/* Release OCF request */
	ocf_req_put(req);
}

int ocf_engine_ops(struct ocf_request *req)
{
	/* Get OCF request - increase reference counter */
	ocf_req_get(req);

	/* IO to the core device and to the cache device */
	env_atomic_set(&req->req_remaining, 2);

	/* Submit operation into core device */
	ocf_submit_volume_req(&req->core->volume, req,
			_ocf_engine_ops_complete);


	/* submit flush to cache device */
	ocf_submit_cache_flush(req,  _ocf_engine_ops_complete);

	/* Put OCF request - decrease reference counter */
	ocf_req_put(req);

	return 0;
}


