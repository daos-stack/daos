/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_CHK_H__
#define __DAOS_CHK_H__

#include <daos_prop.h>
#include <daos_types.h>

struct chk_policy {
	uint32_t		cp_class;
	uint32_t		cp_action;
};

/* Check query result for the pool (all ranks) or pool shard (per target). */
struct chk_query_pool {
	uuid_t			cqp_uuid;
	char			cqp_label[DAOS_PROP_MAX_LABEL_BUF_LEN];
	uint32_t		cqp_status;
	uint32_t		cqp_phase;
	uint32_t		cqp_total;
	uint32_t		cqp_repaired;
	uint32_t		cqp_ignored;
	uint32_t		cqp_failed;
	uint64_t		cqp_start_time;
	union {
		uint64_t	cqp_remain_time;
		uint64_t	cqp_stop_time;
	};
};

/* Check query result for the target including all the pool shards on this target. */
struct chk_query_target {
	d_rank_t		dqt_rank;
	uint32_t		dqt_tgt;
	uint32_t		dqt_status;
	uint32_t		dqt_cnt;
	struct chk_query_pool	dqt_shards[0];
};

typedef int (*chk_query_cb_t)(void *buf, struct chk_query_target *cqt);

typedef int (*chk_prop_cb_t)(void *buf, struct chk_policy *policies);

int chk_start(d_rank_list_t *ranks, struct chk_policy *policies, uuid_t *pools,
	      int pool_cnt, uint32_t flags);

int chk_stop(uuid_t *pools, int pool_cnt);

int chk_query(uuid_t *pools, int pool_cnt, chk_query_cb_t query_cb,
	      struct chk_query_target *cqt, void *buf);

int chk_prop(uint32_t *flags, chk_prop_cb_t prop_cb, struct chk_policy *policy, void *buf);

int chk_act(uint64_t seq, uint32_t act, bool for_all);

#endif /* __DAOS_CHK_H__ */
