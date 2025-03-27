/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "metadata.h"
#include "metadata_internal.h"
#include "../utils/utils_user_part.h"

ocf_part_id_t ocf_metadata_get_partition_id(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	const struct ocf_metadata_list_info *info;
	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;

	info = ocf_metadata_raw_rd_access(cache,
			&(ctrl->raw_desc[metadata_segment_list_info]), line);

	ENV_BUG_ON(!info);

	return info->partition_id;
}

void ocf_metadata_set_partition_id(struct ocf_cache *cache,
		ocf_cache_line_t line, ocf_part_id_t part_id)
{
	struct ocf_metadata_list_info *info;
	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;

	info = ocf_metadata_raw_wr_access(cache,
			&(ctrl->raw_desc[metadata_segment_list_info]), line);

	if (info)
		info->partition_id = part_id;
	else
		ocf_metadata_error(cache);
}
