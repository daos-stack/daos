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
 * dctc: Module Definitions
 *
 * dctc is the DCT client module/library. It exports the DCT API defined in
 * daos_m.h.
 */

#include <daos_ct.h>
#include <pthread.h>
#include <daos/rpc.h>
#include <daos/transport.h>
#include "dct_rpc.h"

#define DTP_DESTROY_FORCE	1 /* belongs in transport.h */

static pthread_mutex_t	module_lock = PTHREAD_MUTEX_INITIALIZER;
static int		module_initialized;


/**
 *   Initialize dsmc client library.
 *
 *   This function will initialize dtp interface and create
 *   a dtp context for the daos_ct client.
 */
int
dct_init(void)
{
	D_DEBUG(DF_MISC, "Entered dct_init()\n");

	int rc;

	pthread_mutex_lock(&module_lock);
	if (module_initialized)
		D_GOTO(unlock, rc = -DER_ALREADY);

	rc = daos_eq_lib_init();
	if (rc != 0)
		D_GOTO(unlock, rc);

	rc = daos_rpc_register(dct_rpcs, NULL, DAOS_TIER_MODULE);
	if (rc != 0) {
		D_ERROR("rpc register failure: rc = %d\n", rc);
		daos_eq_lib_fini();
		D_GOTO(unlock, rc);
	}

	module_initialized = 1;
unlock:
	pthread_mutex_unlock(&module_lock);
	D_DEBUG(DF_MISC, "Returning from dct_init()\n");
	return rc;

}

/**
 * Finish dsmc client.
 */
int
dct_fini(void)
{
	D_DEBUG(DF_MISC, "Entered dct_fini()\n");

	int rc;

	pthread_mutex_lock(&module_lock);
	if (!module_initialized)
		D_GOTO(unlock, rc = -DER_UNINIT);

	daos_rpc_unregister(dct_rpcs);

	rc = daos_eq_lib_fini();
	if (rc != 0) {
		D_ERROR("failed to finalize eq: %d\n", rc);
		D_GOTO(unlock, rc);
	}

	module_initialized = 0;

unlock:
	pthread_mutex_unlock(&module_lock);
	D_DEBUG(DF_MISC, "Returning from dct_fini()\n");
	return rc;
}
