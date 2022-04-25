/**
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <krb5.h>

#include "unit_test.h"
#include "cmocka_tests.h"

struct mocked_fn {
    void **fn_ptr_ptr;
    void *old_value;
    enum MOCK_TYPE mock_type;
};

static struct mocked_fn mocked_fns[1024];
static int mocked_fn_cnt;
bool verbose_unit_test_output = FALSE;

/*
 * Mimick requires some functions setup that are otherwise optional, but doesn't
 * see to provide an include file with them in so define them here and
 * initialize them in main below.
 */
void *(*mmk_malloc_)(size_t);
void *(*mmk_realloc_)(void *, size_t);
void (*mmk_free_)(void *);
void (*mmk_abort_)(void);
void (*mmk_vfprintf_)(FILE *, const char *, va_list);



void add_mocked_function(void **fn_ptr_ptr,
			 void *mock_ptr,
			 enum MOCK_TYPE mock_type)
{
	assert(mocked_fn_cnt < ARRAY_SIZE(mocked_fns));

	mocked_fns[mocked_fn_cnt].mock_type = mock_type;
	mocked_fns[mocked_fn_cnt].fn_ptr_ptr = fn_ptr_ptr;

	switch (mock_type) {
		case MT_SIMPLE: {
			mocked_fns[mocked_fn_cnt].old_value = *fn_ptr_ptr;
			*fn_ptr_ptr = mock_ptr;
			break;
		}

		case MT_MIMICK: {
			struct mmk_mock_options mmk_mock_opts = {
				.sentinel_ = 0,
				.noabort = 0
			};

			mocked_fns[mocked_fn_cnt].old_value =
				mmk_mock_create_internal((char *)fn_ptr_ptr,
							 (mmk_fn) mock_ptr,
							 mmk_mock_opts);
			break;
		}

		default: {
			assert_true_msg(0, "Unknown MOCKTYPE %s", mock_type);
		}
	}

	mocked_fn_cnt++;
}

void clear_mocked_functions(void)
{
	while (mocked_fn_cnt > 0) {
		mocked_fn_cnt--;

		switch (mocked_fns[mocked_fn_cnt].mock_type) {
			case MT_SIMPLE: {
				*mocked_fns[mocked_fn_cnt].fn_ptr_ptr =
					mocked_fns[mocked_fn_cnt].old_value;
				break;
			}

			case MT_MIMICK: {
				mmk_reset(mocked_fns[mocked_fn_cnt].old_value);
				break;
			}

			default: {
				assert_true_msg(0,
					"Unknown MOCKTYPE %s",
					mocked_fns[mocked_fn_cnt].mock_type);
			}
		}
	}
}

static char assert_message_buffer[256];

static char *param_replace(int param_number, unsigned int value)
{
	char *after_str;
	char search_str[4] = "{X}";
	char *location;

	search_str[1] = 0x30 + param_number;

	location = strstr(assert_message_buffer, search_str);

	if (location) {
		after_str = malloc(sizeof(assert_message_buffer));
		snprintf(after_str, sizeof(assert_message_buffer),
			 "%d%s", value, location + sizeof(search_str) - 1);
		strncpy(location, after_str,
			sizeof(assert_message_buffer)
			- (location - assert_message_buffer));
		free(after_str);
	}

	return assert_message_buffer;
}

char *assert_message(char *file, int line, int a, int b, char *message,  ...)
{
	va_list args;

	va_start(args, message);

	vsnprintf(assert_message_buffer, sizeof(assert_message_buffer),
		  message, args);

	param_replace(1, a);
	param_replace(2, b);

	return assert_message_buffer;
}

int global_setup(void **state)
{
	int (**global_setup_function)(void **state);

	for (global_setup_function = global_setup_functions;
	     *global_setup_function != NULL;
	     global_setup_function++) {
		(*global_setup_function)(state);
	}

	return 0;
}

int global_teardown(void **state)
{
	int (**global_teardown_function)(void **state);

	for (global_teardown_function = global_teardown_functions;
	     *global_teardown_function != NULL;
	     global_teardown_function++) {
		(*global_teardown_function)(state);
	}

	return 0;
}

static void
print_usage()
{
	print_message("Use one of these opt(s) for specific test\n");
	print_message("unit_test -s partial-testname-match\n");
	print_message("unit_test -v\n");
}

static void
cull_tests(struct _cmocka_tests *cmocka_tests, char *partial_test_name)
{
	int test_number;

	for (test_number = 0;
	     test_number < cmocka_tests->number_of_tests;
	     test_number++) {
		if (strstr(cmocka_tests->tests[test_number].name,
			   partial_test_name) == NULL) {
			memcpy((void *) (cmocka_tests->tests + test_number),
			       (void *) (cmocka_tests->tests + test_number + 1),
			       (int) (sizeof(cmocka_tests->tests[0]) *
				      (cmocka_tests->number_of_tests -
				       test_number)));
			cmocka_tests->number_of_tests--;
			test_number--;
		}
	}
}

int main(int argc, char *argv[])
{
	int opt;

	/*
	 * Mimick requires some functions setup that are otherwise optional
	 */
	mmk_malloc_ = malloc;
	mmk_realloc_ = realloc;
	mmk_free_ = free;
	mmk_abort_ = abort;
	mmk_vfprintf_ = (void (*)(FILE *, const char *, va_list)) vfprintf;

	struct _cmocka_tests *cmocka_tests = generated_cmocka_tests();

	while ((opt = getopt(argc, argv, "s:hv")) != -1) {
		switch (opt) {
		case 's':
			cull_tests(cmocka_tests, optarg);
			break;
		case 'h':
			print_usage();
			return 0;
		case 'v':
			verbose_unit_test_output = TRUE;
			break;
		default:
			print_error("Unknown option\n");
			print_usage();
			return 1;
		}
	}

	putenv("MOCKA_MESSAGE_OUTPUT='STDOUT'");

	_cmocka_run_group_tests(cmocka_tests->group_name,
				cmocka_tests->tests,
				cmocka_tests->number_of_tests,
				global_setup,
				global_teardown);

	return 0;
}
