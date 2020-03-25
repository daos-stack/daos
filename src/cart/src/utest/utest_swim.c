/* Copyright (C) 2020 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 * 4. All publications or advertising materials mentioning features or use of
 *    this software are asked, but not required, to acknowledge that it was
 *    developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

	rc = crt_init_opt("utest_swim", CRT_FLAG_BIT_SERVER, NULL);
	assert_int_equal(rc, 0);

	rc = crt_rank_self_set(0);
	assert_int_equal(rc, 0);

	rc = crt_swim_rank_add(crt_grp_pub2priv(NULL), 1);
	assert_int_equal(rc, 0);

	rc = crt_swim_rank_add(crt_grp_pub2priv(NULL), 2);
	assert_int_equal(rc, 0);

	rc = crt_swim_rank_add(crt_grp_pub2priv(NULL), 1);
	assert_int_equal(rc, -DER_ALREADY);

	rc = crt_swim_rank_add(crt_grp_pub2priv(NULL), 0);
	assert_int_equal(rc, -DER_ALREADY);

	rc = crt_finalize();
	assert_int_equal(rc, 0);
}

static int
init_tests(void **state)
{
	unsigned int seed;

	/* Seed the random number generator once per test run */
	seed = time(NULL);
	fprintf(stdout, "Seeding this test run with seed=%u\n", seed);
	srand(seed);

	setenv("CRT_PHY_ADDR_STR", "ofi+sockets", 1);
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

	return cmocka_run_group_tests(tests, init_tests, fini_tests);
}
