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

#include "../dlck_engine.h"
#include "ut_dlck_abt_mock.h"

/** dlck_abt.c replacements */

int
dlck_xstream_create(struct dlck_xstream *xs)
{
	int rc;

	check_expected_ptr(xs);
	rc = mock_type(int);
	if (rc == DER_SUCCESS) {
		xs->pool = mock_ptr_type(ABT_pool);
	}

	return rc;
}

int
dlck_xstream_free(struct dlck_xstream *xs)
{
	int rc;

	check_expected_ptr(xs);

	rc = mock_type(int);
	if (rc == DER_SUCCESS) {
		xs->xstream = ABT_XSTREAM_NULL;
	}
	return rc;
}

/**
 * On success the created ULT gets the queued thread handle and is executed synchronously so that
 * all its effects are observable right after the call. The tests have to queue all the values
 * consumed by the ULT body.
 */
int
dlck_ult_create(ABT_pool pool, dlck_ult_func func, void *arg, struct dlck_ult *ult)
{
	int rc;

	check_expected_ptr(pool);
	assert_non_null(func);
	assert_non_null(ult);
	rc = mock_type(int);
	if (rc == DER_SUCCESS) {
		ult->thread = mock_ptr_type(ABT_thread);
		func(arg);
	}

	return rc;
}
