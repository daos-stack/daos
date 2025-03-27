/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#ifndef __LAYER_CLEANING_POLICY_AGGRESSIVE_H__

#define __LAYER_CLEANING_POLICY_AGGRESSIVE_H__

#include "cleaning.h"

void cleaning_policy_acp_setup(ocf_cache_t cache);

int cleaning_policy_acp_initialize(ocf_cache_t cache, int init_metadata);

void cleaning_policy_acp_deinitialize(ocf_cache_t cache);

void cleaning_policy_acp_perform_cleaning(ocf_cache_t cache,
		ocf_cleaner_end_t cmpl);

void cleaning_policy_acp_init_cache_block(ocf_cache_t cache,
		uint32_t cache_line);

void cleaning_policy_acp_set_hot_cache_line(ocf_cache_t cache,
		uint32_t cache_line);

void cleaning_policy_acp_purge_block(ocf_cache_t cache, uint32_t cache_line);

int cleaning_policy_acp_purge_range(ocf_cache_t cache,
		int core_id, uint64_t start_byte, uint64_t end_byte);

int cleaning_policy_acp_set_cleaning_param(ocf_cache_t cache,
		uint32_t param_id, uint32_t param_value);

int cleaning_policy_acp_get_cleaning_param(ocf_cache_t cache,
		uint32_t param_id, uint32_t *param_value);

int cleaning_policy_acp_add_core(ocf_cache_t cache, ocf_core_id_t core_id);

void cleaning_policy_acp_remove_core(ocf_cache_t cache,
		ocf_core_id_t core_id);

#endif

