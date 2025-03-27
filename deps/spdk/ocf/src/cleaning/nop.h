/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __LAYER_CLEANING_POLICY_NOP_H__
#define __LAYER_CLEANING_POLICY_NOP_H__

#include "cleaning.h"
#include "nop_structs.h"

void cleaning_nop_perform_cleaning(ocf_cache_t cache, ocf_cleaner_end_t cmpl);

#endif
