/*
 * (C) Copyright 2019-2020 Intel Corporation.
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
#include <daos_pool.h>
#include <daos_security.h>
#include <uuid/uuid.h>
#include "../acl.pb-c.h"
#include "../pool.pb-c.h"
#include "../cont.pb-c.h"
#include "../srv.pb-c.h"
#include "../drpc_internal.h"
#include "mocks.h"

#define TEST_UUID	"12345678-1234-1234-1234-123456789abc"
#define TEST_OWNER	"test_root@"
#define TEST_GROUP	"test_admins@"
#define TEST_ACES_NR	(3)
#ifndef UUID_STR_LEN
#define UUID_STR_LEN	37
#endif

static uint32_t	TEST_IDXS[] = {0, 1, 2};
static uint32_t	TEST_RANKS[] = {0, 1, 2};
static const char	*TEST_ACES[] = {"A::OWNER@:rw",
					"A::niceuser@:rw",
					"A:G:GROUP@:r"};

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
expect_failure_for_bad_call_payload(drpc_handler_t func)
{
	Drpc__Call	*call;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	call = new_drpc_call_with_bad_body();

	func(call, &resp);

	assert_int_equal(resp.status, DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD);
	assert_null(resp.body.data);
	assert_int_equal(resp.body.len, 0);

	drpc_call_free(call);
}

static void
test_mgmt_drpc_handlers_bad_call_payload(void **state)
{
	/*
	 * Any dRPC call that accepts an input payload should be added here
	 * to test for proper handling of garbage in the payload
	 */
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_prep_shutdown);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_ping_rank);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_set_rank);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_pool_create);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_pool_extend);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_pool_reintegrate);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_pool_destroy);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_pool_evict);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_pool_exclude);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_pool_drain);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_pool_get_acl);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_pool_overwrite_acl);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_pool_update_acl);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_pool_delete_acl);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_pool_query);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_smd_list_devs);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_smd_list_pools);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_bio_health_query);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_pool_list_cont);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_pool_set_prop);
	expect_failure_for_bad_call_payload(ds_mgmt_drpc_cont_set_owner);
}

static daos_prop_t *
new_access_prop(struct daos_acl *acl, const char *owner, const char *group)
{
	daos_prop_t		*prop;
	struct daos_prop_entry	*entry = NULL;
	size_t			num_entries = 0;

	if (acl != NULL)
		num_entries++;

	if (owner != NULL)
		num_entries++;

	if (group != NULL)
		num_entries++;

	if (num_entries == 0)
		return NULL;

	prop = daos_prop_alloc(num_entries);
	entry = &(prop->dpp_entries[0]);

	if (acl != NULL) {
		entry->dpe_type = DAOS_PROP_PO_ACL;
		entry->dpe_val_ptr = daos_acl_dup(acl);
		entry++;
	}

	if (owner != NULL) {
		entry->dpe_type = DAOS_PROP_PO_OWNER;
		D_STRNDUP(entry->dpe_str, owner, DAOS_ACL_MAX_PRINCIPAL_LEN);
		entry++;
	}

	if (group != NULL) {
		entry->dpe_type = DAOS_PROP_PO_OWNER_GROUP;
		D_STRNDUP(entry->dpe_str, group, DAOS_ACL_MAX_PRINCIPAL_LEN);
		entry++;
	}

	return prop;
}

static struct daos_acl *
get_valid_acl(void)
{
	struct daos_acl	*acl = NULL;

	assert_int_equal(daos_acl_from_strs(TEST_ACES, TEST_ACES_NR, &acl), 0);

	return acl;
}

static daos_prop_t *
default_access_prop(void)
{
	daos_prop_t	*prop;
	struct daos_acl	*acl;

	acl = get_valid_acl();
	prop = new_access_prop(acl, TEST_OWNER, TEST_GROUP);
	daos_acl_free(acl);
	return prop;
}

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
	ds_mgmt_pool_get_acl_return = -DER_MISC;

	ds_mgmt_drpc_pool_get_acl(&call, &resp);

	expect_drpc_acl_resp_with_error(&resp, ds_mgmt_pool_get_acl_return);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_get_acl_cant_translate_acl(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;
	struct daos_acl	*acl;
	struct daos_ace	*ace;

	setup_get_acl_drpc_call(&call, TEST_UUID);

	/* Mangle an ACE so it can't be translated to a string */
	acl = get_valid_acl();
	ace = daos_acl_get_next_ace(acl, NULL);
	ace->dae_access_types = 0xff; /* invalid bits */

	ds_mgmt_pool_get_acl_return_acl = new_access_prop(acl, TEST_OWNER,
							  TEST_GROUP);

	ds_mgmt_drpc_pool_get_acl(&call, &resp);

	expect_drpc_acl_resp_with_error(&resp, -DER_INVAL);

	daos_acl_free(acl);
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
	ds_mgmt_pool_get_acl_return_acl = default_access_prop();

	ds_mgmt_drpc_pool_get_acl(&call, &resp);

	expect_drpc_acl_resp_success(&resp, TEST_ACES, TEST_ACES_NR);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

/*
 * dRPC overwrite ACL tests
 */

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
	ds_mgmt_pool_overwrite_acl_return = -DER_MISC;

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
	ds_mgmt_pool_overwrite_acl_result = default_access_prop();

	ds_mgmt_drpc_pool_overwrite_acl(&call, &resp);

	expect_drpc_acl_resp_success(&resp, TEST_ACES, TEST_ACES_NR);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

/*
 * dRPC Update ACL tests
 */

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
	ds_mgmt_pool_update_acl_return = -DER_MISC;

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
	ds_mgmt_pool_update_acl_result = default_access_prop();

	ds_mgmt_drpc_pool_update_acl(&call, &resp);

	expect_drpc_acl_resp_success(&resp, TEST_ACES, TEST_ACES_NR);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

/*
 * dRPC Delete ACL tests
 */

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
	ds_mgmt_pool_delete_acl_return = -DER_MISC;

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

	ds_mgmt_pool_delete_acl_result = default_access_prop();

	ds_mgmt_drpc_pool_delete_acl(&call, &resp);

	expect_drpc_acl_resp_success(&resp, TEST_ACES, TEST_ACES_NR);

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
	ds_mgmt_pool_list_cont_return = -DER_MISC;

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

/*
 * dRPC Pool SetProp setup/teardown
 */

static int
drpc_pool_set_prop_setup(void **state)
{
	mock_ds_mgmt_pool_set_prop_setup();

	return 0;
}

static int
drpc_pool_set_prop_teardown(void **state)
{
	mock_ds_mgmt_pool_set_prop_teardown();

	return 0;
}

/*
 * dRPC Pool SetProp tests
 */

static void
setup_pool_set_prop_drpc_call(Drpc__Call *call, Mgmt__PoolSetPropReq *req)
{
	size_t			len;
	uint8_t			*body;

	len = mgmt__pool_set_prop_req__get_packed_size(req);
	D_ALLOC(body, len);
	assert_non_null(body);

	mgmt__pool_set_prop_req__pack(req, body);

	call->body.data = body;
	call->body.len = len;
}

static void
expect_drpc_pool_set_prop_resp_with_error(Drpc__Response *resp,
					int expected_err)
{
	Mgmt__PoolSetPropResp *set_prop_resp = NULL;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	set_prop_resp = mgmt__pool_set_prop_resp__unpack(NULL,
			resp->body.len, resp->body.data);
	assert_non_null(set_prop_resp);
	assert_int_equal(set_prop_resp->status, expected_err);

	mgmt__pool_set_prop_resp__free_unpacked(set_prop_resp, NULL);
}

static void
test_drpc_pool_set_prop_invalid_property_type(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__PoolSetPropReq	req = MGMT__POOL_SET_PROP_REQ__INIT;

	req.uuid = TEST_UUID;
	/* make the value valid to ensure we're testing the property */
	req.numval = 1;
	req.value_case = MGMT__POOL_SET_PROP_REQ__VALUE_NUMVAL;
	setup_pool_set_prop_drpc_call(&call, &req);

	ds_mgmt_drpc_pool_set_prop(&call, &resp);

	expect_drpc_pool_set_prop_resp_with_error(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_set_prop_invalid_value_type(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__PoolSetPropReq	req = MGMT__POOL_SET_PROP_REQ__INIT;

	req.uuid = TEST_UUID;
	req.number = 1; /* doesn't matter */
	req.property_case = MGMT__POOL_SET_PROP_REQ__PROPERTY_NUMBER;
	setup_pool_set_prop_drpc_call(&call, &req);

	ds_mgmt_drpc_pool_set_prop(&call, &resp);

	expect_drpc_pool_set_prop_resp_with_error(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_set_prop_bad_uuid(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__PoolSetPropReq	req = MGMT__POOL_SET_PROP_REQ__INIT;

	req.uuid = "wow this won't work";
	req.number = 1; /* doesn't matter */
	req.property_case = MGMT__POOL_SET_PROP_REQ__PROPERTY_NUMBER;
	setup_pool_set_prop_drpc_call(&call, &req);

	ds_mgmt_drpc_pool_set_prop(&call, &resp);

	expect_drpc_pool_set_prop_resp_with_error(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
expect_drpc_pool_set_prop_resp_success(Drpc__Response *resp,
				       int prop_number, int val_number)
{
	Mgmt__PoolSetPropResp	*setprop_resp = NULL;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	setprop_resp = mgmt__pool_set_prop_resp__unpack(NULL, resp->body.len,
					 resp->body.data);
	assert_non_null(setprop_resp);
	assert_int_equal(setprop_resp->status, 0);
	assert_int_equal(setprop_resp->number, prop_number);
	assert_int_equal(setprop_resp->numval, val_number);

	mgmt__pool_set_prop_resp__free_unpacked(setprop_resp, NULL);
}

static void
test_drpc_pool_set_prop_success(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__PoolSetPropReq	req = MGMT__POOL_SET_PROP_REQ__INIT;
	daos_prop_t		*exp_result;
	int			prop_number = DAOS_PROP_PO_MAX;
	int			val_number = 1;

	req.uuid = TEST_UUID;
	req.number = prop_number;
	req.property_case = MGMT__POOL_SET_PROP_REQ__PROPERTY_NUMBER;
	req.numval = val_number;
	req.value_case = MGMT__POOL_SET_PROP_REQ__VALUE_NUMVAL;
	setup_pool_set_prop_drpc_call(&call, &req);

	exp_result = daos_prop_alloc(1);
	exp_result->dpp_entries[0].dpe_type = prop_number;
	exp_result->dpp_entries[0].dpe_val = val_number;
	ds_mgmt_pool_set_prop_result = exp_result;

	ds_mgmt_drpc_pool_set_prop(&call, &resp);

	expect_drpc_pool_set_prop_resp_success(&resp, prop_number, val_number);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

/*
 * Pool query test setup
 */
static int
drpc_pool_query_setup(void **state)
{
	mock_ds_mgmt_pool_query_setup();
	return 0;
}

/*
 * dRPC pool query tests
 */
static void
pack_pool_query_req(Drpc__Call *call, Mgmt__PoolQueryReq *req)
{
	size_t	len;
	uint8_t	*body;

	len = mgmt__pool_query_req__get_packed_size(req);
	D_ALLOC(body, len);
	assert_non_null(body);

	mgmt__pool_query_req__pack(req, body);

	call->body.data = body;
	call->body.len = len;
}

static void
setup_pool_query_drpc_call(Drpc__Call *call, char *uuid)
{
	Mgmt__PoolQueryReq req = MGMT__POOL_QUERY_REQ__INIT;

	req.uuid = uuid;
	pack_pool_query_req(call, &req);
}

static void
expect_drpc_pool_query_resp_with_error(Drpc__Response *resp, int expected_err)
{
	Mgmt__PoolQueryResp *pq_resp = NULL;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	pq_resp = mgmt__pool_query_resp__unpack(NULL, resp->body.len,
						resp->body.data);
	assert_non_null(pq_resp);
	assert_int_equal(pq_resp->status, expected_err);

	mgmt__pool_query_resp__free_unpacked(pq_resp, NULL);
}

static void
test_drpc_pool_query_bad_uuid(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_pool_query_drpc_call(&call, "BAD");

	ds_mgmt_drpc_pool_query(&call, &resp);

	expect_drpc_pool_query_resp_with_error(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_query_mgmt_svc_fails(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_pool_query_drpc_call(&call, TEST_UUID);
	ds_mgmt_pool_query_return = -DER_MISC;

	ds_mgmt_drpc_pool_query(&call, &resp);

	expect_drpc_pool_query_resp_with_error(&resp,
					       ds_mgmt_pool_query_return);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
init_test_pool_info(daos_pool_info_t *pool_info)
{
	/* Set up pool info to be returned */
	uuid_parse(TEST_UUID, pool_info->pi_uuid);

	pool_info->pi_bits = DPI_ALL;

	/* Values are arbitrary, just want to see that they are copied over */
	pool_info->pi_ntargets = 100;
	pool_info->pi_ndisabled = 36;

	pool_info->pi_space.ps_ntargets = 51;

	pool_info->pi_space.ps_space.s_total[DAOS_MEDIA_SCM] = 1;
	pool_info->pi_space.ps_space.s_free[DAOS_MEDIA_SCM] = 2;
	pool_info->pi_space.ps_free_max[DAOS_MEDIA_SCM] = 3;
	pool_info->pi_space.ps_free_min[DAOS_MEDIA_SCM] = 4;
	pool_info->pi_space.ps_free_mean[DAOS_MEDIA_SCM] = 5;

	pool_info->pi_space.ps_space.s_total[DAOS_MEDIA_NVME] = 6;
	pool_info->pi_space.ps_space.s_free[DAOS_MEDIA_NVME] = 7;
	pool_info->pi_space.ps_free_max[DAOS_MEDIA_NVME] = 8;
	pool_info->pi_space.ps_free_min[DAOS_MEDIA_NVME] = 9;
	pool_info->pi_space.ps_free_mean[DAOS_MEDIA_NVME] = 10;
}

static void
init_test_rebuild_status(struct daos_rebuild_status *rebuild)
{
	rebuild->rs_obj_nr = 101;
	rebuild->rs_rec_nr = 102;
}

static void
expect_storage_usage(struct daos_pool_space *exp, int media_type,
		     Mgmt__StorageUsageStats *actual)
{
	assert_int_equal(actual->total,
			 exp->ps_space.s_total[media_type]);
	assert_int_equal(actual->free,
			 exp->ps_space.s_free[media_type]);
	assert_int_equal(actual->max,
			 exp->ps_free_max[media_type]);
	assert_int_equal(actual->min,
			 exp->ps_free_min[media_type]);
	assert_int_equal(actual->mean,
			 exp->ps_free_mean[media_type]);
}

static void
expect_rebuild_status(struct daos_rebuild_status *exp,
		      Mgmt__PoolRebuildStatus__State exp_state,
		      Mgmt__PoolRebuildStatus *actual)
{
	assert_int_equal(actual->status, exp->rs_errno);
	assert_int_equal(actual->objects, exp->rs_obj_nr);
	assert_int_equal(actual->records, exp->rs_rec_nr);
	assert_int_equal(actual->state, exp_state);
}

static void
expect_query_resp_with_info(daos_pool_info_t *exp_info,
			    Mgmt__PoolRebuildStatus__State exp_state,
			    Drpc__Response *resp)
{
	Mgmt__PoolQueryResp	*pq_resp = NULL;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	pq_resp = mgmt__pool_query_resp__unpack(NULL, resp->body.len,
						resp->body.data);
	assert_non_null(pq_resp);
	assert_int_equal(pq_resp->status, 0);
	assert_string_equal(pq_resp->uuid, TEST_UUID);
	assert_int_equal(pq_resp->totaltargets, exp_info->pi_ntargets);
	assert_int_equal(pq_resp->disabledtargets, exp_info->pi_ndisabled);
	assert_int_equal(pq_resp->activetargets,
			 exp_info->pi_space.ps_ntargets);

	assert_non_null(pq_resp->scm);
	expect_storage_usage(&exp_info->pi_space, DAOS_MEDIA_SCM, pq_resp->scm);

	assert_non_null(pq_resp->nvme);
	expect_storage_usage(&exp_info->pi_space, DAOS_MEDIA_NVME,
			     pq_resp->nvme);

	assert_non_null(pq_resp->rebuild);
	expect_rebuild_status(&exp_info->pi_rebuild_st, exp_state,
			      pq_resp->rebuild);

	mgmt__pool_query_resp__free_unpacked(pq_resp, NULL);
}

static void
test_drpc_pool_query_success(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	uuid_t			exp_uuid;
	daos_pool_info_t	exp_info = {0};

	init_test_pool_info(&exp_info);
	init_test_rebuild_status(&exp_info.pi_rebuild_st);
	ds_mgmt_pool_query_info_out = exp_info;

	setup_pool_query_drpc_call(&call, TEST_UUID);

	ds_mgmt_drpc_pool_query(&call, &resp);

	/* Make sure inputs to the mgmt svc were sane */
	uuid_parse(TEST_UUID, exp_uuid);
	assert_int_equal(uuid_compare(exp_uuid, ds_mgmt_pool_query_uuid), 0);
	assert_non_null(ds_mgmt_pool_query_info_ptr);
	assert_int_equal(ds_mgmt_pool_query_info_in.pi_bits, DPI_ALL);

	expect_query_resp_with_info(&exp_info,
				    MGMT__POOL_REBUILD_STATUS__STATE__IDLE,
				    &resp);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_query_success_rebuild_busy(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	daos_pool_info_t	exp_info = {0};

	init_test_pool_info(&exp_info);
	init_test_rebuild_status(&exp_info.pi_rebuild_st);
	exp_info.pi_rebuild_st.rs_version = 1;
	ds_mgmt_pool_query_info_out = exp_info;

	setup_pool_query_drpc_call(&call, TEST_UUID);

	ds_mgmt_drpc_pool_query(&call, &resp);

	expect_query_resp_with_info(&exp_info,
				    MGMT__POOL_REBUILD_STATUS__STATE__BUSY,
				    &resp);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_query_success_rebuild_done(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	daos_pool_info_t	exp_info = {0};

	init_test_pool_info(&exp_info);
	init_test_rebuild_status(&exp_info.pi_rebuild_st);
	exp_info.pi_rebuild_st.rs_version = 1;
	exp_info.pi_rebuild_st.rs_done = 1;
	ds_mgmt_pool_query_info_out = exp_info;

	setup_pool_query_drpc_call(&call, TEST_UUID);

	ds_mgmt_drpc_pool_query(&call, &resp);

	expect_query_resp_with_info(&exp_info,
				    MGMT__POOL_REBUILD_STATUS__STATE__DONE,
				    &resp);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_query_success_rebuild_err(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	daos_pool_info_t	exp_info = {0};

	init_test_pool_info(&exp_info);
	exp_info.pi_rebuild_st.rs_version = 1;
	exp_info.pi_rebuild_st.rs_errno = -DER_MISC;

	ds_mgmt_pool_query_info_out = exp_info;
	/*
	 * rebuild results returned to us shouldn't include the number of
	 * objects/records if there's an error.
	 */
	ds_mgmt_pool_query_info_out.pi_rebuild_st.rs_obj_nr = 42;
	ds_mgmt_pool_query_info_out.pi_rebuild_st.rs_rec_nr = 999;

	setup_pool_query_drpc_call(&call, TEST_UUID);

	ds_mgmt_drpc_pool_query(&call, &resp);

	expect_query_resp_with_info(&exp_info,
				    MGMT__POOL_REBUILD_STATUS__STATE__IDLE,
				    &resp);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

/*
 * dRPC pool create tests
 */
static void
expect_create_resp_with_error(Drpc__Response *resp, int exp_error)
{
	Mgmt__PoolCreateResp	*pc_resp = NULL;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	pc_resp = mgmt__pool_create_resp__unpack(NULL, resp->body.len,
						 resp->body.data);
	assert_non_null(pc_resp);
	assert_int_equal(pc_resp->status, exp_error);

	mgmt__pool_create_resp__free_unpacked(pc_resp, NULL);
}

static void
pack_pool_create_req(Mgmt__PoolCreateReq *req, Drpc__Call *call)
{
	size_t	len;
	uint8_t	*body;

	len = mgmt__pool_create_req__get_packed_size(req);
	D_ALLOC(body, len);
	assert_non_null(body);

	mgmt__pool_create_req__pack(req, body);

	call->body.data = body;
	call->body.len = len;
}

static void
test_drpc_pool_create_invalid_acl(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__PoolCreateReq	pc_req = MGMT__POOL_CREATE_REQ__INIT;
	size_t			num_acl = 2;
	size_t			i;
	char			**bad_acl;
	const char		*ace = "A::myuser@:rw"; /* to be duplicated */

	pc_req.uuid = TEST_UUID;

	/* Duplicate entries in the ACL should be invalid */
	D_ALLOC_ARRAY(bad_acl, num_acl);
	assert_non_null(bad_acl);
	for (i = 0; i < num_acl; i++) {
		D_STRNDUP(bad_acl[i], ace, DAOS_ACL_MAX_ACE_STR_LEN);
		assert_non_null(bad_acl[i]);
	}

	pc_req.n_acl = num_acl;
	pc_req.acl = bad_acl;

	pack_pool_create_req(&pc_req, &call);

	ds_mgmt_drpc_pool_create(&call, &resp);

	expect_create_resp_with_error(&resp, -DER_INVAL);

	/* clean up */
	for (i = 0; i < num_acl; i++) {
		D_FREE(bad_acl[i]);
	}
	D_FREE(bad_acl);
	D_FREE(call.body.data);
	D_FREE(resp.body.data);

}

/*
 * Pool exclude test setup
 */
static int
drpc_exclude_setup(void **state)
{
	mock_ds_mgmt_tgt_update_setup();
	return 0;
}

/*
 * dRPC pool exclude tests
 */
static void
pack_pool_exclude_req(Drpc__Call *call, Mgmt__PoolExcludeReq *req)
{
	size_t	len;
	uint8_t	*body;

	len = mgmt__pool_exclude_req__get_packed_size(req);
	D_ALLOC(body, len);
	assert_non_null(body);

	mgmt__pool_exclude_req__pack(req, body);

	call->body.data = body;
	call->body.len = len;
}

static void
setup_exclude_drpc_call(Drpc__Call *call, char *uuid, uint32_t rank)
{
	Mgmt__PoolExcludeReq req = MGMT__POOL_EXCLUDE_REQ__INIT;

	req.uuid = uuid;
	req.n_targetidx = 3;
	req.rank = rank;
	req.targetidx = TEST_IDXS;
	pack_pool_exclude_req(call, &req);
}

static void
expect_drpc_exclude_resp_with_error(Drpc__Response *resp, int exp_error)
{
	Mgmt__PoolExcludeResp	*pc_resp = NULL;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	pc_resp = mgmt__pool_exclude_resp__unpack(NULL, resp->body.len,
						  resp->body.data);
	assert_non_null(pc_resp);
	assert_int_equal(pc_resp->status, exp_error);

	mgmt__pool_exclude_resp__free_unpacked(pc_resp, NULL);
}

static void
test_drpc_exclude_bad_uuid(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_exclude_drpc_call(&call, "BAD", 0);

	ds_mgmt_drpc_pool_exclude(&call, &resp);

	expect_drpc_exclude_resp_with_error(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_exclude_mgmt_svc_fails(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_exclude_drpc_call(&call, TEST_UUID, 0);
	ds_mgmt_target_update_return = -DER_MISC;

	ds_mgmt_drpc_pool_exclude(&call, &resp);
	expect_drpc_exclude_resp_with_error(&resp,
					       ds_mgmt_target_update_return);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_exclude_success(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_exclude_drpc_call(&call, TEST_UUID, 0);
	ds_mgmt_drpc_pool_exclude(&call, &resp);

	expect_drpc_exclude_resp_with_error(&resp, 0);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

/*
 * Pool drain test setup
 */
static int
drpc_drain_setup(void **state)
{
	mock_ds_mgmt_tgt_update_setup();
	return 0;
}

/*
 * dRPC pool drain tests
 */
static void
pack_pool_drain_req(Drpc__Call *call, Mgmt__PoolDrainReq *req)
{
	size_t	len;
	uint8_t	*body;

	len = mgmt__pool_drain_req__get_packed_size(req);
	D_ALLOC(body, len);
	assert_non_null(body);

	mgmt__pool_drain_req__pack(req, body);

	call->body.data = body;
	call->body.len = len;
}

static void
setup_drain_drpc_call(Drpc__Call *call, char *uuid, uint32_t rank)
{
	Mgmt__PoolDrainReq req = MGMT__POOL_DRAIN_REQ__INIT;

	req.uuid = uuid;
	req.n_targetidx = 3;
	req.rank = rank;
	req.targetidx = TEST_IDXS;
	pack_pool_drain_req(call, &req);
}

static void
expect_drpc_drain_resp_with_error(Drpc__Response *resp, int exp_error)
{
	Mgmt__PoolDrainResp	*pc_resp = NULL;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	pc_resp = mgmt__pool_drain_resp__unpack(NULL, resp->body.len,
						  resp->body.data);
	assert_non_null(pc_resp);
	assert_int_equal(pc_resp->status, exp_error);

	mgmt__pool_drain_resp__free_unpacked(pc_resp, NULL);
}

static void
test_drpc_drain_bad_uuid(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_drain_drpc_call(&call, "BAD", 0);

	ds_mgmt_drpc_pool_drain(&call, &resp);

	expect_drpc_drain_resp_with_error(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_drain_mgmt_svc_fails(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_drain_drpc_call(&call, TEST_UUID, 0);
	ds_mgmt_target_update_return = -DER_MISC;

	ds_mgmt_drpc_pool_drain(&call, &resp);
	expect_drpc_drain_resp_with_error(&resp,
					       ds_mgmt_target_update_return);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_drain_success(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_drain_drpc_call(&call, TEST_UUID, 0);
	ds_mgmt_drpc_pool_drain(&call, &resp);

	expect_drpc_drain_resp_with_error(&resp, 0);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

/*
 * dRPC pool extend test setup
 */
static int
drpc_pool_extend_setup(void **state)
{
	mock_ds_mgmt_pool_extend_setup();
	return 0;
}

/*
 * dRPC pool extend tests
 */
static void
pack_pool_extend_req(Drpc__Call *call, Mgmt__PoolExtendReq *req)
{
	size_t	len;
	uint8_t	*body;

	len = mgmt__pool_extend_req__get_packed_size(req);
	D_ALLOC(body, len);
	assert_non_null(body);

	mgmt__pool_extend_req__pack(req, body);

	call->body.data = body;
	call->body.len = len;
}

static void
setup_extend_drpc_call(Drpc__Call *call, char *uuid)
{
	Mgmt__PoolExtendReq req = MGMT__POOL_EXTEND_REQ__INIT;

	req.uuid = uuid;
	req.n_ranks = 3;
	req.scmbytes = 1000000000;
	req.ranks = TEST_RANKS;
	pack_pool_extend_req(call, &req);
}

static void
expect_drpc_extend_resp_with_error(Drpc__Response *resp, int exp_error)
{
	Mgmt__PoolExtendResp	*pc_resp = NULL;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	pc_resp = mgmt__pool_extend_resp__unpack(NULL, resp->body.len,
						 resp->body.data);
	assert_non_null(pc_resp);
	assert_int_equal(pc_resp->status, exp_error);

	mgmt__pool_extend_resp__free_unpacked(pc_resp, NULL);
}

static void
test_drpc_extend_bad_uuid(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_extend_drpc_call(&call, "BAD");

	ds_mgmt_drpc_pool_extend(&call, &resp);

	expect_drpc_extend_resp_with_error(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_extend_mgmt_svc_fails(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_extend_drpc_call(&call, TEST_UUID);
	ds_mgmt_pool_extend_return = -DER_MISC;

	ds_mgmt_drpc_pool_extend(&call, &resp);
	expect_drpc_extend_resp_with_error(&resp, ds_mgmt_pool_extend_return);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_extend_success(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_extend_drpc_call(&call, TEST_UUID);
	ds_mgmt_drpc_pool_extend(&call, &resp);

	expect_drpc_extend_resp_with_error(&resp, 0);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

/*
 * dRPC pool reintegrate tests
 */
static void
pack_pool_reintegrate_req(Drpc__Call *call, Mgmt__PoolReintegrateReq *req)
{
	size_t	len;
	uint8_t	*body;

	len = mgmt__pool_reintegrate_req__get_packed_size(req);
	D_ALLOC(body, len);
	assert_non_null(body);

	mgmt__pool_reintegrate_req__pack(req, body);

	call->body.data = body;
	call->body.len = len;
}

static void
setup_reintegrate_drpc_call(Drpc__Call *call, char *uuid)
{
	Mgmt__PoolReintegrateReq req = MGMT__POOL_REINTEGRATE_REQ__INIT;

	req.uuid = uuid;
	pack_pool_reintegrate_req(call, &req);
}

static void
expect_drpc_reintegrate_resp_with_error(Drpc__Response *resp, int exp_error)
{
	Mgmt__PoolReintegrateResp	*pc_resp = NULL;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	pc_resp = mgmt__pool_reintegrate_resp__unpack(NULL, resp->body.len,
						 resp->body.data);
	assert_non_null(pc_resp);
	assert_int_equal(pc_resp->status, exp_error);

	mgmt__pool_reintegrate_resp__free_unpacked(pc_resp, NULL);
}

static void
test_drpc_reintegrate_bad_uuid(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_reintegrate_drpc_call(&call, "BAD");

	ds_mgmt_drpc_pool_reintegrate(&call, &resp);

	expect_drpc_reintegrate_resp_with_error(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

/*
 * Pool evict test setup
 */
static int
drpc_evict_setup(void **state)
{
	mock_ds_mgmt_pool_evict_setup();
	return 0;
}

/*
 * dRPC pool evict tests
 */
static void
pack_pool_evict_req(Drpc__Call *call, Mgmt__PoolEvictReq *req)
{
	size_t	len;
	uint8_t	*body;

	len = mgmt__pool_evict_req__get_packed_size(req);
	D_ALLOC(body, len);
	assert_non_null(body);

	mgmt__pool_evict_req__pack(req, body);

	call->body.data = body;
	call->body.len = len;
}

static void
setup_evict_drpc_call(Drpc__Call *call, char *uuid, char *sys_name)
{
	Mgmt__PoolEvictReq req = MGMT__POOL_EVICT_REQ__INIT;

	req.uuid = uuid;
	req.sys = sys_name;
	pack_pool_evict_req(call, &req);
}

static void
expect_drpc_evict_resp_with_status(Drpc__Response *resp, int exp_status)
{
	Mgmt__PoolEvictResp	*pc_resp = NULL;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	pc_resp = mgmt__pool_evict_resp__unpack(NULL, resp->body.len,
						 resp->body.data);
	assert_non_null(pc_resp);
	assert_int_equal(pc_resp->status, exp_status);

	mgmt__pool_evict_resp__free_unpacked(pc_resp, NULL);
}

static void
test_drpc_pool_evict_bad_uuid(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_evict_drpc_call(&call, "BAD", "DaosSys");

	ds_mgmt_drpc_pool_evict(&call, &resp);

	expect_drpc_evict_resp_with_status(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

	static void
test_drpc_pool_evict_mgmt_svc_fails(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_evict_drpc_call(&call, TEST_UUID, "DaosSys");
	ds_mgmt_pool_evict_return = -DER_MISC;

	ds_mgmt_drpc_pool_evict(&call, &resp);
	expect_drpc_evict_resp_with_status(&resp,
					       ds_mgmt_pool_evict_return);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_pool_evict_success(void **state)
{
	Drpc__Call	call = DRPC__CALL__INIT;
	Drpc__Response	resp = DRPC__RESPONSE__INIT;

	setup_evict_drpc_call(&call, TEST_UUID, "DaosSys");
	ds_mgmt_drpc_pool_evict(&call, &resp);

	expect_drpc_evict_resp_with_status(&resp, 0);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

/*
 * dRPC Rank test utils
 */
static void
expect_daos_resp_with_der(Drpc__Response *resp, int exp_der)
{
	Mgmt__DaosResp	*d_resp = NULL;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	d_resp = mgmt__daos_resp__unpack(NULL, resp->body.len,
					 resp->body.data);
	assert_non_null(d_resp);
	assert_int_equal(d_resp->status, exp_der);

	mgmt__daos_resp__free_unpacked(d_resp, NULL);
}

/*
 * dRPC ping rank tests
 */
static void
pack_ping_rank_req(Mgmt__PingRankReq *req, Drpc__Call *call)
{
	size_t	len;
	uint8_t	*body;

	len = mgmt__ping_rank_req__get_packed_size(req);
	D_ALLOC(body, len);
	assert_non_null(body);

	mgmt__ping_rank_req__pack(req, body);

	call->body.data = body;
	call->body.len = len;
}

static void
test_drpc_ping_rank_success(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__PingRankReq	pr_req = MGMT__PING_RANK_REQ__INIT;

	pack_ping_rank_req(&pr_req, &call);

	ds_mgmt_drpc_ping_rank(&call, &resp);

	expect_daos_resp_with_der(&resp, 0);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);

}

/*
 * dRPC prep shutdown tests
 */
static void
pack_prep_shutdown_req(Mgmt__PrepShutdownReq *req, Drpc__Call *call)
{
	size_t	len;
	uint8_t	*body;

	len = mgmt__prep_shutdown_req__get_packed_size(req);
	D_ALLOC(body, len);
	assert_non_null(body);

	mgmt__prep_shutdown_req__pack(req, body);

	call->body.data = body;
	call->body.len = len;
}

static void
test_drpc_prep_shutdown_success(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__PrepShutdownReq	ps_req = MGMT__PREP_SHUTDOWN_REQ__INIT;

	pack_prep_shutdown_req(&ps_req, &call);

	ds_mgmt_drpc_prep_shutdown(&call, &resp);

	expect_daos_resp_with_der(&resp, 0);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

/*
 * dRPC cont set owner setup/teardown
 */

static int
drpc_cont_set_owner_setup(void **state)
{
	mock_ds_mgmt_cont_set_owner_setup();

	return 0;
}

static int
drpc_cont_set_owner_teardown(void **state)
{
	mock_ds_mgmt_cont_set_owner_teardown();

	return 0;
}

/*
 * dRPC cont set owner tests
 */

static void
setup_cont_set_owner_drpc_call(Drpc__Call *call, Mgmt__ContSetOwnerReq *req)
{
	size_t	len;
	uint8_t	*body;

	len = mgmt__cont_set_owner_req__get_packed_size(req);
	D_ALLOC(body, len);
	assert_non_null(body);

	mgmt__cont_set_owner_req__pack(req, body);

	call->body.data = body;
	call->body.len = len;
}

static void
expect_drpc_cont_set_owner_resp_with_status(Drpc__Response *resp,
					   int expected_err)
{
	Mgmt__ContSetOwnerResp *payload_resp = NULL;

	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);
	assert_non_null(resp->body.data);

	payload_resp = mgmt__cont_set_owner_resp__unpack(NULL, resp->body.len,
							 resp->body.data);
	assert_non_null(payload_resp);
	assert_int_equal(payload_resp->status, expected_err);

	mgmt__cont_set_owner_resp__free_unpacked(payload_resp, NULL);
}

static void
test_drpc_cont_set_owner_bad_pool_uuid(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__ContSetOwnerReq	req = MGMT__CONT_SET_OWNER_REQ__INIT;

	req.pooluuid = "this isn't really a uuid";
	req.contuuid = "11111111-1111-1111-1111-111111111111";
	req.owneruser = "user@";
	req.ownergroup = "group@";

	setup_cont_set_owner_drpc_call(&call, &req);

	ds_mgmt_drpc_cont_set_owner(&call, &resp);

	expect_drpc_cont_set_owner_resp_with_status(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_cont_set_owner_bad_cont_uuid(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__ContSetOwnerReq	req = MGMT__CONT_SET_OWNER_REQ__INIT;

	req.pooluuid = "11111111-1111-1111-1111-111111111111";
	req.contuuid = "this isn't really a uuid";
	req.owneruser = "user@";
	req.ownergroup = "group@";

	setup_cont_set_owner_drpc_call(&call, &req);

	ds_mgmt_drpc_cont_set_owner(&call, &resp);

	expect_drpc_cont_set_owner_resp_with_status(&resp, -DER_INVAL);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_cont_set_owner_failed(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__ContSetOwnerReq	req = MGMT__CONT_SET_OWNER_REQ__INIT;
	int			exp_rc = -DER_MISC;

	req.pooluuid = "11111111-1111-1111-1111-111111111111";
	req.contuuid = "22222222-2222-2222-2222-222222222222";
	req.owneruser = "user@";
	req.ownergroup = "group@";

	setup_cont_set_owner_drpc_call(&call, &req);
	ds_mgmt_cont_set_owner_return = exp_rc;

	ds_mgmt_drpc_cont_set_owner(&call, &resp);

	expect_drpc_cont_set_owner_resp_with_status(&resp, exp_rc);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

static void
test_drpc_cont_set_owner_success(void **state)
{
	Drpc__Call		call = DRPC__CALL__INIT;
	Drpc__Response		resp = DRPC__RESPONSE__INIT;
	Mgmt__ContSetOwnerReq	req = MGMT__CONT_SET_OWNER_REQ__INIT;
	char			*pool_uuid_str;
	uuid_t			pool_uuid;
	char			*cont_uuid_str;
	uuid_t			cont_uuid;

	pool_uuid_str = "11111111-1111-1111-1111-111111111111";
	uuid_parse(pool_uuid_str, pool_uuid);
	cont_uuid_str = "22222222-2222-2222-2222-222222222222";
	uuid_parse(cont_uuid_str, cont_uuid);

	req.pooluuid = pool_uuid_str;
	req.contuuid = cont_uuid_str;
	req.owneruser = "user@";
	req.ownergroup = "group@";

	setup_cont_set_owner_drpc_call(&call, &req);

	ds_mgmt_drpc_cont_set_owner(&call, &resp);

	expect_drpc_cont_set_owner_resp_with_status(&resp, 0);

	/* Verify mgmt_cont_set_owner called with correct params */
	assert_int_equal(uuid_compare(pool_uuid, ds_mgmt_cont_set_owner_pool),
			 0);
	assert_int_equal(uuid_compare(cont_uuid, ds_mgmt_cont_set_owner_cont),
			 0);
	assert_non_null(ds_mgmt_cont_set_owner_user);
	assert_string_equal(req.owneruser, ds_mgmt_cont_set_owner_user);
	assert_non_null(ds_mgmt_cont_set_owner_group);
	assert_string_equal(req.ownergroup, ds_mgmt_cont_set_owner_group);

	D_FREE(call.body.data);
	D_FREE(resp.body.data);
}

#define ACL_TEST(x)	cmocka_unit_test_setup_teardown(x, \
						drpc_pool_acl_setup, \
						drpc_pool_acl_teardown)

#define LIST_CONT_TEST(x) cmocka_unit_test_setup_teardown(x, \
						drpc_list_cont_setup, \
						drpc_list_cont_teardown)

#define POOL_SET_PROP_TEST(x) cmocka_unit_test_setup_teardown(x, \
						drpc_pool_set_prop_setup, \
						drpc_pool_set_prop_teardown)


#define QUERY_TEST(x)	cmocka_unit_test_setup(x, \
						drpc_pool_query_setup)

#define EXCLUDE_TEST(x)	cmocka_unit_test_setup(x, \
						drpc_exclude_setup)

#define DRAIN_TEST(x)	cmocka_unit_test_setup(x, \
						drpc_drain_setup)

#define POOL_EXTEND_TEST(x)	cmocka_unit_test_setup(x, \
						drpc_pool_extend_setup)

#define REINTEGRATE_TEST(x)	cmocka_unit_test(x)

#define POOL_CREATE_TEST(x)	cmocka_unit_test(x)

#define POOL_EVICT_TEST(x)	cmocka_unit_test_setup(x, \
						drpc_evict_setup)


#define PING_RANK_TEST(x)	cmocka_unit_test(x)

#define PREP_SHUTDOWN_TEST(x)	cmocka_unit_test(x)

#define CONT_SET_OWNER_TEST(x) cmocka_unit_test_setup_teardown(x, \
						drpc_cont_set_owner_setup, \
						drpc_cont_set_owner_teardown)

int
main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_mgmt_drpc_handlers_bad_call_payload),
		ACL_TEST(test_drpc_pool_get_acl_bad_uuid),
		ACL_TEST(test_drpc_pool_get_acl_mgmt_svc_fails),
		ACL_TEST(test_drpc_pool_get_acl_cant_translate_acl),
		ACL_TEST(test_drpc_pool_get_acl_success),
		ACL_TEST(test_drpc_pool_overwrite_acl_bad_uuid),
		ACL_TEST(test_drpc_pool_overwrite_acl_bad_acl),
		ACL_TEST(test_drpc_pool_overwrite_acl_mgmt_svc_fails),
		ACL_TEST(test_drpc_pool_overwrite_acl_success),
		ACL_TEST(test_drpc_pool_update_acl_bad_uuid),
		ACL_TEST(test_drpc_pool_update_acl_bad_acl),
		ACL_TEST(test_drpc_pool_update_acl_mgmt_svc_fails),
		ACL_TEST(test_drpc_pool_update_acl_success),
		ACL_TEST(test_drpc_pool_delete_acl_bad_uuid),
		ACL_TEST(test_drpc_pool_delete_acl_mgmt_svc_fails),
		ACL_TEST(test_drpc_pool_delete_acl_success),
		LIST_CONT_TEST(test_drpc_pool_list_cont_bad_uuid),
		LIST_CONT_TEST(test_drpc_pool_list_cont_mgmt_svc_fails),
		LIST_CONT_TEST(test_drpc_pool_list_cont_no_containers),
		LIST_CONT_TEST(test_drpc_pool_list_cont_with_containers),
		POOL_SET_PROP_TEST(
			test_drpc_pool_set_prop_invalid_property_type),
		POOL_SET_PROP_TEST(
			test_drpc_pool_set_prop_invalid_value_type),
		POOL_SET_PROP_TEST(test_drpc_pool_set_prop_bad_uuid),
		POOL_SET_PROP_TEST(test_drpc_pool_set_prop_success),
		EXCLUDE_TEST(test_drpc_exclude_bad_uuid),
		EXCLUDE_TEST(test_drpc_exclude_mgmt_svc_fails),
		EXCLUDE_TEST(test_drpc_exclude_success),
		DRAIN_TEST(test_drpc_drain_bad_uuid),
		DRAIN_TEST(test_drpc_drain_mgmt_svc_fails),
		DRAIN_TEST(test_drpc_drain_success),
		POOL_EXTEND_TEST(test_drpc_extend_bad_uuid),
		POOL_EXTEND_TEST(test_drpc_extend_mgmt_svc_fails),
		POOL_EXTEND_TEST(test_drpc_extend_success),
		REINTEGRATE_TEST(test_drpc_reintegrate_bad_uuid),
		QUERY_TEST(test_drpc_pool_query_bad_uuid),
		QUERY_TEST(test_drpc_pool_query_mgmt_svc_fails),
		QUERY_TEST(test_drpc_pool_query_success),
		QUERY_TEST(test_drpc_pool_query_success_rebuild_busy),
		QUERY_TEST(test_drpc_pool_query_success_rebuild_done),
		QUERY_TEST(test_drpc_pool_query_success_rebuild_err),
		POOL_CREATE_TEST(test_drpc_pool_create_invalid_acl),
		POOL_EVICT_TEST(test_drpc_pool_evict_bad_uuid),
		POOL_EVICT_TEST(test_drpc_pool_evict_mgmt_svc_fails),
		POOL_EVICT_TEST(test_drpc_pool_evict_success),
		PING_RANK_TEST(test_drpc_ping_rank_success),
		PREP_SHUTDOWN_TEST(test_drpc_prep_shutdown_success),
		CONT_SET_OWNER_TEST(test_drpc_cont_set_owner_bad_cont_uuid),
		CONT_SET_OWNER_TEST(test_drpc_cont_set_owner_bad_pool_uuid),
		CONT_SET_OWNER_TEST(test_drpc_cont_set_owner_failed),
		CONT_SET_OWNER_TEST(test_drpc_cont_set_owner_success),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
