/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "../ocf_cache_priv.h"
#include "../ocf_request.h"
#include "../metadata/metadata.h"
#include "../engine/cache_engine.h"
#include "../ocf_lru.h"
#include "utils_user_part.h"

static struct ocf_lst_entry *ocf_user_part_lst_getter_valid(
		struct ocf_cache *cache, ocf_cache_line_t idx)
{
	ENV_BUG_ON(idx > OCF_USER_IO_CLASS_MAX);
	return &cache->user_parts[idx].lst_valid;
}


static int ocf_user_part_lst_cmp_valid(struct ocf_cache *cache,
		struct ocf_lst_entry *e1, struct ocf_lst_entry *e2)
{
	struct ocf_user_part *p1 = container_of(e1, struct ocf_user_part,
			lst_valid);
	struct ocf_user_part *p2 = container_of(e2, struct ocf_user_part,
			lst_valid);
	size_t p1_size = ocf_cache_is_device_attached(cache) ?
				env_atomic_read(&p1->part.runtime->curr_size)
				: 0;
	size_t p2_size = ocf_cache_is_device_attached(cache) ?
				env_atomic_read(&p2->part.runtime->curr_size)
				: 0;
	int v1 = p1->config->priority;
	int v2 = p2->config->priority;

	/*
	 * If partition is invalid the priority depends on current size:
	 * 1. Partition is empty - move to the end of list
	 * 2. Partition is not empty  - move to the beginning of the list. This
	 * partition will be evicted first
	 */

	if (p1->config->priority == OCF_IO_CLASS_PRIO_PINNED)
		p1->config->flags.eviction = false;
	else
		p1->config->flags.eviction = true;

	if (p2->config->priority == OCF_IO_CLASS_PRIO_PINNED)
		p2->config->flags.eviction = false;
	else
		p2->config->flags.eviction = true;

	if (!p1->config->flags.valid) {
		if (p1_size) {
			v1 = SHRT_MAX;
			p1->config->flags.eviction = true;
		} else {
			v1 = SHRT_MIN;
			p1->config->flags.eviction = false;
		}
	}

	if (!p2->config->flags.valid) {
		if (p2_size) {
			v2 = SHRT_MAX;
			p2->config->flags.eviction = true;
		} else {
			v2 = SHRT_MIN;
			p2->config->flags.eviction = false;
		}
	}

	if (v1 == v2) {
		v1 = p1 - cache->user_parts;
		v2 = p2 - cache->user_parts;
	}

	return v2 - v1;
}

void ocf_user_part_init(struct ocf_cache *cache)
{
	unsigned i;

	ocf_lst_init(cache, &cache->user_part_list, OCF_USER_IO_CLASS_MAX,
			ocf_user_part_lst_getter_valid,
			ocf_user_part_lst_cmp_valid);

	for (i = 0; i < OCF_USER_IO_CLASS_MAX + 1; i++)
		cache->user_parts[i].part.id = i;
}

void ocf_user_part_move(struct ocf_request *req)
{
	struct ocf_cache *cache = req->cache;
	struct ocf_map_info *entry;
	ocf_cache_line_t line;
	ocf_part_id_t id_old, id_new;
	uint32_t i;

	entry = &req->map[0];
	for (i = 0; i < req->core_line_count; i++, entry++) {
		if (!entry->re_part) {
			/* Changing partition not required */
			continue;
		}

		/* Moving cachelines to another partition is needed only
		 * for those already mapped before this request and remapped
		 * cachelines are assigned to target partition during eviction.
		 * So only hit cachelines are interesting.
		 */
		if (entry->status != LOOKUP_HIT) {
			/* No HIT */
			continue;
		}

		line = entry->coll_idx;
		id_old = ocf_metadata_get_partition_id(cache, line);
		id_new = req->part_id;

		ENV_BUG_ON(id_old >= OCF_USER_IO_CLASS_MAX ||
				id_new >= OCF_USER_IO_CLASS_MAX);

		if (id_old == id_new) {
			/* Partition of the request and cache line is the same,
			 * no need to change partition
			 */
			continue;
		}

		if (metadata_test_dirty(cache, line)) {
			/*
			 * Remove cline from cleaning - this if for ioclass
			 * oriented cleaning policy (e.g. ALRU).
			 * TODO: Consider adding update_cache_line() ops
			 * to cleaning policy to let policies handle this.
			 */
			ocf_cleaning_purge_cache_block(cache, line);
		}

		ocf_lru_repart(cache, line, &cache->user_parts[id_old].part,
				&cache->user_parts[id_new].part);

		/* Check if cache line is dirty. If yes then need to change
		 * cleaning  policy and update partition dirty clines
		 * statistics.
		 */
		if (metadata_test_dirty(cache, line)) {
			/* Add cline back to cleaning policy */
			ocf_cleaning_set_hot_cache_line(cache, line);

			env_atomic_inc(&req->core->runtime_meta->
					part_counters[id_new].dirty_clines);
			env_atomic_dec(&req->core->runtime_meta->
					part_counters[id_old].dirty_clines);
		}

		env_atomic_inc(&req->core->runtime_meta->
				part_counters[id_new].cached_clines);
		env_atomic_dec(&req->core->runtime_meta->
				part_counters[id_old].cached_clines);

		/* DONE */
	}
}

void ocf_user_part_set_valid(struct ocf_cache *cache, ocf_part_id_t id,
		bool valid)
{
	struct ocf_user_part *user_part = &cache->user_parts[id];

	if (valid ^ user_part->config->flags.valid) {
		if (valid) {
			user_part->config->flags.valid = true;
			cache->conf_meta->valid_parts_no++;
		} else {
			user_part->config->flags.valid = false;
			cache->conf_meta->valid_parts_no--;
			user_part->config->priority = OCF_IO_CLASS_PRIO_LOWEST;
			user_part->config->min_size = 0;
			user_part->config->max_size = PARTITION_SIZE_MAX;
			ENV_BUG_ON(env_strncpy(user_part->config->name,
					sizeof(user_part->config->name),
					"Inactive", 9));
		}
	}
}
