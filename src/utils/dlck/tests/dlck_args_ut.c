/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos/common.h>

#include "../dlck_args.h"

/** globals */

#define APP_NAME_MOCK  "app_name"
#define PARSER_FAILURE EINVAL

extern struct argp  argp_common;
extern struct argp  argp_file;
extern struct argp  argp_engine;

struct dlck_control Ctrl;

argp_parser_t       Argp_engine_parser_real;

/** wrappers and mocks */

void
__wrap_argp_failure(const struct argp_state *__restrict __state, int __status, int __errnum,
		    const char *__restrict __fmt, ...)
{
	check_expected(__state);
	assert_int_equal(__status, PARSER_FAILURE);
	assert_int_equal(__errnum, PARSER_FAILURE);
}

static error_t
argp_common_parser_mock(int key, char *arg, struct argp_state *state)
{
	check_expected(key);
	assert_non_null(state);
	assert_ptr_equal(state->input, &Ctrl.common);

	return 0;
}

static error_t
argp_file_parser_mock(int key, char *arg, struct argp_state *state)
{
	check_expected(key);
	assert_non_null(state);
	assert_ptr_equal(state->input, &Ctrl.files);

	return 0;
}

static error_t
argp_engine_parser_mock(int key, char *arg, struct argp_state *state)
{
	check_expected_ptr(key);
	assert_non_null(state);
	assert_ptr_equal(state->input, &Ctrl.engine);

	return 0;
}

/** setups & teardowns */

static int
setup_engine_args_default(void **state_ptr)
{
	static struct dlck_args_engine args  = {0};
	static struct argp_state       state = {0};
	error_t                        ret;

	/** bind the input */
	state.input = &args;

	/** set defaults */
	ret = Argp_engine_parser_real(ARGP_KEY_INIT, NULL, &state);
	assert_int_equal(ret, 0);

	*state_ptr = &state;

	return 0;
}

/** tests */

/**
 * Test if all the children parsers are connected properly and if each of them receives all of
 * the expected special key values.
 */
static void
test_parser_children_connection(void **unused)
{
	/** special keys as they are provided for each of the parsers in order */
	int   keys[] = {ARGP_KEY_INIT, ARGP_KEY_NO_ARGS, ARGP_KEY_END, ARGP_KEY_SUCCESS,
			ARGP_KEY_FINI};

	/** empty argument list */
	int   argc   = 1;
	char *argv[] = {APP_NAME_MOCK};

	for (int i = 0; i < ARRAY_SIZE(keys); ++i) {
		expect_value(argp_common_parser_mock, key, keys[i]);
		expect_value(argp_file_parser_mock, key, keys[i]);
		expect_value(argp_engine_parser_mock, key, keys[i]);
	}

	dlck_args_parse(argc, argv, &Ctrl);
}

static void
test_engine_parser_END_no_storage_path_fail(void **state_ptr)
{
	struct argp_state *state = *state_ptr;
	error_t            ret;

	expect_value(__wrap_argp_failure, __state, state);

	ret = Argp_engine_parser_real(ARGP_KEY_END, NULL, state);
	assert_int_equal(ret, PARSER_FAILURE);
}

static const struct CMUnitTest dlck_args_tests[] = {
    {"DLCK_ARGS100: parser - children connection", test_parser_children_connection, NULL, NULL},
    {"DLCK_ARGS200: engine parser + ARGP_KEY_END + no storage path",
     test_engine_parser_END_no_storage_path_fail, setup_engine_args_default, NULL},
};

int
main(int argc, char **argv)
{
	/** collect function pointers to real parsers */
	Argp_engine_parser_real = argp_engine.parser;

	/** overwrite real parsers with mocks */
	argp_common.parser = argp_common_parser_mock;
	argp_file.parser   = argp_file_parser_mock;
	argp_engine.parser = argp_engine_parser_mock;

	return cmocka_run_group_tests_name("dlck_args_ut", dlck_args_tests, NULL, NULL);
}
