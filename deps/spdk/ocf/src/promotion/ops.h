/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef PROMOTION_OPS_H_
#define PROMOTION_OPS_H_

#include "../metadata/metadata.h"
#include "promotion.h"

struct ocf_promotion_policy {
	ocf_cache_t owner;

	ocf_promotion_t type;

	void *config;
	/* Pointer to config values stored in cache superblock */

	void *ctx;
};

struct promotion_policy_ops {
	const char *name;
		/*!< Promotion policy name */

	void (*setup)(ocf_cache_t cache);
		/*!< initialize promotion policy default config */

	ocf_error_t (*init)(ocf_cache_t cache);
		/*!< Allocate and initialize promotion policy */

	void (*deinit)(ocf_promotion_policy_t policy);
		/*!< Deinit and free promotion policy */

	ocf_error_t (*set_param)(ocf_cache_t cache, uint8_t param_id,
			uint32_t param_value);
		/*!< Set promotion policy parameter */

	ocf_error_t (*get_param)(ocf_cache_t cache, uint8_t param_id,
			uint32_t *param_value);
		/*!< Get promotion policy parameter */

	void (*req_purge)(ocf_promotion_policy_t policy,
			struct ocf_request *req);
		/*!< Call when request core lines have been inserted or it is
		 * a discard request */

	bool (*req_should_promote)(ocf_promotion_policy_t policy,
			struct ocf_request *req);
		/*!< Should request lines be inserted into cache */
};

extern struct promotion_policy_ops ocf_promotion_policies[ocf_promotion_max];

#endif /* PROMOTION_OPS_H_ */

