/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "metadata.h"
#include "metadata_io.h"
#include "metadata_segment_id.h"
#include "metadata_raw.h"
#include "metadata_raw_atomic.h"
#include "../utils/utils_io.h"
#include "../utils/utils_cache_line.h"
#include "../ocf_def_priv.h"

#define OCF_METADATA_RAW_ATOMIC_DEBUG 0

#if 1 == OCF_METADATA_RAW_ATOMIC_DEBUG
#define OCF_DEBUG_TRACE(cache) \
	ocf_cache_log(cache, log_info, "[Metadata][Raw][Atomic] %s\n", __func__)

#define OCF_DEBUG_MSG(cache, msg) \
	ocf_cache_log(cache, log_info, "[Metadata][Raw][Atomic] %s - %s\n", \
			__func__, msg)

#define OCF_DEBUG_PARAM(cache, format, ...) \
	ocf_cache_log(cache, log_info, "[Metadata][Raw][Atomic] %s - "format"\n", \
			__func__, ##__VA_ARGS__)
#else
#define OCF_DEBUG_TRACE(cache)
#define OCF_DEBUG_MSG(cache, msg)
#define OCF_DEBUG_PARAM(cache, format, ...)
#endif

struct _raw_atomic_flush_ctx {
	struct ocf_request *req;
	ocf_req_end_t complete;
	env_atomic flush_req_cnt;
};

static void _raw_atomic_io_discard_cmpl(struct _raw_atomic_flush_ctx *ctx,
		int error)
{
	if (error)
		ctx->req->error = error;

	if (env_atomic_dec_return(&ctx->flush_req_cnt))
		return;

	if (ctx->req->error)
		ocf_metadata_error(ctx->req->cache);

	/* Call metadata flush completed call back */
	OCF_DEBUG_MSG(cache, "Asynchronous flushing complete");

	ctx->complete(ctx->req, ctx->req->error);

	env_free(ctx);
}

static void _raw_atomic_io_discard_end(struct ocf_io *io, int error)
{
	struct _raw_atomic_flush_ctx *ctx = io->priv1;

	ocf_io_put(io); /* Release IO */

	_raw_atomic_io_discard_cmpl(ctx, error);
}

static int _raw_atomic_io_discard_do(struct ocf_cache *cache, void *context,
		uint64_t start_addr, uint32_t len, struct _raw_atomic_flush_ctx *ctx)
{
	struct ocf_request *req = context;
	struct ocf_io *io;

	io = ocf_new_cache_io(cache, NULL, start_addr, len, OCF_WRITE, 0, 0);
	if (!io) {
		req->error = -OCF_ERR_NO_MEM;
		return req->error;
	}

	OCF_DEBUG_PARAM(cache, "Page to flushing = %u, count of pages = %u",
			start_line, len);

	env_atomic_inc(&ctx->flush_req_cnt);

	ocf_io_set_cmpl(io, ctx, NULL, _raw_atomic_io_discard_end);

	if (cache->device->volume.features.discard_zeroes)
		ocf_volume_submit_discard(io);
	else
		ocf_volume_submit_write_zeroes(io);

	return req->error;
}

void raw_atomic_flush_mark(struct ocf_cache *cache, struct ocf_request *req,
		uint32_t map_idx, int to_state, uint8_t start, uint8_t stop)
{
	if (to_state == INVALID) {
		req->map[map_idx].flush = true;
		req->map[map_idx].start_flush = start;
		req->map[map_idx].stop_flush = stop;
		req->info.flush_metadata = true;
	}
}

#define MAX_STACK_TAB_SIZE 32

static inline void _raw_atomic_add_page(struct ocf_cache *cache,
		uint32_t *clines_tab, uint64_t line, int *idx)
{
	clines_tab[*idx] = ocf_metadata_map_lg2phy(cache, line);
	(*idx)++;
}

static int _raw_atomic_flush_do_asynch_sec(struct ocf_cache *cache,
		struct ocf_request *req, int map_idx,
		struct _raw_atomic_flush_ctx *ctx)
{
	struct ocf_map_info *map = &req->map[map_idx];
	uint32_t len = 0;
	uint64_t start_addr;
	int result = 0;

	start_addr = ocf_metadata_map_lg2phy(cache, map->coll_idx);
	start_addr *= ocf_line_size(cache);
	start_addr += cache->device->metadata_offset;

	start_addr += SECTORS_TO_BYTES(map->start_flush);
	len = SECTORS_TO_BYTES(map->stop_flush - map->start_flush);
	len += SECTORS_TO_BYTES(1);

	result = _raw_atomic_io_discard_do(cache, req, start_addr, len, ctx);

	return result;
}

int raw_atomic_flush_do_asynch(struct ocf_cache *cache, struct ocf_request *req,
		struct ocf_metadata_raw *raw, ocf_req_end_t complete)
{
	int result = 0, i;
	uint32_t __clines_tab[MAX_STACK_TAB_SIZE];
	uint32_t *clines_tab;
	int clines_to_flush = 0;
	uint32_t len = 0;
	int line_no = req->core_line_count;
	struct ocf_map_info *map;
	uint64_t start_addr;
	struct _raw_atomic_flush_ctx *ctx;

	ENV_BUG_ON(!complete);

	if (!req->info.flush_metadata) {
		/* Nothing to flush call flush callback */
		complete(req, 0);
		return 0;
	}

	ctx = env_zalloc(sizeof(*ctx), ENV_MEM_NOIO);
	if (!ctx) {
		complete(req, -OCF_ERR_NO_MEM);
		return -OCF_ERR_NO_MEM;
	}

	ctx->req = req;
	ctx->complete = complete;
	env_atomic_set(&ctx->flush_req_cnt, 1);

	if (line_no == 1) {
		map = &req->map[0];
		if (map->flush && map->status != LOOKUP_MISS) {
			result = _raw_atomic_flush_do_asynch_sec(cache, req,
					0, ctx);
		}
		_raw_atomic_io_discard_cmpl(ctx, result);
		return result;
	}

	if (line_no <= MAX_STACK_TAB_SIZE) {
		clines_tab = __clines_tab;
	} else {
		clines_tab = env_zalloc(sizeof(*clines_tab) * line_no,
				ENV_MEM_NOIO);
		if (!clines_tab) {
			complete(req, -OCF_ERR_NO_MEM);
			env_free(ctx);
			return -OCF_ERR_NO_MEM;
		}
	}

	for (i = 0; i < line_no; i++) {
		map = &req->map[i];

		if (!map->flush || map->status == LOOKUP_MISS)
			continue;

		if (i == 0) {
			/* First */
			if (map->start_flush) {
				_raw_atomic_flush_do_asynch_sec(cache, req, i,
						ctx);
			} else {
				_raw_atomic_add_page(cache, clines_tab,
					map->coll_idx, &clines_to_flush);
			}
		} else if (i == (line_no - 1)) {
			/* Last */
			if (map->stop_flush != ocf_line_end_sector(cache)) {
				_raw_atomic_flush_do_asynch_sec(cache, req,
						i, ctx);
			} else {
				_raw_atomic_add_page(cache, clines_tab,
					map->coll_idx, &clines_to_flush);
			}
		} else {
			/* Middle */
			_raw_atomic_add_page(cache, clines_tab, map->coll_idx,
					&clines_to_flush);
		}

	}

	env_sort(clines_tab, clines_to_flush, sizeof(*clines_tab),
			_raw_ram_flush_do_page_cmp, NULL);

	i = 0;
	while (i < clines_to_flush) {
		start_addr = clines_tab[i];
		start_addr *= ocf_line_size(cache);
		start_addr += cache->device->metadata_offset;
		len = ocf_line_size(cache);

		while (true) {
			if ((i + 1) >= clines_to_flush)
				break;

			if ((clines_tab[i] + 1) != clines_tab[i + 1])
				break;

			i++;
			len += ocf_line_size(cache);
		}

		result  |= _raw_atomic_io_discard_do(cache, req, start_addr,
				len, ctx);

		if (result)
			break;

		i++;
	}

	_raw_atomic_io_discard_cmpl(ctx, result);

	if (line_no > MAX_STACK_TAB_SIZE)
		env_free(clines_tab);

	return result;
}
