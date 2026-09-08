/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP.
 * Copyright 2026 Google LLC
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>
#include <uuid/uuid.h>
#include <daos_errno.h>

#include "ddb.h"
#include "ddb_vos.h"
#include "ddb_mocks.h"

/*
 * ----------------------------------------------------------------
 * Test Mocks and Stubs
 * ----------------------------------------------------------------
 */

#define COH_COOKIE    0x1515
#define DTX_ID_PTR    ((struct dtx_id *)0x6367)
#define DISCARDED_PTR ((int *)0x9303)

/*
 * Wrapped vos_dtx_discard_invalid() for mocking dv_dtx_active_entry_discard_invalid()'s
 * pass-through call, verifying the expected arguments are forwarded unchanged.
 */
static int
wrap_vos_dtx_discard_invalid_mock(daos_handle_t coh, struct dtx_id *dti, int *discarded)
{
	assert_int_equal(coh.cookie, COH_COOKIE);
	assert_ptr_equal(dti, DTX_ID_PTR);
	assert_ptr_equal(discarded, DISCARDED_PTR);

	return mock_type(int);
}

#define MOCK_DB_PATH       "/mnt/daos"
#define MOCK_POOL_UUID_001 "12345678-1234-1243-1243-123456789001"
#define MOCK_POOL_UUID_002 "12345678-1234-1243-1243-123456789002"
#define MOCK_VOS_PATH      MOCK_DB_PATH "/" MOCK_POOL_UUID_001 "/vos-0"

/*
 * Wrapped ddb_parse_vos_file_parts() for mocking the vos file parsing integration of dv_pool_open()
 * and dv_pool_destroy().
 */
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

	check_expected_ptr(vos_path);
	check_expected_ptr(db_path);
	check_expected_ptr(vos_file_parts);

	rc = mock_type(int);
	if (rc == 0)
		memcpy(vos_file_parts, &mock_vfp, sizeof(mock_vfp));

	return rc;
}

/*
 * Wrapped dwa_can_proceed() for mocking the guard-check integration of dv_pool_open(),
 * dv_pool_destroy(), dv_dev_list(), dv_dev_replace(), dv_run_prov_mem(), and dv_sync_smd().
 */
static int
dwa_can_proceed_mock(struct ddb_ctx *ctx, const char *nvme_conf_dir, bool *can_proceed)
{
	int rc;

	check_expected_ptr(ctx);
	check_expected_ptr(nvme_conf_dir);
	check_expected_ptr(can_proceed);

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
	mock_vos_dtx_discard_invalid_set(wrap_vos_dtx_discard_invalid_mock);
	mock_ddb_parse_vos_file_parts_set(ddb_parse_vos_file_parts_mock);
	mock_dwa_can_proceed_set(dwa_can_proceed_mock);

	return 0;
}

static int
mock_teardown(void **state)
{
	mock_dwa_can_proceed_set(NULL);
	mock_ddb_parse_vos_file_parts_set(NULL);
	mock_vos_dtx_discard_invalid_set(NULL);

	return 0;
}

/*
 * ----------------------------------------------------------------
 * Tests
 * ----------------------------------------------------------------
 */

#define SOME_ERROR (-DER_BAD_CERT)

static void
test_dtx_act_discard_invalid_001(void **state)
{
	daos_handle_t coh = {.cookie = COH_COOKIE};
	int           rc;

	will_return_int(wrap_vos_dtx_discard_invalid_mock, SOME_ERROR);
	rc = dv_dtx_active_entry_discard_invalid(coh, DTX_ID_PTR, DISCARDED_PTR);
	assert_int_equal(rc, SOME_ERROR);

	will_return_int(wrap_vos_dtx_discard_invalid_mock, 0);
	rc = dv_dtx_active_entry_discard_invalid(coh, DTX_ID_PTR, DISCARDED_PTR);
	assert_int_equal(rc, 0);
}

/*
 * When dwa_can_proceed()'s own internal check fails (a real, negative DAOS error, not a refusal),
 * The error is propagated verbatim by dv_pool_open().
 */
static void
test_dv_pool_open_001(void **state)
{
	struct ddb_ctx ctx = {0};
	daos_handle_t  poh;
	int            rc;

	expect_string(ddb_parse_vos_file_parts_mock, vos_path, MOCK_VOS_PATH);
	expect_value(ddb_parse_vos_file_parts_mock, db_path, NULL);
	expect_any(ddb_parse_vos_file_parts_mock, vos_file_parts);
	will_return_int(ddb_parse_vos_file_parts_mock, 0);

	expect_any(dwa_can_proceed_mock, ctx);
	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	expect_any(dwa_can_proceed_mock, can_proceed);
	will_return_int(dwa_can_proceed_mock, -DER_NOMEM);

	rc = dv_pool_open(MOCK_VOS_PATH, NULL, &ctx, &poh, 0, false);
	assert_int_equal(rc, -DER_NOMEM);
}

/*
 * When dwa_can_proceed() returns false, dv_pool_open() returns -DER_NO_SERVICE.
 */
static void
test_dv_pool_open_002(void **state)
{
	struct ddb_ctx ctx = {0};
	daos_handle_t  poh;
	int            rc;

	expect_string(ddb_parse_vos_file_parts_mock, vos_path, MOCK_VOS_PATH);
	expect_value(ddb_parse_vos_file_parts_mock, db_path, NULL);
	expect_any(ddb_parse_vos_file_parts_mock, vos_file_parts);
	will_return_int(ddb_parse_vos_file_parts_mock, 0);

	expect_any(dwa_can_proceed_mock, ctx);
	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	expect_any(dwa_can_proceed_mock, can_proceed);
	will_return_int(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = dv_pool_open(MOCK_VOS_PATH, NULL, &ctx, &poh, 0, false);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/*
 * As for dv_pool_open() with no flags, opening a pool with the VOS_POF_FOR_FEATURE_FLAG flag (as
 * ddb_run_feature() does when no pool is already open in ctx) must check the guard against the
 * db_path derived from the VOS path via ddb_parse_vos_file_parts().
 */
static void
test_dv_pool_open_003(void **state)
{
	struct ddb_ctx ctx = {0};
	daos_handle_t  poh;
	int            rc;

	expect_string(ddb_parse_vos_file_parts_mock, vos_path, MOCK_VOS_PATH);
	expect_value(ddb_parse_vos_file_parts_mock, db_path, NULL);
	expect_any(ddb_parse_vos_file_parts_mock, vos_file_parts);
	will_return_int(ddb_parse_vos_file_parts_mock, 0);

	expect_any(dwa_can_proceed_mock, ctx);
	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	expect_any(dwa_can_proceed_mock, can_proceed);
	will_return_int(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = dv_pool_open(MOCK_VOS_PATH, NULL, &ctx, &poh, VOS_POF_FOR_FEATURE_FLAG, false);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/*
 * When dwa_can_proceed()'s own internal check fails (a real, negative DAOS error, not a refusal),
 * The error is propagated verbatim by dv_pool_destroy().
 */
static void
test_dv_pool_destroy_001(void **state)
{
	struct ddb_ctx ctx = {0};
	int            rc;

	expect_string(ddb_parse_vos_file_parts_mock, vos_path, MOCK_VOS_PATH);
	expect_value(ddb_parse_vos_file_parts_mock, db_path, NULL);
	expect_any(ddb_parse_vos_file_parts_mock, vos_file_parts);
	will_return_int(ddb_parse_vos_file_parts_mock, 0);

	expect_any(dwa_can_proceed_mock, ctx);
	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	expect_any(dwa_can_proceed_mock, can_proceed);
	will_return_int(dwa_can_proceed_mock, -DER_NOMEM);

	rc = dv_pool_destroy(MOCK_VOS_PATH, NULL, &ctx);
	assert_int_equal(rc, -DER_NOMEM);
}

/*
 * When dwa_can_proceed() returns false, dv_pool_destroy() returns -DER_NO_SERVICE.
 */
static void
test_dv_pool_destroy_002(void **state)
{
	struct ddb_ctx ctx = {0};
	int            rc;

	expect_string(ddb_parse_vos_file_parts_mock, vos_path, MOCK_VOS_PATH);
	expect_value(ddb_parse_vos_file_parts_mock, db_path, NULL);
	expect_any(ddb_parse_vos_file_parts_mock, vos_file_parts);
	will_return_int(ddb_parse_vos_file_parts_mock, 0);

	expect_any(dwa_can_proceed_mock, ctx);
	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	expect_any(dwa_can_proceed_mock, can_proceed);
	will_return_int(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = dv_pool_destroy(MOCK_VOS_PATH, NULL, &ctx);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/*
 * When dwa_can_proceed()'s own internal check fails (a real, negative DAOS error, not a refusal),
 * The error is propagated verbatim by dv_dev_list().
 */
static void
test_dv_dev_list_001(void **state)
{
	struct ddb_ctx ctx = {0};
	d_list_t       dev_list;
	int            dev_cnt = 0;
	int            rc;

	D_INIT_LIST_HEAD(&dev_list);

	expect_any(dwa_can_proceed_mock, ctx);
	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	expect_any(dwa_can_proceed_mock, can_proceed);
	will_return_int(dwa_can_proceed_mock, -DER_NOMEM);

	rc = dv_dev_list(MOCK_DB_PATH, &ctx, &dev_list, &dev_cnt);
	assert_int_equal(rc, -DER_NOMEM);
}

/*
 * When dwa_can_proceed() returns false, dv_dev_list() returns -DER_NO_SERVICE.
 */
static void
test_dv_dev_list_002(void **state)
{
	struct ddb_ctx ctx = {0};
	d_list_t       dev_list;
	int            dev_cnt = 0;
	int            rc;

	D_INIT_LIST_HEAD(&dev_list);

	expect_any(dwa_can_proceed_mock, ctx);
	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	expect_any(dwa_can_proceed_mock, can_proceed);
	will_return_int(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = dv_dev_list(MOCK_DB_PATH, &ctx, &dev_list, &dev_cnt);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/*
 * When dwa_can_proceed()'s own internal check fails (a real, negative DAOS error, not a refusal),
 * The error is propagated verbatim by dv_dev_replace().
 */
static void
test_dv_dev_replace_001(void **state)
{
	struct ddb_ctx ctx = {0};
	uuid_t         old_devid, new_devid;
	int            rc;

	uuid_parse(MOCK_POOL_UUID_001, old_devid);
	uuid_parse(MOCK_POOL_UUID_002, new_devid);

	expect_any(dwa_can_proceed_mock, ctx);
	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	expect_any(dwa_can_proceed_mock, can_proceed);
	will_return_int(dwa_can_proceed_mock, -DER_NOMEM);

	rc = dv_dev_replace(MOCK_DB_PATH, &ctx, old_devid, new_devid);
	assert_int_equal(rc, -DER_NOMEM);
}

/*
 * When dwa_can_proceed() returns false, dv_dev_replace() returns -DER_NO_SERVICE.
 */
static void
test_dv_dev_replace_002(void **state)
{
	struct ddb_ctx ctx = {0};
	uuid_t         old_devid, new_devid;
	int            rc;

	uuid_parse(MOCK_POOL_UUID_001, old_devid);
	uuid_parse(MOCK_POOL_UUID_002, new_devid);

	expect_any(dwa_can_proceed_mock, ctx);
	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	expect_any(dwa_can_proceed_mock, can_proceed);
	will_return_int(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = dv_dev_replace(MOCK_DB_PATH, &ctx, old_devid, new_devid);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/*
 * When dwa_can_proceed()'s own internal check fails (a real, negative DAOS error, not a refusal),
 * The error is propagated verbatim by dv_run_prov_mem().
 */
static void
test_dv_run_prov_mem_001(void **state)
{
	struct ddb_ctx ctx = {0};
	int            rc;

	expect_any(dwa_can_proceed_mock, ctx);
	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	expect_any(dwa_can_proceed_mock, can_proceed);
	will_return_int(dwa_can_proceed_mock, -DER_NOMEM);

	rc = dv_run_prov_mem(MOCK_DB_PATH, &ctx, "/mnt/tmpfs", 0);
	assert_int_equal(rc, -DER_NOMEM);
}

/*
 * When dwa_can_proceed() returns false, dv_run_prov_mem() returns -DER_NO_SERVICE.
 */
static void
test_dv_run_prov_mem_002(void **state)
{
	struct ddb_ctx ctx = {0};
	int            rc;

	expect_any(dwa_can_proceed_mock, ctx);
	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	expect_any(dwa_can_proceed_mock, can_proceed);
	will_return_int(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = dv_run_prov_mem(MOCK_DB_PATH, &ctx, "/mnt/tmpfs", 0);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/*
 * When dwa_can_proceed()'s own internal check fails (a real, negative DAOS error, not a refusal),
 * The error is propagated verbatim by dv_sync_smd().
 */
static void
test_dv_sync_smd_001(void **state)
{
	struct ddb_ctx ctx = {0};
	int            rc;

	expect_any(dwa_can_proceed_mock, ctx);
	expect_value(dwa_can_proceed_mock, nvme_conf_dir, NULL);
	expect_any(dwa_can_proceed_mock, can_proceed);
	will_return_int(dwa_can_proceed_mock, -DER_NOMEM);

	rc = dv_sync_smd("/configs/engine.json", "/mnt/daos_without_a_daos_nvme_conf", &ctx, NULL,
			 NULL);
	assert_int_equal(rc, -DER_NOMEM);
}

/*
 * When dwa_can_proceed() returns false, dv_sync_smd() returns -DER_NO_SERVICE.
 */
static void
test_dv_sync_smd_002(void **state)
{
	struct ddb_ctx ctx = {0};
	int            rc;

	expect_any(dwa_can_proceed_mock, ctx);
	expect_value(dwa_can_proceed_mock, nvme_conf_dir, NULL);
	expect_any(dwa_can_proceed_mock, can_proceed);
	will_return_int(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = dv_sync_smd("/configs/engine.json", "/mnt/daos_without_a_daos_nvme_conf", &ctx, NULL,
			 NULL);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

#define TEST(x) {#x, test_##x, NULL, NULL}

const struct CMUnitTest dv_test_cases[] = {
    TEST(dtx_act_discard_invalid_001),
    TEST(dv_pool_open_001),
    TEST(dv_pool_open_002),
    TEST(dv_pool_open_003),
    TEST(dv_pool_destroy_001),
    TEST(dv_pool_destroy_002),
    TEST(dv_dev_list_001),
    TEST(dv_dev_list_002),
    TEST(dv_dev_replace_001),
    TEST(dv_dev_replace_002),
    TEST(dv_run_prov_mem_001),
    TEST(dv_run_prov_mem_002),
    TEST(dv_sync_smd_001),
    TEST(dv_sync_smd_002),
};

int
ddb_vos_ut_run()
{
	return cmocka_run_group_tests_name("DDB VOS Interface Unit Tests", dv_test_cases,
					   mock_setup, mock_teardown);
}
