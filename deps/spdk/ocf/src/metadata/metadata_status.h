/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __METADATA_STATUS_H__
#define __METADATA_STATUS_H__

#include "../concurrency/ocf_metadata_concurrency.h"

/*******************************************************************************
 * Dirty
 ******************************************************************************/

bool ocf_metadata_test_dirty(struct ocf_cache *cache, ocf_cache_line_t line, uint8_t start, uint8_t stop, bool all);
bool ocf_metadata_test_out_dirty(struct ocf_cache *cache, ocf_cache_line_t line, uint8_t start, uint8_t stop);
bool ocf_metadata_clear_dirty(struct ocf_cache *cache, ocf_cache_line_t line, uint8_t start, uint8_t stop);
bool ocf_metadata_set_dirty(struct ocf_cache *cache, ocf_cache_line_t line, uint8_t start, uint8_t stop);
bool ocf_metadata_test_and_set_dirty(struct ocf_cache *cache, ocf_cache_line_t line, uint8_t start, uint8_t stop, bool all);
bool ocf_metadata_test_and_clear_dirty(struct ocf_cache *cache, ocf_cache_line_t line, uint8_t start, uint8_t stop, bool all);

bool ocf_metadata_test_valid(struct ocf_cache *cache, ocf_cache_line_t line, uint8_t start, uint8_t stop, bool all);
bool ocf_metadata_test_out_valid(struct ocf_cache *cache, ocf_cache_line_t line, uint8_t start, uint8_t stop);
bool ocf_metadata_clear_valid(struct ocf_cache *cache, ocf_cache_line_t line, uint8_t start, uint8_t stop);
bool ocf_metadata_set_valid(struct ocf_cache *cache, ocf_cache_line_t line, uint8_t start, uint8_t stop);
bool ocf_metadata_test_and_set_valid(struct ocf_cache *cache, ocf_cache_line_t line, uint8_t start, uint8_t stop, bool all);
bool ocf_metadata_test_and_clear_valid(struct ocf_cache *cache, ocf_cache_line_t line, uint8_t start, uint8_t stop, bool all);

static inline void metadata_init_status_bits(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	ocf_metadata_clear_dirty(cache, line,
			cache->metadata.settings.sector_start,
			cache->metadata.settings.sector_end);
	ocf_metadata_clear_valid(cache, line,
			cache->metadata.settings.sector_start,
			cache->metadata.settings.sector_end);
}

static inline bool metadata_test_dirty_all(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	bool test;

	test = ocf_metadata_test_dirty(cache, line,
		cache->metadata.settings.sector_start,
		cache->metadata.settings.sector_end, true);

	return test;
}

static inline bool metadata_test_dirty(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	bool test;

	test = ocf_metadata_test_dirty(cache, line,
		cache->metadata.settings.sector_start,
		cache->metadata.settings.sector_end, false);

	return test;
}

static inline void metadata_set_dirty(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	ocf_metadata_set_dirty(cache, line,
			cache->metadata.settings.sector_start,
			cache->metadata.settings.sector_end);
}

static inline void metadata_clear_dirty(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	ocf_metadata_clear_dirty(cache, line,
			cache->metadata.settings.sector_start,
			cache->metadata.settings.sector_end);
}

static inline bool metadata_test_and_clear_dirty(
		struct ocf_cache *cache, ocf_cache_line_t line)
{
	return ocf_metadata_test_and_clear_dirty(cache, line,
			cache->metadata.settings.sector_start,
			cache->metadata.settings.sector_end, false);
}

static inline bool metadata_test_and_set_dirty(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	return ocf_metadata_test_and_set_dirty(cache, line,
			cache->metadata.settings.sector_start,
			cache->metadata.settings.sector_end, false);
}

/*******************************************************************************
 * Dirty - Sector Implementation
 ******************************************************************************/

static inline bool metadata_test_dirty_sec(struct ocf_cache *cache,
		ocf_cache_line_t line, uint8_t start, uint8_t stop)
{
	return ocf_metadata_test_dirty(cache, line,
			start, stop, false);
}

static inline bool metadata_test_dirty_all_sec(struct ocf_cache *cache,
		ocf_cache_line_t line, uint8_t start, uint8_t stop)
{
	return ocf_metadata_test_dirty(cache, line,
			start, stop, true);
}

static inline bool metadata_test_dirty_one(struct ocf_cache *cache,
		ocf_cache_line_t line, uint8_t pos)
{
	return metadata_test_dirty_sec(cache, line, pos, pos);
}

static inline bool metadata_test_dirty_out_sec(struct ocf_cache *cache,
		ocf_cache_line_t line, uint8_t start, uint8_t stop)
{
	return ocf_metadata_test_out_dirty(cache, line, start, stop);
}

static inline void metadata_set_dirty_sec(struct ocf_cache *cache,
		ocf_cache_line_t line, uint8_t start, uint8_t stop)
{
	ocf_metadata_set_dirty(cache, line, start, stop);
}

static inline void metadata_clear_dirty_sec(struct ocf_cache *cache,
		ocf_cache_line_t line, uint8_t start, uint8_t stop)
{
	ocf_metadata_clear_dirty(cache, line, start, stop);
}

static inline void metadata_set_dirty_sec_one(struct ocf_cache *cache,
		ocf_cache_line_t line, uint8_t pos)
{
	ocf_metadata_set_dirty(cache, line, pos, pos);
}

static inline void metadata_clear_dirty_sec_one(struct ocf_cache *cache,
		ocf_cache_line_t line, uint8_t pos)
{
	ocf_metadata_clear_dirty(cache, line, pos, pos);
}

static inline bool metadata_test_and_clear_dirty_sec(
		struct ocf_cache *cache, ocf_cache_line_t line,
		uint8_t start, uint8_t stop)
{
	return ocf_metadata_test_and_clear_dirty(cache, line,
			start, stop, false);
}

/*
 * Marks given cache line's bits as clean
 *
 * @return true if any cache line's sector was dirty and became clean
 * @return false for other cases
 */
static inline bool metadata_clear_dirty_sec_changed(
		struct ocf_cache *cache, ocf_cache_line_t line,
		uint8_t start, uint8_t stop, bool *line_is_clean)
{
	bool sec_changed;

	sec_changed = ocf_metadata_test_dirty(cache, line,
			start, stop, false);
	*line_is_clean = !ocf_metadata_clear_dirty(cache, line,
			start, stop);

	return sec_changed;
}

/*
 * Marks given cache line's bits as dirty
 *
 * @return true if any cache line's sector became dirty
 * @return false for other cases
 */
static inline bool metadata_set_dirty_sec_changed(
		struct ocf_cache *cache, ocf_cache_line_t line,
		uint8_t start, uint8_t stop, bool *line_was_dirty)
{
	bool sec_changed;

	sec_changed = !ocf_metadata_test_dirty(cache, line,
			start, stop, true);
	*line_was_dirty = ocf_metadata_set_dirty(cache, line, start,
			stop);

	return sec_changed;
}

/*******************************************************************************
 * Valid
 ******************************************************************************/

static inline bool metadata_test_valid_any(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	return ocf_metadata_test_valid(cache, line,
		cache->metadata.settings.sector_start,
		cache->metadata.settings.sector_end, false);
}

static inline bool metadata_test_valid(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	return ocf_metadata_test_valid(cache, line,
		cache->metadata.settings.sector_start,
		cache->metadata.settings.sector_end, true);
}

static inline void metadata_set_valid(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	ocf_metadata_set_valid(cache, line,
			cache->metadata.settings.sector_start,
			cache->metadata.settings.sector_end);
}

static inline void metadata_clear_valid(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	ocf_metadata_clear_valid(cache, line,
			cache->metadata.settings.sector_start,
			cache->metadata.settings.sector_end);
}

static inline bool metadata_test_and_clear_valid(
		struct ocf_cache *cache, ocf_cache_line_t line)
{
	return ocf_metadata_test_and_clear_valid(cache, line,
			cache->metadata.settings.sector_start,
			cache->metadata.settings.sector_end, true);
}

static inline bool metadata_test_and_set_valid(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	return ocf_metadata_test_and_set_valid(cache, line,
			cache->metadata.settings.sector_start,
			cache->metadata.settings.sector_end, true);
}

/*******************************************************************************
 * Valid - Sector Implementation
 ******************************************************************************/

static inline bool metadata_test_valid_sec(struct ocf_cache *cache,
		ocf_cache_line_t line, uint8_t start, uint8_t stop)
{
	return ocf_metadata_test_valid(cache, line,
			start, stop, true);
}

static inline bool metadata_test_valid_any_out_sec(
		struct ocf_cache *cache, ocf_cache_line_t line,
		uint8_t start, uint8_t stop)
{
	return ocf_metadata_test_out_valid(cache, line,
			start, stop);
}

static inline bool metadata_test_valid_one(struct ocf_cache *cache,
		ocf_cache_line_t line, uint8_t pos)
{
	return metadata_test_valid_sec(cache, line, pos, pos);
}

/*
 * Marks given cache line's bits as valid
 *
 * @return true if any of the cache line's bits was valid before this operation
 * @return false if the cache line was invalid (all bits invalid) before this
 * operation
 */
static inline bool metadata_set_valid_sec_changed(
		struct ocf_cache *cache, ocf_cache_line_t line,
		uint8_t start, uint8_t stop)
{
	return !ocf_metadata_set_valid(cache, line,
			start, stop);
}

static inline void metadata_clear_valid_sec(struct ocf_cache *cache,
		ocf_cache_line_t line, uint8_t start, uint8_t stop)
{
	ocf_metadata_clear_valid(cache, line, start, stop);
}

static inline void metadata_clear_valid_sec_one(struct ocf_cache *cache,
		ocf_cache_line_t line, uint8_t pos)
{
	ocf_metadata_clear_valid(cache, line, pos, pos);
}

static inline void metadata_set_valid_sec_one(struct ocf_cache *cache,
		ocf_cache_line_t line, uint8_t pos)
{
	ocf_metadata_set_valid(cache, line, pos, pos);
}
/*
 * Marks given cache line's bits as invalid
 *
 * @return true if any of the cache line's bits was valid and the cache line
 * became invalid (all bits invalid) after the operation
 * @return false in other cases
 */
static inline bool metadata_clear_valid_sec_changed(
		struct ocf_cache *cache, ocf_cache_line_t line,
		uint8_t start, uint8_t stop, bool *is_valid)
{
	bool was_any_valid;

	was_any_valid = ocf_metadata_test_valid(cache, line,
			cache->metadata.settings.sector_start,
			cache->metadata.settings.sector_end, false);

	*is_valid = ocf_metadata_clear_valid(cache, line,
			start, stop);

	return was_any_valid && !*is_valid;
}

#endif /* METADATA_STATUS_H_ */
