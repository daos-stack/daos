/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <abt.h>
#include <cmocka.h>
#include <gurt/common.h>

#include "dlck_checker_ut_mock.h"

struct dlck_checker_main Dcm;
struct dlck_checker_worker Dcw;
const ABT_mutex Mock_mutex_handle = (ABT_mutex)0x1234;
FILE *const                Mock_file_stream  = (FILE *)0x5678;
int mock_vfprintf_check_args;
static char                Mock_log_file[] = "mock-vos-log";

/* helper functions for setting up and handling mocked checker allocations */
void
expect_checker_d_calloc(size_t size, void *payload)
{
	expect_value(__wrap_d_calloc, nmemb, 1);
	expect_value(__wrap_d_calloc, size, size);
	will_return(__wrap_d_calloc, payload);
}

/* mocks */
void *
__wrap_d_calloc(size_t nmemb, size_t size)
{
	void *payload;

	check_expected(nmemb);
	check_expected(size);
	payload = mock_ptr_type(void *);
	if (payload == NULL)
		return NULL;
	assert_true((payload == &Dcm && size == sizeof(Dcm)) ||
		    (payload == &Dcw && size == sizeof(Dcw)));
	memset(payload, 0, size);
	return payload;
}

void
__wrap_d_free(void *ptr)
{
	if (ptr == &Dcm || ptr == &Dcw) {
		check_expected_ptr(ptr);
		return;
	}
	if (ptr == Mock_log_file)
		return;
	fail_msg("Unexpected d_free(%p)", ptr);
}

char *
__wrap_d_asprintf2(int *rc, const char *fmt, ...)
{
	int mock_rc = mock_type(int);

	(void)fmt;
	if (mock_rc == -1) {
		*rc = -1;
		return NULL;
	}

	*rc = sizeof(Mock_log_file) - 1;
	return Mock_log_file;
}

FILE *
__wrap_fopen(const char *path, const char *mode)
{
	int error = mock_type(int);

	if (error != 0) {
		errno = error;
		return NULL;
	}
	assert_string_equal(path, Mock_log_file);
	assert_string_equal(mode, "w");
	return Mock_file_stream;
}

int
__wrap_fclose(FILE *stream)
{
	int rc = mock_type(int);

	assert_ptr_equal(stream, Mock_file_stream);
	function_called();
	if (rc == EOF)
		errno = EIO;
	return rc;
}

int
__wrap_ABT_mutex_create(ABT_mutex *newmutex)
{
	int rc = mock_type(int);

	check_expected_ptr(newmutex);
	assert_non_null(newmutex);
	if (rc == ABT_SUCCESS)
		*newmutex = Mock_mutex_handle;
	return rc;
}

int
__wrap_ABT_mutex_free(ABT_mutex *mutex)
{
	int rc = mock_type(int);

	check_expected_ptr(mutex);
	assert_non_null(mutex);
	assert_ptr_equal(*mutex, Mock_mutex_handle);
	if (rc == ABT_SUCCESS)
		*mutex = ABT_MUTEX_NULL;
	return rc;
}

int
__wrap_ABT_mutex_lock(ABT_mutex mutex)
{
	int rc = mock_type(int);

	check_expected_ptr(mutex);
	return rc;
}

int
__wrap_ABT_mutex_unlock(ABT_mutex mutex)
{
	int rc = mock_type(int);

	check_expected_ptr(mutex);
	return rc;
}

int
__wrap_vfprintf(FILE *stream, const char *fmt, va_list args)
{
	int rc = mock_type(int);

	(void)stream;
	if (mock_vfprintf_check_args != 0) {
		char    output[128];
		va_list copy;
		int     length;

		mock_vfprintf_check_args = 0;
		check_expected(fmt);
		va_copy(copy, args);
		length = vsnprintf(output, sizeof(output), fmt, copy);
		va_end(copy);
		assert_true(length >= 0 && length < sizeof(output));
		check_expected(output);
	}

	if (rc < 0)
		errno = EIO;
	return rc;
}

int
__wrap_fflush(FILE *stream)
{
	int error = mock_type(int);

	(void)stream;
	errno = error;
	return error != 0 ? EOF : 0;
}
