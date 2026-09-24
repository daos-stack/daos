/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <stdbool.h>
#include <string.h>
#include <cmocka.h>

#include "dlck_checker_ut_mock.h"

static struct checker Checker;

static int
checker_vprintf(struct checker *ck, const char *fmt, va_list args)
{
	assert_ptr_equal(ck, &Checker);
	mock_vfprintf_check_output = 1;
	return vfprintf(Mock_file_stream, fmt, args);
}

static int
checker_indent_set(struct checker *ck)
{
	uint8_t ck_level = ck->ck_level;
	assert_true(ck_level <= CHECKER_INDENT_MAX);
	function_called();
	check_expected_ptr(ck);
	check_expected(ck_level);
	return mock_type(int);
}

static int
setup(void **state)
{
	Checker = (struct checker){
	    .ck_level        = 0,
	    .ck_prefix       = "-> ",
	    .ck_indent_set   = checker_indent_set,
	    .ck_vprintf      = checker_vprintf,
	    .ck_warnings_num = 7,
	};
	mock_vfprintf_check_output = 0;
	*state                     = &Checker;
	return 0;
}

static int
teardown(void **state)
{
	struct checker *ck = *state;

	assert_int_equal(ck->ck_warnings_num, 7);
	assert_string_equal(ck->ck_prefix, "-> ");
	assert_int_equal(ck->ck_level, 0);
	return 0;
}

static void
vfprintf_expected_output(const char *fmt, const char *output)
{
	expect_value(__wrap_vfprintf, stream, Mock_file_stream);
	expect_string(__wrap_vfprintf, fmt, fmt);
	expect_string(__wrap_vfprintf, output, output);
	will_return(__wrap_vfprintf, strlen(output));
}

static void
test_CK_PRINT(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("%sready", "-> ready");
	CK_PRINT(ck, "ready");
}

static void
test_CK_PRINTF(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("%sworker %d: %s", "-> worker 42: ready");
	CK_PRINTF(ck, "worker %d: %s", 42, "ready");
}

static void
test_CK_PRINT_WO_PREFIX(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("ready", "ready");
	CK_PRINT_WO_PREFIX(ck, "ready");
}

static void
test_CK_PRINTF_WO_PREFIX(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("worker %d: %s", "worker 42: ready");
	CK_PRINTF_WO_PREFIX(ck, "worker %d: %s", 42, "ready");
}

static void
test_CK_APPEND_OK(void **state)
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
	char            output[128];

	snprintf(output, sizeof(output), "error: " DF_RC "\n", DP_RC(-DER_IO));
	vfprintf_expected_output("error: " DF_RC "\n", output);
	CK_APPENDL_RC(ck, -DER_IO);
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
	assert_int_equal(ck->ck_warnings_num, 8);

	vfprintf_expected_output("warning: worker %d: %s\n", "warning: worker 43: skipped\n");
	CK_APPENDFL_WARN(ck, "worker %d: %s", 43, "skipped");
	assert_int_equal(ck->ck_warnings_num, 9);
}

static void
test_CK_APPENDL_WARN(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("warning: retry\n", "warning: retry\n");
	CK_APPENDL_WARN(ck, "retry");
	assert_int_equal(ck->ck_warnings_num, 8);

	vfprintf_expected_output("warning: skipped\n", "warning: skipped\n");
	CK_APPENDL_WARN(ck, "skipped");
	assert_int_equal(ck->ck_warnings_num, 9);
}

static void
test_CK_APPENDL(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("ready\n", "ready\n");
	CK_APPENDL(ck, "ready");
}

static void
test_CK_PRINTL_RC_success(void **state)
{
	struct checker *ck = *state;

	vfprintf_expected_output("%scheck: ok.\n", "-> check: ok.\n");
	CK_PRINTL_RC(ck, DER_SUCCESS, "check");
}

static void
test_CK_PRINTL_RC_error(void **state)
{
	struct checker *ck = *state;
	char            output[128];

	snprintf(output, sizeof(output), "-> error: check: " DF_RC "\n", DP_RC(-DER_IO));
	vfprintf_expected_output("%serror: check: " DF_RC "\n", output);
	CK_PRINTL_RC(ck, -DER_IO, "check");
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
	char            output[128];

	snprintf(output, sizeof(output), "-> error: worker 42: check: " DF_RC "\n", DP_RC(-DER_IO));
	vfprintf_expected_output("%serror: worker %d: %s: " DF_RC "\n", output);
	CK_PRINTFL_RC(ck, -DER_IO, "worker %d: %s", 42, "check");
}

static void
test_CK_macros_without_checker(void **state)
{
	struct checker *ck          = NULL;
	int             evaluations = 0;

	(void)state;
	CK_PRINT(ck, "ready");
	CK_PRINTF(ck, "%d", ++evaluations);
	CK_PRINT_WO_PREFIX(ck, "ready");
	CK_PRINTF_WO_PREFIX(ck, "%d", ++evaluations);
	CK_APPENDL_OK(ck);
	CK_APPENDL_RC(ck, DER_SUCCESS);
	CK_APPENDL_RC(ck, -DER_IO);
	CK_APPENDFL_ERR(ck, "%d", ++evaluations);
	CK_APPENDFL_WARN(ck, "%d", ++evaluations);
	CK_APPENDL_WARN(ck, "retry");
	CK_APPENDL(ck, "ready");
	CK_PRINTL_RC(ck, DER_SUCCESS, "check");
	CK_PRINTL_RC(ck, -DER_IO, "check");
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

static void
expect_indent(struct checker *ck, int ck_level)
{
	expect_function_call(checker_indent_set);
	expect_value(checker_indent_set, ck, ck);
	expect_value(checker_indent_set, ck_level, ck_level);
	will_return(checker_indent_set, DER_SUCCESS);
}

static void
test_CK_INDENT(void **state)
{
	struct checker *ck         = *state;
	int             body_calls = 0;

	expect_indent(ck, 1);
	expect_function_call(test_CK_INDENT);
	expect_indent(ck, 0);
	CK_INDENT(ck, {
		function_called();
		assert_int_equal(ck->ck_level, 1);
		body_calls++;
	});
	assert_int_equal(body_calls, 1);
	assert_int_equal(ck->ck_level, 0);
}

static void
test_CK_INDENT_nested(void **state)
{
	struct checker *ck         = *state;
	int             body_calls = 0;

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
			body_calls++;
		});
		function_called();
		assert_int_equal(ck->ck_level, 1);
		body_calls++;
	});
	assert_int_equal(body_calls, 2);
	assert_int_equal(ck->ck_level, 0);
}

static void
test_CK_INDENT_reaches_maximum(void **state)
{
	struct checker *ck         = *state;
	int             body_calls = 0;

	ck->ck_level = CHECKER_INDENT_MAX - 1;
	expect_indent(ck, CHECKER_INDENT_MAX);
	expect_function_call(test_CK_INDENT_reaches_maximum);
	expect_indent(ck, CHECKER_INDENT_MAX - 1);
	CK_INDENT(ck, {
		function_called();
		assert_int_equal(ck->ck_level, CHECKER_INDENT_MAX);
		body_calls++;
	});
	assert_int_equal(body_calls, 1);
	assert_int_equal(ck->ck_level, CHECKER_INDENT_MAX - 1);
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
	volatile int    body_calls = 0;

	ck->ck_level = CHECKER_INDENT_MAX;
	expect_assert_failure(CK_INDENT(ck, body_calls++));
	assert_int_equal(body_calls, 0);
	assert_int_equal(ck->ck_level, CHECKER_INDENT_MAX);
}

static void
test_CK_INDENT_below_minimum(void **state)
{
	struct checker *ck = *state;

	ck->ck_level = 0;
	expect_assert_failure(checker_print_indent_dec(ck));
	assert_int_equal(ck->ck_level, 0);
}

static const struct CMUnitTest dlck_checker_macros_tests[] = {
    {"DLCK_CHECKER_MACRO_101: print includes prefix", test_CK_PRINT, setup, teardown, NULL},
    {"DLCK_CHECKER_MACRO_102: printf forwards arguments", test_CK_PRINTF, setup, teardown, NULL},
    {"DLCK_CHECKER_MACRO_103: print omits prefix", test_CK_PRINT_WO_PREFIX, setup, teardown, NULL},
    {"DLCK_CHECKER_MACRO_104: printf omits prefix", test_CK_PRINTF_WO_PREFIX, setup, teardown,
     NULL},
    {"DLCK_CHECKER_MACRO_105: append ok", test_CK_APPEND_OK, setup, teardown, NULL},
    {"DLCK_CHECKER_MACRO_106: append successful rc", test_CK_APPENDL_RC_success, setup, teardown,
     NULL},
    {"DLCK_CHECKER_MACRO_107: append error rc", test_CK_APPENDL_RC_error, setup, teardown, NULL},
    {"DLCK_CHECKER_MACRO_108: append formatted error", test_CK_APPENDFL_ERR, setup, teardown, NULL},
    {"DLCK_CHECKER_MACRO_109: formatted warnings increment count", test_CK_APPENDFL_WARN, setup,
     NULL, NULL},
    {"DLCK_CHECKER_MACRO_110: warnings increment count", test_CK_APPENDL_WARN, setup, NULL, NULL},
    {"DLCK_CHECKER_MACRO_111: append newline", test_CK_APPENDL, setup, teardown, NULL},
    {"DLCK_CHECKER_MACRO_112: print successful rc", test_CK_PRINTL_RC_success, setup, teardown,
     NULL},
    {"DLCK_CHECKER_MACRO_113: print error rc", test_CK_PRINTL_RC_error, setup, teardown, NULL},
    {"DLCK_CHECKER_MACRO_114: printf successful rc", test_CK_PRINTFL_RC_success, setup, teardown,
     NULL},
    {"DLCK_CHECKER_MACRO_115: printf error rc", test_CK_PRINTFL_RC_error, setup, teardown, NULL},
    {"DLCK_CHECKER_MACRO_116: output without checker is skipped", test_CK_macros_without_checker,
     NULL, NULL, NULL},
    {"DLCK_CHECKER_MACRO_117: true condition reports yes", test_CK_ASSERT_true_with_checker, setup,
     teardown, NULL},
    {"DLCK_CHECKER_MACRO_118: false condition reports no", test_CK_ASSERT_false_with_checker, setup,
     teardown, NULL},
    {"DLCK_CHECKER_MACRO_119: true condition without checker", test_CK_ASSERT_true_without_checker,
     setup, NULL, NULL},
    {"DLCK_CHECKER_MACRO_120: false condition without checker asserts",
     test_CK_ASSERT_false_without_checker, setup, NULL, NULL},
    {"DLCK_CHECKER_MACRO_121: indent brackets body", test_CK_INDENT, setup, teardown, NULL},
    {"DLCK_CHECKER_MACRO_122: nested indent restores levels", test_CK_INDENT_nested, setup,
     teardown, NULL},
    {"DLCK_CHECKER_MACRO_123: indent reaches maximum", test_CK_INDENT_reaches_maximum, setup, NULL,
     NULL},
    {"DLCK_CHECKER_MACRO_124: indent without checker runs body", test_CK_INDENT_without_checker,
     setup, NULL, NULL},
    {"DLCK_CHECKER_MACRO_125: indent beyond maximum asserts", test_CK_INDENT_exceeds_maximum, setup,
     NULL, NULL},
    {"DLCK_CHECKER_MACRO_126: indent below minimum asserts", test_CK_INDENT_below_minimum, setup,
     teardown, NULL},
};

int
main(int argc, char **argv)
{
	d_register_alt_assert(mock_assert);
#if CMOCKA_FILTER_SUPPORTED
	char filter[1024];
	if (argc > 1) {
		snprintf(filter, 1024, "*%s*", argv[1]);
		cmocka_set_test_filter(filter);
	}
#endif
	return cmocka_run_group_tests_name("dlck_checker_macros_ut", dlck_checker_macros_tests,
					   NULL, NULL);
}
