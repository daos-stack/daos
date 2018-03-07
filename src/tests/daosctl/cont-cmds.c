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
#include <uuid/uuid.h>

/* daos specific */
#include <daos.h>
#include <daos_api.h>
#include <daos_mgmt.h>
#include <daos/common.h>

/**
 * This file contains the container related user commands.
 *
 * For each command there are 3 items of interest: a structure that
 * contains the arguments for the command; a callback function that
 * takes the arguments from argp and puts them in the structure; a
 * function that sends the arguments to the DAOS API and handles the
 * reply.  All commands share the same structure and callback function
 * at present.
 */

struct container_cmd_options {
	char          *server_group;
	char          *pool_uuid;
	char          *cont_uuid;
	unsigned int  force;
	unsigned int  mode;
	unsigned int  uid;
	unsigned int  gid;
};

/**
 * Callback function for container commands works with argp to put
 * all the arguments into a structure.
 */
static int
parse_cont_args_cb(int key, char *arg,
		   struct argp_state *state)
{
	struct container_cmd_options *options = state->input;

	switch (key) {
	case 's':
		options->server_group = arg;
		break;
	case 'i':
		options->pool_uuid = arg;
		break;
	case 'c':
		options->cont_uuid = arg;
		break;
	case 'f':
		options->force = 1;
	}
	return 0;
}

/**
 * Process a create container command.
 */
int
cmd_create_container(int argc, const char **argv, void *ctx)
{
	int              rc = -ENXIO;
	uuid_t           pool_uuid;
	uuid_t           cont_uuid;
	daos_handle_t    poh;
	d_rank_list_t    svc;
	unsigned int     flag = DAOS_PC_EX;
	daos_pool_info_t info;

	struct argp_option options[] = {
		{"server-group", 's', "SERVER-GROUP", 0,
		 "ID of the server group that owns the pool"},
		{"p-uuid", 'i', "UUID", 0,
		 "ID of the pool that is to host the new container."},
		{"c-uuid", 'c', "UUID", 0,
		 "ID of the container if a specific one is desired."},
		{0}
	};
	struct argp argp = {options, parse_cont_args_cb};
	struct container_cmd_options cc_options = {"daos_server",
						   NULL, NULL, 0, 0, 0, 0};

	/* adjust the arguments to skip over the command */
	argv++;
	argc--;

	/* once the command is removed the remaining arguments conform
	 * to GNU standards and can be parsed with argp
	 */
	argp_parse(&argp, argc, (char **restrict)argv, 0, 0, &cc_options);

	/* uuid needs extra parsing */
	if (cc_options.pool_uuid == NULL)
		return EINVAL;
	rc = uuid_parse(cc_options.pool_uuid, pool_uuid);

	/* TODO should be a parameter not hard-coded */
	uint32_t          rl_ranks = 0;

	svc.rl_nr = 1;
	svc.rl_ranks = &rl_ranks;

	rc = daos_pool_connect(pool_uuid, cc_options.server_group, &svc,
			       flag, &poh, &info, NULL);

	if (rc) {
		printf("Pool connect fail, result: %d\n", rc);
		return rc;
	}

	/* create a UUID for the container if none supplied*/
	if (cc_options.cont_uuid == NULL) {
		uuid_generate(cont_uuid);
	} else {
		rc = uuid_parse(cc_options.cont_uuid, cont_uuid);
		if (rc != 0)
			goto done;
	}

	rc = daos_cont_create(poh, cont_uuid, NULL);

	if (rc) {
		printf("Container create fail, result: %d\n", rc);
	} else {
		char uuid_str[100];

		uuid_unparse(cont_uuid, uuid_str);
		printf("Success, container UUID: %s\n", uuid_str);
	}

done:
	daos_pool_disconnect(poh, NULL);
	return rc;
}

/**
 * Process a destroy container command.
 */
int
cmd_destroy_container(int argc, const char **argv, void *ctx)
{
	int              rc = -ENXIO;
	uuid_t           pool_uuid;
	uuid_t           cont_uuid;
	daos_handle_t    poh;
	d_rank_list_t    svc;
	unsigned int     flag = DAOS_PC_RW;
	daos_pool_info_t info;

	struct argp_option options[] = {
		{"server-group",    's',  "SERVER-GROUP", 0,
		 "ID of the server group that owns the pool"},
		{"pool-uuid",       'i',  "UUID",         0,
		 "ID of the pool that hosts the container to be destroyed."},
		{"cont-uuid",       'c',  "UUID",         0,
		 "ID of the container to be destroyed."},
		{"force",           'f',  0,              OPTION_ARG_OPTIONAL,
		 "Force pool destruction regardless of current state."},
		{0}
	};
	struct argp argp = {options, parse_cont_args_cb};
	struct container_cmd_options cc_options = {"daos_server", NULL, NULL,
						   0, 0, 0, 0};

	/* adjust the arguments to skip over the command */
	argv++;
	argc--;

	/* once the command is removed the remaining arguments conform
	 * to GNU standards and can be parsed with argp
	 */
	argp_parse(&argp, argc, (char **restrict)argv, 0, 0, &cc_options);

	/* uuids needs extra parsing */
	rc = uuid_parse(cc_options.pool_uuid, pool_uuid);
	rc = uuid_parse(cc_options.cont_uuid, cont_uuid);

	/* TODO should be a parameter not hard-coded */
	uint32_t          rl_ranks = 0;

	svc.rl_nr = 1;

	svc.rl_ranks = &rl_ranks;

	rc = daos_pool_connect(pool_uuid, cc_options.server_group, &svc,
			       flag, &poh, &info, NULL);

	if (rc) {
		printf("Pool connect fail, result: %d\n", rc);
		return rc;
	}

	/**
	 * For now ignore callers force preference because its not implemented
	 * and asserts.
	 */
	rc = daos_cont_destroy(poh, cont_uuid, 1, NULL);

	if (rc)
		printf("Container destroy fail, result: %d\n", rc);
	else
		printf("Container destroyed.\n");

	daos_pool_disconnect(poh, NULL);
	return rc;
}

/**
 * Process a container query command.
 */
int
cmd_query_container(int argc, const char **argv, void *ctx)
{
	int              rc = -ENXIO;
	uuid_t           pool_uuid;
	uuid_t           cont_uuid;
	daos_handle_t    poh;
	d_rank_list_t    svc;
	unsigned int     flag = DAOS_PC_RW;
	daos_pool_info_t pool_info;
	daos_handle_t    coh;
	daos_cont_info_t cont_info;

	struct argp_option options[] = {
		{"server-group",    's',  "SERVER-GROUP", 0,
		 "ID of the server group that owns the pool"},
		{"pool-uuid",       'i',  "UUID",         0,
		 "ID of the pool that hosts the container to be queried."},
		{"cont-uuid",       'c',  "UUID",         0,
		 "ID of the container to be queried."},
		{0}
	};
	struct argp argp = {options, parse_cont_args_cb};
	struct container_cmd_options cc_options = {"daos_server", NULL, NULL,
						   0, 0, 0, 0};

	/* adjust the arguments to skip over the command */
	argv++;
	argc--;

	/* once the command is removed the remaining arguments conform
	 * to GNU standards and can be parsed with argp
	 */
	argp_parse(&argp, argc, (char **restrict)argv, 0, 0, &cc_options);

	/* uuids needs extra parsing */
	rc = uuid_parse(cc_options.pool_uuid, pool_uuid);
	rc = uuid_parse(cc_options.cont_uuid, cont_uuid);

	/* TODO should be a parameter not hard-coded */
	uint32_t          rl_ranks = 0;

	svc.rl_nr = 1;
	svc.rl_ranks = &rl_ranks;

	rc = daos_pool_connect(pool_uuid, cc_options.server_group, &svc,
			       flag, &poh, &pool_info, NULL);

	if (rc) {
		printf("Pool connect fail, result: %d\n", rc);
		return rc;
	}

	rc = daos_cont_open(poh, cont_uuid, DAOS_COO_RO, &coh,
			    &cont_info, NULL);

	if (rc) {
		printf("Container open fail, result: %d\n", rc);
		goto done2;
	}

	rc = daos_cont_query(coh, &cont_info, NULL);

	if (rc) {
		printf("Container query failed, result: %d\n", rc);
		goto done1;
	}

	char uuid_str[100];

	uuid_unparse(pool_uuid, uuid_str);
	printf("Pool UUID: %s\n", uuid_str);

	uuid_unparse(cont_uuid, uuid_str);
	printf("Container UUID: %s\n", uuid_str);

	printf("Highest committed epoch: %llu\n",
	       (long long int)cont_info.ci_epoch_state.es_hce);
	printf("Lowest referenced epoch: %llu\n",
	       (long long int)cont_info.ci_epoch_state.es_lre);
	printf("Lowest held epoch: %llu\n",
	       (long long int)cont_info.ci_epoch_state.es_lhe);
	printf("Global highest committed epoch: %llu\n",
	       (long long int)cont_info.ci_epoch_state.es_ghce);
	printf("Global highest partially commited epoch: %llu\n",
	       (long long int)cont_info.ci_epoch_state.es_ghpce);
	printf("Number of snapshots: %i\n",
	       (int)cont_info.ci_nsnapshots);

 done1:
	daos_cont_close(coh, NULL);
 done2:
	daos_pool_disconnect(poh, NULL);
	return rc;
}
