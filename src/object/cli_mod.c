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
 * object client: Module Definitions
 */
#define DDSUBSYS	DDFAC(object)

#include <daos_types.h>
#include <daos/rpc.h>
#include <pthread.h>
#include <daos/common.h>
#include "obj_rpc.h"
#include "obj_internal.h"

bool	cli_bypass_rpc;

/**
 * Initialize object interface
 */
int
dc_obj_init(void)
{
	char	*env;
	int	 rc;

	env = getenv(IO_BYPASS_ENV);
	if (env && !strcasecmp(env, "cli_rpc")) {
		D__DEBUG(DB_IO, "All client I/O RPCs will be dropped\n");
		cli_bypass_rpc = true;
	}

	rc = daos_rpc_register(daos_obj_rpcs, NULL, DAOS_OBJ_MODULE);
	return rc;
}

/**
 * Finalize object interface
 */
void
dc_obj_fini(void)
{
	daos_rpc_unregister(daos_obj_rpcs);
}
