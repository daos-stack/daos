/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#ifndef __CLEANING_AGGRESSIVE_STRUCTS_H__
#define __CLEANING_AGGRESSIVE_STRUCTS_H__

#include "../utils/utils_cleaner.h"

/* TODO: remove acp metadata */
struct acp_cleaning_policy_meta {
	uint8_t dirty : 1;
};

/* cleaning policy per partition metadata */
struct acp_cleaning_policy_config {
	uint32_t thread_wakeup_time;	/* in milliseconds*/
	uint32_t flush_max_buffers;	/* in lines */
};

#endif


