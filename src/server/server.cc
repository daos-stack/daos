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

static void
sig_handler(int signo)
{
	if (signo == SIGINT)
		server_fini(true);
}

void
test()
{
	int rc;

	/* load the management module */
	rc = dss_module_load("daos_mgmt_srv");
	if (rc)
		return;

	D_DEBUG(DF_SERVER, "management module successfully loaded\n");

	rc = dss_module_unload("daos_mgmt_srv");
	if (rc)
		return;

	D_DEBUG(DF_SERVER, "management module successfully unloaded\n");
}

int
main()
{
	int rc;

	rc = server_init();
	if (rc)
		return rc;

	if (signal(SIGINT, sig_handler) == SIG_ERR) {
		D_DEBUG(DF_SERVER, "cannot register signal handler");
		exit(1);
	}

	/* XXX just for testing, to be removed */
	test();
	sleep(10);

	server_fini(true);
	return 0;
}
