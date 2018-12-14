/*
 * (C) Copyright 2019 Intel Corporation.
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/**
 * ds_sec: Security Framework Server
 *
 * This is part of daos_server. It exports the security RPC handlers and
 * implements Security Framework Server API.
 */
#define D_LOGFAC	DD_FAC(security)

#include <daos/rpc.h>
#include <daos_srv/daos_server.h>
#include "srv_internal.h"

/** Fully qualified path to daos_server socket */
char *ds_sec_server_socket_path;

static int
init(void)
{
	int rc;

	rc = asprintf(&ds_sec_server_socket_path, "%s/%s",
			dss_socket_dir, "daos_server.sock");
	if (rc < 0) {
		return rc;
	}
	return 0;
}

static int
fini(void)
{
	free(ds_sec_server_socket_path);
	ds_sec_server_socket_path = NULL;
	return 0;
}

struct dss_module security_module =  {
	.sm_name	= "security",
	.sm_mod_id	= DAOS_SEC_MODULE,
	.sm_ver		= DAOS_SEC_VERSION,
	.sm_init	= init,
	.sm_fini	= fini,
	.sm_setup	= NULL,
	.sm_cleanup	= NULL,
	.sm_proto_fmt	= NULL,
	.sm_cli_count	= 0,
	.sm_handlers	= NULL,
	.sm_key		= NULL,
};
