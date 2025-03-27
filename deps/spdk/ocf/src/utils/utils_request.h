/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "../ocf_request.h"
#include "../ocf_cache_priv.h"

typedef int (*ocf_req_actor_t)(struct ocf_request *req, uint32_t map_idx);

int ocf_req_actor(struct ocf_request *req, ocf_req_actor_t actor);

void ocf_req_set_cleaning_hot(struct ocf_request *req);
