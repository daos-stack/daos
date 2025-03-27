/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "../ocf_cache_priv.h"
#include "engine_rd.h"
#include "engine_pt.h"
#include "engine_inv.h"
#include "engine_bf.h"
#include "engine_common.h"
#include "cache_engine.h"
#include "../concurrency/ocf_concurrency.h"
#include "../utils/utils_io.h"
#include "../ocf_request.h"
#include "../utils/utils_cache_line.h"
#include "../utils/utils_user_part.h"
#include "../metadata/metadata.h"
#include "../ocf_def_priv.h"

#define OCF_ENGINE_DEBUG_IO_NAME "rd"
#include "engine_debug.h"

static void _ocf_read_generic_hit_complete(struct ocf_request *req, int error)
{
	struct ocf_alock *c = ocf_cache_line_concurrency(
			req->cache);

	if (error)
		req->error |= error;

	if (req->error)
		inc_fallback_pt_error_counter(req->cache);

	/* Handle callback-caller race to let only one of the two complete the
	 * request. Also, complete original request only if this is the last
	 * sub-request to complete
	 */
	if (env_atomic_dec_return(&req->req_remaining) == 0) {
		OCF_DEBUG_RQ(req, "HIT completion");

		if (req->error) {
			ocf_core_stats_cache_error_update(req->core, OCF_READ);
			ocf_engine_push_req_front_pt(req);
		} else {
			ocf_req_unlock(c, req);

			/* Complete request */
			req->complete(req, req->error);

			/* Free the request at the last point
			 * of the completion path
			 */
			ocf_req_put(req);
		}
	}
}

static void _ocf_read_generic_miss_complete(struct ocf_request *req, int error)
{
	struct ocf_cache *cache = req->cache;

	if (error)
		req->error = error;

	/* Handle callback-caller race to let only one of the two complete the
	 * request. Also, complete original request only if this is the last
	 * sub-request to complete
	 */
	if (env_atomic_dec_return(&req->req_remaining) == 0) {
		OCF_DEBUG_RQ(req, "MISS completion");

		if (req->error) {
			/*
			 * --- Do not submit this request to write-back-thread.
			 * Stop it here ---
			 */
			req->complete(req, req->error);

			req->info.core_error = 1;
			ocf_core_stats_core_error_update(req->core, OCF_READ);

			ctx_data_free(cache->owner, req->cp_data);
			req->cp_data = NULL;

			/* Invalidate metadata */
			ocf_engine_invalidate(req);

			return;
		}

		/* Copy pages to copy vec, since this is the one needed
		 * by the above layer
		 */
		ctx_data_cpy(cache->owner, req->cp_data, req->data, 0, 0,
				req->byte_length);

		/* Complete request */
		req->complete(req, req->error);

		ocf_engine_backfill(req);
	}
}

void ocf_read_generic_submit_hit(struct ocf_request *req)
{
	env_atomic_set(&req->req_remaining, ocf_engine_io_count(req));

	ocf_submit_cache_reqs(req->cache, req, OCF_READ, 0, req->byte_length,
		ocf_engine_io_count(req), _ocf_read_generic_hit_complete);
}

static inline void _ocf_read_generic_submit_miss(struct ocf_request *req)
{
	struct ocf_cache *cache = req->cache;
	int ret;

	env_atomic_set(&req->req_remaining, 1);

	req->cp_data = ctx_data_alloc(cache->owner,
			BYTES_TO_PAGES(req->byte_length));
	if (!req->cp_data)
		goto err_alloc;

	ret = ctx_data_mlock(cache->owner, req->cp_data);
	if (ret)
		goto err_alloc;

	/* Submit read request to core device. */
	ocf_submit_volume_req(&req->core->volume, req,
			_ocf_read_generic_miss_complete);

	return;

err_alloc:
	_ocf_read_generic_miss_complete(req, -OCF_ERR_NO_MEM);
}

static int _ocf_read_generic_do(struct ocf_request *req)
{
	if (ocf_engine_is_miss(req) && req->alock_rw == OCF_READ) {
		/* Miss can be handled only on write locks.
		 * Need to switch to PT
		 */
		OCF_DEBUG_RQ(req, "Switching to PT");
		ocf_read_pt_do(req);
		return 0;
	}

	/* Get OCF request - increase reference counter */
	ocf_req_get(req);

	if (ocf_engine_is_miss(req)) {
		if (req->info.dirty_any) {
			ocf_hb_req_prot_lock_rd(req);

			/* Request is dirty need to clean request */
			ocf_engine_clean(req);

			ocf_hb_req_prot_unlock_rd(req);

			/* We need to clean request before processing, return */
			ocf_req_put(req);

			return 0;
		}

		ocf_hb_req_prot_lock_wr(req);

		/* Set valid status bits map */
		ocf_set_valid_map_info(req);

		ocf_hb_req_prot_unlock_wr(req);
	}

	if (ocf_engine_needs_repart(req)) {
		OCF_DEBUG_RQ(req, "Re-Part");

		ocf_hb_req_prot_lock_wr(req);

		/* Probably some cache lines are assigned into wrong
		 * partition. Need to move it to new one
		 */
		ocf_user_part_move(req);

		ocf_hb_req_prot_unlock_wr(req);
	}

	OCF_DEBUG_RQ(req, "Submit");

	/* Submit IO */
	if (ocf_engine_is_hit(req))
		ocf_read_generic_submit_hit(req);
	else
		_ocf_read_generic_submit_miss(req);

	/* Update statistics */
	ocf_engine_update_request_stats(req);
	ocf_engine_update_block_stats(req);

	/* Put OCF request - decrease reference counter */
	ocf_req_put(req);

	return 0;
}

static const struct ocf_io_if _io_if_read_generic_resume = {
	.read = _ocf_read_generic_do,
	.write = _ocf_read_generic_do,
};

static const struct ocf_engine_callbacks _rd_engine_callbacks =
{
	.resume = ocf_engine_on_resume,
};

int ocf_read_generic(struct ocf_request *req)
{
	int lock = OCF_LOCK_NOT_ACQUIRED;
	struct ocf_cache *cache = req->cache;

	ocf_io_start(&req->ioi.io);

	if (env_atomic_read(&cache->pending_read_misses_list_blocked)) {
		/* There are conditions to bypass IO */
		req->force_pt = true;
		ocf_get_io_if(ocf_cache_mode_pt)->read(req);
		return 0;
	}

	/* Get OCF request - increase reference counter */
	ocf_req_get(req);

	/* Set resume call backs */
	req->io_if = &_io_if_read_generic_resume;
	req->engine_cbs = &_rd_engine_callbacks;

	lock = ocf_engine_prepare_clines(req);

	if (!ocf_req_test_mapping_error(req)) {
		if (lock >= 0) {
			if (lock != OCF_LOCK_ACQUIRED) {
				/* Lock was not acquired, need to wait for resume */
				OCF_DEBUG_RQ(req, "NO LOCK");
			} else {
				/* Lock was acquired can perform IO */
				_ocf_read_generic_do(req);
			}
		} else {
			OCF_DEBUG_RQ(req, "LOCK ERROR %d", lock);
			req->complete(req, lock);
			ocf_req_put(req);
		}
	} else {
		ocf_req_clear(req);
		req->force_pt = true;
		ocf_get_io_if(ocf_cache_mode_pt)->read(req);
	}


	/* Put OCF request - decrease reference counter */
	ocf_req_put(req);

	return 0;
}
