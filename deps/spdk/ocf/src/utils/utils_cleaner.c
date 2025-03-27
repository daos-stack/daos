/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "../metadata/metadata.h"
#include "../engine/cache_engine.h"
#include "../engine/engine_common.h"
#include "../concurrency/ocf_concurrency.h"
#include "../ocf_request.h"
#include "utils_cleaner.h"
#include "utils_user_part.h"
#include "utils_io.h"
#include "utils_cache_line.h"
#include "../ocf_queue_priv.h"

#define OCF_UTILS_CLEANER_DEBUG 0

#if 1 == OCF_UTILS_CLEANER_DEBUG
#define OCF_DEBUG_TRACE(cache) \
	ocf_cache_log(cache, log_info, "[Utils][cleaner] %s\n", __func__)

#define OCF_DEBUG_MSG(cache, msg) \
	ocf_cache_log(cache, log_info, "[Utils][cleaner] %s - %s\n", \
			__func__, msg)

#define OCF_DEBUG_PARAM(cache, format, ...) \
	ocf_cache_log(cache, log_info, "[Utils][cleaner] %s - "format"\n", \
			__func__, ##__VA_ARGS__)
#else
#define OCF_DEBUG_TRACE(cache)
#define OCF_DEBUG_MSG(cache, msg)
#define OCF_DEBUG_PARAM(cache, format, ...)
#endif

/*
 * Allocate cleaning request
 */
static struct ocf_request *_ocf_cleaner_alloc_req(struct ocf_cache *cache,
		uint32_t count, const struct ocf_cleaner_attribs *attribs)
{
	struct ocf_request *req = ocf_req_new_extended(attribs->io_queue, NULL,
			0, count * ocf_line_size(cache), OCF_READ);
	int ret;

	if (!req)
		return NULL;

	req->info.internal = true;
	req->info.cleaner_cache_line_lock = attribs->lock_cacheline;

	/* Allocate pages for cleaning IO */
	req->data = ctx_data_alloc(cache->owner,
			ocf_line_size(cache) / PAGE_SIZE * count);
	if (!req->data) {
		ocf_req_put(req);
		return NULL;
	}

	ret = ctx_data_mlock(cache->owner, req->data);
	if (ret) {
		ctx_data_free(cache->owner, req->data);
		ocf_req_put(req);
		return NULL;
	}

	return req;
}

enum {
	ocf_cleaner_req_type_master = 1,
	ocf_cleaner_req_type_slave = 2
};

static struct ocf_request *_ocf_cleaner_alloc_master_req(
	struct ocf_cache *cache, uint32_t count,
	const struct ocf_cleaner_attribs *attribs)
{
	struct ocf_request *req = _ocf_cleaner_alloc_req(cache, count, attribs);

	if (req) {
		/* Set type of cleaning request */
		req->master_io_req_type = ocf_cleaner_req_type_master;

		/* In master, save completion context and function */
		req->priv = attribs->cmpl_context;
		req->master_io_req = attribs->cmpl_fn;

		/* The count of all requests */
		env_atomic_set(&req->master_remaining, 1);

		OCF_DEBUG_PARAM(cache, "New master request, count = %u",
				count);
	}
	return req;
}

static struct ocf_request *_ocf_cleaner_alloc_slave_req(
		struct ocf_request *master,
		uint32_t count, const struct ocf_cleaner_attribs *attribs)
{
	struct ocf_request *req = _ocf_cleaner_alloc_req(
			master->cache, count, attribs);

	if (req) {
		/* Set type of cleaning request */
		req->master_io_req_type = ocf_cleaner_req_type_slave;

		/* Slave refers to master request, get its reference counter */
		ocf_req_get(master);

		/* Slave request contains reference to master */
		req->master_io_req = master;

		/* One more additional slave request, increase global counter
		 * of requests count
		 */
		env_atomic_inc(&master->master_remaining);

		OCF_DEBUG_PARAM(req->cache,
			"New slave request, count = %u,all requests count = %d",
			count, env_atomic_read(&master->master_remaining));
	}
	return req;
}

static void _ocf_cleaner_dealloc_req(struct ocf_request *req)
{
	if (ocf_cleaner_req_type_slave == req->master_io_req_type) {
		/* Slave contains reference to the master request,
		 * release reference counter
		 */
		struct ocf_request *master = req->master_io_req;

		OCF_DEBUG_MSG(req->cache, "Put master request by slave");
		ocf_req_put(master);

		OCF_DEBUG_MSG(req->cache, "Free slave request");
	} else if (ocf_cleaner_req_type_master == req->master_io_req_type) {
		OCF_DEBUG_MSG(req->cache, "Free master request");
	} else {
		ENV_BUG();
	}

	ctx_data_secure_erase(req->cache->owner, req->data);
	ctx_data_munlock(req->cache->owner, req->data);
	ctx_data_free(req->cache->owner, req->data);
	ocf_req_put(req);
}

/*
 * cleaner - Get clean result
 */
static void _ocf_cleaner_set_error(struct ocf_request *req)
{
	struct ocf_request *master = NULL;

	if (ocf_cleaner_req_type_master == req->master_io_req_type) {
		master = req;
	} else if (ocf_cleaner_req_type_slave == req->master_io_req_type) {
		master = req->master_io_req;
	} else {
		ENV_BUG();
		return;
	}

	master->error = -OCF_ERR_IO;
}

static void _ocf_cleaner_complete_req(struct ocf_request *req)
{
	struct ocf_request *master = NULL;
	ocf_req_end_t cmpl;

	if (ocf_cleaner_req_type_master == req->master_io_req_type) {
		OCF_DEBUG_MSG(req->cache, "Master completion");
		master = req;
	} else if (ocf_cleaner_req_type_slave == req->master_io_req_type) {
		OCF_DEBUG_MSG(req->cache, "Slave completion");
		master = req->master_io_req;
	} else {
		ENV_BUG();
		return;
	}

	OCF_DEBUG_PARAM(req->cache, "Master requests remaining = %d",
			env_atomic_read(&master->master_remaining));

	if (env_atomic_dec_return(&master->master_remaining)) {
		/* Not all requests completed */
		return;
	}

	OCF_DEBUG_MSG(req->cache, "All cleaning request completed");

	/* Only master contains completion function and completion context */
	cmpl = master->master_io_req;
	cmpl(master->priv, master->error);
}

static void _ocf_cleaner_on_resume(struct ocf_request *req)
{
	OCF_DEBUG_TRACE(req->cache);
	ocf_engine_push_req_front(req, true);
}

/*
 * cleaner - Cache line lock, function lock cache lines depends on attributes
 */
static int _ocf_cleaner_cache_line_lock(struct ocf_request *req)
{
	if (!req->info.cleaner_cache_line_lock)
		return OCF_LOCK_ACQUIRED;

	OCF_DEBUG_TRACE(req->cache);

	return ocf_req_async_lock_rd(ocf_cache_line_concurrency(req->cache),
			req, _ocf_cleaner_on_resume);
}

/*
 * cleaner - Cache line unlock, function unlock cache lines
 * depends on attributes
 */
static void _ocf_cleaner_cache_line_unlock(struct ocf_request *req)
{
	if (req->info.cleaner_cache_line_lock) {
		OCF_DEBUG_TRACE(req->cache);
		ocf_req_unlock(req->cache->device->concurrency.cache_line,
				req);
	}
}

static bool _ocf_cleaner_sector_is_dirty(struct ocf_cache *cache,
		ocf_cache_line_t line, uint8_t sector)
{
	bool dirty = metadata_test_dirty_one(cache, line, sector);
	bool valid = metadata_test_valid_one(cache, line, sector);

	if (!valid && dirty) {
		/* not valid but dirty - IMPROPER STATE!!! */
		ENV_BUG();
	}

	return valid ? dirty : false;
}

static void _ocf_cleaner_finish_req(struct ocf_request *req)
{
	/* Handle cache lines unlocks */
	_ocf_cleaner_cache_line_unlock(req);

	/* Signal completion to the caller of cleaning */
	_ocf_cleaner_complete_req(req);

	/* Free allocated resources */
	_ocf_cleaner_dealloc_req(req);
}

static void _ocf_cleaner_flush_cache_io_end(struct ocf_io *io, int error)
{
	struct ocf_request *req = io->priv1;

	if (error) {
		ocf_metadata_error(req->cache);
		req->error = error;
	}

	OCF_DEBUG_MSG(req->cache, "Cache flush finished");

	_ocf_cleaner_finish_req(req);

	ocf_io_put(io);
}

static int _ocf_cleaner_fire_flush_cache(struct ocf_request *req)
{
	struct ocf_io *io;

	OCF_DEBUG_TRACE(req->cache);

	io = ocf_new_cache_io(req->cache, req->io_queue, 0, 0, OCF_WRITE, 0, 0);
	if (!io) {
		ocf_metadata_error(req->cache);
		req->error = -OCF_ERR_NO_MEM;
		return -OCF_ERR_NO_MEM;
	}

	ocf_io_set_cmpl(io, req, NULL, _ocf_cleaner_flush_cache_io_end);

	ocf_volume_submit_flush(io);

	return 0;
}

static const struct ocf_io_if _io_if_flush_cache = {
	.read = _ocf_cleaner_fire_flush_cache,
	.write = _ocf_cleaner_fire_flush_cache,
};

static void _ocf_cleaner_metadata_io_end(struct ocf_request *req, int error)
{
	if (error) {
		ocf_metadata_error(req->cache);
		req->error = error;
		_ocf_cleaner_finish_req(req);
		return;
	}

	OCF_DEBUG_MSG(req->cache, "Metadata flush finished");

	req->io_if = &_io_if_flush_cache;
	ocf_engine_push_req_front(req, true);
}

static int _ocf_cleaner_update_metadata(struct ocf_request *req)
{
	struct ocf_cache *cache = req->cache;
	const struct ocf_map_info *iter = req->map;
	uint32_t i;
	ocf_cache_line_t cache_line;
	ocf_core_id_t core_id;

	OCF_DEBUG_TRACE(req->cache);

	/* Update metadata */
	for (i = 0; i < req->core_line_count; i++, iter++) {
		if (iter->status == LOOKUP_MISS)
			continue;

		if (iter->invalid) {
			/* An error, do not clean */
			continue;
		}

		cache_line = iter->coll_idx;

		ocf_hb_cline_prot_lock_wr(&cache->metadata.lock,
				req->lock_idx, req->map[i].core_id,
				req->map[i].core_line);

		if (metadata_test_dirty(cache, cache_line)) {
			ocf_metadata_get_core_and_part_id(cache, cache_line,
					&core_id, &req->part_id);
			req->core = &cache->core[core_id];

			ocf_metadata_start_collision_shared_access(cache,
					cache_line);
			set_cache_line_clean(cache, 0,
					ocf_line_end_sector(cache), req, i);
			ocf_metadata_end_collision_shared_access(cache,
					cache_line);
		}

		ocf_hb_cline_prot_unlock_wr(&cache->metadata.lock,
				req->lock_idx, req->map[i].core_id,
				req->map[i].core_line);
	}

	ocf_metadata_flush_do_asynch(cache, req, _ocf_cleaner_metadata_io_end);
	return 0;
}

static const struct ocf_io_if _io_if_update_metadata = {
		.read = _ocf_cleaner_update_metadata,
		.write = _ocf_cleaner_update_metadata,
};

static void _ocf_cleaner_flush_cores_io_end(struct ocf_map_info *map,
		struct ocf_request *req, int error)
{
	uint32_t i;
	struct ocf_map_info *iter = req->map;

	if (error) {
		/* Flush error, set error for all cache line of this core */
		for (i = 0; i < req->core_line_count; i++, iter++) {
			if (iter->status == LOOKUP_MISS)
				continue;

			if (iter->core_id == map->core_id)
				iter->invalid = true;
		}

		_ocf_cleaner_set_error(req);
	}

	if (env_atomic_dec_return(&req->req_remaining))
		return;

	OCF_DEBUG_MSG(req->cache, "Core flush finished");

	/*
	 * All core writes done, switch to post cleaning activities
	 */
	req->io_if = &_io_if_update_metadata;
	ocf_engine_push_req_front(req, true);
}

static void _ocf_cleaner_flush_cores_io_cmpl(struct ocf_io *io, int error)
{
	_ocf_cleaner_flush_cores_io_end(io->priv1, io->priv2, error);

	ocf_io_put(io);
}

static int _ocf_cleaner_fire_flush_cores(struct ocf_request *req)
{
	uint32_t i;
	ocf_core_id_t core_id = OCF_CORE_MAX;
	struct ocf_cache *cache = req->cache;
	struct ocf_map_info *iter = req->map;
	ocf_core_t core;
	struct ocf_io *io;

	OCF_DEBUG_TRACE(req->cache);

	/* Protect IO completion race */
	env_atomic_set(&req->req_remaining, 1);

	/* Submit flush requests */
	for (i = 0; i < req->core_line_count; i++, iter++) {
		if (iter->invalid) {
			/* IO error, skip this item */
			continue;
		}

		if (iter->status == LOOKUP_MISS)
			continue;

		if (core_id == iter->core_id)
			continue;

		core_id = iter->core_id;

		env_atomic_inc(&req->req_remaining);

		core = ocf_cache_get_core(cache, core_id);
		io = ocf_new_core_io(core, req->io_queue, 0, 0,
				OCF_WRITE, 0, 0);
		if (!io) {
			_ocf_cleaner_flush_cores_io_end(iter, req, -OCF_ERR_NO_MEM);
			continue;
		}

		ocf_io_set_cmpl(io, iter, req, _ocf_cleaner_flush_cores_io_cmpl);

		ocf_volume_submit_flush(io);
	}

	/* Protect IO completion race */
	_ocf_cleaner_flush_cores_io_end(NULL, req, 0);

	return 0;
}

static const struct ocf_io_if _io_if_flush_cores = {
	.read = _ocf_cleaner_fire_flush_cores,
	.write = _ocf_cleaner_fire_flush_cores,
};

static void _ocf_cleaner_core_io_end(struct ocf_request *req)
{
	if (env_atomic_dec_return(&req->req_remaining))
		return;

	OCF_DEBUG_MSG(req->cache, "Core writes finished");

	/*
	 * All cache read requests done, now we can submit writes to cores,
	 * Move processing to thread, where IO will be (and can be) submitted
	 */
	req->io_if = &_io_if_flush_cores;
	ocf_engine_push_req_front(req, true);
}

static void _ocf_cleaner_core_io_cmpl(struct ocf_io *io, int error)
{
	struct ocf_map_info *map = io->priv1;
	struct ocf_request *req = io->priv2;
	ocf_core_t core = ocf_cache_get_core(req->cache, map->core_id);

	if (error) {
		map->invalid |= 1;
		_ocf_cleaner_set_error(req);
		ocf_core_stats_core_error_update(core, OCF_WRITE);
	}

	_ocf_cleaner_core_io_end(req);

	ocf_io_put(io);
}

static void _ocf_cleaner_core_io_for_dirty_range(struct ocf_request *req,
		struct ocf_map_info *iter, uint64_t begin, uint64_t end)
{
	uint64_t addr, offset;
	int err;
	ocf_cache_t cache = req->cache;
	struct ocf_io *io;
	ocf_core_t core = ocf_cache_get_core(cache, iter->core_id);
	ocf_part_id_t part_id = ocf_metadata_get_partition_id(cache,
			iter->coll_idx);

	addr = (ocf_line_size(cache) * iter->core_line)
			+ SECTORS_TO_BYTES(begin);
	offset = (ocf_line_size(cache) * iter->hash)
			+ SECTORS_TO_BYTES(begin);

	io = ocf_new_core_io(core, req->io_queue, addr,
			SECTORS_TO_BYTES(end - begin), OCF_WRITE, part_id, 0);
	if (!io)
		goto error;

	err = ocf_io_set_data(io, req->data, offset);
	if (err) {
		ocf_io_put(io);
		goto error;
	}

	ocf_io_set_cmpl(io, iter, req, _ocf_cleaner_core_io_cmpl);

	ocf_core_stats_core_block_update(core, part_id, OCF_WRITE,
			SECTORS_TO_BYTES(end - begin));

	OCF_DEBUG_PARAM(req->cache, "Core write, line = %llu, "
			"sector = %llu, count = %llu", iter->core_line, begin,
			end - begin);

	/* Increase IO counter to be processed */
	env_atomic_inc(&req->req_remaining);

	/* Send IO */
	ocf_volume_submit_io(io);

	return;
error:
	iter->invalid = true;
	_ocf_cleaner_set_error(req);
}

static void _ocf_cleaner_core_submit_io(struct ocf_request *req,
		struct ocf_map_info *iter)
{
	uint64_t i, dirty_start = 0;
	struct ocf_cache *cache = req->cache;
	bool counting_dirty = false;

	/* Check integrity of entry to be cleaned */
	if (metadata_test_valid(cache, iter->coll_idx)
		&& metadata_test_dirty(cache, iter->coll_idx)) {

		_ocf_cleaner_core_io_for_dirty_range(req, iter, 0,
				ocf_line_sectors(cache));

		return;
	}

	/* Sector cleaning, a little effort is required to this */
	for (i = 0; i < ocf_line_sectors(cache); i++) {
		if (!_ocf_cleaner_sector_is_dirty(cache, iter->coll_idx, i)) {
			if (counting_dirty) {
				counting_dirty = false;
				_ocf_cleaner_core_io_for_dirty_range(req, iter,
						dirty_start, i);
			}

			continue;
		}

		if (!counting_dirty) {
			counting_dirty = true;
			dirty_start = i;
		}

	}

	if (counting_dirty)
		_ocf_cleaner_core_io_for_dirty_range(req, iter, dirty_start, i);
}

static int _ocf_cleaner_fire_core(struct ocf_request *req)
{
	uint32_t i;
	struct ocf_map_info *iter;
	ocf_cache_t cache = req->cache;

	OCF_DEBUG_TRACE(req->cache);

	/* Protect IO completion race */
	env_atomic_set(&req->req_remaining, 1);

	/* Submits writes to the core */
	for (i = 0; i < req->core_line_count; i++) {
		iter = &(req->map[i]);

		if (iter->invalid) {
			/* IO read error on cache, skip this item */
			continue;
		}

		if (iter->status == LOOKUP_MISS)
			continue;

		ocf_hb_cline_prot_lock_rd(&cache->metadata.lock,
				req->lock_idx, req->map[i].core_id,
				req->map[i].core_line);

		_ocf_cleaner_core_submit_io(req, iter);

		ocf_hb_cline_prot_unlock_rd(&cache->metadata.lock,
				req->lock_idx, req->map[i].core_id,
				req->map[i].core_line);
	}

	/* Protect IO completion race */
	_ocf_cleaner_core_io_end(req);

	return 0;
}

static const struct ocf_io_if _io_if_fire_core = {
		.read = _ocf_cleaner_fire_core,
		.write = _ocf_cleaner_fire_core,
};

static void _ocf_cleaner_cache_io_end(struct ocf_request *req)
{
	if (env_atomic_dec_return(&req->req_remaining))
		return;

	/*
	 * All cache read requests done, now we can submit writes to cores,
	 * Move processing to thread, where IO will be (and can be) submitted
	 */
	req->io_if = &_io_if_fire_core;
	ocf_engine_push_req_front(req, true);

	OCF_DEBUG_MSG(req->cache, "Cache reads finished");
}

static void _ocf_cleaner_cache_io_cmpl(struct ocf_io *io, int error)
{
	struct ocf_map_info *map = io->priv1;
	struct ocf_request *req = io->priv2;
	ocf_core_t core = ocf_cache_get_core(req->cache, map->core_id);

	if (error) {
		map->invalid |= 1;
		_ocf_cleaner_set_error(req);
		ocf_core_stats_cache_error_update(core, OCF_READ);
	}

	_ocf_cleaner_cache_io_end(req);

	ocf_io_put(io);
}

/*
 * cleaner - Traverse cache lines to be cleaned, detect sequential IO, and
 * perform cache reads and core writes
 */
static int _ocf_cleaner_fire_cache(struct ocf_request *req)
{
	ocf_cache_t cache = req->cache;
	ocf_core_t core;
	uint32_t i;
	struct ocf_map_info *iter = req->map;
	uint64_t addr, offset;
	ocf_part_id_t part_id;
	struct ocf_io *io;
	int err;

	/* Protect IO completion race */
	env_atomic_inc(&req->req_remaining);

	for (i = 0; i < req->core_line_count; i++, iter++) {
		core = ocf_cache_get_core(cache, iter->core_id);
		if (!core)
			continue;
		if (iter->status == LOOKUP_MISS)
			continue;

		OCF_DEBUG_PARAM(req->cache, "Cache read, line =  %u",
				iter->coll_idx);

		addr = ocf_metadata_map_lg2phy(cache,
				iter->coll_idx);
		addr *= ocf_line_size(cache);
		addr += cache->device->metadata_offset;

		offset = ocf_line_size(cache) * iter->hash;

		part_id = ocf_metadata_get_partition_id(cache, iter->coll_idx);

		io = ocf_new_cache_io(cache, req->io_queue,
				addr, ocf_line_size(cache),
				OCF_READ, part_id, 0);
		if (!io) {
			/* Allocation error */
			iter->invalid = true;
			_ocf_cleaner_set_error(req);
			continue;
		}

		ocf_io_set_cmpl(io, iter, req, _ocf_cleaner_cache_io_cmpl);
		err = ocf_io_set_data(io, req->data, offset);
		if (err) {
			ocf_io_put(io);
			iter->invalid = true;
			_ocf_cleaner_set_error(req);
			continue;
		}

		ocf_core_stats_cache_block_update(core, part_id, OCF_READ,
				ocf_line_size(cache));

		ocf_volume_submit_io(io);
	}

	/* Protect IO completion race */
	_ocf_cleaner_cache_io_end(req);

	return 0;
}

static const struct ocf_io_if _io_if_fire_cache = {
	.read = _ocf_cleaner_fire_cache,
	.write = _ocf_cleaner_fire_cache,
};

static int _ocf_cleaner_fire(struct ocf_request *req)
{
	int result;

	req->io_if = &_io_if_fire_cache;

	/* Handle cache lines locks */
	result = _ocf_cleaner_cache_line_lock(req);

	if (result >= 0) {
		if (result == OCF_LOCK_ACQUIRED) {
			OCF_DEBUG_MSG(req->cache, "Lock acquired");
			_ocf_cleaner_fire_cache(req);
		} else {
			OCF_DEBUG_MSG(req->cache, "NO Lock");
		}
		return  0;
	} else {
		OCF_DEBUG_MSG(req->cache, "Lock error");
	}

	return result;
}

/* Helper function for 'sort' */
static int _ocf_cleaner_cmp_private(const void *a, const void *b)
{
	struct ocf_map_info *_a = (struct ocf_map_info *)a;
	struct ocf_map_info *_b = (struct ocf_map_info *)b;

	static uint32_t step = 0;

	OCF_COND_RESCHED_DEFAULT(step);

	if (_a->core_id == _b->core_id)
		return (_a->core_line > _b->core_line) ? 1 : -1;

	return (_a->core_id > _b->core_id) ? 1 : -1;
}

/**
 * Prepare cleaning request to be fired
 *
 * @param req cleaning request
 * @param i_out number of already filled map requests (remaining to be filled
 *    with missed
 */
static int _ocf_cleaner_do_fire(struct ocf_request *req,  uint32_t i_out,
		bool do_sort)
{
	uint32_t i;
	/* Set counts of cache IOs */
	env_atomic_set(&req->req_remaining, i_out);

	/* fill tail of a request with fake MISSes so that it won't
	 *  be cleaned
	 */
	for (; i_out < req->core_line_count; ++i_out) {
		req->map[i_out].core_id = OCF_CORE_MAX;
		req->map[i_out].core_line = ULLONG_MAX;
		req->map[i_out].status = LOOKUP_MISS;
		req->map[i_out].hash = i_out;
	}

	if (do_sort) {
		/* Sort by core id and core line */
		env_sort(req->map, req->core_line_count, sizeof(req->map[0]),
			_ocf_cleaner_cmp_private, NULL);
		for (i = 0; i < req->core_line_count; i++)
			req->map[i].hash = i;
	}

	/* issue actual request */
	return _ocf_cleaner_fire(req);
}

static inline uint32_t _ocf_cleaner_get_req_max_count(uint32_t count,
		bool low_mem)
{
	if (low_mem || count <= 4096)
		return count < 128 ? count : 128;

	return 1024;
}

static void _ocf_cleaner_fire_error(struct ocf_request *master,
		struct ocf_request *req, int err)
{
	master->error = err;
	_ocf_cleaner_complete_req(req);
	_ocf_cleaner_dealloc_req(req);
}

/*
 * cleaner - Main function
 */
void ocf_cleaner_fire(struct ocf_cache *cache,
		const struct ocf_cleaner_attribs *attribs)
{
	uint32_t i, i_out = 0, count = attribs->count;
	/* max cache lines to be cleaned with one request: 1024 if over 4k lines
	 * to be flushed, otherwise 128. for large cleaning operations, 1024 is
	 * optimal number, but for smaller 1024 is too large to benefit from
	 * cleaning request overlapping
	 */
	uint32_t max = _ocf_cleaner_get_req_max_count(count, false);
	ocf_cache_line_t cache_line;
	/* it is possible that more than one cleaning request will be generated
	 * for each cleaning order, thus multiple allocations. At the end of
	 * loop, req is set to zero and NOT deallocated, as deallocation is
	 * handled in completion.
	 * In addition first request we call master which contains completion
	 * contexts. Then succeeding request we call salve requests which
	 * contains reference to the master request
	 */
	struct ocf_request *req = NULL, *master;
	int err;
	ocf_core_id_t core_id;
	uint64_t core_sector;
	bool skip;

	/* Allocate master request */
	master = _ocf_cleaner_alloc_master_req(cache, max, attribs);

	if (!master) {
		/* Some memory allocation error, try re-allocate request */
		max = _ocf_cleaner_get_req_max_count(count, true);
		master = _ocf_cleaner_alloc_master_req(cache, max, attribs);
	}

	if (!master) {
		attribs->cmpl_fn(attribs->cmpl_context, -OCF_ERR_NO_MEM);
		return;
	}

	req = master;

	/* prevent cleaning completion race */
	ocf_req_get(master);
	env_atomic_inc(&master->master_remaining);

	for (i = 0; i < count; i++) {
		/* when request hasn't yet been allocated or is just issued */
		if (!req) {
			if (max > count - i) {
				/* less than max left */
				max = count - i;
			}

			req = _ocf_cleaner_alloc_slave_req(master, max, attribs);
		}

		if (!req) {
			/* Some memory allocation error,
			 * try re-allocate request
			 */
			max = _ocf_cleaner_get_req_max_count(max, true);
			req = _ocf_cleaner_alloc_slave_req(master, max, attribs);
		}

		/* when request allocation failed stop processing */
		if (!req) {
			master->error = -OCF_ERR_NO_MEM;
			break;
		}

		if (attribs->getter(cache, attribs->getter_context,
				i, &cache_line)) {
			OCF_DEBUG_MSG(cache, "Skip");
			continue;
		}

		/* Get mapping info */
		ocf_metadata_get_core_info(cache, cache_line, &core_id,
				&core_sector);

		if (attribs->lock_metadata) {
			ocf_hb_cline_prot_lock_rd(&cache->metadata.lock,
					req->lock_idx, core_id, core_sector);
		}

		skip = false;

		/* when line already cleaned - rare condition under heavy
		 * I/O workload.
		 */
		if (!metadata_test_dirty(cache, cache_line)) {
			OCF_DEBUG_MSG(cache, "Not dirty");
			skip = true;
		}

		if (!skip && !metadata_test_valid_any(cache, cache_line)) {
			OCF_DEBUG_MSG(cache, "No any valid");

			/*
			 * Extremely disturbing cache line state
			 * Cache line (sector) cannot be dirty and not valid
			 */
			ENV_BUG();
			skip = true;
		}

		if (attribs->lock_metadata) {
			ocf_hb_cline_prot_unlock_rd(&cache->metadata.lock,
					req->lock_idx, core_id, core_sector);
		}

		if (skip)
			continue;

		if (unlikely(!cache->core[core_id].opened)) {
			OCF_DEBUG_MSG(cache, "Core object inactive");
			continue;
		}

		req->map[i_out].core_id = core_id;
		req->map[i_out].core_line = core_sector;
		req->map[i_out].coll_idx = cache_line;
		req->map[i_out].status = LOOKUP_HIT;
		req->map[i_out].hash = i_out;
		i_out++;

		if (max == i_out) {
			err = _ocf_cleaner_do_fire(req, i_out, attribs->do_sort);
			if (err) {
				_ocf_cleaner_fire_error(master, req, err);
				req  = NULL;
				break;
			}
			i_out = 0;
			req  = NULL;
		}

	}

	if (req) {
		err = _ocf_cleaner_do_fire(req, i_out, attribs->do_sort);
		if (err)
			_ocf_cleaner_fire_error(master, req, err);
		req = NULL;
	}

	/* prevent cleaning completion race */
	_ocf_cleaner_complete_req(master);
	ocf_req_put(master);
}

static int _ocf_cleaner_do_flush_data_getter(struct ocf_cache *cache,
		void *context, uint32_t item, ocf_cache_line_t *line)
{
	struct flush_data *flush = context;

	if (flush[item].cache_line < cache->device->collision_table_entries) {
		(*line) = flush[item].cache_line;
		return 0;
	} else {
		return -1;
	}
}

int ocf_cleaner_do_flush_data_async(struct ocf_cache *cache,
		struct flush_data *flush, uint32_t count,
		struct ocf_cleaner_attribs *attribs)
{
	attribs->getter = _ocf_cleaner_do_flush_data_getter;
	attribs->getter_context = flush;
	attribs->count = count;

	ocf_cleaner_fire(cache, attribs);

	return 0;
}

/* Helper function for 'sort' */
static int _ocf_cleaner_cmp(const void *a, const void *b)
{
	struct flush_data *_a = (struct flush_data *)a;
	struct flush_data *_b = (struct flush_data *)b;

	/* TODO: FIXME get rid of static */
	static uint32_t step = 0;

	OCF_COND_RESCHED(step, 1000000)

	if (_a->core_id == _b->core_id)
		return (_a->core_line > _b->core_line) ? 1 : -1;

	return (_a->core_id > _b->core_id) ? 1 : -1;
}

static void _ocf_cleaner_swap(void *a, void *b, int size)
{
	struct flush_data *_a = (struct flush_data *)a;
	struct flush_data *_b = (struct flush_data *)b;
	struct flush_data t;

	t = *_a;
	*_a = *_b;
	*_b = t;
}

void ocf_cleaner_sort_sectors(struct flush_data *tbl, uint32_t num)
{
	env_sort(tbl, num, sizeof(*tbl), _ocf_cleaner_cmp, _ocf_cleaner_swap);
}

void ocf_cleaner_sort_flush_containers(struct flush_container *fctbl,
		uint32_t num)
{
	int i;

	for (i = 0; i < num; i++) {
		env_sort(fctbl[i].flush_data, fctbl[i].count,
				sizeof(*fctbl[i].flush_data), _ocf_cleaner_cmp,
				_ocf_cleaner_swap);
	}
}

void ocf_cleaner_refcnt_freeze(ocf_cache_t cache)
{
	struct ocf_user_part *curr_part;
	ocf_part_id_t part_id;

	for_each_user_part(cache, curr_part, part_id)
		ocf_refcnt_freeze(&curr_part->cleaning.counter);
}

void ocf_cleaner_refcnt_unfreeze(ocf_cache_t cache)
{
	struct ocf_user_part *curr_part;
	ocf_part_id_t part_id;

	for_each_user_part(cache, curr_part, part_id)
		ocf_refcnt_unfreeze(&curr_part->cleaning.counter);
}

static void ocf_cleaner_refcnt_register_zero_cb_finish(void *priv)
{
	struct ocf_cleaner_wait_context *ctx = priv;

	if (!env_atomic_dec_return(&ctx->waiting))
		ctx->cb(ctx->priv);
}

void ocf_cleaner_refcnt_register_zero_cb(ocf_cache_t cache,
		struct ocf_cleaner_wait_context *ctx,
		ocf_cleaner_refcnt_zero_cb_t cb, void *priv)
{
	struct ocf_user_part *curr_part;
	ocf_part_id_t part_id;

	env_atomic_set(&ctx->waiting, 1);
	ctx->cb = cb;
	ctx->priv = priv;

	for_each_user_part(cache, curr_part, part_id) {
		env_atomic_inc(&ctx->waiting);
		ocf_refcnt_register_zero_cb(&curr_part->cleaning.counter,
				ocf_cleaner_refcnt_register_zero_cb_finish, ctx);
	}

	ocf_cleaner_refcnt_register_zero_cb_finish(ctx);
}
