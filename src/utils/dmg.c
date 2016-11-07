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

typedef int (*command_hdlr_t)(int, char *[]);

static int
create_hdlr(int argc, char *argv[])
{
	struct option		options[] = {
		{"gid",		1,	NULL,	'g'},
		{"group",	1,	NULL,	'G'},
		{"mode",	1,	NULL,	'm'},
		{"size",	1,	NULL,	's'},
		{"uid",		1,	NULL,	'u'}
	};
	unsigned int		mode = 0731;
	unsigned int		uid = geteuid();
	unsigned int		gid = getegid();
	crt_size_t		size = 256 << 20;
	char		       *group = "daos_server_group";
	crt_rank_t		ranks[13];
	crt_rank_list_t	svc;
	uuid_t			uuid;
	char			uuid_string[37];
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
			      size, &svc, uuid, NULL /* ev */);
	if (rc != 0) {
		D_ERROR("failed to create pool: %d\n", rc);
		return rc;
	}

	uuid_unparse_lower(uuid, uuid_string);
	printf("%s\n", uuid_string);

	return 0;
}

static int
destroy_hdlr(int argc, char *argv[])
{
	struct option		options[] = {
		{"force",	0,	NULL,	'f'},
		{"group",	1,	NULL,	'G'},
		{"uuid",	1,	NULL,	'U'}
	};
	char		       *group = "daos_server_group";
	uuid_t			uuid;
	int			force = 0;
	int			rc;

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'f':
			force = 1;
			break;
		case 'G':
			group = optarg;
			break;
		case 'U':
			if (uuid_parse(optarg, uuid) != 0) {
				D_ERROR("failed to parse pool UUID: %s\n",
					optarg);
				return 2;
			}
			break;
		default:
			return 2;
		}
	}

	rc = daos_pool_destroy(uuid, group, force, NULL /* ev */);
	if (rc != 0) {
		D_ERROR("failed to destroy pool: %d\n", rc);
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
  help		print this message and exit\n\
create options:\n\
  --gid=GID	pool GID\n\
  --group=STR	pool server process group\n\
  --mode=MODE	pool mode\n\
  --size=BYTES	target size in bytes\n\
  --uid=UID	pool UID\n\
  --uuid=UUID	pool UUID\n\
destroy options:\n\
  --force	destroy the pool even if there are connections\n\
  --group=STR	pool server process group\n\
  --uuid=UUID	pool UUID\n\
  \n");
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
