/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

static inline void cleaning_policy_param_error(ocf_cache_t cache,
		const char *param_name, uint32_t min, uint32_t max)
{
	ocf_cache_log(cache, log_err, "Refusing setting flush "
		"parameters because parameter %s is not within range "
		"of <%d-%d>\n", param_name, min, max);
}

#define OCF_CLEANING_CHECK_PARAM(CACHE, VAL, MIN, MAX, NAME) ({ \
	if (VAL < MIN || VAL > MAX) { \
		cleaning_policy_param_error(CACHE, NAME, MIN, MAX); \
		return -OCF_ERR_INVAL; \
	} \
})
