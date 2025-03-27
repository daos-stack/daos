/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "../ocf_cache_priv.h"
#include "cache_engine.h"
#include "engine_common.h"
#include "engine_wb.h"
#include "engine_inv.h"
#include "../metadata/metadata.h"
#include "../ocf_request.h"
#include "../utils/utils_io.h"
#include "../utils/utils_cache_line.h"
#include "../utils/utils_request.h"
#include "../utils/utils_user_part.h"
#include "../concurrency/ocf_concurrency.h"

#define OCF_ENGINE_DEBUG_IO_NAME "wb"
#include "engine_debug.h"

static const struct ocf_io_if _io_if_wb_resume = {
	.read = ocf_write_wb_do,
	.write = ocf_write_wb_do,
};

static void _ocf_write_wb_update_bits(struct ocf_request *req)
{
	bool miss = ocf_engine_is_miss(req);
	bool clean_any = !ocf_engine_is_dirty_all(req);

	if (!miss && !clean_any) {
		ocf_req_set_cleaning_hot(req);
		return;
	}

	ocf_hb_req_prot_lock_wr(req);
	if (miss) {
		/* Update valid status bits */
		ocf_set_valid_map_info(req);
	}
	if (clean_any) {
		/* set dirty bits, and mark if metadata flushing is required */
		ocf_set_dirty_map_info(req);
	}

	ocf_req_set_cleaning_hot(req);

	ocf_hb_req_prot_unlock_wr(req);
}

static void _ocf_write_wb_io_flush_metadata(struct ocf_request *req, int error)
{
	if (error)
		req->error = error;

	if (env_atomic_dec_return(&req->req_remaining))
		return;

	if (req->error)
		ocf_engine_error(req, true, "Failed to write data to cache");

	ocf_req_unlock_wr(ocf_cache_line_concurrency(req->cache), req);

	req->complete(req, req->error);

	ocf_req_put(req);
}

static int ocf_write_wb_do_flush_metadata(struct ocf_request *req)
{
	struct ocf_cache *cache = req->cache;

	env_atomic_set(&req->req_remaining, 1); /* One core IO */

	_ocf_write_wb_update_bits(req);

	if (req->info.flush_metadata) {
		OCF_DEBUG_RQ(req, "Flush metadata");
		ocf_metadata_flush_do_asynch(cache, req,
				_ocf_write_wb_io_flush_metadata);
	}

	_ocf_write_wb_io_flush_metadata(req, 0);

	return 0;
}

static const struct ocf_io_if _io_if_wb_flush_metadata = {
		.read = ocf_write_wb_do_flush_metadata,
		.write = ocf_write_wb_do_flush_metadata,
};

static void _ocf_write_wb_complete(struct ocf_request *req, int error)
{
	if (error) {
		ocf_core_stats_cache_error_update(req->core, OCF_WRITE);
		req->error |= error;
	}

	if (env_atomic_dec_return(&req->req_remaining))
		return;

	OCF_DEBUG_RQ(req, "Completion");

	if (req->error) {
		ocf_engine_error(req, true, "Failed to write data to cache");

		req->complete(req, req->error);

		ocf_engine_invalidate(req);
	} else {
		ocf_engine_push_req_front_if(req, &_io_if_wb_flush_metadata,
				true);
	}
}


static inline void _ocf_write_wb_submit(struct ocf_request *req)
{
	struct ocf_cache *cache = req->cache;

	env_atomic_set(&req->req_remaining, ocf_engine_io_count(req));

	/*
	 * 1. Submit data
	 * 2. Wait for completion of data
	 * 3. Then continue processing request (flush metadata)
	 */

	if (ocf_engine_needs_repart(req)) {
		OCF_DEBUG_RQ(req, "Re-Part");

		ocf_hb_req_prot_lock_wr(req);

		/* Probably some cache lines are assigned into wrong
		 * partition. Need to move it to new one
		 */
		ocf_user_part_move(req);

		ocf_hb_req_prot_unlock_wr(req);
	}

	OCF_DEBUG_RQ(req, "Submit Data");

	/* Data IO */
	ocf_submit_cache_reqs(cache, req, OCF_WRITE, 0, req->byte_length,
			ocf_engine_io_count(req), _ocf_write_wb_complete);
}

int ocf_write_wb_do(struct ocf_request *req)
{
	/* Get OCF request - increase reference counter */
	ocf_req_get(req);

	/* Submit IO */
	_ocf_write_wb_submit(req);

	/* Update statistics */
	ocf_engine_update_request_stats(req);
	ocf_engine_update_block_stats(req);

	/* Put OCF request - decrease reference counter */
	ocf_req_put(req);

	return 0;
}

static const struct ocf_engine_callbacks _wb_engine_callbacks =
{
	.resume = ocf_engine_on_resume,
};

int ocf_write_wb(struct ocf_request *req)
{
	int lock = OCF_LOCK_NOT_ACQUIRED;

	ocf_io_start(&req->ioi.io);

	/* Not sure if we need this. */
	ocf_req_get(req);

	/* Set resume io_if */
	req->io_if = &_io_if_wb_resume;
	req->engine_cbs = &_wb_engine_callbacks;

	/* TODO: Handle fits into dirty */

	lock = ocf_engine_prepare_clines(req);

	if (!ocf_req_test_mapping_error(req)) {
		if (lock >= 0) {
			if (lock != OCF_LOCK_ACQUIRED) {
				/* WR lock was not acquired, need to wait for resume */
				OCF_DEBUG_RQ(req, "NO LOCK");
			} else {
				ocf_write_wb_do(req);
			}
		} else {
			OCF_DEBUG_RQ(req, "LOCK ERROR %d", lock);
			req->complete(req, lock);
			ocf_req_put(req);
		}
	} else {
		ocf_req_clear(req);
		ocf_get_io_if(ocf_cache_mode_pt)->write(req);
	}

	/* Put OCF request - decrease reference counter */
	ocf_req_put(req);

	return 0;
}
