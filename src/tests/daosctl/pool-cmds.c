/**
 * (C) Copyright 2018 Intel Corporation.
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

/* generic */
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <mpi.h>
#include <stdint.h>
#include <inttypes.h>
#include <argp.h>

/* daos specific */
#include <daos.h>
#include <daos_api.h>
#include <daos_mgmt.h>
#include <daos/common.h>

/**
 * This file contains the pool related user commands.
 *
 * For each command there are 3 items of interest: a structure that
 * contains the arguments for the command; a callback function that
 * takes the arguments from argp and puts them in the structure; a
 * function that sends the arguments to the DAOS API and handles the
 * reply.  All commands share the same structure and callback function
 * at present.
 */

struct pool_cmd_options {
	char          *server_group;
	char          *uuid;
	unsigned int  force;
	unsigned int  mode;
	unsigned int  uid;
	unsigned int  gid;
	uint64_t      size;
};

static int
parse_size(uint64_t *size, char *arg)
{
	char *unit;
	*size = strtoul(arg, &unit, 0);

	switch (*unit) {
	case '\0':
		break;
	case 'k':
	case 'K':
		*size <<= 10;
		break;
	case 'm':
	case 'M':
		*size <<= 20;
		break;
	case 'g':
	case 'G':
		*size <<= 30;
		break;
	}
	return 0;
}

/**
 * Callback function for create poolthat works with argp to put
 * all the arguments into a structure.
 */
static int
parse_pool_args_cb(int key, char *arg,
		   struct argp_state *state)
{
	struct pool_cmd_options *options = state->input;

	switch (key) {
	case 's':
		options->server_group = arg;
		break;
	case 'i':
		options->uuid = arg;
		break;
	case 'm':
		options->mode = atoi(arg);
		break;
	case 'u':
		options->uid = atoi(arg);
		break;
	case 'g':
		options->gid = atoi(arg);
		break;
	case 'z':
		parse_size(&(options->size), arg);
		break;
	case 'f':
		options->force = 1;
	}
	return 0;
}

/**
 * Process a create pool command.
 */
int
cmd_create_pool(int argc, const char **argv, void *ctx)
{
	int           rc = -ENXIO;
	uuid_t        uuid;
	d_rank_list_t svc;

	struct argp_option options[] = {
		{"server-group",   's',    "SERVER-GROUP",     0,
		 "ID of the server group that is to manage the new pool"},
		{"uid",            'u',    "UID",              0,
		 "User ID that is to own the new pool"},
		{"gid",            'g',    "GID",              0,
		 "Group ID that is to own the new pool"},
		{"mode",           'm',    "mode",             0,
		 "Mode defines the operations allowed on the pool"},
		{"size",           'z',    "size",             0,
		 "Size of the pool in bytes or with k/m/g appended (e.g. 10g)"},
		{0}
	};
	struct argp argp = {options, parse_pool_args_cb};
	struct pool_cmd_options cp_options = {"daos_server", NULL, 0, 0700, 0,
					      1024*1024*1024, 0};

	/* adjust the arguments to skip over the command */
	argv++;
	argc--;

	/* once the command is removed the remaining arguments conform
	 * to GNU standards and can be parsed with argp
	 */
	argp_parse(&argp, argc, (char **restrict)argv, 0, 0, &cp_options);

	/* TODO shouldn't be hard-coded */
	uint32_t rl_ranks = 0;

	svc.rl_nr = 1;
	svc.rl_ranks = &rl_ranks;

	rc = daos_pool_create(cp_options.mode, cp_options.uid,
			      cp_options.gid, cp_options.server_group,
				   NULL, "rubbish", cp_options.size, &svc,
				   uuid, NULL);
	if (rc) {
		printf("Pool create fail, result: %d\n", rc);
	} else {
		char uuid_str[100];

		uuid_unparse(uuid, uuid_str);
		printf("%s\n", uuid_str);
	}

	return rc;
}

/**
 * Function to process a destroy pool command.
 */
int
cmd_destroy_pool(int argc, const char **argv, void *ctx)
{
	uuid_t uuid;
	int    rc;

	struct argp_option options[] = {
		{"server-group",   's',   "SERVER-GROUP",   0,
		 "ID of the server group that manages the pool"},
		{"uuid",           'i',   "UUID",           0,
		 "ID of the pool that is to be destroyed"},
		{"force",          'f',   0,                0,
		 "Force pool destruction regardless of current state."},
		{0}
	};
	struct argp argp = {options, parse_pool_args_cb};
	struct pool_cmd_options dp_options = {"daos_server", NULL, 0, 0, 0, 0};

	/* adjust the arguments to skip over the command */
	argv++;
	argc--;

	/* once the command is removed the remaining arguments conform
	 * to GNU standards and can be parsed with argp
	 */
	argp_parse(&argp, argc, (char **restrict)argv, 0, 0, &dp_options);

	printf("destroy_pool uuid:%s server:%s force:%i\n", dp_options.uuid,
	       dp_options.server_group, dp_options.force);

	rc = uuid_parse(dp_options.uuid, uuid);

	rc = daos_pool_destroy(uuid, dp_options.server_group,
			       (int)dp_options.force, NULL);

	if (rc)
		printf("<<<daosctl>>> Pool destroy result: %d\n", rc);
	else
		printf("<<<daosctl>>> Pool destroyed.\n");

	fflush(stdout);

	return rc;
}

/**
 * Function to process an evict pool command which kicks out clients
 * that are attached to a pool.
 */
int
cmd_evict_pool(int argc, const char **argv, void *ctx)
{
	uuid_t uuid;
	int rc;
	struct pool_cmd_options ep_options = {"daos_server", NULL, 0, 0, 0, 0};
	d_rank_list_t svc;
	uint32_t rl_ranks = 0;
	struct argp_option options[] = {
		{"server-group",   's',   "SERVER-GROUP",   0,
		 "ID of the server group that manages the pool"},
		{"uuid",           'i',   "UUID",           0,
		 "ID of the pool to evict"},
		{0}
	};
	struct argp argp = {options, parse_pool_args_cb};

	/* adjust the arguments to skip over the command */
	argv++;
	argc--;

	/* once the command is removed the remaining arguments
	 * conform to GNU standards and can be parsed with argp
	 */
	argp_parse(&argp, argc, (char **restrict)argv, 0, 0, &ep_options);

	/* TODO make this a parameter */
	svc.rl_nr = 1;
	svc.rl_ranks = &rl_ranks;

	rc = uuid_parse(ep_options.uuid, uuid);

	rc = daos_pool_evict(uuid, ep_options.server_group, &svc, NULL);

	if (rc)
		printf("Client pool eviction failed with: %d\n",
		       rc);
	else
		printf("Clients evicted from pool successfuly.\n");

	fflush(stdout);

	return rc;
}
