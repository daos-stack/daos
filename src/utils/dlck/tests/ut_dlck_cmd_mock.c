/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "ut_dlck_cmd_mock.h"

void
mock_exec_one(void *arg)
{
	check_expected_ptr(arg);
}

int
mock_arg_alloc_fn(struct dlck_engine *engine, int idx, void *custom, void **arg)
{
	int rc;

	check_expected_ptr(engine);
	check_expected(idx);
	check_expected_ptr(custom);
	assert_non_null(arg);
	rc = mock_type(int);
	if (rc == DER_SUCCESS) {
		*arg = mock_ptr_type(void *);
	}

	return rc;
}

void
mock_arg_free_fn(void *custom, void **arg)
{
	void *ptr;

	check_expected_ptr(custom);
	assert_non_null(arg);
	ptr = *arg;
	check_expected_ptr(ptr);
	*arg = NULL;
}
