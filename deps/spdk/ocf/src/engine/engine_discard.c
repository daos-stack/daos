/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "ocf/ocf.h"
#include "../ocf_cache_priv.h"
#include "cache_engine.h"
#include "engine_common.h"
#include "engine_discard.h"
#include "../metadata/metadata.h"
#include "../ocf_request.h"
#include "../utils/utils_io.h"
#include "../utils/utils_cache_line.h"
#include "../concurrency/ocf_concurrency.h"

#define OCF_ENGINE_DEBUG 0

#define OCF_ENGINE_DEBUG_IO_NAME "discard"
#include "engine_debug.h"

static int _ocf_discard_step_do(struct ocf_request *req);
static int _ocf_discard_step(struct ocf_request *req);
static int _ocf_discard_flush_cache(struct ocf_request *req);
static int _ocf_discard_core(struct ocf_request *req);

static const struct ocf_io_if _io_if_discard_step = {
	.read = _ocf_discard_step,
	.write = _ocf_discard_step,
};

static const struct ocf_io_if _io_if_discard_step_resume = {
	.read = _ocf_discard_step_do,
	.write = _ocf_discard_step_do,
};

static const struct ocf_io_if _io_if_discard_flush_cache = {
	.read = _ocf_discard_flush_cache,
	.write = _ocf_discard_flush_cache,
};

static const struct ocf_io_if _io_if_discard_core = {
	.read = _ocf_discard_core,
	.write = _ocf_discard_core,
};

static void _ocf_discard_complete_req(struct ocf_request *req, int error)
{
	req->complete(req, error);

	ocf_req_put(req);
}
static void _ocf_discard_core_complete(struct ocf_io *io, int error)
{
	struct ocf_request *req = io->priv1;

	OCF_DEBUG_RQ(req, "Core DISCARD Completion");

	_ocf_discard_complete_req(req, error);

	ocf_io_put(io);
}

static int _ocf_discard_core(struct ocf_request *req)
{
	struct ocf_io *io;
	int err;

	io = ocf_volume_new_io(&req->core->volume, req->io_queue,
			SECTORS_TO_BYTES(req->discard.sector),
			SECTORS_TO_BYTES(req->discard.nr_sects),
			OCF_WRITE, 0, 0);
	if (!io) {
		_ocf_discard_complete_req(req, -OCF_ERR_NO_MEM);
		return -OCF_ERR_NO_MEM;
	}

	ocf_io_set_cmpl(io, req, NULL, _ocf_discard_core_complete);
	err = ocf_io_set_data(io, req->data, 0);
	if (err) {
		_ocf_discard_core_complete(io, err);
		return err;
	}

	ocf_volume_submit_discard(io);

	return 0;
}

static void _ocf_discard_cache_flush_complete(struct ocf_io *io, int error)
{
	struct ocf_request *req = io->priv1;

	if (error) {
		ocf_metadata_error(req->cache);
		_ocf_discard_complete_req(req, error);
		ocf_io_put(io);
		return;
	}

	req->io_if = &_io_if_discard_core;
	ocf_engine_push_req_front(req, true);

	ocf_io_put(io);
}

static int _ocf_discard_flush_cache(struct ocf_request *req)
{
	struct ocf_io *io;

	io = ocf_volume_new_io(&req->cache->device->volume, req->io_queue,
			0, 0, OCF_WRITE, 0, 0);
	if (!io) {
		ocf_metadata_error(req->cache);
		_ocf_discard_complete_req(req, -OCF_ERR_NO_MEM);
		return -OCF_ERR_NO_MEM;
	}

	ocf_io_set_cmpl(io, req, NULL, _ocf_discard_cache_flush_complete);

	ocf_volume_submit_flush(io);

	return 0;
}

static void _ocf_discard_finish_step(struct ocf_request *req)
{
	req->discard.handled += BYTES_TO_SECTORS(req->byte_length);

	if (req->discard.handled < req->discard.nr_sects)
		req->io_if = &_io_if_discard_step;
	else if (!req->cache->metadata.is_volatile)
		req->io_if = &_io_if_discard_flush_cache;
	else
		req->io_if = &_io_if_discard_core;

	ocf_engine_push_req_front(req, true);
}

static void _ocf_discard_step_complete(struct ocf_request *req, int error)
{
	if (error)
		req->error |= error;

	if (env_atomic_dec_return(&req->req_remaining))
		return;

	OCF_DEBUG_RQ(req, "Completion");

	/* Release WRITE lock of request */
	ocf_req_unlock_wr(ocf_cache_line_concurrency(req->cache), req);

	if (req->error) {
		ocf_metadata_error(req->cache);
		_ocf_discard_complete_req(req, req->error);
		return;
	}

	_ocf_discard_finish_step(req);
}

int _ocf_discard_step_do(struct ocf_request *req)
{
	struct ocf_cache *cache = req->cache;

	/* Get OCF request - increase reference counter */
	ocf_req_get(req);

	env_atomic_set(&req->req_remaining, 1); /* One core IO */

	if (ocf_engine_mapped_count(req)) {
		/* There are mapped cache line, need to remove them */

		ocf_hb_req_prot_lock_wr(req);

		/* Remove mapped cache lines from metadata */
		ocf_purge_map_info(req);

		if (req->info.flush_metadata) {
			/* Request was dirty and need to flush metadata */
			ocf_metadata_flush_do_asynch(cache, req,
					_ocf_discard_step_complete);
		}

		ocf_hb_req_prot_unlock_wr(req);
	}

	ocf_hb_req_prot_lock_rd(req);

	/* Even if no cachelines are mapped they could be tracked in promotion
	 * policy. RD lock suffices. */
	ocf_promotion_req_purge(req->cache->promotion_policy, req);

	ocf_hb_req_prot_unlock_rd(req);

	OCF_DEBUG_RQ(req, "Discard");
	_ocf_discard_step_complete(req, 0);

	/* Put OCF request - decrease reference counter */
	ocf_req_put(req);

	return 0;
}

static void _ocf_discard_on_resume(struct ocf_request *req)
{
	OCF_DEBUG_RQ(req, "On resume");
	ocf_engine_push_req_front(req, true);
}

static int _ocf_discard_step(struct ocf_request *req)
{
	int lock;
	struct ocf_cache *cache = req->cache;

	OCF_DEBUG_TRACE(req->cache);

	req->byte_position = SECTORS_TO_BYTES(req->discard.sector +
			req->discard.handled);
	req->byte_length = OCF_MIN(SECTORS_TO_BYTES(req->discard.nr_sects -
			req->discard.handled), MAX_TRIM_RQ_SIZE);
	req->core_line_first = ocf_bytes_2_lines(cache, req->byte_position);
	req->core_line_last =
		ocf_bytes_2_lines(cache, req->byte_position + req->byte_length - 1);
	req->core_line_count = req->core_line_last - req->core_line_first + 1;
	req->io_if = &_io_if_discard_step_resume;

	ENV_BUG_ON(env_memset(req->map, sizeof(*req->map) * req->core_line_count,
			0));

	ocf_req_hash(req);
	ocf_hb_req_prot_lock_rd(req);

	/* Travers to check if request is mapped fully */
	ocf_engine_traverse(req);

	if (ocf_engine_mapped_count(req)) {
		/* Some cache line are mapped, lock request for WRITE access */
		lock = ocf_req_async_lock_wr(
				ocf_cache_line_concurrency(cache),
				req, _ocf_discard_on_resume);
	} else {
		lock = OCF_LOCK_ACQUIRED;
	}

	ocf_hb_req_prot_unlock_rd(req);

	if (lock >= 0) {
		if (OCF_LOCK_ACQUIRED == lock) {
			_ocf_discard_step_do(req);
		} else {
			/* WR lock was not acquired, need to wait for resume */
			OCF_DEBUG_RQ(req, "NO LOCK")
		}
	} else {
		OCF_DEBUG_RQ(req, "LOCK ERROR %d", lock);
		req->error |= lock;
		_ocf_discard_finish_step(req);
	}

	env_cond_resched();

	return 0;
}

int ocf_discard(struct ocf_request *req)
{
	OCF_DEBUG_TRACE(req->cache);

	ocf_io_start(&req->ioi.io);

	if (req->rw == OCF_READ) {
		req->complete(req, -OCF_ERR_INVAL);
		ocf_req_put(req);
		return 0;
	}

	/* Get OCF request - increase reference counter */
	ocf_req_get(req);

	_ocf_discard_step(req);

	/* Put OCF request - decrease reference counter */
	ocf_req_put(req);

	return 0;
}
