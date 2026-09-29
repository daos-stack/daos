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
#include <uuid/uuid.h>
#include <abt.h>
#include <cmocka.h>

#include <daos_srv/checker.h>
#include <daos_srv/mgmt_tgt_common.h>

#include "../dlck_checker.h"
#include "dlck_checker_ut_mock.h"

static int mock_worker_fclose_rc;

static int
setup(void **state)
{
	*state = calloc(1, sizeof(struct checker));
	assert_non_null(*state);
	mock_vfprintf_check_args = 0;
	mock_worker_fclose_rc    = 0;
	return 0;
}

static int
teardown(void **state)
{
	free(*state);
	return 0;
}

static int
setup_worker_checker(void **state)
{
	struct checker         *ck;
	struct checker_options  options = {0};
	uuid_t                  pool_uuid = {0};
	int                     rc;

	rc = setup(state);
	if (rc != 0)
		return rc;

	ck = *state;
	EXPECT_CHECKER_D_CALLOC(Dcw);
	will_return(__wrap_d_asprintf2, 0);
	will_return(__wrap_fopen, 0);
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, NULL, ck),
			 DER_SUCCESS);
	return 0;
}

static int
teardown_worker_checker(void **state)
{
	struct checker  ck_zeroed;
	struct checker *ck = *state;

	memset(&ck_zeroed, 0, sizeof(ck_zeroed));
	expect_value(__wrap_d_free, ptr, &Dcw);
	expect_function_call(__wrap_fclose);
	will_return(__wrap_fclose, mock_worker_fclose_rc);
	dlck_checker_worker_fini(ck);
	assert_memory_equal(ck, &ck_zeroed, sizeof(*ck));
	return teardown(state);
}

/* worker init: valid options install checker state and a logfile stream. */
static void
test_worker_init_success(void **state)
{
	struct checker         *ck = *state;
	struct checker          ck_zeroed;
	struct checker_options  options = {.cko_non_zero_padding = CHECKER_EVENT_WARNING};
	uuid_t                  pool_uuid = {0};

	memset(&ck_zeroed, 0, sizeof(ck_zeroed));
	EXPECT_CHECKER_D_CALLOC(Dcw);
	will_return(__wrap_d_asprintf2, 0);
	will_return(__wrap_fopen, 0);
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 3, NULL, ck),
			 DER_SUCCESS);

	assert_ptr_equal(ck->ck_private, &Dcw);
	assert_int_equal(Dcw.magic, DLCK_CHECKER_WORKER_MAGIC);
	assert_non_null(Dcw.stream);
	assert_non_null(ck->ck_vprintf);
	assert_non_null(ck->ck_indent_set);
	assert_ptr_equal(ck->ck_prefix, Dcw.prefix);
	assert_int_equal(ck->ck_options.cko_non_zero_padding, options.cko_non_zero_padding);

	assert_ptr_equal(Dcw.stream, Mock_file_stream);
	assert_int_equal(ck->ck_level, 0);

	expect_value(__wrap_d_free, ptr, &Dcw);
	expect_function_call(__wrap_fclose);
	will_return(__wrap_fclose, 0);
	dlck_checker_worker_fini(ck);
	assert_memory_equal(ck, &ck_zeroed, sizeof(*ck));
}

/* worker init: fopen fails and returns the mapped I/O error. */
static void
test_worker_init_log_open_failure(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	uuid_t                  pool_uuid = {0};

	EXPECT_CHECKER_D_CALLOC(Dcw);
	will_return(__wrap_d_asprintf2, 0);
	will_return(__wrap_fopen, EIO);
	expect_value(__wrap_d_free, ptr, &Dcw);
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, NULL, ck),
			 daos_errno2der(EIO));
	assert_null(ck->ck_private);
}

/* worker init: logfile path formatting returns an allocation error. */
static void
test_worker_init_log_path_alloc_failure(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	uuid_t                  pool_uuid = {0};

	EXPECT_CHECKER_D_CALLOC(Dcw);
	will_return(__wrap_d_asprintf2, -1);
	expect_value(__wrap_d_free, ptr, &Dcw);
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, NULL, ck),
			 -DER_NOMEM);
	assert_null(ck->ck_private);
}

/* worker init: payload allocation returns NULL. */
static void
test_worker_init_alloc_failure(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	uuid_t                  pool_uuid = {0};

	expect_checker_d_calloc(sizeof(Dcw), NULL);
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, NULL, ck),
			 -DER_NOMEM);
	assert_null(ck->ck_private);
}

/* worker callbacks: indentation and printing use the initialized payload. */
static void
test_worker_callbacks_success(void **state)
{
	struct checker *ck = *state;

	ck->ck_level = 2;
	assert_int_equal(ck->ck_indent_set(ck), DER_SUCCESS);
	assert_string_equal(ck->ck_prefix, "-- ");
	mock_vfprintf_check_args = 1;
	expect_string(__wrap_vfprintf, fmt, "worker %d: %s");
	expect_string(__wrap_vfprintf, output, "worker 42: ready");
	will_return(__wrap_vfprintf, 16);
	will_return(__wrap_fflush, 0);
	assert_int_equal(ck_common_printf(ck, "worker %d: %s", 42, "ready"), DER_SUCCESS);
}

static void
test_worker_printf_with_indent(void **state)
{
	struct checker *ck = *state;

	ck->ck_level = 2;
	assert_int_equal(ck->ck_indent_set(ck), DER_SUCCESS);
	mock_vfprintf_check_args = 1;
	expect_string(__wrap_vfprintf, fmt, "%sworker %d");
	expect_string(__wrap_vfprintf, output, "-- worker 42");
	will_return(__wrap_vfprintf, 12);
	will_return(__wrap_fflush, 0);
	CK_PRINTF(ck, "worker %d", 42);
}

/* worker vprintf: vfprintf failure is propagated from its logfile stream. */
static void
test_worker_vprintf_vfprintf_failure(void **state)
{
	struct checker *ck = *state;

	will_return(__wrap_vfprintf, -1);
	assert_int_equal(ck_common_printf(ck, "worker"), daos_errno2der(EIO));
}

/* worker vprintf: fflush failure is propagated from its logfile stream. */
static void
test_worker_vprintf_fflush_failure(void **state)
{
	struct checker *ck = *state;

	will_return(__wrap_vfprintf, 1);
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
	Dcw.magic = DLCK_CHECKER_WORKER_MAGIC;
}

/* worker fini: fclose failure does not prevent payload and checker cleanup. */
static void
test_worker_fini_fclose_failure(void **state)
{
	(void)state;
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

static const struct CMUnitTest dlck_checker_worker_tests[] = {
    {"DLCK_CHECKER_WORKER_100: init - success", test_worker_init_success, setup, teardown},
    {"DLCK_CHECKER_WORKER_101: init - log open failure", test_worker_init_log_open_failure, setup,
     teardown},
    {"DLCK_CHECKER_WORKER_102: init - log path allocation failure",
     test_worker_init_log_path_alloc_failure, setup, teardown},
    {"DLCK_CHECKER_WORKER_103: init - allocation failure", test_worker_init_alloc_failure, setup,
     teardown},
    {"DLCK_CHECKER_WORKER_104: callbacks - success", test_worker_callbacks_success,
     setup_worker_checker, teardown_worker_checker},
    {"DLCK_CHECKER_WORKER_105: printf - indentation", test_worker_printf_with_indent,
     setup_worker_checker, teardown_worker_checker},
    {"DLCK_CHECKER_WORKER_106: get_custom - invalid magic", test_worker_get_custom_invalid_magic,
     setup_worker_checker, teardown_worker_checker},
    {"DLCK_CHECKER_WORKER_107: vprintf - vfprintf failure", test_worker_vprintf_vfprintf_failure,
     setup_worker_checker, teardown_worker_checker},
    {"DLCK_CHECKER_WORKER_108: vprintf - fflush failure", test_worker_vprintf_fflush_failure,
     setup_worker_checker, teardown_worker_checker},
    {"DLCK_CHECKER_WORKER_109: fini - fclose failure", test_worker_fini_fclose_failure,
     setup_worker_checker, teardown_worker_checker},
    {"DLCK_CHECKER_WORKER_110: indent - out of range", test_worker_indent_set_out_of_range,
     setup_worker_checker, teardown_worker_checker},
};

int
main(void)
{
	d_register_alt_assert(mock_assert);
	return cmocka_run_group_tests_name("dlck_checker_worker_ut", dlck_checker_worker_tests, NULL,
					   NULL);
}
