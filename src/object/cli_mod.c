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
#define D_LOGFAC	DD_FAC(object)

#include <pthread.h>
#include <daos/common.h>
#include <daos/rpc.h>
#include <daos_types.h>
#include "obj_rpc.h"
#include "obj_internal.h"

bool	srv_io_dispatch = true;

/**
 * Initialize object interface
 */
int
dc_obj_init(void)
{
	int	 rc;

	d_getenv_bool("DAOS_IO_SRV_DISPATCH", &srv_io_dispatch);
	if (srv_io_dispatch)
		D_DEBUG(DB_IO, "Server IO dispatch enabled.\n");
	else
		D_DEBUG(DB_IO, "Server IO dispatch disabled.\n");

	rc = daos_rpc_register(&obj_proto_fmt, OBJ_PROTO_CLI_COUNT,
				NULL, DAOS_OBJ_MODULE);
	if (rc != 0)
		D_ERROR("failed to register daos obj RPCs: %d\n", rc);

	return rc;
}

/**
 * Finalize object interface
 */
void
dc_obj_fini(void)
{
	daos_rpc_unregister(&obj_proto_fmt);
}
