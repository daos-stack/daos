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

#include <daos_m.h>
#include <pthread.h>
#include <daos/rpc.h>
#include <daos/transport.h>
#include "dsm_rpc.h"
#include "dsmc_internal.h"

static pthread_mutex_t	module_lock = PTHREAD_MUTEX_INITIALIZER;
static int		module_initialized;
struct daos_hhash *dsmc_hhash;
/**
 * Initialize dsmc client library.
 */
int
dsm_init(void)
{
	int rc;

	pthread_mutex_lock(&module_lock);
	if (module_initialized)
		D_GOTO(unlock, rc = -DER_ALREADY);

	rc = daos_eq_lib_init();
	if (rc != 0)
		D_GOTO(unlock, rc);

	rc = daos_rpc_register(dsm_rpcs, NULL, DAOS_DSM_MODULE);
	if (rc != 0)
		D_GOTO(out_ev, rc);

	rc = daos_hhash_create(DAOS_HHASH_BITS, &dsmc_hhash);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	module_initialized = 1;
unlock:
	pthread_mutex_unlock(&module_lock);
	return rc;

out_rpc:
	daos_rpc_unregister(dsm_rpcs);
out_ev:
	daos_eq_lib_fini();
	D_GOTO(unlock, rc);
}

/**
 * Finish dsmc client.
 */
int
dsm_fini(void)
{
	int	rc;

	pthread_mutex_lock(&module_lock);
	if (!module_initialized)
		D_GOTO(unlock, rc = -DER_UNINIT);

	daos_rpc_unregister(dsm_rpcs);

	rc = daos_eq_lib_fini();
	if (rc != 0) {
		D_ERROR("failed to finalize eq: %d\n", rc);
		D_GOTO(unlock, rc);
	}

	daos_hhash_destroy(dsmc_hhash);
	module_initialized = 0;
unlock:
	pthread_mutex_unlock(&module_lock);
	return rc;
}
