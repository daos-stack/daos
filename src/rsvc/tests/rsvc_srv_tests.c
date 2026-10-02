/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * Unit tests for the replicated service server helpers of daos_srv/rsvc.h
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos/tests_lib.h>
#include <daos_srv/rsvc.h>

#define GIB ((uint64_t)1 << 30)

static void
test_create_timeout_by_size_tiers(void **state)
{
	const struct {
		uint64_t size;
		uint32_t timeout;
	} cases[] = {
	    {0, 15},          {1, 15},
	    {GIB, 15},        {32 * GIB - 1, 15},
	    {32 * GIB, 30},   {64 * GIB - 1, 30},
	    {64 * GIB, 60},   {128 * GIB - 1, 60},
	    {128 * GIB, 90},  {1024 * GIB, 90},
	    {UINT64_MAX, 90},
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		print_message("size=" DF_U64 " expected timeout=%u s\n", cases[i].size,
			      cases[i].timeout);
		assert_int_equal(ds_rsvc_create_timeout_by_size(cases[i].size), cases[i].timeout);
	}
}

static void
test_create_timeout_by_size_min(void **state)
{
	uint64_t size;

	assert_int_equal(ds_rsvc_create_timeout_by_size(0), DS_RSVC_CREATE_TIMEOUT_MIN);

	/* The timeout is a non-decreasing function of the size, never below the minimum. */
	for (size = 1; size != 0; size <<= 1) {
		uint32_t timeout = ds_rsvc_create_timeout_by_size(size);

		assert_true(timeout >= DS_RSVC_CREATE_TIMEOUT_MIN);
		assert_true(timeout >= ds_rsvc_create_timeout_by_size(size - 1));
	}
}

int
main(int argc, char **argv)
{
	const struct CMUnitTest tests[] = {
	    cmocka_unit_test(test_create_timeout_by_size_tiers),
	    cmocka_unit_test(test_create_timeout_by_size_min),
	};

	return cmocka_run_group_tests_name("rsvc_srv", tests, NULL, NULL);
}
