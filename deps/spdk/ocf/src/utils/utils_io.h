/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef UTILS_IO_H_
#define UTILS_IO_H_

#include "../ocf_request.h"

/**
 * Checks if 2 IOs are overlapping.
 * @param start1 start of first range (inclusive)
 * @param end1 end of first range (exclusive)
 * @param start2 start of second range (inclusive)
 * @param end2 end of second range (exclusive)
 * @return 0 in case overlap is not detected, otherwise 1
 */
static inline int ocf_io_range_overlaps(uint32_t start1, uint32_t end1,
		uint32_t start2, uint32_t end2)
{
	if (start2 <= start1 && end2 >= start1)
		return 1;

	if (start2 >= start1 && end1 >= start2)
		return 1;

	return 0;
}

/**
 * Checks if 2 IOs are overlapping.
 * @param start1 start of first range (inclusive)
 * @param count1 no of bytes, cachelines (etc) for first range
 * @param start2 start of second range (inclusive)
 * @param count2 no of bytes, cachelines (etc) for second range
 * @return 0 in case overlap is not detected, otherwise 1
 */
static inline int ocf_io_overlaps(uint32_t start1, uint32_t count1,
		uint32_t start2, uint32_t count2)
{
	return ocf_io_range_overlaps(start1, start1 + count1 - 1, start2,
			start2 + count2 - 1);
}

typedef void (*ocf_submit_end_t)(void *priv, int error);

void ocf_submit_volume_flush(ocf_volume_t volume,
		ocf_submit_end_t cmpl, void *priv);

void ocf_submit_volume_discard(ocf_volume_t volume, uint64_t addr,
		uint64_t length, ocf_submit_end_t cmpl, void *priv);

void ocf_submit_write_zeros(ocf_volume_t volume, uint64_t addr,
		uint64_t length, ocf_submit_end_t cmpl, void *priv);

void ocf_submit_cache_page(ocf_cache_t cache, uint64_t addr, int dir,
		void *buffer, ocf_submit_end_t cmpl, void *priv);

void ocf_submit_volume_req(ocf_volume_t volume, struct ocf_request *req,
		ocf_req_end_t callback);

void ocf_submit_cache_reqs(struct ocf_cache *cache,
		struct ocf_request *req, int dir, uint64_t offset,
		uint64_t size, unsigned int reqs, ocf_req_end_t callback);

void ocf_submit_cache_flush(struct ocf_request *req, ocf_req_end_t callback);

static inline struct ocf_io *ocf_new_cache_io(ocf_cache_t cache,
		ocf_queue_t queue, uint64_t addr, uint32_t bytes,
		uint32_t dir, uint32_t io_class, uint64_t flags)

{
	return ocf_volume_new_io(ocf_cache_get_volume(cache), queue,
			addr, bytes, dir, io_class, flags);
}

static inline struct ocf_io *ocf_new_core_io(ocf_core_t core,
		ocf_queue_t queue, uint64_t addr, uint32_t bytes,
		uint32_t dir, uint32_t io_class, uint64_t flags)
{
	return ocf_volume_new_io(ocf_core_get_volume(core), queue,
			addr, bytes, dir, io_class, flags);
}

#endif /* UTILS_IO_H_ */
