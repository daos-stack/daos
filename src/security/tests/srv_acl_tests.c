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

	token = create_valid_auth_token(user, grp, grp_list, num_grps);

	/* Initialize the cred with token */
	new_cred.token = token;
	buf_len = auth__credential__get_packed_size(&new_cred);
	D_ALLOC(buf, buf_len);
	auth__credential__pack(&new_cred, buf);
	d_iov_set(cred, buf, buf_len);

	/* Return the cred token from the drpc mock, too */
	pack_token_in_drpc_call_resp_body(token);

	auth__token__free_unpacked(token, NULL);
}

static void
init_default_cred(d_iov_t *cred)
{
	init_valid_cred(cred, TEST_USER, TEST_GROUP, NULL, 0);
}

static void
init_default_ownership(struct pool_owner *owner)
{
	owner->user = TEST_USER;
	owner->group = TEST_GROUP;
}

static void
setup_drpc_with_default_token(void)
{
	Auth__Token *token = create_default_auth_token();

	pack_token_in_drpc_call_resp_body(token);

	auth__token__free_unpacked(token, NULL);
}

static void
free_ace_list(struct daos_ace **aces, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		daos_ace_free(aces[i]);
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
 * Unit tests
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

	drpc_call_return = -DER_UNKNOWN;
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
test_validate_creds_drpc_response_empty_token(void **state)
{
	d_iov_t		cred;
	Auth__Token	*result = NULL;
	Auth__Token	bad_token = AUTH__TOKEN__INIT;

	init_default_cred(&cred);

	bad_token.data.data = NULL;
	pack_token_in_drpc_call_resp_body(&bad_token);

	assert_int_equal(ds_sec_validate_credentials(&cred, &result),
			 -DER_PROTO);

	assert_null(result);

	daos_iov_free(&cred);
}

static void
test_validate_creds_success(void **state)
{
	d_iov_t		cred;
	Auth__Token	*result = NULL;
	Auth__Sys	*authsys;

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

	daos_iov_free(&cred);
	auth__sys__free_unpacked(authsys, NULL);
	auth__token__free_unpacked(result, NULL);
}

static void
test_check_pool_access_null_acl(void **state)
{
	d_iov_t			cred;
	struct pool_owner	ownership;

	init_default_cred(&cred);
	init_default_ownership(&ownership);

	assert_int_equal(ds_sec_check_pool_access(NULL, &ownership, &cred,
						  DAOS_PC_RO),
			 -DER_INVAL);

	daos_iov_free(&cred);
}

static void
test_check_pool_access_null_ownership(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;

	init_default_cred(&cred);
	acl = daos_acl_create(NULL, 0);

	assert_int_equal(ds_sec_check_pool_access(acl, NULL, &cred,
						  DAOS_PC_RO),
			 -DER_INVAL);

	daos_iov_free(&cred);
	daos_acl_free(acl);
}

static void
test_check_pool_access_bad_owner_user(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct pool_owner	ownership;

	init_default_cred(&cred);
	acl = daos_acl_create(NULL, 0);

	ownership.user = NULL;
	ownership.group = TEST_GROUP;

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RO),
			 -DER_INVAL);

	daos_iov_free(&cred);
	daos_acl_free(acl);
}

static void
test_check_pool_access_bad_owner_group(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct pool_owner	ownership;

	init_default_cred(&cred);
	acl = daos_acl_create(NULL, 0);

	ownership.user = TEST_USER;
	ownership.group = NULL;

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RO),
			 -DER_INVAL);

	daos_iov_free(&cred);
	daos_acl_free(acl);
}

static void
test_check_pool_access_null_cred(void **state)
{
	struct daos_acl		*acl;
	struct pool_owner	ownership;

	acl = daos_acl_create(NULL, 0);
	init_default_ownership(&ownership);

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, NULL,
						  DAOS_PC_RO),
			 -DER_INVAL);

	daos_acl_free(acl);
}

static void
test_check_pool_access_bad_acl(void **state)
{
	struct daos_acl		*bad_acl;
	d_iov_t			cred;
	struct pool_owner	ownership;

	init_default_cred(&cred);
	init_default_ownership(&ownership);

	/* zeroed out - not a valid ACL */
	D_ALLOC(bad_acl, sizeof(struct daos_acl));
	assert_non_null(bad_acl);

	assert_int_equal(ds_sec_check_pool_access(bad_acl, &ownership, &cred,
						  DAOS_PC_RO),
			 -DER_INVAL);

	D_FREE(bad_acl);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_validate_cred_failed(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct pool_owner	ownership;

	init_default_cred(&cred);
	init_default_ownership(&ownership);
	acl = daos_acl_create(NULL, 0);

	/* drpc call failure will fail validation */
	drpc_call_return = -DER_UNKNOWN;
	drpc_call_resp_return_ptr = NULL;

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RO),
			 drpc_call_return);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
expect_no_access_bad_authsys_payload(int auth_flavor)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	size_t			data_len = 8;
	Auth__Token		token = AUTH__TOKEN__INIT;
	struct pool_owner	ownership;

	init_default_cred(&cred);
	init_default_ownership(&ownership);
	acl = daos_acl_create(NULL, 0);

	token.flavor = auth_flavor;

	/* Put some junk in there */
	D_ALLOC(token.data.data, data_len);
	memset(token.data.data, 0xFF, data_len);
	token.data.len = data_len;

	pack_token_in_drpc_call_resp_body(&token);

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RO),
			 -DER_PROTO);

	daos_acl_free(acl);
	daos_iov_free(&cred);
	D_FREE(token.data.data);
}

static void
test_check_pool_access_wrong_flavor(void **state)
{
	expect_no_access_bad_authsys_payload(AUTH__FLAVOR__AUTH_NONE);
}

static void
test_check_pool_access_bad_payload(void **state)
{
	expect_no_access_bad_authsys_payload(AUTH__FLAVOR__AUTH_SYS);
}

static void
test_check_pool_access_empty_acl(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct pool_owner	ownership;

	init_default_cred(&cred);
	init_default_ownership(&ownership);
	acl = daos_acl_create(NULL, 0);

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RO),
			 -DER_NO_PERM);

	daos_acl_free(acl);
	daos_iov_free(&cred);
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

static void
expect_access_with_acl(struct daos_acl *acl, d_iov_t *cred,
		       uint64_t requested_capas)
{
	struct pool_owner	ownership;

	init_default_ownership(&ownership);

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, cred,
						      requested_capas),
			 0);
}

static void
expect_owner_access_with_perms(uint64_t acl_perms, uint64_t requested_capas)
{
	struct daos_acl	*acl;
	d_iov_t		cred;

	/* Only matches owner */
	init_valid_cred(&cred, TEST_USER, "somerandomgroup@", NULL, 0);
	acl = get_acl_with_perms(acl_perms, 0);

	expect_access_with_acl(acl, &cred, requested_capas);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_owner_success(void **state)
{
	expect_owner_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RO);
	srv_acl_resetup(state);
	expect_owner_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				       DAOS_PC_RO);
	srv_acl_resetup(state);
	expect_owner_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				       DAOS_PC_RW);
	srv_acl_resetup(state);
	expect_owner_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				       DAOS_PC_EX);
}

static void
expect_group_access_with_perms(uint64_t acl_perms, uint64_t requested_capas)
{
	struct daos_acl	*acl;
	d_iov_t		cred;

	/* Only matches group */
	init_valid_cred(&cred, "randomuser@", TEST_GROUP, NULL, 0);
	acl = get_acl_with_perms(0, acl_perms);

	expect_access_with_acl(acl, &cred, requested_capas);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_group_success(void **state)
{
	expect_group_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RO);
	srv_acl_resetup(state);
	expect_group_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				       DAOS_PC_RO);
	srv_acl_resetup(state);
	expect_group_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				       DAOS_PC_RW);
	srv_acl_resetup(state);
	expect_group_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				       DAOS_PC_EX);
}

static void
expect_list_access_with_perms(uint64_t acl_perms,
			      uint64_t requested_capas)
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

	expect_access_with_acl(acl, &cred, requested_capas);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_group_list_success(void **state)
{
	expect_list_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RO);
	srv_acl_resetup(state);
	expect_list_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				      DAOS_PC_RO);
	srv_acl_resetup(state);
	expect_list_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				      DAOS_PC_RW);
	srv_acl_resetup(state);
	expect_list_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				      DAOS_PC_EX);
}

static void
test_check_pool_access_owner_overrides_group(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct pool_owner	ownership;

	init_default_ownership(&ownership);
	init_default_cred(&cred);
	acl = get_acl_with_perms(DAOS_ACL_PERM_READ,
				 DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE);

	/* Owner-specific entry overrides group permissions */
	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RW),
			 -DER_NO_PERM);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_no_match(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct pool_owner	ownership;

	init_default_ownership(&ownership);

	/* Cred is neither owner user nor owner group */
	init_valid_cred(&cred, "fakeuser@", "fakegroup@", NULL, 0);
	acl = get_acl_with_perms(DAOS_ACL_PERM_READ, DAOS_ACL_PERM_READ);

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RO),
			 -DER_NO_PERM);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
expect_no_owner_access_with_perms(uint64_t acl_perms, uint64_t requested_capas)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct pool_owner	ownership;

	init_default_ownership(&ownership);
	init_default_cred(&cred);
	acl = get_acl_with_perms(acl_perms,
				 DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE);

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  requested_capas),
			 -DER_NO_PERM);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_owner_forbidden(void **state)
{
	expect_no_owner_access_with_perms(0, DAOS_PC_RO);
	srv_acl_resetup(state);
	expect_no_owner_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RW);
	srv_acl_resetup(state);
	expect_no_owner_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_EX);
	srv_acl_resetup(state);
	expect_no_owner_access_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_RW);
	srv_acl_resetup(state);
	expect_no_owner_access_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_EX);
}

static void
expect_no_group_access_with_perms(uint64_t acl_perms, uint64_t requested_capas)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct pool_owner	ownership;

	init_default_ownership(&ownership);
	init_valid_cred(&cred, "wronguser@", "wronggroup@", NULL, 0);
	acl = get_acl_with_perms(0, acl_perms);

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  requested_capas),
			 -DER_NO_PERM);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_group_forbidden(void **state)
{
	expect_no_group_access_with_perms(0, DAOS_PC_RO);
	srv_acl_resetup(state);
	expect_no_group_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RW);
	srv_acl_resetup(state);
	expect_no_group_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_EX);
	srv_acl_resetup(state);
	expect_no_group_access_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_RW);
	srv_acl_resetup(state);
	expect_no_group_access_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_EX);
}

static void
expect_no_list_access_with_perms(uint64_t acl_perms, uint64_t requested_capas)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct pool_owner	ownership;
	static const char	*grps[] = { "wronggroup@", TEST_GROUP };

	/* owner group is in list only */
	init_valid_cred(&cred, "wronguser@", "badgroup@", grps,
			sizeof(grps) / sizeof(char *));

	init_default_ownership(&ownership);
	acl = get_acl_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				 acl_perms);

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  requested_capas),
			 -DER_NO_PERM);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_list_forbidden(void **state)
{
	expect_no_list_access_with_perms(0, DAOS_PC_RO);
	srv_acl_resetup(state);
	expect_no_list_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RW);
	srv_acl_resetup(state);
	expect_no_list_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_EX);
	srv_acl_resetup(state);
	expect_no_list_access_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_RW);
	srv_acl_resetup(state);
	expect_no_list_access_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_EX);
}

static void
test_check_pool_access_no_owner_entry(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct pool_owner	ownership;

	init_default_ownership(&ownership);
	init_default_cred(&cred);
	acl = get_acl_with_perms(0, DAOS_ACL_PERM_READ);
	assert_int_equal(daos_acl_remove_ace(&acl, DAOS_ACL_OWNER, NULL), 0);

	/*
	 * Cred is owner and in owner group, but there's no entry for owner,
	 * just owner group. Should still get access.
	 */
	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RO),
			 0);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_no_owner_group_entry(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct pool_owner	ownership;

	init_default_ownership(&ownership);
	init_valid_cred(&cred, "fakeuser@", TEST_GROUP, NULL, 0);
	acl = get_acl_with_perms(DAOS_ACL_PERM_READ, DAOS_ACL_PERM_READ);
	assert_int_equal(daos_acl_remove_ace(&acl, DAOS_ACL_OWNER_GROUP, NULL),
			 0);

	/*
	 * Cred is in owner group, but there's no entry for owner group.
	 */
	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						      DAOS_PC_RO),
			 -DER_NO_PERM);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_no_owner_group_entry_list(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct pool_owner	ownership;
	static const char	*grps[] = { TEST_GROUP };

	init_default_ownership(&ownership);
	init_valid_cred(&cred, "fakeuser@", "fakegroup@", grps, 1);
	acl = get_acl_with_perms(DAOS_ACL_PERM_READ, DAOS_ACL_PERM_READ);
	assert_int_equal(daos_acl_remove_ace(&acl, DAOS_ACL_OWNER_GROUP, NULL),
			 0);

	/*
	 * Cred is in owner group, but there's no entry for owner group.
	 */
	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						      DAOS_PC_RO),
			 -DER_NO_PERM);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
expect_everyone_gets_result_with_perms(uint64_t acl_perms,
				       uint64_t requested_capas,
				       int expected_result)
{
	struct daos_acl		*acl;
	struct daos_ace		*ace;
	d_iov_t			cred;
	struct pool_owner	ownership;

	init_default_ownership(&ownership);
	init_valid_cred(&cred, TEST_USER, TEST_GROUP, NULL, 0);
	ace = daos_ace_create(DAOS_ACL_EVERYONE, NULL);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = acl_perms;
	acl = daos_acl_create(&ace, 1);

	/*
	 * In owner and owner group... but no entries for them.
	 * "Everyone" permissions should apply.
	 */
	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  requested_capas),
			 expected_result);

	daos_acl_free(acl);
	daos_ace_free(ace);
	daos_iov_free(&cred);
}

static void
expect_everyone_access_with_perms(uint64_t acl_perms, uint64_t requested_capas)
{
	expect_everyone_gets_result_with_perms(acl_perms, requested_capas, 0);
}

static void
test_check_pool_access_everyone_success(void **state)
{
	expect_everyone_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RO);
	srv_acl_resetup(state);
	expect_everyone_access_with_perms(DAOS_ACL_PERM_READ |
					  DAOS_ACL_PERM_WRITE,
					  DAOS_PC_RO);
	srv_acl_resetup(state);
	expect_everyone_access_with_perms(DAOS_ACL_PERM_READ |
					  DAOS_ACL_PERM_WRITE,
					  DAOS_PC_RW);
	srv_acl_resetup(state);
	expect_everyone_access_with_perms(DAOS_ACL_PERM_READ |
					  DAOS_ACL_PERM_WRITE,
					  DAOS_PC_EX);
}

static void
expect_everyone_no_access_with_perms(uint64_t acl_perms,
				     uint64_t requested_capas)
{
	expect_everyone_gets_result_with_perms(acl_perms, requested_capas,
					       -DER_NO_PERM);
}

static void
test_check_pool_access_everyone_forbidden(void **state)
{
	expect_everyone_no_access_with_perms(0, DAOS_PC_RO);
	srv_acl_resetup(state);
	expect_everyone_no_access_with_perms(0, DAOS_PC_RW);
	srv_acl_resetup(state);
	expect_everyone_no_access_with_perms(0, DAOS_PC_EX);
	srv_acl_resetup(state);
	expect_everyone_no_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RW);
	srv_acl_resetup(state);
	expect_everyone_no_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_EX);
}

static void
test_check_pool_access_fall_thru_everyone(void **state)
{
	struct daos_acl		*acl;
	struct daos_ace		*ace;
	d_iov_t			cred;
	struct pool_owner	ownership;
	static const char	*grps[] = { "anotherbadgrp@" };

	init_default_ownership(&ownership);
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
	 * Cred doesn't match owner/group, falls thru to everyone
	 */
	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RW),
			 0);

	daos_acl_free(acl);
	daos_ace_free(ace);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_user_matches(void **state)
{
	struct daos_acl		*acl;
	struct daos_ace		*ace;
	d_iov_t			cred;
	struct pool_owner	ownership;

	/* Ownership won't match our creds */
	ownership.user = "someuser@";
	ownership.group = "somegroup@";

	init_default_cred(&cred);

	/* User entry matches our cred */
	ace = daos_ace_create(DAOS_ACL_USER, TEST_USER);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;
	acl = daos_acl_create(&ace, 1);

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RW),
			 0);

	daos_acl_free(acl);
	daos_ace_free(ace);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_user_matches_second(void **state)
{
	struct daos_acl		*acl;
	size_t			num_aces = 2;
	struct daos_ace		*ace[num_aces];
	d_iov_t			cred;
	struct pool_owner	ownership;

	/* Ownership won't match our creds */
	ownership.user = "someuser@";
	ownership.group = "somegroup@";

	init_default_cred(&cred);

	/* Match is not the first in the list */
	ace[0] = daos_ace_create(DAOS_ACL_USER, "fakeuser@");
	ace[0]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[0]->dae_allow_perms = DAOS_ACL_PERM_READ;
	ace[1] = daos_ace_create(DAOS_ACL_USER, TEST_USER);
	ace[1]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[1]->dae_allow_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;
	acl = daos_acl_create(ace, 2);

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RW),
			 0);

	daos_acl_free(acl);
	free_ace_list(ace, num_aces);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_owner_beats_user(void **state)
{
	struct daos_acl		*acl;
	struct daos_ace		*ace;
	d_iov_t			cred;
	struct pool_owner	ownership;

	/* Owner matches our creds */
	ownership.user = TEST_USER;
	ownership.group = "somegroup@";

	init_default_cred(&cred);

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
	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RW),
			 -DER_NO_PERM);

	daos_acl_free(acl);
	daos_ace_free(ace);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_user_beats_owner_grp(void **state)
{
	struct daos_acl		*acl;
	struct daos_ace		*ace;
	d_iov_t			cred;
	struct pool_owner	ownership;

	/* Owner group matches our creds */
	ownership.user = "someuser@";
	ownership.group = TEST_GROUP;

	init_default_cred(&cred);

	acl = get_acl_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				 DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE);

	/* User entry matches our cred */
	ace = daos_ace_create(DAOS_ACL_USER, TEST_USER);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = DAOS_ACL_PERM_READ;
	assert_int_equal(daos_acl_add_ace(&acl, ace), 0);

	/*
	 * Requesting RW - but user ACE has RO. User overrides owner-group
	 * even though both match.
	 * Owner-user doesn't match at all.
	 */
	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RW),
			 -DER_NO_PERM);

	daos_acl_free(acl);
	daos_ace_free(ace);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_grp_matches(void **state)
{
	struct daos_acl		*acl;
	struct daos_ace		*ace;
	d_iov_t			cred;
	struct pool_owner	ownership;

	/* Ownership won't match our creds */
	ownership.user = "someuser@";
	ownership.group = "somegroup@";

	init_default_cred(&cred);

	/* Group entry matches our cred */
	ace = daos_ace_create(DAOS_ACL_GROUP, TEST_GROUP);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;
	acl = daos_acl_create(&ace, 1);

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RW),
			 0);

	daos_acl_free(acl);
	daos_ace_free(ace);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_grp_matches_second(void **state)
{
	struct daos_acl		*acl;
	size_t			num_aces = 2;
	struct daos_ace		*ace[num_aces];
	d_iov_t			cred;
	struct pool_owner	ownership;

	/* Ownership won't match our creds */
	ownership.user = "someuser@";
	ownership.group = "somegroup@";

	init_default_cred(&cred);

	/* Match is not the first in the list */
	ace[0] = daos_ace_create(DAOS_ACL_GROUP, "fakegrp@");
	ace[0]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[0]->dae_allow_perms = DAOS_ACL_PERM_READ;
	ace[1] = daos_ace_create(DAOS_ACL_GROUP, TEST_GROUP);
	ace[1]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[1]->dae_allow_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;
	acl = daos_acl_create(ace, 2);

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RW),
			 0);

	daos_acl_free(acl);
	free_ace_list(ace, num_aces);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_grp_matches_multiple(void **state)
{
	struct daos_acl		*acl;
	size_t			num_aces = 2;
	struct daos_ace		*ace[num_aces];
	d_iov_t			cred;
	struct pool_owner	ownership;
	static const char	*groups[] = { "group1@", "group2@" };

	/* Ownership won't match our creds */
	ownership.user = "someuser@";
	ownership.group = "somegroup@";

	init_valid_cred(&cred, TEST_USER, TEST_GROUP, groups, 2);

	/* Both groups in the ACL with different perms - should be unioned */
	ace[0] = daos_ace_create(DAOS_ACL_GROUP, groups[0]);
	ace[0]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[0]->dae_allow_perms = DAOS_ACL_PERM_READ;
	ace[1] = daos_ace_create(DAOS_ACL_GROUP, groups[1]);
	ace[1]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[1]->dae_allow_perms = DAOS_ACL_PERM_WRITE;
	acl = daos_acl_create(ace, 2);

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RW),
			 0);

	daos_acl_free(acl);
	free_ace_list(ace, num_aces);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_grp_no_match(void **state)
{
	struct daos_acl		*acl;
	size_t			num_aces = 2;
	struct daos_ace		*ace[num_aces];
	d_iov_t			cred;
	struct pool_owner	ownership;
	static const char	*groups[] = { "group1@", "group2@" };

	/* Ownership won't match our creds */
	ownership.user = "someuser@";
	ownership.group = "somegroup@";

	init_valid_cred(&cred, TEST_USER, TEST_GROUP, groups, 2);

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
	acl = daos_acl_create(ace, 2);

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RO),
			 -DER_NO_PERM);

	daos_acl_free(acl);
	free_ace_list(ace, num_aces);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_grp_check_includes_owner(void **state)
{
	struct daos_acl		*acl;
	size_t			num_aces = 2;
	struct daos_ace		*ace[num_aces];
	d_iov_t			cred;
	struct pool_owner	ownership;
	static const char	*groups[] = { "group1@", "group2@" };

	/* Ownership matches group */
	ownership.user = "someuser@";
	ownership.group = TEST_GROUP;

	init_valid_cred(&cred, TEST_USER, TEST_GROUP, groups, 2);

	/* Should get union of owner group and named groups */
	ace[0] = daos_ace_create(DAOS_ACL_OWNER_GROUP, NULL);
	ace[0]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[0]->dae_allow_perms = DAOS_ACL_PERM_WRITE;
	ace[1] = daos_ace_create(DAOS_ACL_GROUP, groups[1]);
	ace[1]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace[1]->dae_allow_perms = DAOS_ACL_PERM_READ;
	acl = daos_acl_create(ace, 2);

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RW),
			 0);

	daos_acl_free(acl);
	free_ace_list(ace, num_aces);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_grps_beat_everyone(void **state)
{
	struct daos_acl		*acl;
	size_t			num_aces = 2;
	struct daos_ace		*ace[num_aces];
	d_iov_t			cred;
	struct pool_owner	ownership;
	static const char	*groups[] = { "group1@", "group2@" };

	/* Ownership doesn't match */
	ownership.user = "someuser@";
	ownership.group = "somegroup@";

	init_valid_cred(&cred, TEST_USER, TEST_GROUP, groups, 2);

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

	assert_int_equal(ds_sec_check_pool_access(acl, &ownership, &cred,
						  DAOS_PC_RO),
			 -DER_NO_PERM);

	daos_acl_free(acl);
	free_ace_list(ace, num_aces);
	daos_iov_free(&cred);
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
		ACL_UTEST(test_validate_creds_drpc_response_empty_token),
		ACL_UTEST(test_validate_creds_success),
		ACL_UTEST(test_check_pool_access_null_acl),
		ACL_UTEST(test_check_pool_access_null_ownership),
		ACL_UTEST(test_check_pool_access_bad_owner_user),
		ACL_UTEST(test_check_pool_access_bad_owner_group),
		ACL_UTEST(test_check_pool_access_null_cred),
		ACL_UTEST(test_check_pool_access_bad_acl),
		ACL_UTEST(test_check_pool_access_validate_cred_failed),
		ACL_UTEST(test_check_pool_access_wrong_flavor),
		ACL_UTEST(test_check_pool_access_bad_payload),
		ACL_UTEST(test_check_pool_access_empty_acl),
		ACL_UTEST(test_check_pool_access_owner_success),
		ACL_UTEST(test_check_pool_access_group_success),
		ACL_UTEST(test_check_pool_access_group_list_success),
		ACL_UTEST(test_check_pool_access_owner_overrides_group),
		ACL_UTEST(test_check_pool_access_no_match),
		ACL_UTEST(test_check_pool_access_owner_forbidden),
		ACL_UTEST(test_check_pool_access_group_forbidden),
		ACL_UTEST(test_check_pool_access_list_forbidden),
		ACL_UTEST(test_check_pool_access_no_owner_entry),
		ACL_UTEST(test_check_pool_access_no_owner_group_entry),
		ACL_UTEST(test_check_pool_access_no_owner_group_entry_list),
		ACL_UTEST(test_check_pool_access_everyone_success),
		ACL_UTEST(test_check_pool_access_everyone_forbidden),
		ACL_UTEST(test_check_pool_access_fall_thru_everyone),
		ACL_UTEST(test_check_pool_access_user_matches),
		ACL_UTEST(test_check_pool_access_user_matches_second),
		ACL_UTEST(test_check_pool_access_owner_beats_user),
		ACL_UTEST(test_check_pool_access_user_beats_owner_grp),
		ACL_UTEST(test_check_pool_access_grp_matches),
		ACL_UTEST(test_check_pool_access_grp_matches_second),
		ACL_UTEST(test_check_pool_access_grp_matches_multiple),
		ACL_UTEST(test_check_pool_access_grp_no_match),
		ACL_UTEST(test_check_pool_access_grp_check_includes_owner),
		ACL_UTEST(test_check_pool_access_grps_beat_everyone),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

#undef ACL_UTEST
