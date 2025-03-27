/*
 * copyright(c )2020 intel corporation
 * spdx-license-identifier: bsd-3-clause-clear
 */

#include "ocf/ocf.h"
#include "metadata.h"
#include "metadata_eviction_policy.h"
#include "metadata_internal.h"

/*
 * Eviction policy - Get
 */
struct ocf_lru_meta * ocf_metadata_get_lru(struct ocf_cache *cache,
		ocf_cache_line_t line)
{
	struct ocf_metadata_ctrl *ctrl
		= (struct ocf_metadata_ctrl *) cache->metadata.priv;

	return ocf_metadata_raw_wr_access(cache,
			&(ctrl->raw_desc[metadata_segment_lru]), line);
}


