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
	assert_rc_equal(-DER_INVAL, __test_run_cmd(NULL, (char *[]){"prog_name", \
	__VA_ARGS__, NULL}))
#define test_run_cmd(ctx, ...) \
	assert_success(__test_run_cmd(ctx, (char *[]){"prog_name", __VA_ARGS__, NULL}))

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

	rc = ddb_parse_cmd_args(&ctx, &parse_args, info);

	if (!SUCCESS(rc))
		return rc;

	return rc;
}

static void
ls_options_parsing(void **state)
{
	struct ddb_cmd_info	 info = {0};
	struct ls_options	*options = &info.dci_cmd_option.dci_ls;

	test_run_inval_cmd("ls", "path", "invalid_argument"); /* invalid argument */
	test_run_inval_cmd("ls", "-z"); /* invalid option */

	test_run_cmd(&info, "ls");
	assert_false(options->recursive);
	assert_null(options->path);

	test_run_cmd(&info, "ls", "-r");
	assert_true(options->recursive);
	assert_null(options->path);

	test_run_cmd(&info, "ls", "/[0]/[0]");
	assert_false(options->recursive);
	assert_non_null(options->path);
	assert_string_equal("/[0]/[0]", options->path);
}

static void
value_dump_options_parsing(void **state)
{
	struct ddb_cmd_info		 info = {0};
	struct dump_value_options	*options = &info.dci_cmd_option.dci_dump_value;

	test_run_inval_cmd("dump_value"); /* no path to dump */
	test_run_inval_cmd("dump_value", "this/is/a/path"); /* no destination path to dump to */

	test_run_cmd(&info, "dump_value", "this/is/a/path", "/this/is/a/destination");
	assert_string_equal("this/is/a/path", options->path);
	assert_string_equal("/this/is/a/destination", options->dst);
}

static void
ilog_dump_parsing(void **state)
{
	struct ddb_cmd_info		 info = {0};
	struct dump_ilog_options	*options = &info.dci_cmd_option.dci_dump_ilog;

	test_run_inval_cmd("dump_ilog"); /* no path to dump */

	test_run_cmd(&info, "dump_ilog", "this/is/a/path");
	assert_string_equal("this/is/a/path", options->path);
}

static void
dtx_dump_parsing(void **state)
{
	struct ddb_cmd_info		 info = {0};
	struct dump_dtx_options		*options = &info.dci_cmd_option.dci_dump_dtx;

	test_run_inval_cmd("dump_dtx"); /* no path to dump */
	test_run_inval_cmd("dump_dtx", "path", "-a", "-t");

	test_run_cmd(&info, "dump_dtx", "path");
	assert_int_equal(DDB_CMD_DUMP_DTX, info.dci_cmd);
	assert_string_equal("path", options->path);
	assert_false(options->active);
	assert_false(options->committed);

	test_run_cmd(&info, "dump_dtx", "path", "-a", "-c");
	assert_string_equal("path", options->path);
	assert_true(options->active);
	assert_true(options->committed);

	test_run_cmd(&info, "dump_dtx", "path", "-ac");
	assert_string_equal("path", options->path);
	assert_true(options->active);
	assert_true(options->committed);
}

static void
rm_parsing(void **state)
{
	struct ddb_cmd_info		 info = {0};
	struct rm_options		*options = &info.dci_cmd_option.dci_rm;

	test_run_inval_cmd("rm"); /* no path to dump */

	test_run_cmd(&info, "rm", "path");
	assert_string_equal("path", options->path);
}

static void
load_parsing(void **state)
{
	struct ddb_cmd_info		 info = {0};
	struct load_options		*options = &info.dci_cmd_option.dci_load;

	test_run_inval_cmd("load"); /* no file path to load or destination to load it to */
	test_run_inval_cmd("load", "only_one_path"); /* no destination to load it to */

	test_run_cmd(&info, "load", "src", "dst", "1");
	assert_string_equal("src", options->src);
	assert_string_equal("dst", options->dst);
	assert_string_equal("1", options->epoch);
}

#define TEST(x) { #x, x, NULL, NULL }
static const struct CMUnitTest tests[] = {
	TEST(ls_options_parsing),
	TEST(value_dump_options_parsing),
	TEST(ilog_dump_parsing),
	TEST(dtx_dump_parsing),
	TEST(rm_parsing),
	TEST(load_parsing),
};

/*
 * -----------------------------------------------
 * Execute
 * -----------------------------------------------
 */
int
ddb_cmd_options_tests_run()
{
	return cmocka_run_group_tests_name("DDB commands option parsing tests", tests,
					   NULL, NULL);
}
