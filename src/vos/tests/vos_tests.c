/**
 * (C) Copyright 2016-2022 Intel Corporation.
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

#define		FORCE_CSUM 0x1001
#define		FORCE_NO_ZERO_COPY 0x1002

static void
print_usage()
{
	print_message("Use one of these opt(s) for specific test\n");
	print_message("vos_tests -p|--pool_tests\n");
	print_message("vos_tests -c|--container_tests\n");
	print_message("vos_tests -i|--io_tests <otype>\n");
	print_message("otypes = DAOS_OT_DKEY_UINT64, DAOS_OT_DKEY_LEXICAL\n");
	print_message("%8s DAOS_OT_AKEY_UINT64, DAOS_OT_AKEY_LEXICAL\n", " ");
	print_message("vos_tests -d |--discard-tests\n");
	print_message("vos_tests -a |--aggregate-tests\n");
	print_message("vos_tests -X|--dtx_tests\n");
	print_message("vos_tests -l|--incarnation-log-tests\n");
	print_message("vos_tests -z|--csum_tests\n");
	print_message("vos_tests -A|--all_tests\n");
	print_message("vos_tests -m|--punch-model-tests\n");
	print_message("vos_tests -C|--mvcc-tests\n");
	print_message("-S|--storage <storage path>\n");
	print_message("vos_tests -h|--help\n");
	print_message("Default <vos_tests> runs all tests\n");
	print_message("The following options can be used with any of the above:\n");
	print_message("  -f|--filter <filter>\n");
	print_message("  -e|--exclude <filter>\n");
	print_message("  --force_checksum\n");
	print_message("  --force_no_zero_copy\n");
}

static int type_list[] = {
	0,
	DAOS_OT_AKEY_UINT64,
	DAOS_OT_AKEY_LEXICAL,
	DAOS_OT_DKEY_UINT64,
	DAOS_OT_DKEY_LEXICAL,
	DAOS_OT_MULTI_UINT64,
	DAOS_OT_MULTI_LEXICAL
};

static inline int
run_all_tests(int keys, bool nest_iterators)
{
	const char	*it;
	char		 cfg_desc_io[DTS_CFG_MAX];
	int		 failed = 0;
	int		 i;

	if (!nest_iterators) {
		dts_create_config(cfg_desc_io, "keys=%d", keys);
		failed += run_ts_tests(cfg_desc_io);
		failed += run_mvcc_tests(cfg_desc_io);
	}

	dts_create_config(cfg_desc_io, "keys=%d", keys);

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
	dts_create_config(cfg_desc_io, "keys=%d iterator=%s", keys, it);

	for (i = 0; i < (sizeof(type_list) / sizeof(int)); i++) {
		failed += run_io_test(type_list[i], keys, nest_iterators, cfg_desc_io);
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
	int	otypes;
	int	keys;
	bool	nest_iterators = false;
	const char *short_options = "apcdglzni:mXA:S:hf:e:tC";
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
		{"storage",		required_argument, 0, 'S'},
		{"force_csum",		no_argument, 0, FORCE_CSUM},
		{"force_no_zero_copy",	no_argument, 0, FORCE_NO_ZERO_COPY},
		{NULL},
	};

	d_register_alt_assert(mock_assert);

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc) {
		print_error("Error initializing debug system\n");
		return rc;
	}

	gc = 0;
	bool test_run = false;

	while ((opt = getopt_long(argc, argv, short_options,
				  long_options, &index)) != -1) {
		switch (opt) {
		case 'S':
			if (strnlen(optarg, STORAGE_PATH_LEN) >= STORAGE_PATH_LEN) {
				print_error("%s is longer than STORAGE_PATH_LEN.\n", optarg);
				goto exit_0;
			}
			strncpy(vos_path, optarg, STORAGE_PATH_LEN);
			break;
		case 'h':
			print_usage();
			goto exit_0;

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
		case FORCE_CSUM:
			g_force_checksum = true;
			break;
		case FORCE_NO_ZERO_COPY:
			g_force_no_zero_copy = true;
			break;
		default:
			break;
		}
	}

	if (vos_path[0] == '\0') {
		strcpy(vos_path, "/mnt/daos");
	}

	rc = vos_self_init(vos_path);
	if (rc) {
		print_error("Error initializing VOS instance\n");
		goto exit_0;
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
			otypes = strtol(optarg, NULL, 16);
			nr_failed += run_io_test(otypes, 0,
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
		case 'S':
		case 'f':
		case 'e':
		case FORCE_CSUM:
		case FORCE_NO_ZERO_COPY:
			/** already handled */
			break;
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
	vos_self_fini();
exit_0:
	daos_debug_fini();
	return nr_failed;
}
