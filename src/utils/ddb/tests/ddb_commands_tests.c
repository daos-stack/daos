/**
 * (C) Copyright 2022-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <gurt/debug.h>
#include <daos/tests_lib.h>
#include <ddb_common.h>
#include <ddb.h>
#include <daos_srv/vos.h>
#include <ddb_vos.h>
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
	.dc_write_mode = true,
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
	assert_success(ddb_run_ls(&ctx, &opt));
	assert_true(ARRAY_SIZE(g_uuids) <= dvt_fake_print_called);

	/* With recursive set, every item in the tree should be printed, this gets huge so turn
	 * off storing it in the fake print buffer.
	 */
	dvt_fake_print_just_count = true;
	opt.recursive = true;
	items_in_tree = ARRAY_SIZE(g_uuids) * ARRAY_SIZE(g_oids) *
			ARRAY_SIZE(g_dkeys) * ARRAY_SIZE(g_akeys);
	dvt_fake_print_called = 0;
	assert_success(ddb_run_ls(&ctx, &opt));
	assert_true(items_in_tree <= dvt_fake_print_called);
	dvt_fake_print_just_count = false;

	/* pick a specific oid - each dkey should be printed */
	opt.path = "[0]/[0]";
	opt.recursive = false;
	assert_success(ddb_run_ls(&ctx, &opt));
	assert_true(ARRAY_SIZE(g_dkeys) <= dvt_fake_print_called);

	/* printing a recx works */
	dvt_fake_print_called = 0;
	opt.path = "/[0]/[0]/[0]/[0]/[0]";
	opt.recursive = true;
	assert_success(ddb_run_ls(&ctx, &opt));

	/* invalid paths ... */
	opt.path = buf;

	sprintf(buf, "%s", g_invalid_uuid_str);
	assert_invalid(ddb_run_ls(&ctx, &opt));
	sprintf(buf, "%s/"DF_OID"/", g_uuids_str[0], DP_OID(g_invalid_oid.id_pub));
	assert_invalid(ddb_run_ls(&ctx, &opt));
	dvt_fake_print_reset();

	opt.path = "/[0]/[1]/dkey-3";
	opt.recursive = true;
	assert_success(ddb_run_ls(&ctx, &opt));
	assert_printed_contains("dkey-3");

	opt.path = "/[0]";
	opt.recursive = false;
	/* The output of this command will show which object ID to use for the next one. Can
	 * use g_verbose=true; to see output. Right now kind of manual, but when json output is
	 * implemented, might be able to automate this a little better.
	 */
	assert_success(ddb_run_ls(&ctx, &opt));
	dvt_fake_print_reset();
	opt.path = "/[0]/[0]";
	assert_success(ddb_run_ls(&ctx, &opt));
	g_verbose = false;
	assert_printed_contains("/12345678-1234-1234-1234-123456789001/"
				"281479271743488.4294967296.0.0");
}

static void
dump_value_cmd_tests(void **state)
{
	struct dt_vos_pool_ctx		*tctx = *state;
	struct ddb_ctx			 ctx = {0};
	struct value_dump_options	 opt = {0};

	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_error = dvt_fake_print;
	ctx.dc_io_ft.ddb_write_file = fake_write_file;
	ctx.dc_poh = tctx->dvt_poh;

	/* requires a path to dump */
	assert_invalid(ddb_run_value_dump(&ctx, &opt));

	/* path must be complete (to a value) */
	opt.path = "[0]";
	assert_rc_equal(ddb_run_value_dump(&ctx, &opt), -DDBER_INCOMPLETE_PATH_VALUE);

	/* Path is complete, no destination means will dump to screen */
	opt.path = "[0]/[0]/[0]/[1]";
	assert_success(ddb_run_value_dump(&ctx, &opt));

	/* success */
	opt.dst = "/tmp/dumped_file";
	assert_success(ddb_run_value_dump(&ctx, &opt));
	assert_true(fake_write_file_called >= 1);
}

static void
dump_ilog_cmd_tests(void **state)
{
	struct dt_vos_pool_ctx		*tctx = *state;
	struct ddb_ctx			 ctx = {0};
	struct ilog_dump_options	 opt = {0};

	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_error = dvt_fake_print;
	ctx.dc_io_ft.ddb_write_file = fake_write_file;
	ctx.dc_poh = tctx->dvt_poh;

	assert_invalid(ddb_run_ilog_dump(&ctx, &opt));

	/* Dump object ilog */
	dvt_fake_print_called = 0;
	opt.path = "[0]/[0]";
	assert_success(ddb_run_ilog_dump(&ctx, &opt));
	assert_true(dvt_fake_print_called);

	/* Dump dkey ilog */
	dvt_fake_print_called = 0;
	opt.path = "[0]/[0]/[0]";
	assert_success(ddb_run_ilog_dump(&ctx, &opt));
	assert_true(dvt_fake_print_called);

	/* Dump akey ilog */
	opt.path = "[0]/[0]/[0]/[0]";
	assert_success(ddb_run_ilog_dump(&ctx, &opt));
}

static void
dump_superblock_cmd_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	struct ddb_ctx		 ctx = {0};

	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_poh = tctx->dvt_poh;

	ddb_run_superblock_dump(&ctx);

	assert_true(dvt_fake_print_called >= 1); /* Should have printed at least once */
}

static void
dump_dtx_cmd_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	struct ddb_ctx		 ctx = {0};
	struct dtx_dump_options	 opt = {0};
	daos_handle_t		 coh;

	dvt_fake_print_reset();

	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_error = dvt_fake_print;
	ctx.dc_poh = tctx->dvt_poh;

	assert_invalid(ddb_run_dtx_dump(&ctx, &opt));

	assert_success(vos_cont_open(tctx->dvt_poh, g_uuids[0], &coh));

	dvt_vos_insert_2_records_with_dtx(coh);
	vos_cont_close(coh);

	opt.path = "[0]";
	assert_success(ddb_run_dtx_dump(&ctx, &opt));

	assert_string_contains(dvt_fake_print_buffer, "Active Transactions:");
	assert_string_contains(dvt_fake_print_buffer, "Committed Transactions:");
}

static void
rm_cmd_tests(void **state)
{
	struct rm_options	 opt = {0};

	assert_invalid(ddb_run_rm(&g_ctx, &opt));

	dvt_fake_print_reset();
	opt.path = "[0]";
	assert_success(ddb_run_rm(&g_ctx, &opt));
	assert_string_equal(dvt_fake_print_buffer,
			    "CONT: (/[0]) /12345678-1234-1234-1234-123456789001 deleted\n");
}

static void
load_cmd_tests(void **state)
{
	struct value_load_options	opt = {0};
	char				buf[256];
	daos_unit_oid_t			new_oid = g_oids[0];

	assert_invalid(ddb_run_value_load(&g_ctx, &opt));

	opt.dst = "/[0]/[0]/[0]/[1]";
	opt.src = "/tmp/value_src";
	dvt_fake_get_file_exists_result = true;
	snprintf(dvt_fake_read_file_buf, ARRAY_SIZE(dvt_fake_read_file_buf), "Some text");
	assert_invalid(ddb_run_value_load(&g_ctx, &opt));
	dvt_fake_get_file_size_result = strlen(dvt_fake_read_file_buf);
	dvt_fake_read_file_result = strlen(dvt_fake_read_file_buf);
	assert_success(ddb_run_value_load(&g_ctx, &opt));

	/* add a new 'a' key */
	opt.dst = "/[0]/[0]/[0]/a-new-key";
	assert_success(ddb_run_value_load(&g_ctx, &opt));

	/* add a new 'd' key */
	opt.dst = "/[0]/[0]/a-new-key/a-new-key";
	assert_success(ddb_run_value_load(&g_ctx, &opt));

	/* add a new object */
	new_oid.id_pub.lo = 999;
	sprintf(buf, "%s/"DF_UOID"/dkey_new/akey_new", g_uuids_str[3], DP_UOID(new_oid));
	opt.dst = buf;
	assert_success(ddb_run_value_load(&g_ctx, &opt));

	/*
	 * Error cases ...
	 */

	/* File not found */
	dvt_fake_get_file_exists_result = false;
	assert_invalid(ddb_run_value_load(&g_ctx, &opt));
	dvt_fake_get_file_exists_result = true;

	/* incomplete path */
	opt.dst = "/[0]/[0]/";
	assert_invalid(ddb_run_value_load(&g_ctx, &opt));

	/* Can't use index for a new path */
	opt.dst = "/[0]/[0]/[0]/[9999]";
	assert_rc_equal(-DER_INVAL, ddb_run_value_load(&g_ctx, &opt));

	/* can't create new container */
	sprintf(buf, "%s/"DF_OID"/'dkey_new'/'akey_new'", g_invalid_uuid_str,
		DP_OID(g_oids[0].id_pub));
	opt.dst = buf;
	assert_rc_equal(-DDBER_INVALID_CONT, ddb_run_value_load(&g_ctx, &opt));
}

static void
rm_ilog_cmd_tests(void **state)
{
	struct ilog_clear_options opt = {0};

	assert_invalid(ddb_run_ilog_clear(&g_ctx, &opt));
	opt.path = "[0]"; /* just container ... bad */
	assert_invalid(ddb_run_ilog_clear(&g_ctx, &opt));

	opt.path = "[1]/[0]"; /* object */
	assert_success(ddb_run_ilog_clear(&g_ctx, &opt));
	opt.path = "[2]/[0]/[0]"; /* dkey */
	assert_success(ddb_run_ilog_clear(&g_ctx, &opt));
}

static void
process_ilog_cmd_tests(void **state)
{
	struct ilog_commit_options opt = {0};

	assert_invalid(ddb_run_ilog_commit(&g_ctx, &opt));
	opt.path = "[0]"; /* just container ... bad */
	assert_invalid(ddb_run_ilog_commit(&g_ctx, &opt));

	opt.path = "[1]/[0]"; /* object */
	assert_success(ddb_run_ilog_commit(&g_ctx, &opt));
	opt.path = "[2]/[0]/[0]"; /* dkey */
	assert_success(ddb_run_ilog_commit(&g_ctx, &opt));
}

static void
clear_cmt_dtx_cmd_tests(void **state)
{
	struct dtx_cmt_clear_options opt = {0};

	assert_invalid(ddb_run_dtx_cmt_clear(&g_ctx, &opt));

	opt.path = "[0]";
	assert_success(ddb_run_dtx_cmt_clear(&g_ctx, &opt));
}

static void
dtx_commit_entry_tests(void **state)
{
	struct dtx_act_options opt = {0};

	assert_invalid(ddb_run_dtx_act_commit(&g_ctx, &opt));
	opt.path = "[0]/[0]";
	assert_invalid(ddb_run_dtx_act_commit(&g_ctx, &opt));

	opt.dtx_id = "12345678-1234-1234-1234-123456789012.1234";
	assert_success(ddb_run_dtx_act_commit(&g_ctx, &opt));
}

static void
dtx_abort_entry_tests(void **state)
{
	struct dtx_act_options opt = {0};

	assert_invalid(ddb_run_dtx_act_abort(&g_ctx, &opt));

	opt.path = "[0]/[0]";
	assert_invalid(ddb_run_dtx_act_abort(&g_ctx, &opt));
	opt.dtx_id = "12345678-1234-1234-1234-123456789012.1234";
	assert_success(ddb_run_dtx_act_abort(&g_ctx, &opt));
}

static void
dtx_act_discard_invalid_tests(void **state)
{
	struct dtx_act_options opt = {0};

	g_ctx.dc_write_mode = false;
	assert_invalid(ddb_run_dtx_act_discard_invalid(&g_ctx, &opt));

	g_ctx.dc_write_mode = true;
	assert_invalid(ddb_run_dtx_act_discard_invalid(&g_ctx, &opt));

	opt.path = "[0]/[0]";
	assert_invalid(ddb_run_dtx_act_discard_invalid(&g_ctx, &opt));

	opt.dtx_id = "12345678-1234-1234-1234-123456789012.1234";
	assert_success(ddb_run_dtx_act_discard_invalid(&g_ctx, &opt));

	opt.dtx_id = "all";
	assert_success(ddb_run_dtx_act_discard_invalid(&g_ctx, &opt));
}

static void
feature_cmd_tests(void **state)
{
	struct dt_vos_pool_ctx *tctx;
	struct feature_options  opt = {0};

	tctx = *state;
	assert_invalid(ddb_run_feature(&g_ctx, &opt));
	opt.path          = tctx->dvt_pmem_file;
	opt.show_features = true;
	assert_success(ddb_run_feature(&g_ctx, &opt));
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
	assert_success(dv_pool_open(tctx->dvt_pmem_file, &tctx->dvt_poh, 0));

	g_ctx.dc_poh = tctx->dvt_poh;

	return 0;
}

static int
dcv_suit_teardown(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;

	if (tctx == NULL) {
		fail_msg("Test not setup correctly");
		return -DER_UNKNOWN;
	}

	assert_success(dv_pool_close(tctx->dvt_poh));
	ddb_teardown_vos(state);

	return 0;
}

#define TEST(test) { #test, test, NULL, NULL }

int
ddb_commands_tests_run()
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
	    TEST(clear_cmt_dtx_cmd_tests),
	    TEST(dtx_commit_entry_tests),
	    TEST(dtx_act_discard_invalid_tests),
	    TEST(dtx_abort_entry_tests),
	    TEST(feature_cmd_tests),
	};

	return cmocka_run_group_tests_name("DDB commands tests", tests,
					   dcv_suit_setup, dcv_suit_teardown);
}
