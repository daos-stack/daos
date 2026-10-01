/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_errno.h>
#include <daos_srv/checker.h>

static int
mock_checker_vprintf(struct checker *ck, const char *fmt, va_list args)
{
	int         value   = va_arg(args, int);
	const char *message = va_arg(args, const char *);

	check_expected_ptr(ck);
	check_expected(fmt);
	check_expected(value);
	check_expected(message);
	return mock_type(int);
}

static void
expect_callback_arguments(struct checker *ck)
{
	expect_value(mock_checker_vprintf, ck, ck);
	expect_string(mock_checker_vprintf, fmt, "worker %d: %s");
	expect_value(mock_checker_vprintf, value, 42);
	expect_string(mock_checker_vprintf, message, "ready");
}

static struct checker Checker_state = {.ck_vprintf = mock_checker_vprintf};

static void
test_ck_common_vprintf_dispatches_arguments(void **state)
{
	struct checker *ck = *state;

	expect_callback_arguments(ck);
	will_return(mock_checker_vprintf, DER_SUCCESS);
	assert_int_equal(ck_common_printf(ck, "worker %d: %s", 42, "ready"), DER_SUCCESS);
}

static void
test_ck_common_vprintf_returns_callback_error(void **state)
{
	struct checker *ck = *state;

	expect_callback_arguments(ck);
	will_return(mock_checker_vprintf, -DER_IO);
	assert_int_equal(ck_common_printf(ck, "worker %d: %s", 42, "ready"), -DER_IO);
}

static const struct CMUnitTest dlck_checker_common_tests[] = {
    {"DLCK_CHECKER_COMMON_101: vprintf dispatches arguments",
     test_ck_common_vprintf_dispatches_arguments, NULL, NULL, &Checker_state},
    {"DLCK_CHECKER_COMMON_102: vprintf returns callback error",
     test_ck_common_vprintf_returns_callback_error, NULL, NULL, &Checker_state},
};

int
main(void)
{
	return cmocka_run_group_tests_name("dlck_checker_common_ut", dlck_checker_common_tests,
					   NULL, NULL);
}
