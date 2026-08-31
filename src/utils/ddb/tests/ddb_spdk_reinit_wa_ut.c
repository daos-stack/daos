/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ftw.h>

#include "ddb.h"
#include "ddb_cmocka.h"
#include "ddb_spdk_reinit_wa.h"
#include "ddb_fake_print.h"

/* ----------------------------------------------------------------
 * Test Mocks and Stubs
 * ---------------------------------------------------------------- */

/* Test-only convenience wrapper collapsing the real int-rc/bool-out-param call back down to a
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

/* ----------------------------------------------------------------
 * Test helpers
 * ---------------------------------------------------------------- */

/* Use temp dir path under PATH_MAX to keep stack frames under -Wframe-larger-than limit. */
#define TEST_PATH_SIZE 128

static int
create_nvme_conf(char *dir)
{
	char path[TEST_PATH_SIZE];
	int  fd;
	int  rc;

	if (mkdtemp(dir) == NULL) {
		print_error("ERROR: Failed to create temp dir %s\n", dir);
		rc = -1;
		goto out;
	}

	rc = snprintf(path, sizeof(path), "%s/daos_nvme.conf", dir);
	if (rc < 0 || rc >= sizeof(path)) {
		print_error("ERROR: Failed to create daos_nvme.conf path\n");
		rc = -1;
		goto out_dir;
	}

	fd = open(path, O_CREAT | O_WRONLY, 0600);
	if (fd < 0) {
		print_error("ERROR: Failed to create daos_nvme.conf file %s\n", path);
		rc = -1;
		goto out_dir;
	}
	if (close(fd) < 0) {
		print_error("ERROR: Failed to close daos_nvme.conf file %s\n", path);
		rc = -1;
		goto out_path;
	}

	rc = 0;
	goto out;

out_path:
	unlink(path);
out_dir:
	rmdir(dir);
out:
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

/* ----------------------------------------------------------------
 * Setup & Teardown functions
 * ---------------------------------------------------------------- */

static char no_nvme_dir[TEST_PATH_SIZE] = "/tmp/no_nvme_XXXXXX";
static char nvme_dir[TEST_PATH_SIZE]    = "/tmp/nvme_XXXXXX";

static int
spdk_reinit_setup(void **state)
{
	int rc;

	if (mkdtemp(no_nvme_dir) == NULL) {
		print_error("ERROR: Failed to create temp dir %s\n", no_nvme_dir);
		rc = -1;
		goto out;
	}

	rc = create_nvme_conf(nvme_dir);
	if (rc == 0)
		goto out;

	rmdir_rec(no_nvme_dir);
out:
	return rc;
}

static int
spdk_reinit_teardown(void **state)
{
	int rc = 0;

	if (rmdir_rec(nvme_dir) != 0) {
		print_error("ERROR: Failed to remove temp dir %s\n", nvme_dir);
		rc = -1;
	}
	if (rmdir_rec(no_nvme_dir) != 0) {
		print_error("ERROR: Failed to remove temp dir %s\n", no_nvme_dir);
		rc = -1;
	}

	return rc;
}

#ifndef DAOS_BUILD_RELEASE
static int
spdk_reinit_override_setup(void **state)
{
	int rc;

	rc = setenv("DAOS_DDB_ALLOW_SPDK_REINIT", "1", 1);
	if (rc != 0)
		print_error("ERROR: Failed to set DAOS_DDB_ALLOW_SPDK_REINIT\n");
	return rc;
}

static int
spdk_reinit_override_teardown(void **state)
{
	int rc;

	rc = unsetenv("DAOS_DDB_ALLOW_SPDK_REINIT");
	if (rc != 0)
		print_error("ERROR: Failed to unset DAOS_DDB_ALLOW_SPDK_REINIT\n");
	return rc;
}
#endif /* !DAOS_BUILD_RELEASE */

/* ----------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------- */

/**
 * dwa_can_proceed() tracks a single, process-lifetime "an NVMe-backed pool has
 * been used once" flag.
 */
static void
spdk_reinit_wa_sequence_test(void **state)
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
	assert_string_contains(fake_print_buf, "SPDK cannot be");
	assert_string_contains(fake_print_buf, "restart the DDB process");
}

/**
 * dwa_can_proceed(ctx, NULL, ...) (used by smd_sync, whose SPDK-driving nvme_conf argument is
 * independent of any db_path) shares the same process-lifetime "used once" flag as a regular,
 * path-checked call -- proven here by observing that it is refused for the same reason:
 * spdk_reinit_wa_sequence_test (which runs immediately before this one) already marked NVMe as
 * used via nvme_dir, so this call must see that same state despite passing NULL.
 */
static void
dwa_can_proceed_unconditional_shares_state_test(void **state)
{
	struct ddb_ctx ctx = {0};

	ctx.dc_io_ft.ddb_print_message = fake_print;
	ctx.dc_io_ft.ddb_print_error   = fake_print;

	fake_print_reset();
	assert_false(can_proceed(&ctx, NULL));
	assert_string_contains(fake_print_buf, "SPDK cannot be");
	assert_string_contains(fake_print_buf, "restart the DDB process");
}

/**
 * Regression test for the DAOS_DDB_ALLOW_SPDK_REINIT diagnostic override. This feature is
 * compiled out of release builds (see ddb_spdk_reinit_wa.c), so this test -- along with its
 * setup/teardown and registration below -- is too: without the guard, its first assertion
 * would be refused (nvme_used_once is already set by spdk_reinit_wa_sequence_test above) since
 * there is no compiled-in override to bypass that in a release build.
 */
#ifndef DAOS_BUILD_RELEASE
static void
spdk_reinit_wa_disable_test(void **state)
{
	struct ddb_ctx ctx = {0};

	ctx.dc_io_ft.ddb_print_message = fake_print;
	ctx.dc_io_ft.ddb_print_error   = fake_print;

	/* With the override set, even two calls in a row against the same NVMe-backed dir must
	 * both be allowed -- the exact sequence that would otherwise be refused. */
	fake_print_reset();
	assert_true(can_proceed(&ctx, nvme_dir));
	assert_true(can_proceed(&ctx, nvme_dir));
	assert_string_equal(fake_print_buf, "");
}
#endif /* !DAOS_BUILD_RELEASE */

/**
 * dwa_can_proceed() must assert (not crash or silently misbehave) if ctx is NULL.
 */
static void
dwa_can_proceed_asserts_on_null_ctx_test(void **state)
{
	bool allowed;

	expect_assert_failure(dwa_can_proceed(NULL, nvme_dir, &allowed));
}

/**
 * dwa_can_proceed() must assert (not crash or silently misbehave) if can_proceed is NULL.
 */
static void
dwa_can_proceed_asserts_on_null_can_proceed_test(void **state)
{
	struct ddb_ctx ctx = {0};

	expect_assert_failure(dwa_can_proceed(&ctx, nvme_dir, NULL));
}

/* ----------------------------------------------------------------
 * Suite registration
 * ---------------------------------------------------------------- */
#define TEST(x, y, z) {#x, x##_test, y, z}

static const struct CMUnitTest ddb_spdk_reinit_wa_ut_cases[] = {
    TEST(spdk_reinit_wa_sequence, NULL, NULL),
    TEST(dwa_can_proceed_unconditional_shares_state, NULL, NULL),
    TEST(dwa_can_proceed_asserts_on_null_ctx, NULL, NULL),
    TEST(dwa_can_proceed_asserts_on_null_can_proceed, NULL, NULL),
#ifndef DAOS_BUILD_RELEASE
    TEST(spdk_reinit_wa_disable, spdk_reinit_override_setup, spdk_reinit_override_teardown),
#endif
};

int
ddb_spdk_reinit_wa_ut_run(void)
{
	return cmocka_run_group_tests_name("DDB SPDK Re-init Workaround Unit Tests",
					   ddb_spdk_reinit_wa_ut_cases, spdk_reinit_setup,
					   spdk_reinit_teardown);
}
