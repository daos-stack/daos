/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include <cmocka.h>

#include "dlck_ut_checker_mock.h"

bool mock_ck_vprintf_check_output;

int
mock_ck_vprintf(struct checker *ck, const char *fmt, va_list args)
{
	check_expected_ptr(ck);
	check_expected(fmt);

	if (mock_ck_vprintf_check_output) {
		char    output[128];
		va_list copy;
		int     length;

		mock_ck_vprintf_check_output = false;
		va_copy(copy, args);
		length = vsnprintf(output, sizeof(output), fmt, copy);
		va_end(copy);
		assert_true(length >= 0 && length < sizeof(output));
		check_expected(output);
	}

	return mock_type(int);
}
