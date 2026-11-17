/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

#include "dlck_ut_stdio_mock.h"

char *
__wrap_d_asprintf2(int *rc, const char *fmt, ...)
{
	char *result;

	assert_non_null(rc);
	check_expected(fmt);
	result = mock_ptr_type(char *);
	*rc    = mock_type(int);
	return result;
}

FILE *
__wrap_fopen(const char *path, const char *mode)
{
	FILE *result;

	check_expected(path);
	check_expected(mode);
	result = mock_ptr_type(FILE *);
	if (result == NULL) {
		errno = mock_type(int);
	}
	return result;
}

int
__wrap_fclose(FILE *stream)
{
	int rc;

	check_expected_ptr(stream);
	rc = mock_type(int);
	if (rc == EOF) {
		errno = mock_type(int);
	}
	return rc;
}

int
__wrap_vfprintf(FILE *stream, const char *fmt, va_list args)
{
	bool check_output;
	int  rc;

	check_expected_ptr(stream);
	check_expected(fmt);
	check_output = mock_type(bool);
	if (check_output) {
		char output[128];
		int  length = vsnprintf(output, sizeof(output), fmt, args);
		assert_true(length >= 0 && length < sizeof(output));
		check_expected(output);
	}
	rc = mock_type(int);
	if (rc < 0) {
		errno = mock_type(int);
	}
	return rc;
}

/* Release builds use the fortified __vfprintf_chk entry point; route it through the mock so the
 * fake stream never reaches libc.
 */
int
__wrap___vfprintf_chk(FILE *stream, int flag, const char *fmt, va_list args)
{
	(void)flag;
	return __wrap_vfprintf(stream, fmt, args);
}

void
__wrap_d_log_sync(void)
{
	function_called();
}

int
__wrap_fflush(FILE *stream)
{
	int rc;

	check_expected_ptr(stream);
	rc = mock_type(int);
	if (rc == EOF) {
		errno = mock_type(int);
	}
	return rc;
}
