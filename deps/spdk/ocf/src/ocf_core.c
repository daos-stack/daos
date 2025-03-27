/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "ocf_priv.h"
#include "ocf_core_priv.h"
#include "ocf_io_priv.h"
#include "metadata/metadata.h"
#include "engine/cache_engine.h"
#include "utils/utils_user_part.h"
#include "ocf_request.h"
#include "ocf_trace_priv.h"

struct ocf_core_volume {
	ocf_core_t core;
};

ocf_cache_t ocf_core_get_cache(ocf_core_t core)
{
	OCF_CHECK_NULL(core);
	return core->volume.cache;
}

ocf_volume_t ocf_core_get_volume(ocf_core_t core)
{
	OCF_CHECK_NULL(core);
	return &core->volume;
}

ocf_volume_t ocf_core_get_front_volume(ocf_core_t core)
{
	OCF_CHECK_NULL(core);
	return &core->front_volume;
}

ocf_core_id_t ocf_core_get_id(ocf_core_t core)
{
	struct ocf_cache *cache;
	ocf_core_id_t core_id;

	OCF_CHECK_NULL(core);

	cache = core->volume.cache;
	core_id = core - cache->core;

	return core_id;
}

int ocf_core_get_by_name(ocf_cache_t cache, const char *name, size_t name_len,
		ocf_core_t *core)
{
	ocf_core_t i_core;
	ocf_core_id_t i_core_id;

	for_each_core(cache, i_core, i_core_id) {
		if (!env_strncmp(ocf_core_get_name(i_core), OCF_CORE_NAME_SIZE,
				name, name_len)) {
			*core = i_core;
			return 0;
		}
	}

	return -OCF_ERR_CORE_NOT_EXIST;
}

const char *ocf_core_get_name(ocf_core_t core)
{
	OCF_CHECK_NULL(core);

	return core->conf_meta->name;
}

ocf_core_state_t ocf_core_get_state(ocf_core_t core)
{
	OCF_CHECK_NULL(core);

	return core->opened ?
			ocf_core_state_active : ocf_core_state_inactive;
}

bool ocf_core_is_valid(ocf_cache_t cache, ocf_core_id_t id)
{
	OCF_CHECK_NULL(cache);

	if (id > OCF_CORE_ID_MAX)
		return false;

	if (!env_bit_test(id, cache->conf_meta->valid_core_bitmap))
		return false;

	return true;
}

int ocf_core_get(ocf_cache_t cache, ocf_core_id_t id, ocf_core_t *core)
{
	OCF_CHECK_NULL(cache);

	if (!ocf_core_is_valid(cache, id))
		return -OCF_ERR_CORE_NOT_AVAIL;

	*core = &cache->core[id];
	return 0;
}

uint32_t ocf_core_get_seq_cutoff_threshold(ocf_core_t core)
{
	return env_atomic_read(&core->conf_meta->seq_cutoff_threshold);
}

ocf_seq_cutoff_policy ocf_core_get_seq_cutoff_policy(ocf_core_t core)
{
	return env_atomic_read(&core->conf_meta->seq_cutoff_policy);
}

uint32_t ocf_core_get_seq_cutoff_promotion_count(ocf_core_t core)
{
	return env_atomic_read(&core->conf_meta->seq_cutoff_promo_count);
}

int ocf_core_visit(ocf_cache_t cache, ocf_core_visitor_t visitor, void *cntx,
		bool only_opened)
{
	ocf_core_id_t id;
	int result = 0;

	OCF_CHECK_NULL(cache);

	if (!visitor)
		return -OCF_ERR_INVAL;

	for (id = 0; id < OCF_CORE_MAX; id++) {
		if (!env_bit_test(id, cache->conf_meta->valid_core_bitmap))
			continue;

		if (only_opened && !cache->core[id].opened)
			continue;

		result = visitor(&cache->core[id], cntx);
		if (result)
			break;
	}

	return result;
}

/* *** HELPER FUNCTIONS *** */

static uint64_t _calc_dirty_for(uint64_t dirty_since)
{
	uint64_t current_time = env_ticks_to_secs(env_get_tick_count());

	return dirty_since ? (current_time - dirty_since) : 0;
}

static inline struct ocf_request *ocf_io_to_req(struct ocf_io *io)
{
	struct ocf_io_internal *ioi;

	ioi = container_of(io, struct ocf_io_internal, io);
	return container_of(ioi, struct ocf_request, ioi);
}

static inline ocf_core_t ocf_volume_to_core(ocf_volume_t volume)
{
	struct ocf_core_volume *core_volume = ocf_volume_get_priv(volume);

	return core_volume->core;
}

static inline void dec_counter_if_req_was_dirty(struct ocf_request *req)
{
	if (!req->dirty)
		return;

	req->dirty = 0;
	ocf_refcnt_dec(&req->cache->refcnt.dirty);
}

static inline int ocf_core_validate_io(struct ocf_io *io)
{
	ocf_volume_t volume = ocf_io_get_volume(io);
	ocf_core_t core = ocf_volume_to_core(volume);

	if (io->addr + io->bytes > ocf_volume_get_length(volume))
		return -OCF_ERR_INVAL;

	if (io->io_class >= OCF_USER_IO_CLASS_MAX)
		return -OCF_ERR_INVAL;

	if (io->dir != OCF_READ && io->dir != OCF_WRITE)
		return -OCF_ERR_INVAL;

	if (!io->io_queue)
		return -OCF_ERR_INVAL;

	if (!io->end)
		return -OCF_ERR_INVAL;

	/* Core volume I/O must not be queued on management queue - this would
	 * break I/O accounting code, resulting in use-after-free type of errors
	 * after cache detach, core remove etc. */
	if (io->io_queue == ocf_core_get_cache(core)->mngt_queue)
		return -OCF_ERR_INVAL;

	return 0;
}

static void ocf_req_complete(struct ocf_request *req, int error)
{
	/* Log trace */
	ocf_trace_io_cmpl(req);

	/* Complete IO */
	ocf_io_end(&req->ioi.io, error);

	dec_counter_if_req_was_dirty(req);

	/* Invalidate OCF IO, it is not valid after completion */
	ocf_io_put(&req->ioi.io);
}

static int ocf_core_submit_io_fast(struct ocf_io *io, struct ocf_request *req,
		ocf_core_t core, ocf_cache_t cache)
{
	struct ocf_event_io trace_event;
	ocf_req_cache_mode_t original_cache_mode;
	int fast;

	if (req->d2c) {
		return -OCF_ERR_IO;
	}

	original_cache_mode = req->cache_mode;

	switch (req->cache_mode) {
	case ocf_req_cache_mode_pt:
		return -OCF_ERR_IO;
	case ocf_req_cache_mode_wb:
	case ocf_req_cache_mode_wo:
		req->cache_mode = ocf_req_cache_mode_fast;
		break;
	default:
		if (cache->use_submit_io_fast)
			break;

		if (io->dir == OCF_WRITE)
			return -OCF_ERR_IO;

		req->cache_mode = ocf_req_cache_mode_fast;
	}

	if (cache->trace.trace_callback) {
		if (io->dir == OCF_WRITE)
			ocf_trace_prep_io_event(&trace_event, req, ocf_event_operation_wr);
		else if (io->dir == OCF_READ)
			ocf_trace_prep_io_event(&trace_event, req, ocf_event_operation_rd);
	}

	fast = ocf_engine_hndl_fast_req(req);
	if (fast != OCF_FAST_PATH_NO) {
		ocf_trace_push(io->io_queue, &trace_event, sizeof(trace_event));
		return 0;
	}

	req->cache_mode = original_cache_mode;
	return -OCF_ERR_IO;
}

void ocf_core_volume_submit_io(struct ocf_io *io)
{
	struct ocf_request *req;
	ocf_core_t core;
	ocf_cache_t cache;
	int ret;

	OCF_CHECK_NULL(io);

	ret = ocf_core_validate_io(io);
	if (ret < 0) {
		ocf_io_end(io, ret);
		return;
	}

	req = ocf_io_to_req(io);
	core = ocf_volume_to_core(ocf_io_get_volume(io));
	cache = ocf_core_get_cache(core);

	ocf_trace_init_io(req);

	if (unlikely(!env_bit_test(ocf_cache_state_running,
					&cache->cache_state))) {
		ocf_io_end(io, -OCF_ERR_CACHE_NOT_AVAIL);
		return;
	}

	ret = ocf_req_alloc_map(req);
	if (ret) {
		ocf_io_end(io, ret);
		return;
	}

	req->part_id = ocf_user_part_class2id(cache, io->io_class);
	req->core = core;
	req->complete = ocf_req_complete;

	ocf_resolve_effective_cache_mode(cache, core, req);

	ocf_core_update_stats(core, io);

	ocf_io_get(io);
	/* Prevent race condition */
	ocf_req_get(req);

	if (!ocf_core_submit_io_fast(io, req, core, cache)) {
		ocf_core_seq_cutoff_update(core, req);
		ocf_req_put(req);
		return;
	}

	ocf_req_put(req);
	ocf_req_clear_map(req);
	ocf_core_seq_cutoff_update(core, req);

	if (io->dir == OCF_WRITE)
		ocf_trace_io(req, ocf_event_operation_wr);
	else if (io->dir == OCF_READ)
		ocf_trace_io(req, ocf_event_operation_rd);

	ret = ocf_engine_hndl_req(req);
	if (ret) {
		dec_counter_if_req_was_dirty(req);
		ocf_io_end(io, ret);
		ocf_io_put(io);
	}
}

static void ocf_core_volume_submit_flush(struct ocf_io *io)
{
	struct ocf_request *req;
	ocf_core_t core;
	ocf_cache_t cache;
	int ret;

	OCF_CHECK_NULL(io);

	ret = ocf_core_validate_io(io);
	if (ret < 0) {
		ocf_io_end(io, ret);
		return;
	}

	req = ocf_io_to_req(io);
	core = ocf_volume_to_core(ocf_io_get_volume(io));
	cache = ocf_core_get_cache(core);

	if (unlikely(!env_bit_test(ocf_cache_state_running,
			&cache->cache_state))) {
		ocf_io_end(io, -OCF_ERR_CACHE_NOT_AVAIL);
		return;
	}

	req->core = core;
	req->complete = ocf_req_complete;

	ocf_trace_io(req, ocf_event_operation_flush);
	ocf_io_get(io);

	ocf_engine_hndl_ops_req(req);
}

static void ocf_core_volume_submit_discard(struct ocf_io *io)
{
	struct ocf_request *req;
	ocf_core_t core;
	ocf_cache_t cache;
	int ret;

	OCF_CHECK_NULL(io);

	if (io->bytes == 0) {
		ocf_io_end(io, -OCF_ERR_INVAL);
		return;
	}

	ret = ocf_core_validate_io(io);
	if (ret < 0) {
		ocf_io_end(io, ret);
		return;
	}

	req = ocf_io_to_req(io);
	core = ocf_volume_to_core(ocf_io_get_volume(io));
	cache = ocf_core_get_cache(core);

	if (unlikely(!env_bit_test(ocf_cache_state_running,
			&cache->cache_state))) {
		ocf_io_end(io, -OCF_ERR_CACHE_NOT_AVAIL);
		return;
	}

	ret = ocf_req_alloc_map_discard(req);
	if (ret) {
		ocf_io_end(io, -OCF_ERR_NO_MEM);
		return;
	}

	req->core = core;
	req->complete = ocf_req_complete;

	ocf_trace_io(req, ocf_event_operation_discard);
	ocf_io_get(io);

	ocf_engine_hndl_discard_req(req);
}

/* *** VOLUME OPS *** */

static int ocf_core_volume_open(ocf_volume_t volume, void *volume_params)
{
	struct ocf_core_volume *core_volume = ocf_volume_get_priv(volume);
	const struct ocf_volume_uuid *uuid = ocf_volume_get_uuid(volume);
	ocf_core_t core = (ocf_core_t)uuid->data;

	core_volume->core = core;

	return 0;
}

static void ocf_core_volume_close(ocf_volume_t volume)
{
}

static unsigned int ocf_core_volume_get_max_io_size(ocf_volume_t volume)
{
	ocf_core_t core = ocf_volume_to_core(volume);

	return ocf_volume_get_max_io_size(&core->volume);
}

static uint64_t ocf_core_volume_get_byte_length(ocf_volume_t volume)
{
	ocf_core_t core = ocf_volume_to_core(volume);

	return ocf_volume_get_length(&core->volume);
}


/* *** IO OPS *** */

static int ocf_core_io_set_data(struct ocf_io *io,
		ctx_data_t *data, uint32_t offset)
{
	struct ocf_request *req;

	OCF_CHECK_NULL(io);

	if (!data || offset)
		return -OCF_ERR_INVAL;

	req = ocf_io_to_req(io);
	req->data = data;

	return 0;
}

static ctx_data_t *ocf_core_io_get_data(struct ocf_io *io)
{
	struct ocf_request *req;

	OCF_CHECK_NULL(io);

	req = ocf_io_to_req(io);
	return req->data;
}

const struct ocf_volume_properties ocf_core_volume_properties = {
	.name = "OCF Core",
	.io_priv_size = 0, /* Not used - custom allocator */
	.volume_priv_size = sizeof(struct ocf_core_volume),
	.caps = {
		.atomic_writes = 0,
	},
	.ops = {
		.submit_io = ocf_core_volume_submit_io,
		.submit_flush = ocf_core_volume_submit_flush,
		.submit_discard = ocf_core_volume_submit_discard,
		.submit_metadata = NULL,

		.open = ocf_core_volume_open,
		.close = ocf_core_volume_close,
		.get_max_io_size = ocf_core_volume_get_max_io_size,
		.get_length = ocf_core_volume_get_byte_length,
	},
	.io_ops = {
		.set_data = ocf_core_io_set_data,
		.get_data = ocf_core_io_get_data,
	},
	.deinit = NULL,
};

static int ocf_core_io_allocator_init(ocf_io_allocator_t allocator,
		uint32_t priv_size, const char *name)
{
	return 0;
}

static void ocf_core_io_allocator_deinit(ocf_io_allocator_t allocator)
{
}

static void *ocf_core_io_allocator_new(ocf_io_allocator_t allocator,
		ocf_volume_t volume, ocf_queue_t queue,
		uint64_t addr, uint32_t bytes, uint32_t dir)
{
	struct ocf_request *req;

	req = ocf_req_new(queue, NULL, addr, bytes, dir);
	if (!req)
		return NULL;

	return &req->ioi;
}

static void ocf_core_io_allocator_del(ocf_io_allocator_t allocator, void *obj)
{
	struct ocf_request *req;

	req = container_of(obj, struct ocf_request, ioi);
	ocf_req_put(req);
}

const struct ocf_io_allocator_type ocf_core_io_allocator_type = {
	.ops = {
		.allocator_init = ocf_core_io_allocator_init,
		.allocator_deinit = ocf_core_io_allocator_deinit,
		.allocator_new = ocf_core_io_allocator_new,
		.allocator_del = ocf_core_io_allocator_del,
	},
};

const struct ocf_volume_extended ocf_core_volume_extended = {
	.allocator_type = &ocf_core_io_allocator_type,
};

int ocf_core_volume_type_init(ocf_ctx_t ctx)
{
	return ocf_ctx_register_volume_type_extended(ctx, 0,
			&ocf_core_volume_properties,
			&ocf_core_volume_extended);
}

int ocf_core_get_info(ocf_core_t core, struct ocf_core_info *info)
{
	ocf_cache_t cache;

	OCF_CHECK_NULL(core);

	cache = ocf_core_get_cache(core);

	if (!info)
		return -OCF_ERR_INVAL;

	ENV_BUG_ON(env_memset(info, sizeof(*info), 0));

	info->core_size_bytes = ocf_volume_get_length(&core->volume);
	info->core_size = ocf_bytes_2_lines_round_up(cache,
			info->core_size_bytes);
	info->seq_cutoff_threshold = ocf_core_get_seq_cutoff_threshold(core);
	info->seq_cutoff_policy = ocf_core_get_seq_cutoff_policy(core);

	info->flushed = env_atomic_read(&core->flushed);
	info->dirty = env_atomic_read(&core->runtime_meta->dirty_clines);

	info->dirty_for = _calc_dirty_for(
			env_atomic64_read(&core->runtime_meta->dirty_since));

	return 0;
}

void ocf_core_set_priv(ocf_core_t core, void *priv)
{
	OCF_CHECK_NULL(core);
	core->priv = priv;
}

void *ocf_core_get_priv(ocf_core_t core)
{
	OCF_CHECK_NULL(core);
	return core->priv;
}
