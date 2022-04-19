/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(chk)

#include <daos_srv/daos_chk.h>

#include "chk.pb-c.h"
#include "chk_internal.h"

int
chk_start(d_rank_list_t *ranks, struct chk_policy *policies, uuid_t *pools,
	  int pool_cnt, uint32_t flags)
{
	return 0;
}

int
chk_stop(uuid_t *pools, int pool_cnt)
{
	return 0;
}

int
chk_query(uuid_t *pools, int pool_cnt, chk_query_cb_t query_cb,
	  struct chk_query_target *cqt, void *buf)
{
	return 0;
}

int
chk_prop(uint32_t *flags, chk_prop_cb_t prop_cb, struct chk_policy *policy, void *buf)
{
	return 0;
}

int
chk_act(uint64_t seq, uint32_t act, bool for_all)
{
	return 0;
}

int
chk_rejoin(void)
{
	return 0;
}

int
chk_pause(void)
{
	return 0;
}
