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

#define COH_COOKIE    0x1515
#define DTX_ID_PTR    ((struct dtx_id *)0x6367)
#define DISCARDED_PTR ((int *)0x9303)

/*
 * ----------------------------------------------------------------
 * Test Mocks and Stubs
 * ----------------------------------------------------------------
 */

int
__wrap_vos_dtx_discard_invalid(daos_handle_t coh, struct dtx_id *dti, int *discarded)
{
	assert_int_equal(coh.cookie, COH_COOKIE);
	assert_ptr_equal(dti, DTX_ID_PTR);
	assert_ptr_equal(discarded, DISCARDED_PTR);

	return mock_type(int);
}

#define SOME_ERROR (-DER_BAD_CERT)

static void
dtx_act_discard_invalid_test(void **state)
{
	daos_handle_t coh = {.cookie = COH_COOKIE};
	int           rc;

	will_return_int(__wrap_vos_dtx_discard_invalid, SOME_ERROR);
	rc = dv_dtx_active_entry_discard_invalid(coh, DTX_ID_PTR, DISCARDED_PTR);
	assert_int_equal(rc, SOME_ERROR);

	will_return_int(__wrap_vos_dtx_discard_invalid, 0);
	rc = dv_dtx_active_entry_discard_invalid(coh, DTX_ID_PTR, DISCARDED_PTR);
	assert_int_equal(rc, 0);
}

/*
 * Wrapped dwa_can_proceed() for mocking the guard-check integration of dv_pool_open(),
 * dv_pool_destroy(), dv_dev_list(), dv_dev_replace(), dv_run_prov_mem(), and dv_sync_smd().
 * check_expected()/mock_type() key off this function's own name (__func__), not the
 * __wrap_dwa_can_proceed() symbol that delegates to it -- so tests must queue expectations
 * via will_return(dwa_can_proceed_mock, ...)/expect_*(dwa_can_proceed_mock, ...). rc is always
 * queued; can_proceed is only read (and must only be queued) when simulating rc == 0, matching
 * dwa_can_proceed()'s own contract that *can_proceed is unspecified on error.
 */
static int
dwa_can_proceed_mock(struct ddb_ctx *ctx, const char *nvme_conf_dir, bool *can_proceed)
{
	int rc;

	check_expected(nvme_conf_dir);
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
mock_dwa_can_proceed_setup(void **state)
{
	mock_dwa_can_proceed_set(dwa_can_proceed_mock);

	return 0;
}

static int
mock_dwa_can_proceed_teardown(void **state)
{
	mock_dwa_can_proceed_set(NULL);

	return 0;
}

/*
 * ----------------------------------------------------------------
 * Tests
 * ----------------------------------------------------------------
 */

#define MOCK_DB_PATH       "/mnt/daos"
#define MOCK_POOL_UUID_001 "12345678-1234-1243-1243-123456789001"
#define MOCK_POOL_UUID_002 "12345678-1234-1243-1243-123456789002"
#define MOCK_VOS_PATH      MOCK_DB_PATH "/" MOCK_POOL_UUID_001 "/vos-0"

/*
 * Regression test: when dwa_can_proceed()'s own internal check fails (a real, negative DAOS
 * error, not a refusal), dv_pool_open()'s goto-based error handling must propagate that exact
 * rc, not silently convert it to -DER_NO_SERVICE.
 */
static void
open_propagates_check_error_test(void **state)
{
	struct ddb_ctx ctx = {0};
	daos_handle_t  poh;
	int            rc;

	expect_any(dwa_can_proceed_mock, nvme_conf_dir);
	will_return(dwa_can_proceed_mock, -DER_NOMEM);

	rc = dv_pool_open(MOCK_POOL_UUID_001 "/vos-0", NULL, &ctx, &poh, 0, false);
	assert_int_equal(rc, -DER_NOMEM);
}

/*
 * dv_pool_open() must check the guard against the db_path derived from the VOS path's
 * directory prefix, not against a raw, omitted db_path.
 */
static void
open_uses_derived_db_path_test(void **state)
{
	struct ddb_ctx ctx = {0};
	daos_handle_t  poh;
	int            rc;

	/*
	 * The VOS path has a directory prefix ("/mnt/daos2"), so parse_db_path() derives
	 * db_path="/mnt/daos2" from it (verified directly against ddb_parse.c's regex).
	 */
	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	will_return(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = dv_pool_open(MOCK_VOS_PATH, NULL, &ctx, &poh, 0, false);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/*
 * As for dv_pool_open(), opening a pool with the VOS_POF_FOR_FEATURE_FLAG flag (as
 * ddb_run_feature() does when no pool is already open in ctx) must check the guard against the
 * db_path derived from the VOS path's directory prefix.
 */
static void
feature_uses_derived_db_path_test(void **state)
{
	struct ddb_ctx ctx = {0};
	daos_handle_t  poh;
	int            rc;

	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	will_return(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = dv_pool_open(MOCK_VOS_PATH, NULL, &ctx, &poh, VOS_POF_FOR_FEATURE_FLAG, false);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/*
 * Symmetric regression test for dv_pool_destroy(), which derives db_path from the VOS path the
 * same way dv_pool_open() does.
 */
static void
rm_pool_uses_derived_db_path_test(void **state)
{
	struct ddb_ctx ctx = {0};
	int            rc;

	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	will_return(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = dv_pool_destroy(MOCK_VOS_PATH, NULL, &ctx);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/*
 * dv_dev_list() takes db_path directly (no VOS-path derivation step): confirmed by inspection
 * that the exact same value is checked by the guard and used for the real vos_self_init() call.
 * This test locks in that property so a future change can't silently regress it.
 */
static void
dev_list_uses_given_db_path_test(void **state)
{
	struct ddb_ctx ctx = {0};
	d_list_t       dev_list;
	int            dev_cnt = 0;
	int            rc;

	D_INIT_LIST_HEAD(&dev_list);

	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	will_return(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = dv_dev_list(MOCK_DB_PATH, &ctx, &dev_list, &dev_cnt);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/*
 * Regression test: dv_dev_list()'s direct-return-style error handling (as opposed to
 * dv_pool_open()'s goto-based style) must also propagate dwa_can_proceed()'s own internal
 * check error verbatim, not silently convert it to -DER_NO_SERVICE.
 */
static void
dev_list_propagates_check_error_test(void **state)
{
	struct ddb_ctx ctx = {0};
	d_list_t       dev_list;
	int            dev_cnt = 0;
	int            rc;

	D_INIT_LIST_HEAD(&dev_list);

	expect_any(dwa_can_proceed_mock, nvme_conf_dir);
	will_return(dwa_can_proceed_mock, -DER_NOMEM);

	rc = dv_dev_list(MOCK_DB_PATH, &ctx, &dev_list, &dev_cnt);
	assert_int_equal(rc, -DER_NOMEM);
}

/*
 * dv_dev_replace() takes db_path directly, same property as dev_list. old_devid/new_devid must
 * be valid, distinct UUIDs for execution to reach the guard call.
 */
static void
dev_replace_uses_given_db_path_test(void **state)
{
	struct ddb_ctx ctx = {0};
	uuid_t         old_devid, new_devid;
	int            rc;

	uuid_parse(MOCK_POOL_UUID_001, old_devid);
	uuid_parse(MOCK_POOL_UUID_002, new_devid);

	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	will_return(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = dv_dev_replace(MOCK_DB_PATH, &ctx, old_devid, new_devid);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/*
 * dv_run_prov_mem() takes db_path directly, same property as dev_list.
 */
static void
prov_mem_uses_given_db_path_test(void **state)
{
	struct ddb_ctx ctx = {0};
	int            rc;

	expect_string(dwa_can_proceed_mock, nvme_conf_dir, MOCK_DB_PATH);
	will_return(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = dv_run_prov_mem(MOCK_DB_PATH, &ctx, "/mnt/tmpfs", 0);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/*
 * Unlike dev_list/dev_replace/prov_mem, dv_sync_smd() checks the guard unconditionally
 * (nvme_conf_dir == NULL), since nvme_conf (its SPDK-driving argument) is independent of
 * db_path. Deliberately mismatched nvme_conf/db_path values prove the guard still fires
 * regardless. The guard check is the first thing dv_sync_smd() does, before
 * vos_self_init_ext()/smd_init(), so this test doesn't need to mock those calls.
 */
static void
smd_sync_checks_guard_unconditionally_test(void **state)
{
	struct ddb_ctx ctx = {0};
	int            rc;

	expect_value(dwa_can_proceed_mock, nvme_conf_dir, 0);
	will_return(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = dv_sync_smd("/configs/engine.json", "/mnt/daos_without_a_daos_nvme_conf", &ctx, NULL,
			 NULL);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

#define TEST(x)                                                                                    \
	{                                                                                          \
		#x, x##_test, NULL, NULL                                                           \
	}

const struct CMUnitTest dv_test_cases[] = {
    TEST(dtx_act_discard_invalid),         TEST(open_propagates_check_error),
    TEST(open_uses_derived_db_path),       TEST(feature_uses_derived_db_path),
    TEST(rm_pool_uses_derived_db_path),    TEST(dev_list_uses_given_db_path),
    TEST(dev_list_propagates_check_error), TEST(dev_replace_uses_given_db_path),
    TEST(prov_mem_uses_given_db_path),     TEST(smd_sync_checks_guard_unconditionally),
};

int
ddb_vos_ut_run()
{
	return cmocka_run_group_tests_name("DDB VOS Interface Unit Tests", dv_test_cases,
					   mock_dwa_can_proceed_setup,
					   mock_dwa_can_proceed_teardown);
}
