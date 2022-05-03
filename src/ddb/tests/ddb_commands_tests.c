/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <gurt/debug.h>
#include <daos/tests_lib.h>
#include <ddb_common.h>
#include <ddb_cmd_options.h>
#include <daos_srv/vos.h>
#include "ddb_cmocka.h"
#include "ddb_test_driver.h"

/*
 * Test that the command line arguments execute the correct tool command with the correct
 * options/arguments for the command. Verification depends on the ability to set fake command
 * functions in a command function table that the program uses.
 */

struct ddb_ctx g_ctx = {
	.dc_io_ft.ddb_print_message = dvt_fake_print,
	.dc_io_ft.ddb_print_error = dvt_fake_print,
	.dc_io_ft.ddb_read_file = dvt_fake_read_file,
	.dc_io_ft.ddb_get_file_size = dvt_fake_get_file_size,
	.dc_io_ft.ddb_get_file_exists = dvt_fake_get_file_exists,
};

static uint32_t fake_write_file_called;
static int
fake_write_file(const char *path, d_iov_t *contents)
{
	fake_write_file_called++;

	return 0;
}

/*
 * -----------------------------------------------
 * Test Functions
 * -----------------------------------------------
 */
static void
quit_cmd_tests(void **state)
{
	/* Quit is really simple and should just indicate to the program context that it's
	 * time to quit
	 */
	assert_success(ddb_run_quit(&g_ctx));
	assert_true(g_ctx.dc_should_quit);
}

static void
ls_cmd_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	struct ddb_ctx		 ctx = {0};
	struct ls_options	 opt = {.recursive = false, .path = ""};
	int			 items_in_tree;
	char			 buf[256];

	ctx.dc_poh = tctx->dvt_poh;
	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_error = dvt_fake_print;
	assert_success(ddb_run_ls(&ctx, &opt));

	/* At least each container should be printed */
	assert_true(ARRAY_SIZE(g_uuids) <= dvt_fake_print_called);

	/* With recursive set, every item in the tree should be printed */
	opt.recursive = true;
	items_in_tree = ARRAY_SIZE(g_uuids) * ARRAY_SIZE(g_oids) *
			ARRAY_SIZE(g_dkeys) * ARRAY_SIZE(g_akeys);
	dvt_fake_print_called = 0;
	assert_success(ddb_run_ls(&ctx, &opt));
	assert_true(items_in_tree <= dvt_fake_print_called);

	/* pick a specific oid - each dkey should be printed */
	opt.path = "[0]/[0]";
	opt.recursive = false;
	assert_success(ddb_run_ls(&ctx, &opt));
	assert_true(ARRAY_SIZE(g_dkeys) <= dvt_fake_print_called);

	/* invalid paths ... */
	opt.path = buf;

	sprintf(buf, "%s", g_invalid_uuid_str);
	assert_rc_equal(-DER_NONEXIST, ddb_run_ls(&ctx, &opt));
	sprintf(buf, "%s/"DF_OID"/", g_uuids_str[0], DP_OID(g_invalid_oid.id_pub));
	assert_rc_equal(-DER_NONEXIST, ddb_run_ls(&ctx, &opt));
}

static void
dump_value_cmd_tests(void **state)
{
	struct dt_vos_pool_ctx		*tctx = *state;
	struct ddb_ctx			 ctx = {0};
	struct dump_value_options	 opt = {0};

	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_error = dvt_fake_print;
	ctx.dc_io_ft.ddb_write_file = fake_write_file;
	ctx.dc_poh = tctx->dvt_poh;

	/* requires a path to dump */
	assert_invalid(ddb_run_dump_value(&ctx, &opt));

	/* path must be complete (to a value) */
	opt.path = "[0]";
	assert_invalid(ddb_run_dump_value(&ctx, &opt));

	/* Path is complete, but needs destination */
	opt.path = "[0]/[0]/[0]/[1]";
	assert_invalid(ddb_run_dump_value(&ctx, &opt));

	/* success */
	opt.dst = "/tmp/dumped_file";
	assert_success(ddb_run_dump_value(&ctx, &opt));
	assert_true(fake_write_file_called >= 1);
}

static void
dump_ilog_cmd_tests(void **state)
{
	struct dt_vos_pool_ctx		*tctx = *state;
	struct ddb_ctx			 ctx = {0};
	struct dump_ilog_options	 opt = {0};

	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_error = dvt_fake_print;
	ctx.dc_io_ft.ddb_write_file = fake_write_file;
	ctx.dc_poh = tctx->dvt_poh;

	assert_invalid(ddb_run_dump_ilog(&ctx, &opt));

	/* Dump object ilog */
	dvt_fake_print_called = 0;
	opt.path = "[0]/[0]";
	assert_success(ddb_run_dump_ilog(&ctx, &opt));
	assert_true(dvt_fake_print_called);

	/* Dump dkey ilog */
	dvt_fake_print_called = 0;
	opt.path = "[0]/[0]/[0]";
	assert_success(ddb_run_dump_ilog(&ctx, &opt));
	assert_true(dvt_fake_print_called);

	opt.path = "[0]/[0]/[0]/[0]";
	assert_invalid(ddb_run_dump_ilog(&ctx, &opt));
}

static void
dump_superblock_cmd_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	struct ddb_ctx		 ctx = {0};

	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_poh = tctx->dvt_poh;

	ddb_run_dump_superblock(&ctx);

	assert_true(dvt_fake_print_called >= 1); /* Should have printed at least once */
}

static void
dump_dtx_cmd_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	struct ddb_ctx		 ctx = {0};
	struct dump_dtx_options	 opt = {0};
	daos_handle_t		 coh;

	dvt_fake_print_reset();

	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_error = dvt_fake_print;
	ctx.dc_poh = tctx->dvt_poh;

	assert_invalid(ddb_run_dump_dtx(&ctx, &opt));

	assert_success(vos_cont_open(tctx->dvt_poh, g_uuids[0], &coh));

	dvt_vos_insert_2_records_with_dtx(coh);
	vos_cont_close(coh);

	opt.path = "[0]";
	assert_success(ddb_run_dump_dtx(&ctx, &opt));

	assert_string_contains(dvt_fake_print_buffer, "Active Transactions:");
	assert_string_contains(dvt_fake_print_buffer, "Committed Transactions:");
}

static void
rm_cmd_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	struct ddb_ctx		 ctx = {0};
	struct rm_options	 opt = {0};

	ctx.dc_poh = tctx->dvt_poh;
	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_error = dvt_fake_print;

	assert_invalid(ddb_run_rm(&ctx, &opt));

	dvt_fake_print_reset();
	opt.path = "[0]";
	assert_success(ddb_run_rm(&ctx, &opt));
	assert_string_equal(dvt_fake_print_buffer,
			    "/12345678-1234-1234-1234-123456789001 deleted\n");
}

static void
load_cmd_tests(void **state)
{
	struct load_options	opt = {0};
	char			buf[256];
	daos_unit_oid_t		new_oid = g_oids[0];

	assert_invalid(ddb_run_load(&g_ctx, &opt));

	opt.dst = "/[0]/[0]/[0]/[1]";
	opt.src = "/tmp/value_src";
	opt.epoch = "1";
	dvt_fake_get_file_exists_result = true;
	snprintf(dvt_fake_read_file_buf, ARRAY_SIZE(dvt_fake_read_file_buf), "Some text");
	assert_invalid(ddb_run_load(&g_ctx, &opt));
	dvt_fake_get_file_size_result = strlen(dvt_fake_read_file_buf);
	dvt_fake_read_file_result = strlen(dvt_fake_read_file_buf);
	assert_success(ddb_run_load(&g_ctx, &opt));

	/* add a new 'a' key */
	opt.dst = "/[0]/[0]/[0]/'a-new-key'";
	assert_success(ddb_run_load(&g_ctx, &opt));

	/* add a new 'd' key */
	opt.dst = "/[0]/[0]/'a-new-key'/'a-new-key'";
	assert_success(ddb_run_load(&g_ctx, &opt));

	/* add a new object */
	new_oid.id_pub.lo = 999;
	sprintf(buf, "%s/"DF_OID"/'dkey_new'/'akey_new'", g_uuids_str[3],
		DP_OID(new_oid.id_pub));
	opt.dst = buf;
	assert_success(ddb_run_load(&g_ctx, &opt));

	/*
	 * Error cases ...
	 */

	/* File not found */
	dvt_fake_get_file_exists_result = false;
	assert_invalid(ddb_run_load(&g_ctx, &opt));
	dvt_fake_get_file_exists_result = true;

	/* invalid epoch */
	opt.epoch = "a";
	assert_invalid(ddb_run_load(&g_ctx, &opt));
	opt.epoch = "1a";
	assert_invalid(ddb_run_load(&g_ctx, &opt));
	opt.epoch = "1";

	/* incomplete path */
	opt.dst = "/[0]/[0]/";
	assert_invalid(ddb_run_load(&g_ctx, &opt));

	/* Can't use index for a new path */
	opt.dst = "/[0]/[0]/[0]/[9999]";
	assert_rc_equal(-DER_NONEXIST, ddb_run_load(&g_ctx, &opt));

	/* can't create new container */
	sprintf(buf, "%s/"DF_OID"/'dkey_new'/'akey_new'", g_invalid_uuid_str,
		DP_OID(g_oids[0].id_pub));
	opt.dst = buf;
	assert_rc_equal(-DER_NONEXIST, ddb_run_load(&g_ctx, &opt));
}

static void
rm_ilog_cmd_tests(void **state)
{
	struct rm_ilog_options opt = {0};

	assert_invalid(ddb_run_rm_ilog(&g_ctx, &opt));
	opt.path = "[0]"; /* just container ... bad */
	assert_invalid(ddb_run_rm_ilog(&g_ctx, &opt));

	opt.path = "[1]/[0]"; /* object */
	assert_success(ddb_run_rm_ilog(&g_ctx, &opt));
	opt.path = "[2]/[0]/[0]"; /* dkey */
	assert_success(ddb_run_rm_ilog(&g_ctx, &opt));
}

static void
process_ilog_cmd_tests(void **state)
{
	struct process_ilog_options opt = {0};

	assert_invalid(ddb_run_process_ilog(&g_ctx, &opt));
	opt.path = "[0]"; /* just container ... bad */
	assert_invalid(ddb_run_process_ilog(&g_ctx, &opt));

	opt.path = "[1]/[0]"; /* object */
	assert_success(ddb_run_process_ilog(&g_ctx, &opt));
	opt.path = "[2]/[0]/[0]"; /* dkey */
	assert_success(ddb_run_process_ilog(&g_ctx, &opt));
}

static void
clear_dtx_cmd_tests(void **state)
{
	struct clear_dtx_options opt = {0};

	assert_invalid(ddb_run_clear_dtx(&g_ctx, &opt));

	opt.path = "[0]";
	assert_success(ddb_run_clear_dtx(&g_ctx, &opt));
}

/*
 * --------------------------------------------------------------
 * End test functions
 * --------------------------------------------------------------
 */

static int
dcv_suit_setup(void **state)
{
	struct dt_vos_pool_ctx *tctx;

	assert_success(ddb_test_setup_vos(state));

	/* test setup creates the pool, but doesn't open it ... leave it open for these tests */
	tctx = *state;
	assert_success(vos_pool_open(tctx->dvt_pmem_file, tctx->dvt_pool_uuid, 0, &tctx->dvt_poh));

	g_ctx.dc_poh = tctx->dvt_poh;

	return 0;
}

static int
dcv_suit_teardown(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;

	if (tctx == NULL)
		fail_msg("Test not setup correctly");
	assert_success(vos_pool_close(tctx->dvt_poh));
	ddb_teardown_vos(state);

	return 0;
}

#define TEST(test) { #test, test, NULL, NULL }

int
dvc_tests_run()
{
	const struct CMUnitTest tests[] = {
		TEST(quit_cmd_tests),
		TEST(ls_cmd_tests),
		TEST(dump_value_cmd_tests),
		TEST(dump_ilog_cmd_tests),
		TEST(dump_superblock_cmd_tests),
		TEST(dump_dtx_cmd_tests),
		TEST(rm_cmd_tests),
		TEST(load_cmd_tests),
		TEST(rm_ilog_cmd_tests),
		TEST(process_ilog_cmd_tests),
		TEST(clear_dtx_cmd_tests),
	};

	return cmocka_run_group_tests_name("DDB commands tests", tests,
					   dcv_suit_setup, dcv_suit_teardown);
}
