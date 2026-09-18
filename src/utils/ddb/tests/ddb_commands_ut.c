/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "ddb.h"
#include "ddb_cmocka.h"
#include "ddb_fake_print.h"

/*
 * ----------------------------------------------------------------
 * Tests
 * ----------------------------------------------------------------
 */

/**
 * ddb_run_version must print a version string and return 0.
 */
static void
version_test(void **state)
{
	struct ddb_ctx ctx = {0};
	int            rc;

	ctx.dc_io_ft.ddb_print_message = fake_print;
	fake_print_reset();

	rc = ddb_run_version(&ctx);
	assert_int_equal(rc, 0);
	assert_string_contains(fake_print_buf, "ddb version");
}

/**
 * ddb_run_close must return 0 when no pool is open (handle is invalid).
 */
static void
close_not_open_test(void **state)
{
	struct ddb_ctx ctx = {0};
	int            rc;

	ctx.dc_poh = DAOS_HDL_INVAL;

	rc = ddb_run_close(&ctx);
	assert_int_equal(rc, 0);
}

/*
 * ----------------------------------------------------------------
 * Suite registration
 * ----------------------------------------------------------------
 */
#define TEST(x) {#x, x##_test, NULL, NULL}

static const struct CMUnitTest ddb_commands_ut_cases[] = {
    TEST(version),
    TEST(close_not_open),
};

int
ddb_commands_ut_run(void)
{
	return cmocka_run_group_tests_name("DDB Commands Unit Tests", ddb_commands_ut_cases, NULL,
					   NULL);
}
