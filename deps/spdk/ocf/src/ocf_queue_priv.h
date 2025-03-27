/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef OCF_QUEUE_PRIV_H_
#define OCF_QUEUE_PRIV_H_

#include "ocf_env.h"

struct ocf_queue {
	ocf_cache_t cache;

	void *priv;

	struct list_head io_list;

	/* per-queue free running global metadata lock index */
	unsigned lock_idx;

	/* per-queue free running lru list index */
	unsigned lru_idx;

	struct ocf_seq_cutoff *seq_cutoff;

	struct list_head list;

	const struct ocf_queue_ops *ops;

	/* Tracing reference counter */
	env_atomic64 trace_ref_cntr;

	/* Tracing stop request */
	env_atomic trace_stop;
	env_atomic io_no;

	env_atomic ref_count;
	env_spinlock io_list_lock;
} __attribute__((__aligned__(64)));

static inline void ocf_queue_kick(ocf_queue_t queue, bool allow_sync)
{
	if (allow_sync && queue->ops->kick_sync)
		queue->ops->kick_sync(queue);
	else
		queue->ops->kick(queue);
}

#endif
