/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include <abt.h>
#include <cmocka.h>

#include "dlck_ut_abt_mock.h"

/** sentinels */
const ABT_mutex    Mock_mutex_handle = (ABT_mutex)0x1234;
const ABT_pool     Mock_pool         = (ABT_pool)0x1234;
const ABT_thread   Mock_thread       = (ABT_thread)0x5678;
const ABT_eventual Mock_eventual     = (ABT_eventual)0x9abc;

/** ABT mutex mocks */

int
__wrap_ABT_mutex_create(ABT_mutex *newmutex)
{
	check_expected_ptr(newmutex);

	int rc = mock_type(int);

	if (rc == ABT_SUCCESS) {
		assert_non_null(newmutex);
		*newmutex = mock_ptr_type(ABT_mutex);
	}

	return rc;
}

int
__wrap_ABT_mutex_free(ABT_mutex *mutex)
{
	check_expected_ptr(mutex);
	assert_non_null(mutex);
	ABT_mutex handle = *mutex;
	check_expected_ptr(handle);

	int rc = mock_type(int);

	if (rc == ABT_SUCCESS)
		*mutex = ABT_MUTEX_NULL;

	return rc;
}

int
__wrap_ABT_mutex_lock(ABT_mutex mutex)
{
	check_expected_ptr(mutex);

	int rc = mock_type(int);

	return rc;
}

int
__wrap_ABT_mutex_unlock(ABT_mutex mutex)
{
	check_expected_ptr(mutex);

	int rc = mock_type(int);

	return rc;
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
	assert_non_null(thread);

	ABT_thread handle = *thread;

	check_expected_ptr(handle);

	int rc = mock_type(int);

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

/**
 * The production code asserts on the return code so only ABT_SUCCESS is sensible here.
 * The emptiness result is always queue-controlled.
 */
int
__wrap_ABT_pool_is_empty(ABT_pool pool, ABT_bool *is_empty)
{
	check_expected_ptr(pool);
	assert_non_null(is_empty);
	*is_empty = mock_type(ABT_bool);

	return ABT_SUCCESS;
}

int
__wrap_ABT_eventual_create(int nbytes, ABT_eventual *neweventual)
{
	assert_non_null(neweventual);

	int rc = mock_type(int);

	if (rc == ABT_SUCCESS) {
		*neweventual = mock_ptr_type(ABT_eventual);
	}

	return rc;
}

int
__wrap_ABT_eventual_free(ABT_eventual *eventual)
{
	assert_non_null(eventual);

	ABT_eventual handle = *eventual;

	check_expected_ptr(handle);

	int rc = mock_type(int);

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
	check_expected_ptr(eventual);
	assert_non_null(is_ready);

	int rc = mock_type(int);

	if (rc == ABT_SUCCESS) {
		*is_ready = mock_type(ABT_bool);
	}

	return rc;
}

/** dlck_abt.c replacements */

int
dlck_xstream_create(struct dlck_xstream *xs)
{
	check_expected_ptr(xs);

	int rc = mock_type(int);

	if (rc == DER_SUCCESS) {
		xs->pool = mock_ptr_type(ABT_pool);
	}

	return rc;
}

int
dlck_xstream_free(struct dlck_xstream *xs)
{
	check_expected_ptr(xs);

	return mock_type(int);
}

/**
 * On success the created ULT is executed synchronously so that all its effects are observable
 * right after the call. The tests have to queue all the values consumed by the ULT body.
 */
int
dlck_ult_create(ABT_pool pool, dlck_ult_func func, void *arg, struct dlck_ult *ult)
{
	check_expected_ptr(pool);
	assert_non_null(func);
	assert_non_null(ult);

	int rc = mock_type(int);

	if (rc == DER_SUCCESS) {
		ult->thread = Mock_thread;
		func(arg);
	}

	return rc;
}
