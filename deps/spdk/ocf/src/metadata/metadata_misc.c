/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "metadata.h"
#include "../utils/utils_cache_line.h"

/* the caller must hold the relevant cache block concurrency reader lock
 * and the metadata lock
 */
void ocf_metadata_remove_cache_line(struct ocf_cache *cache,
		ocf_cache_line_t cache_line)
{
	ocf_part_id_t partition_id =
			ocf_metadata_get_partition_id(cache, cache_line);

	ocf_metadata_remove_from_collision(cache, cache_line, partition_id);
}

void ocf_metadata_sparse_cache_line(struct ocf_cache *cache,
		ocf_cache_line_t cache_line)
{
	ocf_metadata_start_collision_shared_access(cache, cache_line);

	set_cache_line_invalid_no_flush(cache, 0, ocf_line_end_sector(cache),
			cache_line);

	/*
	 * This is especially for removing inactive core
	 */
	metadata_clear_dirty(cache, cache_line);

	ocf_metadata_end_collision_shared_access(cache, cache_line);
}

/* caller must hold metadata lock
 * set core_id to -1 to clean the whole cache device
 */
int ocf_metadata_sparse_range(struct ocf_cache *cache, int core_id,
			  uint64_t start_byte, uint64_t end_byte)
{
	return ocf_metadata_actor(cache, PARTITION_UNSPECIFIED, core_id,
		start_byte, end_byte, ocf_metadata_sparse_cache_line);
}
