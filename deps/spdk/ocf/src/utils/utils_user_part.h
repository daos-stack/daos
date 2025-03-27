/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __UTILS_PARTITION_H__
#define __UTILS_PARTITION_H__

#include "../ocf_request.h"
#include "../engine/cache_engine.h"
#include "../engine/engine_common.h"
#include "../metadata/metadata_partition.h"

void ocf_user_part_init(struct ocf_cache *cache);

static inline bool ocf_user_part_is_valid(struct ocf_user_part *user_part)
{
	return !!user_part->config->flags.valid;
}

static inline void ocf_user_part_set_prio(struct ocf_cache *cache,
		struct ocf_user_part *user_part, int16_t prio)
{
	if (user_part->config->priority != prio)
		user_part->config->priority = prio;
}

static inline int16_t ocf_user_part_get_prio(struct ocf_cache *cache,
		ocf_part_id_t part_id)
{
	if (part_id < OCF_USER_IO_CLASS_MAX)
		return cache->user_parts[part_id].config->priority;

	return OCF_IO_CLASS_PRIO_LOWEST;
}

void ocf_user_part_set_valid(struct ocf_cache *cache, ocf_part_id_t id,
		bool valid);

static inline bool ocf_user_part_is_added(struct ocf_user_part *user_part)
{
	return !!user_part->config->flags.added;
}

static inline ocf_part_id_t ocf_user_part_class2id(ocf_cache_t cache, uint64_t class)
{
	if (class < OCF_USER_IO_CLASS_MAX)
		if (cache->user_parts[class].config->flags.valid)
			return class;

	return PARTITION_DEFAULT;
}

static inline uint32_t ocf_part_get_occupancy(struct ocf_part *part)
{
	return env_atomic_read(&part->runtime->curr_size);
}

static inline uint32_t ocf_user_part_get_min_size(ocf_cache_t cache,
		struct ocf_user_part *user_part)
{
	uint64_t ioclass_size;

	ioclass_size = (uint64_t)user_part->config->min_size *
		(uint64_t)cache->conf_meta->cachelines;

	ioclass_size /= 100;

	return (uint32_t)ioclass_size;
}


static inline uint32_t ocf_user_part_get_max_size(ocf_cache_t cache,
		struct ocf_user_part *user_part)
{
	uint64_t ioclass_size, max_size, cache_size;

	max_size = user_part->config->max_size;
	cache_size = cache->conf_meta->cachelines;

	ioclass_size =  max_size * cache_size;
	ioclass_size = OCF_DIV_ROUND_UP(ioclass_size, 100);

	return (uint32_t)ioclass_size;
}

void ocf_user_part_move(struct ocf_request *req);

#define for_each_user_part(cache, user_part, id) \
	for_each_lst_entry(&cache->user_part_list, user_part, id, \
		struct ocf_user_part, lst_valid)

static inline void ocf_user_part_sort(struct ocf_cache *cache)
{
	ocf_lst_sort(&cache->user_part_list);
}

static inline bool ocf_user_part_is_enabled(struct ocf_user_part *user_part)
{
	return user_part->config->max_size != 0;
}

static inline uint32_t ocf_user_part_overflow_size(struct ocf_cache *cache,
		struct ocf_user_part *user_part)
{
	uint32_t part_occupancy = ocf_part_get_occupancy(&user_part->part);
	uint32_t part_occupancy_limit = ocf_user_part_get_max_size(cache,
			user_part);

	if (part_occupancy > part_occupancy_limit)
		return part_occupancy - part_occupancy_limit;

	return 0;
}

static inline bool ocf_user_part_has_space(struct ocf_request *req)
{
	struct ocf_user_part *user_part = &req->cache->user_parts[req->part_id];
	uint64_t part_occupancy_limit =
		ocf_user_part_get_max_size(req->cache, user_part);
	uint64_t needed_cache_lines = ocf_engine_repart_count(req) +
		ocf_engine_unmapped_count(req);
	uint64_t part_occupancy = ocf_part_get_occupancy(&user_part->part);

	return (part_occupancy + needed_cache_lines <= part_occupancy_limit);
}

static inline ocf_cache_mode_t ocf_user_part_get_cache_mode(ocf_cache_t cache,
		ocf_part_id_t part_id)
{
	if (part_id < OCF_USER_IO_CLASS_MAX)
		return cache->user_parts[part_id].config->cache_mode;
	return ocf_cache_mode_none;
}

static inline bool ocf_user_part_is_prio_valid(int64_t prio)
{
	switch (prio) {
	case OCF_IO_CLASS_PRIO_HIGHEST ... OCF_IO_CLASS_PRIO_LOWEST:
	case OCF_IO_CLASS_PRIO_PINNED:
		return true;

	default:
		return false;
	}
}

/**
 * routine checks for validity of a partition name.
 *
 * Following condition is checked:
 * - string too long
 * - string containing invalid characters (outside of low ascii)
 * Following condition is NOT cheched:
 * - empty string. (empty string is NOT a valid partition name, but
 *   this function returns true on empty string nevertheless).
 *
 * @return returns true if partition name is a valid name
 */
static inline bool ocf_user_part_is_name_valid(const char *name)
{
	uint32_t length = 0;

	while (*name) {
		if (*name < ' ' || *name > '~')
			return false;

		if (',' == *name || '"' == *name)
			return false;

		name++;
		length++;

		if (length >= OCF_IO_CLASS_NAME_MAX)
			return false;
	}

	return true;
}

#endif /* __UTILS_PARTITION_H__ */
