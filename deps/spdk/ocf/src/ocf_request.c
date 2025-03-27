/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "ocf_request.h"
#include "ocf_cache_priv.h"
#include "concurrency/ocf_metadata_concurrency.h"
#include "utils/utils_cache_line.h"

#define OCF_UTILS_RQ_DEBUG 0

#if 1 == OCF_UTILS_RQ_DEBUG
#define OCF_DEBUG_TRACE(cache) \
	ocf_cache_log(cache, log_info, "[Utils][RQ] %s\n", __func__)

#define OCF_DEBUG_PARAM(cache, format, ...) \
	ocf_cache_log(cache, log_info, "[Utils][RQ] %s - "format"\n", \
			__func__, ##__VA_ARGS__)
#else
#define OCF_DEBUG_TRACE(cache)
#define OCF_DEBUG_PARAM(cache, format, ...)
#endif

enum ocf_req_size {
	ocf_req_size_1 = 0,
	ocf_req_size_2,
	ocf_req_size_4,
	ocf_req_size_8,
	ocf_req_size_16,
	ocf_req_size_32,
	ocf_req_size_64,
	ocf_req_size_128,
};

static inline size_t ocf_req_sizeof_map(struct ocf_request *req)
{
	uint32_t lines = req->core_line_count;
	size_t size = (lines * sizeof(struct ocf_map_info));

	ENV_BUG_ON(lines == 0);
	return size;
}

static inline size_t ocf_req_sizeof_alock_status(struct ocf_request *req)
{
	uint32_t lines = req->core_line_count;
	size_t size = (lines * sizeof(uint8_t));

	ENV_BUG_ON(lines == 0);
	return size;
}

int ocf_req_allocator_init(struct ocf_ctx *ocf_ctx)
{
	ocf_ctx->resources.req = env_mpool_create(sizeof(struct ocf_request),
		sizeof(struct ocf_map_info) + sizeof(uint8_t), ENV_MEM_NORMAL, ocf_req_size_128,
		false, NULL, "ocf_req", true);

	if (ocf_ctx->resources.req == NULL)
		return -1;

	return 0;
}

void ocf_req_allocator_deinit(struct ocf_ctx *ocf_ctx)
{
	env_mpool_destroy(ocf_ctx->resources.req);
	ocf_ctx->resources.req = NULL;
}

struct ocf_request *ocf_req_new(ocf_queue_t queue, ocf_core_t core,
		uint64_t addr, uint32_t bytes, int rw)
{
	uint64_t core_line_first, core_line_last, core_line_count;
	ocf_cache_t cache = queue->cache;
	struct ocf_request *req;
	bool map_allocated = true;

	if (likely(bytes)) {
		core_line_first = ocf_bytes_2_lines(cache, addr);
		core_line_last = ocf_bytes_2_lines(cache, addr + bytes - 1);
		core_line_count = core_line_last - core_line_first + 1;
	} else {
		core_line_first = ocf_bytes_2_lines(cache, addr);
		core_line_last = core_line_first;
		core_line_count = 1;
	}

	req = env_mpool_new(cache->owner->resources.req, core_line_count);
	if (!req) {
		map_allocated = false;
		req = env_mpool_new(cache->owner->resources.req, 1);
	}

	if (unlikely(!req))
		return NULL;

	if (map_allocated) {
		req->map = req->__map;
		req->alock_status = (uint8_t*)&req->__map[core_line_count];
		req->alloc_core_line_count = core_line_count;
	} else {
		req->alloc_core_line_count = 1;
	}


	OCF_DEBUG_TRACE(cache);

	ocf_queue_get(queue);
	req->io_queue = queue;

	req->core = core;
	req->cache = cache;

	req->d2c = (queue != cache->mngt_queue) && !ocf_refcnt_inc(
			&cache->refcnt.metadata);

	env_atomic_set(&req->ref_count, 1);

	req->byte_position = addr;
	req->byte_length = bytes;
	req->core_line_first = core_line_first;
	req->core_line_last = core_line_last;
	req->core_line_count = core_line_count;
	req->rw = rw;
	req->part_id = PARTITION_DEFAULT;

	req->discard.sector = BYTES_TO_SECTORS(addr);
	req->discard.nr_sects = BYTES_TO_SECTORS(bytes);
	req->discard.handled = 0;

	req->lock_idx = ocf_metadata_concurrency_next_idx(queue);

	return req;
}

int ocf_req_alloc_map(struct ocf_request *req)
{
	if (req->map)
		return 0;

	req->map = env_zalloc(ocf_req_sizeof_map(req) +
			ocf_req_sizeof_alock_status(req), ENV_MEM_NOIO);
	if (!req->map) {
		req->error = -OCF_ERR_NO_MEM;
		return -OCF_ERR_NO_MEM;
	}

	req->alock_status = &((uint8_t*)req->map)[ocf_req_sizeof_map(req)];

	return 0;
}

int ocf_req_alloc_map_discard(struct ocf_request *req)
{
	ENV_BUILD_BUG_ON(MAX_TRIM_RQ_SIZE / ocf_cache_line_size_4 *
			sizeof(struct ocf_map_info) > 4 * KiB);

	if (req->byte_length <= MAX_TRIM_RQ_SIZE)
		return ocf_req_alloc_map(req);

	/*
	 * NOTE: For cache line size bigger than 8k a single-allocation mapping
	 * can handle more than MAX_TRIM_RQ_SIZE, so for these cache line sizes
	 * discard request uses only part of the mapping array.
	 */
	req->byte_length = MAX_TRIM_RQ_SIZE;
	req->core_line_last = ocf_bytes_2_lines(req->cache,
			req->byte_position + req->byte_length - 1);
	req->core_line_count = req->core_line_last - req->core_line_first + 1;

	return ocf_req_alloc_map(req);
}

struct ocf_request *ocf_req_new_extended(ocf_queue_t queue, ocf_core_t core,
		uint64_t addr, uint32_t bytes, int rw)
{
	struct ocf_request *req;

	req = ocf_req_new(queue, core, addr, bytes, rw);

	if (likely(req) && ocf_req_alloc_map(req)) {
		ocf_req_put(req);
		return NULL;
	}

	return req;
}

struct ocf_request *ocf_req_new_discard(ocf_queue_t queue, ocf_core_t core,
		uint64_t addr, uint32_t bytes, int rw)
{
	struct ocf_request *req;

	req = ocf_req_new_extended(queue, core, addr,
			OCF_MIN(bytes, MAX_TRIM_RQ_SIZE), rw);
	if (!req)
		return NULL;

	return req;
}

void ocf_req_get(struct ocf_request *req)
{
	OCF_DEBUG_TRACE(req->cache);

	env_atomic_inc(&req->ref_count);
}

void ocf_req_put(struct ocf_request *req)
{
	ocf_queue_t queue = req->io_queue;

	if (env_atomic_dec_return(&req->ref_count))
		return;

	OCF_DEBUG_TRACE(req->cache);

	if (!req->d2c && req->io_queue != req->cache->mngt_queue)
		ocf_refcnt_dec(&req->cache->refcnt.metadata);

	if (req->map != req->__map)
		env_free(req->map);

	env_mpool_del(req->cache->owner->resources.req, req,
			req->alloc_core_line_count);

	ocf_queue_put(queue);
}

int ocf_req_set_dirty(struct ocf_request *req)
{
	req->dirty = !!ocf_refcnt_inc(&req->cache->refcnt.dirty);
	return req->dirty ? 0 : -OCF_ERR_AGAIN;
}

void ocf_req_clear_info(struct ocf_request *req)
{
	ENV_BUG_ON(env_memset(&req->info, sizeof(req->info), 0));
}

void ocf_req_clear_map(struct ocf_request *req)
{
	if (likely(req->map))
		ENV_BUG_ON(env_memset(req->map,
			   sizeof(req->map[0]) * req->core_line_count, 0));
}

void ocf_req_hash(struct ocf_request *req)
{
	int i;

	for (i = 0; i < req->core_line_count; i++) {
		req->map[i].hash = ocf_metadata_hash_func(req->cache,
				req->core_line_first + i,
				ocf_core_get_id(req->core));
	}
}
