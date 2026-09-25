/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <abt.h>
#include <cmocka.h>
#include <gurt/common.h>

#include "dlck_checker_ut_mock.h"

struct dlck_checker_main Dcm;
struct dlck_checker_worker Dcw;
const ABT_mutex Mock_mutex_handle = (ABT_mutex)(uintptr_t)0x1234;
void *last_freed_payload;
int mock_vfprintf_enabled;
int mock_fflush_enabled;

void *__real_d_calloc(size_t nmemb, size_t size);
void __real_d_free(void *ptr);
FILE *__real_fopen(const char *path, const char *mode);
int __real_vfprintf(FILE *stream, const char *fmt, va_list args);
int __real_fflush(FILE *stream);

void *
__wrap_d_calloc(size_t nmemb, size_t size)
{
	if (size == sizeof(Dcm)) {
		int rc = mock_type(int);

		if (rc != 0)
			return NULL;
		memset(&Dcm, 0, sizeof(Dcm));
		return &Dcm;
	}
	if (size == sizeof(Dcw)) {
		int rc = mock_type(int);

		if (rc != 0)
			return NULL;
		memset(&Dcw, 0, sizeof(Dcw));
		return &Dcw;
	}

	return __real_d_calloc(nmemb, size);
}

void
__wrap_d_free(void *ptr)
{
	if (ptr == &Dcm || ptr == &Dcw) {
		last_freed_payload = ptr;
		return;
	}
	__real_d_free(ptr);
}

char *
__wrap_d_asprintf2(int *rc, const char *fmt, ...)
{
	char   *buf = NULL;
	va_list args;
	int     mock_rc = mock_type(int);

	if (mock_rc != 0) {
		*rc = -1;
		return NULL;
	}

	va_start(args, fmt);
	*rc = vasprintf(&buf, fmt, args);
	va_end(args);
	if (*rc == -1)
		buf = NULL;
	return buf;
}

FILE *
__wrap_fopen(const char *path, const char *mode)
{
	int error = mock_type(int);

	if (error != 0) {
		errno = error;
		return NULL;
	}
	return __real_fopen(path, mode);
}

int
__wrap_ABT_mutex_create(ABT_mutex *newmutex)
{
	int rc = mock_type(int);

	assert_non_null(newmutex);
	if (rc == ABT_SUCCESS)
		*newmutex = Mock_mutex_handle;
	return rc;
}

int
__wrap_ABT_mutex_free(ABT_mutex *mutex)
{
	int rc = mock_type(int);

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

	assert_ptr_equal(mutex, Mock_mutex_handle);
	return rc;
}

int
__wrap_ABT_mutex_unlock(ABT_mutex mutex)
{
	int rc = mock_type(int);

	assert_ptr_equal(mutex, Mock_mutex_handle);
	return rc;
}

int
__wrap_vfprintf(FILE *stream, const char *fmt, va_list args)
{
	if (mock_vfprintf_enabled != 0) {
		int rc = mock_type(int);

		(void)stream;
		(void)fmt;
		(void)args;
		mock_vfprintf_enabled = 0;
		if (rc < 0)
			errno = EIO;
		return rc;
	}

	return __real_vfprintf(stream, fmt, args);
}

int
__wrap_fflush(FILE *stream)
{
	if (mock_fflush_enabled != 0) {
		int error = mock_type(int);

		(void)stream;
		mock_fflush_enabled = 0;
		if (error != 0) {
			errno = error;
			return EOF;
		}
		return 0;
	}

	return __real_fflush(stream);
}
