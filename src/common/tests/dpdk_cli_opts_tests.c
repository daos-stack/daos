/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include <daos_srv/control.h>

/* Test dpdk_cli_build_opts with valid log levels */
static void
test_dpdk_cli_build_opts_valid(void **state)
{
	const char *opts;
	int         log_level;

	/* Test each valid log level */
	for (log_level = 1; log_level <= 8; log_level++) {
		opts = dpdk_cli_build_opts(log_level, log_level);
		assert_non_null(opts);

		/* Verify the string contains the correct log level */
		char expected[64];
		snprintf(expected, sizeof(expected), "--log-level=lib.eal:%d ", log_level);
		assert_non_null(strstr(opts, expected));

		/* Verify it contains --no-telemetry */
		assert_non_null(strstr(opts, "--no-telemetry"));
	}
}

/* Test dpdk_cli_build_opts with invalid log levels */
static void
test_dpdk_cli_build_opts_invalid(void **state)
{
	const char *opts;

	/* Test below minimum */
	opts = dpdk_cli_build_opts(0, 1);
	assert_null(opts);

	/* Test above maximum */
	opts = dpdk_cli_build_opts(9, 1);
	assert_null(opts);

	/* Test negative */
	opts = dpdk_cli_build_opts(-1, 1);

	/* Test the same for the second input */

	opts = dpdk_cli_build_opts(1, 0);
	assert_null(opts);

	opts = dpdk_cli_build_opts(1, 9);
	assert_null(opts);

	opts = dpdk_cli_build_opts(1, -1);
	assert_null(opts);
}

/* Test dpdk_cli_build_opts_selective */
static void
test_dpdk_cli_build_opts_selective(void **state)
{
	const char *opts;

	/* Test EAL at DEBUG, others at ERROR */
	opts = dpdk_cli_build_opts(8, 4);
	assert_non_null(opts);

	/* Verify EAL is at level 8 */
	assert_non_null(strstr(opts, "--log-level=lib.eal:8 "));

	/* Verify malloc is at level 4 */
	assert_non_null(strstr(opts, "--log-level=lib.malloc:4 "));
}

/* Test that different log levels produce different strings */
static void
test_dpdk_cli_build_opts_different_levels(void **state)
{
	const char *tmp;
	char        opts4[2048];
	const char *opts8;

	/**
	 * Returned will be the single string buffer and it will be overridden on each call to the
	 * function so copy to a local buffer before comparison.
	 */
	tmp = dpdk_cli_build_opts(4, 4);
	strcpy(opts4, tmp);
	opts8 = dpdk_cli_build_opts(8, 8);

	assert_non_null(opts4);
	assert_non_null(opts8);
	assert_non_null(tmp);

	/* Should be different strings */
	assert_string_not_equal(opts4, opts8);

	/* opts4 should have ":4 " */
	assert_non_null(strstr(opts4, ":4 "));
	assert_null(strstr(opts4, ":8 "));

	/* opts8 should have ":8 " */
	assert_non_null(strstr(opts8, ":8 "));
	assert_null(strstr(opts8, ":4 "));
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
	    cmocka_unit_test(test_dpdk_cli_build_opts_valid),
	    cmocka_unit_test(test_dpdk_cli_build_opts_invalid),
	    cmocka_unit_test(test_dpdk_cli_build_opts_selective),
	    cmocka_unit_test(test_dpdk_cli_build_opts_different_levels),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
