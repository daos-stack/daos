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
 * Unit tests for server ACL functions
 */

#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <cmocka.h>

#include "drpc_mocks.h"
#include <daos_types.h>
#include <daos/drpc_modules.h>
#include <daos_srv/security.h>

#include "../srv_internal.h"

/*
 * Mocks
 */
char *ds_sec_server_socket_path = "/fake/socket/path";

/*
 * Test constants and defaults
 */
#define TEST_USER	"myuser@"
#define TEST_GROUP	"mygroup@"

/*
 * Test helper functions
 */
static Auth__Token *
create_valid_auth_token(const char *user, const char *grp,
			const char *grp_list[], size_t num_grps)
{
	Auth__Token	*token;
	Auth__Sys	*authsys;

	D_ALLOC_PTR(token);
	auth__token__init(token);
	token->flavor = AUTH__FLAVOR__AUTH_SYS;

	D_ALLOC_PTR(authsys);
	auth__sys__init(authsys);
	D_STRNDUP(authsys->user, user, DAOS_ACL_MAX_PRINCIPAL_LEN);
	D_STRNDUP(authsys->group, grp, DAOS_ACL_MAX_PRINCIPAL_LEN);

	if (num_grps > 0) {
		size_t i;

		authsys->n_groups = num_grps;

		D_ALLOC_ARRAY(authsys->groups, num_grps);
		for (i = 0; i < num_grps; i++) {
			D_STRNDUP(authsys->groups[i], grp_list[i],
					DAOS_ACL_MAX_PRINCIPAL_LEN);
		}
	}

	token->data.len = auth__sys__get_packed_size(authsys);
	D_ALLOC(token->data.data, token->data.len);
	auth__sys__pack(authsys, token->data.data);

	auth__sys__free_unpacked(authsys, NULL);

	return token;
}

static Auth__Token *
create_default_auth_token(void)
{
	return create_valid_auth_token(TEST_USER, TEST_GROUP, NULL, 0);
}

static void
init_valid_cred(d_iov_t *cred, const char *user, const char *grp,
		const char *grp_list[], size_t num_grps)
{
	Auth__Credential	new_cred = AUTH__CREDENTIAL__INIT;
	Auth__Token		*token;
	uint8_t			*buf;
	size_t			buf_len;
	Auth__ValidateCredResp	resp = AUTH__VALIDATE_CRED_RESP__INIT;

	token = create_valid_auth_token(user, grp, grp_list, num_grps);

	/* Initialize the cred with token */
	new_cred.token = token;
	buf_len = auth__credential__get_packed_size(&new_cred);
	D_ALLOC(buf, buf_len);
	auth__credential__pack(&new_cred, buf);
	d_iov_set(cred, buf, buf_len);

	resp.token = token;

	/* Return the cred token from the drpc mock, too */
	pack_validate_resp_in_drpc_call_resp_body(&resp);

	auth__token__free_unpacked(token, NULL);
}

static void
init_default_cred(d_iov_t *cred)
{
	init_valid_cred(cred, TEST_USER, TEST_GROUP, NULL, 0);
}

static void
init_default_ownership(struct ownership *owner)
{
	owner->user = TEST_USER;
	owner->group = TEST_GROUP;
}

static void
setup_drpc_with_default_token(void)
{
	Auth__Token		*token = create_default_auth_token();
	Auth__ValidateCredResp	resp = AUTH__VALIDATE_CRED_RESP__INIT;

	resp.token = token;

	pack_validate_resp_in_drpc_call_resp_body(&resp);

	auth__token__free_unpacked(token, NULL);
}

static void
free_ace_list(struct daos_ace **aces, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		daos_ace_free(aces[i]);
}

static struct daos_acl *
get_acl_with_perms(uint64_t owner_perms, uint64_t group_perms)
{
	struct daos_acl *acl;
	size_t		num_aces = 2;
	struct daos_ace *aces[num_aces];
	size_t		i;

	aces[0] = daos_ace_create(DAOS_ACL_OWNER, NULL);
	aces[0]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	aces[0]->dae_allow_perms = owner_perms;

	aces[1] = daos_ace_create(DAOS_ACL_OWNER_GROUP, NULL);
	aces[1]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	aces[1]->dae_allow_perms = group_perms;

	acl = daos_acl_create(aces, num_aces);

	for (i = 0; i < num_aces; i++) {
		daos_ace_free(aces[i]);
	}

	return acl;
}

static struct daos_acl *
get_user_acl_with_perms(const char *user, uint64_t perms)
{
	struct daos_acl *acl;
	struct daos_ace *ace;

	ace = daos_ace_create(DAOS_ACL_USER, user);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = perms;

	acl = daos_acl_create(&ace, 1);

	daos_ace_free(ace);

	return acl;
}

/*
 * Setup and teardown
 */
static int
srv_acl_setup(void **state)
{
	mock_drpc_connect_setup();
	mock_drpc_call_setup();
	mock_drpc_close_setup();

	return 0;
}

static int
srv_acl_teardown(void **state)
{
	mock_drpc_connect_teardown();
	mock_drpc_call_teardown();

	return 0;
}

static void
srv_acl_resetup(void **state)
{
	srv_acl_teardown(state);
	srv_acl_setup(state);
}

/*
 * Validate credential tests
 */

static void
test_validate_creds_null_cred(void **state)
{
	Auth__Token *result = NULL;

	assert_int_equal(ds_sec_validate_credentials(NULL, &result),
			 -DER_INVAL);
}

static void
test_validate_creds_null_token_ptr(void **state)
{
	d_iov_t cred;

	init_default_cred(&cred);

	assert_int_equal(ds_sec_validate_credentials(&cred, NULL),
			 -DER_INVAL);

	daos_iov_free(&cred);
}

static void
test_validate_creds_empty_cred(void **state)
{
	Auth__Token	*result = NULL;
	d_iov_t		cred;

	d_iov_set(&cred, NULL, 0);

	assert_int_equal(ds_sec_validate_credentials(&cred, &result),
			 -DER_INVAL);
}

static void
test_validate_creds_drpc_connect_failed(void **state)
{
	d_iov_t		cred;
	Auth__Token	*result = NULL;

	init_default_cred(&cred);

	D_FREE(drpc_connect_return); /* failure returns null */

	assert_int_equal(ds_sec_validate_credentials(&cred, &result),
			 -DER_BADPATH);

	assert_null(result);

	daos_iov_free(&cred);
}

static void
test_validate_creds_drpc_call_failed(void **state)
{
	d_iov_t		cred;
	Auth__Token	*result = NULL;

	init_default_cred(&cred);

	drpc_call_return = -DER_MISC;
	drpc_call_resp_return_ptr = NULL;

	assert_int_equal(ds_sec_validate_credentials(&cred, &result),
			 drpc_call_return);

	assert_null(result);
	assert_non_null(drpc_close_ctx); /* closed regardless of error */

	daos_iov_free(&cred);
}

static void
test_validate_creds_drpc_call_null_response(void **state)
{
	d_iov_t		cred;
	Auth__Token	*result = NULL;

	init_default_cred(&cred);

	drpc_call_resp_return_ptr = NULL;

	assert_int_equal(ds_sec_validate_credentials(&cred, &result),
			 -DER_NOREPLY);

	assert_null(result);

	daos_iov_free(&cred);
}

static void
test_validate_creds_drpc_response_failure(void **state)
{
	d_iov_t		cred;
	Auth__Token	*result = NULL;

	init_default_cred(&cred);
	setup_drpc_with_default_token();

	drpc_call_resp_return_content.status = DRPC__STATUS__FAILURE;

	assert_int_equal(ds_sec_validate_credentials(&cred, &result),
			 -DER_MISC);

	assert_null(result);

	daos_iov_free(&cred);
}

static void
test_validate_creds_drpc_response_malformed_body(void **state)
{
	d_iov_t		cred;
	Auth__Token	*result = NULL;

	init_default_cred(&cred);

	free_drpc_call_resp_body();
	D_ALLOC(drpc_call_resp_return_content.body.data, 1);
	drpc_call_resp_return_content.body.len = 1;

	assert_int_equal(ds_sec_validate_credentials(&cred, &result),
			 -DER_PROTO);

	assert_null(result);

	daos_iov_free(&cred);
}

static void
test_validate_creds_drpc_response_null_token(void **state)
{
	d_iov_t			cred;
	Auth__Token		*result = NULL;
	Auth__ValidateCredResp	resp = AUTH__VALIDATE_CRED_RESP__INIT;

	init_default_cred(&cred);

	pack_validate_resp_in_drpc_call_resp_body(&resp);

	assert_int_equal(ds_sec_validate_credentials(&cred, &result),
			 -DER_PROTO);

	assert_null(result);

	daos_iov_free(&cred);
}

static void
test_validate_creds_drpc_response_empty_token(void **state)
{
	d_iov_t			cred;
	Auth__Token		*result = NULL;
	Auth__ValidateCredResp	resp = AUTH__VALIDATE_CRED_RESP__INIT;
	Auth__Token		bad_token = AUTH__TOKEN__INIT;

	init_default_cred(&cred);

	bad_token.data.data = NULL;
	resp.token = &bad_token;

	pack_validate_resp_in_drpc_call_resp_body(&resp);

	assert_int_equal(ds_sec_validate_credentials(&cred, &result),
			 -DER_PROTO);

	assert_null(result);

	daos_iov_free(&cred);
}

static void
test_validate_creds_drpc_response_bad_status(void **state)
{
	d_iov_t			cred;
	Auth__Token		*result = NULL;
	Auth__ValidateCredResp	resp = AUTH__VALIDATE_CRED_RESP__INIT;

	init_default_cred(&cred);

	resp.status = -DER_MISC;
	pack_validate_resp_in_drpc_call_resp_body(&resp);

	assert_int_equal(ds_sec_validate_credentials(&cred, &result),
			 -DER_MISC);

	assert_null(result);

	daos_iov_free(&cred);
}

static void
test_validate_creds_success(void **state)
{
	d_iov_t			cred;
	Auth__Token		*result = NULL;
	Auth__Sys		*authsys;
	Auth__ValidateCredReq	*req;
	uint8_t			*packed_cred;
	size_t			packed_len;

	init_default_cred(&cred);
	setup_drpc_with_default_token();

	assert_int_equal(ds_sec_validate_credentials(&cred, &result), 0);

	assert_non_null(result);
	assert_int_equal(result->flavor, AUTH__FLAVOR__AUTH_SYS);

	authsys = auth__sys__unpack(NULL, result->data.len, result->data.data);
	assert_non_null(authsys); /* NULL implies malformed payload */
	assert_string_equal(authsys->user, TEST_USER);
	assert_string_equal(authsys->group, TEST_GROUP);
	assert_int_equal(authsys->n_groups, 0);

	/* verify we called drpc with correct params */
	assert_string_equal(drpc_connect_sockaddr, ds_sec_server_socket_path);

	assert_ptr_equal(drpc_call_ctx, drpc_connect_return);
	assert_int_equal(drpc_call_flags, R_SYNC);
	assert_non_null(drpc_call_msg_ptr);
	assert_int_equal(drpc_call_msg_content.module,
			 DRPC_MODULE_SEC);
	assert_int_equal(drpc_call_msg_content.method,
			 DRPC_METHOD_SEC_VALIDATE_CREDS);
	assert_non_null(drpc_call_resp_ptr);

	assert_ptr_equal(drpc_close_ctx, drpc_call_ctx);

	/* Make sure we sent a properly formatted req */
	req = auth__validate_cred_req__unpack(NULL,
					      drpc_call_msg_content.body.len,
					      drpc_call_msg_content.body.data);
	assert_non_null(req);
	assert_non_null(req->cred);

	/* Check the req credential against packed cred we passed in */
	packed_len = auth__credential__get_packed_size(req->cred);
	assert_int_equal(packed_len, cred.iov_len);
	D_ALLOC(packed_cred, packed_len);
	assert_non_null(packed_cred);
	auth__credential__pack(req->cred, packed_cred);
	assert_memory_equal(packed_cred, cred.iov_buf, packed_len);

	D_FREE(packed_cred);
	auth__validate_cred_req__free_unpacked(req, NULL);

	daos_iov_free(&cred);
	auth__sys__free_unpacked(authsys, NULL);
	auth__token__free_unpacked(result, NULL);
}

/*
 * Default ACL tests
 */

static void
expect_ace_is_type_with_perms(struct daos_ace *ace,
			      enum daos_acl_principal_type exp_type,
			      uint8_t exp_flags, uint64_t exp_perms)
{
	assert_int_equal(ace->dae_principal_type, exp_type);
	assert_int_equal(ace->dae_access_types, DAOS_ACL_ACCESS_ALLOW);
	assert_int_equal(ace->dae_access_flags, exp_flags);
	assert_int_equal(ace->dae_allow_perms, exp_perms);
}

static void
test_default_pool_acl(void **state)
{
	struct daos_acl	*acl;
	struct daos_ace	*current = NULL;
	uint64_t	exp_owner_perms = DAOS_ACL_PERM_READ |
					  DAOS_ACL_PERM_WRITE;
	uint64_t	exp_grp_perms = DAOS_ACL_PERM_READ |
					DAOS_ACL_PERM_WRITE;

	acl = ds_sec_alloc_default_daos_pool_acl();

	assert_non_null(acl);
	assert_int_equal(daos_acl_pool_validate(acl), 0); /* valid pool ACL */

	current = daos_acl_get_next_ace(acl, NULL);
	assert_non_null(current);
	expect_ace_is_type_with_perms(current, DAOS_ACL_OWNER, 0,
				      exp_owner_perms);

	current = daos_acl_get_next_ace(acl, current);
	assert_non_null(current);
	expect_ace_is_type_with_perms(current, DAOS_ACL_OWNER_GROUP,
				      DAOS_ACL_FLAG_GROUP, exp_grp_perms);

	current = daos_acl_get_next_ace(acl, current);
	assert_null(current); /* shouldn't be any more ACEs */

	daos_acl_free(acl);
}

static void
test_default_cont_acl(void **state)
{
	struct daos_acl	*acl;
	struct daos_ace	*current = NULL;
	uint64_t	exp_owner_perms = DAOS_ACL_PERM_CONT_ALL;
	uint64_t	exp_grp_perms = DAOS_ACL_PERM_READ |
					DAOS_ACL_PERM_WRITE |
					DAOS_ACL_PERM_GET_PROP |
					DAOS_ACL_PERM_SET_PROP;

	acl = ds_sec_alloc_default_daos_cont_acl();

	assert_non_null(acl);
	assert_int_equal(daos_acl_cont_validate(acl), 0); /* valid cont ACL */

	current = daos_acl_get_next_ace(acl, NULL);
	assert_non_null(current);
	expect_ace_is_type_with_perms(current, DAOS_ACL_OWNER, 0,
				      exp_owner_perms);

	current = daos_acl_get_next_ace(acl, current);
	assert_non_null(current);
	expect_ace_is_type_with_perms(current, DAOS_ACL_OWNER_GROUP,
				      DAOS_ACL_FLAG_GROUP, exp_grp_perms);

	current = daos_acl_get_next_ace(acl, current);
	assert_null(current); /* shouldn't be any more ACEs */

	daos_acl_free(acl);
}

/*
 * Pool get capabilities tests
 */
static void
expect_pool_get_capas_flags_invalid(uint64_t invalid_flags)
{
	struct daos_acl		*valid_acl;
	d_iov_t			valid_cred;
	struct ownership	valid_owner;
	uint64_t		result = 0;

	valid_owner.user = "root@";
	valid_owner.group = "admins@";

	valid_acl = daos_acl_create(NULL, 0);
	assert_non_null(valid_acl);
	init_default_cred(&valid_cred);

	printf("Expecting flags %#lx invalid\n", invalid_flags);
	assert_int_equal(ds_sec_pool_get_capabilities(invalid_flags,
						      &valid_cred,
						      &valid_owner, valid_acl,
						      &result),
			 -DER_INVAL);

	daos_acl_free(valid_acl);
	daos_iov_free(&valid_cred);
}

static void
test_pool_get_capas_invalid_flags(void **state)
{
	expect_pool_get_capas_flags_invalid(0);
	expect_pool_get_capas_flags_invalid(1U << DAOS_PC_NBITS);
	expect_pool_get_capas_flags_invalid(DAOS_PC_RO | DAOS_PC_RW);
	expect_pool_get_capas_flags_invalid(DAOS_PC_RO | DAOS_PC_EX);
	expect_pool_get_capas_flags_invalid(DAOS_PC_RW | DAOS_PC_EX);
}

static void
test_pool_get_capas_null_input(void **state)
{
	struct daos_acl		*valid_acl;
	d_iov_t			valid_cred;
	struct ownership	valid_owner;
	uint64_t		valid_flags = DAOS_PC_RO;
	uint64_t		result = 0;

	init_default_ownership(&valid_owner);
	init_default_cred(&valid_cred);

	valid_acl = daos_acl_create(NULL, 0);
	assert_non_null(valid_acl);

	assert_int_equal(ds_sec_pool_get_capabilities(valid_flags,
						      NULL,
						      &valid_owner, valid_acl,
						      &result),
			 -DER_INVAL);

	assert_int_equal(ds_sec_pool_get_capabilities(valid_flags,
						      &valid_cred,
						      NULL, valid_acl,
						      &result),
			 -DER_INVAL);

	assert_int_equal(ds_sec_pool_get_capabilities(valid_flags,
						      &valid_cred,
						      &valid_owner, NULL,
						      &result),
			 -DER_INVAL);

	assert_int_equal(ds_sec_pool_get_capabilities(valid_flags,
						      &valid_cred,
						      &valid_owner, valid_acl,
						      NULL),
			 -DER_INVAL);

	daos_acl_free(valid_acl);
	daos_iov_free(&valid_cred);
}

static void
expect_pool_get_capas_owner_invalid(char *user, char *group)
{
	struct daos_acl		*valid_acl;
	d_iov_t			valid_cred;
	struct ownership	invalid_owner;
	uint64_t		valid_flags = DAOS_PC_RO;
	uint64_t		result = 0;

	init_default_cred(&valid_cred);

	valid_acl = daos_acl_create(NULL, 0);
	assert_non_null(valid_acl);

	invalid_owner.user = user;
	invalid_owner.group = group;
	assert_int_equal(ds_sec_pool_get_capabilities(valid_flags,
						      &valid_cred,
						      &invalid_owner, valid_acl,
						      &result),
			 -DER_INVAL);

	daos_acl_free(valid_acl);
	daos_iov_free(&valid_cred);
}

static void
test_pool_get_capas_bad_owner(void **state)
{
	expect_pool_get_capas_owner_invalid(NULL, NULL);
	expect_pool_get_capas_owner_invalid(TEST_USER, NULL);
	expect_pool_get_capas_owner_invalid(NULL, TEST_GROUP);
	expect_pool_get_capas_owner_invalid("notavalidname", TEST_GROUP);
	expect_pool_get_capas_owner_invalid(TEST_USER, "notavalidname");
}

static void
test_pool_get_capas_bad_acl(void **state)
{
	struct daos_acl		*bad_acl;
	d_iov_t			cred;
	struct ownership	ownership;
	uint64_t		result;

	init_default_cred(&cred);
	init_default_ownership(&ownership);

	/* zeroed out - not a valid ACL */
	D_ALLOC(bad_acl, sizeof(struct daos_acl));
	assert_non_null(bad_acl);

	assert_int_equal(ds_sec_pool_get_capabilities(DAOS_PC_RO, &cred,
						      &ownership, bad_acl,
						      &result),
			 -DER_INVAL);

	D_FREE(bad_acl);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_validate_cred_failed(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct ownership	ownership;
	uint64_t		result;

	init_default_cred(&cred);
	init_default_ownership(&ownership);
	acl = daos_acl_create(NULL, 0);

	/* drpc call failure will fail validation */
	drpc_call_return = -DER_MISC;
	drpc_call_resp_return_ptr = NULL;

	assert_int_equal(ds_sec_pool_get_capabilities(DAOS_PC_RO, &cred,
						      &ownership, acl,
						      &result),
			 drpc_call_return);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
expect_pool_get_capas_bad_authsys_payload(int auth_flavor)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	size_t			data_len = 8;
	Auth__Token		token = AUTH__TOKEN__INIT;
	Auth__ValidateCredResp	resp = AUTH__VALIDATE_CRED_RESP__INIT;
	struct ownership	ownership;
	uint64_t		result;

	init_default_cred(&cred);
	init_default_ownership(&ownership);
	acl = daos_acl_create(NULL, 0);

	token.flavor = auth_flavor;

	/* Put some junk in there */
	D_ALLOC(token.data.data, data_len);
	memset(token.data.data, 0xFF, data_len);
	token.data.len = data_len;

	resp.token = &token;

	pack_validate_resp_in_drpc_call_resp_body(&resp);

	assert_int_equal(ds_sec_pool_get_capabilities(DAOS_PC_RO, &cred,
						      &ownership, acl,
						      &result),
			 -DER_PROTO);

	daos_acl_free(acl);
	daos_iov_free(&cred);
	D_FREE(token.data.data);
}

static void
test_pool_get_capas_wrong_flavor(void **state)
{
	expect_pool_get_capas_bad_authsys_payload(AUTH__FLAVOR__AUTH_NONE);
}

static void
test_pool_get_capas_bad_payload(void **state)
{
	expect_pool_get_capas_bad_authsys_payload(AUTH__FLAVOR__AUTH_SYS);
}

static void
expect_pool_capas_with_acl(struct daos_acl *acl, d_iov_t *cred,
		      uint64_t flags, uint64_t exp_capas)
{
	struct ownership	ownership;
	uint64_t		result = -1;

	init_default_ownership(&ownership);

	assert_int_equal(ds_sec_pool_get_capabilities(flags, cred, &ownership,
						      acl, &result),
			 0);

	assert_int_equal(result, exp_capas);
}

static void
test_pool_get_capas_empty_acl(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;

	init_default_cred(&cred);
	acl = daos_acl_create(NULL, 0);

	expect_pool_capas_with_acl(acl, &cred, DAOS_PC_RO, 0);
	/* no capabilities allowed */

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
expect_owner_capas_with_perms(uint64_t acl_perms, uint64_t flags,
			      uint64_t exp_capas)
{
	struct daos_acl	*acl;
	d_iov_t		cred;

	/* Only matches owner */
	init_valid_cred(&cred, TEST_USER, "somerandomgroup@", NULL, 0);
	acl = get_acl_with_perms(acl_perms, 0);

	expect_pool_capas_with_acl(acl, &cred, flags, exp_capas);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_owner_success(void **state)
{
	expect_owner_capas_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RO,
				      POOL_CAPA_READ);
	srv_acl_resetup(state);
	expect_owner_capas_with_perms(DAOS_ACL_PERM_GET_PROP, DAOS_PC_RO,
				      POOL_CAPA_READ);
	srv_acl_resetup(state);
	expect_owner_capas_with_perms(DAOS_ACL_PERM_READ |
				      DAOS_ACL_PERM_GET_PROP, DAOS_PC_RO,
				      POOL_CAPA_READ);
	srv_acl_resetup(state);
	expect_owner_capas_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				      DAOS_PC_RO, POOL_CAPA_READ);
	srv_acl_resetup(state);
	expect_owner_capas_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				      DAOS_PC_RW,
				      POOL_CAPA_READ | POOL_CAPA_CREATE_CONT |
				      POOL_CAPA_DEL_CONT);
	srv_acl_resetup(state);
	expect_owner_capas_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				      DAOS_PC_EX,
				      POOL_CAPA_READ | POOL_CAPA_CREATE_CONT |
				      POOL_CAPA_DEL_CONT);
	srv_acl_resetup(state);
	expect_owner_capas_with_perms(DAOS_ACL_PERM_GET_PROP |
				      DAOS_ACL_PERM_CREATE_CONT,
				      DAOS_PC_RW,
				      POOL_CAPA_READ | POOL_CAPA_CREATE_CONT);
	srv_acl_resetup(state);
	expect_owner_capas_with_perms(DAOS_ACL_PERM_GET_PROP |
				      DAOS_ACL_PERM_DEL_CONT,
				      DAOS_PC_RW,
				      POOL_CAPA_READ | POOL_CAPA_DEL_CONT);
}

static void
expect_group_capas_with_perms(uint64_t acl_perms, uint64_t flags,
			      uint64_t exp_capas)
{
	struct daos_acl	*acl;
	d_iov_t		cred;

	/* Only matches group */
	init_valid_cred(&cred, "randomuser@", TEST_GROUP, NULL, 0);
	acl = get_acl_with_perms(0, acl_perms);

	expect_pool_capas_with_acl(acl, &cred, flags, exp_capas);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_group_success(void **state)
{
	expect_group_capas_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RO,
				      POOL_CAPA_READ);
	srv_acl_resetup(state);
	expect_group_capas_with_perms(DAOS_ACL_PERM_GET_PROP, DAOS_PC_RO,
				      POOL_CAPA_READ);
	srv_acl_resetup(state);
	expect_group_capas_with_perms(DAOS_ACL_PERM_READ |
				      DAOS_ACL_PERM_GET_PROP, DAOS_PC_RO,
				      POOL_CAPA_READ);
	srv_acl_resetup(state);
	expect_group_capas_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				      DAOS_PC_RO, POOL_CAPA_READ);
	srv_acl_resetup(state);
	expect_group_capas_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				      DAOS_PC_RW, POOL_CAPA_READ |
				      POOL_CAPA_CREATE_CONT |
				      POOL_CAPA_DEL_CONT);
	srv_acl_resetup(state);
	expect_group_capas_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				      DAOS_PC_EX, POOL_CAPA_READ |
				      POOL_CAPA_CREATE_CONT |
				      POOL_CAPA_DEL_CONT);
	srv_acl_resetup(state);
	expect_group_capas_with_perms(DAOS_ACL_PERM_GET_PROP |
				      DAOS_ACL_PERM_CREATE_CONT,
				      DAOS_PC_RW,
				      POOL_CAPA_READ | POOL_CAPA_CREATE_CONT);
	srv_acl_resetup(state);
	expect_group_capas_with_perms(DAOS_ACL_PERM_GET_PROP |
				      DAOS_ACL_PERM_DEL_CONT,
				      DAOS_PC_RW,
				      POOL_CAPA_READ | POOL_CAPA_DEL_CONT);
}

static void
expect_list_capas_with_perms(uint64_t acl_perms,
			     uint64_t flags, uint64_t exp_capas)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	static const char	*grps[] = { "badgroup@",
					    TEST_GROUP,
					    "worsegroup@" };

	/* Only matches group */
	init_valid_cred(&cred, "fakeuser@", "fakegroup@", grps,
			sizeof(grps) / sizeof(char *));
	acl = get_acl_with_perms(0, acl_perms);

	expect_pool_capas_with_acl(acl, &cred, flags, exp_capas);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_group_list_success(void **state)
{
	expect_list_capas_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RO,
				     POOL_CAPA_READ);
	srv_acl_resetup(state);
	expect_list_capas_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				     DAOS_PC_RO, POOL_CAPA_READ);
	srv_acl_resetup(state);
	expect_list_capas_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				     DAOS_PC_RW, POOL_CAPA_READ |
				     POOL_CAPA_CREATE_CONT |
				     POOL_CAPA_DEL_CONT);
	srv_acl_resetup(state);
	expect_list_capas_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				     DAOS_PC_EX, POOL_CAPA_READ |
				     POOL_CAPA_CREATE_CONT |
				     POOL_CAPA_DEL_CONT);
	srv_acl_resetup(state);
	expect_list_capas_with_perms(DAOS_ACL_PERM_READ |
				     DAOS_ACL_PERM_CREATE_CONT,
				     DAOS_PC_RW,
				     POOL_CAPA_READ | POOL_CAPA_CREATE_CONT);
	srv_acl_resetup(state);
	expect_list_capas_with_perms(DAOS_ACL_PERM_READ |
				     DAOS_ACL_PERM_DEL_CONT,
				     DAOS_PC_RW,
				     POOL_CAPA_READ | POOL_CAPA_DEL_CONT);
}

static void
test_pool_get_capas_owner_overrides_group(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;

	init_default_cred(&cred);
	acl = get_acl_with_perms(DAOS_ACL_PERM_READ,
				 DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE);

	/* Owner-specific entry overrides group permissions */
	expect_pool_capas_with_acl(acl, &cred, DAOS_PC_RW, 0);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_no_match(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;

	/* Cred is neither owner user nor owner group */
	init_valid_cred(&cred, "fakeuser@", "fakegroup@", NULL, 0);
	acl = get_acl_with_perms(DAOS_ACL_PERM_READ, DAOS_ACL_PERM_READ);

	expect_pool_capas_with_acl(acl, &cred, DAOS_PC_RO, 0);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_owner_forbidden(void **state)
{
	expect_owner_capas_with_perms(0, DAOS_PC_RO, 0);
	srv_acl_resetup(state);
	expect_owner_capas_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_RO, 0);
	srv_acl_resetup(state);
	expect_owner_capas_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RW, 0);
	srv_acl_resetup(state);
	expect_owner_capas_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_EX, 0);
	srv_acl_resetup(state);
	expect_owner_capas_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_RW, 0);
	srv_acl_resetup(state);
	expect_owner_capas_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_EX, 0);
	srv_acl_resetup(state);
	expect_owner_capas_with_perms(DAOS_ACL_PERM_CREATE_CONT, DAOS_PC_RW, 0);
	srv_acl_resetup(state);
	expect_owner_capas_with_perms(DAOS_ACL_PERM_CREATE_CONT, DAOS_PC_EX, 0);
	srv_acl_resetup(state);
	expect_owner_capas_with_perms(DAOS_ACL_PERM_DEL_CONT, DAOS_PC_RW, 0);
	srv_acl_resetup(state);
	expect_owner_capas_with_perms(DAOS_ACL_PERM_DEL_CONT, DAOS_PC_EX, 0);
}

static void
test_pool_get_capas_group_forbidden(void **state)
{
	expect_group_capas_with_perms(0, DAOS_PC_RO, 0);
	srv_acl_resetup(state);
	expect_group_capas_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_RO, 0);
	srv_acl_resetup(state);
	expect_group_capas_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RW, 0);
	srv_acl_resetup(state);
	expect_group_capas_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_EX, 0);
	srv_acl_resetup(state);
	expect_group_capas_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_RW, 0);
	srv_acl_resetup(state);
	expect_group_capas_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_EX, 0);
	srv_acl_resetup(state);
	expect_group_capas_with_perms(DAOS_ACL_PERM_CREATE_CONT, DAOS_PC_RW, 0);
	srv_acl_resetup(state);
	expect_group_capas_with_perms(DAOS_ACL_PERM_CREATE_CONT, DAOS_PC_EX, 0);
	srv_acl_resetup(state);
	expect_group_capas_with_perms(DAOS_ACL_PERM_DEL_CONT, DAOS_PC_RW, 0);
	srv_acl_resetup(state);
	expect_group_capas_with_perms(DAOS_ACL_PERM_DEL_CONT, DAOS_PC_EX, 0);
}

static void
test_pool_get_capas_list_forbidden(void **state)
{
	expect_list_capas_with_perms(0, DAOS_PC_RO, 0);
	srv_acl_resetup(state);
	expect_list_capas_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_RO, 0);
	srv_acl_resetup(state);
	expect_list_capas_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RW, 0);
	srv_acl_resetup(state);
	expect_list_capas_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_EX, 0);
	srv_acl_resetup(state);
	expect_list_capas_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_RW, 0);
	srv_acl_resetup(state);
	expect_list_capas_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_EX, 0);
	srv_acl_resetup(state);
	expect_list_capas_with_perms(DAOS_ACL_PERM_CREATE_CONT, DAOS_PC_RW, 0);
	srv_acl_resetup(state);
	expect_list_capas_with_perms(DAOS_ACL_PERM_CREATE_CONT, DAOS_PC_EX, 0);
	srv_acl_resetup(state);
	expect_list_capas_with_perms(DAOS_ACL_PERM_DEL_CONT, DAOS_PC_RW, 0);
	srv_acl_resetup(state);
	expect_list_capas_with_perms(DAOS_ACL_PERM_DEL_CONT, DAOS_PC_EX, 0);
}

static void
test_pool_get_capas_no_owner_entry(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;

	init_default_cred(&cred);
	acl = get_acl_with_perms(0, DAOS_ACL_PERM_READ);
	assert_int_equal(daos_acl_remove_ace(&acl, DAOS_ACL_OWNER, NULL), 0);

	/*
	 * Cred is owner and in owner group, but there's no entry for owner,
	 * just owner group. Should still get access.
	 */
	expect_pool_capas_with_acl(acl, &cred, DAOS_PC_RO, POOL_CAPA_READ);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_no_owner_group_entry(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;

	init_valid_cred(&cred, "fakeuser@", TEST_GROUP, NULL, 0);
	acl = get_acl_with_perms(DAOS_ACL_PERM_READ, DAOS_ACL_PERM_READ);
	assert_int_equal(daos_acl_remove_ace(&acl, DAOS_ACL_OWNER_GROUP, NULL),
			 0);

	/*
	 * Cred is in owner group, but there's no entry for owner group.
	 * So expecting no capas.
	 */
	expect_pool_capas_with_acl(acl, &cred, DAOS_PC_RO, 0);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_no_owner_group_entry_list(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	static const char	*grps[] = { TEST_GROUP };

	init_valid_cred(&cred, "fakeuser@", "fakegroup@", grps, 1);
	acl = get_acl_with_perms(DAOS_ACL_PERM_READ, DAOS_ACL_PERM_READ);
	assert_int_equal(daos_acl_remove_ace(&acl, DAOS_ACL_OWNER_GROUP, NULL),
			 0);

	/*
	 * Cred is in owner group, but there's no entry for owner group.
	 * User should get no capas.
	 */
	expect_pool_capas_with_acl(acl, &cred, DAOS_PC_RO, 0);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
expect_everyone_capas_with_perms(uint64_t acl_perms,
				 uint64_t flags,
				 uint64_t exp_capas)
{
	struct daos_acl		*acl;
	struct daos_ace		*ace;
	d_iov_t			cred;

	init_valid_cred(&cred, TEST_USER, TEST_GROUP, NULL, 0);
	ace = daos_ace_create(DAOS_ACL_EVERYONE, NULL);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = acl_perms;
	acl = daos_acl_create(&ace, 1);

	/*
	 * In owner and owner group... but no entries for them.
	 * "Everyone" permissions should apply.
	 */
	expect_pool_capas_with_acl(acl, &cred, flags, exp_capas);

	daos_acl_free(acl);
	daos_ace_free(ace);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_everyone_success(void **state)
{
	expect_everyone_capas_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RO,
					 POOL_CAPA_READ);
	srv_acl_resetup(state);
	expect_everyone_capas_with_perms(DAOS_ACL_PERM_READ |
					 DAOS_ACL_PERM_WRITE,
					 DAOS_PC_RO,
					 POOL_CAPA_READ);
	srv_acl_resetup(state);
	expect_everyone_capas_with_perms(DAOS_ACL_PERM_READ |
					 DAOS_ACL_PERM_WRITE,
					 DAOS_PC_RW,
					 POOL_CAPA_READ |
					 POOL_CAPA_CREATE_CONT |
					 POOL_CAPA_DEL_CONT);
	srv_acl_resetup(state);
	expect_everyone_capas_with_perms(DAOS_ACL_PERM_READ |
					 DAOS_ACL_PERM_WRITE,
					 DAOS_PC_EX,
					 POOL_CAPA_READ |
					 POOL_CAPA_CREATE_CONT |
					 POOL_CAPA_DEL_CONT);
	srv_acl_resetup(state);
	expect_everyone_capas_with_perms(DAOS_ACL_PERM_READ |
					 DAOS_ACL_PERM_CREATE_CONT,
					 DAOS_PC_RW,
					 POOL_CAPA_READ |
					 POOL_CAPA_CREATE_CONT);
	srv_acl_resetup(state);
	expect_everyone_capas_with_perms(DAOS_ACL_PERM_READ |
					 DAOS_ACL_PERM_DEL_CONT,
					 DAOS_PC_RW,
					 POOL_CAPA_READ |
					 POOL_CAPA_DEL_CONT);
}

static void
test_pool_get_capas_everyone_forbidden(void **state)
{
	expect_everyone_capas_with_perms(0, DAOS_PC_RO, 0);
	srv_acl_resetup(state);
	expect_everyone_capas_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_RO, 0);
	srv_acl_resetup(state);
	expect_everyone_capas_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RW, 0);
	srv_acl_resetup(state);
	expect_everyone_capas_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_EX, 0);
	srv_acl_resetup(state);
	expect_everyone_capas_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_RW, 0);
	srv_acl_resetup(state);
	expect_everyone_capas_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_EX, 0);
	srv_acl_resetup(state);
	expect_everyone_capas_with_perms(DAOS_ACL_PERM_CREATE_CONT, DAOS_PC_RW,
					 0);
	srv_acl_resetup(state);
	expect_everyone_capas_with_perms(DAOS_ACL_PERM_CREATE_CONT, DAOS_PC_EX,
					 0);
	srv_acl_resetup(state);
	expect_everyone_capas_with_perms(DAOS_ACL_PERM_DEL_CONT, DAOS_PC_RW, 0);
	srv_acl_resetup(state);
	expect_everyone_capas_with_perms(DAOS_ACL_PERM_DEL_CONT, DAOS_PC_EX, 0);
}

static void
test_pool_get_capas_fall_thru_everyone(void **state)
{
	struct daos_acl		*acl;
	struct daos_ace		*ace;
	d_iov_t			cred;
	static const char	*grps[] = { "anotherbadgrp@" };

	/* Cred doesn't match owner or group */
	init_valid_cred(&cred, "baduser@", "badgrp@", grps, 1);
	/* Owner/group entries exist with no perms */
	acl = get_acl_with_perms(0, 0);

	/* Everyone entry allowing RW access */
	ace = daos_ace_create(DAOS_ACL_EVERYONE, NULL);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;
	assert_int_equal(daos_acl_add_ace(&acl, ace), 0);

	/*
	 * Cred doesn't match owner/group, falls through to everyone
	 */
	expect_pool_capas_with_acl(acl, &cred, DAOS_PC_RW, POOL_CAPA_READ |
			      POOL_CAPA_CREATE_CONT | POOL_CAPA_DEL_CONT);

	daos_acl_free(acl);
	daos_ace_free(ace);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_user_matches(void **state)
{
	struct daos_acl	*acl;
	struct daos_ace	*ace;
	d_iov_t		cred;
	const char	*username = "pooluser@";

	/* Ownership won't match the cred */
	init_valid_cred(&cred, username, "somegroup@", NULL, 0);

	/* User entry matches our cred */
	ace = daos_ace_create(DAOS_ACL_USER, username);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;
	acl = daos_acl_create(&ace, 1);

	expect_pool_capas_with_acl(acl, &cred, DAOS_PC_RW,
			      POOL_CAPA_READ | POOL_CAPA_CREATE_CONT |
			      POOL_CAPA_DEL_CONT);

	daos_acl_free(acl);
	daos_ace_free(ace);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_user_matches_second(void **state)
{
	struct daos_acl	*acl;
	size_t		num_aces = 2;
	struct daos_ace	*ace[num_aces];
	d_iov_t		cred;
	const char	*username = "pooluser@";

	init_valid_cred(&cred, username, "somegroup@", NULL, 0);

	/* Match is not the first in the list */
	ace[0] = daos_ace_create(DAOS_ACL_USER, "fakeuser@");
	ace[0]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[0]->dae_allow_perms = DAOS_ACL_PERM_READ;
	ace[1] = daos_ace_create(DAOS_ACL_USER, username);
	ace[1]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[1]->dae_allow_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;
	acl = daos_acl_create(ace, 2);

	expect_pool_capas_with_acl(acl, &cred, DAOS_PC_RW,
			      POOL_CAPA_READ | POOL_CAPA_CREATE_CONT |
			      POOL_CAPA_DEL_CONT);

	daos_acl_free(acl);
	free_ace_list(ace, num_aces);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_owner_beats_user(void **state)
{
	struct daos_acl		*acl;
	struct daos_ace		*ace;
	d_iov_t			cred;

	/* Cred user is the owner */
	init_valid_cred(&cred, TEST_USER, "somegroup@", NULL, 0);

	acl = get_acl_with_perms(DAOS_ACL_PERM_READ, DAOS_ACL_PERM_READ);

	/* User entry matches our cred */
	ace = daos_ace_create(DAOS_ACL_USER, TEST_USER);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;
	assert_int_equal(daos_acl_add_ace(&acl, ace), 0);

	/*
	 * Requesting RW - but owner ACE has RO. Owner overrides named user
	 * even though both match.
	 */
	expect_pool_capas_with_acl(acl, &cred, DAOS_PC_RW, 0);

	daos_acl_free(acl);
	daos_ace_free(ace);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_user_beats_owner_grp(void **state)
{
	struct daos_acl		*acl;
	struct daos_ace		*ace;
	d_iov_t			cred;
	const char		*username = "someuser@";

	/* Cred group is the owner group */
	init_valid_cred(&cred, username, TEST_GROUP, NULL, 0);

	acl = get_acl_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				 DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE);

	/* User entry matches our cred */
	ace = daos_ace_create(DAOS_ACL_USER, username);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = DAOS_ACL_PERM_READ;
	assert_int_equal(daos_acl_add_ace(&acl, ace), 0);

	/*
	 * Requesting RW - but user ACE has RO. User overrides owner-group
	 * even though both match.
	 * Owner-user doesn't match at all.
	 */
	expect_pool_capas_with_acl(acl, &cred, DAOS_PC_RW, 0);

	daos_acl_free(acl);
	daos_ace_free(ace);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_grp_matches(void **state)
{
	struct daos_acl		*acl;
	struct daos_ace		*ace;
	d_iov_t			cred;
	const char		*grpname = "wonderfulgroup@";

	/* Ownership won't match our creds */
	init_valid_cred(&cred, "someuser@", grpname, NULL, 0);

	/* Group entry matches our cred */
	ace = daos_ace_create(DAOS_ACL_GROUP, grpname);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;
	acl = daos_acl_create(&ace, 1);

	expect_pool_capas_with_acl(acl, &cred, DAOS_PC_RW, POOL_CAPA_READ |
			      POOL_CAPA_CREATE_CONT | POOL_CAPA_DEL_CONT);

	daos_acl_free(acl);
	daos_ace_free(ace);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_grp_matches_second(void **state)
{
	struct daos_acl		*acl;
	size_t			num_aces = 2;
	struct daos_ace		*ace[num_aces];
	d_iov_t			cred;
	const char		*grpname = "wonderfulgroup@";

	/* Ownership won't match our creds */
	init_valid_cred(&cred, "someuser@", grpname, NULL, 0);

	/* Match is not the first in the list */
	ace[0] = daos_ace_create(DAOS_ACL_GROUP, "fakegrp@");
	ace[0]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[0]->dae_allow_perms = DAOS_ACL_PERM_READ;
	ace[1] = daos_ace_create(DAOS_ACL_GROUP, grpname);
	ace[1]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[1]->dae_allow_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;
	acl = daos_acl_create(ace, 2);

	expect_pool_capas_with_acl(acl, &cred, DAOS_PC_RW, POOL_CAPA_READ |
			      POOL_CAPA_CREATE_CONT | POOL_CAPA_DEL_CONT);

	daos_acl_free(acl);
	free_ace_list(ace, num_aces);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_grp_matches_multiple(void **state)
{
	struct daos_acl		*acl;
	size_t			num_aces = 2;
	struct daos_ace		*ace[num_aces];
	d_iov_t			cred;
	static const char	*groups[] = { "group1@", "group2@" };

	/* Ownership won't match our creds */
	init_valid_cred(&cred, "someuser@", "somegroup@", groups, 2);

	/* Both groups in the ACL with different perms - should be unioned */
	ace[0] = daos_ace_create(DAOS_ACL_GROUP, groups[0]);
	ace[0]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[0]->dae_allow_perms = DAOS_ACL_PERM_READ;
	ace[1] = daos_ace_create(DAOS_ACL_GROUP, groups[1]);
	ace[1]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[1]->dae_allow_perms = DAOS_ACL_PERM_WRITE;
	acl = daos_acl_create(ace, 2);

	expect_pool_capas_with_acl(acl, &cred, DAOS_PC_RW, POOL_CAPA_READ |
			      POOL_CAPA_CREATE_CONT | POOL_CAPA_DEL_CONT);

	daos_acl_free(acl);
	free_ace_list(ace, num_aces);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_grp_no_match(void **state)
{
	struct daos_acl		*acl;
	size_t			num_aces = 3;
	struct daos_ace		*ace[num_aces];
	d_iov_t			cred;
	static const char	*groups[] = { "group1@", "group2@" };

	/* Not the owner */
	init_valid_cred(&cred, "someuser@", "somegroup@", groups, 2);

	/* Shouldn't match any of them */
	ace[0] = daos_ace_create(DAOS_ACL_GROUP, "fakegrp@");
	ace[0]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[0]->dae_allow_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;
	ace[1] = daos_ace_create(DAOS_ACL_GROUP, "fakegrp2@");
	ace[1]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[1]->dae_allow_perms = DAOS_ACL_PERM_READ;
	ace[2] = daos_ace_create(DAOS_ACL_OWNER_GROUP, NULL);
	ace[2]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[2]->dae_allow_perms = DAOS_ACL_PERM_READ;
	acl = daos_acl_create(ace, num_aces);

	expect_pool_capas_with_acl(acl, &cred, DAOS_PC_RW, 0);

	daos_acl_free(acl);
	free_ace_list(ace, num_aces);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_grp_check_includes_owner(void **state)
{
	struct daos_acl		*acl;
	size_t			num_aces = 2;
	struct daos_ace		*ace[num_aces];
	d_iov_t			cred;
	static const char	*groups[] = { "group1@", "group2@" };

	/* Ownership matches group */
	init_valid_cred(&cred, "someuser@", TEST_GROUP, groups, 2);

	/* Should get union of owner group and named groups */
	ace[0] = daos_ace_create(DAOS_ACL_OWNER_GROUP, NULL);
	ace[0]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[0]->dae_allow_perms = DAOS_ACL_PERM_WRITE;
	ace[1] = daos_ace_create(DAOS_ACL_GROUP, groups[1]);
	ace[1]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[1]->dae_allow_perms = DAOS_ACL_PERM_READ;
	acl = daos_acl_create(ace, 2);

	expect_pool_capas_with_acl(acl, &cred, DAOS_PC_RW, POOL_CAPA_READ |
			      POOL_CAPA_CREATE_CONT | POOL_CAPA_DEL_CONT);

	daos_acl_free(acl);
	free_ace_list(ace, num_aces);
	daos_iov_free(&cred);
}

static void
test_pool_get_capas_grps_beat_everyone(void **state)
{
	struct daos_acl		*acl;
	size_t			num_aces = 2;
	struct daos_ace		*ace[num_aces];
	d_iov_t			cred;
	static const char	*groups[] = { "group1@", "group2@" };

	/* Ownership doesn't match */
	init_valid_cred(&cred, "someuser@", "somegroup@", groups, 2);

	/*
	 * "Everyone" has more privs than the group, but the matching group
	 * privileges take priority.
	 */
	ace[0] = daos_ace_create(DAOS_ACL_EVERYONE, NULL);
	ace[0]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[0]->dae_allow_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;
	ace[1] = daos_ace_create(DAOS_ACL_GROUP, groups[1]);
	ace[1]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[1]->dae_allow_perms = 0;
	acl = daos_acl_create(ace, 2);

	expect_pool_capas_with_acl(acl, &cred, DAOS_PC_RO, 0);

	daos_acl_free(acl);
	free_ace_list(ace, num_aces);
	daos_iov_free(&cred);
}

/*
 * Container get capas tests
 */
static void
expect_cont_get_capas_flags_invalid(uint64_t invalid_flags)
{
	struct daos_acl		*valid_acl;
	d_iov_t			valid_cred;
	struct ownership	valid_owner;
	uint64_t		result = 0;

	init_default_ownership(&valid_owner);
	valid_acl = daos_acl_create(NULL, 0);
	assert_non_null(valid_acl);
	init_default_cred(&valid_cred);

	printf("Expecting flags %#lx invalid\n", invalid_flags);
	assert_int_equal(ds_sec_cont_get_capabilities(invalid_flags,
						      &valid_cred,
						      &valid_owner, valid_acl,
						      &result),
			 -DER_INVAL);

	daos_acl_free(valid_acl);
	daos_iov_free(&valid_cred);
}

static void
test_cont_get_capas_invalid_flags(void **state)
{
	expect_cont_get_capas_flags_invalid(0);
	expect_cont_get_capas_flags_invalid(1U << DAOS_COO_NBITS);
	expect_cont_get_capas_flags_invalid(DAOS_COO_RO | DAOS_COO_RW);
}

static void
test_cont_get_capas_null_inputs(void **state)
{
	d_iov_t			cred;
	struct ownership	ownership;
	struct daos_acl		*acl;
	uint64_t		result;

	init_default_cred(&cred);
	init_default_ownership(&ownership);
	acl = daos_acl_create(NULL, 0);

	assert_int_equal(ds_sec_cont_get_capabilities(DAOS_COO_RO, NULL,
						      &ownership, acl, &result),
			 -DER_INVAL);
	assert_int_equal(ds_sec_cont_get_capabilities(DAOS_COO_RO, &cred,
						      NULL, acl, &result),
			 -DER_INVAL);
	assert_int_equal(ds_sec_cont_get_capabilities(DAOS_COO_RO, &cred,
						      &ownership, NULL,
						      &result),
			 -DER_INVAL);
	assert_int_equal(ds_sec_cont_get_capabilities(DAOS_COO_RO, &cred,
						      &ownership, acl, NULL),
			 -DER_INVAL);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
expect_cont_get_capas_owner_invalid(char *user, char *group)
{
	struct daos_acl		*valid_acl;
	d_iov_t			valid_cred;
	struct ownership	invalid_owner;
	uint64_t		valid_flags = DAOS_PC_RO;
	uint64_t		result = 0;

	init_default_cred(&valid_cred);

	valid_acl = daos_acl_create(NULL, 0);
	assert_non_null(valid_acl);

	invalid_owner.user = user;
	invalid_owner.group = group;
	assert_int_equal(ds_sec_cont_get_capabilities(valid_flags,
						      &valid_cred,
						      &invalid_owner, valid_acl,
						      &result),
			 -DER_INVAL);

	daos_acl_free(valid_acl);
	daos_iov_free(&valid_cred);
}

static void
test_cont_get_capas_bad_owner(void **state)
{
	expect_cont_get_capas_owner_invalid(NULL, NULL);
	expect_cont_get_capas_owner_invalid(TEST_USER, NULL);
	expect_cont_get_capas_owner_invalid(NULL, TEST_GROUP);
	expect_cont_get_capas_owner_invalid("notavalidname", TEST_GROUP);
	expect_cont_get_capas_owner_invalid(TEST_USER, "notavalidname");
}

static void
test_cont_get_capas_bad_acl(void **state)
{
	struct daos_acl		*bad_acl;
	d_iov_t			cred;
	struct ownership	ownership;
	uint64_t		result;

	init_default_cred(&cred);
	init_default_ownership(&ownership);

	/* zeroed out - not a valid ACL */
	D_ALLOC(bad_acl, sizeof(struct daos_acl));
	assert_non_null(bad_acl);

	assert_int_equal(ds_sec_cont_get_capabilities(DAOS_PC_RO, &cred,
						      &ownership, bad_acl,
						      &result),
			 -DER_INVAL);

	D_FREE(bad_acl);
	daos_iov_free(&cred);
}

static void
test_cont_get_capas_bad_cred(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			bad_cred;
	struct ownership	ownership;
	uint64_t		result;
	uint8_t			bad_buf[32];
	size_t			i;
	Auth__Credential	cred = AUTH__CREDENTIAL__INIT;
	Auth__Token		token = AUTH__TOKEN__INIT;
	uint8_t			*buf;
	size_t			bufsize;

	init_default_ownership(&ownership);

	acl = daos_acl_create(NULL, 0);
	assert_non_null(acl);

	/* some random bytes that won't translate to an auth credential */
	for (i = 0; i < sizeof(bad_buf); i++)
		bad_buf[i] = (uint8_t)i;
	d_iov_set(&bad_cred, bad_buf, sizeof(bad_buf));

	assert_int_equal(ds_sec_cont_get_capabilities(DAOS_PC_RO, &bad_cred,
						      &ownership, acl,
						      &result),
			 -DER_INVAL);

	/* null data */
	d_iov_set(&bad_cred, NULL, 0);
	assert_int_equal(ds_sec_cont_get_capabilities(DAOS_PC_RO, &bad_cred,
						      &ownership, acl,
						      &result),
			 -DER_INVAL);

	/* Junk in token data */
	token.flavor = AUTH__FLAVOR__AUTH_SYS;
	token.data.data = bad_buf;
	token.data.len = sizeof(bad_buf);
	cred.token = &token;
	bufsize = auth__credential__get_packed_size(&cred);
	D_ALLOC(buf, bufsize);
	auth__credential__pack(&cred, buf);
	d_iov_set(&bad_cred, buf, bufsize);
	assert_int_equal(ds_sec_cont_get_capabilities(DAOS_PC_RO, &bad_cred,
						      &ownership, acl,
						      &result),
			 -DER_PROTO);
	D_FREE(buf);

	daos_acl_free(acl);
}

static void
expect_cont_capas_with_perms(uint64_t acl_perms, uint64_t flags,
			     uint64_t exp_capas)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct ownership	ownership;
	uint64_t		result = -1;

	/*
	 * Just a user specific permission, not the owner
	 */
	init_valid_cred(&cred, "specificuser@", TEST_GROUP, NULL, 0);
	acl = get_user_acl_with_perms("specificuser@", acl_perms);
	init_default_ownership(&ownership);

	printf("Perms: %#lx, Flags: %#lx\n", acl_perms, flags);
	assert_int_equal(ds_sec_cont_get_capabilities(flags, &cred, &ownership,
						      acl, &result),
			 0);

	assert_int_equal(result, exp_capas);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_cont_get_capas_success(void **state)
{
	expect_cont_capas_with_perms(DAOS_ACL_PERM_READ, DAOS_COO_RO,
				     CONT_CAPA_READ_DATA);
	expect_cont_capas_with_perms(DAOS_ACL_PERM_GET_PROP, DAOS_COO_RO,
				     CONT_CAPA_GET_PROP);
	expect_cont_capas_with_perms(DAOS_ACL_PERM_GET_ACL, DAOS_COO_RO,
				     CONT_CAPA_GET_ACL);
	expect_cont_capas_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				     DAOS_COO_RW,
				     CONT_CAPA_READ_DATA |
				     CONT_CAPA_WRITE_DATA);
	expect_cont_capas_with_perms(DAOS_ACL_PERM_SET_PROP |
				     DAOS_ACL_PERM_GET_PROP,
				     DAOS_COO_RW,
				     CONT_CAPA_SET_PROP |
				     CONT_CAPA_GET_PROP);
	expect_cont_capas_with_perms(DAOS_ACL_PERM_SET_ACL |
				     DAOS_ACL_PERM_GET_ACL |
				     DAOS_ACL_PERM_SET_OWNER,
				     DAOS_COO_RW,
				     CONT_CAPA_SET_ACL |
				     CONT_CAPA_GET_ACL |
				     CONT_CAPA_SET_OWNER);
	expect_cont_capas_with_perms(DAOS_ACL_PERM_READ |
				     DAOS_ACL_PERM_DEL_CONT,
				     DAOS_COO_RW,
				     CONT_CAPA_READ_DATA |
				     CONT_CAPA_DELETE);
	expect_cont_capas_with_perms(DAOS_ACL_PERM_CONT_ALL,
				     DAOS_COO_RW,
				     CONT_CAPAS_ALL);
	expect_cont_capas_with_perms(DAOS_ACL_PERM_CONT_ALL,
				     DAOS_COO_RO,
				     CONT_CAPAS_RO_MASK);
}

static void
test_cont_get_capas_denied(void **state)
{
	expect_cont_capas_with_perms(0, DAOS_COO_RO, 0);
	expect_cont_capas_with_perms(0, DAOS_COO_RW, 0);
	expect_cont_capas_with_perms(DAOS_ACL_PERM_READ, DAOS_COO_RW, 0);
	expect_cont_capas_with_perms(DAOS_ACL_PERM_GET_PROP, DAOS_COO_RW, 0);
	expect_cont_capas_with_perms(DAOS_ACL_PERM_GET_ACL, DAOS_COO_RW, 0);
	expect_cont_capas_with_perms(DAOS_ACL_PERM_WRITE, DAOS_COO_RW, 0);
	expect_cont_capas_with_perms(DAOS_ACL_PERM_DEL_CONT, DAOS_COO_RW, 0);
	expect_cont_capas_with_perms(DAOS_ACL_PERM_SET_PROP, DAOS_COO_RW, 0);
	expect_cont_capas_with_perms(DAOS_ACL_PERM_SET_ACL, DAOS_COO_RW, 0);
	expect_cont_capas_with_perms(DAOS_ACL_PERM_SET_OWNER, DAOS_COO_RW, 0);
}


static void
expect_cont_capas_with_owner_perms(uint64_t acl_perms, uint64_t flags,
				   uint64_t exp_capas)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct ownership	ownership;
	uint64_t		result = -1;

	/*
	 * Owner entry matched by cred
	 */
	init_valid_cred(&cred, TEST_USER, TEST_GROUP, NULL, 0);
	acl = get_acl_with_perms(acl_perms, 0);
	init_default_ownership(&ownership);

	printf("Perms: %#lx, Flags: %#lx\n", acl_perms, flags);
	assert_int_equal(ds_sec_cont_get_capabilities(flags, &cred, &ownership,
						      acl, &result),
			 0);

	assert_int_equal(result, exp_capas);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_cont_get_capas_owner_implicit_acl_access(void **state)
{
	/* Owner can always get/set ACL even if not explicitly granted perms */
	expect_cont_capas_with_owner_perms(0, DAOS_COO_RO, CONT_CAPA_GET_ACL);
	expect_cont_capas_with_owner_perms(0, DAOS_COO_RW,
					   CONT_CAPA_GET_ACL |
					   CONT_CAPA_SET_ACL);
	expect_cont_capas_with_owner_perms(DAOS_ACL_PERM_READ |
					   DAOS_ACL_PERM_WRITE,
					   DAOS_COO_RO,
					   CONT_CAPA_GET_ACL |
					   CONT_CAPA_READ_DATA);
	expect_cont_capas_with_owner_perms(DAOS_ACL_PERM_READ |
					   DAOS_ACL_PERM_WRITE,
					   DAOS_COO_RW,
					   CONT_CAPA_READ_DATA |
					   CONT_CAPA_WRITE_DATA |
					   CONT_CAPA_GET_ACL |
					   CONT_CAPA_SET_ACL);
	expect_cont_capas_with_owner_perms(DAOS_ACL_PERM_CONT_ALL,
					   DAOS_COO_RW,
					   CONT_CAPAS_ALL);
	expect_cont_capas_with_owner_perms(DAOS_ACL_PERM_CONT_ALL,
					   DAOS_COO_RO,
					   CONT_CAPAS_RO_MASK);
}

/*
 * Pool access tests
 */
static void
test_pool_can_connect(void **state)
{
	assert_false(ds_sec_pool_can_connect(0));
	assert_false(ds_sec_pool_can_connect(~POOL_CAPA_READ));

	assert_true(ds_sec_pool_can_connect(POOL_CAPA_READ));
	assert_true(ds_sec_pool_can_connect(POOL_CAPAS_RO_MASK));
	assert_true(ds_sec_pool_can_connect(POOL_CAPAS_ALL));
}

static void
test_pool_can_create_cont(void **state)
{
	assert_false(ds_sec_pool_can_create_cont(0));
	assert_false(ds_sec_pool_can_create_cont(~POOL_CAPA_CREATE_CONT));

	assert_true(ds_sec_pool_can_create_cont(POOL_CAPA_CREATE_CONT));
	assert_true(ds_sec_pool_can_create_cont(POOL_CAPAS_ALL));
}

static void
test_pool_can_delete_cont(void **state)
{
	assert_false(ds_sec_pool_can_delete_cont(0));
	assert_false(ds_sec_pool_can_delete_cont(~POOL_CAPA_DEL_CONT));

	assert_true(ds_sec_pool_can_delete_cont(POOL_CAPA_DEL_CONT));
	assert_true(ds_sec_pool_can_delete_cont(POOL_CAPA_READ |
						POOL_CAPA_CREATE_CONT |
						POOL_CAPA_DEL_CONT));
}

/*
 * Container access tests
 */
static void
test_cont_can_open(void **state)
{
	assert_false(ds_sec_cont_can_open(0));
	/* Need read access at minimum - write-only isn't allowed */
	assert_false(ds_sec_cont_can_open(~CONT_CAPAS_RO_MASK));

	assert_true(ds_sec_cont_can_open(CONT_CAPA_READ_DATA));
	assert_true(ds_sec_cont_can_open(CONT_CAPA_GET_PROP));
	assert_true(ds_sec_cont_can_open(CONT_CAPA_GET_ACL));
	assert_true(ds_sec_cont_can_open(CONT_CAPAS_RO_MASK));
	assert_true(ds_sec_cont_can_open(CONT_CAPAS_ALL));
}

static void
test_cont_can_delete(void **state)
{
	d_iov_t			cred;
	struct ownership	owner;
	struct daos_acl		*default_acl;
	struct daos_acl		*no_del_acl;
	struct daos_acl		*min_acl;

	init_default_cred(&cred);
	init_default_ownership(&owner);

	/* minimal good container ACL that allows delete */
	min_acl = get_acl_with_perms(DAOS_ACL_PERM_READ |
				     DAOS_ACL_PERM_DEL_CONT, 0);

	/* all container perms except delete */
	no_del_acl = get_acl_with_perms(DAOS_ACL_PERM_CONT_ALL &
					~DAOS_ACL_PERM_DEL_CONT, 0);

	/* default container ACL */
	default_acl = ds_sec_alloc_default_daos_cont_acl();

	/* Default ACL allows owner to delete it */
	assert_true(ds_sec_cont_can_delete(DAOS_PC_RW, &cred, &owner,
					   default_acl));

	/* Minimal ACL with RW access allowing delete */
	assert_true(ds_sec_cont_can_delete(DAOS_PC_RW, &cred, &owner, min_acl));

	/* Read-only pool flags will prevent deletion */
	assert_false(ds_sec_cont_can_delete(DAOS_PC_RO, &cred, &owner,
					    min_acl));

	/* Invalid inputs don't get any perms */
	assert_false(ds_sec_cont_can_delete(DAOS_PC_RW, NULL, NULL, NULL));

	/* doesn't have delete perms */
	assert_false(ds_sec_cont_can_delete(DAOS_PC_RW, &cred, &owner,
					    no_del_acl));

	daos_acl_free(min_acl);
	daos_acl_free(no_del_acl);
	daos_acl_free(default_acl);
	daos_iov_free(&cred);
}

static void
test_cont_can_get_props(void **state)
{
	assert_false(ds_sec_cont_can_get_props(0));
	assert_false(ds_sec_cont_can_get_props(~CONT_CAPA_GET_PROP));

	assert_true(ds_sec_cont_can_get_props(CONT_CAPAS_RO_MASK));
	assert_true(ds_sec_cont_can_get_props(CONT_CAPAS_ALL));
	assert_true(ds_sec_cont_can_get_props(CONT_CAPA_GET_PROP));
}

static void
test_cont_can_set_props(void **state)
{
	assert_false(ds_sec_cont_can_set_props(0));
	assert_false(ds_sec_cont_can_set_props(~CONT_CAPA_SET_PROP));
	assert_false(ds_sec_cont_can_set_props(CONT_CAPAS_RO_MASK));

	assert_true(ds_sec_cont_can_set_props(CONT_CAPAS_ALL));
	assert_true(ds_sec_cont_can_set_props(CONT_CAPA_SET_PROP));
}

static void
test_cont_can_get_acl(void **state)
{
	assert_false(ds_sec_cont_can_get_acl(0));
	assert_false(ds_sec_cont_can_get_acl(~CONT_CAPA_GET_ACL));

	assert_true(ds_sec_cont_can_get_acl(CONT_CAPAS_RO_MASK));
	assert_true(ds_sec_cont_can_get_acl(CONT_CAPAS_ALL));
	assert_true(ds_sec_cont_can_get_acl(CONT_CAPA_GET_ACL));
}

static void
test_cont_can_set_acl(void **state)
{
	assert_false(ds_sec_cont_can_set_acl(0));
	assert_false(ds_sec_cont_can_set_acl(~CONT_CAPA_SET_ACL));
	assert_false(ds_sec_cont_can_set_acl(CONT_CAPAS_RO_MASK));

	assert_true(ds_sec_cont_can_set_acl(CONT_CAPAS_ALL));
	assert_true(ds_sec_cont_can_set_acl(CONT_CAPA_SET_ACL));
}

static void
test_cont_can_set_owner(void **state)
{
	assert_false(ds_sec_cont_can_set_owner(0));
	assert_false(ds_sec_cont_can_set_owner(~CONT_CAPA_SET_OWNER));
	assert_false(ds_sec_cont_can_set_owner(CONT_CAPAS_RO_MASK));

	assert_true(ds_sec_cont_can_set_owner(CONT_CAPAS_ALL));
	assert_true(ds_sec_cont_can_set_owner(CONT_CAPA_SET_OWNER));
}

static void
test_cont_can_write_data(void **state)
{
	assert_false(ds_sec_cont_can_write_data(0));
	assert_false(ds_sec_cont_can_write_data(~CONT_CAPA_WRITE_DATA));
	assert_false(ds_sec_cont_can_write_data(CONT_CAPAS_RO_MASK));

	assert_true(ds_sec_cont_can_write_data(CONT_CAPAS_ALL));
	assert_true(ds_sec_cont_can_write_data(CONT_CAPA_WRITE_DATA));
}

static void
test_cont_can_read_data(void **state)
{
	assert_false(ds_sec_cont_can_read_data(0));
	assert_false(ds_sec_cont_can_read_data(~CONT_CAPA_READ_DATA));

	assert_true(ds_sec_cont_can_read_data(CONT_CAPAS_RO_MASK));
	assert_true(ds_sec_cont_can_read_data(CONT_CAPAS_ALL));
	assert_true(ds_sec_cont_can_read_data(CONT_CAPA_READ_DATA));
}

static void
test_get_rebuild_cont_capas(void **state)
{
	assert_int_equal(ds_sec_get_rebuild_cont_capabilities(),
			 CONT_CAPA_READ_DATA);
}

static void
test_get_admin_cont_capas(void **state)
{
	assert_int_equal(ds_sec_get_admin_cont_capabilities(),
			 CONT_CAPAS_ALL);
}

/* Convenience macro for unit tests */
#define ACL_UTEST(X)	cmocka_unit_test_setup_teardown(X, srv_acl_setup, \
							srv_acl_teardown)
int
main(void)
{
	const struct CMUnitTest tests[] = {
		ACL_UTEST(test_validate_creds_null_cred),
		ACL_UTEST(test_validate_creds_null_token_ptr),
		ACL_UTEST(test_validate_creds_empty_cred),
		ACL_UTEST(test_validate_creds_drpc_connect_failed),
		ACL_UTEST(test_validate_creds_drpc_call_failed),
		ACL_UTEST(test_validate_creds_drpc_call_null_response),
		ACL_UTEST(test_validate_creds_drpc_response_failure),
		ACL_UTEST(test_validate_creds_drpc_response_malformed_body),
		ACL_UTEST(test_validate_creds_drpc_response_null_token),
		ACL_UTEST(test_validate_creds_drpc_response_empty_token),
		ACL_UTEST(test_validate_creds_drpc_response_bad_status),
		ACL_UTEST(test_validate_creds_success),
		cmocka_unit_test(test_default_pool_acl),
		cmocka_unit_test(test_default_cont_acl),
		ACL_UTEST(test_pool_get_capas_invalid_flags),
		ACL_UTEST(test_pool_get_capas_null_input),
		ACL_UTEST(test_pool_get_capas_bad_owner),
		ACL_UTEST(test_pool_get_capas_bad_acl),
		ACL_UTEST(test_pool_get_capas_validate_cred_failed),
		ACL_UTEST(test_pool_get_capas_wrong_flavor),
		ACL_UTEST(test_pool_get_capas_bad_payload),
		ACL_UTEST(test_pool_get_capas_empty_acl),
		ACL_UTEST(test_pool_get_capas_owner_success),
		ACL_UTEST(test_pool_get_capas_group_success),
		ACL_UTEST(test_pool_get_capas_group_list_success),
		ACL_UTEST(test_pool_get_capas_owner_overrides_group),
		ACL_UTEST(test_pool_get_capas_no_match),
		ACL_UTEST(test_pool_get_capas_owner_forbidden),
		ACL_UTEST(test_pool_get_capas_group_forbidden),
		ACL_UTEST(test_pool_get_capas_list_forbidden),
		ACL_UTEST(test_pool_get_capas_no_owner_entry),
		ACL_UTEST(test_pool_get_capas_no_owner_group_entry),
		ACL_UTEST(test_pool_get_capas_no_owner_group_entry_list),
		ACL_UTEST(test_pool_get_capas_everyone_success),
		ACL_UTEST(test_pool_get_capas_everyone_forbidden),
		ACL_UTEST(test_pool_get_capas_fall_thru_everyone),
		ACL_UTEST(test_pool_get_capas_user_matches),
		ACL_UTEST(test_pool_get_capas_user_matches_second),
		ACL_UTEST(test_pool_get_capas_owner_beats_user),
		ACL_UTEST(test_pool_get_capas_user_beats_owner_grp),
		ACL_UTEST(test_pool_get_capas_grp_matches),
		ACL_UTEST(test_pool_get_capas_grp_matches_second),
		ACL_UTEST(test_pool_get_capas_grp_matches_multiple),
		ACL_UTEST(test_pool_get_capas_grp_no_match),
		ACL_UTEST(test_pool_get_capas_grp_check_includes_owner),
		ACL_UTEST(test_pool_get_capas_grps_beat_everyone),
		cmocka_unit_test(test_cont_get_capas_invalid_flags),
		cmocka_unit_test(test_cont_get_capas_null_inputs),
		cmocka_unit_test(test_cont_get_capas_bad_owner),
		cmocka_unit_test(test_cont_get_capas_bad_acl),
		cmocka_unit_test(test_cont_get_capas_bad_cred),
		cmocka_unit_test(test_cont_get_capas_success),
		cmocka_unit_test(test_cont_get_capas_denied),
		cmocka_unit_test(test_cont_get_capas_owner_implicit_acl_access),
		cmocka_unit_test(test_pool_can_connect),
		cmocka_unit_test(test_pool_can_create_cont),
		cmocka_unit_test(test_pool_can_delete_cont),
		cmocka_unit_test(test_cont_can_open),
		cmocka_unit_test(test_cont_can_delete),
		cmocka_unit_test(test_cont_can_get_props),
		cmocka_unit_test(test_cont_can_set_props),
		cmocka_unit_test(test_cont_can_get_acl),
		cmocka_unit_test(test_cont_can_set_acl),
		cmocka_unit_test(test_cont_can_set_owner),
		cmocka_unit_test(test_cont_can_write_data),
		cmocka_unit_test(test_cont_can_read_data),
		cmocka_unit_test(test_get_rebuild_cont_capas),
		cmocka_unit_test(test_get_admin_cont_capas),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

#undef ACL_UTEST
