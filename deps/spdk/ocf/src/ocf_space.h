/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __LAYER_EVICTION_POLICY_H__
#define __LAYER_EVICTION_POLICY_H__

#include "ocf/ocf.h"
#include "ocf_lru.h"
#include "ocf_lru_structs.h"

#define OCF_NUM_LRU_LISTS 32

struct ocf_part;
struct ocf_user_part;
struct ocf_part_runtime;
struct ocf_part_cleaning_ctx;
struct ocf_request;

/*
 * Deallocates space according to eviction priorities.
 *
 * @returns:
 * 'LOOKUP_HIT' if evicted enough cachelines to serve @req
 * 'LOOKUP_MISS' otherwise
 */
int ocf_space_managment_remap_do(struct ocf_request *req);

typedef void (*ocf_metadata_actor_t)(struct ocf_cache *cache,
		ocf_cache_line_t cache_line);

int ocf_metadata_actor(struct ocf_cache *cache,
		ocf_part_id_t part_id, ocf_core_id_t core_id,
		uint64_t start_byte, uint64_t end_byte,
		ocf_metadata_actor_t actor);
#endif
