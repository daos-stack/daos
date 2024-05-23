/*
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * ds_sec: Security Framework Server
 *
 * This is part of daos_server. It exports the security RPC handlers and
 * implements Security Framework Server API.
 */
#define D_LOGFAC	DD_FAC(security)

#include <daos/rpc.h>
#include <daos_srv/daos_engine.h>
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

struct dss_module security_module = {
    .sm_name        = "security",
    .sm_mod_id      = DAOS_SEC_MODULE,
    .sm_ver         = DAOS_SEC_VERSION,
    .sm_proto_count = 1,
    .sm_init        = init,
    .sm_fini        = fini,
    .sm_setup       = NULL,
    .sm_cleanup     = NULL,
    .sm_proto_fmt   = {NULL},
    .sm_cli_count   = {0},
    .sm_handlers    = {NULL},
    .sm_key         = NULL,
};
