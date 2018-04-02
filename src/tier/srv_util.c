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
/**
 * ds_tier: Tier Server utility functions
 */
#define D_LOGFAC	DD_FAC(tier)

#include <daos_srv/daos_server.h>
#include <daos/rpc.h>
#include "rpc.h"
#include "srv_internal.h"
#include <daos_srv/daos_ct_srv.h>
#include <daos_srv/pool.h>


int
ds_tier_bcast_create(crt_context_t ctx, const uuid_t pool_id,
		     crt_opcode_t opcode, crt_rpc_t **rpc)
{
	int             rc;
	struct ds_pool *pool = ds_pool_lookup(pool_id);

	if (pool == NULL) {
		D_ERROR("pool "DF_UUID" not found\n", DP_UUID(pool_id));
		rc = -DER_INVAL;
	} else
		rc = ds_pool_bcast_create(ctx, pool, DAOS_TIER_MODULE, opcode,
					  rpc, NULL, NULL);

	return rc;
}
