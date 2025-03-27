/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef UTILS_CACHE_LINE_H_
#define UTILS_CACHE_LINE_H_

#include "../metadata/metadata.h"
#include "../concurrency/ocf_cache_line_concurrency.h"
#include "../ocf_space.h"
#include "../engine/cache_engine.h"
#include "../ocf_request.h"
#include "../ocf_def_priv.h"
#include "../cleaning/cleaning_ops.h"

/**
 * @file utils_cache_line.h
 * @brief OCF utilities for cache line operations
 */

static inline ocf_cache_line_size_t ocf_line_size(
		struct ocf_cache *cache)
{
	return cache->metadata.settings.size;
}

static inline uint64_t ocf_line_pages(struct ocf_cache *cache)
{
	return cache->metadata.settings.size / PAGE_SIZE;
}

static inline uint64_t ocf_line_sectors(struct ocf_cache *cache)
{
	return cache->metadata.settings.sector_count;
}

static inline uint64_t ocf_line_end_sector(struct ocf_cache *cache)
{
	return cache->metadata.settings.sector_end;
}

static inline uint64_t ocf_line_start_sector(struct ocf_cache *cache)
{
	return cache->metadata.settings.sector_start;
}

static inline uint64_t ocf_bytes_round_lines(struct ocf_cache *cache,
		uint64_t bytes)
{
	return (bytes + ocf_line_size(cache) - 1) / ocf_line_size(cache);
}

static inline uint64_t ocf_bytes_2_lines(struct ocf_cache *cache,
		uint64_t bytes)
{
	return bytes / ocf_line_size(cache);
}

static inline uint64_t ocf_bytes_2_lines_round_up(
		struct ocf_cache *cache, uint64_t bytes)
{
	return OCF_DIV_ROUND_UP(bytes, ocf_line_size(cache));
}

static inline uint64_t ocf_lines_2_bytes(struct ocf_cache *cache,
		uint64_t lines)
{
	return lines * ocf_line_size(cache);
}

/**
 * @brief Set cache line invalid
 *
 * @note Collision page must be locked by the caller (either exclusive access
 * to collision table page OR write lock on metadata hash bucket combined with
 * shared access to the collision page)
 *
 * @param cache Cache instance
 * @param start_bit Start bit of cache line for which state will be set
 * @param end_bit End bit of cache line for which state will be set
 * @param req OCF request
 * @param map_idx Array index to map containing cache line to invalid
 */
void set_cache_line_invalid(struct ocf_cache *cache, uint8_t start_bit,
		uint8_t end_bit, struct ocf_request *req, uint32_t map_idx);


/**
 * @brief Set cache line invalid without flush
 *
 * @note Collision page must be locked by the caller (either exclusive access
 * to collision table page OR write lock on metadata hash bucket combined with
 * shared access to the collision page)
 *
 * @param cache Cache instance
 * @param start_bit Start bit of cache line for which state will be set
 * @param end_bit End bit of cache line for which state will be set
 * @param line Cache line to invalid
 */
void set_cache_line_invalid_no_flush(struct ocf_cache *cache, uint8_t start_bit,
		uint8_t end_bit, ocf_cache_line_t line);

/**
 * @brief Set cache line valid
 *
 * @note Collision page must be locked by the caller (either exclusive access
 * to collision table page OR write lock on metadata hash bucket combined with
 * shared access to the collision page)
 *
 * @param cache Cache instance
 * @param start_bit Start bit of cache line for which state will be set
 * @param end_bit End bit of cache line for which state will be set
 * @param req OCF request
 * @param map_idx Array index to map containing cache line to invalid
 */
void set_cache_line_valid(struct ocf_cache *cache, uint8_t start_bit,
		uint8_t end_bit, struct ocf_request *req, uint32_t map_idx);

/**
 * @brief Set cache line clean
 *
 * @note Collision page must be locked by the caller (either exclusive access
 * to collision table page OR write lock on metadata hash bucket combined with
 * shared access to the collision page)
 *
 * @param cache Cache instance
 * @param start_bit Start bit of cache line for which state will be set
 * @param end_bit End bit of cache line for which state will be set
 * @param req OCF request
 * @param map_idx Array index to map containing cache line to invalid
 */
void set_cache_line_clean(struct ocf_cache *cache, uint8_t start_bit,
		uint8_t end_bit, struct ocf_request *req, uint32_t map_idx);

/**
 * @brief Set cache line dirty
 *
 * @note Collision page must be locked by the caller (either exclusive access
 * to collision table page OR write lock on metadata hash bucket combined with
 * shared access to the collision page)
 *
 * @param cache Cache instance
 * @param start_bit Start bit of cache line for which state will be set
 * @param end_bit End bit of cache line for which state will be set
 * @param req OCF request
 * @param map_idx Array index to map containing cache line to invalid
 */
void set_cache_line_dirty(struct ocf_cache *cache, uint8_t start_bit,
		uint8_t end_bit, struct ocf_request *req, uint32_t map_idx);

/**
 * @brief Remove cache line from cleaning policy
 *
 * @param cache - cache instance
 * @param line - cache line to be removed
 *
 */
static inline void ocf_purge_cleaning_policy(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	/* Remove from cleaning policy */
	ocf_cleaning_purge_cache_block(cache, line);
}

/**
 * @brief Set cache line clean and invalid and remove form lists
 *
 * @note Collision page must be locked by the caller (either exclusive access
 * to collision table page OR write lock on metadata hash bucket combined with
 * shared access to the collision page)
 *
 * @param cache Cache instance
 * @param start Start bit of range in cache line to purge
 * @param end End bit of range in cache line to purge
 * @param req OCF request
 * @param map_idx Array index to map containing cache line to purge
 */
static inline void _ocf_purge_cache_line_sec(struct ocf_cache *cache,
		uint8_t start, uint8_t stop, struct ocf_request *req,
		uint32_t map_idx)
{

	set_cache_line_clean(cache, start, stop, req, map_idx);

	set_cache_line_invalid(cache, start, stop, req, map_idx);
}

/**
 * @brief Purge cache line (remove from collision and cleaning policy,
 * move to free LRU list).
 *
 * @param req - OCF request to purge
 */
static inline void ocf_purge_map_info(struct ocf_request *req)
{
	uint32_t map_idx = 0;
	uint8_t start_bit;
	uint8_t end_bit;
	struct ocf_map_info *map = req->map;
	struct ocf_cache *cache = req->cache;
	uint32_t count = req->core_line_count;

	/* Purge range on the basis of map info
	 *
	 * | 01234567 | 01234567 | ... | 01234567 | 01234567 |
	 * | -----+++ | ++++++++ | +++ | ++++++++ | +++++--- |
	 * |   first  |          Middle           |   last   |
	 */

	for (map_idx = 0; map_idx < count; map_idx++) {
		if (map[map_idx].status == LOOKUP_MISS)
			continue;

		start_bit = 0;
		end_bit = ocf_line_end_sector(cache);

		if (map_idx == 0) {
			/* First */

			start_bit = BYTES_TO_SECTORS(req->byte_position)
					% ocf_line_sectors(cache);

		}

		if (map_idx == (count - 1)) {
			/* Last */

			end_bit = BYTES_TO_SECTORS(req->byte_position +
					req->byte_length - 1) %
					ocf_line_sectors(cache);
		}

		ocf_metadata_start_collision_shared_access(cache, map[map_idx].
				coll_idx);
		_ocf_purge_cache_line_sec(cache, start_bit, end_bit, req,
				map_idx);
		ocf_metadata_end_collision_shared_access(cache, map[map_idx].
				coll_idx);
	}
}

static inline
uint8_t ocf_map_line_start_sector(struct ocf_request *req, uint32_t line)
{
	if (line == 0) {
		return BYTES_TO_SECTORS(req->byte_position)
					% ocf_line_sectors(req->cache);
	}

	return 0;
}

static inline
uint8_t ocf_map_line_end_sector(struct ocf_request *req, uint32_t line)
{
	if (line == req->core_line_count - 1) {
		return BYTES_TO_SECTORS(req->byte_position +
					req->byte_length - 1) %
					ocf_line_sectors(req->cache);
	}

	return ocf_line_end_sector(req->cache);
}

static inline void ocf_set_valid_map_info(struct ocf_request *req)
{
	uint32_t map_idx = 0;
	uint8_t start_bit;
	uint8_t end_bit;
	struct ocf_cache *cache = req->cache;
	uint32_t count = req->core_line_count;
	struct ocf_map_info *map = req->map;

	/* Set valid bits for sectors on the basis of map info
	 *
	 * | 01234567 | 01234567 | ... | 01234567 | 01234567 |
	 * | -----+++ | ++++++++ | +++ | ++++++++ | +++++--- |
	 * |   first  |          Middle           |   last   |
	 */
	for (map_idx = 0; map_idx < count; map_idx++) {
		ENV_BUG_ON(map[map_idx].status == LOOKUP_MISS);

		start_bit = ocf_map_line_start_sector(req, map_idx);
		end_bit = ocf_map_line_end_sector(req, map_idx);

		ocf_metadata_start_collision_shared_access(cache, map[map_idx].
				coll_idx);
		set_cache_line_valid(cache, start_bit, end_bit, req, map_idx);
		ocf_metadata_end_collision_shared_access(cache, map[map_idx].
				coll_idx);
	}
}

static inline void ocf_set_dirty_map_info(struct ocf_request *req)
{
	uint32_t map_idx = 0;
	uint8_t start_bit;
	uint8_t end_bit;
	struct ocf_cache *cache = req->cache;
	uint32_t count = req->core_line_count;
	struct ocf_map_info *map = req->map;

	/* Set valid bits for sectors on the basis of map info
	 *
	 * | 01234567 | 01234567 | ... | 01234567 | 01234567 |
	 * | -----+++ | ++++++++ | +++ | ++++++++ | +++++--- |
	 * |   first  |          Middle           |   last   |
	 */

	for (map_idx = 0; map_idx < count; map_idx++) {
		start_bit = ocf_map_line_start_sector(req, map_idx);
		end_bit = ocf_map_line_end_sector(req, map_idx);

		ocf_metadata_start_collision_shared_access(cache, map[map_idx].
				coll_idx);
		set_cache_line_dirty(cache, start_bit, end_bit, req, map_idx);
		ocf_metadata_end_collision_shared_access(cache, map[map_idx].
				coll_idx);
	}
}

static inline void ocf_set_clean_map_info(struct ocf_request *req)
{
	uint32_t map_idx = 0;
	uint8_t start_bit;
	uint8_t end_bit;
	struct ocf_cache *cache = req->cache;
	uint32_t count = req->core_line_count;
	struct ocf_map_info *map = req->map;

	/* Set valid bits for sectors on the basis of map info
	 *
	 * | 01234567 | 01234567 | ... | 01234567 | 01234567 |
	 * | -----+++ | ++++++++ | +++ | ++++++++ | +++++--- |
	 * |   first  |          Middle           |   last   |
	 */

	for (map_idx = 0; map_idx < count; map_idx++) {
		start_bit = ocf_map_line_start_sector(req, map_idx);
		end_bit = ocf_map_line_end_sector(req, map_idx);

		ocf_metadata_start_collision_shared_access(cache, map[map_idx].
				coll_idx);
		set_cache_line_clean(cache, start_bit, end_bit, req, map_idx);
		ocf_metadata_end_collision_shared_access(cache, map[map_idx].
				coll_idx);
	}
}

/**
 * @brief Validate cache line size
 *
 * @param[in] size Cache line size
 *
 * @retval true cache line size is valid
 * @retval false cache line is invalid
 */
static inline bool ocf_cache_line_size_is_valid(uint64_t size)
{
	switch (size) {
	case ocf_cache_line_size_4:
	case ocf_cache_line_size_8:
	case ocf_cache_line_size_16:
	case ocf_cache_line_size_32:
	case ocf_cache_line_size_64:
		return true;
	default:
		return false;
	}
}

#endif /* UTILS_CACHE_LINE_H_ */
