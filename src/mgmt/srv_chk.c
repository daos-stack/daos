/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * ds_mgmt: Check Methods
 */
#define D_LOGFAC	DD_FAC(mgmt)

#include <daos_srv/daos_chk.h>
#include <daos_srv/daos_engine.h>

#include "srv_internal.h"

int
ds_mgmt_check_start(uint32_t rank_nr, d_rank_t *ranks, uint32_t policy_nr,
		    Mgmt__CheckInconsistPolicy **policies, int32_t pool_nr, uuid_t pools[],
		    uint32_t flags, int32_t phase)
{
	struct chk_policy *ply = NULL;
	int		   rc = 0;
	int		   i;

	if (policy_nr != 0) {
		D_ALLOC_ARRAY(ply, policy_nr);
		if (ply == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		for (i = 0; i < policy_nr; i++) {
			ply[i].cp_class = policies[i]->inconsist_cas;
			ply[i].cp_action = policies[i]->inconsist_act;
		}
	}

	rc = chk_leader_start(rank_nr, ranks, policy_nr, ply, pool_nr, pools, flags, phase);

out:
	D_FREE(ply);

	return rc;
}

int
ds_mgmt_check_stop(int32_t pool_nr, uuid_t pools[])
{
	return chk_leader_stop(pool_nr, pools);
}

int
ds_mgmt_check_query(int32_t pool_nr, uuid_t pools[], chk_query_head_cb_t head_cb,
		    chk_query_pool_cb_t pool_cb, void *buf)
{
	return chk_leader_query(pool_nr, pools, head_cb, pool_cb, buf);
}

int
ds_mgmt_check_prop(chk_prop_cb_t prop_cb, void *buf)
{
	return chk_leader_prop(prop_cb, buf);
}

int
ds_mgmt_check_act(uint64_t seq, uint32_t act, bool for_all)
{
	return chk_leader_act(seq, act, for_all);
}

bool
ds_mgmt_check_enabled(void)
{
	return engine_in_check();
}
