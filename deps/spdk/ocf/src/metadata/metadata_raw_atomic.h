/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __METADATA_RAW_ATOMIC_H__
#define __METADATA_RAW_ATOMIC_H__

void raw_atomic_flush_mark(struct ocf_cache *cache, struct ocf_request *req,
		uint32_t map_idx, int to_state, uint8_t start, uint8_t stop);

int raw_atomic_flush_do_asynch(struct ocf_cache *cache, struct ocf_request *req,
		struct ocf_metadata_raw *raw, ocf_req_end_t complete);

#endif /* __METADATA_RAW_ATOMIC_H__ */
