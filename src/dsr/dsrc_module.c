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

#include <daos_m.h>
#include <pthread.h>
#include <daos/transport.h>

static pthread_mutex_t	module_lock = PTHREAD_MUTEX_INITIALIZER;
static int		module_initialized;

/**
 * Initialize dsrc client library.
 */
int
dsr_init(void)
{
	int rc;

	pthread_mutex_lock(&module_lock);
	if (module_initialized)
		D_GOTO(unlock, rc = -DER_ALREADY);

	rc = dsm_init();
	if (rc != 0)
		D_GOTO(unlock, rc);

	module_initialized = 1;
unlock:
	pthread_mutex_unlock(&module_lock);
	return rc;
}

/**
 * Finish dsrc client.
 */
int
dsr_fini(void)
{
	int	rc;

	pthread_mutex_lock(&module_lock);
	if (!module_initialized)
		D_GOTO(unlock, rc = -DER_UNINIT);

	rc = dsm_fini();
	if (rc != 0)
		D_GOTO(unlock, rc);

	module_initialized = 0;
unlock:
	pthread_mutex_unlock(&module_lock);
	return rc;
}
