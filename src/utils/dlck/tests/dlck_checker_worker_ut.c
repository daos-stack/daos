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
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <abt.h>
#include <cmocka.h>

#include <daos_srv/checker.h>
#include <daos_srv/mgmt_tgt_common.h>

#include "../dlck_checker.h"
#include "dlck_checker_ut_mock.h"

static int
setup(void **state)
{
	*state = calloc(1, sizeof(struct checker));
	assert_non_null(*state);
	mock_d_calloc_enabled = 0;
	mock_vfprintf_enabled = 0;
	mock_vfprintf_check_args = 0;
	mock_fflush_enabled = 0;
	mock_fopen_fake_stream_enable = 0;
	return 0;
}

/* worker init: valid options create a logfile and install callbacks. */
static void
test_worker_init_success(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {.cko_non_zero_padding = CHECKER_EVENT_WARNING};
	static char             log_dir[] = "/tmp/dlck-worker-XXXXXX";
	static char             log_file[PATH_MAX];
	uuid_t                  pool_uuid;
	struct stat             log_stat;

	assert_non_null(mkdtemp(log_dir));
	uuid_generate(pool_uuid);
	expect_checker_d_calloc(sizeof(Dcw), &Dcw);
	will_return(__wrap_d_asprintf2, 0);
	will_return(__wrap_fopen, 0);
	assert_int_equal(dlck_checker_worker_init(&options, log_dir, pool_uuid, 3, NULL, ck), DER_SUCCESS);

	assert_ptr_equal(ck->ck_private, &Dcw);
	assert_int_equal(Dcw.magic, DLCK_CHECKER_WORKER_MAGIC);
	assert_non_null(Dcw.stream);
	assert_non_null(ck->ck_vprintf);
	assert_non_null(ck->ck_indent_set);
	assert_ptr_equal(ck->ck_prefix, Dcw.prefix);
	assert_int_equal(ck->ck_options.cko_non_zero_padding, options.cko_non_zero_padding);

	snprintf(log_file, sizeof(log_file), "%s/" DF_UUIDF "_%s%d", log_dir, DP_UUID(pool_uuid),
		 VOS_FILE, 3);
	assert_int_equal(stat(log_file, &log_stat), 0);
	assert_int_equal(ck->ck_level, 0);

	expect_value(__wrap_d_free, ptr, &Dcw);
	dlck_checker_worker_fini(ck);
	assert_null(ck->ck_private);
	assert_int_equal(unlink(log_file), 0);
	assert_int_equal(rmdir(log_dir), 0);
}

/* worker init: fopen fails and returns the mapped I/O error. */
static void
test_worker_init_log_open_failure(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	uuid_t                  pool_uuid;

	uuid_generate(pool_uuid);
	expect_checker_d_calloc(sizeof(Dcw), &Dcw);
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
	uuid_t                  pool_uuid;

	uuid_generate(pool_uuid);
	expect_checker_d_calloc(sizeof(Dcw), &Dcw);
	will_return(__wrap_d_asprintf2, ENOMEM);
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
	uuid_t                  pool_uuid;

	uuid_generate(pool_uuid);
	expect_checker_d_calloc(sizeof(Dcw), NULL);
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, NULL, ck),
			 -DER_NOMEM);
	assert_null(ck->ck_private);
}

/* worker callbacks: indentation and printing use the initialized payload. */
static void
test_worker_callbacks_success(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	uuid_t                  pool_uuid;

	uuid_generate(pool_uuid);
	expect_checker_d_calloc(sizeof(Dcw), &Dcw);
	will_return(__wrap_d_asprintf2, 0);
	will_return(__wrap_fopen, 0);
	mock_fopen_fake_stream_enable = 1;
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, NULL, ck),
			 DER_SUCCESS);
	ck->ck_level = 2;
	assert_int_equal(ck->ck_indent_set(ck), DER_SUCCESS);
	assert_string_equal(ck->ck_prefix, "-- ");
	mock_vfprintf_enabled = 1;
	mock_vfprintf_check_args = 1;
	mock_fflush_enabled = 1;
	expect_string(__wrap_vfprintf, fmt, "worker %d: %s");
	expect_string(__wrap_vfprintf, output, "worker 42: ready");
	will_return(__wrap_vfprintf, 16);
	will_return(__wrap_fflush, 0);
	assert_int_equal(ck_common_printf(ck, "worker %d: %s", 42, "ready"), DER_SUCCESS);

	expect_value(__wrap_d_free, ptr, &Dcw);
	dlck_checker_worker_fini(ck);
}

static void
test_worker_printf_with_indent(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	uuid_t                  pool_uuid;

	uuid_generate(pool_uuid);
	expect_checker_d_calloc(sizeof(Dcw), &Dcw);
	will_return(__wrap_d_asprintf2, 0);
	will_return(__wrap_fopen, 0);
	mock_fopen_fake_stream_enable = 1;
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, NULL, ck),
			 DER_SUCCESS);
	ck->ck_level = 2;
	assert_int_equal(ck->ck_indent_set(ck), DER_SUCCESS);

	mock_vfprintf_enabled = 1;
	mock_vfprintf_check_args = 1;
	mock_fflush_enabled = 1;
	expect_string(__wrap_vfprintf, fmt, "%sworker %d");
	expect_string(__wrap_vfprintf, output, "-- worker 42");
	will_return(__wrap_vfprintf, 12);
	will_return(__wrap_fflush, 0);
	CK_PRINTF(ck, "worker %d", 42);

	expect_value(__wrap_d_free, ptr, &Dcw);
	dlck_checker_worker_fini(ck);
}

/* worker vprintf: vfprintf failure is propagated from its logfile stream. */
static void
test_worker_vprintf_vfprintf_failure(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	uuid_t                  pool_uuid;

	uuid_generate(pool_uuid);
	expect_checker_d_calloc(sizeof(Dcw), &Dcw);
	will_return(__wrap_d_asprintf2, 0);
	will_return(__wrap_fopen, 0);
	mock_fopen_fake_stream_enable = 1;
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, NULL, ck),
			 DER_SUCCESS);

	mock_vfprintf_enabled = 1;
	will_return(__wrap_vfprintf, -1);
	assert_int_equal(ck_common_printf(ck, "worker"), daos_errno2der(EIO));

	expect_value(__wrap_d_free, ptr, &Dcw);
	dlck_checker_worker_fini(ck);
}

/* worker vprintf: fflush failure is propagated from its logfile stream. */
static void
test_worker_vprintf_fflush_failure(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	static char             log_dir[] = "/tmp/dlck-worker-fflush-XXXXXX";
	static char             log_file[PATH_MAX];
	uuid_t                  pool_uuid;
	struct stat             log_stat;

	uuid_generate(pool_uuid);
	assert_non_null(mkdtemp(log_dir));
	expect_checker_d_calloc(sizeof(Dcw), &Dcw);
	will_return(__wrap_d_asprintf2, 0);
	will_return(__wrap_fopen, 0);
	assert_int_equal(dlck_checker_worker_init(&options, log_dir, pool_uuid, 0, NULL, ck), DER_SUCCESS);

	mock_vfprintf_enabled = 1;
	mock_fflush_enabled = 1;
	will_return(__wrap_vfprintf, 1);
	will_return(__wrap_fflush, EIO);
	assert_int_equal(ck_common_printf(ck, "worker"), daos_errno2der(EIO));

	snprintf(log_file, sizeof(log_file), "%s/" DF_UUIDF "_%s%d", log_dir, DP_UUID(pool_uuid),
		 VOS_FILE, 0);
	expect_value(__wrap_d_free, ptr, &Dcw);
	dlck_checker_worker_fini(ck);
	assert_int_equal(stat(log_file, &log_stat), 0);
	assert_int_equal(unlink(log_file), 0);
	assert_int_equal(rmdir(log_dir), 0);
}

/* worker get_custom: invalid magic triggers an assertion. */
static void
test_worker_get_custom_invalid_magic(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	uuid_t                  pool_uuid;

	uuid_generate(pool_uuid);
	expect_checker_d_calloc(sizeof(Dcw), &Dcw);
	will_return(__wrap_d_asprintf2, 0);
	will_return(__wrap_fopen, 0);
	mock_fopen_fake_stream_enable = 1;
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, NULL, ck), DER_SUCCESS);
	Dcw.magic = ~DLCK_CHECKER_WORKER_MAGIC;
	expect_assert_failure(dlck_checker_worker_fini(ck));
	Dcw.magic = DLCK_CHECKER_WORKER_MAGIC;
	expect_value(__wrap_d_free, ptr, &Dcw);
	dlck_checker_worker_fini(ck);
}

/* worker indent callback: negative and above-maximum levels assert. */
static void
test_worker_indent_set_out_of_range(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	uuid_t                  pool_uuid;

	uuid_generate(pool_uuid);
	expect_checker_d_calloc(sizeof(Dcw), &Dcw);
	will_return(__wrap_d_asprintf2, 0);
	will_return(__wrap_fopen, 0);
	mock_fopen_fake_stream_enable = 1;
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, NULL, ck),
			 DER_SUCCESS);

	ck->ck_level = -1;
	expect_assert_failure(ck->ck_indent_set(ck));
	ck->ck_level = CHECKER_INDENT_MAX + 1;
	expect_assert_failure(ck->ck_indent_set(ck));

	ck->ck_level = 0;
	expect_value(__wrap_d_free, ptr, &Dcw);
	dlck_checker_worker_fini(ck);
}

static int
teardown(void **state)
{
	free(*state);
	return 0;
}

static const struct CMUnitTest dlck_checker_worker_tests[] = {
	{"DLCK_CHECKER_WORKER_100: init - success", test_worker_init_success, setup, teardown},
	{"DLCK_CHECKER_WORKER_101: init - log open failure", test_worker_init_log_open_failure, setup, teardown},
	{"DLCK_CHECKER_WORKER_102: init - log path allocation failure", test_worker_init_log_path_alloc_failure, setup,
	 teardown},
	{"DLCK_CHECKER_WORKER_103: init - allocation failure", test_worker_init_alloc_failure, setup, teardown},
	{"DLCK_CHECKER_WORKER_104: callbacks - success", test_worker_callbacks_success, setup, teardown},
	{"DLCK_CHECKER_WORKER_105: printf - indentation", test_worker_printf_with_indent, setup, teardown},
	{"DLCK_CHECKER_WORKER_106: get_custom - invalid magic", test_worker_get_custom_invalid_magic, setup,
	 teardown},
	{"DLCK_CHECKER_WORKER_107: vprintf - vfprintf failure",
	 test_worker_vprintf_vfprintf_failure, setup, teardown},
	{"DLCK_CHECKER_WORKER_108: vprintf - fflush failure", test_worker_vprintf_fflush_failure, setup,
	 teardown},
	{"DLCK_CHECKER_WORKER_109: indent - out of range", test_worker_indent_set_out_of_range, setup,
	 teardown},
};

int
main(void)
{
	d_register_alt_assert(mock_assert);
	return cmocka_run_group_tests_name("dlck_checker_worker_ut", dlck_checker_worker_tests, NULL,
					   NULL);
}
