/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __METADATA_CORE_H__
#define __METADATA_CORE_H__

#include <ocf/ocf_types.h>

void ocf_metadata_get_core_info(struct ocf_cache *cache,
		ocf_cache_line_t line, ocf_core_id_t *core_id,
		uint64_t *core_sector);

void ocf_metadata_set_core_info(struct ocf_cache *cache,
		ocf_cache_line_t line, ocf_core_id_t core_id,
		uint64_t core_sector);

ocf_core_id_t ocf_metadata_get_core_id(
		struct ocf_cache *cache, ocf_cache_line_t line);

struct ocf_metadata_uuid *ocf_metadata_get_core_uuid(
		struct ocf_cache *cache, ocf_core_id_t core_id);

void ocf_metadata_get_core_and_part_id(
		struct ocf_cache *cache, ocf_cache_line_t line,
		ocf_core_id_t *core_id, ocf_part_id_t *part_id);

#endif /* METADATA_CORE_H_ */
