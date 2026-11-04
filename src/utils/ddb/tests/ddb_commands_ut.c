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

#include "ddb.h"
#include "ddb_cmocka.h"
#include "ddb_fake_print.h"
#include "ddb_mocks.h"

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

/* Fake WAL session: ddb_commands.c only tests the pointer for NULL, it is never dereferenced. */
#define FAKE_WAL_MC   ((struct bio_meta_context *)0x5a1)
#define FAKE_VOS_PATH "/mnt/daos/12345678-1234-1234-1234-123456789012/vos-0"

static void
wal_ctx_init(struct ddb_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->dc_io_ft.ddb_print_message = fake_print;
	ctx->dc_io_ft.ddb_print_error   = fake_print;
	ctx->dc_poh                     = DAOS_HDL_INVAL;
	fake_print_reset();
}

/**
 * ddb_wal_is_open reflects the presence of a WAL session in the context.
 */
static void
wal_is_open_test(void **state)
{
	struct ddb_ctx ctx;

	wal_ctx_init(&ctx);
	assert_false(ddb_wal_is_open(&ctx));

	ctx.dc_wal_mc = FAKE_WAL_MC;
	assert_true(ddb_wal_is_open(&ctx));
}

/**
 * ddb_run_wal_close must fail with -DER_NO_HDL when no WAL session is open.
 */
static void
wal_close_not_open_test(void **state)
{
	struct ddb_ctx ctx;
	int            rc;

	wal_ctx_init(&ctx);

	rc = ddb_run_wal_close(&ctx);
	assert_int_equal(rc, -DER_NO_HDL);
	assert_string_contains(fake_print_buf, "No WAL session is open");
}

/**
 * ddb_run_wal_open must fail with -DER_INVAL when no path is provided, without touching the
 * context.
 */
static void
wal_open_no_path_test(void **state)
{
	struct ddb_ctx          ctx;
	struct wal_open_options opt = {0};
	int                     rc;

	wal_ctx_init(&ctx);

	rc = ddb_run_wal_open(&ctx, &opt);
	assert_int_equal(rc, -DER_INVAL);
	assert_null(ctx.dc_wal_mc);
	assert_string_contains(fake_print_buf, "required");

	opt.path = "";
	fake_print_reset();
	rc = ddb_run_wal_open(&ctx, &opt);
	assert_int_equal(rc, -DER_INVAL);
	assert_null(ctx.dc_wal_mc);
}

/**
 * ddb_run_wal_open must fail with -DER_BUSY while a pool is open.
 */
static void
wal_open_pool_open_test(void **state)
{
	struct ddb_ctx          ctx;
	struct wal_open_options opt = {.path = FAKE_VOS_PATH};
	int                     rc;

	wal_ctx_init(&ctx);
	ctx.dc_poh.cookie = 0x1234;

	rc = ddb_run_wal_open(&ctx, &opt);
	assert_int_equal(rc, -DER_BUSY);
	assert_null(ctx.dc_wal_mc);
	assert_string_contains(fake_print_buf, "Cannot operate on an opened pool");
}

/**
 * ddb_run_wal_open must fail with -DER_BUSY while another WAL session is open.
 */
static void
wal_open_twice_test(void **state)
{
	struct ddb_ctx          ctx;
	struct wal_open_options opt = {.path = FAKE_VOS_PATH};
	int                     rc;

	wal_ctx_init(&ctx);
	ctx.dc_wal_mc = FAKE_WAL_MC;

	rc = ddb_run_wal_open(&ctx, &opt);
	assert_int_equal(rc, -DER_BUSY);
	assert_ptr_equal(ctx.dc_wal_mc, FAKE_WAL_MC);
	assert_string_contains(fake_print_buf, "A WAL session is open");
}

/**
 * The pool-lifecycle commands must fail with -DER_BUSY while a WAL session is open.
 */
static void
wal_open_excludes_pool_cmds_test(void **state)
{
	struct ddb_ctx             ctx;
	struct open_options        open_opt        = {.path = FAKE_VOS_PATH};
	struct rm_pool_options     rm_pool_opt     = {.path = FAKE_VOS_PATH};
	struct dev_list_options    dev_list_opt    = {0};
	struct dev_replace_options dev_replace_opt = {0};
	struct smd_sync_options    smd_sync_opt    = {0};
	struct prov_mem_options    prov_mem_opt = {.db_path = "/mnt/daos", .tmpfs_mount = "/mnt/x"};
	struct feature_options     feature_opt  = {.show_features = true, .path = open_opt.path};
	int                        rc;

	wal_ctx_init(&ctx);
	ctx.dc_wal_mc = FAKE_WAL_MC;

	rc = ddb_run_open(&ctx, &open_opt);
	assert_int_equal(rc, -DER_BUSY);
	assert_true(daos_handle_is_inval(ctx.dc_poh));

	rc = ddb_run_rm_pool(&ctx, &rm_pool_opt);
	assert_int_equal(rc, -DER_BUSY);

	rc = ddb_run_dev_list(&ctx, &dev_list_opt);
	assert_int_equal(rc, -DER_BUSY);

	rc = ddb_run_dev_replace(&ctx, &dev_replace_opt);
	assert_int_equal(rc, -DER_BUSY);

	rc = ddb_run_smd_sync(&ctx, &smd_sync_opt);
	assert_int_equal(rc, -DER_BUSY);

	rc = ddb_run_prov_mem(&ctx, &prov_mem_opt);
	assert_int_equal(rc, -DER_BUSY);

	rc = ddb_run_feature(&ctx, &feature_opt);
	assert_int_equal(rc, -DER_BUSY);

	assert_string_contains(fake_print_buf, "A WAL session is open");
}

/*
 * Mocked dw_open()/dw_close(): return the rc queued with will_return() and, on a successful open,
 * hand out the fake WAL session. The real functions need SPDK/VOS and are covered by ddb_wal_ut.c
 * and ddb_tests.
 */
static int
dw_open_mock(const char *path, const char *db_path, struct ddb_ctx *ctx,
	     struct bio_meta_context **mc)
{
	int rc;

	assert_string_equal(path, FAKE_VOS_PATH);
	assert_null(db_path);
	assert_non_null(ctx);
	assert_non_null(mc);

	rc = mock_type(int);
	if (rc == 0)
		*mc = FAKE_WAL_MC;

	return rc;
}

static int
dw_close_mock(struct bio_meta_context *mc)
{
	assert_ptr_equal(mc, FAKE_WAL_MC);

	return mock_type(int);
}

static int
wal_mock_setup(void **state)
{
	mock_dw_open_set(dw_open_mock);
	mock_dw_close_set(dw_close_mock);

	return 0;
}

static int
wal_mock_teardown(void **state)
{
	mock_dw_close_set(NULL);
	mock_dw_open_set(NULL);

	return 0;
}

/**
 * A successful ddb_run_wal_open stores the WAL session and the requested write mode in the context.
 */
static void
wal_open_success_test(void **state)
{
	struct ddb_ctx          ctx;
	struct wal_open_options opt = {.path = FAKE_VOS_PATH};
	int                     rc;

	wal_ctx_init(&ctx);
	will_return(dw_open_mock, 0);
	rc = ddb_run_wal_open(&ctx, &opt);
	assert_int_equal(rc, 0);
	assert_ptr_equal(ctx.dc_wal_mc, FAKE_WAL_MC);
	assert_true(ddb_wal_is_open(&ctx));
	assert_false(ctx.dc_write_mode);
	assert_string_contains(fake_print_buf, "opened (no replay)");
	assert_null(strstr(fake_print_buf, "in write mode"));

	wal_ctx_init(&ctx);
	opt.write_mode = true;
	will_return(dw_open_mock, 0);
	rc = ddb_run_wal_open(&ctx, &opt);
	assert_int_equal(rc, 0);
	assert_ptr_equal(ctx.dc_wal_mc, FAKE_WAL_MC);
	assert_true(ctx.dc_write_mode);
	assert_string_contains(fake_print_buf, "opened (no replay) in write mode");
}

/**
 * Each dw_open error is propagated verbatim by ddb_run_wal_open, mapped to its user message (or to
 * none for the SPDK re-initialization guard, which reports by itself) and leaves the context
 * without a WAL session.
 */
static void
wal_open_error_mapping_test(void **state)
{
	static const struct {
		int         rc;
		const char *msg; /* "": nothing is printed; NULL: the generic message is expected */
	} cases[] = {
	    {-DER_NOTSUPPORTED, "'" FAKE_VOS_PATH "' is not a MD-on-SSD target: no WAL to open."},
	    {-DER_NONEXIST, "No meta/WAL blob found for '" FAKE_VOS_PATH "' in the system DB."},
	    {-DER_UNINIT, "Invalid meta or WAL header for '" FAKE_VOS_PATH "' (bad magic)."},
	    {-DER_DF_INCOMPT,
	     "Invalid meta or WAL header for '" FAKE_VOS_PATH "' (unsupported version)."},
	    {-DER_CSUM, "Invalid meta or WAL header for '" FAKE_VOS_PATH "' (bad checksum)."},
	    {-DER_DF_INVAL, "Inconsistent meta or WAL header for '" FAKE_VOS_PATH
			    "' (details in the DAOS log). The header may be repaired with "
			    "'wal_repair_hdr' (not available yet)."},
	    {-DER_NO_SERVICE, ""},
	    {-DER_INVAL, NULL},
	    {-DER_NOMEM, NULL},
	};
	struct ddb_ctx          ctx;
	struct wal_open_options opt = {.path = FAKE_VOS_PATH, .write_mode = true};
	char                    exp[256];
	int                     i;
	int                     rc;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		print_message("dw_open() -> " DF_RC "\n", DP_RC(cases[i].rc));
		wal_ctx_init(&ctx);
		will_return(dw_open_mock, cases[i].rc);
		rc = ddb_run_wal_open(&ctx, &opt);
		assert_int_equal(rc, cases[i].rc);
		assert_null(ctx.dc_wal_mc);
		assert_false(ddb_wal_is_open(&ctx));
		assert_false(ctx.dc_write_mode);
		if (cases[i].msg == NULL) {
			snprintf(exp, sizeof(exp), "Failed to open the WAL of '%s': " DF_RC "\n",
				 FAKE_VOS_PATH, DP_RC(cases[i].rc));
			assert_string_contains(fake_print_buf, exp);
		} else if (cases[i].msg[0] == '\0') {
			assert_string_equal(fake_print_buf, "");
		} else {
			assert_string_contains(fake_print_buf, cases[i].msg);
		}
	}
}

/**
 * ddb_run_wal_close releases the WAL session from the context even when dw_close fails, and
 * reports the failure.
 */
static void
wal_close_error_test(void **state)
{
	struct ddb_ctx ctx;
	char           exp[128];
	int            rc;

	wal_ctx_init(&ctx);
	ctx.dc_wal_mc     = FAKE_WAL_MC;
	ctx.dc_write_mode = true;
	will_return(dw_close_mock, 0);
	rc = ddb_run_wal_close(&ctx);
	assert_int_equal(rc, 0);
	assert_null(ctx.dc_wal_mc);
	assert_false(ddb_wal_is_open(&ctx));
	assert_false(ctx.dc_write_mode);
	assert_string_equal(fake_print_buf, "");

	wal_ctx_init(&ctx);
	ctx.dc_wal_mc     = FAKE_WAL_MC;
	ctx.dc_write_mode = true;
	will_return(dw_close_mock, -DER_IO);
	rc = ddb_run_wal_close(&ctx);
	assert_int_equal(rc, -DER_IO);
	assert_null(ctx.dc_wal_mc);
	assert_false(ddb_wal_is_open(&ctx));
	assert_false(ctx.dc_write_mode);
	snprintf(exp, sizeof(exp), "Failed to close the WAL session: " DF_RC "\n", DP_RC(-DER_IO));
	assert_string_contains(fake_print_buf, exp);
}

/*
 * ----------------------------------------------------------------
 * Suite registration
 * ----------------------------------------------------------------
 */
#define TEST(x)     {#x, x##_test, NULL, NULL}
#define TEST_WAL(x) {#x, x##_test, wal_mock_setup, wal_mock_teardown}

static const struct CMUnitTest ddb_commands_ut_cases[] = {
    TEST(version),
    TEST(close_not_open),
    /* WAL session commands */
    TEST(wal_is_open),
    TEST(wal_close_not_open),
    TEST(wal_open_no_path),
    TEST(wal_open_pool_open),
    TEST(wal_open_twice),
    TEST(wal_open_excludes_pool_cmds),
    TEST_WAL(wal_open_success),
    TEST_WAL(wal_open_error_mapping),
    TEST_WAL(wal_close_error),
};

int
ddb_commands_ut_run(void)
{
	return cmocka_run_group_tests_name("DDB Commands Unit Tests", ddb_commands_ut_cases, NULL,
					   NULL);
}
