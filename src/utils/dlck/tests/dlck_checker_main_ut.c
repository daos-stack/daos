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

static struct checker main_checker_state;

static int
setup(void **state)
{
	*state = &main_checker_state;
	mock_vfprintf_check_output = 0;
	return 0;
}

static int
setup_main_checker(void **state)
{
	struct checker *ck;
	int             rc;

	rc = setup(state);
	if (rc != 0)
		return rc;

	ck = *state;
	EXPECT_CHECKER_D_CALLOC(Dcm);
	expect_value(__wrap_ABT_mutex_create, newmutex, &Dcm.stream_mutex);
	will_return(__wrap_ABT_mutex_create, ABT_SUCCESS);
	assert_int_equal(dlck_checker_main_init(ck), DER_SUCCESS);
	assert_non_null(ck->ck_private);
	return 0;
}

static int
teardown(void **state)
{
	(void)state;
	return 0;
}

static int
teardown_main_checker(void **state)
{
	struct checker  ck_zeroed;
	struct checker *ck = *state;

	memset(&ck_zeroed, 0, sizeof(ck_zeroed));
	expect_value(__wrap_ABT_mutex_free, mutex, &Dcm.stream_mutex);
	will_return(__wrap_ABT_mutex_free, ABT_SUCCESS);
	expect_value(__wrap_d_free, ptr, &Dcm);
	assert_int_equal(dlck_checker_main_fini(ck), DER_SUCCESS);
	assert_memory_equal(ck, &ck_zeroed, sizeof(*ck));
	return teardown(state);
}

/* main init: success initializes the checker and its callbacks. */
static void
test_main_init_success(void **state)
{
	struct checker *ck = *state;

	/* setup_main_checker() initializes this test's checker. */
	assert_ptr_equal(ck->ck_private, dlck_checker_main_get_custom(ck));

	assert_int_equal(Dcm.core.magic, DLCK_CHECKER_MAIN_MAGIC);
	assert_ptr_equal(Dcm.core.stream, stdout);
	assert_ptr_equal(Dcm.stream_mutex, Mock_mutex_handle);
	assert_non_null(ck->ck_vprintf);
	assert_non_null(ck->ck_indent_set);
	assert_ptr_equal(ck->ck_prefix, Dcm.core.prefix);
	/* teardown_main_checker() finalizes the checker and verifies cleanup. */
}

/* main init: mutex creation returns an Argobots error. */
static void
test_main_init_mutex_create_failure(void **state)
{
	struct checker *ck = *state;

	EXPECT_CHECKER_D_CALLOC(Dcm);
	expect_value(__wrap_ABT_mutex_create, newmutex, &Dcm.stream_mutex);
	will_return(__wrap_ABT_mutex_create, ABT_ERR_OTHER);
	expect_value(__wrap_d_free, ptr, &Dcm);
	assert_int_equal(dlck_checker_main_init(ck), dss_abterr2der(ABT_ERR_OTHER));
	assert_null(ck->ck_private);
}

/* main vprintf: positive vfprintf result and successful flush. */
static void
test_vprintf_vfprintf_positive(void **state)
{
	struct checker *ck = *state;

	mock_vfprintf_check_output = 1;
	expect_value(__wrap_vfprintf, stream, stdout);
	expect_string(__wrap_vfprintf, fmt, "main %d: %s");
	expect_string(__wrap_vfprintf, output, "main 42: ready");
	will_return(__wrap_vfprintf, 1);
	expect_value(__wrap_fflush, stream, stdout);
	will_return(__wrap_fflush, 0);
	expect_value(__wrap_ABT_mutex_lock, mutex, Mock_mutex_handle);
	will_return(__wrap_ABT_mutex_lock, ABT_SUCCESS);
	expect_value(__wrap_ABT_mutex_unlock, mutex, Mock_mutex_handle);
	will_return(__wrap_ABT_mutex_unlock, ABT_SUCCESS);
	assert_int_equal(ck_common_printf(ck, "main %d: %s", 42, "ready"), DER_SUCCESS);
}

/* main vprintf: vfprintf returns an I/O error. */
static void
test_vprintf_vfprintf_failure(void **state)
{
	struct checker *ck = *state;

	expect_value(__wrap_vfprintf, stream, stdout);
	expect_string(__wrap_vfprintf, fmt, "test");
	will_return(__wrap_vfprintf, -1);
	expect_value(__wrap_ABT_mutex_lock, mutex, Mock_mutex_handle);
	will_return(__wrap_ABT_mutex_lock, ABT_SUCCESS);
	expect_value(__wrap_ABT_mutex_unlock, mutex, Mock_mutex_handle);
	will_return(__wrap_ABT_mutex_unlock, ABT_SUCCESS);
	assert_int_equal(ck_common_printf(ck, "test"), daos_errno2der(EIO));
}

/* main vprintf: fflush returns EOF with errno set. */
static void
test_vprintf_fflush_failure(void **state)
{
	struct checker *ck = *state;

	expect_value(__wrap_vfprintf, stream, stdout);
	expect_string(__wrap_vfprintf, fmt, "test");
	will_return(__wrap_vfprintf, 1);
	expect_value(__wrap_fflush, stream, stdout);
	will_return(__wrap_fflush, EIO);
	expect_value(__wrap_ABT_mutex_lock, mutex, Mock_mutex_handle);
	will_return(__wrap_ABT_mutex_lock, ABT_SUCCESS);
	expect_value(__wrap_ABT_mutex_unlock, mutex, Mock_mutex_handle);
	will_return(__wrap_ABT_mutex_unlock, ABT_SUCCESS);
	assert_int_equal(ck_common_printf(ck, "test"), daos_errno2der(EIO));
}

/* main vprintf: mutex lock returns an Argobots error. */
static void
test_main_vprintf_lock_failure(void **state)
{
	struct checker *ck = *state;

	expect_value(__wrap_ABT_mutex_lock, mutex, Mock_mutex_handle);
	will_return(__wrap_ABT_mutex_lock, ABT_ERR_OTHER);
	assert_int_equal(ck_common_printf(ck, "test"), dss_abterr2der(ABT_ERR_OTHER));
}

/* main vprintf: mutex unlock returns an Argobots error. */
static void
test_main_vprintf_unlock_failure(void **state)
{
	struct checker *ck = *state;

	expect_value(__wrap_vfprintf, stream, stdout);
	expect_string(__wrap_vfprintf, fmt, "test");
	will_return(__wrap_vfprintf, 1);
	expect_value(__wrap_fflush, stream, stdout);
	will_return(__wrap_fflush, 0);
	expect_value(__wrap_ABT_mutex_lock, mutex, Mock_mutex_handle);
	will_return(__wrap_ABT_mutex_lock, ABT_SUCCESS);
	expect_value(__wrap_ABT_mutex_unlock, mutex, Mock_mutex_handle);
	will_return(__wrap_ABT_mutex_unlock, ABT_ERR_OTHER);
	assert_int_equal(ck_common_printf(ck, "test"), dss_abterr2der(ABT_ERR_OTHER));
}

/* main fini: successful mutex destruction clears checker state. */
static void
test_main_fini_success(void **state)
{
	struct checker *ck = *state;

	assert_ptr_equal(ck->ck_private, &Dcm);
	assert_ptr_equal(Dcm.stream_mutex, Mock_mutex_handle);
}

/* main get_custom: invalid magic triggers an assertion. */
static void
test_main_get_custom_invalid_magic(void **state)
{
	struct checker *ck = *state;

	Dcm.core.magic = ~DLCK_CHECKER_MAIN_MAGIC;
	expect_assert_failure(dlck_checker_main_get_custom(ck));
	Dcm.core.magic = DLCK_CHECKER_MAIN_MAGIC;
}

/* main fini: mutex destruction returns an Argobots error. */
static void
test_main_fini_mutex_free_failure(void **state)
{
	struct checker  ck_zeroed;
	struct checker *ck = *state;

	memset(&ck_zeroed, 0, sizeof(ck_zeroed));
	expect_value(__wrap_ABT_mutex_free, mutex, &Dcm.stream_mutex);
	will_return(__wrap_ABT_mutex_free, ABT_ERR_OTHER);
	expect_value(__wrap_d_free, ptr, &Dcm);
	assert_int_equal(dlck_checker_main_fini(ck), dss_abterr2der(ABT_ERR_OTHER));
	assert_ptr_equal(Dcm.stream_mutex, Mock_mutex_handle);
	assert_memory_equal(ck, &ck_zeroed, sizeof(*ck));
}

/* main init: dcm allocation returns NULL. */
static void
test_main_init_alloc_failure(void **state)
{
	struct checker *ck = *state;

	expect_checker_d_calloc(sizeof(Dcm), NULL);
	assert_int_equal(dlck_checker_main_init(ck), -DER_NOMEM);
	assert_null(ck->ck_private);
}

/* main indent callback: every valid level creates the expected shape. */
static void
test_main_indent_set_levels_zero_to_max(void **state)
{
	struct checker *ck = *state;
	int             indent_index;

	for (ck->ck_level = 0; ck->ck_level <= CHECKER_INDENT_MAX;
	     ck->ck_level++) {
		assert_int_equal(ck->ck_indent_set(ck), DER_SUCCESS);
		if (ck->ck_level == 0) {
			assert_string_equal(ck->ck_prefix, "");
			continue;
		}

		assert_int_equal(strlen(ck->ck_prefix), ck->ck_level + 1);
		for (indent_index = 0; indent_index < ck->ck_level;
		     indent_index++)
			assert_int_equal(ck->ck_prefix[indent_index],
					 DLCK_PRINT_INDENT);
		assert_int_equal(ck->ck_prefix[ck->ck_level], ' ');
	}

}

/* main and indent helpers: invalid levels trigger assertions. */
static void
test_main_indent_set_out_of_range(void **state)
{
	struct checker *ck = *state;

	ck->ck_level = -1;
	expect_assert_failure(ck->ck_indent_set(ck));
	ck->ck_level = -1;
	expect_assert_failure(checker_print_indent_dec(ck));

	ck->ck_level = CHECKER_INDENT_MAX + 1;
	expect_assert_failure(ck->ck_indent_set(ck));
	ck->ck_level = CHECKER_INDENT_MAX + 1;
	expect_assert_failure(checker_print_indent_inc(ck));
}

/* main indent callback: invalid magic triggers an assertion. */
static void
test_main_indent_set_invalid_magic(void **state)
{
	struct checker *ck = *state;

	Dcm.core.magic = ~DLCK_CHECKER_MAIN_MAGIC;
	expect_assert_failure(ck->ck_indent_set(ck));
	Dcm.core.magic = DLCK_CHECKER_MAIN_MAGIC;
}

static const struct CMUnitTest dlck_checker_tests[] = {
	{"DLCK_CHECKER_MAIN_101: init - success",
		test_main_init_success,
		setup_main_checker, teardown_main_checker},
	{"DLCK_CHECKER_MAIN_102: init - allocation failure",
		test_main_init_alloc_failure,
		setup, teardown},
	{"DLCK_CHECKER_MAIN_103: init - mutex create failure",
		test_main_init_mutex_create_failure,
		setup, teardown},
	{"DLCK_CHECKER_MAIN_104: fini - success",
		test_main_fini_success,
		setup_main_checker, teardown_main_checker},
	{"DLCK_CHECKER_MAIN_105: fini - mutex free failure",
		test_main_fini_mutex_free_failure,
		setup_main_checker, teardown},
	{"DLCK_CHECKER_MAIN_106: get_custom - invalid magic",
		test_main_get_custom_invalid_magic,
		setup_main_checker, teardown_main_checker},
	{"DLCK_CHECKER_MAIN_107: indent - levels zero to max",
		test_main_indent_set_levels_zero_to_max,
		setup_main_checker, teardown_main_checker},
	{"DLCK_CHECKER_MAIN_108: indent - out of range",
		test_main_indent_set_out_of_range,
		setup_main_checker, teardown_main_checker},
	{"DLCK_CHECKER_MAIN_109: indent - invalid magic",
		test_main_indent_set_invalid_magic,
		setup_main_checker, teardown_main_checker},
	{"DLCK_CHECKER_MAIN_110: vprintf - vfprintf positive",
		test_vprintf_vfprintf_positive,
		setup_main_checker, teardown_main_checker},
	{"DLCK_CHECKER_MAIN_111: vprintf - vfprintf failure",
		test_vprintf_vfprintf_failure,
		setup_main_checker, teardown_main_checker},
	{"DLCK_CHECKER_MAIN_112: vprintf - fflush failure",
		test_vprintf_fflush_failure,
		setup_main_checker, teardown_main_checker},
	{"DLCK_CHECKER_MAIN_113: vprintf - lock failure",
		test_main_vprintf_lock_failure,
		setup_main_checker, teardown_main_checker},
	{"DLCK_CHECKER_MAIN_114: vprintf - unlock failure",
		test_main_vprintf_unlock_failure,
		setup_main_checker, teardown_main_checker},
};

int
main(void)
{
	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("dlck_checker_main_ut", dlck_checker_tests, NULL, NULL);
}
