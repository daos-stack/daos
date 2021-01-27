/**
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * Unit tests for the agent API for the client lib
 */

#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos_types.h>
#include <daos_errno.h>
#include <daos/common.h>
#include <daos/agent.h>
#include <string.h>

/*
 * Mocks
 */

static char *getenv_return; /* value to be returned */
static const char *getenv_name; /* saved input */
char *getenv(const char *name)
{
	getenv_name = name;
	return getenv_return;
}

/*
 * Unit test setup and teardown
 */

static int
setup_agent_mocks(void **state)
{
	/* Initialize mock values to something sane */
	getenv_return = NULL;
	getenv_name = NULL;

	return 0;
}

static int
teardown_agent_mocks(void **state)
{
	return 0;
}

/*
 * Client lib agent function tests
 */
static void
test_dc_agent_init_no_env(void **state)
{
	dc_agent_init();
	/* Tried to connect to the path we got back from getenv */
	assert_string_equal(dc_agent_sockpath, DEFAULT_DAOS_AGENT_DRPC_SOCK);

	/* Make sure we asked for the right env variable */
	assert_non_null(getenv_name);
	assert_string_equal(getenv_name, DAOS_AGENT_DRPC_DIR_ENV);

	dc_agent_fini();
}

static void
test_dc_agent_init_with_env(void **state)
{
	char *expected_sockaddr = "/nice/good/daos_agent.sock";

	getenv_return = "/nice/good";

	dc_agent_init();
	/* Tried to connect to the path we got back from getenv */
	assert_string_equal(dc_agent_sockpath, expected_sockaddr);

	/* Make sure we asked for the right env variable */
	assert_non_null(getenv_name);
	assert_string_equal(getenv_name, DAOS_AGENT_DRPC_DIR_ENV);

	dc_agent_fini();
}


/* Convenience macro for declaring unit tests in this suite */
#define AGENT_UTEST(X) \
	cmocka_unit_test_setup_teardown(X, setup_agent_mocks, \
			teardown_agent_mocks)

int
main(void)
{
	const struct CMUnitTest tests[] = {
		AGENT_UTEST(
			test_dc_agent_init_no_env),
		AGENT_UTEST(
			test_dc_agent_init_with_env),
	};

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests(tests, NULL, NULL);
}

#undef AGENT_UTEST
