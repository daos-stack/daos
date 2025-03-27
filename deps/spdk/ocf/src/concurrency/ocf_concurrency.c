/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf_concurrency.h"
#include "../metadata/metadata.h"

int ocf_concurrency_init(struct ocf_cache *cache)
{
	int result = 0;

	result = ocf_cache_line_concurrency_init(
			&cache->device->concurrency.cache_line,
			ocf_metadata_collision_table_entries(cache),
			cache);

	if (result)
		ocf_concurrency_deinit(cache);

	return result;
}

void ocf_concurrency_deinit(struct ocf_cache *cache)
{
	ocf_cache_line_concurrency_deinit(
			&cache->device->concurrency.cache_line);
}

