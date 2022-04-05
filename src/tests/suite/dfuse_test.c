/**
 * (C) Copyright 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * tests/suite/dfuse_test
 */

#include <stdio.h>
#include <getopt.h>

static void
print_usage()
{
	printf("DFuse tests\n");
	printf("dfuse_test -m <path to test>\n");
	printf("dfute_test --open-at\n");
}

void
do_openat(char *test_dir)
{
	printf("Hello\n");
}

typedef void(dfuse_test)(char *);

int
main(int argc, char **argv)
{
	int           index = 0;
	int           opt;
	char         *test_dir       = NULL;
	dfuse_test   *test           = NULL;
	struct option long_options[] = {{"test-dir", required_argument, NULL, 'M'},
					{"open-at", no_argument, NULL, 'o'},
					{NULL, 0, NULL, 0} };

	while ((opt = getopt_long(argc, argv, "M:o", long_options, &index)) != -1) {
		switch (opt) {
		case 'o':
			test = do_openat;
			break;
		case 'M':
			test_dir = optarg;
			break;
		default:
			printf("Unknown Option\n");
			print_usage();
			return 1;
		}
	}

	if (test_dir == NULL) {
		printf("--test-dir option required\n");
		return 1;
	}

	/* Check that test_dir exists, and is a directory */

	if (test == NULL) {
		printf("No test specified\n");
		return 1;
	}
	test(test_dir);
	return 0;
}
