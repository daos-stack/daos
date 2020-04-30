/**
 * (C) Copyright 2018-2020 Intel Corporation.
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
#include <stdint.h>
#include <inttypes.h>
#include <argp.h>

/* daos specific */
#include "common_utils.h"
#include <daos.h>
#include <daos_api.h>
#include <daos_mgmt.h>
#include <daos/common.h>

/**
 * This file contains the pool related test commands.  Test commands
 * do nothing useful from a user perspective, for example a test command
 * might create and delete a pool all in one command.  This is great
 * for testing pool creation parameters bug not so useful otherwise.
 *
 * For each command there are 3 items of interest: a structure that
 * contains the arguments for the command; a callback function that
 * takes the arguments from argp and puts them in the structure; a
 * function that sends the arguments to the DAOS API and handles the
 * reply.  All commands share the same structure and callback function
 * at present.
 */

struct test_pool_options {
	char        *server_group;
	char        *uuid;
	char        *server_list;
	unsigned int mode;
	unsigned int uid;
	unsigned int gid;
	unsigned int read;
	unsigned int write;
	unsigned int exclusive;
	uint64_t     size;
	uint32_t     replica_count;
	char         *handle;
};

/**
 * callback function works with argp to put all the arguments
 * into a structure.
 */
static int
parse_pool_test_args_cb(int key, char *arg,
			struct argp_state *state)
{
	struct test_pool_options *options = state->input;

	switch (key) {
	case 'c':
		options->replica_count = atoi(arg);
		break;
	case 's':
		options->server_group = arg;
		break;
	case 'h':
		options->handle = arg;
		break;
	case 'i':
		options->uuid = arg;
		break;
	case 'l':
		options->server_list = arg;
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
	case 'r':
		options->read = 1;
		break;
	case 'w':
		options->write = 1;
		break;
	case 'e':
		options->exclusive = 1;
		break;
	case 'z':
		parse_size(arg, &(options->size));
		break;
	}
	return 0;
}

/**
 * A seemingly dumb command but it has its uses testing connections to pools.
 * It creates, connects to and then destroys a pool.
 */
int
cmd_connect_pool(int argc, const char **argv, void *ctx)
{
	d_rank_list_t		pool_service_list;
	daos_handle_t		poh;
	unsigned int		flag = DAOS_PC_RO;
	daos_pool_info_t	info = {0};
	daos_pool_info_t	info2 = {0};
	uuid_t			uuid;
	char			uuid_str2[100];
	int			rc = 1;
	struct test_pool_options cp_options = {
		"daos_server", NULL, NULL, 0, 0, 0, 0, 0, 0,
		1024*1024*1024, 1, NULL};

	struct argp_option options[] = {
		{"server-group",  's',    "SERVER-GROUP",    0,
		 "ID of the server group that manages the pool"},
		{"uuid",          'i',    "UUID",            0,
		 "ID of the pool to connect to"},
		{"read",          'r',    0,                 0,
		 "Enable read access"},
		{"write",         'm',    0,                 0,
		 "Enable write access"},
		{"exclusive",     'e',    0,                 0,
		 "Enable exclusive access"},
		{"servers",       'l',   "server rank-list", 0,
		 "pool service ranks, comma separated, no spaces e.g. -l 1,2"},
		{0}
	};
	struct argp argp = {options, parse_pool_test_args_cb};

	/* adjust the arguments to skip over the command */
	argv++;
	argc--;

	/* once the command is removed the remaining arguments conform
	 * to GNU standards and can be parsed with argp
	 */
	argp_parse(&argp, argc, (char **)argv, 0, 0, &cp_options);

	if (cp_options.read)
		flag = DAOS_PC_RO;
	else if (cp_options.write)
		flag = DAOS_PC_RW;
	else if (cp_options.exclusive)
		flag = DAOS_PC_EX;

	/* finish parsing the uuid */
	if (!cp_options.uuid || (uuid_parse(cp_options.uuid, uuid) < 0))
		return EINVAL;

	/* turn the list of pool service nodes into a rank list */
	rc = parse_rank_list(cp_options.server_list,
			     &pool_service_list);
	if (rc < 0)
		/* TODO do a better job with failure return */
		return rc;

	rc = daos_pool_connect(uuid, cp_options.server_group,
			       &pool_service_list,
			       flag, &poh, &info, NULL);

	if (rc) {
		printf("<<<daosctl>>> Pool connect fail, result: %d\n",
		       rc);
		return 1;
	}

	rc = daos_pool_query(poh, NULL, &info2, NULL, NULL);

	/* TODO not ready for this test yet */
	/* the info returned by connect should match query */
	/* if (memcmp(&info, &info2, sizeof(daos_pool_info_t))) {
	 *  printf("pool info mismatch\n");
	 *  return 1;
	 *  }
	 */

	uuid_unparse(info2.pi_uuid, uuid_str2);

	if (strcmp(cp_options.uuid, uuid_str2)) {
		printf("uuids don't match: %s %s\n",
		       cp_options.uuid, uuid_str2);
		return 1;
	}

	printf("<<<daosctl>>> Connected to pool.\n");
	return 0;
}

/**
 * A seemingly dumb command but it has its uses testing connections to
 * pools.  It creates, connects to and then destroys a pool.
 */
int
cmd_test_connect_pool(int argc, const char **argv, void *ctx)
{
	int               rc;
	d_rank_list_t     pool_service_list;
	uuid_t            uuid;
	/*d_rank_list_t     tgts;*/
	daos_handle_t     poh;
	unsigned int      flag = DAOS_PC_RO;
	daos_pool_info_t  info = {0};
	struct test_pool_options cp_options = {
		"daos_server", NULL, NULL, 0, 0, 0, 0, 0, 0,
		1024*1024*1024, 1, NULL};

	struct argp_option options[] = {
		{"server-group",    's',    "SERVER-GROUP",    0,
		 "ID of the server group that manages the pool"},
		{"uid",             'u',    "UID",             0,
		 "User ID that is to own the new pool"},
		{"gid",             'g',    "GID",             0,
		 "Group ID that is to own the new pool"},
		{"mode",            'm',    "mode",            0,
		 "Mode defines the operations allowed on the pool"},
		{"read",            'r',    0,                 0,
		 "Enable read access"},
		{"write",           'm',    0,                 0,
		 "Enable write access"},
		{"exclusive",       'e',    0,                 0,
		 "Enable exclusive access"},
		{"servers",       'l',   "server rank-list", 0,
		 "pool service ranks, comma separated, no spaces e.g. -l 1,2"},
		{"size",           'z',    "size",             0,
		 "Size of the pool in bytes or with k/m/g appended (e.g. 10g)"},
		{0}
	};
	struct argp argp = {options, parse_pool_test_args_cb};

	/* adjust the arguments to skip over the command */
	argv++;
	argc--;

	/* once the command is removed the remaining arguments
	 * conform to GNU standards and can be parsed with argp
	 */
	argp_parse(&argp, argc, (char **)argv, 0, 0, &cp_options);

	/* finish parsing connect type */
	/* TODO: not optimal parsing */
	if (cp_options.read)
		flag = DAOS_PC_RO;
	else if (cp_options.write)
		flag = DAOS_PC_RW;
	else if (cp_options.exclusive)
		flag = DAOS_PC_EX;

	/* turn the list of pool service nodes into a rank list */
	rc = parse_rank_list(cp_options.server_list,
			     &pool_service_list);
	if (rc < 0)
		/* TODO do a better job with failure return */
		return rc;

	rc = daos_pool_create(cp_options.mode, cp_options.uid, cp_options.gid,
			      cp_options.server_group, NULL, "rubbish",
			      cp_options.size, 0, NULL,
			      &pool_service_list, uuid, NULL);
	if (rc) {
		printf("<<<daosctl>>> Pool create fail, result: %d\n", rc);
	} else {
		char uuid_str[100];

		uuid_unparse(uuid, uuid_str);
		printf("%s", uuid_str);
	}

	if (rc) {
		printf("<<<daosctl>> Pool create fail, result: %d\n",
		       rc);
		exit(1);
	}

	char uuid_str[100];

	uuid_unparse(uuid, uuid_str);

	/* the create worked, so connect */
	rc = daos_pool_connect(uuid, cp_options.server_group,
			       &pool_service_list, flag, &poh, &info, NULL);

	if (rc) {
		printf("<<<daosctl>>> Pool connect fail, result: %d\n",
		       rc);
	} else {
		daos_pool_info_t pool_info = {0};

		rc = daos_pool_query(poh, NULL, &pool_info, NULL, NULL);

		/* TODO not ready for this test yet */
		/* the info returned by connect should match query */
		/* if (memcmp(&info, &pool_info, sizeof(daos_pool_info_t))) {
		 * printf("pool info returned by connected and query
		 * don't match\n");
		 * return 1;
		 *  }
		 */

		char uuid_str2[100];

		uuid_unparse(pool_info.pi_uuid, uuid_str2);

		if (strcmp(uuid_str, uuid_str2)) {
			printf("uuids don't match: %s %s\n", uuid_str,
			       uuid_str2);
			return 1;
		}
		/* TODO not specifying targets just yet */
		/* if (pool_info.pi_ntargets != tgts.rl_nr) {
		 * printf("tgt count doesn't match: %i %i\n",
		 * pool_info.pi_ntargets,
		 * tgts.rl_nr);
		 * return 1;
		 * }
		 */
		if (pool_info.pi_ndisabled != 0) {
			printf("badtgts should be zero: %i\n",
			       pool_info.pi_ndisabled);
			return 1;
		}
		/* seems to not be implemented yet */
		/* if (pool_info.pi_space.foo != pool_size) {
		 * printf("space is %i, shoud be: %i\n",
		 *	pool_info.pi_space.foo, pool_size);
		 *	return 1;
		 *	}
		 */
	}

	/* not really testing destroy, just cleaning up */
	rc = daos_pool_destroy(uuid, cp_options.server_group, 1, NULL);

	return 0;
}

/**
 * Seemingly stupid function but it lets you test pool creation without leaving
 * a bunch of pools you don't want laying around.
 */
int
cmd_test_create_pool(int argc, const char **argv, void *ctx)
{
	int rc = -ENXIO;
	uuid_t            uuid;
	d_rank_list_t     pool_service_list = {NULL, 0};

	struct test_pool_options cp_options = {
		"daos_server", NULL, NULL, 0, 0, 0, 0, 0, 0,
		1024*1024*1024, 1, NULL};
	struct argp_option options[] = {
		{"server-group",   's',   "SERVER-GROUP",    0,
		 "ID of the server group that is to manage the new pool"},
		{"uid",            'u',   "UID",             0,
		 "User ID that is to own the new pool"},
		{"gid",            'g',   "GID",             0,
		 "Group ID that is to own the new pool"},
		{"mode",           'm',   "mode",            0,
		 "Mode defines the operations allowed on the pool"},
		{"servers",       'l',   "server rank-list", 0,
		 "pool service ranks, comma separated, no spaces e.g. -l 1,2"},
		{"size",           'z',    "size",             0,
		 "Size of the pool in bytes or with k/m/g appended (e.g. 10g)"},
		{0}
	};
	struct argp argp = {options, parse_pool_test_args_cb};

	/* adjust the arguments to skip over the command */
	argv++;
	argc--;

	/* once the command is removed the remaining arguments
	 * conform to GNU standards and can be parsed with argp
	 */
	argp_parse(&argp, argc, (char **)argv, 0, 0, &cp_options);

	/* turn the list of pool service nodes into a rank list */
	rc = parse_rank_list(cp_options.server_list,
			     &pool_service_list);
	if (rc < 0)
		/* TODO do a better job with failure return */
		return rc;

	rc = daos_pool_create(cp_options.mode, cp_options.uid, cp_options.gid,
			      cp_options.server_group, NULL, "rubbish",
			      cp_options.size, 0, NULL, &pool_service_list,
			      uuid, NULL);
	if (rc) {
		printf("<<<daosctl>>> Pool create fail, result: %d\n", rc);
	} else {
		sleep(5);
		rc = daos_pool_destroy(uuid, cp_options.server_group, 1, NULL);
		if (rc)
			printf("<<<daosctl>>> Destroy failed with: %d\n", rc);
	}
	fflush(stdout);
	return rc;
}

/**
 * Seemingly stupid command but it can be used to test eviction.
 */
int
cmd_test_evict_pool(int argc, const char **argv, void *ctx)
{
	int               rc;
	uuid_t            uuid;
	d_rank_list_t     pool_service_list = {NULL, 0};

	/*d_rank_list_t     tgts;*/
	daos_handle_t     poh;
	unsigned int      flag = DAOS_PC_RO;
	daos_pool_info_t  info = {0};
	struct test_pool_options ep_options = {
		"daos_server", NULL, NULL, 0, 0, 0, 0, 0, 0,
		1024*1024*1024, 1, NULL};

	struct argp_option options[] = {
		{"server-group",   's',   "SERVER-GROUP",   0,
		 "ID of the server group that manages the pool"},
		{"uid",            'u',   "UID",            0,
		 "User ID that is to own the new pool"},
		{"gid",            'g',   "GID",            0,
		 "Group ID that is to own the new pool"},
		{"mode",           'm',   "MODE",           0,
		 "Mode defines the operations allowed on the pool"},
		{"replicas",       'c',   "replica-count",  0,
		 "number of service replicas"},
		{"read",           'r',   0,                0,
		 "Enable read access"},
		{"write",          'm',   0,                0,
		 "Enable write access"},
		{"exclusive",      'e',   0,                0,
		 "Enable exclusive access"},
		{"servers",        'l',   "server rank-list", 0,
		 "pool service ranks, comma separated, no spaces e.g. -l 1,2"},
		{"size",           'z',   "pool-size",      0,
		 "Size of the pool in bytes or with k/m/g appended (e.g. 10g)"},
		{0}
	};
	struct argp argp = {options, parse_pool_test_args_cb};

	/* adjust the arguments to skip over the command */
	argv++;
	argc--;

	/* once the command is removed the remaining arguments
	 * conform to GNU standards and can be parsed with argp
	 */
	argp_parse(&argp, argc, (char **)argv, 0, 0, &ep_options);

	/* finish parsing connect type */
	/* TODO: not optimal parsing */
	if (ep_options.read)
		flag = DAOS_PC_RO;
	else if (ep_options.write)
		flag = DAOS_PC_RW;
	else if (ep_options.exclusive)
		flag = DAOS_PC_EX;

	/* turn the list of pool service nodes into a rank list */
	rc = parse_rank_list(ep_options.server_list,
			     &pool_service_list);

	rc = daos_pool_create(ep_options.mode, ep_options.uid, ep_options.gid,
			      ep_options.server_group, NULL, "rubbish",
			      ep_options.size, 0, NULL, &pool_service_list,
			      uuid, NULL);
	if (rc) {
		printf("<<<daosctl>>> Pool create fail, result: %d\n", rc);
	} else {
		char uuid_str[100];

		uuid_unparse(uuid, uuid_str);
		printf("%s", uuid_str);
	}

	if (rc) {
		printf("<<<daosctl>> Pool create fail, result: %d\n",
		       rc);
		exit(1);
	}

	char uuid_str[100];

	uuid_unparse(uuid, uuid_str);

	/* the create worked, so connect */
	rc = daos_pool_connect(uuid, ep_options.server_group,
			       &pool_service_list, flag,
			       &poh, &info, NULL);

	if (rc) {
		printf("<<<daosctl>>> Pool connect fail, result: %d\n",
		       rc);
	} else {
		daos_pool_info_t pool_info = {0};

		rc = daos_pool_query(poh, NULL, &pool_info, NULL, NULL);

		/* TODO not ready for this test yet */
		/* the info returned by connect should match query */
		/* if (memcmp(&info, &pool_info, sizeof(daos_pool_info_t))) {
		 *  printf("pool info returned by connected
		 *  and query don't match\n");
		 *	  return 1;
		 *	  }
		 */
		if (rc)
			return 1;

		rc = daos_pool_evict(uuid, ep_options.server_group,
				     &pool_service_list, NULL);
		if (rc) {
			printf("<<<daosctl>>> Pool evict fail, result: %d\n",
			       rc);
			return 1;
		}

		/* connection should be invalid, test by running
		 * a query
		 */
		rc = daos_pool_query(poh, NULL, &pool_info, NULL, NULL);
		if (!rc) {
			printf("npool connection used successfully, "
			       "but it should be invalid.\n");
			return 1;
		}
	}

	/* not really testing destroy, just cleaning up */
	rc = daos_pool_destroy(uuid, ep_options.server_group, 1, NULL);

	return 0;
}

/**
 * This function is of no use other than to do some obscure injection testing
 * of the pool query function, particularly to pass it an arbitrary, bad
 * pool handle.
 */
int
cmd_test_query_pool(int argc, const char **argv, void *ctx)
{
	daos_handle_t poh;
	daos_pool_info_t pool_info = {0};
	int rc;
	struct argp_option options[] = {
		{"handle",   'h',   "INTERNAL-HANDLE",   0,
		 "test value for the pool handle, just rubbish really"},
		{0}
	};
	struct argp argp = {options, parse_pool_test_args_cb};
	struct test_pool_options qp_options = {
		"daos_server", NULL, NULL, 0, 0, 0, 0, 0, 0,
		1024*1024*1024, 1, NULL};

	/* adjust the arguments to skip over the command */
	argv++;
	argc--;

	/* once the command is removed the remaining arguments conform to
	 * GNU standards and can be parsed with argp
	 */
	argp_parse(&argp, argc, (char **)argv, 0, 0, &qp_options);

	/* a handle must be provided */
	if (qp_options.handle == NULL)
		return EINVAL;

	/* parse the handle internal data and stuff it in the
	 * expected structure
	 */
	poh.cookie = strtoull(qp_options.handle, NULL, 10);

	rc = daos_pool_query(poh, NULL, &pool_info, NULL, NULL);

	return rc;
}
