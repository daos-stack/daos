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
 * This file is part of the DAOS server. It implements the startup/shutdown
 * routines for the daos_server.
 */

#include <signal.h>
#include <abt.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>

#include <daos/btree_class.h>
#include <daos/common.h>
#include "dss_internal.h"

#define MAX_MODULE_OPTIONS	64
#define MODULE_LIST		"vos,mgmt,pool,cont,obj,tier"

/** List of modules to load */
static char		modules[MAX_MODULE_OPTIONS + 1];

/**
 * Number of threads the user would like to start
 * 0 means default value, which is one thread per core
 */
static unsigned int	nr_threads;

/** HW topology */
hwloc_topology_t	dss_topo;

/*
 * Register the dbtree classes used by native server-side modules (e.g.,
 * ds_pool, ds_cont, etc.). Unregistering is currently not supported.
 */
static int
register_dbtree_classes(void)
{
	int rc;

	rc = dbtree_class_register(DBTREE_CLASS_NV, 0 /* feats */,
				   &dbtree_nv_ops);
	if (rc != 0) {
		D_ERROR("failed to register DBTREE_CLASS_NV: %d\n", rc);
		return rc;
	}

	rc = dbtree_class_register(DBTREE_CLASS_UV, 0 /* feats */,
				   &dbtree_uv_ops);
	if (rc != 0) {
		D_ERROR("failed to register DBTREE_CLASS_UV: %d\n", rc);
		return rc;
	}

	rc = dbtree_class_register(DBTREE_CLASS_EC, 0 /* feats */,
				   &dbtree_ec_ops);
	if (rc != 0)
		D_ERROR("failed to register DBTREE_CLASS_EC: %d\n", rc);

	return rc;
}

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
		if (strcmp(mod, "object") == 0)
			mod = "obj";
		else if (strcmp(mod, "po") == 0)
			mod = "pool";
		else if (strcmp(mod, "container") == 0 ||
			 strcmp(mod, "co") == 0)
			mod = "cont";
		else if (strcmp(mod, "management") == 0)
			mod = "mgmt";
		else if (strcmp(mod, "vos") == 0)
			mod = "vos_srv";

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
	int	 rc;

	/** enable DF_SERVER */
	daos_debug_set(DF_SERVER);

	rc = register_dbtree_classes();
	if (rc != 0)
		return rc;

	/** initialize server topology data */
	hwloc_topology_init(&dss_topo);
	hwloc_topology_load(dss_topo);

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
	rc = dss_srv_init(nr_threads);
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
	D_DEBUG(DF_SERVER, "Service is shutting down\n");
	dss_srv_fini();
	dss_module_fini(force);
	dtp_finalize();
	dss_module_unload_all();
}

static void
usage(char *prog, FILE *out)
{
	fprintf(out,
		"Usage: %s [ -m vos,mgmt,pool,cont,obj,tier ] [-c #cores]\n",
		prog);
}

static int
parse(int argc, char **argv)
{
	struct	option opts[] = {
		{ "modules", required_argument, NULL, 'm' },
		{ "number of cores to use", required_argument, NULL, 'c' },
		{ NULL },
	};
	int	rc = 0;
	int	c;

	/* load all of modules by default */
	sprintf(modules, "%s", MODULE_LIST);
	while ((c = getopt_long(argc, argv, "c:m:", opts, NULL)) != -1) {
		switch (c) {
		case 'm':
			if (strlen(optarg) > MAX_MODULE_OPTIONS) {
				rc = -DER_INVAL;
				usage(argv[0], stderr);
				break;
			}
			sprintf(modules, "%s", optarg);
			break;
		case 'c': {
			unsigned int	 nr;
			char		*end;

			nr = strtoul(optarg, &end, 10);
			if (end == optarg || nr == ULONG_MAX) {
				rc = -DER_INVAL;
				break;
			}
			nr_threads = nr;
			break;
		}
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
	sigset_t	set;
	int		sig;
	int		rc;

	/** parse command line arguments */
	rc = parse(argc, argv);
	if (rc)
		exit(EXIT_FAILURE);

	/** block all possible signals */
	sigfillset(&set);
	rc = pthread_sigmask(SIG_BLOCK, &set, NULL);
	if (rc) {
		perror("failed to mask signals");
		exit(EXIT_FAILURE);
	}

	rc = ABT_init(argc, argv);
	if (rc != 0) {
		D_ERROR("failed to init ABT: %d\n", rc);
		exit(EXIT_FAILURE);
	}
	/** server initialization */
	rc = server_init();
	if (rc)
		exit(EXIT_FAILURE);

	/** wait for shutdown signal */
	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGUSR1);
	sigaddset(&set, SIGUSR2);
	rc = sigwait(&set, &sig);
	if (rc)
		D_ERROR("failed to wait for signals: %d\n", rc);

	/** shutdown */
	server_fini(true);

	ABT_finalize();
	exit(EXIT_SUCCESS);
}
