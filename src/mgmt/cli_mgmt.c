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
 * DAOS management client library. It exports the mgmt API defined in
 * daos_mgmt.h
 */

#include "rpc.h"

/**
 * Initialize management interface
 */
int
dc_mgmt_init()
{
	int rc;

	rc = daos_rpc_register(mgmt_rpcs, NULL, DAOS_MGMT_MODULE);
	if (rc != 0)
		D_ERROR("failed to register rpcs: %d\n", rc);

	return rc;
}

/**
 * Finalize management interface
 */
void
dc_mgmt_fini()
{
	daos_rpc_unregister(mgmt_rpcs);
}
