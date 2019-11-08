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

#include <gurt/common.h>
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
 * dRPC Get ACL setup/teardown
 */

static int
drpc_pool_get_acl_setup(void **state)
{
	mock_ds_mgmt_pool_get_acl_setup();

	return 0;
}

static int
drpc_pool_get_acl_teardown(void **state)
{
	mock_ds_mgmt_pool_get_acl_teardown();

	return 0;
}

/*
 * dRPC Get ACL tests
 */
static void
test_drpc_pool_get_acl_bad_request(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;
	uint8_t		bad_bytes[16];
	size_t		i;

	/* Fill out with junk that won't translate to a GetACLReq */
	for (i = 0; i < sizeof(bad_bytes); i++)
		bad_bytes[i] = i;

	call.body.data = bad_bytes;
	call.body.len = sizeof(bad_bytes);

	ds_mgmt_drpc_pool_get_acl(&call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__FAILURE);
	assert_null(resp.body.data);
	assert_int_equal(resp.body.len, 0);
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
	Mgmt__GetACLResp	*acl_resp = NULL;

	setup_get_acl_drpc_call(&call, "Not a UUID at all");

	ds_mgmt_drpc_pool_get_acl(&call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp.body.data);

	acl_resp = mgmt__get_aclresp__unpack(NULL, resp.body.len,
					     resp.body.data);
	assert_non_null(acl_resp);
	assert_int_equal(acl_resp->status, -DER_INVAL);
	assert_int_equal(acl_resp->n_acl, 0);
}

static void
test_drpc_pool_get_acl_pool_svc_fails(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__GetACLResp	*acl_resp = NULL;

	setup_get_acl_drpc_call(&call, TEST_UUID);
	ds_mgmt_pool_get_acl_return = -DER_UNKNOWN;

	ds_mgmt_drpc_pool_get_acl(&call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp.body.data);

	acl_resp = mgmt__get_aclresp__unpack(NULL, resp.body.len,
					     resp.body.data);
	assert_non_null(acl_resp);
	assert_int_equal(acl_resp->status, ds_mgmt_pool_get_acl_return);
	assert_int_equal(acl_resp->n_acl, 0);
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
	Mgmt__GetACLResp	*acl_resp = NULL;
	struct daos_ace		*ace;

	setup_get_acl_drpc_call(&call, TEST_UUID);
	ds_mgmt_pool_get_acl_return_acl = get_valid_acl();

	/* Mangle an ACE so it can't be translated to a string */
	ace = daos_acl_get_next_ace(ds_mgmt_pool_get_acl_return_acl, NULL);
	ace->dae_access_types = 0xff; /* invalid bits */

	ds_mgmt_drpc_pool_get_acl(&call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp.body.data);

	acl_resp = mgmt__get_aclresp__unpack(NULL, resp.body.len,
					     resp.body.data);
	assert_non_null(acl_resp);
	assert_int_equal(acl_resp->status, -DER_INVAL);
	assert_int_equal(acl_resp->n_acl, 0);
}

static void
test_drpc_pool_get_acl_success(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__GetACLResp	*acl_resp = NULL;
	int			i;

	setup_get_acl_drpc_call(&call, TEST_UUID);
	ds_mgmt_pool_get_acl_return_acl = get_valid_acl();

	ds_mgmt_drpc_pool_get_acl(&call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp.body.data);

	acl_resp = mgmt__get_aclresp__unpack(NULL, resp.body.len,
					     resp.body.data);
	assert_non_null(acl_resp);
	assert_int_equal(acl_resp->status, 0);
	assert_int_equal(acl_resp->n_acl, TEST_ACES_NR);

	for (i = 0; i < TEST_ACES_NR; i++) {
		assert_string_equal(acl_resp->acl[i], TEST_ACES[i]);
	}
}

/*
 * dRPC List Pools setup/teardown
 */

static int
drpc_list_pools_setup(void **state)
{
	/* mock_ds_mgmt_list_pools_setup(); */

	return 0;
}

static int
drpc_list_pools_teardown(void **state)
{
	/* mock_ds_mgmt_list_pools_teardown(); */

	return 0;
}

/*
 * dRPC List Pools tests
 */
static void
test_drpc_list_pools_bad_request(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;
	uint8_t		bad_bytes[16];
	size_t		i;

	/* Fill out with junk that won't translate to a ListPoolsReq */
	for (i = 0; i < sizeof(bad_bytes); i++)
		bad_bytes[i] = i;

	call.body.data = bad_bytes;
	call.body.len = sizeof(bad_bytes);

	ds_mgmt_drpc_list_pools(&call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__FAILURE);
	assert_null(resp.body.data);
	assert_int_equal(resp.body.len, 0);
}

#define GET_ACL_TEST(x)	cmocka_unit_test_setup_teardown(x, \
						drpc_pool_get_acl_setup, \
						drpc_pool_get_acl_teardown)

#define LIST_POOLS_TEST(x) cmocka_unit_test_setup_teardown(x, \
						drpc_list_pools_setup, \
						drpc_list_pools_teardown)
int
main(void)
{
	const struct CMUnitTest tests[] = {
		GET_ACL_TEST(test_drpc_pool_get_acl_bad_request),
		GET_ACL_TEST(test_drpc_pool_get_acl_bad_uuid),
		GET_ACL_TEST(test_drpc_pool_get_acl_pool_svc_fails),
		GET_ACL_TEST(test_drpc_pool_get_acl_cant_translate_acl),
		GET_ACL_TEST(test_drpc_pool_get_acl_success),
		LIST_POOLS_TEST(test_drpc_list_pools_bad_request),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
