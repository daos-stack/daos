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
	mock_vfprintf_check_output = 1;
	/* Fake stream is safe: vfprintf is link-wrapped to __wrap_vfprintf. */
	return vfprintf(Mock_file_stream, fmt, args);
}

static void
checker_indent_set(struct checker *ck)
{
	uint8_t ck_level = ck->ck_level;

	assert_true(ck_level <= CHECKER_INDENT_MAX);
	function_called();
	check_expected_ptr(ck);
	check_expected(ck_level);
}

static int
setup(void **state)
{
	Checker = (struct checker){
	    .ck_level        = 0,
	    .ck_prefix       = "-> ",
	    .ck_indent_set   = checker_indent_set,
	    .ck_vprintf      = checker_vprintf,
	    .ck_warnings_num = INITIAL_WARNINGS,
	};
	mock_vfprintf_check_output = 0;
	*state                     = &Checker;
	return 0;
}

static int
teardown(void **state)
{
	struct checker *ck = *state;

	assert_int_equal(ck->ck_warnings_num, INITIAL_WARNINGS);
	assert_string_equal(ck->ck_prefix, "-> ");
	assert_int_equal(ck->ck_level, 0);
	return 0;
}

#define vfprintf_expected_output(FMT, OUTPUT)                                                      \
	do {                                                                                       \
		expect_value(__wrap_vfprintf, stream, Mock_file_stream);                           \
		expect_string(__wrap_vfprintf, fmt, FMT);                                          \
		expect_string(__wrap_vfprintf, output, OUTPUT);                                    \
		will_return(__wrap_vfprintf, strlen(OUTPUT));                                      \
	} while (0)

static void
test_CK_PRINTF_without_args(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("%sready", "-> ready");
	CK_PRINTF(ck, "ready");
}

static void
test_CK_PRINTF(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("%sworker %d: %s", "-> worker 42: ready");
	CK_PRINTF(ck, "worker %d: %s", 42, "ready");
}

static void
test_CK_PRINTF_WO_PREFIX_without_args(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("ready", "ready");
	CK_PRINTF_WO_PREFIX(ck, "ready");
}

static void
test_CK_PRINTF_WO_PREFIX(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("worker %d: %s", "worker 42: ready");
	CK_PRINTF_WO_PREFIX(ck, "worker %d: %s", 42, "ready");
}

static void
test_CK_APPENDL_OK(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("ok.\n", "ok.\n");
	CK_APPENDL_OK(ck);
}

static void
test_CK_APPENDL_RC_success(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("ok.\n", "ok.\n");
	CK_APPENDL_RC(ck, DER_SUCCESS);
}

static void
test_CK_APPENDL_RC_error(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("error: " DF_RC "\n", "error: DER_IO(-2001): 'I / O error'\n");
	CK_APPENDL_RC(ck, -DER_IO);
}

static void
test_CK_APPENDL_RC_positive_error(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("error: " DF_RC "\n",
				 "error: DER_UNKNOWN(1001): 'Unknown error code 1001'\n");
	CK_APPENDL_RC(ck, DER_NO_PERM);
}

static void
test_CK_APPENDFL_ERR(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("error: worker %d: %s\n", "error: worker 42: failed\n");
	CK_APPENDFL_ERR(ck, "worker %d: %s", 42, "failed");
}

static void
test_CK_APPENDFL_WARN(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("warning: worker %d: %s\n", "warning: worker 42: retry\n");
	CK_APPENDFL_WARN(ck, "worker %d: %s", 42, "retry");
	assert_int_equal(ck->ck_warnings_num, INITIAL_WARNINGS + 1);

	vfprintf_expected_output("warning: worker %d: %s\n", "warning: worker 43: skipped\n");
	CK_APPENDFL_WARN(ck, "worker %d: %s", 43, "skipped");
	assert_int_equal(ck->ck_warnings_num, INITIAL_WARNINGS + 2);
	ck->ck_warnings_num = INITIAL_WARNINGS;
}

static void
test_CK_APPENDFL_WARN_without_args(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("warning: retry\n", "warning: retry\n");
	CK_APPENDFL_WARN(ck, "retry");
	assert_int_equal(ck->ck_warnings_num, INITIAL_WARNINGS + 1);

	vfprintf_expected_output("warning: skipped\n", "warning: skipped\n");
	CK_APPENDFL_WARN(ck, "skipped");
	assert_int_equal(ck->ck_warnings_num, INITIAL_WARNINGS + 2);
	ck->ck_warnings_num = INITIAL_WARNINGS;
}

static void
test_CK_APPENDL(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("ready\n", "ready\n");
	CK_APPENDL(ck, "ready");
}

static void
test_CK_PRINTFL_RC_success_without_args(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("%scheck: ok.\n", "-> check: ok.\n");
	CK_PRINTFL_RC(ck, DER_SUCCESS, "check");
}

static void
test_CK_PRINTFL_RC_error_without_args(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("%serror: check: " DF_RC "\n",
				 "-> error: check: DER_IO(-2001): 'I / O error'\n");
	CK_PRINTFL_RC(ck, -DER_IO, "check");
}

static void
test_CK_PRINTFL_RC_success(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("%sworker %d: %s: ok.\n", "-> worker 42: check: ok.\n");
	CK_PRINTFL_RC(ck, DER_SUCCESS, "worker %d: %s", 42, "check");
}

static void
test_CK_PRINTFL_RC_error(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("%serror: worker %d: %s: " DF_RC "\n",
				 "-> error: worker 42: check: DER_IO(-2001): 'I / O error'\n");
	CK_PRINTFL_RC(ck, -DER_IO, "worker %d: %s", 42, "check");
}

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

static void
test_CK_ASSERT_true_with_checker(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("%svalid: %s\n", "-> valid: yes\n");
	CK_ASSERT(ck, "valid: ", true);
}

static void
test_CK_ASSERT_false_with_checker(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("%svalid: %s\n", "-> valid: no\n");
	CK_ASSERT(ck, "valid: ", false);
}

static void
test_CK_ASSERT_true_without_checker(void **state)
{
	struct checker *ck = NULL;

	(void)state;
	CK_ASSERT(ck, "valid: ", true);
}

static void
test_CK_ASSERT_false_without_checker(void **state)
{
	struct checker *ck = NULL;

	(void)state;
	expect_function_call(__wrap_d_log_sync);
	expect_assert_failure(CK_ASSERT(ck, "valid: ", false));
}

#define expect_indent(CK, CK_LEVEL)                                                                \
	do {                                                                                       \
		expect_function_call(checker_indent_set);                                          \
		expect_value(checker_indent_set, ck, CK);                                          \
		expect_value(checker_indent_set, ck_level, CK_LEVEL);                              \
	} while (0)

static void
test_CK_INDENT(void **state)
{
	struct checker *ck = *state;

	expect_indent(ck, 1);
	expect_function_call(test_CK_INDENT);
	expect_indent(ck, 0);
	CK_INDENT(ck, {
		function_called();
		assert_int_equal(ck->ck_level, 1);
	});
	assert_int_equal(ck->ck_level, 0);
}

static void
test_CK_INDENT_nested(void **state)
{
	struct checker *ck = *state;

	expect_indent(ck, 1);
	expect_function_call(test_CK_INDENT_nested);
	expect_indent(ck, 2);
	expect_function_call(test_CK_INDENT_nested);
	expect_indent(ck, 1);
	expect_function_call(test_CK_INDENT_nested);
	expect_indent(ck, 0);
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

static void
test_CK_INDENT_reaches_maximum(void **state)
{
	struct checker *ck = *state;

	ck->ck_level = CHECKER_INDENT_MAX - 1;
	expect_indent(ck, CHECKER_INDENT_MAX);
	expect_function_call(test_CK_INDENT_reaches_maximum);
	expect_indent(ck, CHECKER_INDENT_MAX - 1);
	CK_INDENT(ck, {
		function_called();
		assert_int_equal(ck->ck_level, CHECKER_INDENT_MAX);
	});
	assert_int_equal(ck->ck_level, CHECKER_INDENT_MAX - 1);
	ck->ck_level = 0;
}

static void
test_CK_INDENT_without_checker(void **state)
{
	struct checker *ck         = NULL;
	int             body_calls = 0;

	(void)state;
	CK_INDENT(ck, body_calls++);
	assert_int_equal(body_calls, 1);
}

static void
test_CK_INDENT_exceeds_maximum(void **state)
{
	struct checker *ck         = *state;
	volatile int    body_calls = 0; /* survives longjmp from expect_assert_failure() */

	ck->ck_level = CHECKER_INDENT_MAX;
	expect_assert_failure(CK_INDENT(ck, body_calls++));
	assert_int_equal(body_calls, 0);
	assert_int_equal(ck->ck_level, CHECKER_INDENT_MAX);
	ck->ck_level = 0;
}

static void
test_checker_print_indent_dec_below_minimum(void **state)
{
	struct checker *ck = *state;

	ck->ck_level = 0;
	expect_assert_failure(checker_print_indent_dec(ck));
	assert_int_equal(ck->ck_level, 0);
}

#define DLCK_MACRO_TEST(ID, DESC, FUNC)                                                            \
	{"DLCK_CHECKER_MACRO_" #ID ": " DESC, FUNC, setup, teardown, NULL}
#define DLCK_MACRO_TEST_NO_FIXTURE(ID, DESC, FUNC)                                                 \
	{"DLCK_CHECKER_MACRO_" #ID ": " DESC, FUNC, NULL, NULL, NULL}

static const struct CMUnitTest dlck_checker_macros_tests[] = {
    DLCK_MACRO_TEST(101, "printf without arguments includes prefix", test_CK_PRINTF_without_args),
    DLCK_MACRO_TEST(102, "printf forwards arguments", test_CK_PRINTF),
    DLCK_MACRO_TEST(103, "printf without arguments omits prefix",
		    test_CK_PRINTF_WO_PREFIX_without_args),
    DLCK_MACRO_TEST(104, "printf omits prefix", test_CK_PRINTF_WO_PREFIX),
    DLCK_MACRO_TEST(105, "append ok", test_CK_APPENDL_OK),
    DLCK_MACRO_TEST(106, "append successful rc", test_CK_APPENDL_RC_success),
    DLCK_MACRO_TEST(107, "append error rc", test_CK_APPENDL_RC_error),
    DLCK_MACRO_TEST(108, "append positive error rc", test_CK_APPENDL_RC_positive_error),
    DLCK_MACRO_TEST(109, "append formatted error", test_CK_APPENDFL_ERR),
    DLCK_MACRO_TEST(110, "formatted warnings increment count", test_CK_APPENDFL_WARN),
    DLCK_MACRO_TEST(111, "warnings without arguments increment count",
		    test_CK_APPENDFL_WARN_without_args),
    DLCK_MACRO_TEST(112, "append newline", test_CK_APPENDL),
    DLCK_MACRO_TEST(113, "printf successful rc without arguments",
		    test_CK_PRINTFL_RC_success_without_args),
    DLCK_MACRO_TEST(114, "printf error rc without arguments",
		    test_CK_PRINTFL_RC_error_without_args),
    DLCK_MACRO_TEST(115, "printf successful rc", test_CK_PRINTFL_RC_success),
    DLCK_MACRO_TEST(116, "printf error rc", test_CK_PRINTFL_RC_error),
    DLCK_MACRO_TEST_NO_FIXTURE(117, "output without checker is skipped",
			       test_CK_macros_without_checker),
    DLCK_MACRO_TEST(118, "true condition reports yes", test_CK_ASSERT_true_with_checker),
    DLCK_MACRO_TEST(119, "false condition reports no", test_CK_ASSERT_false_with_checker),
    DLCK_MACRO_TEST_NO_FIXTURE(120, "true condition without checker",
			       test_CK_ASSERT_true_without_checker),
    DLCK_MACRO_TEST_NO_FIXTURE(121, "false condition without checker asserts",
			       test_CK_ASSERT_false_without_checker),
    DLCK_MACRO_TEST(122, "indent brackets body", test_CK_INDENT),
    DLCK_MACRO_TEST(123, "nested indent restores levels", test_CK_INDENT_nested),
    DLCK_MACRO_TEST(124, "indent reaches maximum", test_CK_INDENT_reaches_maximum),
    DLCK_MACRO_TEST_NO_FIXTURE(125, "indent without checker runs body",
			       test_CK_INDENT_without_checker),
    DLCK_MACRO_TEST(126, "indent beyond maximum asserts", test_CK_INDENT_exceeds_maximum),
    DLCK_MACRO_TEST(127, "indent below minimum asserts",
		    test_checker_print_indent_dec_below_minimum),
};

int
main(int argc, char **argv)
{
	d_register_alt_assert(mock_assert);
#if CMOCKA_FILTER_SUPPORTED
	char filter[256];

	if (argc > 1) {
		int rc = snprintf(filter, sizeof(filter), "*%s*", argv[1]);

		if (rc < 0 || (size_t)rc >= sizeof(filter)) {
			fprintf(stderr, "Test filter is too long\n");
			return 1;
		}
		cmocka_set_test_filter(filter);
	}
#else
	(void)argc;
	(void)argv;
#endif
	return cmocka_run_group_tests_name("dlck_checker_macros_ut", dlck_checker_macros_tests,
					   NULL, NULL);
}
