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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of the DAOS server. It implements the dRPC client for
 * communicating with daos_server.
 */

#define D_LOGFAC DD_FAC(server)

#include <daos/drpc.h>
#include <daos/drpc_modules.h>
#include <daos_srv/daos_server.h>

/** dRPC client context */
struct drpc *dss_drpc_ctx;

/* Notify daos_server that we are ready (e.g., to receive dRPC requests). */
static int
notify_ready(void)
{
	Drpc__Call     *req;
	Drpc__Response *resp;
	int		rc;

	req = drpc_call_create(dss_drpc_ctx,
			       DRPC_MODULE_SRV,
			       DRPC_METHOD_SRV_NOTIFY_READY);
	if (req == NULL) {
		return -DER_NOMEM;
	}

	rc = drpc_call(dss_drpc_ctx, R_SYNC, req, &resp);

	drpc_call_free(req);
	drpc_response_free(resp);
	return rc;
}

int
drpc_init(void)
{
	char   *path;
	int	rc;

	rc = asprintf(&path, "%s/%s", dss_socket_dir, "daos_server.sock");
	if (rc < 0) {
		rc = -DER_NOMEM;
		goto out;
	}

	D_ASSERT(dss_drpc_ctx == NULL);
	dss_drpc_ctx = drpc_connect(path);
	if (dss_drpc_ctx == NULL) {
		rc = -DER_NOMEM;
		goto out_path;
	}

	rc = notify_ready();
	if (rc != 0) {
		drpc_close(dss_drpc_ctx);
		dss_drpc_ctx = NULL;
	}

out_path:
	D_FREE(path);
out:
	return rc;
}

void
drpc_fini(void)
{
	int rc;

	D_ASSERT(dss_drpc_ctx != NULL);
	rc = drpc_close(dss_drpc_ctx);
	D_ASSERTF(rc == 0, "%d\n", rc);
	dss_drpc_ctx = NULL;
}
