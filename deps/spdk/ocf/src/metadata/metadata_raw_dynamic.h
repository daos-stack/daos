/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __METADATA_RAW_DYNAMIC_H__
#define __METADATA_RAW_DYNAMIC_H__

/**
 * @file metadata_raw_dynamic.h
 * @brief Metadata RAW container implementation for dynamic numbers of elements
 */

/*
 * RAW DYNAMIC - Initialize
 */
int raw_dynamic_init(ocf_cache_t cache,
		ocf_flush_page_synch_t lock_page_pfn,
		ocf_flush_page_synch_t unlock_page_pfn,
		struct ocf_metadata_raw *raw);

/*
 * RAW DYNAMIC - De-Initialize
 */
int raw_dynamic_deinit(ocf_cache_t cache,
		struct ocf_metadata_raw *raw);

/*
 * RAW DYNAMIC - Get size of memory footprint of this RAW metadata container
 */
size_t raw_dynamic_size_of(ocf_cache_t cache,
		struct ocf_metadata_raw *raw);

/*
 * RAW DYNAMIC Implementation - Size on SSD
 */
uint32_t raw_dynamic_size_on_ssd(struct ocf_metadata_raw *raw);

/*
 * RAW DYNAMIC Implementation - Checksum
 */
uint32_t raw_dynamic_checksum(ocf_cache_t cache,
		struct ocf_metadata_raw *raw);

/*
 * RAM DYNAMIC Implementation - Entry page number
 */
uint32_t raw_dynamic_page(struct ocf_metadata_raw *raw, uint32_t entry);

/*
 * RAW DYNAMIC - Write access for specified entry
 */
void *raw_dynamic_access(ocf_cache_t cache,
		struct ocf_metadata_raw *raw, uint32_t entry);

/*
 * RAW DYNAMIC - Load all metadata of this RAW metadata container
 * from cache device
 */
void raw_dynamic_load_all(ocf_cache_t cache, struct ocf_metadata_raw *raw,
		ocf_metadata_end_t cmpl, void *priv);

/*
 * RAW DYNAMIC - Flush all metadata of this RAW metadata container
 * to cache device
 */
void raw_dynamic_flush_all(ocf_cache_t cache, struct ocf_metadata_raw *raw,
		ocf_metadata_end_t cmpl, void *priv);

/*
 * RAW DYNAMIC - Mark specified entry to be flushed
 */
void raw_dynamic_flush_mark(ocf_cache_t cache, struct ocf_request *req,
		uint32_t map_idx, int to_state, uint8_t start, uint8_t stop);

/*
 * DYNAMIC Implementation - Do Flush Asynchronously
 */
int raw_dynamic_flush_do_asynch(ocf_cache_t cache, struct ocf_request *req,
		struct ocf_metadata_raw *raw, ocf_req_end_t complete);


#endif /* METADATA_RAW_H_ */
