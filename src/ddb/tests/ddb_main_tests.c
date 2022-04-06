/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <daos/tests_lib.h>
#include <ddb_main.h>
#include <ddb_cmd_options.h>
#include "ddb_cmocka.h"
#include "ddb_test_driver.h"

/*
 * Test that the command line interface interacts with a 'user' correctly. Will verify that the
 * command line options and arguments are handled correctly and the interactive mode.
 */


#define assert_main(...) \
	assert_success(__test_run_main((char *[]){"prog_name", __VA_ARGS__, NULL}))
#define assert_invalid_main(...) \
	assert_rc_equal(-DER_INVAL, __test_run_main((char *[]){"prog_name", __VA_ARGS__, NULL}))


static int
fake_print_message(const char *fmt, ...)
{
	return 0;
}

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

	strncpy(buf, input, min(strlen(input) + 1, buf_len));
	fake_get_input_called++;

	return input;
}

static int
__test_run_main(char *argv[])
{
	uint32_t		argc = 0;
	struct ddb_io_ft ft = {
		.ddb_print_message = fake_print_message,
		.ddb_print_error = fake_print_message,
		.ddb_get_input = fake_get_input,
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

/* Not tested yet:
 *   - -R is passed
 *   - -f is passed
 *   - -w is passed
 *   - pool shard file is passed as argument
 */

#define TEST(x) { #x, x, NULL, NULL }

static const struct CMUnitTest tests[] = {
	TEST(interactive_mode_tests),
};

int
ddb_main_tests()
{
	return cmocka_run_group_tests_name("DDB CLI tests", tests, NULL, NULL);
}
