/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include <daos_errno.h>
#include "ddb.h"
#include "ddb_common.h"

/* ----------------------------------------------------------------
 * Wrapped vmd_wa_can_proceed
 * ----------------------------------------------------------------
 * The real function lives in ddb_vmd_wa.c but is intercepted by the linker's
 * --wrap mechanism.  __real_vmd_wa_can_proceed is the original symbol.
 */
bool
__real_vmd_wa_can_proceed(struct ddb_ctx *ctx, const char *db_path);

bool
__wrap_vmd_wa_can_proceed(struct ddb_ctx *ctx, const char *db_path)
{
	return mock_type(bool);
}

/* ----------------------------------------------------------------
 * Fake print helpers (minimal, just for capturing output in these tests)
 * ---------------------------------------------------------------- */
#define PRINT_BUF_SIZE 4096
static char g_print_buf[PRINT_BUF_SIZE];

static int
fake_print(const char *fmt, ...)
{
	va_list ap;
	size_t  offset = strlen(g_print_buf);
	size_t  left   = PRINT_BUF_SIZE - offset;

	va_start(ap, fmt);
	vsnprintf(g_print_buf + offset, left, fmt, ap);
	va_end(ap);

	return 0;
}

static void
fake_print_reset(void)
{
	memset(g_print_buf, 0, sizeof(g_print_buf));
}

/* ----------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------- */

/**
 * ddb_run_open must return -DER_NO_SERVICE when vmd_wa_can_proceed returns
 * false (i.e. DDB_CAN_PROCEED macro fires).
 */
static void
open_can_proceed_failure_test(void **state)
{
	struct ddb_ctx      ctx = {0};
	struct open_options opt = {0};
	int                 rc;

	ctx.dc_io_ft.ddb_print_message = fake_print;
	ctx.dc_io_ft.ddb_print_error   = fake_print;

	opt.path = "/some/path/vos-0";

	/* Make vmd_wa_can_proceed return false */
	will_return(__wrap_vmd_wa_can_proceed, false);

	rc = ddb_run_open(&ctx, &opt);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

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
	assert_non_null(strstr(g_print_buf, "ddb version"));
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

/* ----------------------------------------------------------------
 * Suite registration
 * ---------------------------------------------------------------- */
#define TEST(x) {#x, x##_test, NULL, NULL}

static const struct CMUnitTest ddb_commands_ut_cases[] = {
    TEST(open_can_proceed_failure),
    TEST(version),
    TEST(close_not_open),
};

int
ddb_commands_ut_run(void)
{
	return cmocka_run_group_tests_name("DDB Commands Unit Tests", ddb_commands_ut_cases, NULL,
					   NULL);
}
