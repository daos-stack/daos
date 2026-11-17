/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <abt.h>

#include "dlck_ut_abt_mock.h"

const ABT_mutex Mock_mutex_handle = (ABT_mutex)0x1234;

int
__wrap_ABT_mutex_create(ABT_mutex *newmutex)
{
	int rc;

	check_expected_ptr(newmutex);
	rc = mock_type(int);
	if (rc == ABT_SUCCESS) {
		assert_non_null(newmutex);
		*newmutex = mock_ptr_type(ABT_mutex);
	}
	return rc;
}

int
__wrap_ABT_mutex_free(ABT_mutex *mutex)
{
	ABT_mutex handle;
	int       rc;

	check_expected_ptr(mutex);
	assert_non_null(mutex);
	handle = *mutex;
	check_expected_ptr(handle);
	rc = mock_type(int);
	if (rc == ABT_SUCCESS) {
		*mutex = ABT_MUTEX_NULL;
	}
	return rc;
}

int
__wrap_ABT_mutex_lock(ABT_mutex mutex)
{
	check_expected_ptr(mutex);
	return mock_type(int);
}

int
__wrap_ABT_mutex_unlock(ABT_mutex mutex)
{
	check_expected_ptr(mutex);
	return mock_type(int);
}
