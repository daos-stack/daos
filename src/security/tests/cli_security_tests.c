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
#include "../security.pb-c.h"


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

static struct drpc *drpc_connect_return; /* value to be returned */
static char drpc_connect_sockaddr[PATH_MAX + 1]; /* saved copy of input */
struct drpc *
drpc_connect(char *sockaddr)
{
	strncpy(drpc_connect_sockaddr, sockaddr, PATH_MAX);
	return drpc_connect_return;
}

static int drpc_call_return; /* value to be returned */
static struct drpc *drpc_call_ctx; /* saved input */
static int drpc_call_flags; /* saved input */
static Drpc__Call drpc_call_msg_content; /* saved copy of input */
/* saved input ptr address (for checking non-NULL) */
static Drpc__Call *drpc_call_msg_ptr;
/* saved input ptr address (for checking non-NULL) */
static Drpc__Response **drpc_call_resp_ptr;
/* ptr to content to allocate in response (can be NULL) */
static Drpc__Response *drpc_call_resp_return_ptr;
/* actual content to allocate in response */
static Drpc__Response drpc_call_resp_return_content;
/* unpacked content of response body */
static SecurityCredential *drpc_call_resp_return_security_credential;
int
drpc_call(struct drpc *ctx, int flags, Drpc__Call *msg,
		Drpc__Response **resp)
{
	/* Save off the params passed in */
	drpc_call_ctx = ctx;
	drpc_call_flags = flags;
	drpc_call_msg_ptr = msg;
	if (msg != NULL) {
		memcpy(&drpc_call_msg_content, msg, sizeof(Drpc__Call));

		/* Need a copy of the body data, it's separately allocated */
		D_ALLOC(drpc_call_msg_content.body.data, msg->body.len);
		memcpy(drpc_call_msg_content.body.data, msg->body.data,
				msg->body.len);
	}
	drpc_call_resp_ptr = resp;

	if (resp == NULL) {
		return drpc_call_return;
	}

	/* Fill out the mocked response */
	if (drpc_call_resp_return_ptr == NULL) {
		*resp = NULL;
	} else {
		size_t data_len =
				drpc_call_resp_return_content.body.len;

		/**
		 * Need to allocate a new copy to return - the
		 * production code will free the returned memory.
		 */
		D_ALLOC_PTR(*resp);
		memcpy(*resp, &drpc_call_resp_return_content,
				sizeof(Drpc__Response));

		D_ALLOC((*resp)->body.data, data_len);
		memcpy((*resp)->body.data,
				drpc_call_resp_return_content.body.data,
				data_len);
	}

	return drpc_call_return;
}

static int drpc_close_return; /* value to be returned */
static struct drpc *drpc_close_ctx; /* saved copy of input ctx */
int
drpc_close(struct drpc *ctx)
{
	drpc_close_ctx = ctx;
	return drpc_close_return;
}

/* Clean up dynamically-allocated variables from our mocks */
static void
free_drpc_connect_return()
{
	D_FREE(drpc_connect_return);
}

static void
free_drpc_call_msg_body()
{
	D_FREE(drpc_call_msg_content.body.data);
	drpc_call_msg_content.body.len = 0;
}

static void
free_drpc_call_resp_body()
{
	D_FREE(drpc_call_resp_return_content.body.data);
	drpc_call_resp_return_content.body.len = 0;
}

static void
free_drpc_call_resp_security_credential()
{
	security_credential__free_unpacked(
			drpc_call_resp_return_security_credential, NULL);
}

/* Setup helper functions - for mocks */
static void
init_default_drpc_resp_security_credential()
{
	D_ALLOC_PTR(drpc_call_resp_return_security_credential);
	security_credential__init(drpc_call_resp_return_security_credential);

	D_ALLOC_PTR(drpc_call_resp_return_security_credential->token);
	auth_token__init(drpc_call_resp_return_security_credential->token);
}

static void
pack_drpc_call_resp_body(SecurityCredential *cred)
{
	size_t len = security_credential__get_packed_size(cred);

	drpc_call_resp_return_content.body.len = len;
	D_ALLOC(drpc_call_resp_return_content.body.data, len);
	security_credential__pack(cred,
			drpc_call_resp_return_content.body.data);
}

static void
init_drpc_call_resp()
{
	/* By default, return non-null response */
	drpc_call_resp_return_ptr = &drpc_call_resp_return_content;

	drpc__response__init(&drpc_call_resp_return_content);
	drpc_call_resp_return_content.status = DRPC__STATUS__SUCCESS;

	init_default_drpc_resp_security_credential();
	pack_drpc_call_resp_body(drpc_call_resp_return_security_credential);
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

	D_ALLOC_PTR(drpc_connect_return);
	memset(drpc_connect_sockaddr, 0, sizeof(drpc_connect_sockaddr));

	drpc_call_return = DER_SUCCESS;
	drpc_call_ctx = NULL;
	drpc_call_flags = 0;
	drpc_call_msg_ptr = NULL;
	memset(&drpc_call_msg_content, 0, sizeof(drpc_call_msg_content));
	drpc_call_resp_ptr = NULL;
	init_drpc_call_resp();

	drpc_close_return = 0;
	drpc_close_ctx = NULL;

	return 0;
}

static int
teardown_security_mocks(void **state)
{
	/* Cleanup dynamically allocated mocks */

	free_drpc_connect_return();
	free_drpc_call_msg_body();
	free_drpc_call_resp_body();
	free_drpc_call_resp_security_credential();

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
	daos_iov_t creds;

	memset(&creds, 0, sizeof(daos_iov_t));

	assert_int_equal(dc_sec_request_creds(&creds), DER_SUCCESS);

	daos_iov_free(&creds);
}

static void
test_request_credentials_fails_if_drpc_connect_fails(void **state)
{
	daos_iov_t creds;

	memset(&creds, 0, sizeof(daos_iov_t));
	free_drpc_connect_return(); /* drpc_connect returns NULL on failure */

	assert_int_equal(dc_sec_request_creds(&creds), -DER_BADPATH);

	daos_iov_free(&creds);
}

static void
test_request_credentials_connects_to_default_socket(void **state)
{
	daos_iov_t creds;

	memset(&creds, 0, sizeof(daos_iov_t));

	dc_sec_request_creds(&creds);

	assert_string_equal(drpc_connect_sockaddr,
			DEFAULT_DAOS_AGENT_DRPC_SOCK);

	daos_iov_free(&creds);
}

static void
test_request_credentials_connects_to_env_socket(void **state)
{
	daos_iov_t creds;

	memset(&creds, 0, sizeof(daos_iov_t));
	getenv_return = "/nice/good/wonderful.sock";

	dc_sec_request_creds(&creds);

	/* Tried to connect to the path we got back from getenv */
	assert_string_equal(drpc_connect_sockaddr, getenv_return);

	/* Make sure we asked for the right env variable */
	assert_non_null(getenv_name);
	assert_string_equal(getenv_name, DAOS_AGENT_DRPC_SOCK_ENV);

	daos_iov_free(&creds);
}

static void
test_request_credentials_fails_if_drpc_call_fails(void **state)
{
	daos_iov_t creds;

	memset(&creds, 0, sizeof(daos_iov_t));
	drpc_call_return = -DER_BUSY;

	assert_int_equal(dc_sec_request_creds(&creds),
			drpc_call_return);

	daos_iov_free(&creds);
}

static void
test_request_credentials_calls_drpc_call(void **state)
{
	daos_iov_t creds;

	memset(&creds, 0, sizeof(daos_iov_t));

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
			DRPC_MODULE_SECURITY_AGENT);
	assert_int_equal(drpc_call_msg_content.method,
			DRPC_METHOD_SECURITY_AGENT_REQUEST_CREDENTIALS);

	/* Check that the body has no content */
	assert_int_equal(drpc_call_msg_content.body.len, 0);

	daos_iov_free(&creds);
}

static void
test_request_credentials_closes_socket_when_call_ok(void **state)
{
	daos_iov_t creds;

	memset(&creds, 0, sizeof(daos_iov_t));

	dc_sec_request_creds(&creds);

	assert_ptr_equal(drpc_close_ctx, drpc_connect_return);

	daos_iov_free(&creds);
}

static void
test_request_credentials_closes_socket_when_call_fails(void **state)
{
	daos_iov_t creds;

	memset(&creds, 0, sizeof(daos_iov_t));
	drpc_call_return = -DER_NOMEM;

	dc_sec_request_creds(&creds);

	assert_ptr_equal(drpc_close_ctx, drpc_connect_return);

	daos_iov_free(&creds);
}

static void
test_request_credentials_fails_if_reply_null(void **state)
{
	daos_iov_t creds;

	memset(&creds, 0, sizeof(daos_iov_t));
	drpc_call_resp_return_ptr = NULL;

	assert_int_equal(dc_sec_request_creds(&creds), -DER_NOREPLY);

	daos_iov_free(&creds);
}

static void
test_request_credentials_fails_if_reply_status_failure(void **state)
{
	daos_iov_t creds;

	memset(&creds, 0, sizeof(daos_iov_t));
	drpc_call_resp_return_content.status = DRPC__STATUS__FAILURE;

	assert_int_equal(dc_sec_request_creds(&creds), -DER_MISC);

	daos_iov_free(&creds);
}

static void
test_request_credentials_fails_if_reply_body_malformed(void **state)
{
	daos_iov_t creds;

	memset(&creds, 0, sizeof(daos_iov_t));
	free_drpc_call_resp_body();
	D_ALLOC(drpc_call_resp_return_content.body.data, 1);
	drpc_call_resp_return_content.body.len = 1;

	assert_int_equal(dc_sec_request_creds(&creds), -DER_MISC);

	daos_iov_free(&creds);
}

static void
test_request_credentials_fails_if_reply_token_missing(void **state)
{
	daos_iov_t creds;

	memset(&creds, 0, sizeof(daos_iov_t));
	auth_token__free_unpacked(
			drpc_call_resp_return_security_credential->token, NULL);
	drpc_call_resp_return_security_credential->token = NULL;
	pack_drpc_call_resp_body(drpc_call_resp_return_security_credential);

	assert_int_equal(dc_sec_request_creds(&creds), -DER_MISC);

	daos_iov_free(&creds);
}

static void
test_request_credentials_returns_raw_bytes(void **state)
{
	daos_iov_t	creds;
	size_t		expected_len;
	uint8_t		*expected_data;

	memset(&creds, 0, sizeof(daos_iov_t));

	/*
	 * Credential bytes == raw bytes of the packed SecurityCredential
	 */
	expected_len = security_credential__get_packed_size(
			drpc_call_resp_return_security_credential);
	D_ALLOC(expected_data, expected_len);
	security_credential__pack(drpc_call_resp_return_security_credential,
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
			test_request_credentials_connects_to_env_socket),
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
