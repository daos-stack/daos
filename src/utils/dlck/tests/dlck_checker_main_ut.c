/**
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
#include "dlck_checker_ut_mock.h"

static void
test_setup(void)
{
	mock_vfprintf_enabled = 0;
	mock_fflush_enabled = 0;
	last_freed_payload = NULL;
}

static void
init_checker(struct checker *ck)
{
	will_return(__wrap_d_calloc, 0);
	will_return(__wrap_ABT_mutex_create, ABT_SUCCESS);
	assert_int_equal(dlck_checker_main_init(ck), DER_SUCCESS);
	assert_non_null(ck->ck_private);
}

/* main init: success initializes the checker and its callbacks. */
static void
test_main_init_success(void **state)
{
	struct checker *ck = *state;

	will_return(__wrap_d_calloc, 0);
	will_return(__wrap_ABT_mutex_create, ABT_SUCCESS);
	assert_int_equal(dlck_checker_main_init(ck), DER_SUCCESS);
	assert_ptr_equal(ck->ck_private, dlck_checker_main_get_custom(ck));

	assert_int_equal(Dcm.core.magic, DLCK_CHECKER_MAIN_MAGIC);
	assert_ptr_equal(Dcm.core.stream, stdout);
	assert_ptr_equal(Dcm.stream_mutex, Mock_mutex_handle);
	assert_non_null(ck->ck_vprintf);
	assert_non_null(ck->ck_indent_set);
	assert_ptr_equal(ck->ck_prefix, Dcm.core.prefix);

	ck->ck_level = 2;
	assert_int_equal(ck->ck_indent_set(ck), DER_SUCCESS);
	assert_string_equal(ck->ck_prefix, "-- ");
	will_return(__wrap_ABT_mutex_lock, ABT_SUCCESS);
	will_return(__wrap_ABT_mutex_unlock, ABT_SUCCESS);
	assert_int_equal(ck_common_printf(ck, ""), DER_SUCCESS);

	will_return(__wrap_ABT_mutex_free, ABT_SUCCESS);
	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
	assert_ptr_equal(Dcm.stream_mutex, ABT_MUTEX_NULL);
	assert_ptr_equal(last_freed_payload, &Dcm);
}

/* main init: mutex creation returns an Argobots error. */
static void
test_main_init_mutex_create_failure(void **state)
{
	struct checker *ck = *state;

	will_return(__wrap_d_calloc, 0);
	will_return(__wrap_ABT_mutex_create, ABT_ERR_OTHER);
	assert_int_equal(dlck_checker_main_init(ck), dss_abterr2der(ABT_ERR_OTHER));
	assert_null(ck->ck_private);
	assert_ptr_equal(last_freed_payload, &Dcm);
}

/* main vprintf: positive vfprintf result and successful flush. */
static void
test_vprintf_vfprintf_positive(void **state)
{
	struct checker *ck = *state;

	init_checker(ck);
	mock_vfprintf_enabled = 1;
	mock_fflush_enabled = 1;
	will_return(__wrap_vfprintf, 1);
	will_return(__wrap_fflush, 0);
	will_return(__wrap_ABT_mutex_lock, ABT_SUCCESS);
	will_return(__wrap_ABT_mutex_unlock, ABT_SUCCESS);
	assert_int_equal(ck_common_printf(ck, "test"), DER_SUCCESS);
	will_return(__wrap_ABT_mutex_free, ABT_SUCCESS);
	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
}

/* main vprintf: vfprintf returns an I/O error. */
static void
test_vprintf_vfprintf_failure(void **state)
{
	struct checker *ck = *state;

	init_checker(ck);
	mock_vfprintf_enabled = 1;
	will_return(__wrap_vfprintf, -1);
	will_return(__wrap_ABT_mutex_lock, ABT_SUCCESS);
	will_return(__wrap_ABT_mutex_unlock, ABT_SUCCESS);
	assert_int_equal(ck_common_printf(ck, "test"), daos_errno2der(EIO));
	will_return(__wrap_ABT_mutex_free, ABT_SUCCESS);
	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
}

/* main vprintf: fflush returns EOF with errno set. */
static void
test_vprintf_fflush_failure(void **state)
{
	struct checker *ck = *state;

	init_checker(ck);
	mock_vfprintf_enabled = 1;
	mock_fflush_enabled = 1;
	will_return(__wrap_vfprintf, 1);
	will_return(__wrap_fflush, EIO);
	will_return(__wrap_ABT_mutex_lock, ABT_SUCCESS);
	will_return(__wrap_ABT_mutex_unlock, ABT_SUCCESS);
	assert_int_equal(ck_common_printf(ck, "test"), daos_errno2der(EIO));
	will_return(__wrap_ABT_mutex_free, ABT_SUCCESS);
	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
}

/* main vprintf: mutex lock returns an Argobots error. */
static void
test_main_vprintf_lock_failure(void **state)
{
	struct checker *ck = *state;

	init_checker(ck);
	will_return(__wrap_ABT_mutex_lock, ABT_ERR_OTHER);
	assert_int_equal(ck_common_printf(ck, "test"), dss_abterr2der(ABT_ERR_OTHER));
	will_return(__wrap_ABT_mutex_free, ABT_SUCCESS);
	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
}

/* main vprintf: mutex unlock returns an Argobots error. */
static void
test_main_vprintf_unlock_failure(void **state)
{
	struct checker *ck = *state;

	init_checker(ck);
	mock_vfprintf_enabled = 1;
	mock_fflush_enabled = 1;
	will_return(__wrap_vfprintf, 1);
	will_return(__wrap_fflush, 0);
	will_return(__wrap_ABT_mutex_lock, ABT_SUCCESS);
	will_return(__wrap_ABT_mutex_unlock, ABT_ERR_OTHER);
	assert_int_equal(ck_common_printf(ck, "test"), dss_abterr2der(ABT_ERR_OTHER));
	will_return(__wrap_ABT_mutex_free, ABT_SUCCESS);
	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
}

/* main fini: successful mutex destruction clears checker state. */
static void
test_main_fini_success(void **state)
{
	struct checker *ck = *state;

	init_checker(ck);

	will_return(__wrap_ABT_mutex_free, ABT_SUCCESS);
	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
	assert_ptr_equal(Dcm.stream_mutex, ABT_MUTEX_NULL);
	assert_null(ck->ck_private);
	assert_null(ck->ck_vprintf);
	assert_null(ck->ck_indent_set);
	assert_null(ck->ck_prefix);
}

/* main get_custom: invalid magic triggers an assertion. */
static void
test_main_get_custom_invalid_magic(void **state)
{
	struct checker *ck = *state;

	Dcm.core.magic = ~DLCK_CHECKER_MAIN_MAGIC;
	expect_assert_failure(dlck_checker_main_get_custom(ck));
	Dcm.core.magic = DLCK_CHECKER_MAIN_MAGIC;
	will_return(__wrap_ABT_mutex_free, ABT_SUCCESS);
	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
	assert_ptr_equal(last_freed_payload, &Dcm);
}

/* main fini: mutex destruction returns an Argobots error. */
static void
test_main_fini_mutex_free_failure(void **state)
{
	struct checker *ck = *state;

	init_checker(ck);
	will_return(__wrap_ABT_mutex_free, ABT_ERR_OTHER);
	assert_int_equal(dlck_checker_main_fini(ck), dss_abterr2der(ABT_ERR_OTHER));
	assert_ptr_equal(Dcm.stream_mutex, Mock_mutex_handle);
	assert_ptr_equal(last_freed_payload, &Dcm);
	assert_null(ck->ck_private);
}

/* main init: dcm allocation returns NULL. */
static void
test_main_init_alloc_failure(void **state)
{
	struct checker *ck = *state;

	will_return(__wrap_d_calloc, ENOMEM);
	assert_int_equal(dlck_checker_main_init(ck), -DER_NOMEM);
	assert_null(ck->ck_private);
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

	will_return(__wrap_ABT_mutex_free, ABT_SUCCESS);
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
	will_return(__wrap_ABT_mutex_free, ABT_SUCCESS);
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
setup_main_checker(void **state)
{
	int rc = setup(state);

	if (rc != 0)
		return rc;
	init_checker(*state);
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
	{"DLCK_CHECKER_MAIN_101: init - allocation failure", test_main_init_alloc_failure, setup, teardown},
	{"DLCK_CHECKER_MAIN_102: init - mutex create failure", test_main_init_mutex_create_failure, setup, teardown},
	{"DLCK_CHECKER_MAIN_104: fini - success", test_main_fini_success, setup, teardown},
	{"DLCK_CHECKER_MAIN_105: fini - mutex free failure", test_main_fini_mutex_free_failure, setup, teardown},
	{"DLCK_CHECKER_MAIN_106: get_custom - invalid magic", test_main_get_custom_invalid_magic,
	 setup_main_checker, teardown},
	{"DLCK_CHECKER_MAIN_107: vprintf - vfprintf positive", test_vprintf_vfprintf_positive, setup, teardown},
	{"DLCK_CHECKER_MAIN_108: vprintf - vfprintf failure", test_vprintf_vfprintf_failure, setup, teardown},
	{"DLCK_CHECKER_MAIN_109: vprintf - fflush failure", test_vprintf_fflush_failure, setup, teardown},
	{"DLCK_CHECKER_MAIN_112: vprintf - lock failure", test_main_vprintf_lock_failure, setup, teardown},
	{"DLCK_CHECKER_MAIN_113: vprintf - unlock failure", test_main_vprintf_unlock_failure, setup, teardown},
	{"DLCK_CHECKER_MAIN_103: indent - levels zero to max", test_main_indent_set_levels_zero_to_max, setup,
	 teardown},
	{"DLCK_CHECKER_MAIN_103: indent - out of range", test_main_indent_set_out_of_range, setup, teardown},
};

int
main(void)
{
	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("dlck_checker_main_ut", dlck_checker_tests, NULL, NULL);
}
