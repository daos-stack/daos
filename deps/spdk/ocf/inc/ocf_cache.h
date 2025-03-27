/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */


#ifndef __OCF_CACHE_H__
#define __OCF_CACHE_H__

/**
 * @file
 * @brief OCF cache API
 */

#include "ocf_volume.h"
#include "ocf_ctx.h"
#include "ocf_def.h"
#include "ocf_stats.h"

/**
 * @brief Cache info: configuration, status
 */
struct ocf_cache_info {
	bool attached;
		/*!< True if caching cache is attached to cache */

	uint8_t volume_type;
		/*!< Cache volume type */

	uint8_t state;
		/*!< Cache state (running/flushing/stopping etc...) */

	uint32_t size;
		/*!< Actual cache size (in cache lines) */

	/* Statistics of inactive cores */
	struct {
		struct ocf_stat occupancy;
			/*!< Cache occupancy (in cache lines) */

		struct ocf_stat clean;
			/*!< Clean blocks within cache (in cache lines) */

		struct ocf_stat dirty;
			/*!< Dirty blocks within cache (in cache lines) */
	} inactive;

	uint32_t occupancy;
		/*!< Actual cache occupancy (in cache lines) */

	uint32_t dirty;
		/*!< Dirty blocks within cache (in cache lines) */

	uint64_t dirty_for;
		/*!< How long there are dirty cache lines (in seconds) */

	uint32_t dirty_initial;
		/*!< Dirty blocks within cache that where there when switching
		 * out of WB mode
		 */

	ocf_cache_mode_t cache_mode;
		/*!< Current cache mode */

	/* Statistics of fallback Pass Through */
	struct {
		int error_counter;
			/*!< How many requests to cache failed because of IO error */

		bool status;
			/*!< Current cache mode is PT,
			  set as a result of reaching IO error threshold */
	} fallback_pt;

	ocf_cleaning_t cleaning_policy;
		/*!< Cleaning policy selected */

	ocf_promotion_t promotion_policy;
		/*!< Promotion policy selected */

	ocf_cache_line_size_t cache_line_size;
		/*!< Cache line size in KiB */

	uint32_t flushed;
		/*!< Number of block flushed in ongoing flush operation */

	uint32_t core_count;
		/*!< Number of core devices associated with this cache */

	uint64_t metadata_footprint;
		/*!< Metadata memory footprint (in bytes) */

	uint32_t metadata_end_offset;
		/*!< LBA offset where metadata ends (in 4KiB blocks) */
};

/**
 * @brief Obtain volume from cache
 *
 * @param[in] cache Cache object
 *
 * @retval Volume, NULL if dettached.
 */
ocf_volume_t ocf_cache_get_volume(ocf_cache_t cache);

/**
 * @brief Get name of given cache object
 *
 * @param[in] cache Cache object
 *
 * @retval Cache name
 */
const char *ocf_cache_get_name(ocf_cache_t cache);

/**
 * @brief Check is cache in incomplete state
 *
 * @param[in] cache Cache object
 *
 * @retval 1 Cache is in incomplete state
 * @retval 0 Cache is in complete state
 */
bool ocf_cache_is_incomplete(ocf_cache_t cache);

/**
 * @brief Check if caching device is attached
 *
 * @param[in] cache Cache object
 *
 * @retval 1 Caching device is attached
 * @retval 0 Caching device is detached
 */
bool ocf_cache_is_device_attached(ocf_cache_t cache);

/**
 * @brief Check if cache object is running
 *
 * @param[in] cache Cache object
 *
 * @retval 1 Caching device is being stopped
 * @retval 0 Caching device is being stopped
 */
bool ocf_cache_is_running(ocf_cache_t cache);

/**
 * @brief Get cache mode of given cache object
 *
 * @param[in] cache Cache object
 *
 * @retval Cache mode
 */
ocf_cache_mode_t ocf_cache_get_mode(ocf_cache_t cache);

/**
 * @brief Get cache line size of given cache object
 *
 * @param[in] cache Cache object
 *
 * @retval Cache line size
 */
ocf_cache_line_size_t ocf_cache_get_line_size(ocf_cache_t cache);

/**
 * @brief Convert bytes to cache lines
 *
 * @param[in] cache Cache object
 * @param[in] bytes Number of bytes
 *
 * @retval Cache lines count
 */
uint64_t ocf_cache_bytes_2_lines(ocf_cache_t cache, uint64_t bytes);

/**
 * @brief Get core count of given cache object
 *
 * @param[in] cache Cache object
 *
 * @retval Core count
 */
uint32_t ocf_cache_get_core_count(ocf_cache_t cache);

/**
 * @brief Get cache mode of given cache object
 *
 * @param[in] cache Cache object
 * @param[out] info Cache info structure
 *
 * @retval 0 Success
 * @retval Non-zero Fail
 */
int ocf_cache_get_info(ocf_cache_t cache, struct ocf_cache_info *info);

/**
 * @brief Get UUID of volume associated with cache
 *
 * @param[in] cache Cache object
 *
 * @retval Volume UUID, NULL if detached.
 */
const struct ocf_volume_uuid *ocf_cache_get_uuid(ocf_cache_t cache);

/**
 * @brief Get OCF context of given cache object
 *
 * @param[in] cache Cache object
 *
 * @retval OCF context
 */
ocf_ctx_t ocf_cache_get_ctx(ocf_cache_t cache);

/**
 * @brief Get volume type id of given cache object
 *
 * @param[in] cache Cache object
 *
 * @retval volume type id, -1 if device detached
 */
uint8_t ocf_cache_get_type_id(ocf_cache_t cache);

/**
 * @brief Set cache private data
 *
 * @param[in] cache Cache object
 * @param[in] priv Private data
 */
void ocf_cache_set_priv(ocf_cache_t cache, void *priv);

/**
 * @brief Get cache private data
 *
 * @param[in] cache Cache object
 *
 * @retval Private data
 */
void *ocf_cache_get_priv(ocf_cache_t cache);

#endif /* __OCF_CACHE_H__ */
