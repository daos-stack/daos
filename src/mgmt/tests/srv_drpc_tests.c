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
#include <uuid/uuid.h>
#include "../acl.pb-c.h"
#include "../pool.pb-c.h"
#include "../drpc_internal.h"
#include "mocks.h"

#define TEST_UUID	"12345678-1234-1234-1234-123456789abc"
#define TEST_ACES_NR	(3)
#ifndef UUID_STR_LEN
#define UUID_STR_LEN	37
#endif

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
	mock_ds_mgmt_pool_update_acl_setup();
	mock_ds_mgmt_pool_delete_acl_setup();

	return 0;
}

static int
drpc_pool_acl_teardown(void **state)
{
	mock_ds_mgmt_pool_get_acl_teardown();
	mock_ds_mgmt_pool_overwrite_acl_teardown();
	mock_ds_mgmt_pool_update_acl_teardown();
	mock_ds_mgmt_pool_delete_acl_teardown();

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

	assert_int_equal(resp.status, DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD);
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
expect_drpc_acl_resp_with_error(Drpc__Response *resp, int expected_err)
{
	Mgmt__ACLResp *acl_resp = NULL;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	acl_resp = mgmt__aclresp__unpack(NULL, resp->body.len,
					 resp->body.data);
	assert_non_null(acl_resp);
	assert_int_equal(acl_resp->status, expected_err);
	assert_int_equal(acl_resp->n_acl, 0);

	mgmt__aclresp__free_unpacked(acl_resp, NULL);
}

static void
test_drpc_pool_get_acl_bad_uuid(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_get_acl_drpc_call(&call, "Not a UUID at all");

	ds_mgmt_drpc_pool_get_acl(&call, &resp);

	expect_drpc_acl_resp_with_error(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_get_acl_mgmt_svc_fails(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_get_acl_drpc_call(&call, TEST_UUID);
	ds_mgmt_pool_get_acl_return = -DER_UNKNOWN;

	ds_mgmt_drpc_pool_get_acl(&call, &resp);

	expect_drpc_acl_resp_with_error(&resp, ds_mgmt_pool_get_acl_return);

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
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;
	struct daos_ace	*ace;

	setup_get_acl_drpc_call(&call, TEST_UUID);
	ds_mgmt_pool_get_acl_return_acl = get_valid_acl();

	/* Mangle an ACE so it can't be translated to a string */
	ace = daos_acl_get_next_ace(ds_mgmt_pool_get_acl_return_acl, NULL);
	ace->dae_access_types = 0xff; /* invalid bits */

	ds_mgmt_drpc_pool_get_acl(&call, &resp);

	expect_drpc_acl_resp_with_error(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
expect_drpc_acl_resp_success(Drpc__Response *resp, const char **expected_acl,
			     size_t expected_acl_nr)
{
	Mgmt__ACLResp	*acl_resp = NULL;
	size_t		i;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	acl_resp = mgmt__aclresp__unpack(NULL, resp->body.len,
					 resp->body.data);
	assert_non_null(acl_resp);
	assert_int_equal(acl_resp->status, 0);
	assert_int_equal(acl_resp->n_acl, expected_acl_nr);

	for (i = 0; i < expected_acl_nr; i++) {
		assert_string_equal(acl_resp->acl[i], expected_acl[i]);
	}

	mgmt__aclresp__free_unpacked(acl_resp, NULL);
}

static void
test_drpc_pool_get_acl_success(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_get_acl_drpc_call(&call, TEST_UUID);
	ds_mgmt_pool_get_acl_return_acl = get_valid_acl();

	ds_mgmt_drpc_pool_get_acl(&call, &resp);

	expect_drpc_acl_resp_success(&resp, TEST_ACES, TEST_ACES_NR);

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

	assert_int_equal(resp.status, DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD);
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
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_modify_acl_drpc_call(&call, "invalid UUID", TEST_ACES,
				   TEST_ACES_NR);

	ds_mgmt_drpc_pool_overwrite_acl(&call, &resp);

	expect_drpc_acl_resp_with_error(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_overwrite_acl_bad_acl(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	size_t			bad_nr = 2;
	static const char	*bad_aces[] = {"A::OWNER@:rw", "invalid"};

	setup_modify_acl_drpc_call(&call, TEST_UUID, bad_aces,
				   bad_nr);

	ds_mgmt_drpc_pool_overwrite_acl(&call, &resp);

	expect_drpc_acl_resp_with_error(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_overwrite_acl_mgmt_svc_fails(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_modify_acl_drpc_call(&call, TEST_UUID, TEST_ACES, TEST_ACES_NR);
	ds_mgmt_pool_overwrite_acl_return = -DER_UNKNOWN;

	ds_mgmt_drpc_pool_overwrite_acl(&call, &resp);

	expect_drpc_acl_resp_with_error(&resp,
					ds_mgmt_pool_overwrite_acl_return);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_overwrite_acl_success(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_modify_acl_drpc_call(&call, TEST_UUID, TEST_ACES, TEST_ACES_NR);

	/*
	 * Set up the mgmt svc overwrite function to return the same ACEs
	 * we passed in as its result.
	 */
	assert_int_equal(daos_acl_from_strs(TEST_ACES, TEST_ACES_NR,
			 &ds_mgmt_pool_overwrite_acl_result), 0);

	ds_mgmt_drpc_pool_overwrite_acl(&call, &resp);

	expect_drpc_acl_resp_success(&resp, TEST_ACES, TEST_ACES_NR);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

/*
 * dRPC Update ACL tests
 */
static void
test_drpc_pool_update_acl_bad_request(void **state)
{
	Drpc__Call	*call;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	call = new_drpc_call_with_bad_body();

	ds_mgmt_drpc_pool_update_acl(call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD);
	assert_null(resp.body.data);
	assert_int_equal(resp.body.len, 0);

	drpc_call_free(call);
}

static void
test_drpc_pool_update_acl_bad_uuid(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_modify_acl_drpc_call(&call, "invalid UUID", TEST_ACES,
				   TEST_ACES_NR);

	ds_mgmt_drpc_pool_update_acl(&call, &resp);

	expect_drpc_acl_resp_with_error(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_update_acl_bad_acl(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	size_t			bad_nr = 2;
	static const char	*bad_aces[] = {"A::OWNER@:rw", "invalid"};

	setup_modify_acl_drpc_call(&call, TEST_UUID, bad_aces,
				   bad_nr);

	ds_mgmt_drpc_pool_update_acl(&call, &resp);

	expect_drpc_acl_resp_with_error(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_update_acl_mgmt_svc_fails(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_modify_acl_drpc_call(&call, TEST_UUID, TEST_ACES, TEST_ACES_NR);
	ds_mgmt_pool_update_acl_return = -DER_UNKNOWN;

	ds_mgmt_drpc_pool_update_acl(&call, &resp);

	expect_drpc_acl_resp_with_error(&resp, ds_mgmt_pool_update_acl_return);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_update_acl_success(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_modify_acl_drpc_call(&call, TEST_UUID, TEST_ACES, TEST_ACES_NR);

	/*
	 * Set up the mgmt svc update function to return the same ACEs
	 * we passed in as its result. Arbitrary.
	 */
	assert_int_equal(daos_acl_from_strs(TEST_ACES, TEST_ACES_NR,
			 &ds_mgmt_pool_update_acl_result), 0);

	ds_mgmt_drpc_pool_update_acl(&call, &resp);

	expect_drpc_acl_resp_success(&resp, TEST_ACES, TEST_ACES_NR);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

/*
 * dRPC Delete ACL tests
 */
static void
test_drpc_pool_delete_acl_bad_request(void **state)
{
	Drpc__Call	*call;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	call = new_drpc_call_with_bad_body();

	ds_mgmt_drpc_pool_delete_acl(call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD);
	assert_null(resp.body.data);
	assert_int_equal(resp.body.len, 0);

	drpc_call_free(call);
}

static void
pack_delete_acl_req(Drpc__Call *call, Mgmt__DeleteACLReq *req)
{
	size_t	len;
	uint8_t	*body;

	len = mgmt__delete_aclreq__get_packed_size(req);
	D_ALLOC(body, len);
	assert_non_null(body);

	mgmt__delete_aclreq__pack(req, body);

	call->body.data = body;
	call->body.len = len;
}

static void
setup_delete_acl_drpc_call(Drpc__Call *call, char *uuid, char *principal)
{
	Mgmt__DeleteACLReq req = MGMT__DELETE_ACLREQ__INIT;

	req.uuid = uuid;
	req.principal = principal;

	pack_delete_acl_req(call, &req);
}

static void
test_drpc_pool_delete_acl_bad_uuid(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_delete_acl_drpc_call(&call, "invalid UUID", "OWNER@");

	ds_mgmt_drpc_pool_delete_acl(&call, &resp);

	expect_drpc_acl_resp_with_error(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_delete_acl_mgmt_svc_fails(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_delete_acl_drpc_call(&call, TEST_UUID, "OWNER@");
	ds_mgmt_pool_delete_acl_return = -DER_UNKNOWN;

	ds_mgmt_drpc_pool_delete_acl(&call, &resp);

	expect_drpc_acl_resp_with_error(&resp, ds_mgmt_pool_delete_acl_return);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_delete_acl_success(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_delete_acl_drpc_call(&call, TEST_UUID, "OWNER@");

	assert_int_equal(daos_acl_from_strs(TEST_ACES, TEST_ACES_NR,
			 &ds_mgmt_pool_delete_acl_result), 0);

	ds_mgmt_drpc_pool_delete_acl(&call, &resp);

	expect_drpc_acl_resp_success(&resp, TEST_ACES, TEST_ACES_NR);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

/*
 * dRPC List Pools setup/teardown
 */

static int
drpc_list_pools_setup(void **state)
{
	mock_ds_mgmt_list_pools_setup();

	return 0;
}

static int
drpc_list_pools_teardown(void **state)
{
	mock_ds_mgmt_list_pools_teardown();

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

	assert_int_equal(resp.status, DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD);
	assert_null(resp.body.data);
	assert_int_equal(resp.body.len, 0);
}

static void
setup_list_pools_drpc_call(Drpc__Call *call, char *sys_name)
{
	Mgmt__ListPoolsReq	req = MGMT__LIST_POOLS_REQ__INIT;
	size_t			len;
	uint8_t			*body;

	req.sys = sys_name;

	len = mgmt__list_pools_req__get_packed_size(&req);
	D_ALLOC(body, len);
	assert_non_null(body);

	mgmt__list_pools_req__pack(&req, body);

	call->body.data = body;
	call->body.len = len;
}

static void
expect_drpc_list_pools_resp_with_error(Drpc__Response *resp, int expected_err)
{
	Mgmt__ListPoolsResp *pool_resp = NULL;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	pool_resp = mgmt__list_pools_resp__unpack(NULL, resp->body.len,
						 resp->body.data);
	assert_non_null(pool_resp);
	assert_int_equal(pool_resp->status, expected_err);
	assert_int_equal(pool_resp->n_pools, 0);

	mgmt__list_pools_resp__free_unpacked(pool_resp, NULL);
}

static void
test_drpc_list_pools_mgmt_svc_fails(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_list_pools_drpc_call(&call, "DaosSys");

	ds_mgmt_list_pools_return = -DER_UNKNOWN;

	ds_mgmt_drpc_list_pools(&call, &resp);

	expect_drpc_list_pools_resp_with_error(&resp,
					       ds_mgmt_list_pools_return);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_list_pools_svc_results_invalid(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_list_pools_drpc_call(&call, "DaosSys");

	/* has length but pools is null - something weird happened */
	ds_mgmt_list_pools_len_out = 2;

	ds_mgmt_drpc_list_pools(&call, &resp);

	expect_drpc_list_pools_resp_with_error(&resp, -DER_UNKNOWN);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
expect_drpc_list_pools_resp_with_pools(Drpc__Response *resp,
				       struct mgmt_list_pools_one *exp_pools,
				       size_t exp_pools_len)
{
	Mgmt__ListPoolsResp	*pool_resp = NULL;
	size_t			i, j;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	pool_resp = mgmt__list_pools_resp__unpack(NULL, resp->body.len,
						 resp->body.data);
	assert_non_null(pool_resp);
	assert_int_equal(pool_resp->status, 0);
	assert_int_equal(pool_resp->n_pools, exp_pools_len);

	for (i = 0; i < exp_pools_len; i++) {
		char	exp_uuid[UUID_STR_LEN];

		uuid_unparse(exp_pools[i].lp_puuid, exp_uuid);
		assert_string_equal(pool_resp->pools[i]->uuid, exp_uuid);

		assert_int_equal(pool_resp->pools[i]->n_svcreps,
				 exp_pools[i].lp_svc->rl_nr);
		for (j = 0; j < exp_pools[i].lp_svc->rl_nr; j++)
			assert_int_equal(pool_resp->pools[i]->svcreps[j],
					 exp_pools[i].lp_svc->rl_ranks[j]);
	}

	mgmt__list_pools_resp__free_unpacked(pool_resp, NULL);
}

static void
test_drpc_list_pools_success_no_pools(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;
	char		*exp_sys_name = "daos_sys";

	setup_list_pools_drpc_call(&call, exp_sys_name);

	ds_mgmt_drpc_list_pools(&call, &resp);

	expect_drpc_list_pools_resp_with_pools(&resp, NULL, 0);

	/* Check mgmt svc inputs */
	assert_string_equal(ds_mgmt_list_pools_group, exp_sys_name);
	assert_null(ds_mgmt_list_pools_npools_ptr); /* want all pools */
	assert_non_null(ds_mgmt_list_pools_poolsp_ptr);
	assert_non_null(ds_mgmt_list_pools_len_ptr);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_list_pools_success_with_pools(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_list_pools_drpc_call(&call, "DaosSys");
	mock_ds_mgmt_list_pools_gen_pools(5);

	/* Add a couple additional ranks to some pools */
	d_rank_list_append(ds_mgmt_list_pools_poolsp_out[0].lp_svc, 3);
	d_rank_list_append(ds_mgmt_list_pools_poolsp_out[2].lp_svc, 6);
	d_rank_list_append(ds_mgmt_list_pools_poolsp_out[2].lp_svc, 7);

	ds_mgmt_drpc_list_pools(&call, &resp);

	expect_drpc_list_pools_resp_with_pools(&resp,
					       ds_mgmt_list_pools_poolsp_out,
					       ds_mgmt_list_pools_len_out);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

/*
 * dRPC List Containers setup/teardown
 */

static int
drpc_list_cont_setup(void **state)
{
	mock_ds_mgmt_pool_list_cont_setup();

	return 0;
}

static int
drpc_list_cont_teardown(void **state)
{
	mock_ds_mgmt_pool_list_cont_teardown();

	return 0;
}

/*
 * dRPC List Containers tests
 */
static void
pack_list_cont_req(Drpc__Call *call, Mgmt__ListContReq *req)
{
	size_t	len;
	uint8_t	*body;

	len = mgmt__list_cont_req__get_packed_size(req);
	D_ALLOC(body, len);
	assert_non_null(body);

	mgmt__list_cont_req__pack(req, body);

	call->body.data = body;
	call->body.len = len;
}

static void
setup_list_cont_drpc_call(Drpc__Call *call, char *uuid)
{
	Mgmt__ListContReq lc_req = MGMT__LIST_CONT_REQ__INIT;

	lc_req.uuid = uuid;
	pack_list_cont_req(call, &lc_req);
}

static void
expect_drpc_list_cont_resp_with_error(Drpc__Response *resp,
				      int expected_err)
{
	Mgmt__ListContResp *lc_resp = NULL;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	lc_resp = mgmt__list_cont_resp__unpack(NULL, resp->body.len,
					      resp->body.data);
	assert_non_null(lc_resp);

	assert_int_equal(lc_resp->status, expected_err);

	mgmt__list_cont_resp__free_unpacked(lc_resp, NULL);
}

static void
expect_drpc_list_cont_resp_with_containers(Drpc__Response *resp,
					   struct daos_pool_cont_info *exp_cont,
					   uint64_t exp_cont_len)
{
	Mgmt__ListContResp	*cont_resp = NULL;
	size_t			 i;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	cont_resp = mgmt__list_cont_resp__unpack(NULL, resp->body.len,
						 resp->body.data);
	assert_non_null(cont_resp);
	assert_int_equal(cont_resp->status, 0);

	/* number of containers in response list == expected value. */
	assert_int_equal(cont_resp->n_containers, exp_cont_len);

	for (i = 0; i < exp_cont_len; i++) {
		char exp_uuid[DAOS_UUID_STR_SIZE];

		uuid_unparse(exp_cont[i].pci_uuid, exp_uuid);
		assert_string_equal(cont_resp->containers[i]->uuid, exp_uuid);
	}
}

static void
test_drpc_list_cont_bad_request(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;
	uint8_t		bad_bytes[16];
	size_t		i;

	/* Fill out with junk that won't translate to a ListContReq */
	for (i = 0; i < sizeof(bad_bytes); i++)
		bad_bytes[i] = i;

	call.body.data = bad_bytes;
	call.body.len = sizeof(bad_bytes);

	ds_mgmt_drpc_pool_list_cont(&call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD);
	assert_null(resp.body.data);
	assert_int_equal(resp.body.len, 0);
}

static void
test_drpc_pool_list_cont_bad_uuid(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_list_cont_drpc_call(&call, "invalid UUID");

	ds_mgmt_drpc_pool_list_cont(&call, &resp);

	expect_drpc_list_cont_resp_with_error(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_list_cont_mgmt_svc_fails(void **state)
{
	Drpc__Call	 call = DRPC__CALL__INIT;
	Drpc__Response	 resp = DRPC__RESPONSE__INIT;

	setup_list_cont_drpc_call(&call, TEST_UUID);
	ds_mgmt_pool_list_cont_return = -DER_UNKNOWN;

	ds_mgmt_drpc_pool_list_cont(&call, &resp);

	expect_drpc_list_cont_resp_with_error(&resp,
					      ds_mgmt_pool_list_cont_return);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_list_cont_no_containers(void **state)
{
	Drpc__Call	 call = DRPC__CALL__INIT;
	Drpc__Response	 resp = DRPC__RESPONSE__INIT;

	setup_list_cont_drpc_call(&call, TEST_UUID);

	ds_mgmt_drpc_pool_list_cont(&call, &resp);

	expect_drpc_list_cont_resp_with_containers(&resp, NULL, 0);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_list_cont_with_containers(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;
	const size_t	ncont = 64;

	setup_list_cont_drpc_call(&call, TEST_UUID);
	mock_ds_mgmt_list_cont_gen_cont(ncont);

	ds_mgmt_drpc_pool_list_cont(&call, &resp);

	expect_drpc_list_cont_resp_with_containers(&resp,
						   ds_mgmt_pool_list_cont_out,
						   ncont);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

#define ACL_TEST(x)	cmocka_unit_test_setup_teardown(x, \
						drpc_pool_acl_setup, \
						drpc_pool_acl_teardown)

#define LIST_POOLS_TEST(x) cmocka_unit_test_setup_teardown(x, \
						drpc_list_pools_setup, \
						drpc_list_pools_teardown)

#define LIST_CONT_TEST(x) cmocka_unit_test_setup_teardown(x, \
						drpc_list_cont_setup, \
						drpc_list_cont_teardown)

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
		ACL_TEST(test_drpc_pool_update_acl_bad_request),
		ACL_TEST(test_drpc_pool_update_acl_bad_uuid),
		ACL_TEST(test_drpc_pool_update_acl_bad_acl),
		ACL_TEST(test_drpc_pool_update_acl_mgmt_svc_fails),
		ACL_TEST(test_drpc_pool_update_acl_success),
		ACL_TEST(test_drpc_pool_delete_acl_bad_request),
		ACL_TEST(test_drpc_pool_delete_acl_bad_uuid),
		ACL_TEST(test_drpc_pool_delete_acl_mgmt_svc_fails),
		ACL_TEST(test_drpc_pool_delete_acl_success),
		LIST_POOLS_TEST(test_drpc_list_pools_bad_request),
		LIST_POOLS_TEST(test_drpc_list_pools_mgmt_svc_fails),
		LIST_POOLS_TEST(test_drpc_list_pools_svc_results_invalid),
		LIST_POOLS_TEST(test_drpc_list_pools_success_no_pools),
		LIST_POOLS_TEST(test_drpc_list_pools_success_with_pools),
		LIST_CONT_TEST(test_drpc_list_cont_bad_request),
		LIST_CONT_TEST(test_drpc_pool_list_cont_bad_uuid),
		LIST_CONT_TEST(test_drpc_pool_list_cont_mgmt_svc_fails),
		LIST_CONT_TEST(test_drpc_pool_list_cont_no_containers),
		LIST_CONT_TEST(test_drpc_pool_list_cont_with_containers),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
