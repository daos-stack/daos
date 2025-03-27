/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __METADATA_H__
#define __METADATA_H__

#include "metadata_common.h"
#include "../ocf_cache_priv.h"
#include "../ocf_ctx_priv.h"
#include "metadata_cleaning_policy.h"
#include "metadata_eviction_policy.h"
#include "metadata_partition.h"
#include "metadata_segment_id.h"
#include "metadata_superblock.h"
#include "metadata_status.h"
#include "metadata_collision.h"
#include "metadata_core.h"
#include "metadata_misc.h"

#define INVALID 0
#define VALID 1
#define CLEAN 2
#define DIRTY 3

/**
 * @brief Initialize metadata
 *
 * @param cache - Cache instance
 * @param cache_line_size Cache line size
 * @return 0 - Operation success otherwise failure
 */
int ocf_metadata_init(struct ocf_cache *cache,
		ocf_cache_line_size_t cache_line_size);

/**
 * @brief Initialize per-cacheline metadata
 *
 * @param cache - Cache instance
 * @param device_size - Device size in bytes
 * @param cache_line_size Cache line size
 * @return 0 - Operation success otherwise failure
 */
int ocf_metadata_init_variable_size(struct ocf_cache *cache,
		uint64_t device_size, ocf_cache_line_size_t cache_line_size,
		ocf_metadata_layout_t layout);

/**
 * @brief Initialize collision table
 *
 * @param cache - Cache instance
 */
void ocf_metadata_init_freelist_partition(struct ocf_cache *cache);

/**
 * @brief Initialize hash table
 *
 * @param cache - Cache instance
 */
void ocf_metadata_init_hash_table(struct ocf_cache *cache);

/**
 * @brief Initialize collision table
 *
 * @param cache - Cache instance
 */
void ocf_metadata_init_collision(struct ocf_cache *cache);

/**
 * @brief De-Initialize metadata
 *
 * @param cache - Cache instance
 */
void ocf_metadata_deinit(struct ocf_cache *cache);

/**
 * @brief De-Initialize per-cacheline metadata
 *
 * @param cache - Cache instance
 */
void ocf_metadata_deinit_variable_size(struct ocf_cache *cache);

/**
 * @brief Get memory footprint
 *
 * @param cache - Cache instance
 * @return 0 - memory footprint
 */
size_t ocf_metadata_size_of(struct ocf_cache *cache);

/**
 * @brief Handle metadata error
 *
 * @param cache - Cache instance
 */
void ocf_metadata_error(struct ocf_cache *cache);

/**
 * @brief Get amount of cache lines
 *
 * @param cache - Cache instance
 * @return Amount of cache lines (cache device lines - metadata space)
 */
ocf_cache_line_t
ocf_metadata_get_cachelines_count(struct ocf_cache *cache);

/**
 * @brief Get amount of pages required for metadata
 *
 * @param cache - Cache instance
 * @return Pages required for store metadata on cache device
 */
ocf_cache_line_t ocf_metadata_get_pages_count(struct ocf_cache *cache);

/**
 * @brief Flush metadata
 *
 * @param cache - Cache instance
 * @param cmpl - Completion callback
 * @param priv - Completion context
 */
void ocf_metadata_flush_all(ocf_cache_t cache,
		ocf_metadata_end_t cmpl, void *priv);

/**
 * @brief Flush metadata collision segment
 *
 * @param cache - Cache instance
 * @param cmpl - Completion callback
 * @param priv - Completion context
 */
void ocf_metadata_flush_collision(ocf_cache_t cache,
		ocf_metadata_end_t cmpl, void *priv);

/**
 * @brief Mark specified cache line to be flushed
 *
 * @param[in] cache - Cache instance
 * @param[in] line - cache line which to be flushed
 */
void ocf_metadata_flush_mark(struct ocf_cache *cache, struct ocf_request *req,
		uint32_t map_idx, int to_state, uint8_t start, uint8_t stop);

/**
 * @brief Flush marked cache lines asynchronously
 *
 * @param cache - Cache instance
 * @param queue - I/O queue to which metadata flush should be submitted
 * @param remaining - request remaining
 * @param complete - flushing request callback
 * @param context - context that will be passed into callback
 */
void ocf_metadata_flush_do_asynch(struct ocf_cache *cache,
		struct ocf_request *req, ocf_req_end_t complete);

/**
 * @brief Load metadata
 *
 * @param cache - Cache instance
 * @param cmpl - Completion callback
 * @param priv - Completion context
 */
void ocf_metadata_load_all(ocf_cache_t cache,
		ocf_metadata_end_t cmpl, void *priv);

/**
 * @brief Load metadata required for recovery procedure
 *
 * @param cache Cache instance
 * @param cmpl - Completion callback
 * @param priv - Completion context
 */
void ocf_metadata_load_recovery(ocf_cache_t cache,
		ocf_metadata_end_t cmpl, void *priv);


/**
 * @brief Get reserved area lba
 *
 * @param cache Cache instance
 */
uint64_t ocf_metadata_get_reserved_lba(ocf_cache_t cache);

/*
 * NOTE Hash table is specific for hash table metadata service implementation
 * and should be used internally by metadata service.
 * At the moment there is no high level metadata interface because of that
 * temporary defined in this file.
 */

ocf_cache_line_t
ocf_metadata_get_hash(struct ocf_cache *cache, ocf_cache_line_t index);

void ocf_metadata_set_hash(struct ocf_cache *cache,
		ocf_cache_line_t index, ocf_cache_line_t line);

struct ocf_metadata_load_properties {
	enum ocf_metadata_shutdown_status shutdown_status;
	uint8_t dirty_flushed;
	ocf_metadata_layout_t layout;
	ocf_cache_mode_t cache_mode;
	ocf_cache_line_size_t line_size;
	char *cache_name;
};

typedef void (*ocf_metadata_load_properties_end_t)(void *priv, int error,
		struct ocf_metadata_load_properties *properties);

void ocf_metadata_load_properties(ocf_volume_t volume,
		ocf_metadata_load_properties_end_t cmpl, void *priv);

static inline ocf_cache_line_t ocf_metadata_collision_table_entries(
		struct ocf_cache *cache)
{
	return cache->device->collision_table_entries;
}

#endif /* METADATA_H_ */
