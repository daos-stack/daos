/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "ocf_priv.h"
#include "metadata/metadata.h"
#include "engine/cache_engine.h"
#include "utils/utils_user_part.h"

int ocf_cache_io_class_get_info(ocf_cache_t cache, uint32_t io_class,
		struct ocf_io_class_info *info)
{
	ocf_part_id_t part_id = io_class;
	struct ocf_part *part;

	OCF_CHECK_NULL(cache);

	if (!info)
		return -OCF_ERR_INVAL;

	if (io_class >= OCF_USER_IO_CLASS_MAX)
		return -OCF_ERR_INVAL;

	if (!ocf_user_part_is_valid(&cache->user_parts[part_id])) {
		/* Partition does not exist */
		return -OCF_ERR_IO_CLASS_NOT_EXIST;
	}

	if (env_strncpy(info->name, OCF_IO_CLASS_NAME_MAX - 1,
			cache->user_parts[part_id].config->name,
			sizeof(cache->user_parts[part_id].config->name))) {
		return -OCF_ERR_INVAL;
	}

	part = &cache->user_parts[part_id].part;

	info->priority = cache->user_parts[part_id].config->priority;
	info->curr_size = ocf_cache_is_device_attached(cache) ?
			env_atomic_read(&part->runtime->curr_size) : 0;
	info->min_size = cache->user_parts[part_id].config->min_size;
	info->max_size = cache->user_parts[part_id].config->max_size;

	info->cleaning_policy_type = cache->conf_meta->cleaning_policy_type;

	info->cache_mode = cache->user_parts[part_id].config->cache_mode;

	return 0;
}

int ocf_io_class_visit(ocf_cache_t cache, ocf_io_class_visitor_t visitor,
		void *cntx)
{
	struct ocf_user_part *user_part;
	ocf_part_id_t part_id;
	int result = 0;

	OCF_CHECK_NULL(cache);

	if (!visitor)
		return -OCF_ERR_INVAL;

	for_each_user_part(cache, user_part, part_id) {
		if (!ocf_user_part_is_valid(user_part))
			continue;

		result = visitor(cache, part_id, cntx);
		if (result)
			break;
	}

	return result;
}
