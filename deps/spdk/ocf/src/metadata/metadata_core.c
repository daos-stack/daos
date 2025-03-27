/*
 * Copyright(c) 2020-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "../ocf_priv.h"
#include "metadata.h"
#include "metadata_core.h"
#include "metadata_internal.h"
#include "metadata_raw.h"

void ocf_metadata_get_core_info(struct ocf_cache *cache,
		ocf_cache_line_t line, ocf_core_id_t *core_id,
		uint64_t *core_sector)
{
	const struct ocf_metadata_map *collision;
	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;

	collision = ocf_metadata_raw_rd_access(cache,
			&(ctrl->raw_desc[metadata_segment_collision]), line);

	ENV_BUG_ON(!collision);

	if (core_id)
		*core_id = collision->core_id;
	if (core_sector)
		*core_sector = collision->core_line;
}

void ocf_metadata_set_core_info(struct ocf_cache *cache,
		ocf_cache_line_t line, ocf_core_id_t core_id,
		uint64_t core_sector)
{
	struct ocf_metadata_map *collision;
	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;

	collision = ocf_metadata_raw_wr_access(cache,
			&(ctrl->raw_desc[metadata_segment_collision]), line);

	if (collision) {
		collision->core_id = core_id;
		collision->core_line = core_sector;
	} else {
		ocf_metadata_error(cache);
	}
}

ocf_core_id_t ocf_metadata_get_core_id(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	const struct ocf_metadata_map *collision;
	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;

	collision = ocf_metadata_raw_rd_access(cache,
			&(ctrl->raw_desc[metadata_segment_collision]), line);

	if (collision)
		return collision->core_id;

	ocf_metadata_error(cache);
	return OCF_CORE_MAX;
}

struct ocf_metadata_uuid *ocf_metadata_get_core_uuid(
		struct ocf_cache *cache, ocf_core_id_t core_id)
{
	struct ocf_metadata_uuid *muuid;
	struct ocf_metadata_ctrl *ctrl =
		(struct ocf_metadata_ctrl *) cache->metadata.priv;

	muuid = ocf_metadata_raw_wr_access(cache,
			&(ctrl->raw_desc[metadata_segment_core_uuid]), core_id);

	if (!muuid)
		ocf_metadata_error(cache);

	return muuid;
}
