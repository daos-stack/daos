/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __METADATA_EVICTION_H__
#define __METADATA_EVICTION_H__

struct ocf_lru_meta *
ocf_metadata_get_lru(
		struct ocf_cache *cache, ocf_cache_line_t line);

#endif /* METADATA_EVICTION_H_ */
