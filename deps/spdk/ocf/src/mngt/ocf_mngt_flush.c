/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "ocf_mngt_common.h"
#include "../ocf_priv.h"
#include "../metadata/metadata.h"
#include "../cleaning/cleaning.h"
#include "../engine/cache_engine.h"
#include "../engine/engine_common.h"
#include "../utils/utils_cleaner.h"
#include "../utils/utils_cache_line.h"
#include "../utils/utils_user_part.h"
#include "../utils/utils_pipeline.h"
#include "../utils/utils_refcnt.h"
#include "../ocf_request.h"
#include "../ocf_def_priv.h"

struct ocf_mngt_cache_flush_context;
typedef void (*ocf_flush_complete_t)(struct ocf_mngt_cache_flush_context *, int);

struct flush_containers_context
{
	/* array of container descriptors */
	struct flush_container *fctbl;
	/* fctbl array size */
	uint32_t fcnum;
	/* shared error for all concurrent container flushes */
	env_atomic error;
	/* number of outstanding container flushes */
	env_atomic count;
	/* first container flush to notice interrupt sets this to 1 */
	env_atomic interrupt_seen;
	/* completion to be called after all containers are flushed */
	ocf_flush_complete_t complete;
};

/* common struct for cache/core flush/purge pipeline priv */
struct ocf_mngt_cache_flush_context
{
	/* pipeline for flush / purge */
	ocf_pipeline_t pipeline;
	/* target cache */
	ocf_cache_t cache;
	/* target core */
	ocf_core_t core;

	struct {
		bool lock : 1;
		bool freeze : 1;
	} flags;

	/* management operation identifier */
	enum {
		flush_cache = 0,
		flush_core,
		purge_cache,
		purge_core
	} op;

	/* ocf management entry point completion */
	union {
		ocf_mngt_cache_flush_end_t flush_cache;
		ocf_mngt_core_flush_end_t flush_core;
		ocf_mngt_cache_purge_end_t purge_cache;
		ocf_mngt_core_purge_end_t purge_core;
	} cmpl;

	/* completion pivate data */
	void *priv;

	/* purge parameters */
	struct {
		uint64_t end_byte;
		uint64_t core_id;
	} purge;

	/* context for flush containers logic */
	struct flush_containers_context fcs;
};

static void _ocf_mngt_begin_flush_complete(void *priv)
{
	struct ocf_mngt_cache_flush_context *context = priv;
	ocf_pipeline_next(context->pipeline);
}

static void _ocf_mngt_begin_flush(ocf_pipeline_t pipeline, void *priv,
		ocf_pipeline_arg_t arg)
{
	struct ocf_mngt_cache_flush_context *context = priv;
	ocf_cache_t cache = context->cache;
	int result;

	/* FIXME: need mechanism for async waiting for outstanding flushes to
	 * finish */
	result = env_mutex_trylock(&cache->flush_mutex);
	if (result)
		OCF_PL_FINISH_RET(pipeline, -OCF_ERR_FLUSH_IN_PROGRESS);
	context->flags.lock = true;

	ocf_refcnt_freeze(&cache->refcnt.dirty);
	context->flags.freeze = true;

	ocf_refcnt_register_zero_cb(&cache->refcnt.dirty,
			_ocf_mngt_begin_flush_complete, context);
}

bool ocf_mngt_core_is_dirty(ocf_core_t core)
{
	return !!env_atomic_read(&core->runtime_meta->dirty_clines);
}

bool ocf_mngt_cache_is_dirty(ocf_cache_t cache)
{
	ocf_core_t core;
	ocf_core_id_t core_id;

	OCF_CHECK_NULL(cache);

	for_each_core(cache, core, core_id) {
		if (ocf_mngt_core_is_dirty(core))
			return true;
	}

	return false;
}

/************************FLUSH CORE CODE**************************************/
/* Returns:
 * 0 if OK and tbl & num is filled:
 * * tbl - table with sectors&cacheline
 * * num - number of items in this table.
 * other value means error.
 * NOTE:
 * Table is not sorted.
 */
static int _ocf_mngt_get_sectors(ocf_cache_t cache, ocf_core_id_t core_id,
		struct flush_data **tbl, uint32_t *num)
{
	ocf_core_t core = ocf_cache_get_core(cache, core_id);
	uint64_t core_line;
	ocf_core_id_t i_core_id;
	struct flush_data *elem;
	uint32_t line, dirty_found = 0, dirty_total = 0;
	unsigned ret = 0;

	ocf_metadata_start_exclusive_access(&cache->metadata.lock);

	dirty_total = env_atomic_read(&core->runtime_meta->dirty_clines);
	if (!dirty_total) {
		*num = 0;
		*tbl = NULL;
		goto unlock;
	}

	*tbl = env_vmalloc(dirty_total * sizeof(**tbl));
	if (*tbl == NULL) {
		ret = -OCF_ERR_NO_MEM;
		goto unlock;
	}

	for (line = 0, elem = *tbl;
			line < cache->device->collision_table_entries;
			line++) {
		ocf_metadata_get_core_info(cache, line, &i_core_id,
				&core_line);

		if (i_core_id == core_id &&
				metadata_test_valid_any(cache, line) &&
				metadata_test_dirty(cache, line)) {
			/* It's valid and dirty target core cacheline */
			elem->cache_line = line;
			elem->core_line = core_line;
			elem->core_id = i_core_id;
			elem++;
			dirty_found++;

			/* stop if all cachelines were found */
			if (dirty_found == dirty_total)
				break;
		}

		if ((line + 1) % 131072 == 0) {
			ocf_metadata_end_exclusive_access(
					&cache->metadata.lock);
			env_cond_resched();
			ocf_metadata_start_exclusive_access(
					&cache->metadata.lock);
		}
	}

	ocf_core_log(core, log_debug,
			"%u dirty cache lines to clean\n", dirty_found);


	*num = dirty_found;

unlock:
	ocf_metadata_end_exclusive_access(&cache->metadata.lock);

	return ret;
}

static int _ocf_mngt_get_flush_containers(ocf_cache_t cache,
		struct flush_container **fctbl, uint32_t *fcnum)
{
	struct flush_container *fc;
	struct flush_container *curr;
	uint32_t *core_revmap;
	uint32_t num;
	uint64_t core_line;
	ocf_core_id_t core_id;
	ocf_core_t core;
	uint32_t i, j = 0, line;
	uint32_t dirty_found = 0, dirty_total = 0;
	int ret = 0;

	ocf_metadata_start_exclusive_access(&cache->metadata.lock);

	/*
	 * TODO: Create containers for each physical device, not for
	 *       each core. Cores can be partitions of single device.
	 */
	num = cache->conf_meta->core_count;
	if (num == 0) {
		*fcnum = 0;
		goto unlock;
	}

	core_revmap = env_vzalloc(sizeof(*core_revmap) * OCF_CORE_MAX);
	if (!core_revmap)
		return -OCF_ERR_NO_MEM;

	/* TODO: Alloc fcs and data tables in single allocation */
	fc = env_vzalloc(sizeof(**fctbl) * num);
	if (!fc) {
		env_vfree(core_revmap);
		ret = -OCF_ERR_NO_MEM;
		goto unlock;
	}

	for_each_core(cache, core, core_id) {
		fc[j].core_id = core_id;
		core_revmap[core_id] = j;

		/* Check for dirty blocks */
		fc[j].count = env_atomic_read(
				&core->runtime_meta->dirty_clines);
		dirty_total += fc[j].count;

		if (fc[j].count) {
			fc[j].flush_data = env_vmalloc(fc[j].count *
					sizeof(*fc[j].flush_data));
		}

		if (++j == cache->conf_meta->core_count)
			break;
	}

	if (!dirty_total) {
		env_vfree(core_revmap);
		env_vfree(fc);
		*fcnum = 0;
		goto unlock;
	}

	for (line = 0; line < cache->device->collision_table_entries; line++) {
		ocf_metadata_get_core_info(cache, line, &core_id, &core_line);

		if (metadata_test_valid_any(cache, line) &&
				metadata_test_dirty(cache, line)) {
			curr = &fc[core_revmap[core_id]];

			ENV_BUG_ON(curr->iter >= curr->count);

			/* It's core_id cacheline and it's valid and it's dirty! */
			curr->flush_data[curr->iter].cache_line = line;
			curr->flush_data[curr->iter].core_line = core_line;
			curr->flush_data[curr->iter].core_id = core_id;
			curr->iter++;
			dirty_found++;

			/* stop if all cachelines were found */
			if (dirty_found == dirty_total)
				break;
		}

		if ((line + 1) % 131072 == 0) {
			ocf_metadata_end_exclusive_access(
					&cache->metadata.lock);
			env_cond_resched();
			ocf_metadata_start_exclusive_access(
					&cache->metadata.lock);
		}
	}

	if (dirty_total != dirty_found) {
		for (i = 0; i < num; i++)
			fc[i].count = fc[i].iter;
	}

	for (i = 0; i < num; i++)
		fc[i].iter = 0;

	env_vfree(core_revmap);
	*fctbl = fc;
	*fcnum = num;

unlock:
	ocf_metadata_end_exclusive_access(&cache->metadata.lock);
	return ret;
}

static void _ocf_mngt_free_flush_containers(struct flush_container *fctbl,
	uint32_t num)
{
	int i;

	for (i = 0; i < num; i++)
		env_vfree(fctbl[i].flush_data);
	env_vfree(fctbl);
}

/*
 * OCF will try to guess disk speed etc. and adjust flushing block
 * size accordingly, however these bounds shall be respected regardless
 * of disk speed, cache line size configured etc.
 */
#define OCF_MNG_FLUSH_MIN (4*MiB / ocf_line_size(cache))
#define OCF_MNG_FLUSH_MAX (100*MiB / ocf_line_size(cache))

static void _ocf_mngt_flush_portion(struct flush_container *fc)
{
	ocf_cache_t cache = fc->cache;
	uint64_t flush_portion_div;
	uint32_t curr_count;

	flush_portion_div = env_ticks_to_msecs(fc->ticks2 - fc->ticks1);
	if (unlikely(!flush_portion_div))
		flush_portion_div = 1;

	fc->flush_portion = fc->flush_portion * 1000 / flush_portion_div;
	fc->flush_portion &= ~0x3ffULL;

	/* regardless those calculations, limit flush portion to be
	 * between OCF_MNG_FLUSH_MIN and OCF_MNG_FLUSH_MAX
	 */
	fc->flush_portion = OCF_MIN(fc->flush_portion, OCF_MNG_FLUSH_MAX);
	fc->flush_portion = OCF_MAX(fc->flush_portion, OCF_MNG_FLUSH_MIN);

	curr_count = OCF_MIN(fc->count - fc->iter, fc->flush_portion);

	ocf_cleaner_do_flush_data_async(fc->cache,
			&fc->flush_data[fc->iter],
			curr_count, &fc->attribs);

	fc->iter += curr_count;
}

static void _ocf_mngt_flush_portion_end(void *private_data, int error)
{
	struct flush_container *fc = private_data;
	struct ocf_mngt_cache_flush_context *context = fc->context;
	struct flush_containers_context *fsc = &context->fcs;
	ocf_cache_t cache = context->cache;
	ocf_core_t core = &cache->core[fc->core_id];
	bool first_interrupt;

	env_atomic_set(&core->flushed, fc->iter);

	fc->ticks2 = env_get_tick_count();

	env_atomic_cmpxchg(&fsc->error, 0, error);

	if (cache->flushing_interrupted) {
		first_interrupt = !env_atomic_cmpxchg(
				&fsc->interrupt_seen, 0, 1);
		if (first_interrupt) {
			ocf_cache_log(cache, log_info,
					"Flushing interrupted by user\n");
			env_atomic_cmpxchg(&fsc->error, 0,
					-OCF_ERR_FLUSHING_INTERRUPTED);
		}
	}

	if (env_atomic_read(&fsc->error) || fc->iter == fc->count) {
		ocf_req_put(fc->req);
		fc->end(context);
		return;
	}

	ocf_engine_push_req_back(fc->req, false);
}


static int _ofc_flush_container_step(struct ocf_request *req)
{
	struct flush_container *fc = req->priv;
	ocf_cache_t cache = fc->cache;

	ocf_metadata_start_exclusive_access(&cache->metadata.lock);
	_ocf_mngt_flush_portion(fc);
	ocf_metadata_end_exclusive_access(&cache->metadata.lock);

	return 0;
}

static const struct ocf_io_if _io_if_flush_portion = {
	.read = _ofc_flush_container_step,
	.write = _ofc_flush_container_step,
};

static void _ocf_mngt_flush_container(
		struct ocf_mngt_cache_flush_context *context,
		struct flush_container *fc, ocf_flush_containter_coplete_t end)
{
	ocf_cache_t cache = context->cache;
	struct ocf_request *req;
	int error = 0;

	if (!fc->count)
		goto finish;

	fc->end = end;
	fc->context = context;

	req = ocf_req_new(cache->mngt_queue, NULL, 0, 0, 0);
	if (!req) {
		error = OCF_ERR_NO_MEM;
		goto finish;
	}

	req->info.internal = true;
	req->io_if = &_io_if_flush_portion;
	req->priv = fc;

	fc->req = req;
	fc->attribs.lock_cacheline = true;
	fc->attribs.lock_metadata = false;
	fc->attribs.cmpl_context = fc;
	fc->attribs.cmpl_fn = _ocf_mngt_flush_portion_end;
	fc->attribs.io_queue = cache->mngt_queue;
	fc->cache = cache;
	fc->flush_portion = OCF_MNG_FLUSH_MIN;
	fc->ticks1 = 0;
	fc->ticks2 = UINT_MAX;

	ocf_engine_push_req_back(fc->req, true);
	return;

finish:
	env_atomic_cmpxchg(&context->fcs.error, 0, error);
	end(context);
}

void _ocf_flush_container_complete(void *ctx)
{
	struct ocf_mngt_cache_flush_context *context = ctx;

	if (env_atomic_dec_return(&context->fcs.count)) {
		return;
	}

	_ocf_mngt_free_flush_containers(context->fcs.fctbl,
			context->fcs.fcnum);

	context->fcs.complete(context,
			env_atomic_read(&context->fcs.error));
}

static void _ocf_mngt_flush_containers(
		struct ocf_mngt_cache_flush_context *context,
		struct flush_container *fctbl,
		uint32_t fcnum, ocf_flush_complete_t complete)
{
	int i;

	if (fcnum == 0) {
		complete(context, 0);
		return;
	}

	/* Sort data. Smallest sectors first (0...n). */
	ocf_cleaner_sort_flush_containers(fctbl, fcnum);

	env_atomic_set(&context->fcs.error, 0);
	env_atomic_set(&context->fcs.count, 1);
	context->fcs.complete = complete;
	context->fcs.fctbl = fctbl;
	context->fcs.fcnum = fcnum;

	for (i = 0; i < fcnum; i++) {
		env_atomic_inc(&context->fcs.count);
		_ocf_mngt_flush_container(context, &fctbl[i],
			_ocf_flush_container_complete);
	}

	_ocf_flush_container_complete(context);
}


static void _ocf_mngt_flush_core(
	struct ocf_mngt_cache_flush_context *context,
	ocf_flush_complete_t complete)
{
	ocf_cache_t cache = context->cache;
	ocf_core_t core = context->core;
	ocf_core_id_t core_id = ocf_core_get_id(core);
	struct flush_container *fc;
	int ret;

	fc = env_vzalloc(sizeof(*fc));
	if (!fc) {
		complete(context, -OCF_ERR_NO_MEM);
		return;
	}

	ret = _ocf_mngt_get_sectors(cache, core_id,
			&fc->flush_data, &fc->count);
	if (ret) {
		ocf_core_log(core, log_err, "Flushing operation aborted, "
				"no memory\n");
		env_vfree(fc);
		complete(context, -OCF_ERR_NO_MEM);
		return;
	}

	fc->core_id = core_id;
	fc->iter = 0;

	_ocf_mngt_flush_containers(context, fc, 1, complete);
}

static void _ocf_mngt_flush_all_cores(
	struct ocf_mngt_cache_flush_context *context,
	ocf_flush_complete_t complete)
{
	ocf_cache_t cache = context->cache;
	struct flush_container *fctbl = NULL;
	uint32_t fcnum = 0;
	int ret;

	if (context->op == flush_cache)
		ocf_cache_log(cache, log_info, "Flushing cache\n");
	else if (context->op == purge_cache)
		ocf_cache_log(cache, log_info, "Purging cache\n");

	env_atomic_set(&cache->flush_in_progress, 1);

	/* Get all 'dirty' sectors for all cores */
	ret = _ocf_mngt_get_flush_containers(cache, &fctbl, &fcnum);
	if (ret) {
		ocf_cache_log(cache, log_err, "Flushing operation aborted, "
				"no memory\n");
		ocf_metadata_end_exclusive_access(&cache->metadata.lock);
		complete(context, ret);
		return;
	}

	_ocf_mngt_flush_containers(context, fctbl, fcnum, complete);
}

static void _ocf_mngt_flush_all_cores_complete(
		struct ocf_mngt_cache_flush_context *context, int error)
{
	ocf_cache_t cache = context->cache;
	uint32_t i, j;

	env_atomic_set(&cache->flush_in_progress, 0);

	for (i = 0, j = 0; i < OCF_CORE_MAX; i++) {
		if (!env_bit_test(i, cache->conf_meta->valid_core_bitmap))
			continue;

		env_atomic_set(&cache->core[i].flushed, 0);

		if (++j == cache->conf_meta->core_count)
			break;
	}

	if (error)
		OCF_PL_FINISH_RET(context->pipeline, error);

	if (context->op == flush_cache)
		ocf_cache_log(cache, log_info, "Flushing cache completed\n");

	ocf_pipeline_next(context->pipeline);
}

/**
 * Flush all the dirty data stored on cache (all the cores attached to it)
 */
static void _ocf_mngt_cache_flush(ocf_pipeline_t pipeline, void *priv,
		ocf_pipeline_arg_t arg)
{
	struct ocf_mngt_cache_flush_context *context = priv;

	context->cache->flushing_interrupted = 0;
	_ocf_mngt_flush_all_cores(context, _ocf_mngt_flush_all_cores_complete);
}

static void _ocf_mngt_flush_finish(ocf_pipeline_t pipeline, void *priv,
		int error)

{
	struct ocf_mngt_cache_flush_context *context = priv;
	ocf_cache_t cache = context->cache;
	ocf_core_t core = context->core;

	if (context->flags.freeze)
		ocf_refcnt_unfreeze(&cache->refcnt.dirty);

	if (context->flags.lock)
		env_mutex_unlock(&cache->flush_mutex);

	switch (context->op) {
	case flush_cache:
		context->cmpl.flush_cache(cache, context->priv, error);
		break;
	case flush_core:
		context->cmpl.flush_core(core, context->priv, error);
		break;
	case purge_cache:
		context->cmpl.purge_cache(cache, context->priv, error);
		break;
	case purge_core:
		context->cmpl.purge_core(core, context->priv, error);
		break;
	default:
		ENV_BUG();
	}

	ocf_pipeline_destroy(pipeline);
}

static struct ocf_pipeline_properties _ocf_mngt_cache_flush_pipeline_properties = {
	.priv_size = sizeof(struct ocf_mngt_cache_flush_context),
	.finish = _ocf_mngt_flush_finish,
	.steps = {
		OCF_PL_STEP(_ocf_mngt_begin_flush),
		OCF_PL_STEP(_ocf_mngt_cache_flush),
		OCF_PL_STEP_TERMINATOR(),
	},
};

void ocf_mngt_cache_flush(ocf_cache_t cache,
		ocf_mngt_cache_flush_end_t cmpl, void *priv)
{
	ocf_pipeline_t pipeline;
	struct ocf_mngt_cache_flush_context *context;
	int result = 0;

	OCF_CHECK_NULL(cache);

	if (!ocf_cache_is_device_attached(cache)) {
		ocf_cache_log(cache, log_err, "Cannot flush cache - "
				"cache device is detached\n");
		OCF_CMPL_RET(cache, priv, -OCF_ERR_INVAL);
	}

	if (ocf_cache_is_incomplete(cache)) {
		ocf_cache_log(cache, log_err, "Cannot flush cache - "
				"cache is in incomplete state\n");
		OCF_CMPL_RET(cache, priv, -OCF_ERR_CACHE_IN_INCOMPLETE_STATE);
	}

	if (!cache->mngt_queue) {
		ocf_cache_log(cache, log_err,
				"Cannot flush cache - no flush queue set\n");
		OCF_CMPL_RET(cache, priv, -OCF_ERR_INVAL);
	}

	result = ocf_pipeline_create(&pipeline, cache,
			&_ocf_mngt_cache_flush_pipeline_properties);
	if (result)
		OCF_CMPL_RET(cache, priv, -OCF_ERR_NO_MEM);

	context = ocf_pipeline_get_priv(pipeline);

	context->pipeline = pipeline;
	context->cmpl.flush_cache = cmpl;
	context->priv = priv;
	context->cache = cache;
	context->op = flush_cache;

	ocf_pipeline_next(context->pipeline);
}

static void _ocf_mngt_flush_core_complete(
		struct ocf_mngt_cache_flush_context *context, int error)
{
	ocf_cache_t cache = context->cache;
	ocf_core_t core = context->core;

	env_atomic_set(&core->flushed, 0);

	if (error)
		OCF_PL_FINISH_RET(context->pipeline, error);

	if (context->op == flush_core)
		ocf_cache_log(cache, log_info, "Flushing completed\n");

	ocf_pipeline_next(context->pipeline);
}

static void _ocf_mngt_core_flush(ocf_pipeline_t pipeline, void *priv,
		ocf_pipeline_arg_t arg)
{
	struct ocf_mngt_cache_flush_context *context = priv;
	ocf_cache_t cache = context->cache;

	if (context->op == flush_core)
		ocf_cache_log(cache, log_info, "Flushing core\n");
	else if (context->op == purge_core)
		ocf_cache_log(cache, log_info, "Purging core\n");

	context->cache->flushing_interrupted = 0;
	_ocf_mngt_flush_core(context, _ocf_mngt_flush_core_complete);
}

static
struct ocf_pipeline_properties _ocf_mngt_core_flush_pipeline_properties = {
	.priv_size = sizeof(struct ocf_mngt_cache_flush_context),
	.finish = _ocf_mngt_flush_finish,
	.steps = {
		OCF_PL_STEP(_ocf_mngt_begin_flush),
		OCF_PL_STEP(_ocf_mngt_core_flush),
		OCF_PL_STEP_TERMINATOR(),
	},
};

void ocf_mngt_core_flush(ocf_core_t core,
		ocf_mngt_core_flush_end_t cmpl, void *priv)
{
	ocf_pipeline_t pipeline;
	struct ocf_mngt_cache_flush_context *context;
	ocf_cache_t cache;
	int result;

	OCF_CHECK_NULL(core);

	cache = ocf_core_get_cache(core);

	if (!ocf_cache_is_device_attached(cache)) {
		ocf_cache_log(cache, log_err, "Cannot flush core - "
				"cache device is detached\n");
		OCF_CMPL_RET(core, priv, -OCF_ERR_INVAL);
	}

	if (!core->opened) {
		ocf_core_log(core, log_err, "Cannot flush - core is in "
				"inactive state\n");
		OCF_CMPL_RET(core, priv, -OCF_ERR_CORE_IN_INACTIVE_STATE);
	}

	if (!cache->mngt_queue) {
		ocf_core_log(core, log_err,
				"Cannot flush core - no flush queue set\n");
		OCF_CMPL_RET(core, priv, -OCF_ERR_INVAL);
	}

	result = ocf_pipeline_create(&pipeline, cache,
			&_ocf_mngt_core_flush_pipeline_properties);
	if (result)
		OCF_CMPL_RET(core, priv, -OCF_ERR_NO_MEM);

	context = ocf_pipeline_get_priv(pipeline);

	context->pipeline = pipeline;
	context->cmpl.flush_core = cmpl;
	context->priv = priv;
	context->cache = cache;
	context->op = flush_core;
	context->core = core;

	ocf_pipeline_next(context->pipeline);
}

static void _ocf_mngt_cache_invalidate(ocf_pipeline_t pipeline, void *priv,
		ocf_pipeline_arg_t arg)
{
	struct ocf_mngt_cache_flush_context *context = priv;
	ocf_cache_t cache = context->cache;
	int result;

	ocf_metadata_start_exclusive_access(&cache->metadata.lock);
	result = ocf_metadata_sparse_range(cache, context->purge.core_id, 0,
			context->purge.end_byte);
	ocf_metadata_end_exclusive_access(&cache->metadata.lock);

	OCF_PL_NEXT_ON_SUCCESS_RET(context->pipeline, result);
}

static
struct ocf_pipeline_properties _ocf_mngt_cache_purge_pipeline_properties = {
	.priv_size = sizeof(struct ocf_mngt_cache_flush_context),
	.finish = _ocf_mngt_flush_finish,
	.steps = {
		OCF_PL_STEP(_ocf_mngt_begin_flush),
		OCF_PL_STEP(_ocf_mngt_cache_flush),
		OCF_PL_STEP(_ocf_mngt_cache_invalidate),
		OCF_PL_STEP_TERMINATOR(),
	},
};

void ocf_mngt_cache_purge(ocf_cache_t cache,
		ocf_mngt_cache_purge_end_t cmpl, void *priv)
{
	ocf_pipeline_t pipeline;
	int result = 0;
	struct ocf_mngt_cache_flush_context *context;

	OCF_CHECK_NULL(cache);

	if (!cache->mngt_queue) {
		ocf_cache_log(cache, log_err,
				"Cannot purge cache - no flush queue set\n");
		OCF_CMPL_RET(cache, priv, -OCF_ERR_INVAL);
	}

	result = ocf_pipeline_create(&pipeline, cache,
			&_ocf_mngt_cache_purge_pipeline_properties);
	if (result)
		OCF_CMPL_RET(cache, priv, -OCF_ERR_NO_MEM);

	context = ocf_pipeline_get_priv(pipeline);

	context->pipeline = pipeline;
	context->cmpl.purge_cache = cmpl;
	context->priv = priv;
	context->cache = cache;
	context->op = purge_cache;
	context->purge.core_id = OCF_CORE_ID_INVALID;
	context->purge.end_byte = ~0ULL;

	ocf_pipeline_next(context->pipeline);
}

static
struct ocf_pipeline_properties _ocf_mngt_core_purge_pipeline_properties = {
	.priv_size = sizeof(struct ocf_mngt_cache_flush_context),
	.finish = _ocf_mngt_flush_finish,
	.steps = {
		OCF_PL_STEP(_ocf_mngt_begin_flush),
		OCF_PL_STEP(_ocf_mngt_core_flush),
		OCF_PL_STEP(_ocf_mngt_cache_invalidate),
		OCF_PL_STEP_TERMINATOR(),
	},
};

void ocf_mngt_core_purge(ocf_core_t core,
		ocf_mngt_core_purge_end_t cmpl, void *priv)
{
	ocf_pipeline_t pipeline;
	struct ocf_mngt_cache_flush_context *context;
	ocf_cache_t cache;
	ocf_core_id_t core_id;
	int result = 0;
	uint64_t core_size = ~0ULL;

	OCF_CHECK_NULL(core);

	cache = ocf_core_get_cache(core);
	core_id = ocf_core_get_id(core);

	if (!cache->mngt_queue) {
		ocf_core_log(core, log_err,
				"Cannot purge core - no flush queue set\n");
		OCF_CMPL_RET(core, priv, -OCF_ERR_INVAL);
	}

	core_size = ocf_volume_get_length(&cache->core[core_id].volume);

	result = ocf_pipeline_create(&pipeline, cache,
			&_ocf_mngt_core_purge_pipeline_properties);
	if (result)
		OCF_CMPL_RET(core, priv, -OCF_ERR_NO_MEM);

	context = ocf_pipeline_get_priv(pipeline);

	context->pipeline = pipeline;
	context->cmpl.purge_core = cmpl;
	context->priv = priv;
	context->cache = cache;
	context->op = purge_core;
	context->purge.core_id = core_id;
	context->purge.end_byte = core_size ?: ~0ULL;
	context->core = core;

	ocf_pipeline_next(context->pipeline);
}

void ocf_mngt_cache_flush_interrupt(ocf_cache_t cache)
{
	OCF_CHECK_NULL(cache);

	ocf_cache_log(cache, log_alert, "Flushing interrupt\n");
	cache->flushing_interrupted = 1;
}

struct ocf_mngt_cache_set_cleaning_context
{
	/* pipeline for switching cleaning policy */
	ocf_pipeline_t pipeline;
	/* target cache */
	ocf_cache_t cache;
	/* new cleaning policy */
	ocf_cleaning_t new_policy;
	/* old cleaning policy */
	ocf_cleaning_t old_policy;
	/* completion function */
	ocf_mngt_cache_set_cleaning_policy_end_t cmpl;
	/* completion function */
	void *priv;
};

static void _ocf_mngt_cleaning_deinit_complete(void *priv)
{
	struct ocf_mngt_cache_set_cleaning_context *context = priv;
	ocf_cache_t cache = context->cache;

	ocf_cleaning_deinitialize(cache);

	ocf_pipeline_next(context->pipeline);
}

static void _ocf_mngt_deinit_clean_policy(ocf_pipeline_t pipeline, void *priv,
		ocf_pipeline_arg_t arg)
{
	struct ocf_mngt_cache_set_cleaning_context *context = priv;
	ocf_cache_t cache = context->cache;

	ocf_metadata_start_exclusive_access(&cache->metadata.lock);

	ocf_refcnt_freeze(&cache->cleaner.refcnt);
	ocf_refcnt_register_zero_cb(&cache->cleaner.refcnt,
			_ocf_mngt_cleaning_deinit_complete, context);
}

static void _ocf_mngt_init_clean_policy(ocf_pipeline_t pipeline, void *priv,
		ocf_pipeline_arg_t arg)
{
	int result;
	struct ocf_mngt_cache_set_cleaning_context *context = priv;
	ocf_cache_t cache = context->cache;
	ocf_cleaning_t old_policy = context->old_policy;
	ocf_cleaning_t new_policy = context->new_policy;
	ocf_cleaning_t emergency_policy = ocf_cleaning_nop;

	result = ocf_cleaning_initialize(cache, new_policy, 1);
	if (result) {
		ocf_cache_log(cache, log_info, "Failed to initialize %s cleaning "
				"policy. Setting %s instead\n",
				ocf_cleaning_get_name(new_policy),
				ocf_cleaning_get_name(emergency_policy));
		new_policy = emergency_policy;
	} else {
		ocf_cache_log(cache, log_info, "Changing cleaning policy from "
				"%s to %s\n", ocf_cleaning_get_name(old_policy),
				ocf_cleaning_get_name(new_policy));
	}

	cache->conf_meta->cleaning_policy_type = new_policy;

	ocf_refcnt_unfreeze(&cache->cleaner.refcnt);
	ocf_metadata_end_exclusive_access(&cache->metadata.lock);

	OCF_PL_NEXT_ON_SUCCESS_RET(pipeline, result);
}

static void _ocf_mngt_set_cleaning_finish(ocf_pipeline_t pipeline, void *priv,
		int error)
{
	struct ocf_mngt_cache_set_cleaning_context *context = priv;

	context->cmpl(context->priv, error);

	ocf_pipeline_destroy(pipeline);
}

static
struct ocf_pipeline_properties _ocf_mngt_cache_set_cleaning_policy = {
	.priv_size = sizeof(struct ocf_mngt_cache_set_cleaning_context),
	.finish = _ocf_mngt_set_cleaning_finish,
	.steps = {
		OCF_PL_STEP(_ocf_mngt_deinit_clean_policy),
		OCF_PL_STEP(_ocf_mngt_init_clean_policy),
		OCF_PL_STEP_TERMINATOR(),
	},
};

void ocf_mngt_cache_cleaning_set_policy(ocf_cache_t cache,
		ocf_cleaning_t new_policy,
		ocf_mngt_cache_set_cleaning_policy_end_t cmpl, void *priv)
{
	struct ocf_mngt_cache_set_cleaning_context *context;
	ocf_pipeline_t pipeline;
	ocf_cleaning_t old_policy;
	int ret = 0;

	OCF_CHECK_NULL(cache);

	if (new_policy < 0 || new_policy >= ocf_cleaning_max)
		OCF_CMPL_RET(priv, -OCF_ERR_INVAL);

	old_policy = cache->conf_meta->cleaning_policy_type;

	if (new_policy == old_policy) {
		ocf_cache_log(cache, log_info, "Cleaning policy %s is already "
				"set\n", ocf_cleaning_get_name(old_policy));
		OCF_CMPL_RET(priv, 0);
	}

	ret = ocf_pipeline_create(&pipeline, cache,
			&_ocf_mngt_cache_set_cleaning_policy);
	if (ret)
		OCF_CMPL_RET(priv, ret);

	context = ocf_pipeline_get_priv(pipeline);

	context->cmpl = cmpl;
	context->cache = cache;
	context->pipeline = pipeline;
	context->new_policy = new_policy;
	context->old_policy = old_policy;
	context->priv = priv;

	OCF_PL_NEXT_RET(pipeline);
}

int ocf_mngt_cache_cleaning_get_policy(ocf_cache_t cache, ocf_cleaning_t *type)
{
	OCF_CHECK_NULL(cache);
	OCF_CHECK_NULL(type);

	*type = cache->conf_meta->cleaning_policy_type;

	return 0;
}

int ocf_mngt_cache_cleaning_set_param(ocf_cache_t cache, ocf_cleaning_t type,
		uint32_t param_id, uint32_t param_value)
{
	int ret;

	OCF_CHECK_NULL(cache);

	if (type < 0 || type >= ocf_cleaning_max)
		return -OCF_ERR_INVAL;

	ocf_metadata_start_exclusive_access(&cache->metadata.lock);

	ret = ocf_cleaning_set_param(cache, type, param_id, param_value);

	ocf_metadata_end_exclusive_access(&cache->metadata.lock);

	return ret;
}

int ocf_mngt_cache_cleaning_get_param(ocf_cache_t cache, ocf_cleaning_t type,
		uint32_t param_id, uint32_t *param_value)
{
	OCF_CHECK_NULL(cache);
	OCF_CHECK_NULL(param_value);

	if (type < 0 || type >= ocf_cleaning_max)
		return -OCF_ERR_INVAL;

	return ocf_cleaning_get_param(cache, type, param_id, param_value);
}
