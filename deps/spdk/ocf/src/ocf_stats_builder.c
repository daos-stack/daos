/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "ocf_priv.h"
#include "metadata/metadata.h"
#include "engine/cache_engine.h"
#include "utils/utils_user_part.h"
#include "utils/utils_cache_line.h"
#include "utils/utils_stats.h"

static void _fill_req(struct ocf_stats_requests *req, struct ocf_stats_core *s)
{
	uint64_t serviced = s->read_reqs.total + s->write_reqs.total;
	uint64_t total = serviced + s->read_reqs.pass_through +
			s->write_reqs.pass_through;
	uint64_t hit;

	/* Reads Section */
	hit = s->read_reqs.total - (s->read_reqs.full_miss +
			s->read_reqs.partial_miss);
	_set(&req->rd_hits, hit, total);
	_set(&req->rd_partial_misses, s->read_reqs.partial_miss, total);
	_set(&req->rd_full_misses, s->read_reqs.full_miss, total);
	_set(&req->rd_total, s->read_reqs.total, total);

	/* Write Section */
	hit = s->write_reqs.total - (s->write_reqs.full_miss +
					s->write_reqs.partial_miss);
	_set(&req->wr_hits, hit, total);
	_set(&req->wr_partial_misses, s->write_reqs.partial_miss, total);
	_set(&req->wr_full_misses, s->write_reqs.full_miss, total);
	_set(&req->wr_total, s->write_reqs.total, total);

	/* Pass-Through section */
	_set(&req->rd_pt, s->read_reqs.pass_through, total);
	_set(&req->wr_pt, s->write_reqs.pass_through, total);

	/* Summary */
	_set(&req->serviced, serviced, total);
	_set(&req->total, total, total);
}

static void _fill_req_part(struct ocf_stats_requests *req,
		struct ocf_stats_io_class *s)
{
	uint64_t serviced = s->read_reqs.total + s->write_reqs.total;
	uint64_t total = serviced + s->read_reqs.pass_through +
			s->write_reqs.pass_through;
	uint64_t hit;

	/* Reads Section */
	hit = s->read_reqs.total - (s->read_reqs.full_miss +
			s->read_reqs.partial_miss);
	_set(&req->rd_hits, hit, total);
	_set(&req->rd_partial_misses, s->read_reqs.partial_miss, total);
	_set(&req->rd_full_misses, s->read_reqs.full_miss, total);
	_set(&req->rd_total, s->read_reqs.total, total);

	/* Write Section */
	hit = s->write_reqs.total - (s->write_reqs.full_miss +
					s->write_reqs.partial_miss);
	_set(&req->wr_hits, hit, total);
	_set(&req->wr_partial_misses, s->write_reqs.partial_miss, total);
	_set(&req->wr_full_misses, s->write_reqs.full_miss, total);
	_set(&req->wr_total, s->write_reqs.total, total);

	/* Pass-Through section */
	_set(&req->rd_pt, s->read_reqs.pass_through, total);
	_set(&req->wr_pt, s->write_reqs.pass_through, total);

	/* Summary */
	_set(&req->serviced, serviced, total);
	_set(&req->total, total, total);
}

static void _fill_blocks(struct ocf_stats_blocks *blocks,
		struct ocf_stats_core *s)
{
	uint64_t rd, wr, total;

	/* Core volume */
	rd = _bytes4k(s->core_volume.read);
	wr = _bytes4k(s->core_volume.write);
	total = rd + wr;
	_set(&blocks->core_volume_rd, rd, total);
	_set(&blocks->core_volume_wr, wr, total);
	_set(&blocks->core_volume_total, total, total);

	/* Cache volume */
	rd = _bytes4k(s->cache_volume.read);
	wr = _bytes4k(s->cache_volume.write);
	total = rd + wr;
	_set(&blocks->cache_volume_rd, rd, total);
	_set(&blocks->cache_volume_wr, wr, total);
	_set(&blocks->cache_volume_total, total, total);

	/* Core (cache volume) */
	rd = _bytes4k(s->core.read);
	wr = _bytes4k(s->core.write);
	total = rd + wr;
	_set(&blocks->volume_rd, rd, total);
	_set(&blocks->volume_wr, wr, total);
	_set(&blocks->volume_total, total, total);
}

static void _fill_blocks_part(struct ocf_stats_blocks *blocks,
		struct ocf_stats_io_class *s)
{
	uint64_t rd, wr, total;

	/* Core volume */
	rd = _bytes4k(s->core_blocks.read);
	wr = _bytes4k(s->core_blocks.write);
	total = rd + wr;
	_set(&blocks->core_volume_rd, rd, total);
	_set(&blocks->core_volume_wr, wr, total);
	_set(&blocks->core_volume_total, total, total);

	/* Cache volume */
	rd = _bytes4k(s->cache_blocks.read);
	wr = _bytes4k(s->cache_blocks.write);
	total = rd + wr;
	_set(&blocks->cache_volume_rd, rd, total);
	_set(&blocks->cache_volume_wr, wr, total);
	_set(&blocks->cache_volume_total, total, total);

	/* Core (cache volume) */
	rd = _bytes4k(s->blocks.read);
	wr = _bytes4k(s->blocks.write);
	total = rd + wr;
	_set(&blocks->volume_rd, rd, total);
	_set(&blocks->volume_wr, wr, total);
	_set(&blocks->volume_total, total, total);
}

static void _fill_errors(struct ocf_stats_errors *errors,
		struct ocf_stats_core *s)
{
	uint64_t rd, wr, total;

	rd = s->core_errors.read;
	wr = s->core_errors.write;
	total = rd + wr;
	_set(&errors->core_volume_rd, rd, total);
	_set(&errors->core_volume_wr, wr, total);
	_set(&errors->core_volume_total, total, total);

	rd = s->cache_errors.read;
	wr = s->cache_errors.write;
	total = rd + wr;
	_set(&errors->cache_volume_rd, rd, total);
	_set(&errors->cache_volume_wr, wr, total);
	_set(&errors->cache_volume_total, total, total);

	total = s->core_errors.read + s->core_errors.write +
		s->cache_errors.read + s->cache_errors.write;

	_set(&errors->total, total, total);
}

static void _accumulate_block(struct ocf_stats_block *to,
		const struct ocf_stats_block *from)
{
	to->read += from->read;
	to->write += from->write;
}

static void _accumulate_reqs(struct ocf_stats_req *to,
		const struct ocf_stats_req *from)
{
	to->full_miss += from->full_miss;
	to->partial_miss += from->partial_miss;
	to->total += from->total;
	to->pass_through += from->pass_through;
}

static void _accumulate_errors(struct ocf_stats_error *to,
		const struct ocf_stats_error *from)
{
	to->read += from->read;
	to->write += from->write;
}

struct io_class_stats_context {
	struct ocf_stats_io_class *stats;
	ocf_part_id_t part_id;
};

static int _accumulate_io_class_stats(ocf_core_t core, void *cntx)
{
	int result;
	struct ocf_stats_io_class stats;
	struct ocf_stats_io_class *total =
		((struct io_class_stats_context*)cntx)->stats;
	ocf_part_id_t part_id = ((struct io_class_stats_context*)cntx)->part_id;

	result = ocf_core_io_class_get_stats(core, part_id, &stats);
	if (result)
		return result;

	total->occupancy_clines += stats.occupancy_clines;
	total->dirty_clines += stats.dirty_clines;
	total->free_clines = stats.free_clines;

	_accumulate_block(&total->cache_blocks, &stats.cache_blocks);
	_accumulate_block(&total->core_blocks, &stats.core_blocks);
	_accumulate_block(&total->blocks, &stats.blocks);

	_accumulate_reqs(&total->read_reqs, &stats.read_reqs);
	_accumulate_reqs(&total->write_reqs, &stats.write_reqs);

	return 0;
}

static void _ocf_stats_part_fill(ocf_cache_t cache, ocf_part_id_t part_id,
		struct ocf_stats_io_class *stats , struct ocf_stats_usage *usage,
		struct ocf_stats_requests *req, struct ocf_stats_blocks *blocks)
{
	uint64_t cache_size, cache_line_size;

	cache_line_size = ocf_cache_get_line_size(cache);
	cache_size = cache->conf_meta->cachelines;

	if (usage) {
		_set(&usage->occupancy,
			_lines4k(stats->occupancy_clines, cache_line_size),
			_lines4k(cache_size, cache_line_size));

		_set(&usage->free,
			_lines4k(stats->free_clines, cache_line_size),
			_lines4k(cache_size, cache_line_size));

		_set(&usage->clean,
			_lines4k(stats->occupancy_clines - stats->dirty_clines,
				cache_line_size),
			_lines4k(stats->occupancy_clines, cache_line_size));

		_set(&usage->dirty,
			_lines4k(stats->dirty_clines, cache_line_size),
			_lines4k(stats->occupancy_clines, cache_line_size));
	}

	if (req)
		_fill_req_part(req, stats);

	if (blocks)
		_fill_blocks_part(blocks, stats);
}

int ocf_stats_collect_part_core(ocf_core_t core, ocf_part_id_t part_id,
		struct ocf_stats_usage *usage, struct ocf_stats_requests *req,
		struct ocf_stats_blocks *blocks)
{
	struct ocf_stats_io_class s;
	ocf_cache_t cache;
	int result = 0;

	OCF_CHECK_NULL(core);

	if (part_id > OCF_IO_CLASS_ID_MAX)
		return -OCF_ERR_INVAL;

	cache = ocf_core_get_cache(core);

	_ocf_stats_zero(usage);
	_ocf_stats_zero(req);
	_ocf_stats_zero(blocks);

	result = ocf_core_io_class_get_stats(core, part_id, &s);
	if (result)
		return result;

	_ocf_stats_part_fill(cache, part_id, &s, usage, req, blocks);

	return result;
}

int ocf_stats_collect_part_cache(ocf_cache_t cache, ocf_part_id_t part_id,
		struct ocf_stats_usage *usage, struct ocf_stats_requests *req,
		struct ocf_stats_blocks *blocks)
{
	struct io_class_stats_context ctx;
	struct ocf_stats_io_class s = {};
	int result = 0;

	OCF_CHECK_NULL(cache);

	if (part_id > OCF_IO_CLASS_ID_MAX)
		return -OCF_ERR_INVAL;

	_ocf_stats_zero(usage);
	_ocf_stats_zero(req);
	_ocf_stats_zero(blocks);

	ctx.part_id = part_id;
	ctx.stats = &s;

	result = ocf_core_visit(cache, _accumulate_io_class_stats, &ctx, true);
	if (result)
		return result;

	_ocf_stats_part_fill(cache, part_id, &s, usage, req, blocks);

	return result;
}

int ocf_stats_collect_core(ocf_core_t core,
		struct ocf_stats_usage *usage,
		struct ocf_stats_requests *req,
		struct ocf_stats_blocks *blocks,
		struct ocf_stats_errors *errors)
{
	ocf_cache_t cache;
	uint64_t cache_occupancy, cache_size, cache_line_size;
	struct ocf_stats_core s;
	int result;

	OCF_CHECK_NULL(core);

	result = ocf_core_get_stats(core, &s);
	if (result)
		return result;

	cache = ocf_core_get_cache(core);
	cache_line_size = ocf_cache_get_line_size(cache);
	cache_size = cache->conf_meta->cachelines;
	cache_occupancy = ocf_get_cache_occupancy(cache);

	_ocf_stats_zero(usage);
	_ocf_stats_zero(req);
	_ocf_stats_zero(blocks);
	_ocf_stats_zero(errors);

	if (usage) {
		_set(&usage->occupancy,
			_lines4k(s.cache_occupancy, cache_line_size),
			_lines4k(cache_size, cache_line_size));

		_set(&usage->free,
			_lines4k(cache_size - cache_occupancy, cache_line_size),
			_lines4k(cache_size, cache_line_size));

		_set(&usage->clean,
			_lines4k(s.cache_occupancy - s.dirty, cache_line_size),
			_lines4k(s.cache_occupancy, cache_line_size));

		_set(&usage->dirty,
			_lines4k(s.dirty, cache_line_size),
			_lines4k(s.cache_occupancy, cache_line_size));
	}

	if (req)
		_fill_req(req, &s);

	if (blocks)
		_fill_blocks(blocks, &s);

	if (errors)
		_fill_errors(errors, &s);

	return 0;
}

static int _accumulate_stats(ocf_core_t core, void *cntx)
{
	struct ocf_stats_core stats, *total = cntx;
	int result;

	result = ocf_core_get_stats(core, &stats);
	if (result)
		return result;

	_accumulate_block(&total->cache_volume, &stats.cache_volume);
	_accumulate_block(&total->core_volume, &stats.core_volume);
	_accumulate_block(&total->core, &stats.core);

	_accumulate_reqs(&total->read_reqs, &stats.read_reqs);
	_accumulate_reqs(&total->write_reqs, &stats.write_reqs);

	_accumulate_errors(&total->cache_errors, &stats.cache_errors);
	_accumulate_errors(&total->core_errors, &stats.core_errors);

	return 0;
}

int ocf_stats_collect_cache(ocf_cache_t cache,
		struct ocf_stats_usage *usage,
		struct ocf_stats_requests *req,
		struct ocf_stats_blocks *blocks,
		struct ocf_stats_errors *errors)
{
	uint64_t cache_line_size;
	struct ocf_cache_info info;
	struct ocf_stats_core s = { 0 };
	int result;

	OCF_CHECK_NULL(cache);

	result = ocf_cache_get_info(cache, &info);
	if (result)
		return result;

	cache_line_size = ocf_cache_get_line_size(cache);

	_ocf_stats_zero(usage);
	_ocf_stats_zero(req);
	_ocf_stats_zero(blocks);
	_ocf_stats_zero(errors);

	result = ocf_core_visit(cache, _accumulate_stats, &s, true);
	if (result)
		return result;

	if (usage) {
		_set(&usage->occupancy,
			_lines4k(info.occupancy, cache_line_size),
			_lines4k(info.size, cache_line_size));

		_set(&usage->free,
			_lines4k(info.size - info.occupancy, cache_line_size),
			_lines4k(info.size, cache_line_size));

		_set(&usage->clean,
			_lines4k(info.occupancy - info.dirty, cache_line_size),
			_lines4k(info.size, cache_line_size));

		_set(&usage->dirty,
			_lines4k(info.dirty, cache_line_size),
			_lines4k(info.size, cache_line_size));
	}

	if (req)
		_fill_req(req, &s);

	if (blocks)
		_fill_blocks(blocks, &s);

	if (errors)
		_fill_errors(errors, &s);

	return 0;
}
