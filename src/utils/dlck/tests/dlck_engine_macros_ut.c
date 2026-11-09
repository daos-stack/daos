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

/** verify dlck_status_set_rc() ignores DER_SUCCESS and keeps the stored return code intact */
static void
test_set_rc_ignores_success(void **unused)
{
	struct checker_status status = {.rc = DER_SUCCESS};

	dlck_status_set_rc(&status, DER_SUCCESS);
	assert_int_equal(status.rc, DER_SUCCESS);
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

/** verify dlck_xstream_progress_get() reads the current progress value */
static void
test_progress_get(void **unused)
{
	struct xstream_arg xa       = {.progress = 42};
	unsigned           progress = 0;

	dlck_xstream_progress_get(&xa, &progress);
	assert_int_equal(progress, 42);
}

#define TEST(ID, FUNC) {"DLCK_ENGINE_MACRO_" #ID ": " #FUNC, FUNC, NULL, NULL, NULL}

static const struct CMUnitTest dlck_engine_macros_tests[] = {
    TEST(101, test_set_rc_ignores_success),
    TEST(102, test_set_rc_records_first_error),
    TEST(103, test_set_rc_keeps_first_error),
    TEST(104, test_uadd_no_overflow_sum),
    TEST(105, test_uadd_no_overflow_exact_max),
    TEST(106, test_uadd_no_overflow_clamps),
    TEST(107, test_progress_get),
};

int
main(int argc, char **argv)
{
#if CMOCKA_FILTER_SUPPORTED
	if (argc > 1) {
		char filter[256];
		int  rc = snprintf(filter, sizeof(filter), "*%s*", argv[1]);

		if (rc > 0 && (size_t)rc < sizeof(filter)) {
			cmocka_set_test_filter(filter);
		} else {
			fprintf(stderr, "ERROR: Test filter '%s' cannot be applied.\n", argv[1]);
			return 1;
		}
	}
#else
	(void)argc;
	(void)argv;
#endif
	return cmocka_run_group_tests_name("dlck_engine_macros_ut", dlck_engine_macros_tests, NULL,
					   NULL);
}
