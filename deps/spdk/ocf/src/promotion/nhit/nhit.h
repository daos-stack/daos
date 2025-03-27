/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef NHIT_PROMOTION_POLICY_H_
#define NHIT_PROMOTION_POLICY_H_

#include "ocf/ocf.h"
#include "../../ocf_request.h"
#include "../promotion.h"
#include "nhit_structs.h"

void nhit_setup(ocf_cache_t cache);

ocf_error_t nhit_init(ocf_cache_t cache);

void nhit_deinit(ocf_promotion_policy_t policy);

ocf_error_t nhit_set_param(ocf_cache_t cache, uint8_t param_id,
		uint32_t param_value);

ocf_error_t nhit_get_param(ocf_cache_t cache, uint8_t param_id,
		uint32_t *param_value);

void nhit_req_purge(ocf_promotion_policy_t policy,
		struct ocf_request *req);

bool nhit_req_should_promote(ocf_promotion_policy_t policy,
		struct ocf_request *req);

#endif /* NHIT_PROMOTION_POLICY_H_ */
