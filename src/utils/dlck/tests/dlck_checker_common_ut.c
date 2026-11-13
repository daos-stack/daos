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
#include <daos/debug.h>
#include <daos/checker.h>

#include "dlck_ut_checker_mock.h"

#define FMT  "worker %d: %s"
#define ARG0 42
#define ARG1 "ready"

static struct checker Checker_state = {.ck_vprintf = mock_ck_vprintf};

/* Reset the rendered-output verification before every test. */
static int
setup(void **unused)
{
	mock_ck_vprintf_check_output = false;

	return 0;
}

static void
test_ck_common_vprintf_dispatches_arguments(void **state)
{
	struct checker *ck = *state;

	mock_ck_vprintf_check_output = true;
	expect_string(mock_ck_vprintf, output, "worker 42: ready");
	EXPECT_MOCK_CK_VPRINTF(ck, FMT, DER_SUCCESS);
	assert_int_equal(ck_common_printf(ck, FMT, ARG0, ARG1), DER_SUCCESS);
}

static void
test_ck_common_vprintf_returns_callback_error(void **state)
{
	struct checker *ck = *state;

	EXPECT_MOCK_CK_VPRINTF(ck, FMT, -DER_IO);
	assert_int_equal(ck_common_printf(ck, FMT, ARG0, ARG1), -DER_IO);
}

static const struct CMUnitTest dlck_checker_common_tests[] = {
    {"DLCK_CHECKER_COMMON_101: vprintf dispatches arguments",
     test_ck_common_vprintf_dispatches_arguments, setup, NULL, &Checker_state},
    {"DLCK_CHECKER_COMMON_102: vprintf returns callback error",
     test_ck_common_vprintf_returns_callback_error, setup, NULL, &Checker_state},
};

int
main(void)
{
	return cmocka_run_group_tests_name("dlck_checker_common_ut", dlck_checker_common_tests,
					   NULL, NULL);
}
