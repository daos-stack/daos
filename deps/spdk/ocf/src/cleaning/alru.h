/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#ifndef __LAYER_CLEANING_POLICY_ALRU_H__

#define __LAYER_CLEANING_POLICY_ALRU_H__

#include "cleaning.h"
#include "alru_structs.h"

void cleaning_policy_alru_setup(ocf_cache_t cache);
int cleaning_policy_alru_initialize(ocf_cache_t cache, int init_metadata);
void cleaning_policy_alru_deinitialize(ocf_cache_t cache);
void cleaning_policy_alru_init_cache_block(ocf_cache_t cache,
		uint32_t cache_line);
void cleaning_policy_alru_purge_cache_block(ocf_cache_t cache,
		uint32_t cache_line);
int cleaning_policy_alru_purge_range(ocf_cache_t cache, int core_id,
		uint64_t start_byte, uint64_t end_byte);
void cleaning_policy_alru_set_hot_cache_line(ocf_cache_t cache,
		uint32_t cache_line);
int cleaning_policy_alru_set_cleaning_param(ocf_cache_t cache,
		uint32_t param_id, uint32_t param_value);
int cleaning_policy_alru_get_cleaning_param(ocf_cache_t cache,
		uint32_t param_id, uint32_t *param_value);
void cleaning_alru_perform_cleaning(ocf_cache_t cache, ocf_cleaner_end_t cmpl);

#endif

