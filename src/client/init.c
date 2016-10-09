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
 * dsrc: Module Definitions
 */

#include <daos/common.h>
#include <daos/client.h>
#include <daos/event.h>
#include <daos/pool.h>
#include <pthread.h>

static pthread_mutex_t	module_lock = PTHREAD_MUTEX_INITIALIZER;
static bool		module_initialized;

/**
 * Initialize DAOS client library.
 */
int
daos_init(void)
{
	int rc;

	pthread_mutex_lock(&module_lock);
	if (module_initialized)
		D_GOTO(unlock, rc = -DER_ALREADY);

	/** set up event queue */
	rc = daos_eq_lib_init();
	if (rc != 0) {
		D_ERROR("failed to initialize eq_lib: %d\n", rc);
		D_GOTO(unlock, rc);
	}

	/** set up management interface */
	rc = dc_mgmt_init();
	if (rc != 0)
		D_GOTO(out_eq, rc);

	/** set up pool */
	rc = dc_pool_init();
	if (rc != 0)
		D_GOTO(out_mgmt, rc);

	/** set up container */
	rc = dc_cont_init();
	if (rc != 0)
		D_GOTO(out_pool, rc);

	/** set up object */
	rc = dc_obj_init();
	if (rc != 0)
		D_GOTO(out_co, rc);

	module_initialized = true;
	D_GOTO(unlock, rc = 0);

out_co:
	dc_cont_fini();
out_pool:
	dc_pool_fini();
out_mgmt:
	dc_mgmt_fini();
out_eq:
	daos_eq_lib_fini();
unlock:
	pthread_mutex_unlock(&module_lock);
	return rc;
}

/**
 * Turn down DAOS client library
 */
int
daos_fini(void)
{
	int	rc;

	pthread_mutex_lock(&module_lock);
	if (!module_initialized)
		D_GOTO(unlock, rc = -DER_UNINIT);

	rc = daos_eq_lib_fini();
	if (rc != 0) {
		D_ERROR("failed to finalize eq: %d\n", rc);
		D_GOTO(unlock, rc);
	}

	dc_obj_fini();
	dc_cont_fini();
	dc_pool_fini();
	dc_mgmt_fini();

	module_initialized = false;
unlock:
	pthread_mutex_unlock(&module_lock);
	return rc;
}
