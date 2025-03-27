/*
 * copyright(c) 2020 intel corporation
 * spdx-license-identifier: bsd-3-clause-clear
 */

#include "ocf/ocf.h"
#include "metadata.h"
#include "metadata_cleaning_policy.h"
#include "metadata_internal.h"

/*
 * Cleaning policy - Get
 */
struct cleaning_policy_meta *
ocf_metadata_get_cleaning_policy(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	struct ocf_metadata_ctrl *ctrl
		= (struct ocf_metadata_ctrl *) cache->metadata.priv;

	return ocf_metadata_raw_wr_access(cache,
			&(ctrl->raw_desc[metadata_segment_cleaning]), line);
}
