/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef PROMOTION_H_
#define PROMOTION_H_

#include "ocf/ocf.h"
#include "../ocf_request.h"

#define PROMOTION_POLICY_CONFIG_BYTES 256
#define PROMOTION_POLICY_TYPE_MAX 2


struct promotion_policy_config {
	uint8_t data[PROMOTION_POLICY_CONFIG_BYTES];
};

typedef struct ocf_promotion_policy *ocf_promotion_policy_t;

/**
 * @brief Initialize promotion policy default values. Should be called after
 * cache metadata has been allocated and cache->conf_meta->promotion_policy_type
 * has been set.
 *
 * @param[in] cache OCF cache instance
 */
void ocf_promotion_setup(ocf_cache_t cache);

/**
 * @brief Allocate and initialize promotion policy. Should be called after cache
 * metadata has been allocated and cache->conf_meta->promotion_policy_type has
 * been set.
 *
 * @param[in] cache OCF cache instance
 * @param[in] type type of promotion policy to initialize
 *
 * @retval ocf_error_t
 */
ocf_error_t ocf_promotion_init(ocf_cache_t cache, ocf_promotion_t type);

/**
 * @brief Stop, deinitialize and free promotion policy structures.
 *
 * @param[in] policy promotion policy handle
 *
 * @retval none
 */
void ocf_promotion_deinit(ocf_promotion_policy_t policy);

/**
 * @brief Switch promotion policy to type. On failure will fall back to 'always'
 *
 * @param[in] policy promotion policy handle
 * @param[in] type promotion policy target type
 *
 * @retval ocf_error_t
 */
ocf_error_t ocf_promotion_set_policy(ocf_promotion_policy_t policy,
		ocf_promotion_t type);
/**
 * @brief Set promotion policy parameter
 *
 * @param[in] cache cache handle
 * @param[in] type id of promotion policy to be configured
 * @param[in] param_id id of parameter to be set
 * @param[in] param_value value of parameter to be set
 *
 * @retval ocf_error_t
 */
ocf_error_t ocf_promotion_set_param(ocf_cache_t cache, ocf_promotion_t type,
		uint8_t param_id, uint32_t param_value);

/**
 * @brief Get promotion policy parameter
 *
 * @param[in] cache cache handle
 * @param[in] type id of promotion policy to be configured
 * @param[in] param_id id of parameter to be set
 * @param[out] param_value value of parameter to be set
 *
 * @retval ocf_error_t
 */
ocf_error_t ocf_promotion_get_param(ocf_cache_t cache, ocf_promotion_t type,
		uint8_t param_id, uint32_t *param_value);

/**
 * @brief Update promotion policy after cache lines have been promoted to cache
 * or discarded from core device
 *
 * @param[in] policy promotion policy handle
 * @param[in] req OCF request to be purged
 *
 * @retval none
 */
void ocf_promotion_req_purge(ocf_promotion_policy_t policy,
		struct ocf_request *req);

/**
 * @brief Check in promotion policy whether core lines in request can be promoted
 *
 * @param[in] policy promotion policy handle
 * @param[in] req OCF request which is to be promoted
 *
 * @retval should core lines belonging to this request be promoted
 */
bool ocf_promotion_req_should_promote(ocf_promotion_policy_t policy,
		struct ocf_request *req);

#endif /* PROMOTION_H_ */
