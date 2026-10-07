/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>
#include <uuid/uuid.h>
#include <daos_errno.h>

#include "ddb.h"
#include "ddb_wal.h"
#include "ddb_mocks.h"

/*
 * ----------------------------------------------------------------
 * Test Mocks and Stubs
 * ----------------------------------------------------------------
 */

#define MOCK_DB_PATH   "/mnt/daos"
#define MOCK_POOL_UUID "12345678-1234-1243-1243-123456789001"
#define MOCK_VOS_PATH  MOCK_DB_PATH "/" MOCK_POOL_UUID "/vos-0"

/* Wrapped ddb_parse_vos_file_parts() for mocking the VOS target path parsing of dw_open(). */
static int
ddb_parse_vos_file_parts_mock(const char *vos_path, const char *db_path,
			      struct vos_file_parts *vos_file_parts)
{
	int                                rc;
	static const struct vos_file_parts mock_vfp = {
	    .vf_vos_file_name = "vos-0",
	    .vf_vos_file_path = MOCK_VOS_PATH,
	    .vf_db_path       = MOCK_DB_PATH,
	    .vf_pool_uuid = {0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x12, 0x43, 0x12, 0x43, 0x12, 0x34,
			     0x56, 0x78, 0x90, 0x01},
	    .vf_target_idx = 0,
	};

	assert_ptr_equal(vos_path, MOCK_VOS_PATH);
	assert_ptr_equal(db_path, NULL);
	assert_non_null(vos_file_parts);

	rc = mock_type(int);
	if (rc == 0)
		memcpy(vos_file_parts, &mock_vfp, sizeof(mock_vfp));

	return rc;
}

/* Wrapped dwa_can_proceed() for mocking the SPDK re-initialization guard check of dw_open(). */
static int
dwa_can_proceed_mock(struct ddb_ctx *ctx, const char *nvme_conf_dir, bool *can_proceed)
{
	int rc;

	check_expected_ptr(ctx);
	check_expected_ptr(nvme_conf_dir);
	assert_non_null(can_proceed);

	rc = mock_type(int);
	if (rc == 0)
		*can_proceed = mock_type(bool);

	return rc;
}

/*
 * ----------------------------------------------------------------
 * Setup & Teardown functions
 * ----------------------------------------------------------------
 */

static int
mock_setup(void **state)
{
	mock_ddb_parse_vos_file_parts_set(ddb_parse_vos_file_parts_mock);
	mock_dwa_can_proceed_set(dwa_can_proceed_mock);

	return 0;
}

static int
mock_teardown(void **state)
{
	mock_dwa_can_proceed_set(NULL);
	mock_ddb_parse_vos_file_parts_set(NULL);

	return 0;
}

/*
 * ----------------------------------------------------------------
 * Tests
 * ----------------------------------------------------------------
 */

/*
 * A VOS path parsing error is propagated verbatim by dw_open(), before the guard is checked.
 */
static void
test_dw_open_001(void **state)
{
	struct ddb_ctx           ctx = {0};
	struct bio_meta_context *mc  = NULL;
	int                      rc;

	will_return_int(ddb_parse_vos_file_parts_mock, -DER_INVAL);

	rc = dw_open(MOCK_VOS_PATH, NULL, &ctx, &mc);
	assert_int_equal(rc, -DER_INVAL);
	assert_null(mc);
}

/*
 * When dwa_can_proceed()'s own internal check fails (a real, negative DAOS error, not a refusal),
 * the error is propagated verbatim by dw_open().
 */
static void
test_dw_open_002(void **state)
{
	struct ddb_ctx           ctx = {0};
	struct bio_meta_context *mc  = NULL;
	int                      rc;

	will_return_int(ddb_parse_vos_file_parts_mock, 0);

	expect_value(dwa_can_proceed_mock, ctx, &ctx);
	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	will_return_int(dwa_can_proceed_mock, -DER_NOMEM);

	rc = dw_open(MOCK_VOS_PATH, NULL, &ctx, &mc);
	assert_int_equal(rc, -DER_NOMEM);
	assert_null(mc);
}

/*
 * When dwa_can_proceed() returns false, dw_open() returns -DER_NO_SERVICE without initializing
 * VOS.
 */
static void
test_dw_open_003(void **state)
{
	struct ddb_ctx           ctx = {0};
	struct bio_meta_context *mc  = NULL;
	int                      rc;

	will_return_int(ddb_parse_vos_file_parts_mock, 0);

	expect_value(dwa_can_proceed_mock, ctx, &ctx);
	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	will_return_int(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = dw_open(MOCK_VOS_PATH, NULL, &ctx, &mc);
	assert_int_equal(rc, -DER_NO_SERVICE);
	assert_null(mc);
}

#define TEST(x) {#x, test_##x, NULL, NULL}

const struct CMUnitTest dw_test_cases[] = {
    TEST(dw_open_001),
    TEST(dw_open_002),
    TEST(dw_open_003),
};

int
ddb_wal_ut_run()
{
	return cmocka_run_group_tests_name("DDB WAL Interface Unit Tests", dw_test_cases,
					   mock_setup, mock_teardown);
}
