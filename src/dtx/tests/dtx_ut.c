/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Launcher for all DTX unit tests.
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

int
run_structs_tests(void);
int
run_discard_invalid_tests(void);

static void
print_usage()
{
	print_message("Use one of these opt(s) for specific test\n");
	print_message("dtx_ut -h|--help\n");
	print_message("Default <dtx_ut> runs all tests\n");
	print_message("The following options can be used with any of the above:\n");
	print_message("  -f|--filter <filter>\n");
	print_message("  -e|--exclude <filter>\n");
}

static inline int
run_all_tests(int keys)
{
	int failed = 0;

	failed += run_structs_tests();
	failed += run_discard_invalid_tests();

	return failed;
}

int
main(int argc, char **argv)
{
	int                  rc             = 0;
	int                  nr_failed      = 0;
	int                  opt            = 0;
	int                  index          = 0;
	const char          *short_options  = "he:f:";
	static struct option long_options[] = {
	    {"help", no_argument, 0, 'h'},
	    {"exclude", required_argument, 0, 'e'},
	    {"filter", required_argument, 0, 'f'},
	    {NULL},
	};

	d_register_alt_assert(mock_assert);

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc) {
		print_error("Error initializing debug system\n");
		return rc;
	}

	while ((opt = getopt_long(argc, argv, short_options, long_options, &index)) != -1) {
		switch (opt) {
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

	nr_failed = run_all_tests(0);

	if (nr_failed)
		print_error("ERROR, %i TEST(S) FAILED\n", nr_failed);
	else
		print_message("\nSUCCESS! NO TEST FAILURES\n");

exit_0:
	daos_debug_fini();
	return nr_failed;
}
