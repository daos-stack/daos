/**
 * (C) Copyright 2022-2024 Intel Corporation.
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <uuid/uuid.h>

#include "gurt/common.h"
#include <gurt/debug.h>
#include "daos/common.h"
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
	opt.path              = "/[0]/[0]/[0]/[1]/[0]";
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
	opt.path = "[0]/[0]/[0]/[2]";
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

	/* Dump dkey ilog - invalid */
	dvt_fake_print_called = 0;
	opt.path              = "[0]/[0]//";
	assert_rc_equal(ddb_run_ilog_dump(&ctx, &opt), -DER_INVAL);
	assert_true(dvt_fake_print_called);

	/* Dump dkey ilog */
	dvt_fake_print_called = 0;
	opt.path = "[0]/[0]/[0]";
	assert_success(ddb_run_ilog_dump(&ctx, &opt));
	assert_true(dvt_fake_print_called);

	/* Dump akey ilog - invalid */
	opt.path = "[0]/[0]/[0]//";
	assert_rc_equal(ddb_run_ilog_dump(&ctx, &opt), -DER_INVAL);

	/* Dump akey ilog */
	opt.path = "[0]/[0]/[0]/[1]";
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
	struct dtx_dump_options  opt  = {0};

	dvt_fake_print_reset();

	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_error = dvt_fake_print;
	ctx.dc_poh = tctx->dvt_poh;

	assert_invalid(ddb_run_dtx_dump(&ctx, &opt));

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

static void
dtx_stat_tests(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;
	struct ddb_ctx          ctx  = {0};
	struct dtx_stat_options opt  = {0};
	int                     i;
	int                     cont_cnt;

	ctx.dc_poh                     = tctx->dvt_poh;
	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_error   = dvt_fake_print;
	ctx.dc_write_mode              = false;
	opt.path                       = "[0]";
	dvt_fake_print_reset();
	assert_success(ddb_run_dtx_stat(&ctx, &opt));
	assert_regex_match(dvt_fake_print_buffer,
			   "^DTX entries statistics of container "
			   "CONT:[[:blank:]]+\\(/\\[0\\]\\)[[:blank:]]+/[[:digit:]-]+$");
	assert_regex_match(dvt_fake_print_buffer,
			   "^[[:blank:]]+- Committed DTX count:[[:blank:]]+1$");
	assert_regex_match(dvt_fake_print_buffer,
			   "^[[:blank:]]+- DTX aggregated epoch:[[:blank:]]+NA \\(NA\\)$");
	assert_nl_equal(dvt_fake_print_buffer, 3);

	opt.details = true;
	dvt_fake_print_reset();
	assert_success(ddb_run_dtx_stat(&ctx, &opt));
	assert_regex_match(dvt_fake_print_buffer,
			   "^DTX entries statistics of container "
			   "CONT:[[:blank:]]+\\(/\\[0\\]\\)[[:blank:]]+/[[:digit:]-]+$");
	assert_regex_match(
	    dvt_fake_print_buffer,
	    "^[[:blank:]]+- Committed DTX time:[[:blank:]]+min=20.+, max=20.+, mean=20.+$");
	assert_regex_match(
	    dvt_fake_print_buffer,
	    "^[[:blank:]]+- Committed DTX epoch:[[:blank:]]+min=20.+, max=20.+, mean=20.+$");
	assert_regex_match(dvt_fake_print_buffer,
			   "^[[:blank:]]+- DTX aggregated epoch:[[:blank:]]+NA \\(NA\\)$");
	assert_nl_equal(dvt_fake_print_buffer, 5);

	opt.path = "";
	dvt_fake_print_reset();
	assert_success(ddb_run_dtx_stat(&ctx, &opt));
	cont_cnt = (DAOS_ON_VALGRIND) ? 8 : 10;
	for (i = 0; i < cont_cnt; i++) {
		char buf[] = "^DTX entries statistics of container "
			     "CONT:[[:blank:]]+\\(/\\[0\\]\\)[[:blank:]]+/[[:digit:]-]+$";

		buf[59] += i;
		assert_regex_match(dvt_fake_print_buffer, buf);
	}
	assert_regex_match(dvt_fake_print_buffer, "^DTX entries statistics of the pool:$");
}

static uint64_t
dtx_get_cmt_time(char *buf)
{
	uint64_t cmt_time;

	buf = strstr(buf, "- Committed DTX time:");
	assert_non_null(buf);
	buf = strstr(buf, "(");
	assert_non_null(buf);

	buf++;
	cmt_time = 0;
	while (*buf >= '0' && *buf <= '9') {
		cmt_time *= 10;
		cmt_time += *buf - '0';
		buf++;
	}

	return cmt_time;
}

static void
open_cmd_tests(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;
	struct ddb_ctx          ctx  = {0};
	struct open_options     opt  = {0};

	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_error   = dvt_fake_print;

	/* Non-existent path: must return DER_INVAL */
	opt.path = "/non/existent/vos-0";
	assert_invalid(ddb_run_open(&ctx, &opt));

	/* Read-only open: pool handle must be valid, dc_write_mode must be false */
	opt.path       = tctx->dvt_pmem_file;
	opt.write_mode = false;
	assert_success(ddb_run_open(&ctx, &opt));
	assert_true(daos_handle_is_valid(ctx.dc_poh));
	assert_false(ctx.dc_write_mode);
	assert_success(ddb_run_close(&ctx));

	/* Write-mode open: dc_write_mode must be propagated to the context */
	opt.write_mode = true;
	assert_success(ddb_run_open(&ctx, &opt));
	assert_true(daos_handle_is_valid(ctx.dc_poh));
	assert_true(ctx.dc_write_mode);

	/* Pool already open: must return DER_BUSY with an error message */
	dvt_fake_print_reset();
	assert_rc_equal(-DER_BUSY, ddb_run_open(&ctx, &opt));
	assert_printed_contains("Cannot operate on an opened pool. Close it first.");
	assert_success(ddb_run_close(&ctx));
}

static void
dtx_aggr_tests(void **state)
{
	uuid_t                 *p_uuid   = &g_uuids[3];
	struct dt_vos_pool_ctx *tctx     = *state;
	struct ddb_ctx          ctx      = {0};
	struct dtx_stat_options opt_stat = {0};
	struct dtx_aggr_options opt_aggr = {0};
	daos_handle_t           coh;
	char                    buf[256];

	ctx.dc_poh                     = tctx->dvt_poh;
	ctx.dc_io_ft.ddb_print_error   = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_write_mode              = true;

	buf[0] = '/';
	uuid_unparse(*p_uuid, &buf[1]);

	/* Insert 8  mocked DTX entries */
	assert_success(vos_cont_open(tctx->dvt_poh, *p_uuid, &coh));
	dvt_vos_insert_dtx_records(coh, 10, 8);
	assert_success(vos_cont_close(coh));

	opt_stat.path = buf;
	dvt_fake_print_reset();
	assert_success(ddb_run_dtx_stat(&ctx, &opt_stat));
	assert_regex_match(dvt_fake_print_buffer,
			   "^[[:blank:]]+- Committed DTX count:[[:blank:]]+8$");

	/* Test aggregation without epoch (i.e. all 8 DTX entries) */
	opt_aggr.path   = buf;
	opt_aggr.format = DDB_DTX_AGGR_NOW;
	assert_success(ddb_run_dtx_aggr(&ctx, &opt_aggr));

	dvt_fake_print_reset();
	assert_success(ddb_run_dtx_stat(&ctx, &opt_stat));
	assert_regex_match(dvt_fake_print_buffer,
			   "^[[:blank:]]+- Committed DTX count:[[:blank:]]+0$");
	assert_regex_match(
	    dvt_fake_print_buffer,
	    "^[[:blank:]]+- DTX aggregated epoch:[[:blank:]]+.+ \\([[:digit:]]+\\)$");

	/* Insert 10  mocked DTX entries */
	assert_success(vos_cont_open(tctx->dvt_poh, *p_uuid, &coh));
	dvt_vos_insert_dtx_records(coh, 10, 3);
	sleep(2);
	dvt_vos_insert_dtx_records(coh, 10, 7);
	assert_success(vos_cont_close(coh));

	opt_stat.details = true;
	dvt_fake_print_reset();
	assert_success(ddb_run_dtx_stat(&ctx, &opt_stat));
	assert_regex_match(dvt_fake_print_buffer,
			   "^[[:blank:]]+- Committed DTX count:[[:blank:]]+10$");

	/* Test aggregation with an epoch (i.e. aggregate 3 first DTX entries) */
	opt_aggr.format   = DDB_DTX_AGGR_CMT_TIME;
	opt_aggr.cmt_time = dtx_get_cmt_time(dvt_fake_print_buffer) + 1;
	assert_success(ddb_run_dtx_aggr(&ctx, &opt_aggr));

	dvt_fake_print_reset();
	assert_success(ddb_run_dtx_stat(&ctx, &opt_stat));
	assert_regex_match(dvt_fake_print_buffer,
			   "^[[:blank:]]+- Committed DTX count:[[:blank:]]+7$");

	/* Test aggregation without epoch (i.e. aggregate last 7 DTX entries) */
	opt_aggr.format = DDB_DTX_AGGR_NOW;
	assert_success(ddb_run_dtx_aggr(&ctx, &opt_aggr));

	dvt_fake_print_reset();
	assert_success(ddb_run_dtx_stat(&ctx, &opt_stat));
	assert_regex_match(dvt_fake_print_buffer,
			   "^[[:blank:]]+- Committed DTX count:[[:blank:]]+0$");
}

static void
csum_test_sv_path_init(char *path, size_t path_size, const daos_unit_oid_t *oid, const char *akey)
{
	int rc;

	rc = snprintf(path, path_size, "/%s/" DF_UOID "/%s/%s", g_csum_uuid_str, DP_UOID(*oid),
		      g_dkeys_str[0], akey);
	if (rc < 0 || rc >= path_size)
		fail_msg("path buffer too small");
}

static void
csum_dump_error_tests(void **state)
{
	char                    *path_invalid = "foo";
	struct dt_vos_pool_ctx  *tctx         = *state;
	struct ddb_ctx           ctx          = {0};
	struct csum_dump_options opt          = {0};
	int                      rc;

	ctx.dc_poh                     = tctx->dvt_poh;
	ctx.dc_io_ft.ddb_print_error   = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_write_mode              = false;

	rc = ddb_run_csum_dump(&ctx, &opt);
	assert_rc_equal(-DER_INVAL, rc);
	assert_string_contains(dvt_fake_print_buffer, "A VOS path to dump is required.");
	dvt_fake_print_reset();

	opt.path = &path_invalid[0];
	rc       = ddb_run_csum_dump(&ctx, &opt);
	assert_rc_equal(-DER_INVAL, rc);
	assert_string_contains(dvt_fake_print_buffer, "Container is invalid");
	dvt_fake_print_reset();
}

static void
csumbuf_dump(char *buf, uint8_t *csumbuf, size_t csumbuf_size)
{
	size_t i;

	for (i = 0; i < csumbuf_size; i++)
		buf += sprintf(buf, "%02" PRIx8, csumbuf[i]);
	buf[0] = '\0';
}

static void
print_csum_sv_tests(void **state)
{
	const char              *regex_prf = "0x";
	struct dt_vos_pool_ctx  *tctx      = *state;
	struct dt_csum_ctx      *csum_ctx  = tctx->dvt_extra;
	struct ddb_ctx           ctx       = {0};
	struct csum_dump_options opt       = {0};
	char                     path[128];
	char                     buf[256];
	struct dcs_csum_info    *ci;
	int                      rc;

	ctx.dc_poh                     = tctx->dvt_poh;
	ctx.dc_io_ft.ddb_print_error   = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_write_mode              = false;

	opt.path  = path;
	opt.epoch = DAOS_EPOCH_MAX;

	/* no csum info (g_oids[0]: SV at epoch 1, no checksum stored) */
	csum_test_sv_path_init(path, sizeof(path), &g_oids[0], g_akeys_str[0]);
	assert_success(ddb_run_csum_dump(&ctx, &opt));
	rc = snprintf(buf, sizeof(buf), "^No checksum at AKEY:[[:blank:]].+[[:blank:]]%s$", path);
	assert_true(rc > 0 && rc < sizeof(buf));
	assert_regex_match(dvt_fake_print_buffer, buf);
	dvt_fake_print_reset();

	/* with csum info, EPOCH_MAX returns the epoch-2 (latest) checksum */
	csum_test_sv_path_init(path, sizeof(path), &g_oids[1], g_akeys_str[0]);
	assert_success(ddb_run_csum_dump(&ctx, &opt));
	assert_string_contains(dvt_fake_print_buffer, "Epoch: 2");
	memcpy(buf, regex_prf, strlen(regex_prf));
	ci = csum_ctx->dct_sv_ics[1]->ic_data;
	csumbuf_dump(buf + strlen(regex_prf), ci_idx2csum(ci, 0), ci->cs_len);
	assert_string_contains(dvt_fake_print_buffer, buf);
	dvt_fake_print_reset();

	/* with csum info, epoch 1 returns the epoch-1 checksum */
	opt.epoch = 1;
	assert_success(ddb_run_csum_dump(&ctx, &opt));
	assert_string_contains(dvt_fake_print_buffer, "Epoch: 1");
	memcpy(buf, regex_prf, strlen(regex_prf));
	ci = csum_ctx->dct_sv_ics[0]->ic_data;
	csumbuf_dump(buf + strlen(regex_prf), ci_idx2csum(ci, 0), ci->cs_len);
	assert_string_contains(dvt_fake_print_buffer, buf);
	dvt_fake_print_reset();
}

static int
csum_sv_fake_write_file(const char *dst_path, d_iov_t *contents)
{
	struct dcs_csum_info *ci;

	assert_string_equal(dst_path, mock_ptr_type(const char *));

	ci = mock_ptr_type(struct dcs_csum_info *);
	assert_true(contents->iov_len == ci->cs_buf_len);
	assert_true(memcmp(contents->iov_buf, ci_idx2csum(ci, 0), ci->cs_buf_len) == 0);

	return mock();
}

static void
write_csum_sv_tests(void **state)
{
	char                    *path_dst = "/tmp/write_csum_sv_test_output.dat";
	struct dt_vos_pool_ctx  *tctx     = *state;
	struct dt_csum_ctx      *csum_ctx = tctx->dvt_extra;
	struct ddb_ctx           ctx      = {0};
	struct csum_dump_options opt      = {0};
	char                     path[128];
	char                     buf[256];
	int                      rc;

	ctx.dc_poh                     = tctx->dvt_poh;
	ctx.dc_io_ft.ddb_print_error   = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_io_ft.ddb_write_file    = csum_sv_fake_write_file;
	ctx.dc_write_mode              = false;

	opt.path  = path;
	opt.epoch = DAOS_EPOCH_MAX;
	opt.dst   = path_dst;

	/* no csum info */
	csum_test_sv_path_init(path, sizeof(path), &g_oids[0], g_akeys_str[0]);
	assert_success(ddb_run_csum_dump(&ctx, &opt));
	rc = snprintf(buf, sizeof(buf), "^No checksum at AKEY:[[:blank:]].+[[:blank:]]%s$", path);
	assert_true(rc > 0 && rc < sizeof(buf));
	assert_regex_match(dvt_fake_print_buffer, buf);
	dvt_fake_print_reset();

	/* with csum info, EPOCH_MAX returns the epoch-2 (latest) checksum */
	csum_test_sv_path_init(path, sizeof(path), &g_oids[1], g_akeys_str[0]);
	will_return(csum_sv_fake_write_file, path_dst);
	will_return(csum_sv_fake_write_file, csum_ctx->dct_sv_ics[1]->ic_data);
	will_return(csum_sv_fake_write_file, 0);
	assert_success(ddb_run_csum_dump(&ctx, &opt));
	assert_string_contains(dvt_fake_print_buffer, "Dumping checksum");
	assert_string_contains(dvt_fake_print_buffer, "epoch: 2");
	dvt_fake_print_reset();

	/* with csum info, epoch 1 returns the epoch-1 checksum */
	opt.epoch = 1;
	will_return(csum_sv_fake_write_file, path_dst);
	will_return(csum_sv_fake_write_file, csum_ctx->dct_sv_ics[0]->ic_data);
	will_return(csum_sv_fake_write_file, 0);
	assert_success(ddb_run_csum_dump(&ctx, &opt));
	assert_string_contains(dvt_fake_print_buffer, "Dumping checksum");
	assert_string_contains(dvt_fake_print_buffer, "epoch: 1");
	dvt_fake_print_reset();
}

static void
csum_test_recx_path_init(char *path, size_t path_size, const daos_unit_oid_t *oid, const char *akey,
			 const daos_recx_t *recx)
{
	int rc;

	rc = snprintf(path, path_size, "/%s/" DF_UOID "/%s/%s/{" DF_U64 "-" DF_U64 "}",
		      g_csum_uuid_str, DP_UOID(*oid), g_dkeys_str[0], akey, recx->rx_idx,
		      recx->rx_idx + recx->rx_nr - 1);
	if (rc < 0 || rc >= path_size)
		fail_msg("path buffer too small");
}

static void
print_csum_recx_tests(void **state)
{
	const char              *regex_prf = "0x";
	struct dt_vos_pool_ctx  *tctx      = *state;
	struct dt_csum_ctx      *csum_ctx  = tctx->dvt_extra;
	struct ddb_ctx           ctx       = {0};
	struct csum_dump_options opt       = {0};
	daos_recx_t              recx      = {.rx_idx = 0, .rx_nr = csum_ctx->dct_recx_size};
	char                     path[128];
	char                     buf[256];
	char                    *buf_csum;
	int                      i;
	int                      rc;

	ctx.dc_poh                     = tctx->dvt_poh;
	ctx.dc_io_ft.ddb_print_error   = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_write_mode              = false;

	opt.path  = path;
	opt.epoch = DAOS_EPOCH_MAX;

	/* no csum info */
	csum_test_recx_path_init(path, sizeof(path), &g_oids[0], g_akeys_str[1], &recx);
	assert_success(ddb_run_csum_dump(&ctx, &opt));
	rc = snprintf(buf, sizeof(buf), "^No checksum at RECX:[[:blank:]].+$");
	assert_true(rc > 0 && rc < sizeof(buf));
	assert_regex_match(dvt_fake_print_buffer, buf);
	dvt_fake_print_reset();

	csum_test_recx_path_init(path, sizeof(path), &g_oids[1], g_akeys_str[1], &recx);
	assert_success(ddb_run_csum_dump(&ctx, &opt));
	memcpy(buf, regex_prf, strlen(regex_prf));
	buf_csum = buf + strlen(regex_prf);
	for (i = 0; i < DVT_FAKE_RECX_COUNT; i++) {
		int                   csum_idx;
		struct dcs_csum_info *ci;

		ci = csum_ctx->dct_recx_ics[i]->ic_data;
		for (csum_idx = 0; csum_idx < ci->cs_nr; ++csum_idx) {
			csumbuf_dump(buf_csum, ci_idx2csum(ci, csum_idx), ci->cs_len);
			assert_string_contains(dvt_fake_print_buffer, buf);
		}
	}
	dvt_fake_print_reset();
}

static int
csum_recx_fake_write_file(const char *dst_path, d_iov_t *contents)
{
	int      i;
	uint8_t *buf;

	assert_string_equal(dst_path, mock_ptr_type(const char *));

	buf = (uint8_t *)contents->iov_buf;
	for (i = 0; i < DVT_FAKE_RECX_COUNT; i++) {
		int                   idx;
		struct dcs_csum_info *ci;

		ci = mock_ptr_type(struct dcs_csum_info *);
		for (idx = 0; idx < ci->cs_nr; ++idx) {
			assert_true(memcmp(buf, ci_idx2csum(ci, idx), ci->cs_len) == 0);
			buf += ci->cs_len;
		}
	}
	assert_true(buf - (uint8_t *)contents->iov_buf == contents->iov_len);

	return mock();
}

static void
write_csum_recx_tests(void **state)
{
	char                    *path_dst = "/tmp/write_csum_recx_test_output.dat";
	struct dt_vos_pool_ctx  *tctx     = *state;
	struct dt_csum_ctx      *csum_ctx = tctx->dvt_extra;
	struct ddb_ctx           ctx      = {0};
	struct csum_dump_options opt      = {0};
	daos_recx_t              recx     = {.rx_idx = 0, .rx_nr = csum_ctx->dct_recx_size};
	char                     path[128];
	char                     buf[256];
	int                      i;
	int                      rc;

	ctx.dc_poh                     = tctx->dvt_poh;
	ctx.dc_io_ft.ddb_print_error   = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_io_ft.ddb_write_file    = csum_recx_fake_write_file;
	ctx.dc_write_mode              = false;

	opt.path  = path;
	opt.epoch = DAOS_EPOCH_MAX;
	opt.dst   = path_dst;

	csum_test_recx_path_init(path, sizeof(path), &g_oids[0], g_akeys_str[1], &recx);
	assert_success(ddb_run_csum_dump(&ctx, &opt));
	rc = snprintf(buf, sizeof(buf), "^No checksum at RECX:[[:blank:]].+$");
	assert_true(rc > 0 && rc < sizeof(buf));
	assert_regex_match(dvt_fake_print_buffer, buf);
	dvt_fake_print_reset();

	csum_test_recx_path_init(path, sizeof(path), &g_oids[1], g_akeys_str[1], &recx);
	will_return(csum_recx_fake_write_file, path_dst);
	for (i = 0; i < DVT_FAKE_RECX_COUNT; i++)
		will_return(csum_recx_fake_write_file, csum_ctx->dct_recx_ics[i]->ic_data);
	will_return(csum_recx_fake_write_file, 0);
	assert_success(ddb_run_csum_dump(&ctx, &opt));
	assert_string_contains(dvt_fake_print_buffer, "Dumping checksum");
	dvt_fake_print_reset();
}

static void
csum_check_error_tests(void **state)
{
	char                     *path_invalid = "foo";
	struct dt_vos_pool_ctx   *tctx         = *state;
	struct ddb_ctx            ctx          = {0};
	struct csum_check_options opt          = {0};
	char                      path[128];
	int                       rc;

	ctx.dc_poh                     = tctx->dvt_poh;
	ctx.dc_io_ft.ddb_print_error   = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_write_mode              = false;

	rc = ddb_run_csum_check(&ctx, &opt);
	assert_rc_equal(-DER_INVAL, rc);
	assert_string_contains(dvt_fake_print_buffer, "A VOS path to check is required.");
	dvt_fake_print_reset();

	opt.path = &path_invalid[0];
	rc       = ddb_run_csum_check(&ctx, &opt);
	assert_rc_equal(-DER_INVAL, rc);
	assert_string_contains(dvt_fake_print_buffer, "Container is invalid");
	dvt_fake_print_reset();

	/* path must be complete (point to a value): an array akey path with no extent
	 * resolves to neither a RECX nor an SV value, so it is rejected as incomplete. */
	opt.path = path;
	csum_test_sv_path_init(path, sizeof(path), &g_oids[1], g_akeys_str[1]);
	rc = ddb_run_csum_check(&ctx, &opt);
	assert_rc_equal(-DDBER_INCOMPLETE_PATH_VALUE, rc);
	assert_string_contains(dvt_fake_print_buffer, "is incomplete");
	dvt_fake_print_reset();
}

static void
check_csum_sv_tests(void **state)
{
	const char               *regex_prf = "0x";
	struct dt_vos_pool_ctx   *tctx      = *state;
	struct dt_csum_ctx       *csum_ctx  = tctx->dvt_extra;
	struct ddb_ctx            ctx       = {0};
	struct csum_check_options opt       = {0};
	char                      path[128];
	char                      buf[256];
	uint8_t                   good_bytes[64];
	struct dcs_csum_info     *ci;
	int                       rc;

	ctx.dc_poh                     = tctx->dvt_poh;
	ctx.dc_io_ft.ddb_print_error   = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_write_mode              = false;

	opt.path  = path;
	opt.epoch = DAOS_EPOCH_MAX;

	/* no csum info (g_oids[0]: SV at epoch 1, no checksum stored, non-verbose) */
	csum_test_sv_path_init(path, sizeof(path), &g_oids[0], g_akeys_str[0]);
	assert_success(ddb_run_csum_check(&ctx, &opt));
	assert_string_equal(dvt_fake_print_buffer, "");
	dvt_fake_print_reset();

	/* valid, matching checksum, default (non-verbose) */
	csum_test_sv_path_init(path, sizeof(path), &g_oids[1], g_akeys_str[0]);
	assert_success(ddb_run_csum_check(&ctx, &opt));
	assert_string_equal(dvt_fake_print_buffer, "");
	dvt_fake_print_reset();

	/* same, but --verbose: every entry is printed (matching, in this case). */
	opt.verbose = true;
	csum_test_sv_path_init(path, sizeof(path), &g_oids[1], g_akeys_str[0]);
	assert_success(ddb_run_csum_check(&ctx, &opt));
	assert_string_contains(dvt_fake_print_buffer, "Epoch: 2");
	memcpy(buf, regex_prf, strlen(regex_prf));
	ci = csum_ctx->dct_sv_ics[1]->ic_data;
	csumbuf_dump(buf + strlen(regex_prf), ci_idx2csum(ci, 0), ci->cs_len);
	assert_string_contains(dvt_fake_print_buffer, buf);
	assert_string_contains(dvt_fake_print_buffer, "State: OK");
	assert_string_contains(dvt_fake_print_buffer, "OK: no corruption detected.");
	dvt_fake_print_reset();
	opt.verbose = false;

	/* deliberately corrupted checksum: reports CORRUPTED and -DER_CSUM, along with the
	 * want (stored)/got (recomputed) checksum values -- always printed regardless of
	 * verbose, since corrupted entries are never suppressed. */
	csum_test_sv_path_init(path, sizeof(path), &g_oids[2], g_akeys_str[0]);
	rc = ddb_run_csum_check(&ctx, &opt);
	assert_rc_equal(-DER_CSUM, rc);
	assert_string_contains(dvt_fake_print_buffer, "State: NOK");
	assert_string_contains(dvt_fake_print_buffer, "Data corruption detected");

	/* "want" is the stored (corrupted) checksum, shown on the main Value: line; "got" is
	 * the checksum recomputed from the still-valid data, shown in "(got=...)". The fixture
	 * only flips the stored checksum's first byte before writing it (see
	 * csum_test_corrupt_sv_setup()), so "got" is "want" with that flip undone. */
	ci = csum_ctx->dct_sv_ic_bad->ic_data;
	strcpy(buf, "Value: 0x");
	csumbuf_dump(buf + strlen(buf), ci_idx2csum(ci, 0), ci->cs_len);
	assert_string_contains(dvt_fake_print_buffer, buf);

	memcpy(good_bytes, ci_idx2csum(ci, 0), ci->cs_len);
	good_bytes[0] ^= 0xff;
	strcpy(buf, "(got=0x");
	csumbuf_dump(buf + strlen(buf), good_bytes, ci->cs_len);
	assert_string_contains(dvt_fake_print_buffer, buf);
	dvt_fake_print_reset();
}

static void
check_csum_recx_tests(void **state)
{
	const char               *regex_prf = "0x";
	struct dt_vos_pool_ctx   *tctx      = *state;
	struct dt_csum_ctx       *csum_ctx  = tctx->dvt_extra;
	struct ddb_ctx            ctx       = {0};
	struct csum_check_options opt       = {0};
	daos_recx_t               recx      = {.rx_idx = 0, .rx_nr = csum_ctx->dct_recx_size};
	char                      path[128];
	char                      buf[256];
	char                     *buf_csum;
	uint8_t                   good_bytes[64];
	char                      got_buf[64];
	struct dcs_csum_info     *ci;
	int                       i;
	int                       rc;

	ctx.dc_poh                     = tctx->dvt_poh;
	ctx.dc_io_ft.ddb_print_error   = dvt_fake_print;
	ctx.dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx.dc_write_mode              = false;

	opt.path  = path;
	opt.epoch = DAOS_EPOCH_MAX;

	/* no csum info, non-verbose: fully silent (no path/header, no summary). */
	csum_test_recx_path_init(path, sizeof(path), &g_oids[0], g_akeys_str[1], &recx);
	assert_success(ddb_run_csum_check(&ctx, &opt));
	assert_string_equal(dvt_fake_print_buffer, "");
	dvt_fake_print_reset();

	/* same, but --verbose: "No checksum at ..." is reported, plus the final "OK" summary
	 * (still success -- there's simply nothing to check). */
	opt.verbose = true;
	csum_test_recx_path_init(path, sizeof(path), &g_oids[0], g_akeys_str[1], &recx);
	assert_success(ddb_run_csum_check(&ctx, &opt));
	assert_string_contains(dvt_fake_print_buffer, "No checksum at ");
	assert_string_contains(dvt_fake_print_buffer, "OK: no corruption detected.");
	dvt_fake_print_reset();
	opt.verbose = false;

	/* valid, matching checksums, default (non-verbose): fully silent, no per-entry
	 * detail and no final summary line either. */
	csum_test_recx_path_init(path, sizeof(path), &g_oids[1], g_akeys_str[1], &recx);
	assert_success(ddb_run_csum_check(&ctx, &opt));
	assert_string_equal(dvt_fake_print_buffer, "");
	dvt_fake_print_reset();

	/* same, but --verbose: every entry is printed (matching, for each segment here). */
	opt.verbose = true;
	csum_test_recx_path_init(path, sizeof(path), &g_oids[1], g_akeys_str[1], &recx);
	assert_success(ddb_run_csum_check(&ctx, &opt));
	memcpy(buf, regex_prf, strlen(regex_prf));
	buf_csum = buf + strlen(regex_prf);
	for (i = 0; i < DVT_FAKE_RECX_COUNT; i++) {
		int csum_idx;

		ci = csum_ctx->dct_recx_ics[i]->ic_data;
		for (csum_idx = 0; csum_idx < ci->cs_nr; ++csum_idx) {
			csumbuf_dump(buf_csum, ci_idx2csum(ci, csum_idx), ci->cs_len);
			assert_string_contains(dvt_fake_print_buffer, buf);
		}
	}
	assert_string_contains(dvt_fake_print_buffer, "State: OK");
	assert_string_contains(dvt_fake_print_buffer, "OK: no corruption detected.");
	dvt_fake_print_reset();
	opt.verbose = false;

	/* shared by both the non-verbose and --verbose corrupted-checksum checks below: the
	 * bad entry's stored (corrupted) checksum ("want") and its recomputed value ("got") */
	ci = csum_ctx->dct_recx_ics_bad[DVT_FAKE_RECX_BAD_IDX]->ic_data;
	strcpy(buf, "Checksum Value(s): 0x");
	csumbuf_dump(buf + strlen(buf), ci_idx2csum(ci, 0), ci->cs_len);
	memcpy(good_bytes, ci_idx2csum(ci, 0), ci->cs_len);
	good_bytes[0] ^= 0xff;
	strcpy(got_buf, " (got=0x");
	csumbuf_dump(got_buf + strlen(got_buf), good_bytes, ci->cs_len);

	/* g_oids[2]: one of DVT_FAKE_RECX_COUNT segments has a corrupted checksum. Non-verbose
	 * still reports it (corrupted entries are never suppressed); the good entry is
	 * suppressed here (see --verbose below). */
	csum_test_recx_path_init(path, sizeof(path), &g_oids[2], g_akeys_str[1], &recx);
	rc = ddb_run_csum_check(&ctx, &opt);
	assert_rc_equal(-DER_CSUM, rc);
	assert_string_contains(dvt_fake_print_buffer, "State: NOK");
	assert_string_contains(dvt_fake_print_buffer, "Data corruption detected");
	assert_string_contains(dvt_fake_print_buffer, buf);
	assert_string_contains(dvt_fake_print_buffer, got_buf);
	assert_string_not_contains(dvt_fake_print_buffer, "State: OK");
	dvt_fake_print_reset();

	/* same, but --verbose: the good entry must report "State: OK\n" (no "(got=...)"),
	 * proving the bad entry's corruption didn't bleed into it (the bug fixed in
	 * 65dacd2e4a). */
	opt.verbose = true;
	rc          = ddb_run_csum_check(&ctx, &opt);
	assert_rc_equal(-DER_CSUM, rc);
	assert_string_contains(dvt_fake_print_buffer, "State: OK\n");
	assert_string_contains(dvt_fake_print_buffer, "State: NOK");
	assert_string_contains(dvt_fake_print_buffer, buf);
	assert_string_contains(dvt_fake_print_buffer, got_buf);
	dvt_fake_print_reset();
}

/*
 * --------------------------------------------------------------
 * End test functions
 * --------------------------------------------------------------
 */

static int
dcv_suit_setup(void **state)
{
	struct ddb_ctx          ctx = {0};
	struct dt_vos_pool_ctx *tctx;
	daos_handle_t           coh;

	assert_success(ddb_test_setup_vos(state));

	/* test setup creates the pool, but doesn't open it ... leave it open for these tests */
	tctx = *state;
	ctx.dc_write_mode = true;
	assert_success(dv_pool_open(tctx->dvt_pmem_file, NULL, &ctx.dc_poh, 0, ctx.dc_write_mode));
	tctx->dvt_poh = ctx.dc_poh;
	g_ctx.dc_poh = tctx->dvt_poh;

	assert_success(vos_cont_open(ctx.dc_poh, g_uuids[0], &coh));

	/* Seed the first container with 1 active + 1 committed DTX entry required by
	 * dtx_stat_tests, dtx_commit_entry_tests, dtx_act_discard_invalid_tests, and
	 * dtx_abort_entry_tests. */
	dvt_vos_insert_2_records_with_dtx(coh);
	vos_cont_close(coh);

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

static int
dcv_test_csum_setup(void **state)
{
	struct ddb_ctx          ctx  = {0};
	struct dt_vos_pool_ctx *tctx = *state;

	ctx.dc_write_mode = true;
	assert_success(dv_pool_open(tctx->dvt_pmem_file, NULL, &ctx.dc_poh, 0, ctx.dc_write_mode));
	tctx->dvt_poh = ctx.dc_poh;

	assert_success(ddb_test_csum_setup(state));

	return 0;
}

static int
dcv_test_csum_teardown(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;

	ddb_test_csum_teardown(state);

	assert_success(dv_pool_close(tctx->dvt_poh));

	return 0;
}

#define TEST(test) { #test, test, NULL, NULL }
#define TEST_CSUM(test) {#test, test, dcv_test_csum_setup, dcv_test_csum_teardown}

int
ddb_commands_tests_run()
{
	const struct CMUnitTest tests[] = {
	    TEST(ls_cmd_tests),
	    TEST(dump_value_cmd_tests),
	    TEST(dump_ilog_cmd_tests),
	    TEST(dump_superblock_cmd_tests),
	    TEST(dump_dtx_cmd_tests),
	    TEST(dtx_stat_tests),
	    TEST(rm_cmd_tests),
	    TEST(load_cmd_tests),
	    TEST(rm_ilog_cmd_tests),
	    TEST(process_ilog_cmd_tests),
	    TEST(clear_cmt_dtx_cmd_tests),
	    TEST(dtx_commit_entry_tests),
	    TEST(dtx_act_discard_invalid_tests),
	    TEST(dtx_abort_entry_tests),
	    TEST(feature_cmd_tests),
	    TEST(open_cmd_tests),
	    TEST(dtx_aggr_tests),
	    TEST_CSUM(csum_dump_error_tests),
	    TEST_CSUM(print_csum_sv_tests),
	    TEST_CSUM(write_csum_sv_tests),
	    TEST_CSUM(print_csum_recx_tests),
	    TEST_CSUM(write_csum_recx_tests),
	    TEST_CSUM(csum_check_error_tests),
	    TEST_CSUM(check_csum_sv_tests),
	    TEST_CSUM(check_csum_recx_tests),
	};

	return cmocka_run_group_tests_name("DDB commands tests", tests,
					   dcv_suit_setup, dcv_suit_teardown);
}
