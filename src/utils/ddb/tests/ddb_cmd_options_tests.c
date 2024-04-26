/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <daos/tests_lib.h>
#include <gurt/debug.h>
#include <ddb_common.h>
#include <ddb_parse.h>
#include <ddb.h>
#include "ddb_cmocka.h"
#include "ddb_test_driver.h"

#define test_run_inval_cmd(...) \
	assert_rc_equal(-DER_INVAL, __test_run_cmd(NULL, (char *[]){__VA_ARGS__, NULL}))
#define test_run_cmd(ctx, ...) \
	assert_success(__test_run_cmd(ctx, (char *[]){__VA_ARGS__, NULL}))

static int
fake_print(const char *fmt, ...)
{
	return 0;
}

static int
__test_run_cmd(struct ddb_cmd_info *info, char *argv[])
{
	struct argv_parsed	parse_args = {0};
	uint32_t		argc = 0;
	struct ddb_ctx		ctx = {0};
	struct ddb_cmd_info	tmp_info = {0};
	int			rc;

	ctx.dc_io_ft.ddb_print_message = fake_print;
	ctx.dc_io_ft.ddb_print_error = fake_print;
	if (info == NULL)
		info = &tmp_info;

	assert_non_null(argv);
	if (g_verbose)
		printf("Command: ");
	while (argv[argc] != NULL) {
		if (g_verbose)
			printf("%s ", argv[argc]);
		argc++;
	}
	if (g_verbose)
		printf("\n");

	parse_args.ap_argv = argv;
	parse_args.ap_argc = argc;

	rc = ddb_parse_cmd_args(&ctx, parse_args.ap_argc, parse_args.ap_argv, info);

	if (!SUCCESS(rc))
		return rc;

	return rc;
}

static void
ls_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct ls_options	*options = &info.dci_cmd_option.dci_ls;

	/* test invalid arguments and options */
	test_run_inval_cmd("ls", "path", "extra"); /* too many argument */
	test_run_inval_cmd("ls", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "ls", "path");
	assert_non_null(options->path);
	assert_false(options->recursive);
	assert_false(options->details);

	/* test all options and arguments */
	test_run_cmd(&info, "ls", "-r", "-d", "path");
	assert_non_null(options->path);
	assert_true(options->recursive);
	assert_true(options->details);
}

static void
open_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct open_options	*options = &info.dci_cmd_option.dci_open;

	/* test invalid arguments and options */
	test_run_inval_cmd("open", "path", "extra"); /* too many argument */
	test_run_inval_cmd("open", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "open", "path");
	assert_non_null(options->path);
	assert_false(options->write_mode);

	/* test all options and arguments */
	test_run_cmd(&info, "open", "-w", "path");
	assert_non_null(options->path);
	assert_true(options->write_mode);
}

static void
value_dump_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct value_dump_options	*options = &info.dci_cmd_option.dci_value_dump;

	/* test invalid arguments and options */
	test_run_inval_cmd("value_dump", "path", "dst", "extra"); /* too many argument */
	test_run_inval_cmd("value_dump", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "value_dump", "path", "dst");
	assert_non_null(options->path);
	assert_non_null(options->dst);
}

static void
rm_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct rm_options	*options = &info.dci_cmd_option.dci_rm;

	/* test invalid arguments and options */
	test_run_inval_cmd("rm", "path", "extra"); /* too many argument */
	test_run_inval_cmd("rm", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "rm", "path");
	assert_non_null(options->path);
}

static void
value_load_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct value_load_options	*options = &info.dci_cmd_option.dci_value_load;

	/* test invalid arguments and options */
	test_run_inval_cmd("value_load", "src", "dst", "extra"); /* too many argument */
	test_run_inval_cmd("value_load", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "value_load", "src", "dst");
	assert_non_null(options->src);
	assert_non_null(options->dst);
}

static void
ilog_dump_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct ilog_dump_options	*options = &info.dci_cmd_option.dci_ilog_dump;

	/* test invalid arguments and options */
	test_run_inval_cmd("ilog_dump", "path", "extra"); /* too many argument */
	test_run_inval_cmd("ilog_dump", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "ilog_dump", "path");
	assert_non_null(options->path);
}

static void
ilog_commit_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct ilog_commit_options	*options = &info.dci_cmd_option.dci_ilog_commit;

	/* test invalid arguments and options */
	test_run_inval_cmd("ilog_commit", "path", "extra"); /* too many argument */
	test_run_inval_cmd("ilog_commit", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "ilog_commit", "path");
	assert_non_null(options->path);
}

static void
ilog_clear_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct ilog_clear_options	*options = &info.dci_cmd_option.dci_ilog_clear;

	/* test invalid arguments and options */
	test_run_inval_cmd("ilog_clear", "path", "extra"); /* too many argument */
	test_run_inval_cmd("ilog_clear", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "ilog_clear", "path");
	assert_non_null(options->path);
}

static void
dtx_dump_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct dtx_dump_options	*options = &info.dci_cmd_option.dci_dtx_dump;

	/* test invalid arguments and options */
	test_run_inval_cmd("dtx_dump", "path", "extra"); /* too many argument */
	test_run_inval_cmd("dtx_dump", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "dtx_dump", "path");
	assert_non_null(options->path);
	assert_false(options->active);
	assert_false(options->committed);

	/* test all options and arguments */
	test_run_cmd(&info, "dtx_dump", "-a", "-c", "path");
	assert_non_null(options->path);
	assert_true(options->active);
	assert_true(options->committed);
}

static void
dtx_cmt_clear_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct dtx_cmt_clear_options	*options = &info.dci_cmd_option.dci_dtx_cmt_clear;

	/* test invalid arguments and options */
	test_run_inval_cmd("dtx_cmt_clear", "path", "extra"); /* too many argument */
	test_run_inval_cmd("dtx_cmt_clear", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "dtx_cmt_clear", "path");
	assert_non_null(options->path);
}

static void
smd_sync_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct smd_sync_options	*options = &info.dci_cmd_option.dci_smd_sync;

	/* test invalid arguments and options */
	test_run_inval_cmd("smd_sync", "nvme_conf", "db_path", "extra"); /* too many argument */
	test_run_inval_cmd("smd_sync", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "smd_sync", "nvme_conf", "db_path");
	assert_non_null(options->nvme_conf);
	assert_non_null(options->db_path);
}

static void
vea_update_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct vea_update_options	*options = &info.dci_cmd_option.dci_vea_update;

	/* test invalid arguments and options */
	test_run_inval_cmd("vea_update", "offset", "blk_cnt", "extra"); /* too many argument */
	test_run_inval_cmd("vea_update", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "vea_update", "offset", "blk_cnt");
	assert_non_null(options->offset);
	assert_non_null(options->blk_cnt);
}

static void
dtx_act_commit_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct dtx_act_commit_options	*options = &info.dci_cmd_option.dci_dtx_act_commit;

	/* test invalid arguments and options */
	test_run_inval_cmd("dtx_act_commit", "path", "dtx_id", "extra"); /* too many argument */
	test_run_inval_cmd("dtx_act_commit", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "dtx_act_commit", "path", "dtx_id");
	assert_non_null(options->path);
	assert_non_null(options->dtx_id);
}

static void
dtx_act_abort_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct dtx_act_abort_options	*options = &info.dci_cmd_option.dci_dtx_act_abort;

	/* test invalid arguments and options */
	test_run_inval_cmd("dtx_act_abort", "path", "dtx_id", "extra"); /* too many argument */
	test_run_inval_cmd("dtx_act_abort", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "dtx_act_abort", "path", "dtx_id");
	assert_non_null(options->path);
	assert_non_null(options->dtx_id);
}

/*
 * -----------------------------------------------
 * Execute
 * -----------------------------------------------
 */
#define TEST(x) { #x, x, NULL, NULL }
int
ddb_cmd_options_tests_run()
{
	static const struct CMUnitTest tests[] = {
		TEST(ls_options_parsing),
		TEST(open_options_parsing),
		TEST(value_dump_options_parsing),
		TEST(rm_options_parsing),
		TEST(value_load_options_parsing),
		TEST(ilog_dump_options_parsing),
		TEST(ilog_commit_options_parsing),
		TEST(ilog_clear_options_parsing),
		TEST(dtx_dump_options_parsing),
		TEST(dtx_cmt_clear_options_parsing),
		TEST(smd_sync_options_parsing),
		TEST(vea_update_options_parsing),
		TEST(dtx_act_commit_options_parsing),
		TEST(dtx_act_abort_options_parsing),
	};

	return cmocka_run_group_tests_name("DDB commands option parsing tests", tests,
					   NULL, NULL);
}
