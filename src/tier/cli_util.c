/**
 * (C) Copyright 2015, 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#define D_LOGFAC	DD_FAC(client)

#include <daos/common.h>
#include <daos/tse.h>
#include <daos/tier.h>
#include <daos/rpc.h>
#include "cli_internal.h"


static daos_tier_info_t *
setup_1_tier(daos_tier_info_t **ppt, const uuid_t uuid, const char *grp)
{
	daos_tier_info_t *pt = *ppt;

	if (pt == NULL) {
		D__ALLOC_PTR(pt);
		*ppt = pt;
	}
	if (pt) {
		char *p;

		pt->ti_leader = 0;

		D__ALLOC(p, strlen(grp) + 1);
		if (p != NULL) {
			strcpy(p, grp);
			pt->ti_group_id = p;
			uuid_copy(pt->ti_pool_id, uuid);
			daos_group_attach(pt->ti_group_id, &pt->ti_group);
			D_DEBUG(DF_TIERS, "group ID:%s\n", pt->ti_group_id);
			D_DEBUG(DF_TIERS, "pool ID:"DF_UUIDF"\n",
				DP_UUID(pt->ti_pool_id));
		} else {
			D__FREE(pt, sizeof(*pt));
			pt = NULL;
			*ppt = pt;
		}
	}
	return pt;
}

static void
tier_teardown_one(daos_tier_info_t **ptier)
{
	daos_tier_info_t *tier = *ptier;

	if (tier != NULL) {
		daos_group_detach(tier->ti_group);
		D__FREE(tier->ti_group_id, strlen(tier->ti_group_id));
		D__FREE_PTR(tier);
		*ptier = NULL;
	}
}

void tier_teardown(void)
{
	tier_teardown_one(&g_tierctx.dtc_colder);
	tier_teardown_one(&g_tierctx.dtc_this);
}

daos_tier_info_t *
tier_setup_cold_tier(const uuid_t uuid, const char *grp)
{
	D_DEBUG(DF_TIERS, "setting up cold tier\n");
	return setup_1_tier(&g_tierctx.dtc_colder, uuid, grp);
}

daos_tier_info_t *
tier_setup_this_tier(const uuid_t uuid, const char *grp)
{
	D_DEBUG(DF_TIERS, "setting up warm tier\n");
	return setup_1_tier(&g_tierctx.dtc_this, uuid, grp);
}

daos_tier_info_t *
tier_lookup(const char *tier_id)
{
	daos_tier_info_t *pt;

	D_DEBUG(DF_TIERS, "%s\n", tier_id);
	pt = g_tierctx.dtc_this;
	if (pt && (!strncmp(pt->ti_group_id, tier_id, strlen(pt->ti_group_id))))
		return pt;
	pt = g_tierctx.dtc_colder;
	if (pt && (!strncmp(pt->ti_group_id, tier_id, strlen(pt->ti_group_id))))
		return pt;
	D_DEBUG(DF_TIERS, "%s NOT FOUND\n", tier_id);
	return NULL;
}

crt_group_t *
tier_crt_group_lookup(const char *tier_id)
{
	daos_tier_info_t *pt = tier_lookup(tier_id);

	if (pt)
		return pt->ti_group;

	return NULL;
}
