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

/* TODO: Why isn't this set on the command line. */
#define _GNU_SOURCE

#ifndef O_PATH
#define O_PATH 0
#endif

#include <stdio.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static void
print_usage()
{
	printf("DFuse tests\n");
	printf("dfuse_test -m <path to test>\n");
	printf("dfute_test --open-at\n");
}

int
do_openat(char *test_dir)
{
	int fd;
	int root;
	int rc;

	root = open(test_dir, O_PATH | O_DIRECTORY);
	if (root < 0)
		return root;

	fd = openat(root, "my_file", O_RDWR | O_CREAT);
	printf("Hello\n");

	rc = write(fd, "hello", 5);
	if (rc < 0) {
		printf("Write failed %d %d\n", rc, errno);
		return rc;
	}

	rc = close(fd);
	if (rc != 0) {
		printf("close fd failed %d %d\n", rc, errno);
		return rc;
	}

	rc = close(root);
	if (rc != 0) {
		printf("close root failed %d %d\n", rc, errno);
		return rc;
	}

	return 0;
}

typedef int(dfuse_test)(char *);

int
main(int argc, char **argv)
{
	int           index = 0;
	int           opt;
	int           rc;
	char         *test_dir       = NULL;
	dfuse_test   *test           = NULL;
	struct option long_options[] = {{"test-dir", required_argument, NULL, 'M'},
					{"open-at", no_argument, NULL, 'o'},
					{NULL, 0, NULL, 0}};

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

	rc = test(test_dir);
	return rc;
}
