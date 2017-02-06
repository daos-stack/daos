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
#define DD_SUBSYS	DD_FAC(server)

#include <signal.h>
#include <abt.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>

#include <daos/btree_class.h>
#include <daos/common.h>
#include "srv_internal.h"

#include <daos.h> /* for daos_init() */

#define MAX_MODULE_OPTIONS	64
#define MODULE_LIST		"vos,mgmt,pool,cont,obj,tier"

/** List of modules to load */
static char		modules[MAX_MODULE_OPTIONS + 1];

/**
 * Number of threads the user would like to start
 * 0 means default value, which is one thread per core
 */
static unsigned int	nr_threads;

/** Server crt group ID */
static char	       *server_group_id = DAOS_DEFAULT_GROUP_ID;

/** Storage path (hack) */
const char	       *storage_path = "/mnt/daos";

/** HW topology */
hwloc_topology_t	dss_topo;

/** Module facility bitmask */
static uint64_t		dss_mod_facs;

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
modules_load(uint64_t *facs)
{
	char		*mod;
	char		*sep;
	char		*run;
	uint64_t	 mod_facs;
	int		 rc = 0;

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

		mod_facs = 0;
		rc = dss_module_load(mod, &mod_facs);
		if (rc != 0) {
			D_ERROR("Failed to load module %s: %d\n",
				mod, rc);
			break;
		}

		if (facs != NULL)
			*facs |= mod_facs;

		mod = strsep(&run, ",");
	}

	free(sep);
	return rc;
}

static int
server_init()
{
	int		rc;
	crt_rank_t	rank = -1;
	uint32_t	size = -1;

	rc = daos_debug_init(NULL);
	if (rc != 0)
		return rc;

	rc = register_dbtree_classes();
	if (rc != 0)
		D_GOTO(exit_debug_init, rc);

	/** initialize server topology data */
	hwloc_topology_init(&dss_topo);
	hwloc_topology_load(dss_topo);

	/* initialize the modular interface */
	rc = dss_module_init();
	if (rc)
		D_GOTO(exit_debug_init, rc);

	D_INFO("Module interface successfully initialized\n");

	/* initialize the network layer */
	rc = crt_init(server_group_id, CRT_FLAG_BIT_SERVER);
	if (rc)
		D_GOTO(exit_mod_init, rc);
	D_INFO("Network successfully initialized\n");

	/* load modules */
	rc = modules_load(&dss_mod_facs);
	if (rc)
		D_GOTO(exit_mod_loaded, rc);
	D_INFO("Module %s successfully loaded\n", modules);

	/* start up service */
	rc = dss_srv_init(nr_threads);
	if (rc)
		D_GOTO(exit_mod_loaded, rc);
	D_INFO("Service is now running\n");

	if (dss_mod_facs & DSS_FAC_LOAD_CLI) {
		rc = daos_init();
		if (rc) {
			D_ERROR("daos_init (client) failed, rc: %d.\n", rc);
			D_GOTO(exit_srv_init, rc);
		}
		D_INFO("Client stack enabled\n");
	}

	crt_group_rank(NULL, &rank);
	crt_group_size(NULL, &size);
	D_PRINT("DAOS server (v%s) started on rank %u (out of %u) with %u "
		"xstream(s)\n", DAOS_VERSION, rank, size, dss_nxstreams);

	return 0;
exit_srv_init:
	dss_srv_fini(true);
exit_mod_loaded:
	dss_module_unload_all();
	crt_finalize();
exit_mod_init:
	dss_module_fini(true);
exit_debug_init:
	daos_debug_fini();
	return rc;
}

static void
server_fini(bool force)
{
	D_INFO("Service is shutting down\n");
	if (dss_mod_facs & DSS_FAC_LOAD_CLI)
		daos_fini();
	dss_srv_fini(force);
	dss_module_fini(force);
	crt_finalize();
	dss_module_unload_all();
}

static void
usage(char *prog, FILE *out)
{
	fprintf(out,
		"Usage: %s [ -m vos,mgmt,pool,cont,obj,tier ] [-c #cores]"
		"          [-g server_group_name]\n",
		prog);
}

static int
parse(int argc, char **argv)
{
	struct	option opts[] = {
		{ "modules",	required_argument,	NULL,	'm' },
		{ "cores",	required_argument,	NULL,	'c' },
		{ "group",	required_argument,	NULL,	'g' },
		{ "storage",	required_argument,	NULL,	's' },
		{ NULL,		0,			NULL,	0}
	};
	int	rc = 0;
	int	c;

	/* load all of modules by default */
	sprintf(modules, "%s", MODULE_LIST);
	while ((c = getopt_long(argc, argv, "c:m:g:s:", opts, NULL)) != -1) {
		switch (c) {
		case 'm':
			if (strlen(optarg) > MAX_MODULE_OPTIONS) {
				rc = -DER_INVAL;
				usage(argv[0], stderr);
				break;
			}
			snprintf(modules, sizeof(modules), "%s", optarg);
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
		case 'g':
			server_group_id = optarg;
			break;
		case 's':
			storage_path = optarg;
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
