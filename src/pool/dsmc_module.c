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
 * dsmc: Module Definitions
 *
 * dsmc is the DSM client module/library. It exports the DSM API defined in
 * daos_m.h.
 */

#include <daos_types.h>
#include <pthread.h>
#include <daos/rpc.h>
#include <daos/transport.h>
#include "dsm_rpc.h"
#include "dsmc_internal.h"

struct daos_hhash *dsmc_hhash;

/**
 * Initialize pool interface
 */
int
dc_pool_init(void)
{
	int rc;

	rc = daos_rpc_register(pool_rpcs, NULL, DAOS_POOL_MODULE);
	if (rc != 0)
		return rc;

	rc = daos_hhash_create(DAOS_HHASH_BITS, &dsmc_hhash);
	if (rc != 0)
		daos_rpc_unregister(pool_rpcs);

	return rc;
}

/**
 * Finalize pool interface
 */
void
dc_pool_fini(void)
{
	daos_rpc_unregister(pool_rpcs);

	if (dsmc_hhash != NULL) {
		daos_hhash_destroy(dsmc_hhash);
		dsmc_hhash = NULL;
	}
}
