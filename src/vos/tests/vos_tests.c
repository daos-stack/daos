/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(tests)

#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "vts_common.h"
#include <cmocka.h>
#include <getopt.h>
#include <daos_srv/vos.h>
#include <vos_internal.h>

static void
print_usage()
{
	print_message("Use one of these opt(s) for specific test\n");
	print_message("vos_tests -p|--pool_tests\n");
	print_message("vos_tests -c|--container_tests\n");
	print_message("vos_tests -i|--io_tests <ofeat>\n");
	print_message("ofeats = DAOS_OF_DKEY_UINT64, DAOS_OF_DKEY_LEXICAL\n");
	print_message("%8s DAOS_OF_AKEY_UINT64, DAOS_OF_AKEY_LEXICAL\n", " ");
	print_message("vos_tests -d |--discard-tests\n");
	print_message("vos_tests -a |--aggregate-tests\n");
	print_message("vos_tests -X|--dtx_tests\n");
	print_message("vos_tests -l|--incarnation-log-tests\n");
	print_message("vos_tests -A|--all_tests\n");
	print_message("vos_tests -f|--filter <filter>\n");
	print_message("vos_tests -e|--exclude <filter>\n");
	print_message("vos_tests -h|--help\n");
	print_message("Default <vos_tests> runs all tests\n");
}

static int dkey_feats[] = {
	0,	/* regular opaque key */
	DAOS_OF_DKEY_UINT64,
	DAOS_OF_DKEY_LEXICAL,
	-1,
};

static int akey_feats[] = {
	0,	/* regular opaque key */
	DAOS_OF_AKEY_UINT64,
	DAOS_OF_AKEY_LEXICAL,
	-1,
};

static inline int
run_all_tests(int keys, bool nest_iterators)
{
	int	failed = 0;
	int	feats;
	int	i;
	int	j;

	failed += run_pool_test();
	failed += run_co_test();
	for (i = 0; dkey_feats[i] >= 0; i++) {
		for (j = 0; akey_feats[j] >= 0; j++) {
			feats = dkey_feats[i] | akey_feats[j];
			failed += run_io_test(feats, keys, nest_iterators);
		}
	}
	failed += run_discard_tests();
	failed += run_aggregate_tests(false);
	failed += run_gc_tests();
	failed += run_dtx_tests();
	failed += run_ilog_tests();
	return failed;
}

int
main(int argc, char **argv)
{
	int	rc = 0;
	int	nr_failed = 0;
	int	opt = 0;
	int	index = 0;
	int	ofeats;
	int	keys;
	bool	nest_iterators = false;

	static struct option long_options[] = {
		{"all_tests",		required_argument, 0, 'A'},
		{"pool_tests",		no_argument, 0, 'p'},
		{"container_tests",	no_argument, 0, 'c'},
		{"io_tests",		required_argument, 0, 'i'},
		{"discard_tests",	no_argument, 0, 'd'},
		{"nest_iterators",	no_argument, 0, 'n'},
		{"aggregate_tests",	no_argument, 0, 'a'},
		{"dtx_tests",		no_argument, 0, 'X'},
		{"garbage_collector",	no_argument, 0, 'g'},
		{"ilog_tests",		no_argument, 0, 'l'},
		{"help",		no_argument, 0, 'h'},
		{"filter",		required_argument, 0, 'f'},
		{"exclude",		required_argument, 0, 'e'},
	};

	rc = daos_debug_init(NULL);
	if (rc) {
		print_error("Error initializing debug system\n");
		return rc;
	}

	rc = vos_init();
	if (rc) {
		print_error("Error initializing VOS instance\n");
		goto exit_0;
	}

	gc = 0;
	bool test_run = false;

	while ((opt = getopt_long(argc, argv, "apcdglnti:XA:hf:e:",
				  long_options, &index)) != -1) {
		switch (opt) {
		case 'e':
#if CMOCKA_FILTER_SUPPORTED == 1 /** requires cmocka 1.1.5 */
			cmocka_set_skip_filter(optarg);
#else
			D_PRINT("filter not enabled");
#endif

			break;
		case 'f':
#if CMOCKA_FILTER_SUPPORTED == 1 /** requires cmocka 1.1.5 */
			cmocka_set_test_filter(optarg);
				printf("Test filter: %s\n", optarg);
#else
			D_PRINT("filter not enabled");
#endif
			break;
		default:
			break;
		}
	}
	index = 0;
	optind = 0;

	while ((opt = getopt_long(argc, argv, "apcdlnti:A:hf:e:",
				  long_options, &index)) != -1) {
		switch (opt) {
		case 'p':
			nr_failed += run_pool_test();
			test_run = true;
			break;
		case 'c':
			nr_failed += run_co_test();
			test_run = true;
			break;
		case 'n':
			nest_iterators = true;
			break;
		case 'i':
			ofeats = strtol(optarg, NULL, 16);
			nr_failed += run_io_test(ofeats, 0,
						 nest_iterators);
			test_run = true;
			break;
		case 'a':
			nr_failed += run_aggregate_tests(true);
			test_run = true;
			break;
		case 'd':
			nr_failed += run_discard_tests();
			test_run = true;
			break;
		case 'g':
			nr_failed += run_gc_tests();
			break;
		case 'X':
			nr_failed += run_dtx_tests();
			test_run = true;
			break;
		case 'A':
			keys = atoi(optarg);
			nr_failed = run_all_tests(keys, nest_iterators);
			test_run = true;
			break;
		case 'l':
			nr_failed += run_ilog_tests();
			test_run = true;
			break;
		case 'f':
		case 'e':
			/** already handled */
			break;
		case 'h':
			print_usage();
			goto exit_1;
		default:
			print_error("Unkown option\n");
			print_usage();
			goto exit_1;
		}
	}

	/** options didn't include specific tests, just run them all */
	if (!test_run)
		nr_failed = run_all_tests(0, false);


	if (nr_failed)
		print_error("ERROR, %i TEST(S) FAILED\n", nr_failed);
	else
		print_message("\nSUCCESS! NO TEST FAILURES\n");

exit_1:
	/* There is no ULT/thread calls vos_gc_run() in this utility, it is
	 * possible VOS GC might still take refcount on already closed pools.
	 * These in-mem pools will be freed by calling gc_wait().
	 *
	 * NB: this function is only defined for standalone mode.
	 */
	gc_wait();
	vos_fini();
exit_0:
	daos_debug_fini();
	return nr_failed;
}
