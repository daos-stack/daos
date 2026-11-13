/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include <cmocka.h>

#include <daos/debug.h>
#include <daos_errno.h>

#include "../dlck_engine.h"
#include "dlck_ut.h"

/** verify dlck_status_set_rc() ignores DER_SUCCESS and keeps the stored return code intact */
static void
test_set_rc_ignores_success(void **unused)
{
	struct checker_status status = {.rc = -DER_DURATION_MISMATCH};

	dlck_status_set_rc(&status, DER_SUCCESS);
	assert_int_equal(status.rc, -DER_DURATION_MISMATCH);
}

/** verify dlck_status_set_rc() records the first error reported */
static void
test_set_rc_records_first_error(void **unused)
{
	struct checker_status status = {.rc = DER_SUCCESS};

	dlck_status_set_rc(&status, -DER_NOMEM);
	assert_int_equal(status.rc, -DER_NOMEM);
}

/** verify dlck_status_set_rc() does not overwrite an already recorded error */
static void
test_set_rc_keeps_first_error(void **unused)
{
	struct checker_status status = {.rc = -DER_NOMEM};

	dlck_status_set_rc(&status, -DER_IO);
	assert_int_equal(status.rc, -DER_NOMEM);
}

/** verify dlck_uadd_no_overflow() adds two values when no overflow occurs */
static void
test_uadd_no_overflow_sum(void **unused)
{
	assert_int_equal(dlck_uadd_no_overflow(2, 3), 5);
}

/** verify dlck_uadd_no_overflow() produces the exact maximum without clamping */
static void
test_uadd_no_overflow_exact_max(void **unused)
{
	assert_int_equal(dlck_uadd_no_overflow(UINT_MAX - 2, 2), UINT_MAX);
}

/** verify dlck_uadd_no_overflow() clamps the result to UINT_MAX on overflow */
static void
test_uadd_no_overflow_clamps(void **unused)
{
	assert_int_equal(dlck_uadd_no_overflow(UINT_MAX - 1, 2), UINT_MAX);
}

#define TEST(ID, FUNC) {"DLCK_ENGINE_MACRO_" #ID ": " #FUNC, FUNC, NULL, NULL, NULL}

static const struct CMUnitTest dlck_engine_macros_tests[] = {
    TEST(101, test_set_rc_ignores_success),     TEST(102, test_set_rc_records_first_error),
    TEST(103, test_set_rc_keeps_first_error),   TEST(104, test_uadd_no_overflow_sum),
    TEST(105, test_uadd_no_overflow_exact_max), TEST(106, test_uadd_no_overflow_clamps),
};

int
main(int argc, char **argv)
{
	int rc = dlck_ut_filter(argc, argv);

	if (rc != 0) {
		return rc;
	}

	return cmocka_run_group_tests_name("dlck_engine_macros_ut", dlck_engine_macros_tests, NULL,
					   NULL);
}
