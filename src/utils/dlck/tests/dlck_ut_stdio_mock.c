/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include <cmocka.h>

#include "dlck_ut_stdio_mock.h"

#define MOCK_ASPRINTF_LENGTH 573

FILE *const Mock_file_stream = (FILE *)0x5678;
bool        mock_vfprintf_check_output;

char *
__wrap_d_asprintf2(int *rc, const char *fmt, ...)
{
	char *result;

	(void)fmt;
	result = mock_ptr_type(char *);
	if (result == NULL) {
		*rc = -1;
	} else {
		*rc = MOCK_ASPRINTF_LENGTH;
	}

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
	int rc;

	check_expected_ptr(stream);
	check_expected(fmt);

	if (mock_vfprintf_check_output) {
		char    output[128];
		va_list copy;
		int     length;

		mock_vfprintf_check_output = false;
		va_copy(copy, args);
		length = vsnprintf(output, sizeof(output), fmt, copy);
		va_end(copy);
		assert_true(length >= 0 && length < sizeof(output));
		check_expected(output);
	}

	rc = mock_type(int);
	if (rc < 0) {
		errno = mock_type(int);
	}

	return rc;
}

/*
 * Fortified builds may emit __vfprintf_chk() instead of vfprintf(). Route it
 * through the same mock so the sentinel FILE stream never reaches libc.
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
