/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ftw.h>
#include <errno.h>

#include <gurt/common.h>

#include "ddb.h"
#include "ddb_cmocka.h"
#include "ddb_spdk_reinit_wa.h"
#include "ddb_fake_print.h"
#include "ddb_mocks.h"

/*
 * ----------------------------------------------------------------
 * Test Mocks and Stubs
 * ----------------------------------------------------------------
 */

/*
 * Test-only convenience wrapper collapsing the real int-rc/bool-out-param call back down to a
 * single bool, since these tests only ever need to assert on the "can proceed" decision itself
 * (the internal check never fails in these tests, so rc is always 0).
 */
static bool
can_proceed(struct ddb_ctx *ctx, const char *nvme_conf_dir)
{
	bool allowed;
	int  rc;

	rc = dwa_can_proceed(ctx, nvme_conf_dir, &allowed);
	assert_int_equal(rc, 0);
	return allowed;
}

/*
 * Wrapped d_asprintf2() for mocking the call to nvme_conf_exists() in dwa_can_proceed()'s internal
 * check.
 */
static char *
d_asprintf2_mock(int *rc, const char *fmt, ...)
{
	check_expected_ptr(rc);
	check_expected_ptr(fmt);

	*rc = mock_type(int);
	return mock_ptr_type(char *);
}

/*
 * ----------------------------------------------------------------
 * Test helpers
 * ----------------------------------------------------------------
 */

/* Use temp dir path under PATH_MAX to keep stack frames under -Wframe-larger-than limit. */
#define TEST_PATH_SIZE 128

static int
create_nvme_conf(char *dir)
{
	char *path;
	int   fd;
	int   rc;

	D_ASSERT(dir != NULL);

	if (mkdtemp(dir) == NULL) {
		print_error("ERROR: Failed to create temp dir %s: %s\n", dir, strerror(errno));
		return -1;
	}

	D_ASPRINTF(path, "%s/daos_nvme.conf", dir);
	if (path == NULL) {
		print_error("ERROR: Failed to create daos_nvme.conf path\n");
		D_GOTO(out_dir, rc = -1);
	}

	fd = open(path, O_CREAT | O_WRONLY, 0600);
	if (fd < 0) {
		print_error("ERROR: Failed to create daos_nvme.conf file %s: %s\n", path,
			    strerror(errno));
		D_GOTO(out_path, rc = -1);
	}

	rc = close(fd);
	if (rc != 0) {
		print_error("ERROR: Failed to close daos_nvme.conf file %s: %s\n", path,
			    strerror(errno));
		goto out_path;
	}

	D_FREE(path);
	return 0;

out_path:
	(void)unlink(path);
	D_FREE(path);
out_dir:
	(void)rmdir(dir);
	return rc;
}

static int
remove_entry(const char *path, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
	return remove(path);
}

static int
rmdir_rec(const char *path)
{
	return nftw(path, remove_entry, 64, FTW_DEPTH | FTW_PHYS);
}

/*
 * ----------------------------------------------------------------
 * Setup & Teardown functions
 * ----------------------------------------------------------------
 */

static char no_nvme_dir[TEST_PATH_SIZE] = "/tmp/no_nvme_XXXXXX";
static char nvme_dir[TEST_PATH_SIZE]    = "/tmp/nvme_XXXXXX";

static int
spdk_reinit_setup(void **state)
{
	int rc;

	if (mkdtemp(no_nvme_dir) == NULL) {
		print_error("ERROR: Failed to create temp dir %s: %s\n", no_nvme_dir,
			    strerror(errno));
		return -1;
	}

	rc = create_nvme_conf(nvme_dir);
	if (rc != 0)
		(void)rmdir(no_nvme_dir);
	return rc;
}

static int
spdk_reinit_teardown(void **state)
{
	int rc = 0;

	if (rmdir_rec(nvme_dir) != 0) {
		print_error("ERROR: Failed to remove temp dir %s: %s\n", nvme_dir, strerror(errno));
		rc = -1;
	}
	if (rmdir(no_nvme_dir) != 0) {
		print_error("ERROR: Failed to remove temp dir %s: %s\n", no_nvme_dir,
			    strerror(errno));
		rc = -1;
	}

	return rc;
}

#ifndef DAOS_BUILD_RELEASE
static int
spdk_reinit_override_setup(void **state)
{
	int rc;

	rc = setenv(DDB_ALLOW_SPDK_REINIT_ENV, "1", 1);
	if (rc != 0)
		print_error("ERROR: Failed to set " DDB_ALLOW_SPDK_REINIT_ENV ": %s\n",
			    strerror(errno));
	return rc;
}

static int
spdk_reinit_override_teardown(void **state)
{
	int rc;

	rc = unsetenv(DDB_ALLOW_SPDK_REINIT_ENV);
	if (rc != 0)
		print_error("ERROR: Failed to unset " DDB_ALLOW_SPDK_REINIT_ENV ": %s\n",
			    strerror(errno));
	return rc;
}
#endif /* !DAOS_BUILD_RELEASE */

static int
mock_d_asprintf2_setup(void **state)
{
	mock_d_asprintf2_set(d_asprintf2_mock);

	return 0;
}

static int
mock_d_asprintf2_teardown(void **state)
{
	mock_d_asprintf2_set(NULL);

	return 0;
}

/*
 * ----------------------------------------------------------------
 * Tests
 * ----------------------------------------------------------------
 */

/**
 * dwa_can_proceed() must assert (not crash or silently misbehave) if ctx is NULL.
 */
static void
test_dwa_can_proceed_001(void **state)
{
	bool allowed;

	expect_assert_failure(dwa_can_proceed(NULL, nvme_dir, &allowed));
}

/**
 * dwa_can_proceed() must assert (not crash or silently misbehave) if can_proceed is NULL.
 */
static void
test_dwa_can_proceed_002(void **state)
{
	struct ddb_ctx ctx = {0};

	expect_assert_failure(dwa_can_proceed(&ctx, nvme_dir, NULL));
}

#ifndef DAOS_BUILD_RELEASE
/**
 * Regression test for the DAOS_DDB_ALLOW_SPDK_REINIT diagnostic override. This feature is
 * compiled out of release builds (see ddb_spdk_reinit_wa.c), so this test -- along with its
 * setup/teardown and registration below -- is too: without the guard, its first assertion
 * would be refused (nvme_used_once is already set by spdk_reinit_wa_sequence_test above) since
 * there is no compiled-in override to bypass that in a release build.
 */
static void
test_dwa_can_proceed_003(void **state)
{
	struct ddb_ctx ctx = {0};

	ctx.dc_io_ft.ddb_print_message = fake_print;
	ctx.dc_io_ft.ddb_print_error   = fake_print;

	/*
	 * With the override set, even two calls in a row against the same NVMe-backed dir must
	 * both be allowed -- the exact sequence that would otherwise be refused.
	 */
	fake_print_reset();
	assert_true(can_proceed(&ctx, nvme_dir));
	assert_true(can_proceed(&ctx, nvme_dir));
	assert_string_equal(fake_print_buf, "");
}
#endif /* !DAOS_BUILD_RELEASE */

/**
 * d_asprintf2() fails, dwa_can_proceed() must return that error code instead of silently
 * misbehaving.
 */
static void
test_dwa_can_proceed_004(void **state)
{
	struct ddb_ctx ctx = {0};
	bool           allowed;
	int            rc;

	fake_print_reset();
	expect_any(d_asprintf2_mock, rc);
	expect_string(d_asprintf2_mock, fmt, "%s/%s");
	will_return_int(d_asprintf2_mock, -DER_NOMEM);
	will_return(d_asprintf2_mock, NULL);

	rc = dwa_can_proceed(&ctx, nvme_dir, &allowed);
	assert_int_equal(rc, -DER_NOMEM);
}

static const char *SUBSTRING_WARNING_001 = "SPDK cannot be";
static const char *SUBSTRING_WARNING_002 = "restart the DDB process";

/**
 * dwa_can_proceed() tracks a single, process-lifetime "an NVMe-backed pool has
 * been used once" flag.
 */
static void
test_dwa_can_proceed_005(void **state)
{
	struct ddb_ctx ctx = {0};

	ctx.dc_io_ft.ddb_print_message = fake_print;
	ctx.dc_io_ft.ddb_print_error   = fake_print;

	/* Pools with no daos_nvme.conf never touch SPDK: safe to reuse indefinitely. */
	fake_print_reset();
	assert_true(can_proceed(&ctx, no_nvme_dir));
	assert_true(can_proceed(&ctx, no_nvme_dir));
	assert_string_equal(fake_print_buf, "");

	/* First NVMe-backed pool: allowed, and now marks NVMe as used for this process. */
	fake_print_reset();
	assert_true(can_proceed(&ctx, nvme_dir));
	assert_string_equal(fake_print_buf, "");

	/* A pool with no daos_nvme.conf remains safe even after an NVMe-backed pool was used. */
	fake_print_reset();
	assert_true(can_proceed(&ctx, no_nvme_dir));
	assert_string_equal(fake_print_buf, "");

	/* A second, different NVMe-backed pool must now be cleanly refused. */
	fake_print_reset();
	assert_false(can_proceed(&ctx, nvme_dir));
	assert_string_contains(fake_print_buf, SUBSTRING_WARNING_001);
	assert_string_contains(fake_print_buf, SUBSTRING_WARNING_002);
}

/**
 * dwa_can_proceed(ctx, NULL, ...) (used by smd_sync, whose SPDK-driving nvme_conf argument is
 * independent of any db_path) shares the same process-lifetime "used once" flag as a regular,
 * path-checked call -- proven here by observing that it is refused for the same reason:
 * spdk_reinit_wa_sequence_test (which runs immediately before this one) already marked NVMe as
 * used via nvme_dir, so this call must see that same state despite passing NULL.
 */
static void
test_dwa_can_proceed_006(void **state)
{
	struct ddb_ctx ctx = {0};

	ctx.dc_io_ft.ddb_print_message = fake_print;
	ctx.dc_io_ft.ddb_print_error   = fake_print;

	fake_print_reset();
	assert_false(can_proceed(&ctx, NULL));
	assert_string_contains(fake_print_buf, SUBSTRING_WARNING_001);
	assert_string_contains(fake_print_buf, SUBSTRING_WARNING_002);
}

/*
 * ----------------------------------------------------------------
 * Suite registration
 * ----------------------------------------------------------------
 */
#define TEST(x, y, z) {#x, test_##x, y, z}

/* The order of these tests matters. They share global state which cannot get reset. */
static const struct CMUnitTest ddb_spdk_reinit_wa_ut_cases[] = {
    TEST(dwa_can_proceed_001, NULL, NULL),
    TEST(dwa_can_proceed_002, NULL, NULL),
#ifndef DAOS_BUILD_RELEASE
    TEST(dwa_can_proceed_003, spdk_reinit_override_setup, spdk_reinit_override_teardown),
#endif
    TEST(dwa_can_proceed_004, mock_d_asprintf2_setup, mock_d_asprintf2_teardown),
    TEST(dwa_can_proceed_005, NULL, NULL),
    TEST(dwa_can_proceed_006, NULL, NULL)};

int
ddb_spdk_reinit_wa_ut_run(void)
{
	return cmocka_run_group_tests_name("DDB SPDK Re-init Workaround Unit Tests",
					   ddb_spdk_reinit_wa_ut_cases, spdk_reinit_setup,
					   spdk_reinit_teardown);
}
