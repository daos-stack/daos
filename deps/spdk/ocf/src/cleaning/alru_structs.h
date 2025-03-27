/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#ifndef __CLEANING_ALRU_STRUCTS_H__
#define __CLEANING_ALRU_STRUCTS_H__

#include "ocf/ocf.h"
#include "ocf_env.h"

struct alru_cleaning_policy_meta {
	/* Lru pointers 2*4=8 bytes */
	uint32_t timestamp;
	uint32_t lru_prev;
	uint32_t lru_next;
} __attribute__((packed));

struct alru_cleaning_policy_config {
	uint32_t thread_wakeup_time;	/* in seconds */
	uint32_t stale_buffer_time;	/* in seconds */
	uint32_t flush_max_buffers;	/* in lines */
	uint32_t activity_threshold;	/* in milliseconds */
};

struct alru_cleaning_policy {
	env_atomic size;
	uint32_t lru_head;
	uint32_t lru_tail;
};


#endif
