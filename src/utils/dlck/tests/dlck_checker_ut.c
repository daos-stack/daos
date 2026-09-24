/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
 * (C) Copyright 2026 Google LLC
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
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <cmocka.h>

#include <abt.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/checker.h>
#include <daos_srv/mgmt_tgt_common.h>

#include "../dlck_checker.h"

static int abt_mutex_create_rc;
static int abt_mutex_free_rc;
static int abt_mutex_lock_rc;
static int abt_mutex_unlock_rc;
static int mock_fopen_errno;
static int mock_d_asprintf2_failure;
static int mock_d_calloc_failure;
static int mock_vfprintf_enabled;
static int mock_vfprintf_rc;
static int mock_fflush_enabled;
static int mock_fflush_errno;
static ABT_mutex *abt_mutex_create_arg;
static ABT_mutex *abt_mutex_free_arg;
static ABT_mutex abt_mutex_lock_arg;
static ABT_mutex abt_mutex_unlock_arg;
static ABT_mutex mutex_handle;

FILE *__real_fopen(const char *path, const char *mode);
int __real_vfprintf(FILE *stream, const char *fmt, va_list args);
int __real_fflush(FILE *stream);
void *__real_d_calloc(size_t nmemb, size_t size);

void *
__wrap_d_calloc(size_t nmemb, size_t size)
{
	if (mock_d_calloc_failure != 0)
		return NULL;

	return __real_d_calloc(nmemb, size);
}

int
__wrap_vfprintf(FILE *stream, const char *fmt, va_list args)
{
	if (mock_vfprintf_enabled != 0) {
		(void)stream;
		(void)fmt;
		(void)args;
		if (mock_vfprintf_rc < 0)
			errno = EIO;
		return mock_vfprintf_rc;
	}

	return __real_vfprintf(stream, fmt, args);
}

int
__wrap_fflush(FILE *stream)
{
	if (mock_fflush_enabled != 0) {
		(void)stream;
		if (mock_fflush_errno != 0) {
			errno = mock_fflush_errno;
			return EOF;
		}
		return 0;
	}

	return __real_fflush(stream);
}

char *
__wrap_d_asprintf2(int *rc, const char *fmt, ...)
{
	char   *buf = NULL;
	va_list args;

	if (mock_d_asprintf2_failure != 0) {
		*rc = -1;
		return NULL;
	}

	va_start(args, fmt);
	*rc = vasprintf(&buf, fmt, args);
	va_end(args);
	if (*rc == -1)
		buf = NULL;

	return buf;
}

FILE *
__wrap_fopen(const char *path, const char *mode)
{
	if (mock_fopen_errno != 0) {
		(void)path;
		(void)mode;
		errno = mock_fopen_errno;
		mock_fopen_errno = 0;
		return NULL;
	}

	return __real_fopen(path, mode);
}

int
__wrap_ABT_mutex_create(ABT_mutex *newmutex)
{
	abt_mutex_create_arg = newmutex;
	if (abt_mutex_create_rc == ABT_SUCCESS)
		*newmutex = mutex_handle;

	return abt_mutex_create_rc;
}

int
__wrap_ABT_mutex_free(ABT_mutex *mutex)
{
	abt_mutex_free_arg = mutex;
	return abt_mutex_free_rc;
}

int
__wrap_ABT_mutex_lock(ABT_mutex mutex)
{
	abt_mutex_lock_arg = mutex;
	return abt_mutex_lock_rc;
}

int
__wrap_ABT_mutex_unlock(ABT_mutex mutex)
{
	abt_mutex_unlock_arg = mutex;
	return abt_mutex_unlock_rc;
}

static void
test_setup(void)
{
	abt_mutex_create_rc = ABT_SUCCESS;
	abt_mutex_free_rc = ABT_SUCCESS;
	abt_mutex_lock_rc = ABT_SUCCESS;
	abt_mutex_unlock_rc = ABT_SUCCESS;
	mock_fopen_errno = 0;
	mock_d_asprintf2_failure = 0;
	mock_d_calloc_failure = 0;
	mock_vfprintf_enabled = 0;
	mock_vfprintf_rc = 0;
	mock_fflush_enabled = 0;
	mock_fflush_errno = 0;
	abt_mutex_create_arg = NULL;
	abt_mutex_free_arg = NULL;
	abt_mutex_lock_arg = ABT_MUTEX_NULL;
	abt_mutex_unlock_arg = ABT_MUTEX_NULL;
	mutex_handle = (ABT_mutex)(uintptr_t)0x1234;
}

static void
init_checker(struct checker *ck)
{
	assert_int_equal(dlck_checker_main_init(ck), DER_SUCCESS);
	assert_non_null(ck->ck_private);
}

/* main init: success initializes the checker and its callbacks. */
static void
test_main_init_success(void **state)
{
	struct checker *ck = *state;
	struct dlck_checker_main *dcm;
	ABT_mutex *mutex_ptr;

	assert_int_equal(dlck_checker_main_init(ck), DER_SUCCESS);
	assert_non_null(ck->ck_private);
	assert_ptr_equal(ck->ck_private, dlck_checker_main_get_custom(ck));

	dcm = ck->ck_private;
	mutex_ptr = &dcm->stream_mutex;
	assert_ptr_equal(abt_mutex_create_arg, &dcm->stream_mutex);
	assert_ptr_equal(dcm->stream_mutex, mutex_handle);
	assert_int_equal(dcm->core.magic, DLCK_CHECKER_MAIN_MAGIC);
	assert_ptr_equal(dcm->core.stream, stdout);
	assert_non_null(ck->ck_vprintf);
	assert_non_null(ck->ck_indent_set);
	assert_ptr_equal(ck->ck_prefix, dcm->core.prefix);
	assert_ptr_equal(abt_mutex_free_arg, NULL);

	ck->ck_level = 2;
	assert_int_equal(ck->ck_indent_set(ck), DER_SUCCESS);
	assert_string_equal(ck->ck_prefix, "-- ");
	assert_int_equal(ck_common_printf(ck, ""), DER_SUCCESS);
	assert_ptr_equal(abt_mutex_lock_arg, mutex_handle);
	assert_ptr_equal(abt_mutex_unlock_arg, mutex_handle);

	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
	assert_ptr_equal(abt_mutex_free_arg, mutex_ptr);
}

/* main init: mutex creation returns an Argobots error. */
static void
test_main_init_mutex_create_failure(void **state)
{
	struct checker *ck = *state;

	abt_mutex_create_rc = ABT_ERR_OTHER;
	assert_int_not_equal(dlck_checker_main_init(ck), DER_SUCCESS);
	assert_non_null(abt_mutex_create_arg);
	assert_null(ck->ck_private);
}

/* main vprintf: positive vfprintf result and successful flush. */
static void
test_vprintf_vfprintf_positive(void **state)
{
	struct checker *ck = *state;

	init_checker(ck);
	mock_vfprintf_enabled = 1;
	mock_vfprintf_rc = 1;
	mock_fflush_enabled = 1;
	mock_fflush_errno = 0;
	assert_int_equal(ck_common_printf(ck, "test"), DER_SUCCESS);
	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
}

/* main vprintf: vfprintf returns an I/O error. */
static void
test_vprintf_vfprintf_failure(void **state)
{
	struct checker *ck = *state;

	init_checker(ck);
	mock_vfprintf_enabled = 1;
	mock_vfprintf_rc = -1;
	assert_int_equal(ck_common_printf(ck, "test"), daos_errno2der(EIO));
	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
}

/* main vprintf: fflush returns EOF with errno set. */
static void
test_vprintf_fflush_failure(void **state)
{
	struct checker *ck = *state;

	init_checker(ck);
	mock_vfprintf_enabled = 1;
	mock_vfprintf_rc = 1;
	mock_fflush_enabled = 1;
	mock_fflush_errno = EIO;
	assert_int_equal(ck_common_printf(ck, "test"), daos_errno2der(EIO));
	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
}

/* main vprintf: mutex lock returns an Argobots error. */
static void
test_main_vprintf_lock_failure(void **state)
{
	struct checker *ck = *state;

	init_checker(ck);
	abt_mutex_lock_rc = ABT_ERR_OTHER;
	assert_int_equal(ck_common_printf(ck, "test"), dss_abterr2der(ABT_ERR_OTHER));
	assert_ptr_equal(abt_mutex_lock_arg, mutex_handle);
	assert_ptr_equal(abt_mutex_unlock_arg, ABT_MUTEX_NULL);
	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
}

/* main vprintf: mutex unlock returns an Argobots error. */
static void
test_main_vprintf_unlock_failure(void **state)
{
	struct checker *ck = *state;

	init_checker(ck);
	mock_vfprintf_enabled = 1;
	mock_vfprintf_rc = 1;
	mock_fflush_enabled = 1;
	abt_mutex_unlock_rc = ABT_ERR_OTHER;
	assert_int_equal(ck_common_printf(ck, "test"), dss_abterr2der(ABT_ERR_OTHER));
	assert_ptr_equal(abt_mutex_unlock_arg, mutex_handle);
	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
}

/* main fini: successful mutex destruction clears checker state. */
static void
test_main_fini_success(void **state)
{
	struct checker *ck = *state;
	ABT_mutex *mutex_ptr;

	init_checker(ck);
	mutex_ptr = &((struct dlck_checker_main *)ck->ck_private)->stream_mutex;

	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
	assert_ptr_equal(abt_mutex_free_arg, mutex_ptr);
	assert_null(ck->ck_private);
	assert_null(ck->ck_vprintf);
	assert_null(ck->ck_indent_set);
	assert_null(ck->ck_prefix);
}

/* main custom payload: invalid magic triggers an assertion. */
static void
test_main_invalid_magic(void **state)
{
	struct checker *ck = *state;
	struct dlck_checker_main *dcm;

	assert_int_equal(dlck_checker_main_init(ck), DER_SUCCESS);
	assert_non_null(ck->ck_private);
	dcm = ck->ck_private;
	assert_non_null(dcm);
	dcm->core.magic = ~DLCK_CHECKER_MAIN_MAGIC;
	expect_assert_failure(dlck_checker_main_get_custom(ck));
	dcm->core.magic = DLCK_CHECKER_MAIN_MAGIC;
	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
}

/* main fini: mutex destruction returns an Argobots error. */
static void
test_main_fini_mutex_free_failure(void **state)
{
	struct checker *ck = *state;
	ABT_mutex *mutex_ptr;
	int         expected_rc;

	init_checker(ck);
	mutex_ptr = &((struct dlck_checker_main *)ck->ck_private)->stream_mutex;
	abt_mutex_free_rc = ABT_ERR_OTHER;
	expected_rc = dss_abterr2der(abt_mutex_free_rc);

	assert_int_equal(dlck_checker_main_fini(ck), expected_rc);
	assert_ptr_equal(abt_mutex_free_arg, mutex_ptr);
	assert_null(ck->ck_private);
}

/* main init: dcm allocation returns NULL. */
static void
test_main_init_alloc_failure(void **state)
{
	struct checker *ck = *state;

	mock_d_calloc_failure = 1;
	assert_int_equal(dlck_checker_main_init(ck), -DER_NOMEM);
	assert_null(ck->ck_private);
}

/* worker init: valid options create logfile and callbacks. */
static void
test_worker_init_success(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {.cko_non_zero_padding = CHECKER_EVENT_WARNING};
	struct dlck_checker_worker *dcw;
	uuid_t                   pool_uuid;
	static char              log_dir[] = "/tmp/dlck-worker-XXXXXX";
	static char              log_file[PATH_MAX];
	struct stat              log_stat;

	assert_non_null(mkdtemp(log_dir));
	uuid_generate(pool_uuid);
	assert_int_equal(dlck_checker_worker_init(&options, log_dir, pool_uuid, 3, NULL, ck),
			 DER_SUCCESS);

	dcw = ck->ck_private;
	assert_non_null(dcw);
	assert_int_equal(dcw->magic, DLCK_CHECKER_WORKER_MAGIC);
	assert_non_null(dcw->stream);
	assert_non_null(ck->ck_vprintf);
	assert_non_null(ck->ck_indent_set);
	assert_ptr_equal(ck->ck_prefix, dcw->prefix);
	assert_int_equal(ck->ck_options.cko_non_zero_padding, options.cko_non_zero_padding);

	snprintf(log_file, sizeof(log_file), "%s/" DF_UUIDF "_%s%d", log_dir, DP_UUID(pool_uuid),
		 VOS_FILE, 3);
	assert_int_equal(stat(log_file, &log_stat), 0);

	dlck_checker_worker_fini(ck);
	assert_null(ck->ck_private);
	assert_int_equal(unlink(log_file), 0);
	assert_int_equal(rmdir(log_dir), 0);
}

/* worker init: fopen returns an I/O error. */
static void
test_worker_init_log_open_failure(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	uuid_t                   pool_uuid;

	uuid_generate(pool_uuid);
	mock_fopen_errno = EIO;
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, NULL, ck),
				 daos_errno2der(mock_fopen_errno));
	assert_null(ck->ck_private);
}

/* worker init: fopen failure is reported through the main checker. */
static void
test_worker_init_log_open_failure_with_main_checker(void **state)
{
	struct checker         *ck = *state;
	struct checker          main_ck = {0};
	struct checker_options  options = {0};
	uuid_t                   pool_uuid;

	init_checker(&main_ck);
	uuid_generate(pool_uuid);
	mock_fopen_errno = EIO;
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, &main_ck, ck),
				 daos_errno2der(EIO));
	assert_null(ck->ck_private);
	assert_int_equal(dlck_checker_main_fini(&main_ck), DER_SUCCESS);
}

/* worker init: logfile path allocation returns NULL. */
static void
test_worker_init_log_path_alloc_failure(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	uuid_t                   pool_uuid;

	uuid_generate(pool_uuid);
	mock_d_asprintf2_failure = 1;
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, NULL, ck),
				 -DER_NOMEM);
	assert_null(ck->ck_private);
}

/* worker init: allocation failure is reported through the main checker. */
static void
test_worker_init_alloc_failure_with_main_checker(void **state)
{
	struct checker         *ck = *state;
	struct checker          main_ck = {0};
	struct checker_options  options = {0};
	uuid_t                   pool_uuid;

	init_checker(&main_ck);
	uuid_generate(pool_uuid);
	mock_d_calloc_failure = 1;
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, &main_ck, ck),
				 -DER_NOMEM);
	assert_null(ck->ck_private);
	assert_int_equal(dlck_checker_main_fini(&main_ck), DER_SUCCESS);
}

/* worker init: dcw allocation returns NULL. */
static void
test_worker_init_alloc_failure(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	uuid_t                   pool_uuid;

	uuid_generate(pool_uuid);
	mock_d_calloc_failure = 1;
	assert_int_equal(dlck_checker_worker_init(&options, "/tmp", pool_uuid, 0, NULL, ck),
				 -DER_NOMEM);
	assert_null(ck->ck_private);
}

/* worker callbacks: indent and vprintf use the initialized worker payload. */
static void
test_worker_callbacks_success(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	uuid_t                   pool_uuid;
	static char              log_dir[] = "/tmp/dlck-worker-callback-XXXXXX";
	static char              log_file[PATH_MAX];
	struct stat              log_stat;

	uuid_generate(pool_uuid);
	assert_non_null(mkdtemp(log_dir));
	assert_int_equal(dlck_checker_worker_init(&options, log_dir, pool_uuid, 0, NULL, ck),
				 DER_SUCCESS);
	ck->ck_level = 2;
	assert_int_equal(ck->ck_indent_set(ck), DER_SUCCESS);
	assert_string_equal(ck->ck_prefix, "-- ");
	assert_int_equal(ck_common_printf(ck, "worker"), DER_SUCCESS);

	snprintf(log_file, sizeof(log_file), "%s/" DF_UUIDF "_%s%d", log_dir, DP_UUID(pool_uuid),
		 VOS_FILE, 0);
	dlck_checker_worker_fini(ck);
	assert_int_equal(stat(log_file, &log_stat), 0);
	assert_int_equal(unlink(log_file), 0);
	assert_int_equal(rmdir(log_dir), 0);
}

/* worker custom payload: invalid magic triggers an assertion. */
static void
test_worker_invalid_magic(void **state)
{
	struct checker         *ck = *state;
	struct checker_options  options = {0};
	uuid_t                   pool_uuid;
	struct dlck_checker_worker *dcw;
	static char              log_dir[] = "/tmp/dlck-worker-magic-XXXXXX";
	static char              log_file[PATH_MAX];
	struct stat              log_stat;

	uuid_generate(pool_uuid);
	assert_non_null(mkdtemp(log_dir));
	assert_int_equal(dlck_checker_worker_init(&options, log_dir, pool_uuid, 0, NULL, ck),
				 DER_SUCCESS);
	dcw = ck->ck_private;
	snprintf(log_file, sizeof(log_file), "%s/" DF_UUIDF "_%s%d", log_dir, DP_UUID(pool_uuid),
		 VOS_FILE, 0);
	assert_int_equal(stat(log_file, &log_stat), 0);
	dcw->magic = ~DLCK_CHECKER_WORKER_MAGIC;
	expect_assert_failure(dlck_checker_worker_fini(ck));
	dcw->magic = DLCK_CHECKER_WORKER_MAGIC;
	dlck_checker_worker_fini(ck);
	assert_int_equal(unlink(log_file), 0);
	assert_int_equal(rmdir(log_dir), 0);
}

/* main indent callback: every valid level creates the expected shape. */
static void
test_main_indent_set_levels_zero_to_max(void **state)
{
	struct checker *ck = *state;

	init_checker(ck);
	for (ck->ck_level = 0; ck->ck_level <= CHECKER_INDENT_MAX; ck->ck_level++) {
		assert_int_equal(ck->ck_indent_set(ck), DER_SUCCESS);
		if (ck->ck_level == 0) {
			assert_string_equal(ck->ck_prefix, "");
			continue;
		}

		assert_int_equal(strlen(ck->ck_prefix), ck->ck_level + 1);
		assert_int_equal(ck->ck_prefix[0], DLCK_PRINT_INDENT);
		assert_int_equal(ck->ck_prefix[ck->ck_level - 1], DLCK_PRINT_INDENT);
		assert_int_equal(ck->ck_prefix[ck->ck_level], ' ');
	}

	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
}

/* main and indent helpers: invalid levels trigger assertions. */
static void
test_main_indent_set_out_of_range(void **state)
{
	struct checker *ck = *state;

	init_checker(ck);
	ck->ck_level = -1;
	expect_assert_failure(ck->ck_indent_set(ck));
	ck->ck_level = -1;
	expect_assert_failure(checker_print_indent_dec(ck));

	ck->ck_level = CHECKER_INDENT_MAX + 1;
	expect_assert_failure(ck->ck_indent_set(ck));
	ck->ck_level = CHECKER_INDENT_MAX + 1;
	expect_assert_failure(checker_print_indent_inc(ck));
	
	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
}

static int
setup(void **state)
{
	*state = calloc(1, sizeof(struct checker));
	assert_non_null(*state);
	test_setup();
	return 0;
}

static int
teardown(void **state)
{
	free(*state);
	return 0;
}

static const struct CMUnitTest dlck_checker_tests[] = {
	{"DLCK_CHECKER_MAIN_100: init - success", test_main_init_success, setup, teardown},
	{"DLCK_CHECKER_MAIN_101: init - mutex create failure", test_main_init_mutex_create_failure, setup, teardown},
	{"DLCK_CHECKER_MAIN_111: init - allocation failure", test_main_init_alloc_failure, setup, teardown},
	{"DLCK_CHECKER_MAIN_107: vprintf - vfprintf positive", test_vprintf_vfprintf_positive, setup, teardown},
	{"DLCK_CHECKER_MAIN_108: vprintf - vfprintf failure", test_vprintf_vfprintf_failure, setup, teardown},
	{"DLCK_CHECKER_MAIN_109: vprintf - fflush failure", test_vprintf_fflush_failure, setup, teardown},
	{"DLCK_CHECKER_MAIN_112: vprintf - lock failure", test_main_vprintf_lock_failure, setup, teardown},
	{"DLCK_CHECKER_MAIN_113: vprintf - unlock failure", test_main_vprintf_unlock_failure, setup, teardown},
	{"DLCK_CHECKER_MAIN_104: fini - success", test_main_fini_success, setup, teardown},
	{"DLCK_CHECKER_MAIN_105: fini - mutex free failure", test_main_fini_mutex_free_failure, setup, teardown},
	{"DLCK_CHECKER_WORKER_100: init - success", test_worker_init_success, setup, teardown},
	{"DLCK_CHECKER_WORKER_101: init - log open failure", test_worker_init_log_open_failure, setup, teardown},
	{"DLCK_CHECKER_WORKER_104: init - log open failure with main checker",
	 test_worker_init_log_open_failure_with_main_checker, setup, teardown},
	{"DLCK_CHECKER_WORKER_102: init - log path allocation failure", test_worker_init_log_path_alloc_failure, setup,
	 teardown},
	{"DLCK_CHECKER_WORKER_107: init - allocation failure", test_worker_init_alloc_failure, setup, teardown},
	{"DLCK_CHECKER_WORKER_105: init - allocation failure with main checker",
	 test_worker_init_alloc_failure_with_main_checker, setup, teardown},
	{"DLCK_CHECKER_WORKER_106: callbacks - success", test_worker_callbacks_success, setup, teardown},
	{"DLCK_CHECKER_WORKER_103: invalid magic", test_worker_invalid_magic, setup, teardown},
	{"DLCK_CHECKER_MAIN_106: invalid magic", test_main_invalid_magic, setup, teardown},
	{"DLCK_CHECKER_MAIN_102: indent - levels zero to max", test_main_indent_set_levels_zero_to_max, setup,
	 teardown},
	{"DLCK_CHECKER_MAIN_103: indent - out of range", test_main_indent_set_out_of_range, setup, teardown},
};

int
main(void)
{
	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("dlck_checker_ut", dlck_checker_tests, NULL, NULL);
}
