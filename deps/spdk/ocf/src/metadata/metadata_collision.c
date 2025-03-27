/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "metadata.h"
#include "metadata_internal.h"
#include "../utils/utils_cache_line.h"

static ocf_cache_line_t ocf_metadata_map_lg2phy_seq(
		struct ocf_cache *cache, ocf_cache_line_t coll_idx)
{
	return coll_idx;
}

static ocf_cache_line_t ocf_metadata_map_phy2lg_seq(
		struct ocf_cache *cache, ocf_cache_line_t cache_line)
{
	return cache_line;
}

/**
 * This function is mapping collision index to appropriate cache line
 * (logical cache line to physical one mapping).
 *
 * It is necessary because we want to generate sequential workload with
 * data to cache device.
 *	Our collision list, for example, looks:
 *			0 3 6 9
 *			1 4 7 10
 *			2 5 8
 *	All collision index in each column is on the same page
 *	on cache device. We don't want send request x times to the same
 *	page. To don't do it we use collision index by row, but in this
 *	case we can't use collision index directly as cache line,
 *	because we will generate non sequential workload (we will write
 *	pages: 0 -> 3 -> 6 ...). To map collision index in correct way
 *	we use this function.
 *
 *	After use this function, collision index in the above array
 *	corresponds with below cache line:
 *			0 1 2 3
 *			4 5 6 7
 *			8 9 10
 *
 * @param cache - cache instance
 * @param idx - index in collision list
 * @return mapped cache line
 */static ocf_cache_line_t ocf_metadata_map_lg2phy_striping(
		struct ocf_cache *cache, ocf_cache_line_t coll_idx)
{
	ocf_cache_line_t cache_line = 0, offset = 0;
	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;
	unsigned int entries_in_page =
		ctrl->raw_desc[metadata_segment_collision].entries_in_page;
	unsigned int pages =
		ctrl->raw_desc[metadata_segment_collision].ssd_pages;
	ocf_cache_line_t collision_table_entries =
			cache->device->collision_table_entries;
	ocf_cache_line_t delta =
			(entries_in_page * pages) - collision_table_entries;
	unsigned int row = coll_idx % entries_in_page;

	if (row > entries_in_page - delta)
		offset = row - (entries_in_page - delta);
	else
		offset = 0;

	cache_line = (row * pages) +  (coll_idx / entries_in_page) - offset;
	return cache_line;
}

/**
 * @brief Map physical cache line on cache device to logical one
 * @note This function is the inverse of map_coll_idx_to_cache_line
 *
 * @param cache Cache instance
 * @param phy Physical cache line of cache device
 * @return Logical cache line
 */
static ocf_cache_line_t ocf_metadata_map_phy2lg_striping(
		struct ocf_cache *cache, ocf_cache_line_t cache_line)
{
	ocf_cache_line_t coll_idx = 0;

	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;

	struct ocf_metadata_raw *raw =
		&ctrl->raw_desc[metadata_segment_collision];

	unsigned int pages = raw->ssd_pages;
	unsigned int entries_in_page = raw->entries_in_page;
	unsigned int entries_in_last_page = raw->entries % entries_in_page ?:
			entries_in_page;

	unsigned int row = 0, coll = 0;

	unsigned int last = entries_in_last_page * pages;

	if (cache_line < last) {
		row = cache_line % pages;
		coll = cache_line / pages;
	} else {
		cache_line -= last;
		row = cache_line % (pages - 1);
		coll = cache_line / (pages - 1) + entries_in_last_page;
	}

	coll_idx = (row * entries_in_page) + coll;

	return coll_idx;
}
ocf_cache_line_t ocf_metadata_map_lg2phy(
		struct ocf_cache *cache, ocf_cache_line_t coll_idx)
{
	switch (cache->metadata.layout) {
	case ocf_metadata_layout_striping:
		return ocf_metadata_map_lg2phy_striping(
				cache, coll_idx);
	case ocf_metadata_layout_seq:
		return ocf_metadata_map_lg2phy_seq(
				cache, coll_idx);
	default:
		ENV_BUG();
		return 0;
	}
}

ocf_cache_line_t ocf_metadata_map_phy2lg(
		struct ocf_cache *cache, ocf_cache_line_t cache_line)
{
	switch (cache->metadata.layout) {
	case ocf_metadata_layout_striping:
		return ocf_metadata_map_phy2lg_striping(
				cache, cache_line);
	case ocf_metadata_layout_seq:
		return ocf_metadata_map_phy2lg_seq(
				cache, cache_line);
	default:
		ENV_BUG();
		return 0;
	}
}

void ocf_metadata_set_collision_info(struct ocf_cache *cache,
		ocf_cache_line_t line, ocf_cache_line_t next,
		ocf_cache_line_t prev)
{
	struct ocf_metadata_list_info *info;
	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;

	info = ocf_metadata_raw_wr_access(cache,
			&(ctrl->raw_desc[metadata_segment_list_info]), line);

	if (info) {
		info->next_col = next;
		info->prev_col = prev;
	} else {
		ocf_metadata_error(cache);
	}
}

void ocf_metadata_set_collision_next(struct ocf_cache *cache,
		ocf_cache_line_t line, ocf_cache_line_t next)
{
	struct ocf_metadata_list_info *info;
	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;

	info = ocf_metadata_raw_wr_access(cache,
			&(ctrl->raw_desc[metadata_segment_list_info]), line);

	if (info)
		info->next_col = next;
	else
		ocf_metadata_error(cache);
}

void ocf_metadata_set_collision_prev(struct ocf_cache *cache,
		ocf_cache_line_t line, ocf_cache_line_t prev)
{
	struct ocf_metadata_list_info *info;
	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;

	info = ocf_metadata_raw_wr_access(cache,
			&(ctrl->raw_desc[metadata_segment_list_info]), line);

	if (info)
		info->prev_col = prev;
	else
		ocf_metadata_error(cache);
}

void ocf_metadata_get_collision_info(struct ocf_cache *cache,
		ocf_cache_line_t line, ocf_cache_line_t *next,
		ocf_cache_line_t *prev)
{
	const struct ocf_metadata_list_info *info;
	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;

	ENV_BUG_ON(NULL == next && NULL == prev);

	info = ocf_metadata_raw_rd_access(cache,
			&(ctrl->raw_desc[metadata_segment_list_info]), line);
	if (info) {
		if (next)
			*next = info->next_col;
		if (prev)
			*prev = info->prev_col;
	} else {
		ocf_metadata_error(cache);

		if (next)
			*next = cache->device->collision_table_entries;
		if (prev)
			*prev = cache->device->collision_table_entries;
	}
}


/*
 *
 */
void ocf_metadata_add_to_collision(struct ocf_cache *cache,
		ocf_core_id_t core_id, uint64_t core_line,
		ocf_cache_line_t hash, ocf_cache_line_t cache_line)
{
	ocf_cache_line_t prev_cache_line = ocf_metadata_get_hash(cache, hash);
	ocf_cache_line_t line_entries = cache->device->collision_table_entries;
	ocf_cache_line_t hash_entries = cache->device->hash_table_entries;

	ENV_BUG_ON(!(hash < hash_entries));
	ENV_BUG_ON(!(cache_line < line_entries));

	/* Setup new node */
	ocf_metadata_set_core_info(cache, cache_line, core_id,
			core_line);

	/* Update collision info:
	 * - next is set to value from hash table;
	 * - previous is set to collision table entries value
	 */
	ocf_metadata_set_collision_info(cache, cache_line, prev_cache_line,
			line_entries);

	/* Update previous head */
	if (prev_cache_line != line_entries) {
		ocf_metadata_set_collision_prev(cache, prev_cache_line,
				cache_line);
	}

	/* Update hash Table: hash table contains pointer to
	 * collision table so it contains indexes in collision table
	 */
	ocf_metadata_set_hash(cache, hash, cache_line);
}

/*
 *
 */
void ocf_metadata_remove_from_collision(struct ocf_cache *cache,
		ocf_cache_line_t line, ocf_part_id_t part_id)
{
	ocf_core_id_t core_id;
	uint64_t core_sector;
	ocf_cache_line_t hash_father;
	ocf_cache_line_t prev_line, next_line;
	ocf_cache_line_t line_entries = cache->device->collision_table_entries;
	ocf_cache_line_t hash_entries = cache->device->hash_table_entries;

	ENV_BUG_ON(!(line < line_entries));

	ocf_metadata_get_collision_info(cache, line, &next_line, &prev_line);

	/* Update previous node if any. */
	if (prev_line != line_entries)
		ocf_metadata_set_collision_next(cache, prev_line, next_line);

	/* Update next node if any. */
	if (next_line != line_entries)
		ocf_metadata_set_collision_prev(cache, next_line, prev_line);

	ocf_metadata_get_core_info(cache, line, &core_id, &core_sector);

	/* Update hash table, because if it was pointing to the given node it
	 * must now point to the given's node next
	 */
	hash_father = ocf_metadata_hash_func(cache, core_sector, core_id);
	ENV_BUG_ON(!(hash_father < hash_entries));

	if (ocf_metadata_get_hash(cache, hash_father) == line)
		ocf_metadata_set_hash(cache, hash_father, next_line);

	ocf_metadata_set_collision_info(cache, line,
			line_entries, line_entries);

	ocf_metadata_set_core_info(cache, line,
			OCF_CORE_MAX, ULLONG_MAX);
}

/* must be called under global metadata read(shared) lock */
void ocf_metadata_start_collision_shared_access(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;
	struct ocf_metadata_raw *raw =
			&ctrl->raw_desc[metadata_segment_collision];
	uint32_t page = ocf_metadata_raw_page(raw, line);

	ocf_collision_start_shared_access(&cache->metadata.lock, page);
}

/* must be called under  global metadata read(shared) lock */
void ocf_metadata_end_collision_shared_access(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;
	struct ocf_metadata_raw *raw =
			&ctrl->raw_desc[metadata_segment_collision];
	uint32_t page = ocf_metadata_raw_page(raw, line);

	ocf_collision_end_shared_access(&cache->metadata.lock, page);
}


