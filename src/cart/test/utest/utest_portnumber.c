/*
 * (C) Copyright 2020 Intel Corporation.
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

/*
 * This code tests whether a provider returns an error if 2 independent
 * instances attempts to open the same port number.
 *
 * Two child process are forked with the same provider information.
 * The test is setup so that first child opens the port and then
 * the second child should fail.
 * Note, synchronization between child process is performed via sleep
 * function calls.  The sleep time should prevent any problems, but
 * beware if an issue arise where the results are swapped.
 */
/*
 * Reference jira ticket DAOS-5732 to include socket and verb tests.
 */
#define tests_not_included

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <time.h>
#include <sys/wait.h>

#include <cmocka.h>
#include <cart/api.h>
#include "../cart/crt_internal.h"


static void
run_test_fork(void **state)
{
	int	result1;
	int	result2;
	int	status;
	int	rc = 0;
	int	child_result;
	pid_t	pid1 = 0;
	pid_t	pid2 = 0;
	crt_context_t crt_context = NULL;

	pid1 = fork();
	/* fork first child process */
	if (pid1 == 0) {
		rc = crt_init(NULL, CRT_FLAG_BIT_SERVER);
		if (rc != 0)
			exit(10);
		child_result = crt_context_create(&crt_context);
		sleep(10);	/* wait for second child */
		rc = crt_context_destroy(crt_context, false);
		if (rc != 0)
			exit(11);
		rc = crt_finalize();
		if (rc != 0)
			exit(12);
		exit(child_result);
	}

	/* fork second child process */
	pid2 = fork();
	if (pid2 == 0) {
		sleep(2);  /* wait for first child just in case */
		rc = crt_init(NULL, CRT_FLAG_BIT_SERVER);
		if (rc != 0)
			exit(20);
		child_result = crt_context_create(&crt_context);
		rc = crt_context_destroy(crt_context, false);
		if (rc != 0)
			exit(21);
		rc = crt_finalize();
		if (rc != 0)
			exit(22);
		exit(child_result);
	}

	/* Wait for first child and get results */
	waitpid(pid1, &status, 0);
	if (WIFEXITED(status)) {
		result1 = WEXITSTATUS(status);
	}

	/* Wait for second child and get results */
	waitpid(pid2, &status, 0);
	if (WIFEXITED(status)) {
		result2 = WEXITSTATUS(status);
	}

	/* Test results.  first child should should succed. */
	assert_true(result1 == 0);
	assert_true(result2 != 0);
	assert_true(rc == 0);    /* prevents compile issue */
}
static void
test_port_tcp(void **state)
{
	setenv("OFI_INTERFACE", "lo", 1);
	setenv("CRT_PHY_ADDR_STR", "ofi+tcp;ofi_rxm", 1);
	run_test_fork(state);
}

#ifndef tests_not_included
static void
test_port_sockets(void **state)
{
	setenv("OFI_INTERFACE", "eth0", 1);
	setenv("CRT_PHY_ADDR_STR", "ofi+sockets", 1);
	run_test_fork(state);
};

static void
test_port_verb(void **state)
{
	setenv("OFI_INTERFACE", "eth0", 1);
	setenv("CRT_PHY_ADDR_STR", "ofi+verbs;ofi_rxm", 1);
	run_test_fork(state);
};
#endif

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
		cmocka_unit_test(test_port_tcp),
#ifndef tests_not_included
		cmocka_unit_test(test_port_sockets),
		cmocka_unit_test(test_port_verb),
#endif
	};

	setenv("FI_UNIVERSE_SIZE", "2048", 1);
	setenv("FI_OFI_RXM_USE_SRX", "1", 1);
	setenv("D_LOG_MASK", "CRIT", 1);
	setenv("OFI_PORT", "34571", 1);

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests(tests, init_tests, fini_tests);
}
