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
 * dcont(8): DAOS Container Management Utility
 */
#include <getopt.h>
#include <stdio.h>
#include <daos.h>
#include <daos/common.h>

const char		*default_group;
typedef int (*command_hdlr_t)(int, char *[]);

enum cont_op {
	CONT_CREATE,
	CONT_DESTROY,
	CONT_QUERY
};

static enum cont_op
cont_op_parse(const char *str)
{
	if (strcmp(str, "create") == 0)
		return CONT_CREATE;
	else if (strcmp(str, "destroy") == 0)
		return CONT_DESTROY;
	else if (strcmp(str, "query") == 0)
		return CONT_QUERY;
	assert(0);
	return -1;
}


static int
cont_op_hdlr(int argc, char *argv[])
{
	struct option		options[] = {
		{"group",	required_argument,	NULL, 'G'},
		{"pool",	required_argument,	NULL, 'p'},
		{"svc",		required_argument,	NULL, 'v'},
		{"cont",	required_argument,	NULL, 'c'},
		{NULL,		0,			NULL,  0}
	};
	const char		*group = default_group;
	uuid_t			pool_uuid;
	uuid_t			cont_uuid;
	daos_handle_t		pool;
	daos_handle_t		coh;
	const char		*svc_str = NULL;
	d_rank_list_t	*svc;
	daos_cont_info_t	cont_info;
	enum cont_op		op = cont_op_parse(argv[1]);
	int			rc;

	uuid_clear(pool_uuid);
	uuid_clear(cont_uuid);

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'G':
			group = optarg;
			break;
		case 'p':
			if (uuid_parse(optarg, pool_uuid) != 0) {
				fprintf(stderr,
					"failed to parse pool UUID: %s\n",
					optarg);
				return 2;
			}
			break;
		case 'v':
			svc_str = optarg;
			break;
		case 'c':
			if (uuid_parse(optarg, cont_uuid) != 0) {
				fprintf(stderr,
					"failed to parse cont UUID: %s\n",
					optarg);
				return 2;
			}
			break;
		default:
			printf("unknown option : %d\n", rc);
			return 2;
		}
	}

	/* Check the pool UUID. */
	if (uuid_is_null(pool_uuid)) {
		fprintf(stderr, "pool UUID required\n");
		return 2;
	}
	/* Check the pool service ranks. */
	if (svc_str == NULL) {
		fprintf(stderr, "--svc must be specified\n");
		return 2;
	}

	svc = daos_rank_list_parse(svc_str, ":");
	if (svc == NULL) {
		fprintf(stderr, "failed to parse service ranks\n");
		return 2;
	}

	if (svc->rl_nr == 0) {
		fprintf(stderr, "--svc mustn't be empty\n");
		daos_rank_list_free(svc);
		return 2;
	}

	if (uuid_is_null(cont_uuid)) {
		fprintf(stderr, "valid cont uuid required\n");
		daos_rank_list_free(svc);
		return 2;
	}

	/*
	 * all cont operations require a pool handle, lets make a =
	 * pool connection
	 */
	rc = daos_pool_connect(pool_uuid, group, svc, DAOS_PC_RW, &pool,
			       NULL /* info */, NULL /* ev */);
	daos_rank_list_free(svc);
	if (rc != 0) {
		fprintf(stderr, "failed to connect to pool: %d\n", rc);
		return rc;
	}

	if (op == CONT_CREATE) {
		rc = daos_cont_create(pool, cont_uuid, NULL, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to create container: %d\n", rc);
			return rc;
		}
		fprintf(stdout, "Successfully created container "DF_UUIDF"\n",
			DP_UUID(cont_uuid));
	}

	if (op != CONT_DESTROY) {
		rc = daos_cont_open(pool, cont_uuid, DAOS_COO_RW, &coh,
				    &cont_info, NULL);
		if (rc != 0) {
			fprintf(stderr,"cont open failed: %d\n", rc);
			return rc;
		}
	}


	if (op != CONT_DESTROY) {
		rc = daos_cont_close(coh, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to close container: %d\n", rc);
			return rc;
		}
	}

	if (op == CONT_DESTROY) {
		rc = daos_cont_destroy(pool, cont_uuid, 1, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to destroy container: %d\n",
				rc);
			return rc;
		}
		fprintf(stdout, "Successfully destroyed container "DF_UUIDF"\n",
			DP_UUID(cont_uuid));
	}

	rc = daos_pool_disconnect(pool, NULL);
	if (rc != 0) {
		fprintf(stderr, "Pool disconnect failed : %d\n", rc);
		return rc;
	}
	return 0;
}


static int
help_hdlr(int argc, char *argv[])
{
	printf("\
usage: dcont COMMAND [OPTIONS]\n\
commands:\n\
	create        create a container\n\
	destroy       destroy a conainer\n\
	query         query a container\n\
	help          print this message and exit\n");

	printf("\
create options:\n\
	--pool=UUID    pool UUID \n\
	--cont=UUID    cont UUID \n\
	--group=STR    pool server process group (\"%s\")\n\
	--svc=RANKS    pool service replicas like 1:2:3\n",
	default_group);

	printf("\
destroy options:\n\
	--pool=UUID   pool UUID\n\
	--group=STR   pool server process group (\"%s\")\n\
	--svc=RANKS   pool service replicas like 1:2:3\n\
	--cont=UUID   container UUID\n", default_group);

	printf("\
query options:\n\
	--pool=UUID   pool UUID\n\
	--group=STR   pool server process group (\"%s\")\n\
	--svc=RANKS   pool service replicas like 1:2:3\n\
	--cont=UUID   cont UUID\n", default_group);
	return 0;
}

int
main(int argc, char *argv[])
{
	command_hdlr_t		hdlr = NULL;
	int			rc = 0;

	if (argc == 1 || strcmp(argv[1], "help") == 0)
		hdlr = help_hdlr;
	else if (strcmp(argv[1], "create") == 0)
		hdlr = cont_op_hdlr;
	else if (strcmp(argv[1], "destroy") == 0)
		hdlr = cont_op_hdlr;
	else if (strcmp(argv[1], "query") == 0)
		hdlr = cont_op_hdlr;

	if (hdlr == NULL || hdlr == help_hdlr) {
		help_hdlr(argc, argv);
		return hdlr == NULL ? 2 : 0;
	}

	rc = daos_init();
	if (rc != 0) {
		fprintf(stderr, "failed to initialize daos: %d\n", rc);
		return 1;
	}

	rc = hdlr(argc, argv);
	daos_fini();

	if (rc < 0) {
		return 1;
	} else if (rc > 0) {
		printf("rc: %d\n", rc);
		help_hdlr(argc, argv);
		return 2;
	}

	return 0;
}
