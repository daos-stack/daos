/**
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * Unit tests for the security API for the client lib
 */

#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos_types.h>
#include <daos_errno.h>
#include <daos/common.h>
#include <daos/drpc.h>
#include <daos/drpc_modules.h>
#include <daos/drpc.pb-c.h>
#include <daos/agent.h>
#include <daos/security.h>
#include <string.h>
#include <linux/limits.h>

#include "../auth.pb-c.h"
#include "drpc_mocks.h"

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

/* unpacked content of response body */
static Auth__Credential *drpc_call_resp_return_auth_credential;
char *dc_agent_sockpath;

static void
init_default_drpc_resp_auth_credential(void)
{
	D_ALLOC_PTR(drpc_call_resp_return_auth_credential);
	auth__credential__init(drpc_call_resp_return_auth_credential);

	D_ALLOC_PTR(drpc_call_resp_return_auth_credential->token);
	auth__token__init(drpc_call_resp_return_auth_credential->token);
}

static void
init_drpc_resp_with_cred(Auth__Credential *cred)
{
	Auth__GetCredResp resp = AUTH__GET_CRED_RESP__INIT;

	resp.cred = cred;
	pack_get_cred_resp_in_drpc_call_resp_body(&resp);
}

static void
init_drpc_resp_with_default_cred(void)
{
	init_default_drpc_resp_auth_credential();
	init_drpc_resp_with_cred(drpc_call_resp_return_auth_credential);
}

void
free_drpc_call_resp_auth_credential()
{
	auth__credential__free_unpacked(drpc_call_resp_return_auth_credential,
					NULL);
}

/*
 * Unit test setup and teardown
 */

static int
setup_security_mocks(void **state)
{
	/* Initialize mock values to something sane */
	getenv_return = NULL;
	getenv_name = NULL;
	dc_agent_sockpath = DEFAULT_DAOS_AGENT_DRPC_SOCK;

	mock_drpc_connect_setup();
	mock_drpc_call_setup();
	mock_drpc_close_setup();

	init_drpc_resp_with_default_cred();

	return 0;
}

static int
teardown_security_mocks(void **state)
{
	/* Cleanup dynamically allocated mocks */

	mock_drpc_connect_teardown();
	mock_drpc_call_teardown();
	free_drpc_call_resp_auth_credential();

	return 0;
}

/*
 * Client lib security function tests
 */

static void
test_request_credentials_fails_with_null_creds(void **state)
{
	assert_int_equal(dc_sec_request_creds(NULL), -DER_INVAL);
}

static void
test_request_credentials_returns_raw_bytes(void **state)
{
	d_iov_t	creds;
	size_t	expected_len;
	uint8_t	*expected_data;

	memset(&creds, 0, sizeof(d_iov_t));

	/*
	 * Credential bytes == raw bytes of the packed Auth__Credential
	 */
	expected_len = auth__credential__get_packed_size(
			drpc_call_resp_return_auth_credential);
	D_ALLOC(expected_data, expected_len);
	auth__credential__pack(drpc_call_resp_return_auth_credential,
			expected_data);

	assert_int_equal(dc_sec_request_creds(&creds), DER_SUCCESS);

	assert_int_equal(creds.iov_buf_len, expected_len);
	assert_int_equal(creds.iov_len, expected_len);
	assert_memory_equal(creds.iov_buf, expected_data, expected_len);

	D_FREE(expected_data);
	daos_iov_free(&creds);
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
                { "test_request_credentials_fails_with_null_creds", test_request_credentials_fails_with_null_creds, NULL, NULL},
                { "test_request_credentials_returns_raw_bytes", test_request_credentials_returns_raw_bytes, NULL, NULL},
	};

	return cmocka_run_group_tests_name("security_cli_security",
					   tests, setup_security_mocks, teardown_security_mocks);
}

