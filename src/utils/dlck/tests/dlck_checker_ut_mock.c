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

struct dlck_checker_main   Dcm;
struct dlck_checker_worker Dcw;
const ABT_mutex            Mock_mutex_handle = (ABT_mutex)0x1234;
FILE *const                Mock_file_stream  = (FILE *)0x5678;
int                        mock_vfprintf_check_output;
char                       Mock_log_file[] = "mock-vos-log";
uuid_t                     Mock_pool_uuid  = {0x66, 0x88, 0xdb, 0xaa, 0x95, 0x42, 0x46, 0x2d,
					      0xaa, 0x6d, 0xd7, 0x8b, 0xcd, 0x28, 0xd5, 0x59};

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
	void *payload = mock_ptr_type(void *);

	check_expected(nmemb);
	check_expected(size);

	if (payload != NULL) {
		assert_true((payload == &Dcm && size == sizeof(Dcm)) ||
			    (payload == &Dcw && size == sizeof(Dcw)));
		memset(payload, 0, size);
	}
	return payload;
}

void
__wrap_d_free(void *ptr)
{
	check_expected_ptr(ptr);
}

char *
__wrap_d_asprintf2(int *rc, const char *fmt, ...)
{
	char *result = mock_ptr_type(char *);
	(void)fmt;
	if (result == NULL)
		*rc = -1;
	else
		*rc = 573;

	return result;
}

FILE *
__wrap_fopen(const char *path, const char *mode)
{
	int error = mock_type(int);

	check_expected(path);
	check_expected(mode);
	if (error != 0) {
		errno = error;
		return NULL;
	}

	return mock_ptr_type(FILE *);
}

int
__wrap_fclose(FILE *stream)
{
	int rc = mock_type(int);

	check_expected_ptr(stream);
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
		*newmutex = mock_ptr_type(ABT_mutex);
	return rc;
}

int
__wrap_ABT_mutex_free(ABT_mutex *mutex)
{
	ABT_mutex handle;
	int       rc = mock_type(int);

	check_expected_ptr(mutex);
	assert_non_null(mutex);
	handle = *mutex;
	check_expected_ptr(handle);
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

	check_expected_ptr(stream);
	check_expected(fmt);
	if (mock_vfprintf_check_output != 0) {
		char    output[128];
		va_list copy;
		int     length;

		mock_vfprintf_check_output = 0;
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

int
__wrap_fflush(FILE *stream)
{
	int error = mock_type(int);

	check_expected_ptr(stream);
	if (error != 0) {
		errno = error;
		return EOF;
	}
	return 0;
}
