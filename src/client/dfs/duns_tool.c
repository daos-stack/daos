/**
 * (C) Copyright 2019 Intel Corporation.
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
 * duns(8): DUNS Utility
 */

#define D_LOGFAC	DD_FAC(duns)

#include <dirent.h>
#include <sys/stat.h>
#include <getopt.h>
#include <daos/common.h>
#include "daos_types.h"
#include "daos_api.h"
#include "daos_uns.h"

typedef int (*command_hdlr_t)(int, char *[]);

static int
link_hdlr(int argc, char *argv[])
{
	struct option		options[] = {
		{"path",	required_argument,	NULL,	'P'},
		{"pool",	required_argument,	NULL,	'p'},
		{"type",	required_argument,	NULL,	't'},
		{"oclass",	required_argument,	NULL,	'o'},
		{NULL,		0,			NULL,	0}
	};
	const char		*path = NULL;
	struct duns_attr_t	attr = {0};
	int			rc;

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'p':
			rc = uuid_parse(optarg, attr.da_puuid);
			if (rc) {
				D_ERROR("Pool UUID cannot be parsed\n");
				return -DER_INVAL;
			}
			break;
		case 't':
			daos_parse_ctype(optarg, &attr.da_type);
			break;
		case 'P':
			path = optarg;
			break;
		case 'o':
			daos_parse_oclass(optarg, &attr.da_oclass);
			break;
		default:
			return 2;
		}
	}


	rc = duns_link_path(path, attr);
	if (rc)
		fprintf(stderr, "Failed to link path %s\n", path);
	return rc;
}

static void
print_oclass(daos_oclass_id_t objectClass)
{
	switch (objectClass) {
	case DAOS_OC_TINY_RW:
		printf("tiny\n");
		break;
	case DAOS_OC_SMALL_RW:
		printf("small\n");
		break;
	case DAOS_OC_LARGE_RW:
		printf("large\n");
		break;
	case DAOS_OC_R2_RW:
		printf("R2\n");
		break;
	case DAOS_OC_R2S_RW:
		printf("R2S\n");
		break;
	case DAOS_OC_REPL_MAX_RW:
		printf("repl_max\n");
		break;
	default:
		printf("unknown\n");
		break;
	}
}

static void
print_ctype(daos_cont_layout_t type)
{
	switch (type) {
	case DAOS_PROP_CO_LAYOUT_POSIX:
		printf("POSIX\n");
		break;
	case DAOS_PROP_CO_LAYOUT_HDF5:
		printf("HDF5\n");
		break;
	default:
		printf("unknown\n");
		break;
	}
}

static int
resolve_hdlr(int argc, char *argv[])
{
	struct option		options[] = {
		{"path",	required_argument,	NULL,	'P'},
		{NULL,		0,			NULL,	0}
	};
	const char		*path = NULL;
	struct duns_attr_t	attr;
	int			rc;

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'P':
			path = optarg;
			break;
		default:
			return 2;
		}
	}

	rc = duns_resolve_path(path, &attr);
	if (rc)
		fprintf(stderr, "Failed to resolve path %s\n", path);

	printf("DAOS Unified Namespace Attributes on path %s:\n", path);
	printf("Container Type:\t");
	print_ctype(attr.da_type);
	printf("Pool UUID:\t"DF_UUIDF"\n", DP_UUID(attr.da_puuid));
	printf("Container UUID:\t"DF_UUIDF"\n", DP_UUID(attr.da_cuuid));
	printf("Object Class:\t");
	print_oclass(attr.da_oclass);

	return rc;
}

static int
help_hdlr(int argc, char *argv[])
{
	printf("\
usage: duns COMMAND [OPTIONS]\n\
commands:\n\
	link_path	create a container and link it with the path provided\n\
	unlink_path	unlink path and destroy associated DAOS container\n \
	resolve_path	view attributes on the path (pool, container, etc.)\n\
	help		print this message and exit\n");
	printf("\
link_path options:\n\
	--path=STR	path name\n\
	--pool=UUID	pool UUID to connect to\n\
	--oclass=STR	object class (tiny, small, large, R2S, R2, repl_max)\n\
	--type=STR	container type to create (POSIX, HDF5)\n");
	printf("\
resolve_path options:\n\
	--path=STR	path name\n");
	printf("\
unlink_path options:\n\
	--path=STR	path name\n");
	return 0;
}

int
main(int argc, char *argv[])
{
	command_hdlr_t		hdlr = NULL;
	int			rc = 0;

	if (argc == 1 || strcmp(argv[1], "help") == 0)
		hdlr = help_hdlr;
	else if (strcmp(argv[1], "link_path") == 0)
		hdlr = link_hdlr;
	else if (strcmp(argv[1], "resolve_path") == 0)
		hdlr = resolve_hdlr;

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
		help_hdlr(argc, argv);
		return 2;
	}

	return 0;
}
