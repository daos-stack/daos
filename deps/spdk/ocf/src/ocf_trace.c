/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf_env.h"
#include "ocf_priv.h"
#include "ocf/ocf.h"
#include "ocf/ocf_trace.h"
#include "ocf_core_priv.h"
#include "ocf_cache_priv.h"
#include "ocf_trace_priv.h"

struct core_trace_visitor_ctx {
	ocf_cache_t cache;
	ocf_queue_t io_queue;
};

static int _ocf_core_desc(ocf_core_t core, void  *ctx)
{
	struct ocf_event_core_desc core_desc;
	struct core_trace_visitor_ctx *visitor_ctx =
			(struct core_trace_visitor_ctx *) ctx;
	ocf_cache_t cache = visitor_ctx->cache;

	ocf_event_init_hdr(&core_desc.hdr, ocf_event_type_core_desc,
			ocf_trace_seq_id(cache),
			env_ticks_to_nsecs(env_get_tick_count()),
			sizeof(core_desc));
	core_desc.name = ocf_core_get_name(core);
	core_desc.core_size = ocf_volume_get_length(
			ocf_core_get_volume(core));

	ocf_trace_push(visitor_ctx->io_queue,
			&core_desc, sizeof(core_desc));

	return 0;
}

static int _ocf_trace_cache_info(ocf_cache_t cache, ocf_queue_t io_queue)
{
	struct ocf_event_cache_desc cache_desc;
	int retval;
	struct core_trace_visitor_ctx visitor_ctx;

	ocf_event_init_hdr(&cache_desc.hdr, ocf_event_type_cache_desc,
			ocf_trace_seq_id(cache),
			env_ticks_to_nsecs(env_get_tick_count()),
			sizeof(cache_desc));

	cache_desc.name = ocf_cache_get_name(cache);
	cache_desc.cache_line_size = ocf_cache_get_line_size(cache);
	cache_desc.cache_mode = ocf_cache_get_mode(cache);

	if (ocf_cache_is_device_attached(cache)) {
		/* Attached cache */
		cache_desc.cache_size = ocf_volume_get_length(
				ocf_cache_get_volume(cache));
	} else {
		cache_desc.cache_size = 0;
	}

	cache_desc.cores_no = ocf_cache_get_core_count(cache);
	cache_desc.version = OCF_EVENT_VERSION;

	ocf_trace_push(io_queue, &cache_desc, sizeof(cache_desc));

	visitor_ctx.cache = cache;
	visitor_ctx.io_queue = io_queue;

	retval = ocf_core_visit(cache, _ocf_core_desc, &visitor_ctx, true);

	return retval;
}

int ocf_mngt_start_trace(ocf_cache_t cache, void *trace_ctx,
	ocf_trace_callback_t trace_callback)
{
	ocf_queue_t queue;
	int result = 0;

	OCF_CHECK_NULL(cache);

	if (!trace_callback)
		return -EINVAL;

	if (cache->trace.trace_callback) {
		ocf_cache_log(cache, log_err, "Tracing already started\n");
		return -EINVAL;
	}

	cache->trace.trace_callback = trace_callback;
	cache->trace.trace_ctx = trace_ctx;

	// Reset trace stop flag
	list_for_each_entry(queue, &cache->io_queues, list) {
		env_atomic_set(&queue->trace_stop, 0);
	}

	list_for_each_entry(queue, &cache->io_queues, list) {
		result = _ocf_trace_cache_info(cache, queue);
		if (result) {
			cache->trace.trace_callback = NULL;
			return result;
		}
	}

	ocf_cache_log(cache, log_info, "Tracing started\n");

	return result;
}

int ocf_mngt_stop_trace(ocf_cache_t cache)
{
	ocf_queue_t queue;

	OCF_CHECK_NULL(cache);

	if (!cache->trace.trace_callback) {
		ocf_cache_log(cache, log_err, "Tracing not started\n");
		return -EINVAL;
	}

	// Set trace stop flag
	list_for_each_entry(queue, &cache->io_queues, list) {
		env_atomic_set(&queue->trace_stop, OCF_TRACING_STOP);
	}

	cache->trace.trace_callback = NULL;
	cache->trace.trace_ctx = NULL;

	// Poll for all ongoing traces completion
	while (ocf_is_trace_ongoing(cache))
		env_msleep(20);

	return 0;
}
