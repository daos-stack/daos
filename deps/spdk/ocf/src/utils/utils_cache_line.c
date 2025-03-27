/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "utils_cache_line.h"
#include "../promotion/promotion.h"

static void __set_cache_line_invalid(struct ocf_cache *cache, uint8_t start_bit,
		uint8_t end_bit, ocf_cache_line_t line,
		ocf_core_id_t core_id, ocf_part_id_t part_id)
{
	ocf_core_t core;
	bool is_valid;

	ENV_BUG_ON(core_id >= OCF_CORE_MAX);
	core = ocf_cache_get_core(cache, core_id);

	if (metadata_clear_valid_sec_changed(cache, line, start_bit, end_bit,
			&is_valid)) {
		/*
		 * Update the number of cached data for that core object
		 */
		env_atomic_dec(&core->runtime_meta->cached_clines);
		env_atomic_dec(&core->runtime_meta->
				part_counters[part_id].cached_clines);
	}

	/* If we have waiters, do not remove cache line
	 * for this cache line which will use one, clear
	 * only valid bits
	 */
	if (!is_valid && !ocf_cache_line_are_waiters(
			ocf_cache_line_concurrency(cache), line)) {
		ocf_lru_rm_cline(cache, line);
		ocf_metadata_remove_cache_line(cache, line);
	}
}

void set_cache_line_invalid(struct ocf_cache *cache, uint8_t start_bit,
		uint8_t end_bit, struct ocf_request *req, uint32_t map_idx)
{
	ocf_cache_line_t line = req->map[map_idx].coll_idx;
	ocf_part_id_t part_id;
	ocf_core_id_t core_id;

	ENV_BUG_ON(!req);

	part_id = ocf_metadata_get_partition_id(cache, line);
	core_id = ocf_core_get_id(req->core);

	__set_cache_line_invalid(cache, start_bit, end_bit, line, core_id,
			part_id);

	ocf_metadata_flush_mark(cache, req, map_idx, INVALID, start_bit,
			end_bit);
}

void set_cache_line_invalid_no_flush(struct ocf_cache *cache, uint8_t start_bit,
		uint8_t end_bit, ocf_cache_line_t line)
{
	ocf_part_id_t part_id;
	ocf_core_id_t core_id;

	ocf_metadata_get_core_and_part_id(cache, line, &core_id, &part_id);

	__set_cache_line_invalid(cache, start_bit, end_bit, line, core_id,
			part_id);
}

void set_cache_line_valid(struct ocf_cache *cache, uint8_t start_bit,
		uint8_t end_bit, struct ocf_request *req, uint32_t map_idx)
{
	ocf_cache_line_t line = req->map[map_idx].coll_idx;
	ocf_part_id_t part_id = ocf_metadata_get_partition_id(cache, line);

	if (metadata_set_valid_sec_changed(cache, line, start_bit, end_bit)) {
		/*
		 * Update the number of cached data for that core object
		 */
		env_atomic_inc(&req->core->runtime_meta->cached_clines);
		env_atomic_inc(&req->core->runtime_meta->
				part_counters[part_id].cached_clines);
	}
}

void set_cache_line_clean(struct ocf_cache *cache, uint8_t start_bit,
		uint8_t end_bit, struct ocf_request *req, uint32_t map_idx)
{
	ocf_cache_line_t line = req->map[map_idx].coll_idx;
	ocf_part_id_t part_id = ocf_metadata_get_partition_id(cache, line);
	struct ocf_part *part = &cache->user_parts[part_id].part;
	bool line_is_clean;

	ENV_BUG_ON(part_id > OCF_USER_IO_CLASS_MAX);
	part = &cache->user_parts[part_id].part;

	if (metadata_clear_dirty_sec_changed(cache, line, start_bit, end_bit,
				&line_is_clean)) {
		ocf_metadata_flush_mark(cache, req, map_idx, CLEAN, start_bit,
				end_bit);
		if (line_is_clean) {
			/*
			 * Update the number of dirty cached data for that
			 * core object
			 */
			if (env_atomic_dec_and_test(&req->core->runtime_meta->
						dirty_clines)) {
				/*
				 * If this is last dirty cline reset dirty
				 * timestamp
				 */
				env_atomic64_set(&req->core->runtime_meta->
						dirty_since, 0);
			}

			/*
			 * decrement dirty clines statistic for given cline
			 */
			env_atomic_dec(&req->core->runtime_meta->
					part_counters[part_id].dirty_clines);
			ocf_lru_clean_cline(cache, part, line);
			ocf_purge_cleaning_policy(cache, line);
		}
	}

}

void set_cache_line_dirty(struct ocf_cache *cache, uint8_t start_bit,
		uint8_t end_bit, struct ocf_request *req, uint32_t map_idx)
{
	ocf_cache_line_t line = req->map[map_idx].coll_idx;
	ocf_part_id_t part_id = ocf_metadata_get_partition_id(cache, line);
	struct ocf_part *part;
	bool line_was_dirty;

	ENV_BUG_ON(part_id > OCF_USER_IO_CLASS_MAX);
	part = &cache->user_parts[part_id].part;

	if (metadata_set_dirty_sec_changed(cache, line, start_bit, end_bit,
				&line_was_dirty)) {
		ocf_metadata_flush_mark(cache, req, map_idx, DIRTY, start_bit,
				end_bit);
		if (!line_was_dirty) {
			/*
			 * If this is first dirty cline set dirty timestamp
			 */
			if (!env_atomic64_read(&req->core->runtime_meta->dirty_since))
				env_atomic64_cmpxchg(
					&req->core->runtime_meta->dirty_since, 0,
					env_ticks_to_secs(env_get_tick_count()));

			/*
			 * Update the number of dirty cached data for that
			 * core object
			 */
			env_atomic_inc(&req->core->runtime_meta->dirty_clines);

			/*
			 * increment dirty clines statistic for given cline
			 */
			env_atomic_inc(&req->core->runtime_meta->
					part_counters[part_id].dirty_clines);
			ocf_lru_dirty_cline(cache, part, line);
		}
	}

	ocf_cleaning_set_hot_cache_line(cache, line);
}
