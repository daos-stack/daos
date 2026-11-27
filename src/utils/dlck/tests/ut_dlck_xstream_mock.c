/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "ut_dlck_xstream_mock.h"

int
dlck_xstream_create(struct dlck_xstream *xs)
{
	int rc;

	check_expected_ptr(xs);
	check_expected(xs->tgt_id);
	rc = mock_type(int);
	if (rc == DER_SUCCESS) {
		xs->xstream = mock_ptr_type(ABT_xstream);
		xs->pool    = mock_ptr_type(ABT_pool);
	}

	return rc;
}

int
dlck_xstream_init(struct dlck_xstream *xs)
{
	check_expected_ptr(xs);

	return mock_type(int);
}

int
dlck_xstream_stop(struct dlck_xstream *xs)
{
	check_expected_ptr(xs);

	return mock_type(int);
}

void
dlck_xstream_stop_all_no_error(struct dlck_xstream *xss, unsigned targets)
{
	check_expected_ptr(xss);
	check_expected(targets);
	function_called();
}
