/**
 * (C) Copyright 2016 Intel Corporation.
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
/*
 * cli.c: Module Definitions
 *
 * Daos caching/tiering client module/library. It exports the DCT API defined in
 * daos_api.h.
 */
#define DD_SUBSYS	DD_FAC(tier)

#include <pthread.h>
#include <daos/rpc.h>
#include "rpc.h"
#include <daos/tier.h>
#include "cli_internal.h"

struct dc_tier_context g_tierctx;

/**
 *   Initialize daos client library.
 *
 *   This function will initialize crt interface and create
 *   a crt context for the daos_ct client.
 */
int
dc_tier_init(void)
{
	int rc = 0;

	D_DEBUG(DF_TIER, "Entered dc_tier_init()\n");
	rc = daos_rpc_register(tier_rpcs, NULL, DAOS_TIER_MODULE);
	if (rc != 0)
		D_ERROR("rpc register failure: rc = %d\n", rc);

	return rc;

}

/**
 * Finish daos client.
 */
void
dc_tier_fini(void)
{
	D_DEBUG(DF_TIER, "Entered dc_tier_fini()\n");
	daos_rpc_unregister(tier_rpcs);
}
