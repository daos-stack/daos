/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"

#include "metadata.h"
#include "metadata_collision.h"
#include "metadata_segment_id.h"
#include "metadata_internal.h"
#include "metadata_io.h"
#include "metadata_raw.h"
#include "metadata_segment.h"
#include "../concurrency/ocf_concurrency.h"
#include "../ocf_def_priv.h"
#include "../ocf_priv.h"
#include "../utils/utils_cache_line.h"
#include "../utils/utils_io.h"
#include "../utils/utils_pipeline.h"


#define OCF_METADATA_DEBUG 0

#if 1 == OCF_METADATA_DEBUG
#define OCF_DEBUG_TRACE(cache) \
	ocf_cache_log(cache, log_info, "[Metadata] %s\n", __func__)
#define OCF_DEBUG_PARAM(cache, format, ...) \
	ocf_cache_log(cache, log_info, "[Metadata] %s - "format"\n", \
			__func__, ##__VA_ARGS__)
#else
#define OCF_DEBUG_TRACE(cache)
#define OCF_DEBUG_PARAM(cache, format, ...)
#endif

#define OCF_METADATA_HASH_DIFF_MAX 1000

struct ocf_part_runtime_meta {
	struct ocf_part_runtime runtime;
	struct cleaning_policy clean_pol;
};

enum {
	ocf_metadata_status_type_valid = 0,
	ocf_metadata_status_type_dirty,

	ocf_metadata_status_type_max
};

static inline size_t ocf_metadata_status_sizeof(
		const struct ocf_cache_line_settings *settings) {
	/* Number of bytes required to mark cache line status */
	size_t size = settings->sector_count / 8;

	/* Number of types of status (valid, dirty, etc...) */
	size *= ocf_metadata_status_type_max;

	/* At the end we have size */
	return size;
}

/*
 * get entries for specified metadata hash type
 */
static ocf_cache_line_t ocf_metadata_get_entries(
		enum ocf_metadata_segment_id type,
		ocf_cache_line_t cache_lines)
{
	ENV_BUG_ON(type >= metadata_segment_variable_size_start && cache_lines == 0);

	switch (type) {
	case metadata_segment_collision:
	case metadata_segment_cleaning:
	case metadata_segment_lru:
	case metadata_segment_list_info:
		return cache_lines;

	case metadata_segment_hash:
		return OCF_DIV_ROUND_UP(cache_lines, 4);

	case metadata_segment_sb_config:
		return OCF_DIV_ROUND_UP(sizeof(struct ocf_superblock_config),
				PAGE_SIZE);

	case metadata_segment_sb_runtime:
		return OCF_DIV_ROUND_UP(sizeof(struct ocf_superblock_runtime),
				PAGE_SIZE);

	case metadata_segment_reserved:
		return 32;

	case metadata_segment_part_config:
		return OCF_USER_IO_CLASS_MAX + 1;

	case metadata_segment_part_runtime:
		return OCF_NUM_PARTITIONS;

	case metadata_segment_core_config:
		return OCF_CORE_MAX;

	case metadata_segment_core_runtime:
		return OCF_CORE_MAX;

	case metadata_segment_core_uuid:
		return OCF_CORE_MAX;

	default:
		break;
	}

	ENV_BUG();
	return 0;
}

/*
 * Get size of particular hash metadata type element
 */
static int64_t ocf_metadata_get_element_size(
		enum ocf_metadata_segment_id type,
		const struct ocf_cache_line_settings *settings)
{
	int64_t size = 0;

	ENV_BUG_ON(type >= metadata_segment_variable_size_start && !settings);

	switch (type) {
	case metadata_segment_lru:
		size = sizeof(struct ocf_lru_meta);
		break;

	case metadata_segment_cleaning:
		size = sizeof(struct cleaning_policy_meta);
		break;

	case metadata_segment_collision:
		size = sizeof(struct ocf_metadata_map)
			+ ocf_metadata_status_sizeof(settings);
		break;

	case metadata_segment_list_info:
		size = sizeof(struct ocf_metadata_list_info);
		break;

	case metadata_segment_sb_config:
		size = PAGE_SIZE;
		break;

	case metadata_segment_sb_runtime:
		size = PAGE_SIZE;
		break;

	case metadata_segment_reserved:
		size = PAGE_SIZE;
		break;

	case metadata_segment_part_config:
		size = sizeof(struct ocf_user_part_config);
		break;

	case metadata_segment_part_runtime:
		size = sizeof(struct ocf_part_runtime_meta);
		break;

	case metadata_segment_hash:
		size = sizeof(ocf_cache_line_t);
		break;

	case metadata_segment_core_config:
		size = sizeof(struct ocf_core_meta_config);
		break;

	case metadata_segment_core_runtime:
		size = sizeof(struct ocf_core_meta_runtime);
		break;

	case metadata_segment_core_uuid:
		size = sizeof(struct ocf_metadata_uuid);
		break;

	default:
		break;

	}

	ENV_BUG_ON(size > PAGE_SIZE);

	return size;
}

/*
 * Metadata calculation exception handling.
 *
 * @param unused_lines - Unused pages
 * @param device_lines - SSD Cache device pages amount
 *
 * @return true - Accept unused sapce
 * @return false - unused space is not acceptable
 */
static bool ocf_metadata_calculate_exception_hndl(ocf_cache_t cache,
		int64_t unused_lines, int64_t device_lines)
{
	static bool warn;
	int64_t utilization = 0;

	if (!warn) {
		ocf_cache_log(cache, log_warn,
				"Metadata size calculation problem\n");
		warn = true;
	}

	if (unused_lines < 0)
		return false;

	/*
	 * Accepted disk utilization is 90 % off SSD space
	 */
	utilization = (device_lines - unused_lines) * 100 / device_lines;

	if (utilization < 90)
		return false;

	return true;
}

/*
 * Algorithm to calculate amount of cache lines taking into account required
 * space for metadata
 */
static int ocf_metadata_calculate_metadata_size(
		struct ocf_cache *cache,
		struct ocf_metadata_ctrl *ctrl,
		const struct ocf_cache_line_settings *settings)
{
	int64_t i_diff = 0, diff_lines = 0, cache_lines = ctrl->device_lines;
	int64_t lowest_diff;
	ocf_cache_line_t count_pages;
	uint32_t i;

	OCF_DEBUG_PARAM(cache, "Cache lines = %lld", cache_lines);

	lowest_diff = cache_lines;

	do {
		count_pages = ctrl->count_pages;
		for (i = metadata_segment_variable_size_start;
				i < metadata_segment_max; i++) {
			struct ocf_metadata_raw *raw = &ctrl->raw_desc[i];

			/* Setup number of entries */
			raw->entries
				= ocf_metadata_get_entries(i, cache_lines);

			/*
			 * Setup SSD location and size
			 */
			raw->ssd_pages_offset = count_pages;
			raw->ssd_pages = OCF_DIV_ROUND_UP(raw->entries,
					raw->entries_in_page);

			/* Update offset for next container */
			count_pages += ocf_metadata_raw_size_on_ssd(raw);
		}

		/*
		 * Check if max allowed iteration exceeded
		 */
		if (i_diff >= OCF_METADATA_HASH_DIFF_MAX) {
			/*
			 * Never should be here but try handle this exception
			 */
			if (ocf_metadata_calculate_exception_hndl(cache,
					diff_lines, ctrl->device_lines)) {
				break;
			}

			if (i_diff > (2 * OCF_METADATA_HASH_DIFF_MAX)) {
				/*
				 * We tried, but we fallen, have to return error
				 */
				ocf_cache_log(cache, log_err,
					"Metadata size calculation ERROR\n");
				return -1;
			}
		}

		/* Calculate diff of cache lines */

		/* Cache size in bytes */
		diff_lines = ctrl->device_lines * settings->size;
		/* Sub metadata size which is in 4 kiB unit */
		diff_lines -= count_pages * PAGE_SIZE;
		/* Convert back to cache lines */
		diff_lines /= settings->size;
		/* Calculate difference */
		diff_lines -= cache_lines;

		if (diff_lines > 0) {
			if (diff_lines < lowest_diff)
				lowest_diff = diff_lines;
			else if (diff_lines == lowest_diff)
				break;
		}

		/* Update new value of cache lines */
		cache_lines += diff_lines;

		OCF_DEBUG_PARAM(cache, "Diff pages = %lld", diff_lines);
		OCF_DEBUG_PARAM(cache, "Cache lines = %lld", cache_lines);

		i_diff++;

	} while (diff_lines);

	ctrl->count_pages = count_pages;
	ctrl->cachelines = cache_lines;
	OCF_DEBUG_PARAM(cache, "Cache lines = %u", ctrl->cachelines);

	if (ctrl->device_lines < ctrl->cachelines)
		return -1;

	return 0;
}

const char * const ocf_metadata_segment_names[] = {
		[metadata_segment_sb_config]		= "Super block config",
		[metadata_segment_sb_runtime]		= "Super block runtime",
		[metadata_segment_reserved]		= "Reserved",
		[metadata_segment_part_config]		= "Part config",
		[metadata_segment_part_runtime]		= "Part runtime",
		[metadata_segment_cleaning]		= "Cleaning",
		[metadata_segment_lru]			= "LRU list",
		[metadata_segment_collision]		= "Collision",
		[metadata_segment_list_info]		= "List info",
		[metadata_segment_hash]			= "Hash",
		[metadata_segment_core_config]		= "Core config",
		[metadata_segment_core_runtime]		= "Core runtime",
		[metadata_segment_core_uuid]		= "Core UUID",
};
#if 1 == OCF_METADATA_DEBUG
/*
 * Debug info functions prints metadata and raw containers information
 */
static void ocf_metadata_raw_info(struct ocf_cache *cache,
		struct ocf_metadata_ctrl *ctrl)
{
	uint64_t capacity = 0;
	uint64_t capacity_sum = 0;
	uint32_t i = 0;
	const char *unit;

	for (i = 0; i < metadata_segment_max; i++) {
		struct ocf_metadata_raw *raw = &(ctrl->raw_desc[i]);

		OCF_DEBUG_PARAM(cache, "Raw : name            = %s",
				ocf_metadata_segment_names[i]);
		OCF_DEBUG_PARAM(cache, "    : metadata type   = %u", i);
		OCF_DEBUG_PARAM(cache, "    : raw type        = %u",
				raw->raw_type);
		OCF_DEBUG_PARAM(cache, "    : entry size      = %u",
				raw->entry_size);
		OCF_DEBUG_PARAM(cache, "    : entries         = %llu",
				raw->entries);
		OCF_DEBUG_PARAM(cache, "    : entries in page = %u",
				raw->entries_in_page);
		OCF_DEBUG_PARAM(cache, "    : page offset     = %llu",
				raw->ssd_pages_offset);
		OCF_DEBUG_PARAM(cache, "    : pages           = %llu",
				raw->ssd_pages);
	}

	/* Provide capacity info */
	for (i = 0; i < metadata_segment_max; i++) {
		capacity = ocf_metadata_raw_size_of(cache,
				&(ctrl->raw_desc[i]));

		capacity_sum += capacity;

		if (capacity / MiB) {
			capacity = capacity / MiB;
			unit = "MiB";
		} else {
			unit = "KiB";
			capacity = capacity / KiB;

		}

		OCF_DEBUG_PARAM(cache, "%s capacity %llu %s",
			ocf_metadata_segment_names[i], capacity, unit);
	}
}
#else
#define ocf_metadata_raw_info(cache, ctrl)
#endif

/*
 * Deinitialize hash metadata interface
 */
void ocf_metadata_deinit_variable_size(struct ocf_cache *cache)
{

	uint32_t i = 0;

	struct ocf_metadata_ctrl *ctrl = (struct ocf_metadata_ctrl *)
			cache->metadata.priv;

	OCF_DEBUG_TRACE(cache);

	ocf_metadata_concurrency_attached_deinit(&cache->metadata.lock);

	/*
	 * De initialize RAW types
	 */
	for (i = metadata_segment_variable_size_start;
			i < metadata_segment_max; i++) {
		ocf_metadata_segment_destroy(cache, ctrl->segment[i]);
	}
}

static inline void ocf_metadata_config_init(struct ocf_cache *cache,
		struct ocf_cache_line_settings *settings, size_t size)
{
	ENV_BUG_ON(!ocf_cache_line_size_is_valid(size));

	ENV_BUG_ON(env_memset(settings, sizeof(*settings), 0));

	settings->size = size;
	settings->sector_count = BYTES_TO_SECTORS(settings->size);
	settings->sector_start = 0;
	settings->sector_end = settings->sector_count - 1;

	OCF_DEBUG_PARAM(cache, "Cache line size = %lu, bits count = %llu, "
			"status size = %lu",
			settings->size, settings->sector_count,
			ocf_metadata_status_sizeof(settings));
}

static void ocf_metadata_deinit_fixed_size(struct ocf_cache *cache)
{
	int result = 0;
	uint32_t i;

	struct ocf_metadata_ctrl *ctrl = (struct ocf_metadata_ctrl *)
			cache->metadata.priv;

	struct ocf_metadata_segment *superblock =
		ctrl->segment[metadata_segment_sb_config];

	for (i = 0; i < metadata_segment_fixed_size_max; i++) {
		if (i != metadata_segment_sb_config)
			ocf_metadata_segment_destroy(cache, ctrl->segment[i]);
	}

	ocf_metadata_superblock_destroy(cache, superblock);

	env_vfree(ctrl);
	cache->metadata.priv = NULL;

	if (result)
		ENV_BUG();
}

static struct ocf_metadata_ctrl *ocf_metadata_ctrl_init(
		bool metadata_volatile)
{
	struct ocf_metadata_ctrl *ctrl = NULL;
	uint32_t page = 0;
	uint32_t i = 0;

	ctrl = env_vzalloc(sizeof(*ctrl));
	if (!ctrl)
		return NULL;

	/* Initial setup of RAW containers */
	for (i = 0; i < metadata_segment_fixed_size_max; i++) {
		struct ocf_metadata_raw *raw = &ctrl->raw_desc[i];

		raw->metadata_segment = i;

		/* Default type for metadata RAW container */
		raw->raw_type = metadata_raw_type_ram;

		if (metadata_volatile) {
			raw->raw_type = metadata_raw_type_volatile;
		} else if (i == metadata_segment_core_uuid) {
			raw->raw_type = metadata_raw_type_dynamic;
		}

		/* Entry size configuration */
		raw->entry_size
			= ocf_metadata_get_element_size(i, NULL);
		raw->entries_in_page = PAGE_SIZE / raw->entry_size;

		/* Setup number of entries */
		raw->entries = ocf_metadata_get_entries(i, 0);

		/*
		 * Setup SSD location and size
		 */
		raw->ssd_pages_offset = page;
		raw->ssd_pages = OCF_DIV_ROUND_UP(raw->entries,
				raw->entries_in_page);

		/* Update offset for next container */
		page += ocf_metadata_raw_size_on_ssd(raw);
	}

	ctrl->count_pages = page;

	return ctrl;
}

static int ocf_metadata_init_fixed_size(struct ocf_cache *cache,
		ocf_cache_line_size_t cache_line_size)
{
	struct ocf_metadata_ctrl *ctrl = NULL;
	struct ocf_metadata *metadata = &cache->metadata;
	struct ocf_cache_line_settings *settings =
		(struct ocf_cache_line_settings *)&metadata->settings;
	struct ocf_core_meta_config *core_meta_config;
	struct ocf_core_meta_runtime *core_meta_runtime;
	struct ocf_user_part_config *part_config;
	struct ocf_part_runtime_meta *part_runtime_meta;
	struct ocf_metadata_segment *superblock;
	ocf_core_t core;
	ocf_core_id_t core_id;
	uint32_t i = 0;
	int result = 0;

	OCF_DEBUG_TRACE(cache);

	ENV_WARN_ON(metadata->priv);

	ocf_metadata_config_init(cache, settings, cache_line_size);

	ctrl = ocf_metadata_ctrl_init(metadata->is_volatile);
	if (!ctrl)
		return -OCF_ERR_NO_MEM;
	metadata->priv = ctrl;

	result = ocf_metadata_superblock_init(
			&ctrl->segment[metadata_segment_sb_config], cache,
			&ctrl->raw_desc[metadata_segment_sb_config]);
	if (result) {
		ocf_metadata_deinit_fixed_size(cache);
		return result;
	}

	superblock = ctrl->segment[metadata_segment_sb_config];

	for (i = 0; i < metadata_segment_fixed_size_max; i++) {
		if (i == metadata_segment_sb_config)
			continue;
		result |= ocf_metadata_segment_init(
				&ctrl->segment[i],
				cache,
				&ctrl->raw_desc[i],
				NULL, NULL,
				superblock);
		if (result)
			break;
	}

	if (result) {
		ocf_metadata_deinit_fixed_size(cache);
		return result;
	}

	cache->conf_meta = METADATA_MEM_POOL(ctrl, metadata_segment_sb_config);

	/* Set partition metadata */
	part_config = METADATA_MEM_POOL(ctrl, metadata_segment_part_config);
	part_runtime_meta = METADATA_MEM_POOL(ctrl,
			metadata_segment_part_runtime);

	for (i = 0; i < OCF_USER_IO_CLASS_MAX + 1; i++) {
		cache->user_parts[i].config = &part_config[i];
		cache->user_parts[i].clean_pol = &part_runtime_meta[i].clean_pol;
		cache->user_parts[i].part.runtime =
			&part_runtime_meta[i].runtime;
	}
	cache->free.runtime= &part_runtime_meta[PARTITION_FREELIST].runtime;

	/* Set core metadata */
	core_meta_config = METADATA_MEM_POOL(ctrl,
			metadata_segment_core_config);
	core_meta_runtime = METADATA_MEM_POOL(ctrl,
			metadata_segment_core_runtime);

	for_each_core_all(cache, core, core_id) {
		core->conf_meta = &core_meta_config[core_id];
		core->runtime_meta = &core_meta_runtime[core_id];
	}

	return 0;
}

static void ocf_metadata_flush_lock_collision_page(struct ocf_cache *cache,
		struct ocf_metadata_raw *raw, uint32_t page)

{
	ocf_collision_start_exclusive_access(&cache->metadata.lock,
			page);
}

static void ocf_metadata_flush_unlock_collision_page(
		struct ocf_cache *cache, struct ocf_metadata_raw *raw,
		uint32_t page)

{
	ocf_collision_end_exclusive_access(&cache->metadata.lock,
			page);
}

static void ocf_metadata_init_layout(struct ocf_cache *cache,
		ocf_metadata_layout_t layout)
{
	ENV_BUG_ON(layout >= ocf_metadata_layout_max || layout < 0);

	/* Initialize metadata location interface*/
	if (cache->metadata.is_volatile)
		layout = ocf_metadata_layout_seq;
	cache->metadata.layout = layout;
}

/*
 * Initialize hash metadata interface
 */
int ocf_metadata_init_variable_size(struct ocf_cache *cache,
		uint64_t device_size, ocf_cache_line_size_t cache_line_size,
		ocf_metadata_layout_t layout)
{
	int result = 0;
	uint32_t i = 0;
	struct ocf_metadata_ctrl *ctrl = NULL;
	struct ocf_cache_line_settings *settings =
		(struct ocf_cache_line_settings *)&cache->metadata.settings;
	ocf_flush_page_synch_t lock_page, unlock_page;
	uint64_t device_lines;
	struct ocf_metadata_segment *superblock;

	OCF_DEBUG_TRACE(cache);

	ENV_WARN_ON(!cache->metadata.priv);

	ctrl = cache->metadata.priv;

	device_lines = device_size / cache_line_size;
	if (device_lines >= (ocf_cache_line_t)(-1)){
		/* TODO: This is just a rough check. Most optimal one would be
		 * located in calculate_metadata_size. */
		ocf_cache_log(cache, log_err, "Device exceeds maximum suported size "
				"with this cache line size. Try bigger cache line size.");
		return -OCF_ERR_INVAL_CACHE_DEV;
	}

	ctrl->device_lines = device_lines;

	if (settings->size != cache_line_size)
		/* Re-initialize settings with different cache line size */
		ocf_metadata_config_init(cache, settings, cache_line_size);

	ctrl->mapping_size = ocf_metadata_status_sizeof(settings)
		+ sizeof(struct ocf_metadata_map);

	ocf_metadata_init_layout(cache, layout);

	/* Initial setup of dynamic size RAW containers */
	for (i = metadata_segment_variable_size_start;
			i < metadata_segment_max; i++) {
		struct ocf_metadata_raw *raw = &ctrl->raw_desc[i];

		raw->metadata_segment = i;

		/* Default type for metadata RAW container */
		raw->raw_type = metadata_raw_type_ram;

		if (cache->metadata.is_volatile) {
			raw->raw_type = metadata_raw_type_volatile;
		} else if (i == metadata_segment_collision &&
				ocf_volume_is_atomic(&cache->device->volume)) {
			raw->raw_type = metadata_raw_type_atomic;
		}

		/* Entry size configuration */
		raw->entry_size
			= ocf_metadata_get_element_size(i, settings);
		raw->entries_in_page = PAGE_SIZE / raw->entry_size;
	}

	if (0 != ocf_metadata_calculate_metadata_size(cache, ctrl,
			settings)) {
		return -1;
	}

	OCF_DEBUG_PARAM(cache, "Metadata begin pages = %u", ctrl->start_page);
	OCF_DEBUG_PARAM(cache, "Metadata count pages = %u", ctrl->count_pages);
	OCF_DEBUG_PARAM(cache, "Metadata end pages = %u", ctrl->start_page
			+ ctrl->count_pages);

	superblock = ctrl->segment[metadata_segment_sb_config];

	/*
	 * Initialize all dynamic size  RAW types
	 */
	for (i = metadata_segment_variable_size_start;
			i < metadata_segment_max; i++) {
		if (i == metadata_segment_collision) {
			lock_page =
				ocf_metadata_flush_lock_collision_page;
			unlock_page =
				ocf_metadata_flush_unlock_collision_page;
		} else {
			lock_page = unlock_page = NULL;
		}

		result |= ocf_metadata_segment_init(
				&ctrl->segment[i],
				cache,
				&ctrl->raw_desc[i],
				lock_page, unlock_page,
				superblock);

		if (result)
			goto finalize;
	}

	for (i = 0; i < metadata_segment_max; i++) {
		ocf_cache_log(cache, log_info, "%s offset : %llu kiB\n",
				ocf_metadata_segment_names[i],
				ctrl->raw_desc[i].ssd_pages_offset
				* PAGE_SIZE / KiB);
		if (i == metadata_segment_sb_config) {
			ocf_cache_log(cache, log_info, "%s size : %lu B\n",
				ocf_metadata_segment_names[i],
				offsetof(struct ocf_superblock_config, checksum)
				+ sizeof(((struct ocf_superblock_config *)0)
						->checksum));
		} else if (i == metadata_segment_sb_runtime) {
			ocf_cache_log(cache, log_info, "%s size : %lu B\n",
				ocf_metadata_segment_names[i],
				sizeof(struct ocf_superblock_runtime));
		} else {
			ocf_cache_log(cache, log_info, "%s size : %llu kiB\n",
					ocf_metadata_segment_names[i],
					ctrl->raw_desc[i].ssd_pages
					* PAGE_SIZE / KiB);
		}
	}

finalize:
	if (result) {
		/*
		 * Hash De-Init also contains RAW deinitialization
		 */
		ocf_metadata_deinit_variable_size(cache);
		return result;
	}

	cache->device->runtime_meta = METADATA_MEM_POOL(ctrl,
			metadata_segment_sb_runtime);

	cache->device->collision_table_entries = ctrl->cachelines;

	cache->device->hash_table_entries =
			ctrl->raw_desc[metadata_segment_hash].entries;

	cache->device->metadata_offset = ctrl->count_pages * PAGE_SIZE;

	cache->conf_meta->cachelines = ctrl->cachelines;
	cache->conf_meta->line_size = cache_line_size;

	ocf_metadata_raw_info(cache, ctrl);

	ocf_cache_log(cache, log_info, "Cache line size: %llu kiB\n",
			settings->size / KiB);

	ocf_cache_log(cache, log_info, "Metadata capacity: %llu MiB\n",
			(uint64_t)ocf_metadata_size_of(cache) / MiB);

	result = ocf_metadata_concurrency_attached_init(&cache->metadata.lock,
			cache, ctrl->raw_desc[metadata_segment_hash].entries,
			(uint32_t)ctrl->raw_desc[metadata_segment_collision].
			ssd_pages);
	if (result) {
		ocf_cache_log(cache, log_err, "Failed to initialize attached "
				"metadata concurrency\n");
		ocf_metadata_deinit_variable_size(cache);
		return  result;
	}

	return 0;
}

static inline void _ocf_init_collision_entry(struct ocf_cache *cache,
		ocf_cache_line_t idx)
{
	ocf_cache_line_t invalid_idx = cache->device->collision_table_entries;

	ocf_metadata_set_collision_info(cache, idx, invalid_idx, invalid_idx);
	ocf_metadata_set_core_info(cache, idx,
			OCF_CORE_MAX, ULONG_MAX);
	metadata_init_status_bits(cache, idx);
}

/*
 * Initialize collision table
 */
void ocf_metadata_init_collision(struct ocf_cache *cache)
{
	unsigned int i;
	unsigned int step = 0;

	for (i = 0; i < cache->device->collision_table_entries; i++) {
		_ocf_init_collision_entry(cache, i);
		OCF_COND_RESCHED_DEFAULT(step);
	}
}

/*
 * Initialize hash table
 */
void ocf_metadata_init_hash_table(struct ocf_cache *cache)
{
	unsigned int i;
	unsigned int hash_table_entries = cache->device->hash_table_entries;
	ocf_cache_line_t invalid_idx = cache->device->collision_table_entries;

	/* Init hash table */
	for (i = 0; i < hash_table_entries; i++) {
		/* hash_table contains indexes from collision_table
		 * thus it shall be initialized in improper values
		 * from collision_table
		 **/
		ocf_metadata_set_hash(cache, i, invalid_idx);
	}

}

/*
 * Get count of pages that is dedicated for metadata
 */
ocf_cache_line_t ocf_metadata_get_pages_count(struct ocf_cache *cache)
{
	struct ocf_metadata_ctrl *ctrl = NULL;

	OCF_DEBUG_TRACE(cache);

	ctrl = (struct ocf_metadata_ctrl *) cache->metadata.priv;

	return ctrl->count_pages;
}

/*
 * Get amount of cache lines
 */
ocf_cache_line_t ocf_metadata_get_cachelines_count(
		struct ocf_cache *cache)
{
	struct ocf_metadata_ctrl *ctrl = NULL;

	OCF_DEBUG_TRACE(cache);

	ctrl = (struct ocf_metadata_ctrl *) cache->metadata.priv;

	return ctrl->cachelines;
}

size_t ocf_metadata_size_of(struct ocf_cache *cache)
{
	uint32_t i = 0;
	size_t size = 0;
	struct ocf_metadata_ctrl *ctrl = NULL;

	OCF_DEBUG_TRACE(cache);

	ctrl = (struct ocf_metadata_ctrl *) cache->metadata.priv;

	/*
	 * Get size of all RAW metadata container
	 */
	for (i = 0; i < metadata_segment_max; i++) {
		size += ocf_metadata_raw_size_of(cache,
				&(ctrl->raw_desc[i]));
	}

	/* Get additional part of memory footprint */

	/* Cache concurrency mechnism */
	size += ocf_cache_line_concurrency_size_of(cache);

	return size;
}
/*******************************************************************************
 * RESERVED AREA
 ******************************************************************************/

uint64_t ocf_metadata_get_reserved_lba(
		struct ocf_cache *cache)
{
	struct ocf_metadata_ctrl *ctrl;

	OCF_DEBUG_TRACE(cache);

	ctrl = (struct ocf_metadata_ctrl *) cache->metadata.priv;
	return ctrl->raw_desc[metadata_segment_reserved].ssd_pages_offset *
			PAGE_SIZE;
}

/*******************************************************************************
 * FLUSH AND LOAD ALL
 ******************************************************************************/

static void ocf_metadata_flush_all_set_status_complete(
		void *priv, int error)
{
	struct ocf_metadata_context *context = priv;

	OCF_PL_NEXT_ON_SUCCESS_RET(context->pipeline, error);
}

static void ocf_metadata_flush_all_set_status(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_metadata_context *context = priv;
	ocf_cache_t cache = context->cache;
	enum ocf_metadata_shutdown_status shutdown_status =
			ocf_pipeline_arg_get_int(arg);

	ocf_metadata_set_shutdown_status(cache, shutdown_status,
			ocf_metadata_flush_all_set_status_complete,
			context);
}

static void ocf_metadata_flush_all_finish(ocf_pipeline_t pipeline,
		void *priv, int error)
{
	struct ocf_metadata_context *context = priv;
	ocf_cache_t cache = context->cache;

	if (error) {
		ocf_cache_log(cache, log_err, "Metadata Flush ERROR\n");
		ocf_metadata_error(cache);
		goto out;
	}

	ocf_cache_log(cache, log_info, "Done saving cache state!\n");

out:
	context->cmpl(context->priv, error);
	ocf_pipeline_destroy(pipeline);
}

struct ocf_pipeline_arg ocf_metadata_flush_all_args[] = {
	OCF_PL_ARG_INT(metadata_segment_sb_runtime),
	OCF_PL_ARG_INT(metadata_segment_part_runtime),
	OCF_PL_ARG_INT(metadata_segment_core_runtime),
	OCF_PL_ARG_INT(metadata_segment_cleaning),
	OCF_PL_ARG_INT(metadata_segment_lru),
	OCF_PL_ARG_INT(metadata_segment_collision),
	OCF_PL_ARG_INT(metadata_segment_list_info),
	OCF_PL_ARG_INT(metadata_segment_hash),
	OCF_PL_ARG_TERMINATOR(),
};

struct ocf_pipeline_properties ocf_metadata_flush_all_pipeline_props = {
	.priv_size = sizeof(struct ocf_metadata_context),
	.finish = ocf_metadata_flush_all_finish,
	.steps = {
		OCF_PL_STEP_ARG_INT(ocf_metadata_flush_all_set_status,
				ocf_metadata_dirty_shutdown),
		OCF_PL_STEP_FOREACH(ocf_metadata_flush_segment,
				ocf_metadata_flush_all_args),
		OCF_PL_STEP_FOREACH(ocf_metadata_calculate_crc,
				ocf_metadata_flush_all_args),
		OCF_PL_STEP_ARG_INT(ocf_metadata_flush_all_set_status,
				ocf_metadata_clean_shutdown),
		OCF_PL_STEP_TERMINATOR(),
	},
};

/*
 * Flush all metadata
 */
void ocf_metadata_flush_all(ocf_cache_t cache,
		ocf_metadata_end_t cmpl, void *priv)
{
	struct ocf_metadata_context *context;
	ocf_pipeline_t pipeline;
	int result;

	OCF_DEBUG_TRACE(cache);

	result = ocf_pipeline_create(&pipeline, cache,
			&ocf_metadata_flush_all_pipeline_props);
	if (result)
		OCF_CMPL_RET(priv, result);

	context = ocf_pipeline_get_priv(pipeline);

	context->cmpl = cmpl;
	context->priv = priv;
	context->pipeline = pipeline;
	context->cache = cache;
	context->ctrl = cache->metadata.priv;

	ocf_pipeline_next(pipeline);
}

/*
 * Flush collision metadata
 */
void ocf_metadata_flush_collision(ocf_cache_t cache,
		ocf_metadata_end_t cmpl, void *priv)
{
	struct ocf_metadata_ctrl *ctrl;
	struct ocf_metadata_raw *raw;

	OCF_DEBUG_TRACE(cache);

	ctrl = cache->metadata.priv;
	raw = &ctrl->raw_desc[metadata_segment_collision];

	ocf_metadata_raw_flush_all(cache, raw, cmpl, priv);
}

/*
 * Flush specified cache line
 */
void ocf_metadata_flush_mark(struct ocf_cache *cache,
		struct ocf_request *req, uint32_t map_idx, int to_state,
		uint8_t start, uint8_t stop)
{
	struct ocf_metadata_ctrl *ctrl = NULL;

	OCF_DEBUG_TRACE(cache);

	ctrl = (struct ocf_metadata_ctrl *) cache->metadata.priv;

	/*
	 * Mark all required metadata elements to make given metadata cache
	 * line persistent in case of recovery
	 */

	/* Collision table to get mapping cache line to HDD sector*/
	ocf_metadata_raw_flush_mark(cache,
			&(ctrl->raw_desc[metadata_segment_collision]),
			req, map_idx, to_state, start, stop);
}

/*
 * Flush specified cache lines asynchronously
 */
void ocf_metadata_flush_do_asynch(struct ocf_cache *cache,
		struct ocf_request *req, ocf_req_end_t complete)
{
	int result = 0;
	struct ocf_metadata_ctrl *ctrl = NULL;

	OCF_DEBUG_TRACE(cache);

	ctrl = (struct ocf_metadata_ctrl *) cache->metadata.priv;

	/*
	 * Flush all required metadata elements to make given metadata cache
	 * line persistent in case of recovery
	 */

	env_atomic_inc(&req->req_remaining); /* Core device IO */

	result |= ocf_metadata_raw_flush_do_asynch(cache, req,
			&(ctrl->raw_desc[metadata_segment_collision]),
			complete);

	if (result) {
		ocf_metadata_error(cache);
		ocf_cache_log(cache, log_err, "Metadata Flush ERROR\n");
	}
}

static void ocf_metadata_load_all_finish(ocf_pipeline_t pipeline,
		void *priv, int error)
{
	struct ocf_metadata_context *context = priv;
	ocf_cache_t cache = context->cache;

	if (error) {
		ocf_cache_log(cache, log_err, "Metadata read FAILURE\n");
		ocf_metadata_error(cache);
		goto out;
	}

	ocf_cache_log(cache, log_info, "Done loading cache state\n");

out:
	context->cmpl(context->priv, error);
	ocf_pipeline_destroy(pipeline);
}

struct ocf_pipeline_arg ocf_metadata_load_all_args[] = {
	OCF_PL_ARG_INT(metadata_segment_core_runtime),
	OCF_PL_ARG_INT(metadata_segment_cleaning),
	OCF_PL_ARG_INT(metadata_segment_lru),
	OCF_PL_ARG_INT(metadata_segment_collision),
	OCF_PL_ARG_INT(metadata_segment_list_info),
	OCF_PL_ARG_INT(metadata_segment_hash),
	OCF_PL_ARG_TERMINATOR(),
};

struct ocf_pipeline_properties ocf_metadata_load_all_pipeline_props = {
	.priv_size = sizeof(struct ocf_metadata_context),
	.finish = ocf_metadata_load_all_finish,
	.steps = {
		OCF_PL_STEP_FOREACH(ocf_metadata_load_segment,
				ocf_metadata_load_all_args),
		OCF_PL_STEP_FOREACH(ocf_metadata_check_crc,
				ocf_metadata_load_all_args),
		OCF_PL_STEP_TERMINATOR(),
	},
};

/*
 * Load all metadata
 */
void ocf_metadata_load_all(ocf_cache_t cache,
		ocf_metadata_end_t cmpl, void *priv)
{
	struct ocf_metadata_context *context;
	ocf_pipeline_t pipeline;
	int result;

	OCF_DEBUG_TRACE(cache);

	result = ocf_pipeline_create(&pipeline, cache,
			&ocf_metadata_load_all_pipeline_props);
	if (result)
		OCF_CMPL_RET(priv, result);

	context = ocf_pipeline_get_priv(pipeline);

	context->cmpl = cmpl;
	context->priv = priv;
	context->pipeline = pipeline;
	context->cache = cache;
	context->ctrl = cache->metadata.priv;

	ocf_pipeline_next(pipeline);
}

static void _recovery_rebuild_cline_metadata(ocf_cache_t cache,
		ocf_core_id_t core_id, uint64_t core_line,
		ocf_cache_line_t cache_line)
{
	ocf_core_t core = ocf_cache_get_core(cache, core_id);
	ocf_part_id_t part_id;
	ocf_cache_line_t hash_index;
	struct ocf_part_runtime *part;

	part_id = PARTITION_DEFAULT;
	part = cache->user_parts[part_id].part.runtime;

	ocf_metadata_set_partition_id(cache, cache_line, part_id);
	env_atomic_inc(&part->curr_size);

	hash_index = ocf_metadata_hash_func(cache, core_line, core_id);
	ocf_metadata_add_to_collision(cache, core_id, core_line, hash_index,
			cache_line);

	ocf_lru_init_cline(cache, cache_line);

	ocf_lru_add(cache, cache_line);

	env_atomic_inc(&core->runtime_meta->cached_clines);
	env_atomic_inc(&core->runtime_meta->
			part_counters[part_id].cached_clines);

	if (metadata_test_dirty(cache, cache_line)) {
		env_atomic_inc(&core->runtime_meta->dirty_clines);
		env_atomic_inc(&core->runtime_meta->
				part_counters[part_id].dirty_clines);
		if (!env_atomic64_read(&core->runtime_meta->dirty_since))
			env_atomic64_cmpxchg(&core->runtime_meta->dirty_since, 0,
					env_ticks_to_secs(env_get_tick_count()));
	}
}

static void _recovery_invalidate_clean_sec(struct ocf_cache *cache,
		ocf_cache_line_t cline)
{
	uint8_t i;

	for (i = ocf_line_start_sector(cache);
	     i <= ocf_line_end_sector(cache); i++) {
		if (!metadata_test_dirty_one(cache, cline, i)) {
			/* Invalidate clear sectors */
			metadata_clear_valid_sec_one(cache, cline, i);
		}
	}
}

static void _recovery_reset_cline_metadata(struct ocf_cache *cache,
		ocf_cache_line_t cline)
{

	ocf_metadata_set_core_info(cache, cline, OCF_CORE_MAX, ULLONG_MAX);

	metadata_clear_valid(cache, cline);

	ocf_cleaning_init_cache_block(cache, cline);
}

static void _recovery_rebuild_metadata(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_metadata_context *context = priv;
	bool dirty_only = ocf_pipeline_arg_get_int(arg);
	ocf_cache_t cache = context->cache;
	ocf_cache_line_t cline;
	ocf_core_id_t core_id;
	uint64_t core_line;
	unsigned char step = 0;
	const uint64_t collision_table_entries =
			ocf_metadata_collision_table_entries(cache);

	ocf_metadata_start_exclusive_access(&cache->metadata.lock);

	for (cline = 0; cline < collision_table_entries; cline++) {
		ocf_metadata_get_core_info(cache, cline, &core_id, &core_line);
		if (core_id != OCF_CORE_MAX &&
				(!dirty_only || metadata_test_dirty(cache,
						cline))) {
			/* Rebuild metadata for mapped cache line */
			_recovery_rebuild_cline_metadata(cache, core_id,
					core_line, cline);
			if (dirty_only)
				_recovery_invalidate_clean_sec(cache, cline);
		} else {
			/* Reset metadata for not mapped or clean cache line */
			_recovery_reset_cline_metadata(cache, cline);
		}

		OCF_COND_RESCHED(step, 128);
	}

	ocf_metadata_end_exclusive_access(&cache->metadata.lock);

	ocf_pipeline_next(pipeline);
}

static void ocf_metadata_load_recovery_legacy_finish(
		ocf_pipeline_t pipeline, void *priv, int error)
{
	struct ocf_metadata_context *context = priv;
	ocf_cache_t cache = context->cache;

	if (error) {
		ocf_cache_log(cache, log_err,
				"Metadata read for recovery FAILURE\n");
		ocf_metadata_error(cache);
		goto out;
	}

	ocf_cache_log(cache, log_info, "Done loading cache state\n");

out:
	context->cmpl(context->priv, error);
	ocf_pipeline_destroy(pipeline);
}

struct ocf_pipeline_properties
ocf_metadata_load_recovery_legacy_pl_props = {
	.priv_size = sizeof(struct ocf_metadata_context),
	.finish = ocf_metadata_load_recovery_legacy_finish,
	.steps = {
		OCF_PL_STEP_ARG_INT(ocf_metadata_load_segment,
				metadata_segment_collision),
		OCF_PL_STEP_ARG_INT(_recovery_rebuild_metadata, true),
		OCF_PL_STEP_TERMINATOR(),
	},
};

static void _ocf_metadata_load_recovery_legacy(ocf_cache_t cache,
		ocf_metadata_end_t cmpl, void *priv)
{
	struct ocf_metadata_context *context;
	ocf_pipeline_t pipeline;
	int result;

	OCF_DEBUG_TRACE(cache);

	result = ocf_pipeline_create(&pipeline, cache,
			&ocf_metadata_load_recovery_legacy_pl_props);
	if (result)
		OCF_CMPL_RET(priv, result);

	context = ocf_pipeline_get_priv(pipeline);

	context->cmpl = cmpl;
	context->priv = priv;
	context->pipeline = pipeline;
	context->cache = cache;
	context->ctrl = cache->metadata.priv;

	ocf_pipeline_next(pipeline);
}

static ocf_core_id_t _ocf_metadata_find_core_by_seq(
		struct ocf_cache *cache, ocf_seq_no_t seq_no)
{
	ocf_core_t core;
	ocf_core_id_t core_id;

	if (seq_no == OCF_SEQ_NO_INVALID)
		return OCF_CORE_ID_INVALID;

	for_each_core_all(cache, core, core_id) {
		if (core->conf_meta->seq_no == seq_no)
			break;
	}

	return core_id;
}

static void ocf_metadata_load_atomic_metadata_complete(
		ocf_cache_t cache, void *priv, int error)
{
	struct ocf_metadata_context *context = priv;

	OCF_PL_NEXT_ON_SUCCESS_RET(context->pipeline, error);
}

static int ocf_metadata_load_atomic_metadata_drain(void *priv,
		uint64_t sector_addr, uint32_t sector_no, ctx_data_t *data)
{
	struct ocf_metadata_context *context = priv;
	ocf_cache_t cache = context->cache;
	struct ocf_atomic_metadata meta;
	ocf_cache_line_t line = 0;
	uint8_t pos = 0;
	ocf_seq_no_t core_seq_no = OCF_SEQ_NO_INVALID;
	ocf_core_id_t core_id = OCF_CORE_ID_INVALID;
	uint64_t core_line = 0;
	bool core_line_ok = false;
	uint32_t i;

	for (i = 0; i < sector_no; i++) {
		ctx_data_rd_check(cache->owner, &meta, data, sizeof(meta));

		line = (sector_addr + i) / ocf_line_sectors(cache);
		line = ocf_metadata_map_phy2lg(cache, line);
		pos = (sector_addr + i) % ocf_line_sectors(cache);
		core_seq_no = meta.core_seq_no;
		core_line = meta.core_line;

		/* Look for core with sequence number same as cache line */
		core_id = _ocf_metadata_find_core_by_seq(
				cache, core_seq_no);

		if (pos == 0)
			core_line_ok = false;

		if (meta.valid && core_id != OCF_CORE_ID_INVALID) {
			if (!core_line_ok) {
				ocf_metadata_set_core_info(cache, line,
							core_id, core_line);
				core_line_ok = true;
			}

			metadata_set_valid_sec_one(cache, line, pos);
			meta.dirty ?
				metadata_set_dirty_sec_one(cache, line, pos) :
				metadata_clear_dirty_sec_one(cache, line, pos);
		}
	}

	return 0;
}

static void ocf_metadata_load_atomic_metadata(
		ocf_pipeline_t pipeline, void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_metadata_context *context = priv;
	ocf_cache_t cache = context->cache;
	int result;

	result = metadata_io_read_i_atomic(cache, cache->mngt_queue,
			context, ocf_metadata_load_atomic_metadata_drain,
			ocf_metadata_load_atomic_metadata_complete);
	if (result) {
		ocf_metadata_error(cache);
		ocf_cache_log(cache, log_err,
				"Metadata read for recovery FAILURE\n");
		OCF_PL_FINISH_RET(pipeline, result);
	}
}

static void ocf_metadata_load_recovery_atomic_finish(
		ocf_pipeline_t pipeline, void *priv, int error)
{
	struct ocf_metadata_context *context = priv;
	ocf_cache_t cache = context->cache;

	if (error) {
		ocf_cache_log(cache, log_err,
				"Metadata read for recovery FAILURE\n");
		ocf_metadata_error(cache);
	}

	context->cmpl(context->priv, error);
	ocf_pipeline_destroy(pipeline);
}

struct ocf_pipeline_properties
ocf_metadata_load_recovery_atomic_pl_props = {
	.priv_size = sizeof(struct ocf_metadata_context),
	.finish = ocf_metadata_load_recovery_atomic_finish,
	.steps = {
		OCF_PL_STEP(ocf_metadata_load_atomic_metadata),
		OCF_PL_STEP_ARG_INT(_recovery_rebuild_metadata, false),
		OCF_PL_STEP_TERMINATOR(),
	},
};

/*
 * RAM Implementation - Load all metadata elements from SSD
 */
static void _ocf_metadata_load_recovery_atomic(ocf_cache_t cache,
		ocf_metadata_end_t cmpl, void *priv)
{
	struct ocf_metadata_context *context;
	ocf_pipeline_t pipeline;
	int result;

	OCF_DEBUG_TRACE(cache);

	result = ocf_pipeline_create(&pipeline, cache,
			&ocf_metadata_load_recovery_atomic_pl_props);
	if (result)
		OCF_CMPL_RET(priv, result);

	context = ocf_pipeline_get_priv(pipeline);

	context->cmpl = cmpl;
	context->priv = priv;
	context->pipeline = pipeline;
	context->cache = cache;
	context->ctrl = cache->metadata.priv;

	ocf_pipeline_next(pipeline);
}

/*
 * Load for recovery - Load only data that is required for recovery procedure
 */
void ocf_metadata_load_recovery(ocf_cache_t cache,
		ocf_metadata_end_t cmpl, void *priv)
{
	OCF_DEBUG_TRACE(cache);

	if (ocf_volume_is_atomic(&cache->device->volume))
		_ocf_metadata_load_recovery_atomic(cache, cmpl, priv);
	else
		_ocf_metadata_load_recovery_legacy(cache, cmpl, priv);
}

/*******************************************************************************
 * Core and part id
 ******************************************************************************/

void ocf_metadata_get_core_and_part_id(struct ocf_cache *cache,
		ocf_cache_line_t line, ocf_core_id_t *core_id,
		ocf_part_id_t *part_id)
{
	const struct ocf_metadata_map *collision;
	const struct ocf_metadata_list_info *info;
	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;

	collision = ocf_metadata_raw_rd_access(cache,
			&(ctrl->raw_desc[metadata_segment_collision]), line);

	info =  ocf_metadata_raw_rd_access(cache,
			&(ctrl->raw_desc[metadata_segment_list_info]), line);

	ENV_BUG_ON(!collision || !info);

	if (core_id)
		*core_id = collision->core_id;
	if (part_id)
		*part_id = info->partition_id;
}
/*******************************************************************************
 * Hash Table
 ******************************************************************************/

/*
 * Hash Table - Get
 */
ocf_cache_line_t ocf_metadata_get_hash(struct ocf_cache *cache,
		ocf_cache_line_t index)
{
	struct ocf_metadata_ctrl *ctrl
		= (struct ocf_metadata_ctrl *) cache->metadata.priv;

	return *(ocf_cache_line_t *)ocf_metadata_raw_rd_access(cache,
			&(ctrl->raw_desc[metadata_segment_hash]), index);
}

/*
 * Hash Table - Set
 */
void ocf_metadata_set_hash(struct ocf_cache *cache, ocf_cache_line_t index,
		ocf_cache_line_t line)
{
	struct ocf_metadata_ctrl *ctrl
		= (struct ocf_metadata_ctrl *) cache->metadata.priv;

	*(ocf_cache_line_t *)ocf_metadata_raw_wr_access(cache,
			&(ctrl->raw_desc[metadata_segment_hash]), index) = line;
}

/*******************************************************************************
 *  Bitmap status
 ******************************************************************************/

#include "metadata_bit.h"

#define _ocf_metadata_funcs_5arg(what) \
bool ocf_metadata_##what(struct ocf_cache *cache, \
	 ocf_cache_line_t line, uint8_t start, uint8_t stop, bool all) \
{ \
	switch (cache->metadata.settings.size) { \
		case ocf_cache_line_size_4: \
			return _ocf_metadata_##what##_u8(cache, line, start, stop, all); \
		case ocf_cache_line_size_8: \
			return _ocf_metadata_##what##_u16(cache, line, start, stop, all); \
		case ocf_cache_line_size_16: \
			return _ocf_metadata_##what##_u32(cache, line, start, stop, all); \
		case ocf_cache_line_size_32: \
			return _ocf_metadata_##what##_u64(cache, line, start, stop, all); \
		case ocf_cache_line_size_64: \
			return _ocf_metadata_##what##_u128(cache, line, start, stop, all); \
		case ocf_cache_line_size_none: \
		default: \
			ENV_BUG_ON(1); \
			return false; \
	} \
} \


#define _ocf_metadata_funcs_4arg(what) \
bool ocf_metadata_##what(struct ocf_cache *cache, \
	 ocf_cache_line_t line, uint8_t start, uint8_t stop) \
{ \
	switch (cache->metadata.settings.size) { \
		case ocf_cache_line_size_4: \
			return _ocf_metadata_##what##_u8(cache, line, start, stop); \
		case ocf_cache_line_size_8: \
			return _ocf_metadata_##what##_u16(cache, line, start, stop); \
		case ocf_cache_line_size_16: \
			return _ocf_metadata_##what##_u32(cache, line, start, stop); \
		case ocf_cache_line_size_32: \
			return _ocf_metadata_##what##_u64(cache, line, start, stop); \
		case ocf_cache_line_size_64: \
			return _ocf_metadata_##what##_u128(cache, line, start, stop); \
		case ocf_cache_line_size_none: \
		default: \
			ENV_BUG_ON(1); \
			return false; \
	} \
} \

#define _ocf_metadata_funcs(what) \
	_ocf_metadata_funcs_5arg(test_##what) \
	_ocf_metadata_funcs_4arg(test_out_##what) \
	_ocf_metadata_funcs_4arg(clear_##what) \
	_ocf_metadata_funcs_4arg(set_##what) \
	_ocf_metadata_funcs_5arg(test_and_set_##what) \
	_ocf_metadata_funcs_5arg(test_and_clear_##what)

_ocf_metadata_funcs(dirty)
_ocf_metadata_funcs(valid)

int ocf_metadata_init(struct ocf_cache *cache,
		ocf_cache_line_size_t cache_line_size)
{
	int ret;

	OCF_DEBUG_TRACE(cache);

	ret = ocf_metadata_init_fixed_size(cache, cache_line_size);
	if (ret)
		return ret;

	ret = ocf_metadata_concurrency_init(&cache->metadata.lock);
	if (ret) {
		ocf_metadata_deinit_fixed_size(cache);
		return ret;
	}

	return 0;
}

void ocf_metadata_deinit(struct ocf_cache *cache)
{
	OCF_DEBUG_TRACE(cache);

	ocf_metadata_deinit_fixed_size(cache);
	ocf_metadata_concurrency_deinit(&cache->metadata.lock);
}

void ocf_metadata_error(struct ocf_cache *cache)
{
	if (cache->device->metadata_error == 0)
		ocf_cache_log(cache, log_err, "Metadata Error\n");

	env_bit_clear(ocf_cache_state_running, &cache->cache_state);
	cache->device->metadata_error = -1;
}

struct ocf_metadata_read_sb_ctx;

typedef void (*ocf_metadata_read_sb_end_t)(
		struct ocf_metadata_read_sb_ctx *context);

struct ocf_metadata_read_sb_ctx {
	struct ocf_superblock_config superblock;
	ocf_metadata_read_sb_end_t cmpl;
	ocf_ctx_t ctx;
	void *priv1;
	void *priv2;
	int error;
};

static void ocf_metadata_read_sb_complete(struct ocf_io *io, int error)
{
	struct ocf_metadata_read_sb_ctx *context = io->priv1;
	ctx_data_t *data = ocf_io_get_data(io);

	if (!error) {
		/* Read data from data into super block buffer */
		ctx_data_rd_check(context->ctx, &context->superblock, data,
				sizeof(context->superblock));
	}

	ctx_data_free(context->ctx, data);
	ocf_io_put(io);

	context->error = error;
	context->cmpl(context);

	env_free(context);
}

static int ocf_metadata_read_sb(ocf_ctx_t ctx, ocf_volume_t volume,
		ocf_metadata_read_sb_end_t cmpl, void *priv1, void *priv2)
{
	struct ocf_metadata_read_sb_ctx *context;
	size_t sb_pages = BYTES_TO_PAGES(sizeof(context->superblock));
	ctx_data_t *data;
	struct ocf_io *io;
	int result = 0;

	/* Allocate memory for first page of super block */
	context = env_zalloc(sizeof(*context), ENV_MEM_NORMAL);
	if (!context) {
		ocf_log(ctx, log_err, "Memory allocation error");
		return -OCF_ERR_NO_MEM;
	}

	context->cmpl = cmpl;
	context->ctx = ctx;
	context->priv1 = priv1;
	context->priv2 = priv2;

	/* Allocate resources for IO */
	io = ocf_volume_new_io(volume, NULL, 0, sb_pages * PAGE_SIZE,
			OCF_READ, 0, 0);
	if (!io) {
		ocf_log(ctx, log_err, "Memory allocation error");
		result = -OCF_ERR_NO_MEM;
		goto err_io;
	}

	data = ctx_data_alloc(ctx, sb_pages);
	if (!data) {
		ocf_log(ctx, log_err, "Memory allocation error");
		result = -OCF_ERR_NO_MEM;
		goto err_data;
	}

	/*
	 * Read first page of cache device in order to recover metadata
	 * properties
	 */
	result = ocf_io_set_data(io, data, 0);
	if (result) {
		ocf_log(ctx, log_err, "Metadata IO configuration error\n");
		result = -OCF_ERR_IO;
		goto err_set_data;
	}

	ocf_io_set_cmpl(io, context, NULL, ocf_metadata_read_sb_complete);
	ocf_volume_submit_io(io);

	return 0;

err_set_data:
	ctx_data_free(ctx, data);
err_data:
	ocf_io_put(io);
err_io:
	env_free(context);
	return result;
}

static void ocf_metadata_load_properties_cmpl(
		struct ocf_metadata_read_sb_ctx *context)
{
	struct ocf_metadata_load_properties properties;
	struct ocf_superblock_config *superblock = &context->superblock;
	ocf_metadata_load_properties_end_t cmpl = context->priv1;
	void *priv = context->priv2;
	ocf_ctx_t ctx = context->ctx;

	if (superblock->magic_number != CACHE_MAGIC_NUMBER) {
		ocf_log(ctx, log_info, "Cannot detect pre-existing metadata\n");
		OCF_CMPL_RET(priv, -OCF_ERR_NO_METADATA, NULL);
	}

	if (METADATA_VERSION() != superblock->metadata_version) {
		ocf_log(ctx, log_err, "Metadata version mismatch!\n");
		OCF_CMPL_RET(priv, -OCF_ERR_METADATA_VER, NULL);
	}

	if (!ocf_cache_line_size_is_valid(superblock->line_size)) {
		ocf_log(ctx, log_err, "ERROR: Invalid cache line size!\n");
		OCF_CMPL_RET(priv, -OCF_ERR_INVAL, NULL);
	}

	if ((unsigned)superblock->metadata_layout >= ocf_metadata_layout_max) {
		ocf_log(ctx, log_err, "ERROR: Invalid metadata layout!\n");
		OCF_CMPL_RET(priv, -OCF_ERR_INVAL, NULL);
	}

	if (superblock->cache_mode >= ocf_cache_mode_max) {
		ocf_log(ctx, log_err, "ERROR: Invalid cache mode!\n");
		OCF_CMPL_RET(priv, -OCF_ERR_INVAL, NULL);
	}

	if (superblock->clean_shutdown > ocf_metadata_clean_shutdown) {
		ocf_log(ctx, log_err, "ERROR: Invalid shutdown status!\n");
		OCF_CMPL_RET(priv, -OCF_ERR_INVAL, NULL);
	}

	if (superblock->dirty_flushed > DIRTY_FLUSHED) {
		ocf_log(ctx, log_err, "ERROR: Invalid flush status!\n");
		OCF_CMPL_RET(priv, -OCF_ERR_INVAL, NULL);
	}

	properties.line_size = superblock->line_size;
	properties.layout = superblock->metadata_layout;
	properties.cache_mode = superblock->cache_mode;
	properties.shutdown_status = superblock->clean_shutdown;
	properties.dirty_flushed = superblock->dirty_flushed;
	properties.cache_name = superblock->name;

	OCF_CMPL_RET(priv, 0, &properties);
}

void ocf_metadata_load_properties(ocf_volume_t volume,
		ocf_metadata_load_properties_end_t cmpl, void *priv)
{
	int result;

	OCF_DEBUG_TRACE(cache);

	result = ocf_metadata_read_sb(volume->cache->owner, volume,
			ocf_metadata_load_properties_cmpl, cmpl, priv);
	if (result)
		OCF_CMPL_RET(priv, result, NULL);
}

/* metadata segment data + iterators */
struct query_cores_data
{
	/* array of data */
	ctx_data_t *data;
	/* current metadata entry counter */
	uint32_t entry;
	/* number of entries per page */
	uint32_t entries_in_page;
};

/* query cores context */
struct query_cores_context
{
	ocf_ctx_t ctx;

	struct ocf_superblock_config superblock;
	struct ocf_metadata_uuid muuid;

	struct {
		struct query_cores_data core_uuids;
		struct query_cores_data core_config;
		struct query_cores_data superblock;
	} data;

	env_atomic count;
	env_atomic error;

	/* OCF entry point parameters */
	struct {
		struct ocf_volume_uuid *uuids;
		uint32_t uuids_count;
		void *priv;
		ocf_metadata_query_cores_end_t cmpl;
	} params;
};

/* copy next metadata entry from data to memory buffer */
static void ocf_metadata_query_cores_data_read(ocf_ctx_t ctx,
		struct query_cores_data *data,
		void *buf, uint32_t size)
{
	if (data->entry > 0 && data->entry % data->entries_in_page == 0) {
		ctx_data_seek_check(ctx, data->data,
				ctx_data_seek_current,
				PAGE_SIZE - data->entries_in_page * size);
	}

	ctx_data_rd_check(ctx, buf, data->data, size);

	++data->entry;
}

static void ocf_metadata_query_cores_end(struct query_cores_context *context,
		int error)
{
	ocf_ctx_t ctx = context->ctx;
	unsigned i, core_idx;
	struct ocf_metadata_uuid *muuid = &context->muuid;
	struct ocf_core_meta_config core_config;
	unsigned core_count = 0;
	unsigned long valid_core_bitmap[(OCF_CORE_MAX /
			(sizeof(unsigned long) * 8)) + 1];
	unsigned out_cores;

	if (error)
		env_atomic_cmpxchg(&context->error, 0, error);

	if (env_atomic_dec_return(&context->count))
		return;

	error = env_atomic_read(&context->error);
	if (error)
		goto exit;

	/* read superblock */
	ctx_data_rd_check(ctx, &context->superblock,
			context->data.superblock.data,
			sizeof(context->superblock));

	if (context->superblock.magic_number != CACHE_MAGIC_NUMBER) {
		error = -OCF_ERR_NO_METADATA;
		goto exit;
	}

	env_memset(&valid_core_bitmap, sizeof(valid_core_bitmap), 0);

	/* read valid cores from core config segment */
	for (i = 0; i < OCF_CORE_MAX; i++) {
		ocf_metadata_query_cores_data_read(ctx,
				&context->data.core_config,
				&core_config, sizeof(core_config));
		if (core_config.valid) {
			env_bit_set(i, valid_core_bitmap);
			++core_count;
		}
	}

	/* read core uuids */
	out_cores = OCF_MIN(core_count, context->params.uuids_count);
	for (i = 0, core_idx = 0; i < OCF_CORE_MAX && core_idx < out_cores;
			i++) {
		ocf_metadata_query_cores_data_read(ctx,
				&context->data.core_uuids,
				muuid, sizeof(*muuid));

		if (!env_bit_test(i, valid_core_bitmap))
			continue;

		if (muuid->size > OCF_VOLUME_UUID_MAX_SIZE) {
			error = -OCF_ERR_INVAL;
			goto exit;
		}
		if (muuid->size > context->params.uuids[core_idx].size) {
			error = -OCF_ERR_INVAL;
			goto exit;
		}

		error = env_memcpy(context->params.uuids[core_idx].data,
				context->params.uuids[core_idx].size,
				muuid->data, muuid->size);
		if (error)
			goto exit;
		context->params.uuids[core_idx].size = muuid->size;

		++core_idx;
	}

exit:
	/* provide actual core count to completion */
	context->params.cmpl(context->params.priv, error, core_count);

	/* free data */
	ctx_data_free(ctx, context->data.core_uuids.data);
	ctx_data_free(ctx, context->data.core_config.data);
	ctx_data_free(ctx, context->data.superblock.data);

	env_secure_free(context, sizeof(*context));
}

static void ocf_metadata_query_cores_end_io(struct ocf_io *io, int error)
{
	struct query_cores_context *context = io->priv1;

	ocf_io_put(io);
	ocf_metadata_query_cores_end(context, error);
}

static int ocf_metadata_query_cores_io(ocf_volume_t volume,
		struct query_cores_context *context, ctx_data_t *data,
		uint32_t offset, uint64_t page, uint32_t num_pages)
{
	struct ocf_io *io;
	int err;

	env_atomic_inc(&context->count);

	/* Allocate new IO */
	io = ocf_volume_new_io(volume, NULL,
			PAGES_TO_BYTES(page),
			PAGES_TO_BYTES(num_pages),
			OCF_READ, 0, 0);
	if (!io) {
		err = -OCF_ERR_NO_MEM;
		goto exit_error;
	}

	/* Setup IO */
	ocf_io_set_cmpl(io, context, NULL,
			ocf_metadata_query_cores_end_io);
	err = ocf_io_set_data(io, data, PAGES_TO_BYTES(offset));
	if (err) {
		ocf_io_put(io);
		goto exit_error;
	}

	ocf_volume_submit_io(io);

	return 0;

exit_error:
	env_atomic_dec(&context->count);
	return err;
}

int ocf_metadata_query_cores_segment_io(
		 struct query_cores_context *context,
		ocf_ctx_t owner,
		ocf_volume_t volume,
		enum ocf_metadata_segment_id segment,
		struct ocf_metadata_ctrl *ctrl,
		struct query_cores_data *segment_data)
{
	uint32_t pages_left;
	uint32_t pages;
	uint32_t addr;
	uint32_t offset;
	uint32_t io_count;
	uint32_t i;
	uint32_t max_pages_per_io;
	int err = 0;
	unsigned int max_io_size = ocf_volume_get_max_io_size(volume);

	if (!max_io_size) {
		err = -OCF_ERR_INVAL;
		goto exit;
	}

	max_pages_per_io = max_io_size / PAGE_SIZE;

	/* Allocate data */
	segment_data->data = ctx_data_alloc(owner,
			ctrl->raw_desc[segment].ssd_pages);
	if (!segment_data->data) {
		err = -OCF_ERR_NO_MEM;
		goto exit;
	}

	segment_data->entries_in_page = ctrl->raw_desc[segment].entries_in_page;

	io_count = OCF_DIV_ROUND_UP(ctrl->raw_desc[segment].ssd_pages,
			max_pages_per_io);

	/* submit segment data I/O */
	pages_left = ctrl->raw_desc[segment].ssd_pages;
	addr = ctrl->raw_desc[segment].ssd_pages_offset;
	offset = 0;
	i = 0;
	while (pages_left) {
		ENV_BUG_ON(i >= io_count);

		pages = OCF_MIN(pages_left, max_pages_per_io);

		err = ocf_metadata_query_cores_io(volume, context,
				segment_data->data, offset, addr, pages);
		if (err)
			goto exit;

		addr += pages;
		offset += pages;
		pages_left -= pages;
		++i;
	}

exit:
	return err;
}

void ocf_metadata_query_cores(ocf_ctx_t owner, ocf_volume_t volume,
		struct ocf_volume_uuid *uuid, uint32_t count,
		ocf_metadata_query_cores_end_t cmpl, void *priv)
{
	struct ocf_metadata_ctrl *ctrl = NULL;
	struct query_cores_context *context;
	int err;

	if (count > OCF_CORE_MAX)
		OCF_CMPL_RET(priv, -OCF_ERR_INVAL, 0);

	/* intialize query context */
	context = env_secure_alloc(sizeof(*context));
	if (!context)
		OCF_CMPL_RET(priv, -OCF_ERR_NO_MEM, 0);

	ENV_BUG_ON(env_memset(context, sizeof(*context), 0));
	context->ctx = owner;
	context->params.cmpl = cmpl;
	context->params.priv = priv;
	context->params.uuids = uuid;
	context->params.uuids_count = count;
	env_atomic_set(&context->count, 1);

	ctrl = ocf_metadata_ctrl_init(false);
	if (!ctrl) {
		err = -OCF_ERR_NO_MEM;
		goto exit;
	}

	/* superblock I/O */
	err = ocf_metadata_query_cores_segment_io(context, owner,
			volume, metadata_segment_sb_config, ctrl,
			&context->data.superblock);
	if (err)
		goto exit;

	/* core config I/O */
	err = ocf_metadata_query_cores_segment_io(context, owner,
			volume, metadata_segment_core_uuid, ctrl,
			&context->data.core_uuids);
	if (err)
		goto exit;

	/* core uuid I/O */
	err = ocf_metadata_query_cores_segment_io(context, owner,
			volume, metadata_segment_core_config, ctrl,
			&context->data.core_config);
	if (err)
		goto exit;
exit:
	env_vfree(ctrl);
	ocf_metadata_query_cores_end(context, err);
}


static void ocf_metadata_probe_cmpl(struct ocf_metadata_read_sb_ctx *context)
{
	struct ocf_metadata_probe_status status;
	struct ocf_superblock_config *superblock = &context->superblock;
	ocf_metadata_probe_end_t cmpl = context->priv1;
	void *priv = context->priv2;

	if (superblock->magic_number != CACHE_MAGIC_NUMBER)
		OCF_CMPL_RET(priv, -OCF_ERR_NO_METADATA, NULL);

	if (superblock->clean_shutdown > ocf_metadata_clean_shutdown)
		OCF_CMPL_RET(priv, -OCF_ERR_INVAL, NULL);

	if (superblock->dirty_flushed > DIRTY_FLUSHED)
		OCF_CMPL_RET(priv, -OCF_ERR_INVAL, NULL);

	status.clean_shutdown = (superblock->clean_shutdown !=
			ocf_metadata_dirty_shutdown);
	status.cache_dirty = (superblock->dirty_flushed == DIRTY_NOT_FLUSHED);

	if (METADATA_VERSION() != superblock->metadata_version)
		OCF_CMPL_RET(priv, -OCF_ERR_METADATA_VER, &status);

	env_strncpy(status.cache_name, OCF_CACHE_NAME_SIZE, superblock->name,
			OCF_CACHE_NAME_SIZE);

	OCF_CMPL_RET(priv, 0, &status);
}

void ocf_metadata_probe(ocf_ctx_t ctx, ocf_volume_t volume,
		ocf_metadata_probe_end_t cmpl, void *priv)
{
	int result;

	OCF_CHECK_NULL(ctx);
	OCF_CHECK_NULL(volume);

	result = ocf_metadata_read_sb(ctx, volume, ocf_metadata_probe_cmpl,
			cmpl, priv);
	if (result)
		OCF_CMPL_RET(priv, result, NULL);
}

/* completion context for query_cores */
struct ocf_metadata_query_cores_context
{
	ocf_metadata_probe_cores_end_t cmpl;
	void *priv;
};

static void ocf_metadata_probe_cores_end(void *_context, int error,
		unsigned num_cores)
{
	struct ocf_metadata_query_cores_context *context = _context;

	context->cmpl(context->priv, error, num_cores);
	env_vfree(context);
}

void ocf_metadata_probe_cores(ocf_ctx_t ctx, ocf_volume_t volume,
		struct ocf_volume_uuid *uuids, uint32_t uuids_count,
		ocf_metadata_probe_cores_end_t cmpl, void *priv)
{
	struct ocf_metadata_query_cores_context *context;

	context = env_vzalloc(sizeof(*context));
	if (!context)
		OCF_CMPL_RET(priv, -OCF_ERR_NO_MEM, 0);

	context->cmpl = cmpl;
	context->priv = priv;

	ocf_metadata_query_cores(ctx, volume, uuids, uuids_count,
			ocf_metadata_probe_cores_end, context);
}
