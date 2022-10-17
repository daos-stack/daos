/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <daos/tests_lib.h>
#include <gurt/debug.h>
#include <ddb_common.h>
#include <ddb_parse.h>
#include <ddb_cmd_options.h>
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

	/* test all options and arguments */
	test_run_cmd(&info, "ls", "-r", "path");
	assert_non_null(options->path);
	assert_true(options->recursive);
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
dump_value_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct dump_value_options	*options = &info.dci_cmd_option.dci_dump_value;

	/* test invalid arguments and options */
	test_run_inval_cmd("dump_value", "path", "dst", "extra"); /* too many argument */
	test_run_inval_cmd("dump_value", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "dump_value", "path", "dst");
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
load_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct load_options	*options = &info.dci_cmd_option.dci_load;

	/* test invalid arguments and options */
	test_run_inval_cmd("load", "src", "dst", "extra"); /* too many argument */
	test_run_inval_cmd("load", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "load", "src", "dst");
	assert_non_null(options->src);
	assert_non_null(options->dst);
}

static void
dump_ilog_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct dump_ilog_options	*options = &info.dci_cmd_option.dci_dump_ilog;

	/* test invalid arguments and options */
	test_run_inval_cmd("dump_ilog", "path", "extra"); /* too many argument */
	test_run_inval_cmd("dump_ilog", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "dump_ilog", "path");
	assert_non_null(options->path);
}

static void
commit_ilog_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct commit_ilog_options	*options = &info.dci_cmd_option.dci_commit_ilog;

	/* test invalid arguments and options */
	test_run_inval_cmd("commit_ilog", "path", "extra"); /* too many argument */
	test_run_inval_cmd("commit_ilog", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "commit_ilog", "path");
	assert_non_null(options->path);
}

static void
rm_ilog_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct rm_ilog_options	*options = &info.dci_cmd_option.dci_rm_ilog;

	/* test invalid arguments and options */
	test_run_inval_cmd("rm_ilog", "path", "extra"); /* too many argument */
	test_run_inval_cmd("rm_ilog", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "rm_ilog", "path");
	assert_non_null(options->path);
}

static void
dump_dtx_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct dump_dtx_options	*options = &info.dci_cmd_option.dci_dump_dtx;

	/* test invalid arguments and options */
	test_run_inval_cmd("dump_dtx", "path", "extra"); /* too many argument */
	test_run_inval_cmd("dump_dtx", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "dump_dtx", "path");
	assert_non_null(options->path);
	assert_false(options->active);
	assert_false(options->committed);

	/* test all options and arguments */
	test_run_cmd(&info, "dump_dtx", "-a", "-c", "path");
	assert_non_null(options->path);
	assert_true(options->active);
	assert_true(options->committed);
}

static void
clear_cmt_dtx_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct clear_cmt_dtx_options	*options = &info.dci_cmd_option.dci_clear_cmt_dtx;

	/* test invalid arguments and options */
	test_run_inval_cmd("clear_cmt_dtx", "path", "extra"); /* too many argument */
	test_run_inval_cmd("clear_cmt_dtx", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "clear_cmt_dtx", "path");
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
update_vea_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct update_vea_options	*options = &info.dci_cmd_option.dci_update_vea;

	/* test invalid arguments and options */
	test_run_inval_cmd("update_vea", "offset", "blk_cnt", "extra"); /* too many argument */
	test_run_inval_cmd("update_vea", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "update_vea", "offset", "blk_cnt");
	assert_non_null(options->offset);
	assert_non_null(options->blk_cnt);
}

static void
dtx_commit_options_parsing(void **state)
{
	struct ddb_cmd_info		 info = {0};
	struct dtx_commit_options	*options = &info.dci_cmd_option.dci_dtx_commit;

	/* test invalid arguments and options */
	test_run_inval_cmd("dtx_commit", "path", "dtx_id", "extra"); /* too many argument */
	test_run_inval_cmd("dtx_commit", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "dtx_commit", "path", "dtx_id");
	assert_non_null(options->path);
	assert_non_null(options->dtx_id);
}

static void
dtx_abort_options_parsing(void **state)
{
	struct ddb_cmd_info		 info = {0};
	struct dtx_abort_options	*options = &info.dci_cmd_option.dci_dtx_abort;

	/* test invalid arguments and options */
	test_run_inval_cmd("dtx_abort", "path", "dtx_id", "extra"); /* too many argument */
	test_run_inval_cmd("dtx_abort", "-z"); /* invalid option */

	/* test all arguments */
	test_run_cmd(&info, "dtx_abort", "path", "dtx_id");
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
		TEST(dump_value_options_parsing),
		TEST(rm_options_parsing),
		TEST(load_options_parsing),
		TEST(dump_ilog_options_parsing),
		TEST(commit_ilog_options_parsing),
		TEST(rm_ilog_options_parsing),
		TEST(dump_dtx_options_parsing),
		TEST(clear_cmt_dtx_options_parsing),
		TEST(smd_sync_options_parsing),
		TEST(update_vea_options_parsing),
		TEST(dtx_commit_options_parsing),
		TEST(dtx_abort_options_parsing),
	};

	return cmocka_run_group_tests_name("DDB commands option parsing tests", tests,
					   NULL, NULL);
}
