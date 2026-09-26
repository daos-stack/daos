/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <stdio.h>
#include <cmocka.h>

#include <daos_srv/checker.h>

static int
capture_vprintf(struct checker *checker, const char *fmt, va_list args)
{
	char output[128];
	int  length;

	(void)checker;
	length = vsnprintf(output, sizeof(output), fmt, args);
	assert_true(length >= 0 && length < sizeof(output));
	check_expected(output);
	return DER_SUCCESS;
}

static void
test_prefixed_print(void **state)
{
	struct checker checker = {.ck_prefix = "-- ", .ck_vprintf = capture_vprintf};

	expect_string(capture_vprintf, output, "-- ready");
	CK_PRINT(&checker, "ready");
	expect_string(capture_vprintf, output, "-- value=42");
	CK_PRINTF(&checker, "value=%d", 42);
}

static void
test_unprefixed_print(void **state)
{
	struct checker checker = {.ck_prefix = "-- ", .ck_vprintf = capture_vprintf};

	expect_string(capture_vprintf, output, "ready");
	CK_PRINT_WO_PREFIX(&checker, "ready");
	expect_string(capture_vprintf, output, "value=42");
	CK_PRINTF_WO_PREFIX(&checker, "value=%d", 42);
}

static void
test_warning_print(void **state)
{
	struct checker checker = {.ck_prefix = "-- ", .ck_vprintf = capture_vprintf};

	expect_string(capture_vprintf, output, "warning: value=42\n");
	CK_APPENDFL_WARN(&checker, "value=%d", 42);
	expect_string(capture_vprintf, output, "warning: ready\n");
	CK_APPENDL_WARN(&checker, "ready");
	assert_int_equal(checker.ck_warnings_num, 2);
}

static void
test_null_checker(void **state)
{
	struct checker *checker = NULL;
	int             evaluated = 0;

	CK_PRINT(checker, "ready");
	CK_PRINTF(checker, "value=%d", ++evaluated);
	CK_PRINT_WO_PREFIX(checker, "ready");
	CK_PRINTF_WO_PREFIX(checker, "value=%d", ++evaluated);
	CK_APPENDL_WARN(checker, "ready");
	assert_int_equal(evaluated, 0);
}

static const struct CMUnitTest checker_macro_tests[] = {
	cmocka_unit_test(test_prefixed_print),
	cmocka_unit_test(test_unprefixed_print),
	cmocka_unit_test(test_warning_print),
	cmocka_unit_test(test_null_checker),
};

int
main(void)
{
	return cmocka_run_group_tests_name("dlck_checker_macros_ut", checker_macro_tests, NULL, NULL);
}