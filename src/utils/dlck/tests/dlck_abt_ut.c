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

#define MOCK_XSTREAM ((ABT_xstream)(uintptr_t)0x4000)
#define MOCK_POOL    ((ABT_pool)(uintptr_t)0x1000)

/** ABT_xstream_free mock */

int
__wrap_ABT_xstream_free(ABT_xstream *xstream)
{
	int rc;

	check_expected_ptr(xstream);
	rc = mock_type(int);
	if (rc == ABT_SUCCESS) {
		*xstream = ABT_XSTREAM_NULL;
	}

	return rc;
}

/**
 * Real dlck_xstream_free() must release the xstream and keep the pool handle, which is owned by
 * the ABT runtime and is not released here.
 */
static void
test_dlck_xstream_free_keeps_pool(void **state)
{
	struct dlck_xstream xs = {.xstream = MOCK_XSTREAM, .pool = MOCK_POOL};
	int                 rc;

	expect_value(__wrap_ABT_xstream_free, xstream, &xs.xstream);
	will_return(__wrap_ABT_xstream_free, ABT_SUCCESS);

	rc = dlck_xstream_free(&xs);
	assert_int_equal(rc, DER_SUCCESS);
	assert_ptr_equal(xs.xstream, ABT_XSTREAM_NULL);
	assert_ptr_equal(xs.pool, MOCK_POOL);
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
	    cmocka_unit_test(test_dlck_xstream_free_keeps_pool),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
