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
#include <daos_srv/security.h>
#include "../srv_internal.h"

/*
 * Mocks
 */
char *ds_sec_server_socket_path = "/fake/socket/path";

/*
 * Test constants and defaults
 */
static const uint32_t TEST_UID = 4;
static const uint32_t TEST_GID = 100;

/*
 * Test helper functions
 */
static AuthToken *
create_valid_auth_token(uint32_t uid, uint32_t gid, uint32_t *gid_list,
			size_t num_gids)
{
	AuthToken	*token;
	AuthSys		*authsys;
	size_t		gid_list_size;

	D_ALLOC_PTR(token);
	auth_token__init(token);
	token->flavor = AUTH_FLAVOR__AUTH_SYS;
	token->has_flavor = true;

	D_ALLOC_PTR(authsys);
	auth_sys__init(authsys);
	authsys->uid = uid;
	authsys->has_uid = true;
	authsys->gid = gid;
	authsys->has_gid = true;

	if (num_gids > 0) {
		authsys->n_gids = num_gids;
		gid_list_size = sizeof(uint32_t) * num_gids;

		D_ALLOC(authsys->gids, gid_list_size);
		memcpy(authsys->gids, gid_list, gid_list_size);
	}

	token->data.len = auth_sys__get_packed_size(authsys);
	D_ALLOC(token->data.data, token->data.len);
	auth_sys__pack(authsys, token->data.data);
	token->has_data = true;

	auth_sys__free_unpacked(authsys, NULL);

	return token;
}

static void
init_valid_cred(d_iov_t *cred, uint32_t uid, uint32_t gid, uint32_t *gid_list,
		size_t num_gids)
{
	SecurityCredential	new_cred = SECURITY_CREDENTIAL__INIT;
	AuthToken		*token;
	uint8_t			*buf;
	size_t			buf_len;

	token = create_valid_auth_token(uid, gid, gid_list, num_gids);

	/* Initialize the cred with token */
	new_cred.token = token;
	buf_len = security_credential__get_packed_size(&new_cred);
	D_ALLOC(buf, buf_len);
	security_credential__pack(&new_cred, buf);
	d_iov_set(cred, buf, buf_len);

	/* Return the cred token from the drpc mock, too */
	pack_token_in_drpc_call_resp_body(token);

	auth_token__free_unpacked(token, NULL);
}

static void
init_default_cred(d_iov_t *cred)
{
	init_valid_cred(cred, TEST_UID, TEST_GID, NULL, 0);
}

static void
init_default_ugm(struct pool_prop_ugm *ugm)
{
	ugm->pp_uid = TEST_UID;
	ugm->pp_gid = TEST_GID;
	ugm->pp_mode = 777;
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

/*
 * Unit tests
 */

static void
test_validate_creds_null_cred(void **state)
{
	AuthToken *result = NULL;

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
	AuthToken	*result = NULL;
	d_iov_t		cred;

	d_iov_set(&cred, NULL, 0);

	assert_int_equal(ds_sec_validate_credentials(&cred, &result),
			 -DER_INVAL);
}

static void
test_check_pool_access_null_acl(void **state)
{
	d_iov_t			cred;
	struct pool_prop_ugm	ugm;

	init_default_cred(&cred);
	init_default_ugm(&ugm);

	assert_int_equal(ds_sec_check_pool_access(NULL, &ugm, &cred,
						      DAOS_PC_RO),
			 -DER_INVAL);

	daos_iov_free(&cred);
}

static void
test_check_pool_access_null_ugm(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;

	init_default_cred(&cred);
	acl = daos_acl_create(NULL, 0);

	assert_int_equal(ds_sec_check_pool_access(acl, NULL, &cred,
						      DAOS_PC_RO),
			 -DER_INVAL);

	daos_acl_free(acl);
}

static void
test_check_pool_access_null_cred(void **state)
{
	struct daos_acl		*acl;
	struct pool_prop_ugm	ugm;

	acl = daos_acl_create(NULL, 0);
	init_default_ugm(&ugm);

	assert_int_equal(ds_sec_check_pool_access(acl, &ugm, NULL,
						      DAOS_PC_RO),
			 -DER_INVAL);

	daos_acl_free(acl);
}

static void
test_check_pool_access_bad_acl(void **state)
{
	struct daos_acl		*bad_acl;
	d_iov_t			cred;
	struct pool_prop_ugm	ugm;

	init_default_cred(&cred);
	init_default_ugm(&ugm);

	/* zeroed out - not a valid ACL */
	D_ALLOC(bad_acl, sizeof(struct daos_acl));
	assert_non_null(bad_acl);

	assert_int_equal(ds_sec_check_pool_access(bad_acl, &ugm, &cred,
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
	struct pool_prop_ugm	ugm;

	init_default_cred(&cred);
	init_default_ugm(&ugm);
	acl = daos_acl_create(NULL, 0);

	/* drpc call failure will fail validation */
	drpc_call_return = -DER_UNKNOWN;

	assert_int_equal(ds_sec_check_pool_access(acl, &ugm, &cred,
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
	AuthToken		token = AUTH_TOKEN__INIT;
	struct pool_prop_ugm	ugm;

	init_default_cred(&cred);
	init_default_ugm(&ugm);
	acl = daos_acl_create(NULL, 0);

	token.flavor = auth_flavor;
	token.has_flavor = true;

	/* Put some junk in there */
	D_ALLOC(token.data.data, data_len);
	memset(token.data.data, 0xFF, data_len);
	token.data.len = data_len;
	token.has_data = true;

	pack_token_in_drpc_call_resp_body(&token);

	assert_int_equal(ds_sec_check_pool_access(acl, &ugm, &cred,
						      DAOS_PC_RO),
			 -DER_PROTO);

	daos_acl_free(acl);
	daos_iov_free(&cred);
	D_FREE(token.data.data);
}

static void
test_check_pool_access_not_authsys(void **state)
{
	expect_no_access_bad_authsys_payload(AUTH_FLAVOR__AUTH_NONE);
	expect_no_access_bad_authsys_payload(AUTH_FLAVOR__AUTH_SYS);
}

static void
test_check_pool_access_empty_acl(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct pool_prop_ugm	ugm;

	init_default_cred(&cred);
	init_default_ugm(&ugm);
	acl = daos_acl_create(NULL, 0);

	assert_int_equal(ds_sec_check_pool_access(acl, &ugm, &cred,
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
	struct pool_prop_ugm	ugm;

	init_default_ugm(&ugm);

	assert_int_equal(ds_sec_check_pool_access(acl, &ugm, cred,
						      requested_capas),
			 0);
}

static void
expect_owner_access_with_perms(uint64_t acl_perms, uint64_t requested_capas)
{
	struct daos_acl	*acl;
	d_iov_t		cred;

	/* Only matches owner */
	init_valid_cred(&cred, TEST_UID, TEST_GID + 1, NULL, 0);
	acl = get_acl_with_perms(acl_perms, 0);

	expect_access_with_acl(acl, &cred, requested_capas);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_owner_success(void **state)
{
	expect_owner_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RO);
	expect_owner_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				       DAOS_PC_RO);
	expect_owner_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				       DAOS_PC_RW);
	expect_owner_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				       DAOS_PC_EX);
}

static void
expect_group_access_with_perms(uint64_t acl_perms, uint64_t requested_capas)
{
	struct daos_acl	*acl;
	d_iov_t		cred;

	/* Only matches group */
	init_valid_cred(&cred, TEST_UID + 1, TEST_GID, NULL, 0);
	acl = get_acl_with_perms(0, acl_perms);

	expect_access_with_acl(acl, &cred, requested_capas);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_group_success(void **state)
{
	expect_group_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RO);
	expect_group_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				       DAOS_PC_RO);
	expect_group_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				       DAOS_PC_RW);
	expect_group_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				       DAOS_PC_EX);
}

static void
expect_list_access_with_perms(uint64_t acl_perms,
			      uint64_t requested_capas)
{
	struct daos_acl	*acl;
	d_iov_t		cred;
	uint32_t	gids[] = { TEST_GID - 1, TEST_GID, TEST_GID + 1 };

	/* Only matches group */
	init_valid_cred(&cred, TEST_UID + 1, TEST_GID + 1, gids,
			sizeof(gids) / sizeof(uint32_t));
	acl = get_acl_with_perms(0, acl_perms);

	expect_access_with_acl(acl, &cred, requested_capas);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_group_list_success(void **state)
{
	expect_list_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RO);
	expect_list_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				      DAOS_PC_RO);
	expect_list_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				      DAOS_PC_RW);
	expect_list_access_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				      DAOS_PC_EX);
}

static void
test_check_pool_access_owner_overrides_group(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct pool_prop_ugm	ugm;

	init_default_ugm(&ugm);
	init_default_cred(&cred);
	acl = get_acl_with_perms(DAOS_ACL_PERM_READ,
				 DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE);

	/* Owner-specific entry overrides group permissions */
	assert_int_equal(ds_sec_check_pool_access(acl, &ugm, &cred,
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
	struct pool_prop_ugm	ugm;

	init_default_ugm(&ugm);

	/* Cred is neither owner user nor owner group */
	init_valid_cred(&cred, TEST_UID + 1, TEST_GID + 1, NULL, 0);
	acl = get_acl_with_perms(DAOS_ACL_PERM_READ, DAOS_ACL_PERM_READ);

	assert_int_equal(ds_sec_check_pool_access(acl, &ugm, &cred,
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
	struct pool_prop_ugm	ugm;

	init_default_ugm(&ugm);
	init_default_cred(&cred);
	acl = get_acl_with_perms(acl_perms,
				 DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE);

	assert_int_equal(ds_sec_check_pool_access(acl, &ugm, &cred,
						      requested_capas),
			 -DER_NO_PERM);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_owner_forbidden(void **state)
{
	expect_no_owner_access_with_perms(0, DAOS_PC_RO);
	expect_no_owner_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RW);
	expect_no_owner_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_EX);
	expect_no_owner_access_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_RW);
	expect_no_owner_access_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_EX);
}

static void
expect_no_group_access_with_perms(uint64_t acl_perms, uint64_t requested_capas)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct pool_prop_ugm	ugm;

	init_default_ugm(&ugm);
	init_valid_cred(&cred, TEST_UID + 1, TEST_GID, NULL, 0);
	acl = get_acl_with_perms(0, acl_perms);

	assert_int_equal(ds_sec_check_pool_access(acl, &ugm, &cred,
						      requested_capas),
			 -DER_NO_PERM);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_group_forbidden(void **state)
{
	expect_no_group_access_with_perms(0, DAOS_PC_RO);
	expect_no_group_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RW);
	expect_no_group_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_EX);
	expect_no_group_access_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_RW);
	expect_no_group_access_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_EX);
}

static void
expect_no_list_access_with_perms(uint64_t acl_perms, uint64_t requested_capas)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct pool_prop_ugm	ugm;
	uint32_t		gids[] = { TEST_GID - 1, TEST_GID };

	/* owner group is in gid list only */
	init_valid_cred(&cred, TEST_UID + 1, TEST_GID + 1, gids,
			sizeof(gids) / sizeof(uint32_t));

	init_default_ugm(&ugm);
	acl = get_acl_with_perms(DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				 acl_perms);

	assert_int_equal(ds_sec_check_pool_access(acl, &ugm, &cred,
						      requested_capas),
			 -DER_NO_PERM);

	daos_acl_free(acl);
	daos_iov_free(&cred);
}

static void
test_check_pool_access_list_forbidden(void **state)
{
	expect_no_list_access_with_perms(0, DAOS_PC_RO);
	expect_no_list_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RW);
	expect_no_list_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_EX);
	expect_no_list_access_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_RW);
	expect_no_list_access_with_perms(DAOS_ACL_PERM_WRITE, DAOS_PC_EX);
}

static void
test_check_pool_access_no_owner_entry(void **state)
{
	struct daos_acl		*acl;
	d_iov_t			cred;
	struct pool_prop_ugm	ugm;

	init_default_ugm(&ugm);
	init_default_cred(&cred);
	acl = get_acl_with_perms(0, DAOS_ACL_PERM_READ);
	assert_int_equal(daos_acl_remove_ace(&acl, DAOS_ACL_OWNER, NULL), 0);

	/*
	 * Cred is owner and in owner group, but there's no entry for owner,
	 * just owner group. Should still get access.
	 */
	assert_int_equal(ds_sec_check_pool_access(acl, &ugm, &cred,
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
	struct pool_prop_ugm	ugm;

	init_default_ugm(&ugm);
	init_valid_cred(&cred, TEST_UID + 1, TEST_GID, NULL, 0);
	acl = get_acl_with_perms(DAOS_ACL_PERM_READ, DAOS_ACL_PERM_READ);
	assert_int_equal(daos_acl_remove_ace(&acl, DAOS_ACL_OWNER_GROUP, NULL),
			 0);

	/*
	 * Cred is in owner group, but there's no entry for owner group.
	 */
	assert_int_equal(ds_sec_check_pool_access(acl, &ugm, &cred,
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
	struct pool_prop_ugm	ugm;
	uint32_t		gids[] = { TEST_GID };

	init_default_ugm(&ugm);
	init_valid_cred(&cred, TEST_UID + 1, TEST_GID + 1, gids, 1);
	acl = get_acl_with_perms(DAOS_ACL_PERM_READ, DAOS_ACL_PERM_READ);
	assert_int_equal(daos_acl_remove_ace(&acl, DAOS_ACL_OWNER_GROUP, NULL),
			 0);

	/*
	 * Cred is in owner group, but there's no entry for owner group.
	 */
	assert_int_equal(ds_sec_check_pool_access(acl, &ugm, &cred,
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
	struct pool_prop_ugm	ugm;

	init_default_ugm(&ugm);
	init_valid_cred(&cred, TEST_UID, TEST_GID, NULL, 0);
	ace = daos_ace_create(DAOS_ACL_EVERYONE, NULL);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = acl_perms;
	acl = daos_acl_create(&ace, 1);

	/*
	 * In owner and owner group... but no entries for them.
	 * "Everyone" permissions should apply.
	 */
	assert_int_equal(ds_sec_check_pool_access(acl, &ugm, &cred,
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
	expect_everyone_access_with_perms(DAOS_ACL_PERM_READ |
					  DAOS_ACL_PERM_WRITE,
					  DAOS_PC_RO);
	expect_everyone_access_with_perms(DAOS_ACL_PERM_READ |
					  DAOS_ACL_PERM_WRITE,
					  DAOS_PC_RW);
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
	expect_everyone_no_access_with_perms(0, DAOS_PC_RW);
	expect_everyone_no_access_with_perms(0, DAOS_PC_EX);
	expect_everyone_no_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_RW);
	expect_everyone_no_access_with_perms(DAOS_ACL_PERM_READ, DAOS_PC_EX);
}

static void
test_check_pool_access_fall_thru_everyone(void **state)
{
	struct daos_acl		*acl;
	struct daos_ace		*ace;
	d_iov_t			cred;
	struct pool_prop_ugm	ugm;
	uint32_t		gids[] = { TEST_GID - 1 };

	init_default_ugm(&ugm);
	/* Cred doesn't match owner or group */
	init_valid_cred(&cred, TEST_UID + 1, TEST_GID + 1, gids, 1);
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
	assert_int_equal(ds_sec_check_pool_access(acl, &ugm, &cred, DAOS_PC_RW),
			 0);

	daos_acl_free(acl);
	daos_ace_free(ace);
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
		ACL_UTEST(test_check_pool_access_null_acl),
		ACL_UTEST(test_check_pool_access_null_ugm),
		ACL_UTEST(test_check_pool_access_null_cred),
		ACL_UTEST(test_check_pool_access_bad_acl),
		ACL_UTEST(test_check_pool_access_validate_cred_failed),
		ACL_UTEST(test_check_pool_access_not_authsys),
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
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

#undef ACL_UTEST
