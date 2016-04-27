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
 * routines for the daos_server.
 */

#include <signal.h>
#include <stdlib.h>
#include <errno.h>

#include <daos/common.h>
#include "dss_internal.h"

static int
server_init()
{
	int rc;

	/* use full debug dy default for now */
	rc = setenv("DAOS_DEBUG", "-1", false);
	if (rc)
		D_ERROR("failed to enable full debug, %d\n", rc);

	/* initialize the modular interface */
	rc = dss_module_init();
	if (rc) {
		D_ERROR("failed to initialize the modular interface, %d\n", rc);
		return rc;
	}
	D_DEBUG(DF_SERVER, "Module interface successfully initialized\n");

	/* start up service */
	rc = dss_srv_init();
	if (rc != 0) {
		D_ERROR("failed to initialize service: rc = %d\n", rc);
		goto exit_module;
	}

	return 0;

exit_module:
	dss_module_fini(true);
	return rc;
}

static void
server_fini(bool force)
{
	dss_srv_fini();
	dss_module_fini(force);
}

static int
modules_load()
{
	int rc;

	rc = dss_module_load("daos_mgmt_srv");
	if (rc) {
		D_DEBUG(DF_SERVER, "failed to load daos_mgmt_srv module\n");
		return rc;
	}

	D_DEBUG(DF_SERVER, "daos_mgmt_srv module successfully loaded\n");

	/* XXX daos_m_srv should be dynamically loaded, instead of by default */
	rc = dss_module_load("daos_m_srv");
	if (rc != 0) {
		D_DEBUG(DF_SERVER, "failed to load daos_m_srv module\n");
		return rc;
	}

	return 0;
}

static void
modules_unload()
{
	dss_module_unload("daos_m_srv");
	dss_module_unload("daos_mgmt_srv");
	D_DEBUG(DF_SERVER, "daos_mgmt_srv module successfully unloaded\n");
}

static void
shutdown(int signo)
{
	server_fini(true);
	modules_unload();
	exit(EXIT_SUCCESS);
}

int
main()
{
	int rc;

	/* generic server initialization */
	rc = server_init();
	if (rc)
		exit(EXIT_FAILURE);

	/* load default modules */
	modules_load();
	if (rc) {
		server_fini(true);
		exit(EXIT_FAILURE);
	}

	/* register signal handler for shutdown */
	signal(SIGINT, shutdown);
	signal(SIGTERM, shutdown);
	signal(SIGCHLD, SIG_IGN);

	/* sleep indefinitely until we receive a signal */
	while (true)
		sleep(3600);

	shutdown(SIGTERM);
	exit(EXIT_SUCCESS);
}
