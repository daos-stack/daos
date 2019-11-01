/*
 * (C) Copyright 2019 Intel Corporation.
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/**
 * Unit tests for the Management dRPC handlers
 */

#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos/drpc.h>
#include <daos_security.h>
#include "../acl.pb-c.h"
#include "../drpc_internal.h"
#include "mocks.h"

#define TEST_UUID	"12345678-1234-1234-1234-123456789abc"
#define TEST_ACES_NR	(3)

static const char	*TEST_ACES[] = {"A::OWNER@:rw",
					"A::niceuser@:rw",
					"A:G:GROUP@:r"};

/*
 * dRPC setup/teardown for ACL related tests
 */

static int
drpc_pool_acl_setup(void **state)
{
	mock_ds_mgmt_pool_get_acl_setup();
	mock_ds_mgmt_pool_overwrite_acl_setup();

	return 0;
}

static int
drpc_pool_acl_teardown(void **state)
{
	mock_ds_mgmt_pool_get_acl_teardown();
	mock_ds_mgmt_pool_overwrite_acl_teardown();

	return 0;
}

/*
 * dRPC Get ACL tests
 */
static Drpc__Call *
new_drpc_call_with_bad_body(void)
{
	Drpc__Call	*call;
	uint8_t		*bad_bytes;
	size_t		bad_bytes_len = 16; /* arbitrary */
	size_t		i;

	D_ALLOC(call, sizeof(Drpc__Call));
	assert_non_null(call);
	drpc__call__init(call);

	D_ALLOC_ARRAY(bad_bytes, bad_bytes_len);
	assert_non_null(bad_bytes);

	/* Fill out with junk that won't translate to a PB struct */
	for (i = 0; i < bad_bytes_len; i++)
		bad_bytes[i] = i;

	call->body.data = bad_bytes;
	call->body.len = bad_bytes_len;

	return call;
}

static void
test_drpc_pool_get_acl_bad_request(void **state)
{
	Drpc__Call	*call;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	call = new_drpc_call_with_bad_body();

	ds_mgmt_drpc_pool_get_acl(call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__FAILURE);
	assert_null(resp.body.data);
	assert_int_equal(resp.body.len, 0);

	drpc_call_free(call);
}

static void
pack_get_acl_req(Drpc__Call *call, Mgmt__GetACLReq *req)
{
	size_t	len;
	uint8_t	*body;

	len = mgmt__get_aclreq__get_packed_size(req);
	D_ALLOC(body, len);
	assert_non_null(body);

	mgmt__get_aclreq__pack(req, body);

	call->body.data = body;
	call->body.len = len;
}

static void
setup_get_acl_drpc_call(Drpc__Call *call, char *uuid)
{
	Mgmt__GetACLReq acl_req = MGMT__GET_ACLREQ__INIT;

	acl_req.uuid = uuid;
	pack_get_acl_req(call, &acl_req);
}

static void
test_drpc_pool_get_acl_bad_uuid(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__ACLResp		*acl_resp = NULL;

	setup_get_acl_drpc_call(&call, "Not a UUID at all");

	ds_mgmt_drpc_pool_get_acl(&call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp.body.data);

	acl_resp = mgmt__aclresp__unpack(NULL, resp.body.len,
					 resp.body.data);
	assert_non_null(acl_resp);
	assert_int_equal(acl_resp->status, -DER_INVAL);
	assert_int_equal(acl_resp->n_acl, 0);

	mgmt__aclresp__free_unpacked(acl_resp, NULL);
	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_get_acl_mgmt_svc_fails(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__ACLResp		*acl_resp = NULL;

	setup_get_acl_drpc_call(&call, TEST_UUID);
	ds_mgmt_pool_get_acl_return = -DER_UNKNOWN;

	ds_mgmt_drpc_pool_get_acl(&call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp.body.data);

	acl_resp = mgmt__aclresp__unpack(NULL, resp.body.len,
					 resp.body.data);
	assert_non_null(acl_resp);
	assert_int_equal(acl_resp->status, ds_mgmt_pool_get_acl_return);
	assert_int_equal(acl_resp->n_acl, 0);

	mgmt__aclresp__free_unpacked(acl_resp, NULL);
	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static struct daos_acl *
get_valid_acl(void)
{
	struct daos_acl	*acl = NULL;

	assert_int_equal(daos_acl_from_strs(TEST_ACES, TEST_ACES_NR, &acl), 0);

	return acl;
}

static void
test_drpc_pool_get_acl_cant_translate_acl(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__ACLResp		*acl_resp = NULL;
	struct daos_ace		*ace;

	setup_get_acl_drpc_call(&call, TEST_UUID);
	ds_mgmt_pool_get_acl_return_acl = get_valid_acl();

	/* Mangle an ACE so it can't be translated to a string */
	ace = daos_acl_get_next_ace(ds_mgmt_pool_get_acl_return_acl, NULL);
	ace->dae_access_types = 0xff; /* invalid bits */

	ds_mgmt_drpc_pool_get_acl(&call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp.body.data);

	acl_resp = mgmt__aclresp__unpack(NULL, resp.body.len,
					 resp.body.data);
	assert_non_null(acl_resp);
	assert_int_equal(acl_resp->status, -DER_INVAL);
	assert_int_equal(acl_resp->n_acl, 0);

	mgmt__aclresp__free_unpacked(acl_resp, NULL);
	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_get_acl_success(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__ACLResp		*acl_resp = NULL;
	int			i;

	setup_get_acl_drpc_call(&call, TEST_UUID);
	ds_mgmt_pool_get_acl_return_acl = get_valid_acl();

	ds_mgmt_drpc_pool_get_acl(&call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp.body.data);

	acl_resp = mgmt__aclresp__unpack(NULL, resp.body.len,
					 resp.body.data);
	assert_non_null(acl_resp);
	assert_int_equal(acl_resp->status, 0);
	assert_int_equal(acl_resp->n_acl, TEST_ACES_NR);

	for (i = 0; i < TEST_ACES_NR; i++) {
		assert_string_equal(acl_resp->acl[i], TEST_ACES[i]);
	}

	mgmt__aclresp__free_unpacked(acl_resp, NULL);
	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

/*
 * dRPC overwrite ACL tests
 */
static void
test_drpc_pool_overwrite_acl_bad_request(void **state)
{
	Drpc__Call	*call;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	call = new_drpc_call_with_bad_body();

	ds_mgmt_drpc_pool_overwrite_acl(call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__FAILURE);
	assert_null(resp.body.data);
	assert_int_equal(resp.body.len, 0);

	drpc_call_free(call);
}

static void
pack_modify_acl_req(Drpc__Call *call, Mgmt__ModifyACLReq *req)
{
	size_t	len;
	uint8_t	*body;

	len = mgmt__modify_aclreq__get_packed_size(req);
	D_ALLOC(body, len);
	assert_non_null(body);

	mgmt__modify_aclreq__pack(req, body);

	call->body.data = body;
	call->body.len = len;
}

static void
setup_modify_acl_drpc_call(Drpc__Call *call, char *uuid, const char **acl,
			      size_t acl_nr)
{
	Mgmt__ModifyACLReq req = MGMT__MODIFY_ACLREQ__INIT;

	req.uuid = uuid;
	req.acl = (char **)acl;
	req.n_acl = acl_nr;

	pack_modify_acl_req(call, &req);
}

static void
test_drpc_pool_overwrite_acl_bad_uuid(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__ACLResp		*acl_resp = NULL;

	setup_modify_acl_drpc_call(&call, "invalid UUID", TEST_ACES,
				   TEST_ACES_NR);

	ds_mgmt_drpc_pool_overwrite_acl(&call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp.body.data);

	acl_resp = mgmt__aclresp__unpack(NULL, resp.body.len,
					 resp.body.data);
	assert_non_null(acl_resp);
	assert_int_equal(acl_resp->status, -DER_INVAL);
	assert_int_equal(acl_resp->n_acl, 0);

	mgmt__aclresp__free_unpacked(acl_resp, NULL);
	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_overwrite_acl_bad_acl(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__ACLResp		*acl_resp = NULL;
	size_t			bad_nr = 2;
	static const char	*bad_aces[] = {"A::OWNER@:rw", "invalid"};

	setup_modify_acl_drpc_call(&call, TEST_UUID, bad_aces,
				   bad_nr);

	ds_mgmt_drpc_pool_overwrite_acl(&call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp.body.data);

	acl_resp = mgmt__aclresp__unpack(NULL, resp.body.len,
					 resp.body.data);
	assert_non_null(acl_resp);
	assert_int_equal(acl_resp->status, -DER_INVAL);
	assert_int_equal(acl_resp->n_acl, 0);

	mgmt__aclresp__free_unpacked(acl_resp, NULL);
	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_overwrite_acl_mgmt_svc_fails(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__ACLResp		*acl_resp = NULL;

	setup_modify_acl_drpc_call(&call, TEST_UUID, TEST_ACES, TEST_ACES_NR);
	ds_mgmt_pool_overwrite_acl_return = -DER_UNKNOWN;

	ds_mgmt_drpc_pool_overwrite_acl(&call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp.body.data);

	acl_resp = mgmt__aclresp__unpack(NULL, resp.body.len,
					 resp.body.data);
	assert_non_null(acl_resp);
	assert_int_equal(acl_resp->status, ds_mgmt_pool_overwrite_acl_return);
	assert_int_equal(acl_resp->n_acl, 0);

	mgmt__aclresp__free_unpacked(acl_resp, NULL);
	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_overwrite_acl_success(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__ACLResp		*acl_resp = NULL;
	size_t			i;

	setup_modify_acl_drpc_call(&call, TEST_UUID, TEST_ACES, TEST_ACES_NR);

	/*
	 * Set up the mgmt svc overwrite function to return the same ACEs
	 * we passed in as its result.
	 */
	assert_int_equal(daos_acl_from_strs(TEST_ACES, TEST_ACES_NR,
			 &ds_mgmt_pool_overwrite_acl_result), 0);

	ds_mgmt_drpc_pool_overwrite_acl(&call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp.body.data);

	acl_resp = mgmt__aclresp__unpack(NULL, resp.body.len,
					 resp.body.data);
	assert_non_null(acl_resp);
	assert_int_equal(acl_resp->status, 0);
	assert_int_equal(acl_resp->n_acl, TEST_ACES_NR);

	for (i = 0; i < TEST_ACES_NR; i++) {
		assert_string_equal(acl_resp->acl[i], TEST_ACES[i]);
	}

	mgmt__aclresp__free_unpacked(acl_resp, NULL);
	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

#define ACL_TEST(x)	cmocka_unit_test_setup_teardown(x, \
						drpc_pool_acl_setup, \
						drpc_pool_acl_teardown)
int
main(void)
{
	const struct CMUnitTest tests[] = {
		ACL_TEST(test_drpc_pool_get_acl_bad_request),
		ACL_TEST(test_drpc_pool_get_acl_bad_uuid),
		ACL_TEST(test_drpc_pool_get_acl_mgmt_svc_fails),
		ACL_TEST(test_drpc_pool_get_acl_cant_translate_acl),
		ACL_TEST(test_drpc_pool_get_acl_success),
		ACL_TEST(test_drpc_pool_overwrite_acl_bad_request),
		ACL_TEST(test_drpc_pool_overwrite_acl_bad_uuid),
		ACL_TEST(test_drpc_pool_overwrite_acl_bad_acl),
		ACL_TEST(test_drpc_pool_overwrite_acl_mgmt_svc_fails),
		ACL_TEST(test_drpc_pool_overwrite_acl_success),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
