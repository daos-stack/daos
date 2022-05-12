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

/* Time information on related component: system, pool or target. */
struct chk_time {
	/* The time of check instance being started on the component. */
	uint64_t		ct_start_time;
	union {
		/* The time of the check instance completed, failed or stopped on the component. */
		uint64_t	ct_stop_time;
		/* The estimated remaining time to complete the check on the component. */
		uint64_t	ct_left_time;
	};
};

/* Inconsistency statistics on related component: system, pool or target. */
struct chk_statistics {
	/* The count of total found inconsistency on the component. */
	uint64_t		cs_total;
	/* The count of repaired inconsistency on the component. */
	uint64_t		cs_repaired;
	/* The count of ignored inconsistency on the component. */
	uint64_t		cs_ignored;
	/* The count of fail to repaired inconsistency on the component. */
	uint64_t		cs_failed;
};

struct chk_query_target {
	d_rank_t		cqt_rank;
	uint32_t		cqt_tgt;
	uint32_t		cqt_ins_status;
	uint32_t		cqt_padding;
	struct chk_statistics	cqt_statistics;
	struct chk_time		cqt_time;
};

struct chk_query_pool_shard {
	uuid_t			 cqps_uuid;
	uint32_t		 cqps_status;
	uint32_t		 cqps_phase;
	struct chk_statistics	 cqps_statistics;
	struct chk_time		 cqps_time;
	uint32_t		 cqps_rank;
	uint32_t		 cqps_target_nr;
	struct chk_query_target	*cqps_targets;
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
