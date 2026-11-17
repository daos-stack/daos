/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_errno.h>
#include <daos/debug.h>
#include <daos/checker.h>

#include "dlck_ut.h"
#include "dlck_ut_checker_mock.h"

#define FMT  "worker %d: %s"
#define ARG0 42
#define ARG1 "ready"

static struct checker Checker_state = {.ck_vprintf = mock_ck_vprintf};

static void
test_ck_common_vprintf_dispatches_arguments(void **state)
{
	struct checker *ck = *state;

	EXPECT_MOCK_CK_VPRINTF(ck, FMT, true, "worker 42: ready", DER_SUCCESS);
	assert_int_equal(ck_common_printf(ck, FMT, ARG0, ARG1), DER_SUCCESS);
}

static void
test_ck_common_vprintf_returns_callback_error(void **state)
{
	struct checker *ck = *state;

	EXPECT_MOCK_CK_VPRINTF(ck, FMT, false, "", -DER_IO);
	assert_int_equal(ck_common_printf(ck, FMT, ARG0, ARG1), -DER_IO);
}

static const struct CMUnitTest dlck_checker_common_tests[] = {
    {"DLCK_CHECKER_COMMON_101: vprintf dispatches arguments",
     test_ck_common_vprintf_dispatches_arguments, NULL, NULL, &Checker_state},
    {"DLCK_CHECKER_COMMON_102: vprintf returns callback error",
     test_ck_common_vprintf_returns_callback_error, NULL, NULL, &Checker_state},
};

int
main(int argc, char **argv)
{
	if (argc > 1) {
		dlck_ut_filter(argc, argv);
	}
	return cmocka_run_group_tests_name("dlck_checker_common_ut", dlck_checker_common_tests,
					   NULL, NULL);
}
