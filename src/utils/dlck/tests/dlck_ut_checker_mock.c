/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdbool.h>
#include <stdio.h>

#include "dlck_ut_checker_mock.h"

int
mock_ck_vprintf(struct checker *ck, const char *fmt, va_list args)
{
	bool check_output;

	check_expected_ptr(ck);
	check_expected(fmt);
	check_output = mock_type(bool);
	if (check_output) {
		char output[128];
		int  length;

		length = vsnprintf(output, sizeof(output), fmt, args);
		assert_true(length >= 0 && length < sizeof(output));
		check_expected(output);
	}
	return mock_type(int);
}
