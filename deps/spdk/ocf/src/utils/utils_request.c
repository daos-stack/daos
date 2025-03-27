/*
 * Copyright(c) 2012-2020 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef UTILS_REQUEST_H_
#define UTILS_REQUEST_H_

#include "utils_request.h"
#include "utils_cache_line.h"

int ocf_req_actor(struct ocf_request *req, ocf_req_actor_t actor)
{
	uint32_t count = req->core_line_count;
	uint32_t map_idx = 0;
	int result = 0;

	for (map_idx = 0; map_idx < count; map_idx++) {
		result = actor(req, map_idx);
		if (result)
			break;
	}

	return result;
}

static int _set_cleaning_hot_actor(struct ocf_request *req, uint32_t map_idx)
{
	ocf_cache_t cache = req->cache;
	ocf_cache_line_t line = req->map[map_idx].coll_idx;

	ocf_cleaning_set_hot_cache_line(cache, line);

	return 0;
}

void ocf_req_set_cleaning_hot(struct ocf_request *req)
{
	ocf_req_actor(req, _set_cleaning_hot_actor);
}

#endif
