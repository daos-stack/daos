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

#include "ut_abt_mock.h"

/** Mock handles */
const ABT_mutex  Mock_mutex_handle = (ABT_mutex)0x1234;
const ABT_thread Mock_thread       = (ABT_thread)0x5678;

/** ABT mutex mocks */

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

/** ABT thread, pool, and eventual mocks */

int
__wrap_ABT_thread_join(ABT_thread thread)
{
	check_expected_ptr(thread);

	return mock_type(int);
}

int
__wrap_ABT_thread_free(ABT_thread *thread)
{
	ABT_thread handle;
	int        rc;

	assert_non_null(thread);
	handle = *thread;
	check_expected_ptr(handle);
	rc = mock_type(int);
	if (rc == ABT_SUCCESS) {
		*thread = ABT_THREAD_NULL;
	}

	return rc;
}

int
__wrap_ABT_thread_yield(void)
{
	return mock_type(int);
}

int
__wrap_ABT_pool_is_empty(ABT_pool pool, ABT_bool *is_empty)
{
	int rc;

	check_expected_ptr(pool);
	assert_non_null(is_empty);
	rc = mock_type(int);
	if (rc == ABT_SUCCESS) {
		*is_empty = mock_type(ABT_bool);
	}

	return rc;
}

int
__wrap_ABT_eventual_create(int nbytes, ABT_eventual *neweventual)
{
	int rc;

	assert_non_null(neweventual);
	rc = mock_type(int);
	if (rc == ABT_SUCCESS) {
		*neweventual = mock_ptr_type(ABT_eventual);
	}

	return rc;
}

int
__wrap_ABT_eventual_free(ABT_eventual *eventual)
{
	ABT_eventual handle;
	int          rc;

	assert_non_null(eventual);
	handle = *eventual;
	check_expected_ptr(handle);
	rc = mock_type(int);
	if (rc == ABT_SUCCESS) {
		*eventual = ABT_EVENTUAL_NULL;
	}

	return rc;
}

int
__wrap_ABT_eventual_set(ABT_eventual eventual, void *value, int nbytes)
{
	check_expected_ptr(eventual);

	return mock_type(int);
}

int
__wrap_ABT_eventual_test(ABT_eventual eventual, void **value, ABT_bool *is_ready)
{
	int rc;

	check_expected_ptr(eventual);
	assert_non_null(is_ready);
	rc = mock_type(int);
	if (rc == ABT_SUCCESS) {
		*is_ready = mock_type(ABT_bool);
	}

	return rc;
}
