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
 * This file is part of vos
 * src/vos/tests/vos_tests.c
 * Launcher for all tests
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <vts_common.h>
#include <cmocka.h>
#include <getopt.h>
#include <daos_srv/vos.h>

static void
print_usage()
{
	print_message("Use one of these opt(s) for specific test\n");
	print_message("vos_tests -p|--pool_tests\n");
	print_message("vos_tests -c|--container_tests\n");
	print_message("vos_tests -i|--io_tests\n");
	print_message("vos_tests -a|--all_tests\n");
	print_message("vos_tests -h|--help\n");
	print_message("Default <vos_tests> runs all tests\n");
}

static inline int
run_all_tests()
{
	int failed = 0;

	failed += run_pool_test();
	failed += run_co_test();
	failed += run_io_test();

	return failed;
}

int
main(int argc, char **argv)
{
	int		rc = 0;
	int		nr_failed = 0;
	int		opt = 0, index = 0;

	static struct option long_options[] = {
		{"all_tests",	no_argument, 0, 'a'},
		{"pool_tests",	no_argument, 0, 'p'},
		{"container_tests", no_argument, 0, 'c'},
		{"io_tests", no_argument, 0, 'i'},
		{"help", no_argument, 0, 'h'},
	};

	rc = vos_init();
	if (rc) {
		print_error("Error initializing VOS instance\n");
		return rc;
	}

	gc = 0;
	if (argc < 2) {
		nr_failed = run_all_tests();
	} else {
		while ((opt = getopt_long(argc, argv, "apctih",
				  long_options, &index)) != -1) {
			switch (opt) {
			case 'p':
				nr_failed += run_pool_test();
				break;
			case 'c':
				nr_failed += run_co_test();
				break;
			case 'i':
				nr_failed += run_io_test();
				break;
			case 'h':
				print_usage();
				goto exit;
			case 'a':
				nr_failed = run_all_tests();
				break;
			default:
				print_error("Unkown option\n");
				print_usage();
				goto exit;
			}
		}
	}

	if (nr_failed)
		print_error("ERROR, %i TEST(S) FAILED\n", nr_failed);
	else
		print_message("\nSUCCESS! NO TEST FAILURES\n");

exit:
	vos_fini();
	return rc;
}


