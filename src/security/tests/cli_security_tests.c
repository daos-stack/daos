/**
 * (C) Copyright 2018-2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
init_drpc_resp_with_sec_cred(void)
{
	init_default_drpc_resp_auth_credential();
	pack_cred_in_drpc_call_resp_body(
			drpc_call_resp_return_auth_credential);
}

void
free_drpc_call_resp_auth_credential()
{
	auth__credential__free_unpacked(
			drpc_call_resp_return_auth_credential, NULL);
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

	init_drpc_resp_with_sec_cred();

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
test_request_credentials_succeeds_with_good_values(void **state)
{
	d_iov_t creds;

	memset(&creds, 0, sizeof(d_iov_t));

	assert_int_equal(dc_sec_request_creds(&creds), DER_SUCCESS);

	daos_iov_free(&creds);
}

static void
test_request_credentials_fails_if_drpc_connect_fails(void **state)
{
	d_iov_t creds;

	memset(&creds, 0, sizeof(d_iov_t));
	free_drpc_connect_return(); /* drpc_connect returns NULL on failure */

	assert_int_equal(dc_sec_request_creds(&creds), -DER_BADPATH);

	daos_iov_free(&creds);
}

static void
test_request_credentials_connects_to_default_socket(void **state)
{
	d_iov_t creds;

	memset(&creds, 0, sizeof(d_iov_t));

	dc_sec_request_creds(&creds);

	assert_string_equal(drpc_connect_sockaddr,
			DEFAULT_DAOS_AGENT_DRPC_SOCK);

	daos_iov_free(&creds);
}

static void
test_request_credentials_fails_if_drpc_call_fails(void **state)
{
	d_iov_t creds;

	memset(&creds, 0, sizeof(d_iov_t));
	drpc_call_return = -DER_BUSY;

	assert_int_equal(dc_sec_request_creds(&creds),
			drpc_call_return);

	daos_iov_free(&creds);
}

static void
test_request_credentials_calls_drpc_call(void **state)
{
	d_iov_t creds;

	memset(&creds, 0, sizeof(d_iov_t));

	dc_sec_request_creds(&creds);

	/* Used the drpc conn that we previously connected to */
	assert_ptr_equal(drpc_call_ctx, drpc_connect_return);
	assert_int_equal(drpc_call_flags, R_SYNC); /* Synchronous */
	assert_non_null(drpc_call_resp_ptr); /* Passed valid ptr for response */
	assert_non_null(drpc_call_msg_ptr); /* Sent an RPC message */

	/* Check if RPC message is properly initialized */
	assert_true(drpc_call_msg_content.base.descriptor ==
			&drpc__call__descriptor);

	/* Make sure it's the correct method call */
	assert_int_equal(drpc_call_msg_content.module,
			DRPC_MODULE_SEC_AGENT);
	assert_int_equal(drpc_call_msg_content.method,
			DRPC_METHOD_SEC_AGENT_REQUEST_CREDS);

	/* Check that the body has no content */
	assert_int_equal(drpc_call_msg_content.body.len, 0);

	daos_iov_free(&creds);
}

static void
test_request_credentials_closes_socket_when_call_ok(void **state)
{
	d_iov_t creds;

	memset(&creds, 0, sizeof(d_iov_t));

	dc_sec_request_creds(&creds);

	assert_ptr_equal(drpc_close_ctx, drpc_connect_return);

	daos_iov_free(&creds);
}

static void
test_request_credentials_closes_socket_when_call_fails(void **state)
{
	d_iov_t creds;

	memset(&creds, 0, sizeof(d_iov_t));
	drpc_call_return = -DER_NOMEM;

	dc_sec_request_creds(&creds);

	assert_ptr_equal(drpc_close_ctx, drpc_connect_return);

	daos_iov_free(&creds);
}

static void
test_request_credentials_fails_if_reply_null(void **state)
{
	d_iov_t creds;

	memset(&creds, 0, sizeof(d_iov_t));
	drpc_call_resp_return_ptr = NULL;

	assert_int_equal(dc_sec_request_creds(&creds), -DER_NOREPLY);

	daos_iov_free(&creds);
}

static void
test_request_credentials_fails_if_reply_status_failure(void **state)
{
	d_iov_t creds;

	memset(&creds, 0, sizeof(d_iov_t));
	drpc_call_resp_return_content.status = DRPC__STATUS__FAILURE;

	assert_int_equal(dc_sec_request_creds(&creds), -DER_MISC);

	daos_iov_free(&creds);
}

static void
test_request_credentials_fails_if_reply_body_malformed(void **state)
{
	d_iov_t creds;

	memset(&creds, 0, sizeof(d_iov_t));
	free_drpc_call_resp_body();
	D_ALLOC(drpc_call_resp_return_content.body.data, 1);
	drpc_call_resp_return_content.body.len = 1;

	assert_int_equal(dc_sec_request_creds(&creds), -DER_MISC);

	daos_iov_free(&creds);
}

static void
test_request_credentials_fails_if_reply_token_missing(void **state)
{
	d_iov_t creds;

	memset(&creds, 0, sizeof(d_iov_t));
	auth__token__free_unpacked(
			drpc_call_resp_return_auth_credential->token, NULL);
	drpc_call_resp_return_auth_credential->token = NULL;
	pack_cred_in_drpc_call_resp_body(
			drpc_call_resp_return_auth_credential);

	assert_int_equal(dc_sec_request_creds(&creds), -DER_MISC);

	daos_iov_free(&creds);
}

static void
test_request_credentials_returns_raw_bytes(void **state)
{
	d_iov_t	creds;
	size_t		expected_len;
	uint8_t		*expected_data;

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

/* Convenience macro for declaring unit tests in this suite */
#define SECURITY_UTEST(X) \
	cmocka_unit_test_setup_teardown(X, setup_security_mocks, \
			teardown_security_mocks)

int
main(void)
{
	const struct CMUnitTest tests[] = {
		SECURITY_UTEST(
			test_request_credentials_fails_with_null_creds),
		SECURITY_UTEST(
			test_request_credentials_succeeds_with_good_values),
		SECURITY_UTEST(
			test_request_credentials_fails_if_drpc_connect_fails),
		SECURITY_UTEST(
			test_request_credentials_connects_to_default_socket),
		SECURITY_UTEST(
			test_request_credentials_fails_if_drpc_call_fails),
		SECURITY_UTEST(
			test_request_credentials_calls_drpc_call),
		SECURITY_UTEST(
			test_request_credentials_closes_socket_when_call_ok),
		SECURITY_UTEST(
			test_request_credentials_closes_socket_when_call_fails),
		SECURITY_UTEST(
			test_request_credentials_fails_if_reply_null),
		SECURITY_UTEST(
			test_request_credentials_fails_if_reply_status_failure),
		SECURITY_UTEST(
			test_request_credentials_fails_if_reply_body_malformed),
		SECURITY_UTEST(
			test_request_credentials_fails_if_reply_token_missing),
		SECURITY_UTEST(
			test_request_credentials_returns_raw_bytes),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

#undef SECURITY_UTEST
