/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <limits.h>

#include <daos_errno.h>

#include "../dlck_engine.h"
#include "dlck_ut.h"

/** A successful result does not replace a stored error. */
static void
test_dlck_status_set_rc_ignores_success(void **unused)
{
	struct checker_status status = {.rc = -DER_DURATION_MISMATCH};

	dlck_status_set_rc(&status, DER_SUCCESS);
	assert_int_equal(status.rc, -DER_DURATION_MISMATCH);
}

/** The first error is recorded. */
static void
test_dlck_status_set_rc_records_first_error(void **unused)
{
	struct checker_status status = {.rc = DER_SUCCESS};

	dlck_status_set_rc(&status, -DER_NOMEM);
	assert_int_equal(status.rc, -DER_NOMEM);
}

/** A later error does not replace the first error. */
static void
test_dlck_status_set_rc_keeps_first_error(void **unused)
{
	struct checker_status status = {.rc = -DER_NOMEM};

	dlck_status_set_rc(&status, -DER_IO);
	assert_int_equal(status.rc, -DER_NOMEM);
}

/** Values are added when the sum fits. */
static void
test_dlck_uadd_no_overflow_sum(void **unused)
{
	assert_int_equal(dlck_uadd_no_overflow(2, 3), 5);
}

/** A sum equal to the maximum remains unchanged. */
static void
test_dlck_uadd_no_overflow_exact_max(void **unused)
{
	assert_int_equal(dlck_uadd_no_overflow(UINT_MAX - 2, 2), UINT_MAX);
}

/** An overflowing sum is clamped to the maximum. */
static void
test_dlck_uadd_no_overflow_clamps(void **unused)
{
	assert_int_equal(dlck_uadd_no_overflow(UINT_MAX - 1, 2), UINT_MAX);
}

/** The aggregate accumulates the counters, keeps the first error, and clears the source. */
static void
test_dlck_status_set_aggregates_and_clears(void **unused)
{
	struct checker_status status = {.warnings_num = 1, .errors_num = 2, .rc = -DER_NOMEM};
	struct checker_status cks    = {.warnings_num = 3, .errors_num = 4, .rc = -DER_IO};

	dlck_status_set(&status, &cks);
	assert_int_equal(status.warnings_num, 4);
	assert_int_equal(status.errors_num, 6);
	assert_int_equal(status.rc, -DER_NOMEM);
	assert_int_equal(cks.warnings_num, 0);
	assert_int_equal(cks.errors_num, 0);
	assert_int_equal(cks.rc, DER_SUCCESS);
}

/** The aggregated counters are clamped to the maximum. */
static void
test_dlck_status_set_clamps_counters(void **unused)
{
	struct checker_status status = {.warnings_num = UINT_MAX, .errors_num = UINT_MAX - 1};
	struct checker_status cks    = {.warnings_num = 1, .errors_num = 2, .rc = -DER_IO};

	dlck_status_set(&status, &cks);
	assert_int_equal(status.warnings_num, UINT_MAX);
	assert_int_equal(status.errors_num, UINT_MAX);
	assert_int_equal(status.rc, -DER_IO);
}

#define TEST(ID, FUNC) {"DLCK_ENGINE_MACRO_" #ID ": " #FUNC, FUNC, NULL, NULL, NULL}

static const struct CMUnitTest dlck_engine_macros_tests[] = {
    TEST(101, test_dlck_status_set_rc_ignores_success),
    TEST(102, test_dlck_status_set_rc_records_first_error),
    TEST(103, test_dlck_status_set_rc_keeps_first_error),
    TEST(104, test_dlck_uadd_no_overflow_sum),
    TEST(105, test_dlck_uadd_no_overflow_exact_max),
    TEST(106, test_dlck_uadd_no_overflow_clamps),
    TEST(107, test_dlck_status_set_aggregates_and_clears),
    TEST(108, test_dlck_status_set_clamps_counters),
};

int
main(int argc, char **argv)
{
	if (argc > 1) {
		dlck_ut_filter(argc, argv);
	}

	return cmocka_run_group_tests_name("dlck_engine_macros_ut", dlck_engine_macros_tests, NULL,
					   NULL);
}
