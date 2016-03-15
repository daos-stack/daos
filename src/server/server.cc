/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * This file is part of the DAOS server. It implements the startup/shutdown
 * routine for the daos_server.
 */

#include <signal.h>
#include <stdlib.h>

#include <daos/daos_common.h>
#include <daos/daos_transport.h>

#include "dss_internal.h"

static int
server_init()
{
	int rc;

	/* use full debug dy default for now */
	rc = setenv("DAOS_DEBUG", "-1", false);
	if (rc)
		D_ERROR("failed to enable full debug, %d\n", rc);

	/* initialize the network layer */
	rc = dtp_init("bmi+tcp://localhost:8889", true);
	if (rc) {
		D_ERROR("failed to initialize network, %d\n", rc);
		return rc;
	}
	D_DEBUG(DF_SERVER, "Network successfully initialized\n");

	/* initialize the modular interface */
	rc = dss_module_init();
	if (rc) {
		D_ERROR("failed to initialize the modular interface, %d\n", rc);
		goto exit_net;
	}
	D_DEBUG(DF_SERVER, "Module interface successfully initialized\n");

	return 0;

exit_net:
	dtp_finalize();
	return rc;
}

static void
server_fini(bool force)
{
	dss_module_fini(force);
	dtp_finalize();
}

void
test()
{
	dtp_context_t   ctx;
	int		i = 0;
	int		rc;

	rc = dtp_context_create(NULL, &ctx);
	if (rc) {
		D_ERROR("failed to create context %d\n", rc);
		return;
	}

	while (i < 100) {
		rc = dtp_progress(ctx, 1, NULL, NULL, NULL);
		if (rc != 0 && rc != -ETIMEDOUT) {
			D_ERROR("progress failed %d\n", rc);
			break;
		}
		sleep(1);
		i++;
	}

	rc = dtp_context_destroy(ctx, 0);
	if (rc)
		D_ERROR("failed to destroy context %d\n", rc);
}

static int
modules_load()
{
	int rc;

	rc = dss_module_load("daos_mgmt_srv");
	if (rc)
		return rc;

	D_DEBUG(DF_SERVER, "daos_mgmt_srv module successfully unloaded\n");

	return 0;
}

static void
modules_unload()
{
	dss_module_unload("daos_mgmt_srv");
}

static void
sig_handler(int signo)
{
	if (signo == SIGINT) {
		modules_unload();
		server_fini(true);
	}
}

int
main()
{
	int rc;

	/* generic server initialization */
	rc = server_init();
	if (rc)
		return rc;

	/* loaded default modules */
	modules_load();
	if (rc)
		goto out_server;

	if (signal(SIGINT, sig_handler) == SIG_ERR) {
		D_ERROR("cannot register signal handler\n");
		rc = -DER_INVAL;
		goto out_mod;
	}

	/* XXX just for testing, to be removed */
	test();

out_mod:
	modules_unload();
out_server:
	server_fini(true);

	return rc;
}
