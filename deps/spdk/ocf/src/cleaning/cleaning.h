/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __LAYER_CLEANING_POLICY_H__
#define __LAYER_CLEANING_POLICY_H__

#include "alru_structs.h"
#include "nop_structs.h"
#include "acp_structs.h"
#include "ocf/ocf_cleaner.h"
#include "../utils/utils_refcnt.h"

#define CLEANING_POLICY_CONFIG_BYTES 256
#define CLEANING_POLICY_TYPE_MAX 4

#define SLEEP_TIME_MS (1000)

struct ocf_request;

struct cleaning_policy_config {
	uint8_t data[CLEANING_POLICY_CONFIG_BYTES];
};

struct cleaning_policy {
	union {
		struct nop_cleaning_policy nop;
		struct alru_cleaning_policy alru;
	} policy;
};

/* Cleaning policy metadata per cache line */
struct cleaning_policy_meta {
	union {
		struct nop_cleaning_policy_meta nop;
		struct alru_cleaning_policy_meta alru;
		struct acp_cleaning_policy_meta acp;
	} meta;
};

struct ocf_cleaner {
	struct ocf_refcnt refcnt __attribute__((aligned(64)));
	void *cleaning_policy_context;
	ocf_queue_t io_queue;
	ocf_cleaner_end_t end;
	void *priv;
};

int ocf_start_cleaner(ocf_cache_t cache);

void ocf_kick_cleaner(ocf_cache_t cache);

void ocf_stop_cleaner(ocf_cache_t cache);

#endif
