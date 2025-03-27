/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "ocf_mngt_common.h"
#include "../ocf_priv.h"
#include "../metadata/metadata.h"
#include "../engine/cache_engine.h"
#include "../utils/utils_user_part.h"
#include "../ocf_lru.h"
#include "ocf_env.h"

static uint64_t _ocf_mngt_count_user_parts_min_size(struct ocf_cache *cache)
{
	struct ocf_user_part *user_part;
	ocf_part_id_t part_id;
	uint64_t count = 0;

	for_each_user_part(cache, user_part, part_id) {
		if (ocf_user_part_is_valid(user_part))
			count += user_part->config->min_size;
	}

	return count;
}

int ocf_mngt_add_partition_to_cache(struct ocf_cache *cache,
		ocf_part_id_t part_id, const char *name, uint32_t min_size,
		uint32_t max_size, uint8_t priority, bool valid)
{
	uint32_t size;
	struct ocf_lst_entry *iter;
	uint32_t iter_id;

	if (!name)
		return -OCF_ERR_INVAL;

	if (part_id >= OCF_USER_IO_CLASS_MAX)
		return -OCF_ERR_INVAL;

	if (cache->user_parts[part_id].config->flags.valid)
		return -OCF_ERR_INVAL;

	if (min_size > max_size)
		return -OCF_ERR_INVAL;

	if (max_size > PARTITION_SIZE_MAX)
		return -OCF_ERR_INVAL;

	if (env_strnlen(name, OCF_IO_CLASS_NAME_MAX) >=
			OCF_IO_CLASS_NAME_MAX) {
		ocf_cache_log(cache, log_info,
				"Name of the partition is too long\n");
		return -OCF_ERR_INVAL;
	}

	for_each_lst(&cache->user_part_list, iter, iter_id) {
		if (iter_id == part_id) {
			ocf_cache_log(cache, log_err,
					"Part with id %hu already exists\n", part_id);
			return -OCF_ERR_INVAL;
		}
	}

	size = sizeof(cache->user_parts[part_id].config->name);
	if (env_strncpy(cache->user_parts[part_id].config->name, size, name, size))
		return -OCF_ERR_INVAL;

	cache->user_parts[part_id].config->min_size = min_size;
	cache->user_parts[part_id].config->max_size = max_size;
	cache->user_parts[part_id].config->priority = priority;
	cache->user_parts[part_id].config->cache_mode = ocf_cache_mode_max;

	ocf_user_part_set_valid(cache, part_id, valid);
	ocf_lst_add(&cache->user_part_list, part_id);
	ocf_user_part_sort(cache);

	cache->user_parts[part_id].config->flags.added = 1;

	return 0;
}

static int _ocf_mngt_set_partition_size(struct ocf_cache *cache,
		ocf_part_id_t part_id, uint32_t min, uint32_t max)
{
	struct ocf_user_part *user_part = &cache->user_parts[part_id];

	if (min > max)
		return -OCF_ERR_INVAL;

	if (_ocf_mngt_count_user_parts_min_size(cache) + min >
			PARTITION_SIZE_MAX) {
		/* Illegal configuration in which sum of all min_sizes exceeds
		 * cache size.
		 */
		return -OCF_ERR_INVAL;
	}

	if (max > PARTITION_SIZE_MAX)
		max = PARTITION_SIZE_MAX;

	user_part->config->min_size = min;
	user_part->config->max_size = max;

	return 0;
}

static int _ocf_mngt_io_class_configure(ocf_cache_t cache,
		const struct ocf_mngt_io_class_config *cfg)
{
	int result = -1;
	struct ocf_user_part *dest_part;

	ocf_part_id_t part_id = cfg->class_id;
	const char *name = cfg->name;
	int16_t prio = cfg->prio;
	ocf_cache_mode_t cache_mode = cfg->cache_mode;
	uint32_t min = 0;
	uint32_t max = cfg->max_size;

	OCF_CHECK_NULL(cache->device);

	dest_part = &cache->user_parts[part_id];

	if (!ocf_user_part_is_added(dest_part)) {
		ocf_cache_log(cache, log_info, "Setting IO class, id: %u, "
			"name: '%s' [ ERROR ]\n", part_id, dest_part->config->name);
		return -OCF_ERR_INVAL;
	}

	if (!name[0])
		return -OCF_ERR_INVAL;

	if (part_id == PARTITION_DEFAULT) {
		/* Special behavior for default partition */

		if (env_strncmp(name, OCF_IO_CLASS_NAME_MAX,
				dest_part->config->name, OCF_IO_CLASS_NAME_MAX)) {
			ocf_cache_log(cache, log_err,
				"Can't change classification rule of the default IO class"
				" [ ERROR ]\n");
			return -OCF_ERR_INVAL;
		}

		/* Try set partition size */
		if (_ocf_mngt_set_partition_size(cache, part_id, min, max)) {
			ocf_cache_log(cache, log_err,
				"Setting IO class size, id: %u, name: '%s', max size: %u%%"
				" [ ERROR ]\n", part_id, dest_part->config->name, max);
			return -OCF_ERR_INVAL;
		}
		ocf_user_part_set_prio(cache, dest_part, prio);
		dest_part->config->cache_mode = cache_mode;

		ocf_cache_log(cache, log_info,
				"Updating unclassified IO class, id: %u, name :'%s',"
				"max size: %u%% [ OK ]\n",
				part_id, dest_part->config->name, max);
		return 0;
	}

	/* Setting */
	result = env_strncpy(dest_part->config->name,
			sizeof(dest_part->config->name), name,
			sizeof(dest_part->config->name));
	if (result)
		return result;

	/* Try set partition size */
	if (_ocf_mngt_set_partition_size(cache, part_id, min, max)) {
		ocf_cache_log(cache, log_err,
			"Setting IO class size, id: %u, name: '%s', max size %u%%"
			"[ ERROR ]\n", part_id, dest_part->config->name, max);
		return -OCF_ERR_INVAL;
	}

	if (ocf_user_part_is_valid(dest_part)) {
		/* Updating existing */
		ocf_cache_log(cache, log_info, "Updating existing IO "
				"class, id: %u, name: '%s', max size %u%% [ OK ]\n",
				part_id, dest_part->config->name, max);
	} else {
		/* Adding new */
		ocf_user_part_set_valid(cache, part_id, true);

		ocf_cache_log(cache, log_info, "Adding new IO class, "
				"id: %u, name: '%s', max size %u%% [ OK ]\n", part_id,
				dest_part->config->name, max);
	}

	ocf_user_part_set_prio(cache, dest_part, prio);
	dest_part->config->cache_mode = cache_mode;

	return result;
}

static void _ocf_mngt_io_class_remove(ocf_cache_t cache,
		const struct ocf_mngt_io_class_config *cfg)
{
	struct ocf_user_part *dest_part;
	ocf_part_id_t part_id = cfg->class_id;

	dest_part = &cache->user_parts[part_id];

	OCF_CHECK_NULL(cache->device);

	if (part_id == PARTITION_DEFAULT) {
		ocf_cache_log(cache, log_info,
				"Cannot remove unclassified IO class, "
				"id: %u [ ERROR ]\n", part_id);
		return;
	}

	if (!ocf_user_part_is_valid(dest_part)) {
		/* Does not exist */
		return;
	}


	ocf_user_part_set_valid(cache, part_id, false);

	ocf_cache_log(cache, log_info,
			"Removing IO class, id: %u [ OK ]\n", part_id);
}

static int _ocf_mngt_io_class_edit(ocf_cache_t cache,
		const struct ocf_mngt_io_class_config *cfg)
{
	int result = 0;

	if (cfg->name)
		result = _ocf_mngt_io_class_configure(cache, cfg);
	else
		_ocf_mngt_io_class_remove(cache, cfg);

	return result;
}

static int _ocf_mngt_io_class_validate_cfg(ocf_cache_t cache,
		const struct ocf_mngt_io_class_config *cfg)
{
	if (cfg->class_id >= OCF_USER_IO_CLASS_MAX)
		return -OCF_ERR_INVAL;

	/* Name set to null means particular io_class should be removed */
	if (!cfg->name)
		return 0;

	if (cfg->cache_mode < ocf_cache_mode_none ||
			cfg->cache_mode > ocf_cache_mode_max) {
		return -OCF_ERR_INVAL;
	}

	if (!ocf_user_part_is_name_valid(cfg->name)) {
		ocf_cache_log(cache, log_info,
			"The name of the partition is not valid\n");
		return -OCF_ERR_INVAL;
	}

	if (!ocf_user_part_is_prio_valid(cfg->prio)) {
		ocf_cache_log(cache, log_info,
			"Invalid value of the partition priority\n");
		return -OCF_ERR_INVAL;
	}

	if (cfg->max_size < PARTITION_SIZE_MIN ||
			cfg->max_size > PARTITION_SIZE_MAX) {
		ocf_cache_log(cache, log_info,
				"Invalid value of the partition max size\n");
		return -OCF_ERR_INVAL;
	}

	return 0;
}

int ocf_mngt_cache_io_classes_configure(ocf_cache_t cache,
		const struct ocf_mngt_io_classes_config *cfg)
{
	struct ocf_user_part *old_config;
	int result;
	int i;

	OCF_CHECK_NULL(cache);
	OCF_CHECK_NULL(cfg);

	for (i = 0; i < OCF_USER_IO_CLASS_MAX; i++) {
		result = _ocf_mngt_io_class_validate_cfg(cache, &cfg->config[i]);
		if (result)
			return result;
	}

	old_config = env_malloc(sizeof(cache->user_parts), ENV_MEM_NORMAL);
	if (!old_config)
		return -OCF_ERR_NO_MEM;

	ocf_metadata_start_exclusive_access(&cache->metadata.lock);

	result = env_memcpy(old_config, sizeof(cache->user_parts),
			cache->user_parts, sizeof(cache->user_parts));
	if (result)
		goto out_cpy;

	for (i = 0; i < OCF_USER_IO_CLASS_MAX; i++) {
		result = _ocf_mngt_io_class_edit(cache, &cfg->config[i]);
		if (result) {
			ocf_cache_log(cache, log_err,
					"Failed to set new io class config\n");
			goto out_edit;
		}
	}

	ocf_user_part_sort(cache);

out_edit:
	if (result) {
		ENV_BUG_ON(env_memcpy(cache->user_parts, sizeof(cache->user_parts),
					old_config, sizeof(cache->user_parts)));
	}

out_cpy:
	ocf_metadata_end_exclusive_access(&cache->metadata.lock);
	env_free(old_config);

	return result;
}
