/*
 * Copyright(c) 2020-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __OCF_SEQ_CUTOFF_H__
#define __OCF_SEQ_CUTOFF_H__

#include "ocf/ocf.h"
#include "ocf_request.h"
#include "utils/utils_rbtree.h"

struct ocf_seq_cutoff_stream {
	uint64_t last;
	uint64_t bytes;
	uint32_t rw : 1;
	uint32_t valid : 1;
	uint32_t req_count : 16;
	struct ocf_rb_node node;
	struct list_head list;
};

struct ocf_seq_cutoff {
	ocf_core_t core;
	env_rwlock lock;
	struct ocf_rb_tree tree;
	struct list_head lru;
	struct ocf_seq_cutoff_stream streams[];
};

struct ocf_seq_cutoff_percore {
	struct ocf_seq_cutoff base;
	struct ocf_seq_cutoff_stream streams[OCF_SEQ_CUTOFF_PERCORE_STREAMS];
};

struct ocf_seq_cutoff_perqueue {
	struct ocf_seq_cutoff base;
	struct ocf_seq_cutoff_stream streams[OCF_SEQ_CUTOFF_PERQUEUE_STREAMS];
};

int ocf_core_seq_cutoff_init(ocf_core_t core);

void ocf_core_seq_cutoff_deinit(ocf_core_t core);

int ocf_queue_seq_cutoff_init(ocf_queue_t queue);

void ocf_queue_seq_cutoff_deinit(ocf_queue_t queue);

bool ocf_core_seq_cutoff_check(ocf_core_t core, struct ocf_request *req);

void ocf_core_seq_cutoff_update(ocf_core_t core, struct ocf_request *req);

#endif /* __OCF_SEQ_CUTOFF_H__ */
