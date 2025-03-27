/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __OCF_TRACE_PRIV_H__
#define __OCF_TRACE_PRIV_H__

#include "ocf/ocf.h"
#include "ocf_env.h"
#include "ocf/ocf_trace.h"
#include "engine/engine_common.h"
#include "ocf_request.h"
#include "ocf_core_priv.h"
#include "ocf_queue_priv.h"

static inline bool ocf_is_trace_ongoing(ocf_cache_t cache)
{
	ocf_queue_t q;

	list_for_each_entry(q, &cache->io_queues, list) {
		if (env_atomic64_read(&q->trace_ref_cntr))
			return true;
	}

	return false;
}
static inline void ocf_event_init_hdr(struct ocf_event_hdr *hdr,
		ocf_event_type type, uint64_t sid, uint64_t timestamp,
		uint32_t size)
{
	hdr->sid = sid;
	hdr->timestamp = timestamp;
	hdr->type = type;
	hdr->size = size;
}

static inline uint64_t ocf_trace_seq_id(ocf_cache_t cache)
{
	return env_atomic64_inc_return(&cache->trace.trace_seq_ref);
}

static inline void ocf_trace_init_io(struct ocf_request *req)
{
	req->timestamp = env_ticks_to_nsecs(env_get_tick_count());
	req->sid = ocf_trace_seq_id(req->cache);
}

static inline void ocf_trace_prep_io_event(struct ocf_event_io *ev,
		struct ocf_request *req, ocf_event_operation_t op)
{
	ocf_event_init_hdr(&ev->hdr, ocf_event_type_io, req->sid,
		req->timestamp, sizeof(*ev));

	ev->addr = req->byte_position;
	if (op == ocf_event_operation_discard)
		ev->len = req->discard.nr_sects << ENV_SECTOR_SHIFT;
	else
		ev->len = req->byte_length;

	ev->operation = op;
	ev->core_name = ocf_core_get_name(req->core);

	ev->io_class = req->ioi.io.io_class;
}

static inline void ocf_trace_push(ocf_queue_t queue, void *trace, uint32_t size)
{
	ocf_cache_t cache;
	ocf_trace_callback_t trace_callback;
	void *trace_ctx;

	OCF_CHECK_NULL(queue);

	cache = ocf_queue_get_cache(queue);

	if (cache->trace.trace_callback == NULL)
		return;

	env_atomic64_inc(&queue->trace_ref_cntr);

	if (env_atomic_read(&queue->trace_stop)) {
		// Tracing stop was requested
		env_atomic64_dec(&queue->trace_ref_cntr);
		return;
	}

	/*
	 * Remember callback and context pointers.
	 * These will be valid even when later on original pointers
	 * will be set to NULL as cleanup will wait till trace
	 * reference counter is zero
	 */
	trace_callback = cache->trace.trace_callback;
	trace_ctx = cache->trace.trace_ctx;

	if (trace_callback && trace_ctx) {
		trace_callback(cache, trace_ctx, queue, trace, size);
	}

	env_atomic64_dec(&queue->trace_ref_cntr);
}

static inline void ocf_trace_io(struct ocf_request *req,
		ocf_event_operation_t dir)
{
	struct ocf_event_io ev;

	if (!req->cache->trace.trace_callback)
		return;

	ocf_trace_prep_io_event(&ev, req, dir);

	ocf_trace_push(req->io_queue, &ev, sizeof(ev));
}

static inline void ocf_trace_io_cmpl(struct ocf_request *req)
{
	struct ocf_event_io_cmpl ev;

	if (!req->cache->trace.trace_callback)
		return;

	ocf_event_init_hdr(&ev.hdr, ocf_event_type_io_cmpl,
			ocf_trace_seq_id(req->cache),
			env_ticks_to_nsecs(env_get_tick_count()),
			sizeof(ev));
	ev.rsid = req->sid;
	ev.is_hit = ocf_engine_is_hit(req);

	ocf_trace_push(req->io_queue, &ev, sizeof(ev));
}

#endif /* __OCF_TRACE_PRIV_H__ */
