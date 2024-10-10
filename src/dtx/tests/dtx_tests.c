/**
 * (C) Copyright 2023-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Launcher for all DTX tests.
 */
#define D_LOGFAC DD_FAC(tests)

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <getopt.h>
#include <daos_srv/vos.h>
#include <daos/tests_lib.h>

#include "vts_common.h"

int
run_local_tests(const char *cfg);
int
run_local_rdb_tests(const char *cfg);
int
run_structs_tests(const char *cfg);

static void
print_usage()
{
	print_message("Use one of these opt(s) for specific test\n");
	print_message("dtx_tests -A|--all <size>\n");
	print_message("dtx_tests -h|--help\n");
	print_message("-S|--storage <storage path>\n");
	print_message("Default <dtx_tests> runs all tests\n");
	print_message("The following options can be used with any of the above:\n");
	print_message("  -f|--filter <filter>\n");
	print_message("  -e|--exclude <filter>\n");
}

static inline int
run_all_tests(int keys)
{
	char cfg_desc_io[DTS_CFG_MAX];
	int  failed = 0;

	dts_create_config(cfg_desc_io, "keys=%d", keys);

	failed += run_local_tests(cfg_desc_io);
	failed += run_local_rdb_tests(cfg_desc_io);
	failed += run_structs_tests(cfg_desc_io);

	return failed;
}

int
main(int argc, char **argv)
{
	int                  rc        = 0;
	int                  nr_failed = 0;
	int                  opt       = 0;
	int                  index     = 0;
	int                  keys;
	const char          *short_options  = "Ahe:f:S:";
	static struct option long_options[] = {
	    {"all", no_argument, 0, 'A'},           {"help", no_argument, 0, 'h'},
	    {"exclude", required_argument, 0, 'e'}, {"filter", required_argument, 0, 'f'},
	    {"storage", required_argument, 0, 'S'}, {NULL},
	};

	d_register_alt_assert(mock_assert);

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc) {
		print_error("Error initializing debug system\n");
		return rc;
	}

	bool test_run = false;

	while ((opt = getopt_long(argc, argv, short_options, long_options, &index)) != -1) {
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
		default:
			break;
		}
	}

	if (vos_path[0] == '\0') {
		strcpy(vos_path, "/mnt/daos");
	}

	rc = vos_self_init(vos_path, false, BIO_STANDALONE_TGT_ID);
	if (rc) {
		print_error("Error initializing VOS instance\n");
		goto exit_0;
	}

	index  = 0;
	optind = 0;

	while ((opt = getopt_long(argc, argv, short_options, long_options, &index)) != -1) {
		switch (opt) {
		case 'A':
			keys      = atoi(optarg);
			nr_failed = run_all_tests(keys);
			test_run  = true;
			break;
		case 'S':
		case 'f':
		case 'e':
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
		nr_failed = run_all_tests(0);

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
