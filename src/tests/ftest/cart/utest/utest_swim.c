/*
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT testing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <time.h>

#include <cmocka.h>

#include <cart/api.h>
#include "../cart/crt_internal.h"

static void
test_swim(void **state)
{
	int rc;

	rc = crt_init(NULL, CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	assert_int_equal(rc, 0);

	rc = crt_swim_init(0);
	assert_int_equal(rc, 0);

	rc = crt_rank_self_set(0, 1 /* group_version_min */);
	assert_int_equal(rc, 0);

	rc = crt_swim_rank_add(crt_grp_pub2priv(NULL), 1, d_hlc_get());
	assert_int_equal(rc, 0);

	rc = crt_swim_rank_add(crt_grp_pub2priv(NULL), 2, d_hlc_get());
	assert_int_equal(rc, 0);

	rc = crt_swim_rank_add(crt_grp_pub2priv(NULL), 1, d_hlc_get());
	assert_int_equal(rc, -DER_ALREADY);

	rc = crt_swim_rank_add(crt_grp_pub2priv(NULL), 0, d_hlc_get());
	assert_int_equal(rc, -DER_ALREADY);

	crt_swim_fini();
	rc = crt_finalize();
	assert_int_equal(rc, 0);
}

static int
init_tests(void **state)
{
	unsigned int seed;

	/* Seed the random number generator once per test run */
	seed = (unsigned int)(time(NULL) & 0x0FFFFFFFFULL);
	fprintf(stdout, "Seeding this test run with seed=%u\n", seed);
	srand(seed);

	setenv("CRT_PHY_ADDR_STR", "ofi+tcp", 1);
	setenv("OFI_INTERFACE", "lo", 1);

	return 0;
}

static int
fini_tests(void **state)
{
	return 0;
}

int main(int argc, char **argv)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_swim),
	};

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("utest_swim", tests, init_tests,
		fini_tests);
}
