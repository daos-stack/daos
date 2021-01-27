/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
	print_message("vos_tests -z|--csum_tests\n");
	print_message("vos_tests -A|--all_tests\n");
	print_message("vos_tests -f|--filter <filter>\n");
	print_message("vos_tests -e|--exclude <filter>\n");
	print_message("vos_tests -m|--punch-model-tests\n");
	print_message("vos_tests -C|--mvcc-tests\n");
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
	const char	*bypass = getenv("DAOS_IO_BYPASS");
	const char	*it;
	char		 cfg_desc_io[DTS_CFG_MAX];
	int		 failed = 0;
	int		 feats;
	int		 i;
	int		 j;

	if (!bypass) {
		if (!nest_iterators) {
			dts_create_config(cfg_desc_io, "keys=%d", keys);
			failed += run_ts_tests(cfg_desc_io);
			failed += run_mvcc_tests(cfg_desc_io);
		}
		bypass = "none";
	}

	dts_create_config(cfg_desc_io, "keys=%d bypass=%s", keys, bypass);

	if (nest_iterators == false) {
		failed += run_pm_tests(cfg_desc_io);
		failed += run_pool_test(cfg_desc_io);
		failed += run_co_test(cfg_desc_io);
		failed += run_discard_tests(cfg_desc_io);
		failed += run_aggregate_tests(false, cfg_desc_io);
		failed += run_gc_tests(cfg_desc_io);
		failed += run_dtx_tests(cfg_desc_io);
		failed += run_ilog_tests(cfg_desc_io);
		failed += run_csum_extent_tests(cfg_desc_io);

		it = "standalone";
	} else {
		it = "nested";
	}
	dts_create_config(cfg_desc_io, "keys=%d bypass=%s iterator=%s", keys,
		      bypass, it);

	for (i = 0; dkey_feats[i] >= 0; i++) {
		for (j = 0; akey_feats[j] >= 0; j++) {
			feats = dkey_feats[i] | akey_feats[j];
			failed += run_io_test(feats, keys, nest_iterators,
					      cfg_desc_io);
		}
	}

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
	const char *short_options = "apcdglzni:mXA:hf:e:tC";
	static struct option long_options[] = {
		{"all_tests",		required_argument, 0, 'A'},
		{"pool_tests",		no_argument, 0, 'p'},
		{"container_tests",	no_argument, 0, 'c'},
		{"io_tests",		required_argument, 0, 'i'},
		{"discard_tests",	no_argument, 0, 'd'},
		{"nest_iterators",	no_argument, 0, 'n'},
		{"aggregate_tests",	no_argument, 0, 'a'},
		{"dtx_tests",		no_argument, 0, 'X'},
		{"punch_model_tests",	no_argument, 0, 'm'},
		{"garbage_collector",	no_argument, 0, 'g'},
		{"ilog_tests",		no_argument, 0, 'l'},
		{"epoch cache tests",	no_argument, 0, 't'},
		{"mvcc_tests",		no_argument, 0, 'C'},
		{"csum_tests",		no_argument, 0, 'z'},
		{"help",		no_argument, 0, 'h'},
		{"filter",		required_argument, 0, 'f'},
		{"exclude",		required_argument, 0, 'e'},
	};

	d_register_alt_assert(mock_assert);

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
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

	while ((opt = getopt_long(argc, argv, short_options,
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
			{
				/** Add wildcards for easier filtering */
				char filter[sizeof(optarg) + 2];

				sprintf(filter, "*%s*", optarg);
				cmocka_set_test_filter(filter);
				printf("Test filter: %s\n", filter);
			}
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

	while ((opt = getopt_long(argc, argv, short_options,
				  long_options, &index)) != -1) {
		switch (opt) {
		case 'p':
			nr_failed += run_pool_test("");
			test_run = true;
			break;
		case 'c':
			nr_failed += run_co_test("");
			test_run = true;
			break;
		case 'n':
			nest_iterators = true;
			break;
		case 'i':
			ofeats = strtol(optarg, NULL, 16);
			nr_failed += run_io_test(ofeats, 0,
						 nest_iterators,
						 "");
			test_run = true;
			break;
		case 'a':
			nr_failed += run_aggregate_tests(true,
							 "");
			test_run = true;
			break;
		case 'd':
			nr_failed += run_discard_tests("");
			test_run = true;
			break;
		case 'g':
			nr_failed += run_gc_tests("");
			test_run = true;
			break;
		case 'X':
			nr_failed += run_dtx_tests("");
			test_run = true;
			break;
		case 'm':
			nr_failed += run_pm_tests("");
			test_run = true;
			break;
		case 'A':
			keys = atoi(optarg);
			nr_failed = run_all_tests(keys, nest_iterators);
			test_run = true;
			break;
		case 'l':
			nr_failed += run_ilog_tests("");
			test_run = true;
			break;
		case 'z':
			nr_failed += run_csum_extent_tests("");
			test_run = true;
			break;
		case 't':
			nr_failed += run_ts_tests("");
			test_run = true;
			break;
		case 'C':
			nr_failed += run_mvcc_tests("");
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
			print_error("Unknown option\n");
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
	vos_fini();
exit_0:
	daos_debug_fini();
	return nr_failed;
}
