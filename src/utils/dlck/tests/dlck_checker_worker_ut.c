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
	last_freed_payload = NULL;
	return 0;
}

static int
setup_main_checker(struct checker *ck)
{
	will_return(__wrap_d_calloc, 0);
	will_return(__wrap_ABT_mutex_create, ABT_SUCCESS);
	assert_int_equal(dlck_checker_main_init(ck), DER_SUCCESS);
	return 0;
}

static int
fini_main_checker(struct checker *ck)
{
	will_return(__wrap_ABT_mutex_free, ABT_SUCCESS);
	return dlck_checker_main_fini(ck);
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
	will_return(__wrap_d_calloc, 0);
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

	dlck_checker_worker_fini(ck);
	assert_null(ck->ck_private);
	assert_ptr_equal(last_freed_payload, &Dcw);
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
	will_return(__wrap_d_calloc, 0);
	will_return(__wrap_d_asprintf2, 0);
	will_return(__wrap_fopen, EIO);
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, NULL, ck),
			 daos_errno2der(EIO));
	assert_null(ck->ck_private);
	assert_ptr_equal(last_freed_payload, &Dcw);
}

/* worker init: logfile path formatting returns an allocation error. */
static void
test_worker_init_log_path_alloc_failure(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	uuid_t                  pool_uuid;

	uuid_generate(pool_uuid);
	will_return(__wrap_d_calloc, 0);
	will_return(__wrap_d_asprintf2, ENOMEM);
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, NULL, ck),
			 -DER_NOMEM);
	assert_null(ck->ck_private);
	assert_ptr_equal(last_freed_payload, &Dcw);
}

/* worker init: payload allocation returns NULL. */
static void
test_worker_init_alloc_failure(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	uuid_t                  pool_uuid;

	uuid_generate(pool_uuid);
	will_return(__wrap_d_calloc, ENOMEM);
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, NULL, ck),
			 -DER_NOMEM);
	assert_null(ck->ck_private);
	assert_null(last_freed_payload);
}

/* worker init: payload allocation failure returns before main checker reporting. */
static void
test_worker_init_alloc_failure_with_main_checker(void **state)
{
	struct checker         *ck = *state;
	struct checker          main_ck = {0};
	struct checker_options  options = {0};
	uuid_t                  pool_uuid;

	setup_main_checker(&main_ck);
	uuid_generate(pool_uuid);
	will_return(__wrap_d_calloc, ENOMEM);
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, &main_ck, ck),
			 -DER_NOMEM);
	assert_null(ck->ck_private);
	assert_ptr_equal(last_freed_payload, NULL);
	assert_int_equal(fini_main_checker(&main_ck), DER_SUCCESS);
}

/* worker init: fopen failure is reported using main checker. */
static void
test_worker_init_log_open_failure_with_main_checker(void **state)
{
	struct checker         *ck = *state;
	struct checker          main_ck = {0};
	struct checker_options  options = {0};
	uuid_t                  pool_uuid;

	setup_main_checker(&main_ck);
	uuid_generate(pool_uuid);
	will_return(__wrap_d_calloc, 0);
	will_return(__wrap_d_asprintf2, 0);
	will_return(__wrap_fopen, EIO);
	will_return(__wrap_ABT_mutex_lock, ABT_SUCCESS);
	will_return(__wrap_ABT_mutex_unlock, ABT_SUCCESS);
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, &main_ck, ck),
			 daos_errno2der(EIO));
	assert_null(ck->ck_private);
	assert_ptr_equal(last_freed_payload, &Dcw);
	assert_int_equal(fini_main_checker(&main_ck), DER_SUCCESS);
}

/* worker callbacks: indentation and printing use the initialized payload. */
static void
test_worker_callbacks_success(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	static char             log_dir[] = "/tmp/dlck-worker-callback-XXXXXX";
	static char             log_file[PATH_MAX];
	uuid_t                  pool_uuid;
	struct stat             log_stat;

	uuid_generate(pool_uuid);
	assert_non_null(mkdtemp(log_dir));
	will_return(__wrap_d_calloc, 0);
	will_return(__wrap_d_asprintf2, 0);
	will_return(__wrap_fopen, 0);
	assert_int_equal(dlck_checker_worker_init(&options, log_dir, pool_uuid, 0, NULL, ck), DER_SUCCESS);
	ck->ck_level = 2;
	assert_int_equal(ck->ck_indent_set(ck), DER_SUCCESS);
	assert_string_equal(ck->ck_prefix, "-- ");
	assert_int_equal(ck_common_printf(ck, "worker"), DER_SUCCESS);

	snprintf(log_file, sizeof(log_file), "%s/" DF_UUIDF "_%s%d", log_dir, DP_UUID(pool_uuid),
		 VOS_FILE, 0);
	dlck_checker_worker_fini(ck);
	assert_int_equal(stat(log_file, &log_stat), 0);
	assert_ptr_equal(last_freed_payload, &Dcw);
	assert_int_equal(unlink(log_file), 0);
	assert_int_equal(rmdir(log_dir), 0);
}

/* worker get_custom: invalid magic triggers an assertion. */
static void
test_worker_get_custom_invalid_magic(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	static char             log_dir[] = "/tmp/dlck-worker-magic-XXXXXX";
	static char             log_file[PATH_MAX];
	uuid_t                  pool_uuid;
	struct stat             log_stat;

	uuid_generate(pool_uuid);
	assert_non_null(mkdtemp(log_dir));
	will_return(__wrap_d_calloc, 0);
	will_return(__wrap_d_asprintf2, 0);
	will_return(__wrap_fopen, 0);
	assert_int_equal(dlck_checker_worker_init(&options, log_dir, pool_uuid, 0, NULL, ck), DER_SUCCESS);
	snprintf(log_file, sizeof(log_file), "%s/" DF_UUIDF "_%s%d", log_dir, DP_UUID(pool_uuid),
		 VOS_FILE, 0);
	assert_int_equal(stat(log_file, &log_stat), 0);
	Dcw.magic = ~DLCK_CHECKER_WORKER_MAGIC;
	expect_assert_failure(dlck_checker_worker_fini(ck));
	Dcw.magic = DLCK_CHECKER_WORKER_MAGIC;
	dlck_checker_worker_fini(ck);
	assert_ptr_equal(last_freed_payload, &Dcw);
	assert_int_equal(unlink(log_file), 0);
	assert_int_equal(rmdir(log_dir), 0);
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
	{"DLCK_CHECKER_WORKER_104: init - allocation failure with main checker",
	 test_worker_init_alloc_failure_with_main_checker, setup, teardown},
	{"DLCK_CHECKER_WORKER_105: init - log open failure with main checker",
	 test_worker_init_log_open_failure_with_main_checker, setup, teardown},
	{"DLCK_CHECKER_WORKER_106: callbacks - success", test_worker_callbacks_success, setup, teardown},
	{"DLCK_CHECKER_WORKER_107: get_custom - invalid magic", test_worker_get_custom_invalid_magic, setup,
	 teardown},
};

int
main(void)
{
	d_register_alt_assert(mock_assert);
	return cmocka_run_group_tests_name("dlck_checker_worker_ut", dlck_checker_worker_tests, NULL,
					   NULL);
}
