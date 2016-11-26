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
 * dmg(8): DAOS Management Utility
 */

#include <getopt.h>
#include <stdio.h>
#include <daos.h>
#include <daos/common.h>

static const unsigned int	default_mode = 0731;
static const daos_size_t	default_size = 256 << 20;
static const char * const	default_group = "daos_server_group";

typedef int (*command_hdlr_t)(int, char *[]);

static int
create_hdlr(int argc, char *argv[])
{
	struct option		options[] = {
		{"gid",		required_argument,	NULL,	'g'},
		{"group",	required_argument,	NULL,	'G'},
		{"mode",	required_argument,	NULL,	'm'},
		{"size",	required_argument,	NULL,	's'},
		{"uid",		required_argument,	NULL,	'u'},
		{NULL,		0,			NULL,	0}
	};
	unsigned int		mode = default_mode;
	unsigned int		uid = geteuid();
	unsigned int		gid = getegid();
	daos_size_t		size = default_size;
	const char	       *group = default_group;
	daos_rank_t		ranks[13];
	daos_rank_list_t	svc;
	uuid_t			pool_uuid;
	int			rc;

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'g':
			gid = atoi(optarg);
			break;
		case 'G':
			group = optarg;
			break;
		case 'm':
			mode = strtoul(optarg, NULL /* endptr */, 0 /* base */);
			break;
		case 's':
			size = strtoul(optarg, NULL /* endptr */, 0 /* base */);
			break;
		case 'u':
			gid = atoi(optarg);
			break;
		default:
			return 2;
		}
	}

	svc.rl_nr.num = ARRAY_SIZE(ranks);
	svc.rl_nr.num_out = 0;
	svc.rl_ranks = ranks;

	rc = daos_pool_create(mode, uid, gid, group, NULL /* tgts */, "pmem",
			      size, &svc, pool_uuid, NULL /* ev */);
	if (rc != 0) {
		D_ERROR("failed to create pool: %d\n", rc);
		return rc;
	}

	printf(DF_UUIDF"\n", DP_UUID(pool_uuid));

	return 0;
}

static int
destroy_hdlr(int argc, char *argv[])
{
	struct option		options[] = {
		{"force",	no_argument,		NULL,	'f'},
		{"group",	required_argument,	NULL,	'G'},
		{"pool",	required_argument,	NULL,	'p'},
		{NULL,		0,			NULL,	0}
	};
	const char	       *group = default_group;
	uuid_t			pool_uuid;
	int			force = 0;
	int			rc;

	uuid_clear(pool_uuid);

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'f':
			force = 1;
			break;
		case 'G':
			group = optarg;
			break;
		case 'p':
			if (uuid_parse(optarg, pool_uuid) != 0) {
				D_ERROR("failed to parse pool UUID: %s\n",
					optarg);
				return 2;
			}
			break;
		default:
			return 2;
		}
	}

	if (uuid_is_null(pool_uuid)) {
		D_ERROR("pool UUID required\n");
		return 2;
	}

	rc = daos_pool_destroy(pool_uuid, group, force, NULL /* ev */);
	if (rc != 0) {
		D_ERROR("failed to destroy pool: %d\n", rc);
		return rc;
	}

	return 0;
}

static int
evict_hdlr(int argc, char *argv[])
{
	struct option		options[] = {
		{"group",	required_argument,	NULL,	'G'},
		{"pool",	required_argument,	NULL,	'p'},
		{NULL,		0,			NULL,	0}
	};
	const char	       *group = default_group;
	uuid_t			pool_uuid;
	int			rc;

	uuid_clear(pool_uuid);

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'G':
			group = optarg;
			break;
		case 'p':
			if (uuid_parse(optarg, pool_uuid) != 0) {
				D_ERROR("failed to parse pool UUID: %s\n",
					optarg);
				return 2;
			}
			break;
		default:
			return 2;
		}
	}

	if (uuid_is_null(pool_uuid)) {
		D_ERROR("pool UUID required\n");
		return 2;
	}

	rc = daos_pool_evict(pool_uuid, group, NULL /* ev */);
	if (rc != 0)
		D_ERROR("failed to evict pool connections: %d\n", rc);

	return rc;
}

static int
exclude_hdlr(int argc, char *argv[])
{
	struct option		options[] = {
		{"group",	required_argument,	NULL,	'G'},
		{"pool",	required_argument,	NULL,	'p'},
		{"target",	required_argument,	NULL,	't'},
		{NULL,		0,			NULL,	0}
	};
	const char	       *group = default_group;
	uuid_t			pool_uuid;
	daos_rank_t		target = -1;
	daos_handle_t		pool;
	daos_rank_list_t	targets;
	int			rc;

	uuid_clear(pool_uuid);

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'G':
			group = optarg;
			break;
		case 't':
			target = atoi(optarg);
			break;
		case 'p':
			if (uuid_parse(optarg, pool_uuid) != 0) {
				D_ERROR("failed to parse pool UUID: %s\n",
					optarg);
				return 2;
			}
			break;
		default:
			return 2;
		}
	}

	if (uuid_is_null(pool_uuid)) {
		D_ERROR("pool UUID required\n");
		return 2;
	}

	if (target == -1) {
		D_ERROR("valid target rank required\n");
		return 2;
	}

	rc = daos_pool_connect(pool_uuid, group, NULL /* svc */, DAOS_PC_RW,
			       &pool, NULL /* info */, NULL /* ev */);
	if (rc != 0) {
		D_ERROR("failed to connect to pool: %d\n", rc);
		return rc;
	}

	targets.rl_nr.num = 1;
	targets.rl_nr.num_out = 0;
	targets.rl_ranks = &target;

	rc = daos_pool_exclude(pool, &targets, NULL /* ev */);
	if (rc != 0)
		D_ERROR("failed to exclude target: %d\n", rc);

	rc = daos_pool_disconnect(pool, NULL /* ev */);
	if (rc != 0) {
		D_ERROR("failed to disconnect from pool: %d\n", rc);
		return rc;
	}

	return 0;
}

static int
kill_hdlr(int argc, char *argv[])
{
	struct option		options[] = {
		{"group",	required_argument,	NULL,	'G'},
		{"force",	0,			NULL,	'f'},
		{"rank",	required_argument,	NULL,	'r'},
		{NULL,		0,			NULL,	0}
	};
	const char	       *group = default_group;
	bool			force = false;
	daos_rank_t		rank = -1;
	int			rc;

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'G':
			group = optarg;
			break;
		case 'f':
			force = true;
			break;
		case 'r':
			rank = atoi(optarg);
			break;
		default:
			return 2;
		}
	}


	if (rank < 0) {
		D_ERROR("valid target rank required\n");
		return 2;
	}

	rc = daos_mgmt_svc_rip(group, rank, force, NULL);
	if (rc != 0) {
		D_ERROR("failed to kill rank: %d\n", rank);
		return rc;
	}

	return 0;
}

static int
help_hdlr(int argc, char *argv[])
{
	printf("\
usage: dmg COMMAND [OPTIONS]\n\
commands:\n\
  create	create a pool\n\
  destroy	destroy a pool\n\
  evict		evict all pool connections to a pool\n\
  exclude	exclude a target from a pool\n\
  kill		kill remote daos server\n\
  help		print this message and exit\n");
	printf("\
create options:\n\
  --gid=GID	pool GID (getegid()) \n\
  --group=STR	pool server process group (\"%s\")\n\
  --mode=MODE	pool mode (%#o)\n\
  --size=BYTES	target size in bytes ("DF_U64")\n\
  --uid=UID	pool UID (geteuid())\n", default_group, default_mode,
	       default_size);
	printf("\
destroy options:\n\
  --force	destroy the pool even if there are connections\n\
  --group=STR	pool server process group (\"%s\")\n\
  --pool=UUID	pool UUID\n", default_group);
	printf("\
evict options:\n\
  --group=STR	pool server process group (\"%s\")\n\
  --pool=UUID	pool UUID\n", default_group);
	printf("\
exclude options:\n\
  --group=STR	pool server process group (\"%s\")\n\
  --pool=UUID	pool UUID\n\
  --target=RANK	target rank\n", default_group);
	printf("\
kill options:\n\
  --group=STR	pool server process group (\"%s\")\n\
  --force	unclean shutdown\n\
  --rank=INT	rank of the DAOS server to kill\n", default_group);
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
		hdlr = create_hdlr;
	else if (strcmp(argv[1], "destroy") == 0)
		hdlr = destroy_hdlr;
	else if (strcmp(argv[1], "evict") == 0)
		hdlr = evict_hdlr;
	else if (strcmp(argv[1], "exclude") == 0)
		hdlr = exclude_hdlr;
	else if (strcmp(argv[1], "kill") == 0)
		hdlr = kill_hdlr;

	if (hdlr == NULL || hdlr == help_hdlr) {
		help_hdlr(argc, argv);
		return hdlr == NULL ? 2 : 0;
	}

	rc = daos_init();
	if (rc != 0) {
		D_ERROR("failed to initialize daos: %d\n", rc);
		return 1;
	}

	rc = hdlr(argc, argv);

	daos_fini();

	if (rc < 0) {
		return 1;
	} else if (rc > 0) {
		help_hdlr(argc, argv);
		return 2;
	}

	return 0;
}
