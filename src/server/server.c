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
#include <getopt.h>
#include <errno.h>

#include <daos/common.h>
#include "dss_internal.h"

#define MAX_MODULE_OPTIONS	64
#define MODULE_LIST		"vos,dmg,dsm,dsr"

char	modules[MAX_MODULE_OPTIONS + 1];

static int
modules_load()
{
	char	*mod;
	char	*sep;
	char	*run;
	int	 rc = 0;

	sep = strdup(modules);
	if (sep == NULL)
		return -DER_NOMEM;
	run = sep;

	mod = strsep(&run, ",");
	while (mod != NULL) {
		if (strcmp(mod, "daos_sr") == 0 ||
		    strcmp(mod, "dsr") == 0)
			rc = dss_module_load("daos_sr_srv");
		else if (strcmp(mod, "daos_m") == 0 ||
			 strcmp(mod, "dsm") == 0)
			rc = dss_module_load("daos_m_srv");
		else if (strcmp(mod, "daos_mgmt") == 0 ||
			 strcmp(mod, "dmg") == 0)
			rc = dss_module_load("daos_mgmt_srv");
		else if (strcmp(mod, "vos") == 0)
			rc = dss_module_load("vos");
		else
			rc = dss_module_load(mod);

		if (rc != 0) {
			D_DEBUG(DF_SERVER, "Failed to load module %s: %d\n",
				mod, rc);
			break;
		}

		mod = strsep(&run, ",");
	}

	free(sep);
	return rc;
}

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
	if (rc)
		return rc;
	D_DEBUG(DF_SERVER, "Module interface successfully initialized\n");

	/* initialize the network layer */
	rc = dtp_init(true);
	if (rc)
		D_GOTO(exit_mod_init, rc);
	D_DEBUG(DF_SERVER, "Network successfully initialized\n");

	/* load modules */
	rc = modules_load();
	if (rc)
		D_GOTO(exit_mod_loaded, rc);
	D_DEBUG(DF_SERVER, "Module %s successfully loaded\n", modules);

	/* start up service */
	rc = dss_srv_init();
	if (rc)
		D_GOTO(exit_mod_loaded, rc);
	D_DEBUG(DF_SERVER, "Service is now running\n");

	return 0;

exit_mod_loaded:
	dss_module_unload_all();
	dtp_finalize();
exit_mod_init:
	dss_module_fini(true);
	return rc;
}

static void
server_fini(bool force)
{
	dss_srv_fini();
	dss_module_fini(force);
	dtp_finalize();
}

static void
shutdown(int signo)
{
	server_fini(true);
	dss_module_unload_all();
	exit(EXIT_SUCCESS);
}

static void
usage(char *prog, FILE *out)
{
	fprintf(out, "Usage: %s [ -m vos,dmg,dsm,dsr ]\n", prog);
}

static int
parse(int argc, char **argv)
{
	struct	option opts[] = {
		{ "modules", required_argument, NULL, 'm' },
		{ NULL },
	};
	int	rc = 0;
	int	c;

	/* load all of modules by default */
	sprintf(modules, "%s", MODULE_LIST);
	while ((c = getopt_long(argc, argv, "m:", opts, NULL)) != -1) {
		switch (c) {
		case 'm':
			if (strlen(optarg) > MAX_MODULE_OPTIONS) {
				rc = -DER_INVAL;
				usage(argv[0], stderr);
				break;
			}
			sprintf(modules, "%s", optarg);
			break;
		default:
			usage(argv[0], stderr);
			rc = -DER_INVAL;
		}
		if (rc < 0)
			return rc;
	}

	return 0;
}

int
main(int argc, char **argv)
{
	int rc;

	/* parse command line arguments */
	rc = parse(argc, argv);
	if (rc)
		exit(EXIT_FAILURE);

	/* server initialization */
	rc = server_init();
	if (rc)
		exit(EXIT_FAILURE);

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
