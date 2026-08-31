/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_errno.h>
#include <daos_srv/vos.h>
#include <daos_srv/smd.h>
#include "ddb.h"
#include "ddb_common.h"
#include "ddb_cmocka.h"
#include "ddb_mocks.h"
#include "ddb_fake_print.h"

/* ----------------------------------------------------------------
 * Wrapped dwa_can_proceed() for mocking in ddb_commands_ut.c
 * ----------------------------------------------------------------
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

static int
ddb_commands_ut_setup(void **state)
{
	mock_dwa_can_proceed_setup(dwa_can_proceed_mock);

	return 0;
}

static int
ddb_commands_ut_teardown(void **state)
{
	mock_dwa_can_proceed_teardown();

	return 0;
}

/* ----------------------------------------------------------------
 * Wrapped VOS/SMD init-and-teardown calls, used only by smd_sync tests
 * ----------------------------------------------------------------
 * Needed because dv_sync_smd()'s guard check is placed after a real vos_self_init_ext()/smd_init(),
 * so the test must call those two functions to reach the guard.
 */
int
__wrap_vos_self_init_ext(const char *db_path, bool use_sys_db, int tgt_id, bool init_spdk)
{
	return mock_type(int);
}

void
__wrap_vos_self_fini(void)
{
}

int
__wrap_smd_init(struct sys_db *db)
{
	return mock_type(int);
}

void
__wrap_smd_fini(void)
{
}

/* ----------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------- */

/**
 * ddb_run_open must return -DER_NO_SERVICE when dwa_can_proceed (called from
 * dv_pool_open(), after the VOS path has been resolved into a db_path) returns false.
 */
static void
open_can_proceed_failure_test(void **state)
{
	struct ddb_ctx      ctx = {0};
	struct open_options opt = {0};
	int                 rc;

	ctx.dc_io_ft.ddb_print_message = fake_print;
	ctx.dc_io_ft.ddb_print_error   = fake_print;

	/* Must be a well-formed VOS path so create_vos_file_parts() succeeds and execution reaches
	 * the wrapped dwa_can_proceed() call inside dv_pool_open().
	 */
	opt.path = "123e4567-e89b-12d3-a456-426614174000/vos-0";

	/* Make dwa_can_proceed return false */
	expect_string(dwa_can_proceed_mock, nvme_conf_dir, ".");
	will_return(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = ddb_run_open(&ctx, &opt);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/**
 * Regression test: when dwa_can_proceed()'s own internal check fails (a real, negative DAOS
 * error, not a refusal), dv_pool_open()'s goto-based error handling must propagate that exact
 * rc, not silently convert it to -DER_NO_SERVICE.
 */
static void
open_propagates_check_error_test(void **state)
{
	struct ddb_ctx      ctx = {0};
	struct open_options opt = {0};
	int                 rc;

	opt.path = "123e4567-e89b-12d3-a456-426614174000/vos-0";

	expect_any(dwa_can_proceed_mock, nvme_conf_dir);
	will_return(dwa_can_proceed_mock, -DER_NOMEM);

	rc = ddb_run_open(&ctx, &opt);
	assert_int_equal(rc, -DER_NOMEM);
}

/**
 * ddb_run_open() must check the guard against the db_path derived from the VOS path's
 * directory prefix, not against the raw, omitted opt->db_path.
 */
static void
open_uses_derived_db_path_test(void **state)
{
	struct ddb_ctx      ctx = {0};
	struct open_options opt = {0};
	int                 rc;

	/* opt.db_path intentionally left NULL (omitted). The VOS path has a directory prefix
	 * ("/mnt/daos2"), so parse_db_path() derives db_path="/mnt/daos2" from it (verified
	 * directly against ddb_parse.c's regex).
	 */
	opt.path = "/mnt/daos2/123e4567-e89b-12d3-a456-426614174000/vos-0";

	expect_string(dwa_can_proceed_mock, nvme_conf_dir, "/mnt/daos2");
	will_return(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = ddb_run_open(&ctx, &opt);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/**
 * As for ddb_run_open(), ddb_run_feature() must check the guard against the db_path derived
 * from the VOS path's directory prefix -- but only when no pool is already open in ctx (i.e.
 * ddb_pool_is_open() is false).
 */
static void
feature_uses_derived_db_path_test(void **state)
{
	struct ddb_ctx         ctx = {0};
	struct feature_options opt = {0};
	int                    rc;

	/* opt.db_path intentionally left NULL (omitted), same derivation as open above. No pool
	 * is open in ctx, so ddb_run_feature() must open one itself, reaching dv_pool_open().
	 * show_features (with no set/clear flags) satisfies ddb_run_feature()'s two early checks
	 * without requiring write-mode/permission setup.
	 */
	opt.path          = "/mnt/daos2/123e4567-e89b-12d3-a456-426614174000/vos-0";
	opt.show_features = true;

	expect_string(dwa_can_proceed_mock, nvme_conf_dir, "/mnt/daos2");
	will_return(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = ddb_run_feature(&ctx, &opt);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/**
 * Symmetric regression test for ddb_run_rm_pool()/dv_pool_destroy(), which derives db_path from
 * the VOS path the same way dv_pool_open() does.
 */
static void
rm_pool_uses_derived_db_path_test(void **state)
{
	struct ddb_ctx         ctx = {0};
	struct rm_pool_options opt = {0};
	int                    rc;

	opt.path = "/mnt/daos2/223e4567-e89b-12d3-a456-426614174001/vos-0";

	expect_string(dwa_can_proceed_mock, nvme_conf_dir, "/mnt/daos2");
	will_return(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = ddb_run_rm_pool(&ctx, &opt);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/**
 * ddb_run_dev_list() takes db_path directly (no VOS-path derivation step): confirmed by
 * inspection that the exact same value is checked by the guard and used for the real
 * vos_self_init() call. This test locks in that property so a future change can't silently
 * regress it.
 */
static void
dev_list_uses_given_db_path_test(void **state)
{
	struct ddb_ctx          ctx = {0};
	struct dev_list_options opt = {0};
	int                     rc;

	opt.db_path = "/some/explicit/db_path";

	expect_string(dwa_can_proceed_mock, nvme_conf_dir, "/some/explicit/db_path");
	will_return(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = ddb_run_dev_list(&ctx, &opt);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/**
 * Regression test: dv_dev_list()'s direct-return-style error handling (as opposed to
 * dv_pool_open()'s goto-based style) must also propagate dwa_can_proceed()'s own internal
 * check error verbatim, not silently convert it to -DER_NO_SERVICE.
 */
static void
dev_list_propagates_check_error_test(void **state)
{
	struct ddb_ctx          ctx = {0};
	struct dev_list_options opt = {0};
	int                     rc;

	opt.db_path = "/some/explicit/db_path";

	expect_any(dwa_can_proceed_mock, nvme_conf_dir);
	will_return(dwa_can_proceed_mock, -DER_NOMEM);

	rc = ddb_run_dev_list(&ctx, &opt);
	assert_int_equal(rc, -DER_NOMEM);
}

/**
 * ddb_run_dev_replace() takes db_path directly, same property as dev_list. old_devid/new_devid
 * must be valid, distinct UUIDs for execution to reach the guard call inside dv_dev_replace().
 */
static void
dev_replace_uses_given_db_path_test(void **state)
{
	struct ddb_ctx             ctx = {0};
	struct dev_replace_options opt = {0};
	int                        rc;

	opt.db_path   = "/some/explicit/db_path";
	opt.old_devid = "123e4567-e89b-12d3-a456-426614174000";
	opt.new_devid = "223e4567-e89b-12d3-a456-426614174001";

	expect_string(dwa_can_proceed_mock, nvme_conf_dir, "/some/explicit/db_path");
	will_return(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = ddb_run_dev_replace(&ctx, &opt);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/**
 * ddb_run_prov_mem() takes db_path directly, same property as dev_list. db_path and
 * tmpfs_mount must both be non-empty for execution to reach the guard call inside
 * dv_run_prov_mem().
 */
static void
prov_mem_uses_given_db_path_test(void **state)
{
	struct ddb_ctx          ctx = {0};
	struct prov_mem_options opt = {0};
	int                     rc;

	opt.db_path     = "/some/explicit/db_path";
	opt.tmpfs_mount = "/mnt/tmpfs";

	expect_string(dwa_can_proceed_mock, nvme_conf_dir, "/some/explicit/db_path");
	will_return(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = ddb_run_prov_mem(&ctx, &opt);
	assert_int_equal(rc, -DER_NO_SERVICE);
}

/**
 * Unlike dev_list/dev_replace/prov_mem, dv_sync_smd() checks the guard unconditionally
 * (nvme_conf_dir == NULL), since nvme_conf (its SPDK-driving argument) is independent of
 * db_path. Deliberately mismatched nvme_conf/db_path values prove the guard still fires
 * regardless; vos_self_init_ext()/smd_init() are mocked too, since the guard check sits after
 * they succeed.
 */
static void
smd_sync_checks_guard_unconditionally_test(void **state)
{
	struct ddb_ctx          ctx = {0};
	struct smd_sync_options opt = {0};
	int                     rc;

	opt.nvme_conf = "/configs/engine.json";
	opt.db_path   = "/mnt/daos_without_a_daos_nvme_conf";

	will_return(__wrap_vos_self_init_ext, 0);
	will_return(__wrap_smd_init, 0);
	expect_value(dwa_can_proceed_mock, nvme_conf_dir, 0);
	will_return(dwa_can_proceed_mock, 0);
	will_return(dwa_can_proceed_mock, false);

	rc = ddb_run_smd_sync(&ctx, &opt);
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

/* ----------------------------------------------------------------
 * Suite registration
 * ---------------------------------------------------------------- */
#define TEST(x) {#x, x##_test, NULL, NULL}

static const struct CMUnitTest ddb_commands_ut_cases[] = {
    TEST(open_can_proceed_failure),
    TEST(open_propagates_check_error),
    TEST(open_uses_derived_db_path),
    TEST(feature_uses_derived_db_path),
    TEST(rm_pool_uses_derived_db_path),
    TEST(dev_list_uses_given_db_path),
    TEST(dev_list_propagates_check_error),
    TEST(dev_replace_uses_given_db_path),
    TEST(prov_mem_uses_given_db_path),
    TEST(smd_sync_checks_guard_unconditionally),
    TEST(version),
    TEST(close_not_open),
};

int
ddb_commands_ut_run(void)
{
	return cmocka_run_group_tests_name("DDB Commands Unit Tests", ddb_commands_ut_cases,
					   ddb_commands_ut_setup, ddb_commands_ut_teardown);
}
