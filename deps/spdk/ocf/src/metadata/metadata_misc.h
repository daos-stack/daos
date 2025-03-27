/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __METADATA_MISC_H__
#define __METADATA_MISC_H__

/* Hash function intentionally returns consecutive (modulo @hash_table_entries)
 * values for consecutive @core_line_num. This way it is trivial to sort all
 * core lines within a single request in ascending hash value order. This kind
 * of sorting is required to assure that (future) hash bucket metadata locks are
 * always acquired in fixed order, eliminating the risk of dead locks.
 */
static inline ocf_cache_line_t ocf_metadata_hash_func(ocf_cache_t cache,
		uint64_t core_line_num, ocf_core_id_t core_id)
{
	const unsigned int entries = cache->device->hash_table_entries;

	return (ocf_cache_line_t) ((core_line_num  + (core_id * (entries / 32)))
			% entries);
}

void ocf_metadata_remove_cache_line(struct ocf_cache *cache,
		ocf_cache_line_t cache_line);

void ocf_metadata_sparse_cache_line(struct ocf_cache *cache,
		ocf_cache_line_t cache_line);

int ocf_metadata_sparse_range(struct ocf_cache *cache, int core_id,
			uint64_t start_byte, uint64_t end_byte);

#endif /* __METADATA_MISC_H__ */
