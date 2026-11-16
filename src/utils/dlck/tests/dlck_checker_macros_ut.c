/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include <daos/debug.h>
#include <daos/checker.h>

#include "dlck_checker_ut_mock.h"

#define INITIAL_WARNINGS 7

static struct checker Checker;

static int
checker_vprintf(struct checker *ck, const char *fmt, va_list args)
{
	assert_ptr_equal(ck, &Checker);
	mock_vfprintf_check_output = true;
	/* Fake stream is safe: vfprintf is link-wrapped to __wrap_vfprintf. */
	return vfprintf(Mock_file_stream, fmt, args);
}

static void
checker_indent_set(struct checker *ck)
{
	uint8_t ck_level = ck->ck_level;

	assert_true(ck_level <= CHECKER_INDENT_MAX);
	function_called(); /* necessary to assure the call order */
	check_expected_ptr(ck);
	check_expected(ck_level);
}

static int
setup(void **state)
{
	Checker = (struct checker){
	    .ck_level               = 0,
	    .ck_prefix              = "-> ",
	    .ck_indent_set          = checker_indent_set,
	    .ck_vprintf             = checker_vprintf,
	    .ck_status.warnings_num = INITIAL_WARNINGS,
	};
	mock_vfprintf_check_output = false;
	*state                     = &Checker;
	return 0;
}

static int
teardown(void **state)
{
	struct checker *ck = *state;

	assert_int_equal(ck->ck_status.warnings_num, INITIAL_WARNINGS);
	assert_string_equal(ck->ck_prefix, "-> ");
	assert_int_equal(ck->ck_level, 0);
	return 0;
}

#define EXPECT_VFPRINTF(FMT, OUTPUT)                                                               \
	do {                                                                                       \
		expect_value(__wrap_vfprintf, stream, Mock_file_stream);                           \
		expect_string(__wrap_vfprintf, fmt, FMT);                                          \
		expect_string(__wrap_vfprintf, output, OUTPUT);                                    \
		will_return(__wrap_vfprintf, strlen(OUTPUT));                                      \
	} while (0)

/* CK_PRINTF: a format without arguments is printed after ck_prefix. */
static void
test_CK_PRINTF_without_args(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("%sready", "-> ready");
	CK_PRINTF(ck, "ready");
}

/* CK_PRINTF: format arguments are forwarded after ck_prefix. */
static void
test_CK_PRINTF(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("%sworker %d: %s", "-> worker 42: ready");
	CK_PRINTF(ck, "worker %d: %s", 42, "ready");
}

/* CK_PRINTF_WO_PREFIX: a format without arguments is printed without ck_prefix. */
static void
test_CK_PRINTF_WO_PREFIX_without_args(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("ready", "ready");
	CK_PRINTF_WO_PREFIX(ck, "ready");
}

/* CK_PRINTF_WO_PREFIX: format arguments are forwarded without ck_prefix. */
static void
test_CK_PRINTF_WO_PREFIX(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("worker %d: %s", "worker 42: ready");
	CK_PRINTF_WO_PREFIX(ck, "worker %d: %s", 42, "ready");
}

/* CK_APPENDL_OK: appends "ok." and a newline without ck_prefix. */
static void
test_CK_APPENDL_OK(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("ok.\n", "ok.\n");
	CK_APPENDL_OK(ck);
}

/* CK_APPENDL_RC: DER_SUCCESS appends "ok." and a newline. */
static void
test_CK_APPENDL_RC_success(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("ok.\n", "ok.\n");
	CK_APPENDL_RC(ck, DER_SUCCESS);
}

/* CK_APPENDL_RC: a negative DER code appends its name, value, and description. */
static void
test_CK_APPENDL_RC_error(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("error: " DF_RC "\n", "error: DER_IO(-2001): 'I / O error'\n");
	CK_APPENDL_RC(ck, -DER_IO);
}

/* CK_APPENDL_RC: a positive rc is reported as an error, not as success. */
static void
test_CK_APPENDL_RC_positive_error(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("error: " DF_RC "\n",
			"error: DER_UNKNOWN(1001): 'Unknown error code 1001'\n");
	CK_APPENDL_RC(ck, DER_NO_PERM);
}

/* CK_APPENDFL_ERR: appends a formatted message with the error infix and a newline. */
static void
test_CK_APPENDFL_ERR(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("error: worker %d: %s\n", "error: worker 42: failed\n");
	CK_APPENDFL_ERR(ck, "worker %d: %s", 42, "failed");
}

/* CK_APPENDFL_WARN: each formatted warning is printed and increments the warnings counter. */
static void
test_CK_APPENDFL_WARN(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("warning: worker %d: %s\n", "warning: worker 42: retry\n");
	CK_APPENDFL_WARN(ck, "worker %d: %s", 42, "retry");
	assert_int_equal(ck->ck_status.warnings_num, INITIAL_WARNINGS + 1);

	EXPECT_VFPRINTF("warning: worker %d: %s\n", "warning: worker 43: skipped\n");
	CK_APPENDFL_WARN(ck, "worker %d: %s", 43, "skipped");
	assert_int_equal(ck->ck_status.warnings_num, INITIAL_WARNINGS + 2);

	/* Reset the counter before teardown. */
	ck->ck_status.warnings_num = INITIAL_WARNINGS;
}

/* CK_APPENDFL_WARN: each warning without arguments increments the warnings counter. */
static void
test_CK_APPENDFL_WARN_without_args(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("warning: retry\n", "warning: retry\n");
	CK_APPENDFL_WARN(ck, "retry");
	assert_int_equal(ck->ck_status.warnings_num, INITIAL_WARNINGS + 1);

	EXPECT_VFPRINTF("warning: skipped\n", "warning: skipped\n");
	CK_APPENDFL_WARN(ck, "skipped");
	assert_int_equal(ck->ck_status.warnings_num, INITIAL_WARNINGS + 2);

	/* Reset the counter before teardown. */
	ck->ck_status.warnings_num = INITIAL_WARNINGS;
}

/* CK_APPENDL: appends a message and a newline without ck_prefix. */
static void
test_CK_APPENDL(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("ready\n", "ready\n");
	CK_APPENDL(ck, "ready");
}

/* CK_PRINTFL_RC: DER_SUCCESS prints ck_prefix, a format without arguments, then ": ok.". */
static void
test_CK_PRINTFL_RC_success_without_args(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("%scheck: ok.\n", "-> check: ok.\n");
	CK_PRINTFL_RC(ck, DER_SUCCESS, "check");
}

/* CK_PRINTFL_RC: an error prints ck_prefix, error infix, format without arguments, then rc. */
static void
test_CK_PRINTFL_RC_error_without_args(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("%serror: check: " DF_RC "\n",
			"-> error: check: DER_IO(-2001): 'I / O error'\n");
	CK_PRINTFL_RC(ck, -DER_IO, "check");
}

/* CK_PRINTFL_RC: DER_SUCCESS prints ck_prefix, the formatted message, then ": ok.". */
static void
test_CK_PRINTFL_RC_success(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("%sworker %d: %s: ok.\n", "-> worker 42: check: ok.\n");
	CK_PRINTFL_RC(ck, DER_SUCCESS, "worker %d: %s", 42, "check");
}

/* CK_PRINTFL_RC: an error prints ck_prefix, error infix, formatted message, then rc. */
static void
test_CK_PRINTFL_RC_error(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("%serror: worker %d: %s: " DF_RC "\n",
			"-> error: worker 42: check: DER_IO(-2001): 'I / O error'\n");
	CK_PRINTFL_RC(ck, -DER_IO, "worker %d: %s", 42, "check");
}

/* Output macros print nothing and skip format arguments when the checker is NULL. */
static void
test_CK_macros_without_checker(void **state)
{
	struct checker *ck          = NULL;
	int             evaluations = 0;

	(void)state;
	CK_PRINTF(ck, "ready");
	CK_PRINTF(ck, "%d", ++evaluations);
	CK_PRINTF_WO_PREFIX(ck, "ready");
	CK_PRINTF_WO_PREFIX(ck, "%d", ++evaluations);
	CK_APPENDL_OK(ck);
	CK_APPENDL_RC(ck, DER_SUCCESS);
	CK_APPENDL_RC(ck, -DER_IO);
	CK_APPENDFL_ERR(ck, "%d", ++evaluations);
	CK_APPENDFL_WARN(ck, "%d", ++evaluations);
	CK_APPENDFL_WARN(ck, "retry");
	CK_APPENDL(ck, "ready");
	CK_PRINTFL_RC(ck, DER_SUCCESS, "check");
	CK_PRINTFL_RC(ck, -DER_IO, "check");
	CK_PRINTFL_RC(ck, DER_SUCCESS, "%d", ++evaluations);
	CK_PRINTFL_RC(ck, -DER_IO, "%d", ++evaluations);
	assert_int_equal(evaluations, 0);
}

/* CK_ASSERT: with a checker, ck_prefix precedes the true condition reported as "yes". */
static void
test_CK_ASSERT_true_with_checker(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("%svalid: %s\n", "-> valid: yes\n");
	CK_ASSERT(ck, "valid: ", true);
}

/* CK_ASSERT: with a checker, ck_prefix precedes the false condition reported as "no". */
static void
test_CK_ASSERT_false_with_checker(void **state)
{
	struct checker *ck = *state;

	EXPECT_VFPRINTF("%svalid: %s\n", "-> valid: no\n");
	CK_ASSERT(ck, "valid: ", false);
}

/* CK_ASSERT: without a checker, a true condition passes silently. */
static void
test_CK_ASSERT_true_without_checker(void **state)
{
	struct checker *ck = NULL;

	(void)state;
	CK_ASSERT(ck, "valid: ", true);
}

/* CK_ASSERT: without a checker, a false condition syncs the log and asserts. */
static void
test_CK_ASSERT_false_without_checker(void **state)
{
	struct checker *ck = NULL;

	(void)state;
	expect_function_call(__wrap_d_log_sync);
	expect_assert_failure(CK_ASSERT(ck, "valid: ", false));
}

#define EXPECT_CHECKER_INDENT_SET(CK, CK_LEVEL)                                                    \
	do {                                                                                       \
		expect_function_call(checker_indent_set);                                          \
		expect_value(checker_indent_set, ck, CK);                                          \
		expect_value(checker_indent_set, ck_level, CK_LEVEL);                              \
	} while (0)

/* CK_INDENT: raises the level before the body and restores it afterwards. */
static void
test_CK_INDENT(void **state)
{
	struct checker *ck = *state;

	EXPECT_CHECKER_INDENT_SET(ck, 1);
	expect_function_call(test_CK_INDENT);
	EXPECT_CHECKER_INDENT_SET(ck, 0);
	CK_INDENT(ck, {
		function_called();
		assert_int_equal(ck->ck_level, 1);
	});
	assert_int_equal(ck->ck_level, 0);
}

/* CK_INDENT: nested bodies run at increasing levels, restored in reverse order. */
static void
test_CK_INDENT_nested(void **state)
{
	struct checker *ck = *state;

	EXPECT_CHECKER_INDENT_SET(ck, 1);
	expect_function_call(test_CK_INDENT_nested);
	EXPECT_CHECKER_INDENT_SET(ck, 2);
	expect_function_call(test_CK_INDENT_nested);
	EXPECT_CHECKER_INDENT_SET(ck, 1);
	expect_function_call(test_CK_INDENT_nested);
	EXPECT_CHECKER_INDENT_SET(ck, 0);
	CK_INDENT(ck, {
		function_called();
		assert_int_equal(ck->ck_level, 1);
		CK_INDENT(ck, {
			function_called();
			assert_int_equal(ck->ck_level, 2);
		});
		function_called();
		assert_int_equal(ck->ck_level, 1);
	});
	assert_int_equal(ck->ck_level, 0);
}

/* CK_INDENT: may raise the level from CHECKER_INDENT_MAX - 1 to the maximum. */
static void
test_CK_INDENT_reaches_maximum(void **state)
{
	struct checker *ck = *state;

	ck->ck_level = CHECKER_INDENT_MAX - 1;
	EXPECT_CHECKER_INDENT_SET(ck, CHECKER_INDENT_MAX);
	expect_function_call(test_CK_INDENT_reaches_maximum);
	EXPECT_CHECKER_INDENT_SET(ck, CHECKER_INDENT_MAX - 1);
	CK_INDENT(ck, {
		function_called();
		assert_int_equal(ck->ck_level, CHECKER_INDENT_MAX);
	});
	assert_int_equal(ck->ck_level, CHECKER_INDENT_MAX - 1);

	/* Reset the level before teardown. */
	ck->ck_level = 0;
}

/* CK_INDENT: without a checker, the body runs once with no indent callbacks. */
static void
test_CK_INDENT_without_checker(void **state)
{
	struct checker *ck         = NULL;
	int             body_calls = 0;

	(void)state;
	CK_INDENT(ck, body_calls++);
	assert_int_equal(body_calls, 1);
}

/* CK_INDENT: at CHECKER_INDENT_MAX, asserts before the body or level changes. */
static void
test_CK_INDENT_exceeds_maximum(void **state)
{
	struct checker *ck         = *state;
	volatile int    body_calls = 0; /* survives longjmp from expect_assert_failure() */

	ck->ck_level = CHECKER_INDENT_MAX;
	expect_assert_failure(CK_INDENT(ck, body_calls++));
	assert_int_equal(body_calls, 0);
	assert_int_equal(ck->ck_level, CHECKER_INDENT_MAX);

	/* Reset the level before teardown. */
	ck->ck_level = 0;
}

/* checker_print_indent_dec: at level 0, asserts without changing the level. */
static void
test_checker_print_indent_dec_below_minimum(void **state)
{
	struct checker *ck = *state;

	ck->ck_level = 0;
	expect_assert_failure(checker_print_indent_dec(ck));
	assert_int_equal(ck->ck_level, 0);
}

#define TEST(ID, FUNC)            {"DLCK_CHECKER_MACRO_" #ID ": " #FUNC, FUNC, setup, teardown, NULL}
#define TEST_NO_FIXTURE(ID, FUNC) {"DLCK_CHECKER_MACRO_" #ID ": " #FUNC, FUNC, NULL, NULL, NULL}

static const struct CMUnitTest dlck_checker_macros_tests[] = {
    TEST(101, test_CK_PRINTF_without_args),
    TEST(102, test_CK_PRINTF),
    TEST(103, test_CK_PRINTF_WO_PREFIX_without_args),
    TEST(104, test_CK_PRINTF_WO_PREFIX),
    TEST(105, test_CK_APPENDL_OK),
    TEST(106, test_CK_APPENDL_RC_success),
    TEST(107, test_CK_APPENDL_RC_error),
    TEST(108, test_CK_APPENDL_RC_positive_error),
    TEST(109, test_CK_APPENDFL_ERR),
    TEST(110, test_CK_APPENDFL_WARN),
    TEST(111, test_CK_APPENDFL_WARN_without_args),
    TEST(112, test_CK_APPENDL),
    TEST(113, test_CK_PRINTFL_RC_success_without_args),
    TEST(114, test_CK_PRINTFL_RC_error_without_args),
    TEST(115, test_CK_PRINTFL_RC_success),
    TEST(116, test_CK_PRINTFL_RC_error),
    TEST_NO_FIXTURE(117, test_CK_macros_without_checker),
    TEST(118, test_CK_ASSERT_true_with_checker),
    TEST(119, test_CK_ASSERT_false_with_checker),
    TEST_NO_FIXTURE(120, test_CK_ASSERT_true_without_checker),
    TEST_NO_FIXTURE(121, test_CK_ASSERT_false_without_checker),
    TEST(122, test_CK_INDENT),
    TEST(123, test_CK_INDENT_nested),
    TEST(124, test_CK_INDENT_reaches_maximum),
    TEST_NO_FIXTURE(125, test_CK_INDENT_without_checker),
    TEST(126, test_CK_INDENT_exceeds_maximum),
    TEST(127, test_checker_print_indent_dec_below_minimum),
};

int
main(int argc, char **argv)
{
	d_register_alt_assert(mock_assert);
#if CMOCKA_FILTER_SUPPORTED
	if (argc > 1) {
		char filter[256];
		int  rc = snprintf(filter, sizeof(filter), "*%s*", argv[1]);

		if (rc > 0 && (size_t)rc < sizeof(filter)) {
			cmocka_set_test_filter(filter);
		} else {
			fprintf(stderr, "ERROR: Test filter '%s' cannot be applied.\n", argv[1]);
			return 1;
		}
	}
#else
	(void)argc;
	(void)argv;
#endif
	return cmocka_run_group_tests_name("dlck_checker_macros_ut", dlck_checker_macros_tests,
					   NULL, NULL);
}
