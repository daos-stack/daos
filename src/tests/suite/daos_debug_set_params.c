/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * tests/suite/daos_debug_set_params
 */

#include <getopt.h>
#include <sys/types.h>
#include <fcntl.h>
#include "daos_test.h"
#include <daos_fs.h>
#include <daos_fs_sys.h>

static void
print_usage()
{
	print_message("\n\ndaos_debug_set_params\n=============================\n");
	print_message("--server_group|-s specify server group\n");
	print_message("--rank|-r Ranks to set parameter. -1 means all\n");
	print_message("--key_id|-k Key ID to set\n");
	print_message("--value|-v value to set\n");
	print_message("--value_extra|-V optional extra value to set the fail value\n");
	print_message("when a key_id is DMG_CMD_FAIL_LOC and a value is in DAOS_FAIL_VALUE mode\n");
	print_message("--help|-h\n");
	print_message("\n=============================\n");
}

int main(int argc, char **argv)
{
	int		 opt = 0, index = 0;
	int		 rc, rc1 = 0;
	int rank = -1;
	unsigned long key_id = DMG_KEY_FAIL_LOC;
	unsigned long value = 0;
	unsigned long extra_value = 0;
	char *group = NULL;

	d_register_alt_assert(mock_assert);

	static struct option long_options[] = {
		{"server_group", required_argument,	NULL,	's'},
		{"rank",	required_argument,	NULL,	'r'},
		{"key_id",	required_argument,	NULL,	'k'},
		{"value",	required_argument,	NULL,	'v'},
		{"value_extra",	required_argument,	NULL,	'V'},
		{"help",	no_argument,		NULL,	'h'},
		{NULL,		0,			NULL,	0}
	};

	rc = daos_init();
	if (rc) {
		print_message("daos_init() failed with %d\n", rc);
		return -1;
	}

	while ((opt = getopt_long(argc, argv, "r:k:v:V:h",
				  long_options, &index)) != -1) {
		char *endp;

		switch (opt) {
		case 's':
			group = optarg;
		case 'r':
			rank = atoi(optarg);
			break;
		case 'k':
			key_id = strtoul(optarg, &endp, 0);
			if (endp && *endp != '\0') {
				print_message("invalid numeric key_id: %s\n",
					      optarg);
				rc1 = -1;
				goto exit;
			}
			break;
		case 'v':
			value = strtoul(optarg, &endp, 0);
			if (endp && *endp != '\0') {
				print_message("invalid numeric value: %s\n",
					      optarg);
				rc1 = -1;
				goto exit;
			}
			break;
		case 'V':
			extra_value = strtoul(optarg, &endp, 0);
			if (endp && *endp != '\0') {
				print_message("invalid numeric extra value: %s\n",
					      optarg);
				rc1 = -1;
				goto exit;
			}
			break;
		case 'h':
			print_usage();
			goto exit;
		default:
			print_usage();
			rc = -1;
			goto exit;
		}
	}

	rc1 = daos_debug_set_params(group, rank, key_id, value,
				    extra_value, NULL);
	if (rc1)
		print_message("fail to set params: %d\n", rc1);
exit:
	rc = daos_fini();
	if (rc)
		print_message("daos_fini() failed with %d\n", rc);
	else if (rc1)
		rc = rc1;

	return rc;
}
