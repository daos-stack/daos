/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "ocf/ocf.h"
#include "../ocf_cache_priv.h"
#include "engine_wa.h"
#include "engine_common.h"
#include "cache_engine.h"
#include "../ocf_request.h"
#include "../utils/utils_io.h"
#include "../metadata/metadata.h"

#define OCF_ENGINE_DEBUG_IO_NAME "wa"
#include "engine_debug.h"

int ocf_write_wa(struct ocf_request *req)
{
	ocf_io_start(&req->ioi.io);

	/* Get OCF request - increase reference counter */
	ocf_req_get(req);

	ocf_req_hash(req);

	ocf_hb_req_prot_lock_rd(req); /*- Metadata RD access -----------------------*/

	/* Traverse request to check if there are mapped cache lines */
	ocf_engine_traverse(req);

	ocf_hb_req_prot_unlock_rd(req); /*- END Metadata RD access -----------------*/

	if (ocf_engine_is_hit(req)) {
		ocf_req_clear(req);

		/* There is HIT, do WT */
		ocf_get_io_if(ocf_cache_mode_wt)->write(req);

	} else {
		ocf_req_clear(req);

		/* MISS, do WI */
		ocf_get_io_if(ocf_cache_mode_wi)->write(req);
	}

	/* Put OCF request - decrease reference counter */
	ocf_req_put(req);

	return 0;
}


