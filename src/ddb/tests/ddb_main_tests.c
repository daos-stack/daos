/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <daos/tests_lib.h>
#include <ddb_main.h>
#include <ddb_cmd_options.h>
#include <daos_srv/vos.h>
#include "ddb_cmocka.h"
#include "ddb_test_driver.h"

/*
 * Test that the command line interface interacts with a 'user' correctly. Will verify that the
 * command line options and arguments are handled correctly and the interactive mode.
 */

uint32_t fake_get_input_called;
int fake_get_input_inputs_count;
int fake_get_input_inputs_idx;
char fake_get_input_inputs[64][64];

#define set_fake_inputs(...) __set_fake_inputs((char *[]){__VA_ARGS__, NULL})
static inline void
__set_fake_inputs(char *inputs[])
{
	int i = 0;

	while (inputs[i] != NULL) {
		/* input from user will always have a new line at the end */
		sprintf(fake_get_input_inputs[i], "%s\n", inputs[i]);
		i++;
	}
	fake_get_input_inputs_count = i;
	fake_get_input_inputs_idx = 0;
}

static char *
fake_get_input(char *buf, uint32_t buf_len)
{
	char *input;

	assert_true(fake_get_input_inputs_idx < ARRAY_SIZE(fake_get_input_inputs));
	input = fake_get_input_inputs[fake_get_input_inputs_idx++];
	assert_true(strlen(input) < buf_len);

	strcpy(buf, input);
	fake_get_input_called++;

	return input;
}

int dvt_fake_get_lines_result;
int dvt_fake_get_lines_called;
int
dvt_fake_get_lines(const char *path, ddb_io_line_cb line_cb, void *cb_args)
{
	int i;
	int rc;

	dvt_fake_get_lines_called++;

	for (i = 0; i < fake_get_input_inputs_count; i++) {
		rc = line_cb(cb_args, fake_get_input_inputs[i], strlen(fake_get_input_inputs[i]));
		if (rc != 0)
			return rc;
	}


	return dvt_fake_get_lines_result;
}

#define assert_main(...) \
	assert_success(__test_run_main((char *[]){"prog_name", __VA_ARGS__, NULL}))
#define assert_invalid_main(...) \
	assert_rc_equal(-DER_INVAL, __test_run_main((char *[]){"prog_name", __VA_ARGS__, NULL}))

static int
__test_run_main(char *argv[])
{
	uint32_t		argc = 0;
	struct ddb_io_ft ft = {
		.ddb_print_message = dvt_fake_print,
		.ddb_print_error = dvt_fake_print,
		.ddb_get_input = fake_get_input,
		.ddb_read_file = dvt_fake_read_file,
		.ddb_get_file_exists = dvt_fake_get_file_exists,
		.ddb_get_file_size = dvt_fake_get_file_size,
		.ddb_get_lines = dvt_fake_get_lines
	};

	assert_non_null(argv);
	if (g_verbose)
		printf("Command: ");
	while (argv[argc] != NULL && strcmp(argv[argc], "") != 0) {
		if (g_verbose)
			printf("%s ", argv[argc]);
		argc++;
	}
	if (g_verbose)
		printf("\n");

	return ddb_main(&ft, argc, argv);
}

#define assert_main_interactive_with_input(...) \
	__assert_main_interactive_with_input((char *[]) {__VA_ARGS__, NULL})
static void
__assert_main_interactive_with_input(char *inputs[])
{
	__set_fake_inputs(inputs);
	assert_main("");
}

/*
 * -----------------------------------------------
 * Test Functions
 * -----------------------------------------------
 */

static void
interactive_mode_tests(void **state)
{
	assert_main_interactive_with_input("quit");
	assert_int_equal(1, fake_get_input_called);

	fake_get_input_called = 0;
	assert_main_interactive_with_input("ls", "ls", "quit");
	assert_int_equal(3, fake_get_input_called);

	assert_invalid_main("path", "invalid_extra_arg");
}

static void
run_inline_command_with_opt_r_tests(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;

	assert_main(tctx->dvt_pmem_file, "-R", "ls [0] -r");
}

static void
only_modify_with_option_w_tests(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;

#define assert_requires_write_mode(cmd) \
do { \
	assert_invalid_main(tctx->dvt_pmem_file, "-R", cmd); \
	assert_main(tctx->dvt_pmem_file, "-w", "-R", cmd); \
} while (0)

	dvt_fake_print_reset();
	assert_requires_write_mode("rm [0]");

	/* Set up test for the load command */
	dvt_fake_get_file_exists_result = true;
	dvt_fake_get_file_size_result = 10;
	dvt_fake_read_file_result = dvt_fake_get_file_size_result;
	assert_requires_write_mode("load src [0]/[0]/[0]/[1] 1");

	assert_requires_write_mode("clear_cmt_dtx [0]");
}

static void
run_many_commands_with_option_f_tests(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;

	/* file doesn't exist */
	dvt_fake_get_file_exists_result = false;
	assert_invalid_main(tctx->dvt_pmem_file, "-f", "file_path");

	/* Empty file is still success */
	dvt_fake_get_file_exists_result = true;
	assert_main(tctx->dvt_pmem_file, "-f", "file_path");

	/* one command */
	dvt_fake_get_lines_called = 0;
	assert_main(tctx->dvt_pmem_file, "-f", "file_path");
	assert_int_equal(1, dvt_fake_get_lines_called);

	/* handles invalid commands */
	dvt_fake_get_file_exists_result = true;
	set_fake_inputs("bad_command");
	assert_invalid_main(tctx->dvt_pmem_file, "-f", "file_path");

	/* multiple lines/commands */
	dvt_fake_get_file_exists_result = true;
	dvt_fake_get_lines_called = 0;
	set_fake_inputs("ls", "dump_superblock", "ls [0]");
	assert_main(tctx->dvt_pmem_file, "-f", "file_path");
	assert_int_equal(1, dvt_fake_get_lines_called);

	/* empty lines are ignored */
	dvt_fake_get_file_exists_result = true;
	dvt_fake_get_lines_called = 0;
	set_fake_inputs("ls", "", "dump_superblock");
	assert_main(tctx->dvt_pmem_file, "-f", "file_path");
	assert_int_equal(1, dvt_fake_get_lines_called);

	/* Lines with just whitespace are ignored */
	dvt_fake_get_file_exists_result = true;
	dvt_fake_get_lines_called = 0;
	set_fake_inputs("ls", "\t   \t \t\n", "dump_superblock", "\n");
	assert_main(tctx->dvt_pmem_file, "-f", "file_path");
	assert_int_equal(1, dvt_fake_get_lines_called);

	/* commands that modify tree must have '-w' also */
	dvt_fake_get_file_exists_result = true;
	set_fake_inputs("ls", "rm [0]");
	assert_invalid_main(tctx->dvt_pmem_file, "-f", "file_path");
	assert_main(tctx->dvt_pmem_file, "-w", "-f", "file_path");
}

static void
option_f_and_option_R_is_invalid_tests(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;

	/* Make sure that the fakes are setup to work so they are not invalid */
	set_fake_inputs("ls");
	dvt_fake_get_file_exists_result = true;

	assert_invalid_main(tctx->dvt_pmem_file, "-R", "ls", "-f", "file_path");
}

static int
ddb_main_suit_setup(void **state)
{
	struct dt_vos_pool_ctx *tctx;

	assert_success(ddb_test_setup_vos(state));

	/* test setup creates the pool, but doesn't open it ... leave it open for these tests */
	tctx = *state;
	assert_success(vos_pool_open(tctx->dvt_pmem_file, tctx->dvt_pool_uuid, 0, &tctx->dvt_poh));

	return 0;
}

static int
ddb_main_suit_teardown(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;

	if (tctx == NULL)
		fail_msg("Test not setup correctly");
	assert_success(vos_pool_close(tctx->dvt_poh));
	ddb_teardown_vos(state);

	return 0;
}

#define TEST(x) { #x, x, NULL, NULL }
int
ddb_main_tests()
{
	static const struct CMUnitTest tests[] = {
		TEST(interactive_mode_tests),
		TEST(run_inline_command_with_opt_r_tests),
		TEST(only_modify_with_option_w_tests),
		TEST(run_many_commands_with_option_f_tests),
		TEST(option_f_and_option_R_is_invalid_tests),
	};

	return cmocka_run_group_tests_name("DDB CLI tests", tests, ddb_main_suit_setup,
					   ddb_main_suit_teardown);
}
