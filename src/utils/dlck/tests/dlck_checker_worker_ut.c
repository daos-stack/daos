/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <abt.h>
#include <cmocka.h>

#include <daos_srv/checker.h>

#include "../dlck_checker.h"
#include "dlck_checker_ut_mock.h"

#define MOCK_LOG_DIR ((char *)0x104D14)

static int            mock_worker_fclose_rc;
static struct checker worker_checker_state;

static int
setup(void **state)
{
	*state                     = &worker_checker_state;
	mock_vfprintf_check_output = 0;
	mock_worker_fclose_rc      = 0;
	return 0;
}

static int
teardown(void **state)
{
	(void)state;
	return 0;
}

static void
expect_worker_fopen(int error)
{
	expect_string(__wrap_fopen, path, Mock_log_file);
	expect_string(__wrap_fopen, mode, "w");
	will_return(__wrap_fopen, error);
	if (error == 0)
		will_return(__wrap_fopen, Mock_file_stream);
}

static int
mock_main_ck_vprintf(struct checker *ck, const char *fmt, va_list args)
{
	(void)ck;
	(void)fmt;
	(void)args;
	function_called();
	return DER_SUCCESS;
}

/* Start a worker test with DLCK defaults and an initialized checker. */
static int
setup_worker_checker(void **state)
{
	struct checker        *ck;
	struct checker_options options = {
	    .cko_non_zero_padding = CHECKER_EVENT_WARNING,
	};
	int rc;

	rc = setup(state);
	if (rc != 0)
		return rc;

	ck = *state;
	EXPECT_CHECKER_D_CALLOC(Dcw);
	will_return(__wrap_d_asprintf2, Mock_log_file);
	expect_worker_fopen(0);
	expect_value(__wrap_d_free, ptr, Mock_log_file);
	assert_int_equal(
	    dlck_checker_worker_init(&options, MOCK_LOG_DIR, Mock_pool_uuid, 0, NULL, ck),
	    DER_SUCCESS);
	return 0;
}

/* Finalize the worker checker and verify teardown clears all checker state. */
static int
teardown_worker_checker(void **state)
{
	struct checker  ck_zeroed = {0};
	struct checker *ck        = *state;

	expect_value(__wrap_fclose, stream, Mock_file_stream);
	expect_function_call(__wrap_fclose);
	will_return(__wrap_fclose, mock_worker_fclose_rc);
	expect_value(__wrap_d_free, ptr, &Dcw);
	dlck_checker_worker_fini(ck);
	assert_memory_equal(ck, &ck_zeroed, sizeof(*ck));
	return teardown(state);
}

static void
test_worker_init_success(void **state)
{
	struct checker *ck = *state;

	/* setup_worker_checker() is an integral part of this test. */
	assert_ptr_equal(ck->ck_private, &Dcw);
	assert_int_equal(Dcw.magic, DLCK_CHECKER_WORKER_MAGIC);
	assert_non_null(ck->ck_vprintf);
	assert_non_null(ck->ck_indent_set);
	assert_ptr_equal(ck->ck_prefix, Dcw.prefix);
	assert_int_equal(ck->ck_options.cko_non_zero_padding, CHECKER_EVENT_WARNING);

	assert_ptr_equal(Dcw.stream, Mock_file_stream);
	assert_int_equal(ck->ck_level, 0);
	/* teardown_worker_checker() is an integral part of this test. */
}

/* worker init: fopen fails and returns the mapped I/O error. */
static void
test_worker_init_log_open_failure(void **state)
{
	struct checker        *ck      = *state;
	struct checker         main_ck = {.ck_vprintf = mock_main_ck_vprintf};
	struct checker_options options = {0};

	EXPECT_CHECKER_D_CALLOC(Dcw);
	will_return(__wrap_d_asprintf2, Mock_log_file);
	expect_worker_fopen(EIO);
	expect_function_call(mock_main_ck_vprintf);
	expect_value(__wrap_d_free, ptr, Mock_log_file);
	expect_value(__wrap_d_free, ptr, &Dcw);
	assert_int_equal(
	    dlck_checker_worker_init(&options, MOCK_LOG_DIR, Mock_pool_uuid, 0, &main_ck, ck),
	    daos_errno2der(EIO));
	assert_null(ck->ck_private);
}

/* worker init: logfile path formatting returns an allocation error. */
static void
test_worker_init_log_path_alloc_failure(void **state)
{
	struct checker        *ck      = *state;
	struct checker         main_ck = {.ck_vprintf = mock_main_ck_vprintf};
	struct checker_options options = {0};

	EXPECT_CHECKER_D_CALLOC(Dcw);
	will_return(__wrap_d_asprintf2, NULL);
	expect_function_call(mock_main_ck_vprintf);
	expect_value(__wrap_d_free, ptr, &Dcw);
	assert_int_equal(
	    dlck_checker_worker_init(&options, MOCK_LOG_DIR, Mock_pool_uuid, 0, &main_ck, ck),
	    -DER_NOMEM);
	assert_null(ck->ck_private);
}

/* worker init: payload allocation returns NULL. */
static void
test_worker_init_alloc_failure(void **state)
{
	struct checker        *ck      = *state;
	struct checker_options options = {0};

	expect_checker_d_calloc(sizeof(Dcw), NULL);
	assert_int_equal(
	    dlck_checker_worker_init(&options, MOCK_LOG_DIR, Mock_pool_uuid, 0, NULL, ck),
	    -DER_NOMEM);
	assert_null(ck->ck_private);
}

/* worker vprintf: successful output is written to the initialized stream. */
static void
test_worker_vprintf_success(void **state)
{
	struct checker *ck = *state;

	mock_vfprintf_check_output = 1;
	expect_value(__wrap_vfprintf, stream, Mock_file_stream);
	expect_string(__wrap_vfprintf, fmt, "worker %d: %s");
	expect_string(__wrap_vfprintf, output, "worker 42: ready");
	will_return(__wrap_vfprintf, 16);
	expect_value(__wrap_fflush, stream, Mock_file_stream);
	will_return(__wrap_fflush, 0);
	assert_int_equal(ck_common_printf(ck, "worker %d: %s", 42, "ready"), DER_SUCCESS);
}

static void
test_worker_CK_PRINTF_with_indent(void **state)
{
	struct checker *ck = *state;

	ck->ck_level = 2;
	assert_int_equal(ck->ck_indent_set(ck), DER_SUCCESS);
	mock_vfprintf_check_output = 1;
	expect_value(__wrap_vfprintf, stream, Mock_file_stream);
	expect_string(__wrap_vfprintf, fmt, "%sworker %d");
	expect_string(__wrap_vfprintf, output, "-- worker 42");
	will_return(__wrap_vfprintf, 12);
	expect_value(__wrap_fflush, stream, Mock_file_stream);
	will_return(__wrap_fflush, 0);
	CK_PRINTF(ck, "worker %d", 42);
}

/* worker vprintf: vfprintf failure is propagated from its logfile stream. */
static void
test_worker_vprintf_vfprintf_failure(void **state)
{
	struct checker *ck = *state;

	expect_value(__wrap_vfprintf, stream, Mock_file_stream);
	expect_string(__wrap_vfprintf, fmt, "worker");
	will_return(__wrap_vfprintf, -1);
	assert_int_equal(ck_common_printf(ck, "worker"), daos_errno2der(EIO));
}

/* worker vprintf: fflush failure is propagated from its logfile stream. */
static void
test_worker_vprintf_fflush_failure(void **state)
{
	struct checker *ck = *state;

	expect_value(__wrap_vfprintf, stream, Mock_file_stream);
	expect_string(__wrap_vfprintf, fmt, "worker");
	will_return(__wrap_vfprintf, 1);
	expect_value(__wrap_fflush, stream, Mock_file_stream);
	will_return(__wrap_fflush, EIO);
	assert_int_equal(ck_common_printf(ck, "worker"), daos_errno2der(EIO));
}

/* worker get_custom: invalid magic triggers an assertion. */
static void
test_worker_get_custom_invalid_magic(void **state)
{
	struct checker *ck = *state;

	Dcw.magic = ~DLCK_CHECKER_WORKER_MAGIC;
	expect_assert_failure(dlck_checker_worker_fini(ck));
	/* Restore magic so teardown_worker_checker() can clean up. */
	Dcw.magic = DLCK_CHECKER_WORKER_MAGIC;
}

/* worker fini: fclose failure does not prevent payload and checker cleanup. */
static void
test_worker_fini_fclose_failure(void **state)
{
	(void)state;
	/* teardown_worker_checker() continues this test to check cleanup after
	   fclose fails. */
	mock_worker_fclose_rc = EOF;
}

/* worker indent callback: negative and above-maximum levels assert. */
static void
test_worker_indent_set_out_of_range(void **state)
{
	struct checker *ck = *state;

	ck->ck_level = -1;
	expect_assert_failure(ck->ck_indent_set(ck));
	ck->ck_level = CHECKER_INDENT_MAX + 1;
	expect_assert_failure(ck->ck_indent_set(ck));
	ck->ck_level = 0;
}

/* worker indent callback: invalid magic triggers an assertion. */
static void
test_worker_indent_set_invalid_magic(void **state)
{
	struct checker *ck = *state;

	Dcw.magic = ~DLCK_CHECKER_WORKER_MAGIC;
	expect_assert_failure(ck->ck_indent_set(ck));
	/* Restore magic so teardown_worker_checker() can clean up. */
	Dcw.magic = DLCK_CHECKER_WORKER_MAGIC;
}

static const struct CMUnitTest dlck_checker_worker_tests[] = {
    {"DLCK_CHECKER_WORKER_101: init - success", test_worker_init_success, setup_worker_checker,
     teardown_worker_checker},
    {"DLCK_CHECKER_WORKER_102: init - log open failure", test_worker_init_log_open_failure, setup,
     teardown},
    {"DLCK_CHECKER_WORKER_103: init - log path allocation failure",
     test_worker_init_log_path_alloc_failure, setup, teardown},
    {"DLCK_CHECKER_WORKER_104: init - allocation failure", test_worker_init_alloc_failure, setup,
     teardown},
    {"DLCK_CHECKER_WORKER_105: fini - fclose failure", test_worker_fini_fclose_failure,
     setup_worker_checker, teardown_worker_checker},
    {"DLCK_CHECKER_WORKER_106: get_custom - invalid magic", test_worker_get_custom_invalid_magic,
     setup_worker_checker, teardown_worker_checker},
    {"DLCK_CHECKER_WORKER_107: indent - out of range", test_worker_indent_set_out_of_range,
     setup_worker_checker, teardown_worker_checker},
    {"DLCK_CHECKER_WORKER_108: indent - invalid magic", test_worker_indent_set_invalid_magic,
     setup_worker_checker, teardown_worker_checker},
    {"DLCK_CHECKER_WORKER_109: vprintf - success", test_worker_vprintf_success,
     setup_worker_checker, teardown_worker_checker},
    {"DLCK_CHECKER_WORKER_110: vprintf - vfprintf failure", test_worker_vprintf_vfprintf_failure,
     setup_worker_checker, teardown_worker_checker},
    {"DLCK_CHECKER_WORKER_111: vprintf - fflush failure", test_worker_vprintf_fflush_failure,
     setup_worker_checker, teardown_worker_checker},
    {"DLCK_CHECKER_WORKER_112: CK_PRINTF - indentation", test_worker_CK_PRINTF_with_indent,
     setup_worker_checker, teardown_worker_checker},
};

int
main(void)
{
	d_register_alt_assert(mock_assert);
	return cmocka_run_group_tests_name("dlck_checker_worker_ut", dlck_checker_worker_tests,
					   NULL, NULL);
}
