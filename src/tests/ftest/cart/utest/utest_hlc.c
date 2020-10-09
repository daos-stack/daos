/*
 * (C) Copyright 2019-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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

#define COUNT 32000

static uint64_t last;

static void
test_hlc_get(void **state)
{
	uint64_t time;
	int i;

	for (i = 0; i < COUNT; i++) {
		time = crt_hlc_get();
		assert_true(last < time);
		last = time;
		if (i == 9)
			sleep(1);
	}
}

static void
test_hlc_get_msg(void **state)
{
	uint64_t time = last, time2 = last;
	int i;

	for (i = 0; i < COUNT; i++) {
		int rc;

		if (i % 5 == 1)
			time2 = time + 0x100;
		else if (i % 5 == 2)
			time2 = time - 0x100;
		else
			time2 = time + (i % 3);
		rc = crt_hlc_get_msg(time2, &time);
		assert_true(rc == 0);
		assert_true(time2 < time);
		assert_true(last < time);
		last = time;
		if (i == 9)
			sleep(1);
	}
}

static void
test_hlc_epsilon(void **state)
{
	int shift = 16;
	uint64_t mask = (1ULL << shift) - 1;
	uint64_t eps, pt1, pt2, hlc1, hlc2;

	/*
	 * Each subtest below tests these:
	 *
	 *   - Setting an epsilon shall get the value rounded up to the
	 *     internal physical resolution.
	 *
	 *   - Event 1 happens before (via out of band communication) event 2.
	 *     Event 1's physical timestamp >= event 2's due to their clock
	 *     offsets. Based on event 2's HLC timestamp, the bound of
	 *     event 1's HLC timestamp shall >= event 1's actual HLC timestamp
	 *     and <= event 1's physical timestamp rounded up.
	 */

	eps = 0;
	crt_hlc_epsilon_set(eps);
	assert_true(crt_hlc_epsilon_get() == 0);
	pt1 = (0x123ULL << shift) + 0x456;
	hlc1 = pt1 | mask;			/* max logical */
	pt2 = pt1 - eps;
	hlc2 = pt2 & ~mask;			/* min logical */
	assert_true(crt_hlc_epsilon_get_bound(hlc2) >= hlc1);
	assert_true(crt_hlc_epsilon_get_bound(hlc2) <= ((pt1 + mask) | mask));

	eps = 1;
	crt_hlc_epsilon_set(eps);
	assert_true(crt_hlc_epsilon_get() == 1ULL << shift);
	pt1 = 0x123ULL << shift;
	hlc1 = pt1 | mask;			/* max logical */
	pt2 = pt1 - eps;
	hlc2 = pt2 & ~mask;			/* min logical */
	assert_true(crt_hlc_epsilon_get_bound(hlc2) >= hlc1);
	assert_true(crt_hlc_epsilon_get_bound(hlc2) <= ((pt1 + mask) | mask));

	eps = 1ULL << shift;
	crt_hlc_epsilon_set(eps);
	assert_true(crt_hlc_epsilon_get() == 1ULL << shift);
	pt1 = (0x123ULL << shift) + 0x456;
	hlc1 = pt1 | mask;			/* max logical */
	pt2 = pt1 - eps;
	hlc2 = pt2 & ~mask;			/* min logical */
	assert_true(crt_hlc_epsilon_get_bound(hlc2) >= hlc1);
	assert_true(crt_hlc_epsilon_get_bound(hlc2) <= ((pt1 + mask) | mask));

	eps = (1ULL << shift) + 1;
	crt_hlc_epsilon_set(eps);
	assert_true(crt_hlc_epsilon_get() == 2ULL << shift);
	pt1 = 0x123ULL << shift;
	hlc1 = pt1 | mask;			/* max logical */
	pt2 = pt1 - eps;
	hlc2 = pt2 & ~mask;			/* min logical */
	assert_true(crt_hlc_epsilon_get_bound(hlc2) >= hlc1);
	assert_true(crt_hlc_epsilon_get_bound(hlc2) <= ((pt1 + mask) | mask));
}

static int
init_tests(void **state)
{
	unsigned int seed;

	/* Seed the random number generator once per test run */
	seed = time(NULL);
	fprintf(stdout, "Seeding this test run with seed=%u\n", seed);
	srand(seed);

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
		cmocka_unit_test(test_hlc_get),
		cmocka_unit_test(test_hlc_get_msg),
		cmocka_unit_test(test_hlc_epsilon),
	};

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("utest_hlc", tests, init_tests,
		fini_tests);
}
