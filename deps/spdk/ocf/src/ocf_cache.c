/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "metadata/metadata.h"
#include "engine/cache_engine.h"
#include "utils/utils_cache_line.h"
#include "ocf_request.h"
#include "utils/utils_user_part.h"
#include "ocf_priv.h"
#include "ocf_cache_priv.h"
#include "ocf_queue_priv.h"
#include "utils/utils_stats.h"

ocf_volume_t ocf_cache_get_volume(ocf_cache_t cache)
{
	return cache->device ? &cache->device->volume : NULL;
}

int ocf_cache_set_name(ocf_cache_t cache, const char *src, size_t src_size)
{
	OCF_CHECK_NULL(cache);
	return env_strncpy(cache->conf_meta->name, OCF_CACHE_NAME_SIZE,
			src, src_size);
}

const char *ocf_cache_get_name(ocf_cache_t cache)
{
	OCF_CHECK_NULL(cache);
	return cache->conf_meta->name;
}

bool ocf_cache_is_incomplete(ocf_cache_t cache)
{
	OCF_CHECK_NULL(cache);
	return env_bit_test(ocf_cache_state_incomplete, &cache->cache_state);
}

bool ocf_cache_is_running(ocf_cache_t cache)
{
	OCF_CHECK_NULL(cache);
	return env_bit_test(ocf_cache_state_running, &cache->cache_state);
}

bool ocf_cache_is_device_attached(ocf_cache_t cache)
{
	OCF_CHECK_NULL(cache);
	return !ocf_refcnt_frozen(&cache->refcnt.metadata);
}

ocf_cache_mode_t ocf_cache_get_mode(ocf_cache_t cache)
{
	OCF_CHECK_NULL(cache);

	return cache->conf_meta->cache_mode;
}

static uint64_t _calc_dirty_for(uint64_t dirty_since)
{
	uint64_t current_time = env_ticks_to_secs(env_get_tick_count());

	return dirty_since ? (current_time - dirty_since) : 0;
}

int ocf_cache_get_info(ocf_cache_t cache, struct ocf_cache_info *info)
{
	uint32_t cache_occupancy_total = 0;
	uint32_t dirty_blocks_total = 0;
	uint32_t initial_dirty_blocks_total = 0;
	uint32_t flushed_total = 0;
	uint32_t curr_dirty_cnt;
	uint64_t dirty_since = 0;
	uint32_t init_dirty_cnt;
	uint64_t core_dirty_since;
	uint32_t dirty_blocks_inactive = 0;
	uint32_t cache_occupancy_inactive = 0;
	ocf_core_t core;
	ocf_core_id_t core_id;

	OCF_CHECK_NULL(cache);

	if (!info)
		return -OCF_ERR_INVAL;

	ENV_BUG_ON(env_memset(info, sizeof(*info), 0));

	_ocf_stats_zero(&info->inactive);

	info->attached = ocf_cache_is_device_attached(cache);
	if (info->attached) {
		info->volume_type = ocf_ctx_get_volume_type_id(cache->owner,
				cache->device->volume.type);
		info->size = cache->conf_meta->cachelines;
	}
	info->core_count = cache->conf_meta->core_count;

	info->cache_mode = ocf_cache_get_mode(cache);

	/* iterate through all possibly valid core objcts, as list of
	 * valid objects may be not continuous
	 */
	for_each_core(cache, core, core_id) {
		/* If current dirty blocks exceeds saved initial dirty
		 * blocks then update the latter
		 */
		curr_dirty_cnt = env_atomic_read(
				&core->runtime_meta->dirty_clines);
		init_dirty_cnt = env_atomic_read(
				&core->runtime_meta->initial_dirty_clines);
		if (init_dirty_cnt && (curr_dirty_cnt > init_dirty_cnt)) {
			env_atomic_set(
				&core->runtime_meta->initial_dirty_clines,
				env_atomic_read(
					&core->runtime_meta->dirty_clines));
		}
		cache_occupancy_total += env_atomic_read(
				&core->runtime_meta->cached_clines);

		dirty_blocks_total += env_atomic_read(
				&core->runtime_meta->dirty_clines);
		initial_dirty_blocks_total += env_atomic_read(
				&core->runtime_meta->initial_dirty_clines);

		if (!core->opened) {
			cache_occupancy_inactive += env_atomic_read(
				&core->runtime_meta->cached_clines);

			dirty_blocks_inactive += env_atomic_read(
				&core->runtime_meta->dirty_clines);
		}
		core_dirty_since = env_atomic64_read(
				&core->runtime_meta->dirty_since);
		if (core_dirty_since) {
			dirty_since = (dirty_since ?
				OCF_MIN(dirty_since, core_dirty_since) :
				core_dirty_since);
		}

		flushed_total += env_atomic_read(&core->flushed);
	}

	info->dirty = dirty_blocks_total;
	info->dirty_initial = initial_dirty_blocks_total;
	info->occupancy = cache_occupancy_total;
	info->dirty_for = _calc_dirty_for(dirty_since);
	info->metadata_end_offset = ocf_cache_is_device_attached(cache) ?
			cache->device->metadata_offset / PAGE_SIZE : 0;

	info->state = cache->cache_state;

	if (info->attached) {
		_set(&info->inactive.occupancy,
				_lines4k(cache_occupancy_inactive, ocf_line_size(cache)),
				_lines4k(info->size, ocf_line_size(cache)));
		_set(&info->inactive.clean,
				_lines4k(cache_occupancy_inactive - dirty_blocks_inactive,
					ocf_line_size(cache)),
				_lines4k(cache_occupancy_total, ocf_line_size(cache)));
		_set(&info->inactive.dirty,
				_lines4k(dirty_blocks_inactive, ocf_line_size(cache)),
				_lines4k(cache_occupancy_total, ocf_line_size(cache)));
	}

	info->flushed = (env_atomic_read(&cache->flush_in_progress)) ?
			flushed_total : 0;

	info->fallback_pt.status = ocf_fallback_pt_is_on(cache);
	info->fallback_pt.error_counter =
		env_atomic_read(&cache->fallback_pt_error_counter);

	info->cleaning_policy = cache->conf_meta->cleaning_policy_type;
	info->promotion_policy = cache->conf_meta->promotion_policy_type;
	info->metadata_footprint = ocf_cache_is_device_attached(cache) ?
			ocf_metadata_size_of(cache) : 0;
	info->cache_line_size = ocf_line_size(cache);

	return 0;
}

const struct ocf_volume_uuid *ocf_cache_get_uuid(ocf_cache_t cache)
{
	if (!ocf_cache_is_device_attached(cache))
		return NULL;

	return ocf_volume_get_uuid(ocf_cache_get_volume(cache));
}

uint8_t ocf_cache_get_type_id(ocf_cache_t cache)
{
	if (!ocf_cache_is_device_attached(cache))
		return 0xff;

	return ocf_ctx_get_volume_type_id(ocf_cache_get_ctx(cache),
		ocf_volume_get_type(ocf_cache_get_volume(cache)));
}

ocf_cache_line_size_t ocf_cache_get_line_size(ocf_cache_t cache)
{
	OCF_CHECK_NULL(cache);
	return ocf_line_size(cache);
}

uint64_t ocf_cache_bytes_2_lines(ocf_cache_t cache, uint64_t bytes)
{
	OCF_CHECK_NULL(cache);
	return ocf_bytes_2_lines(cache, bytes);
}

uint32_t ocf_cache_get_core_count(ocf_cache_t cache)
{
	OCF_CHECK_NULL(cache);
	return cache->conf_meta->core_count;
}

ocf_ctx_t ocf_cache_get_ctx(ocf_cache_t cache)
{
	OCF_CHECK_NULL(cache);
	return cache->owner;
}

void ocf_cache_set_priv(ocf_cache_t cache, void *priv)
{
	OCF_CHECK_NULL(cache);
	cache->priv = priv;
}

void *ocf_cache_get_priv(ocf_cache_t cache)
{
	OCF_CHECK_NULL(cache);
	return cache->priv;
}
