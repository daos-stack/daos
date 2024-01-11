/**
 * (C) Copyright 2018-2023 Intel Corporation.
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
#include <daos/tests_lib.h>
#include <daos_types.h>
#include <daos_errno.h>
#include <daos/common.h>
#include <daos/drpc.h>
#include <daos/drpc_modules.h>
#include <daos/agent.h>
#include <daos/security.h>
#include <string.h>
#include <grp.h>
#include <pwd.h>
#include <linux/limits.h>

#include "../auth.pb-c.h"
#include "drpc_mocks.h"
#include "sec_test_util.h"

/* unpacked content of response body */
static Auth__Credential *drpc_call_resp_return_auth_cred;
char *dc_agent_sockpath;

static void
init_default_drpc_resp_auth_credential(void)
{
	D_ALLOC_PTR(drpc_call_resp_return_auth_cred);
	auth__credential__init(drpc_call_resp_return_auth_cred);

	D_ALLOC_PTR(drpc_call_resp_return_auth_cred->token);
	auth__token__init(drpc_call_resp_return_auth_cred->token);

	D_ALLOC_PTR(drpc_call_resp_return_auth_cred->verifier);
	auth__token__init(drpc_call_resp_return_auth_cred->verifier);
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
	init_drpc_resp_with_cred(drpc_call_resp_return_auth_cred);
}

void
free_drpc_call_resp_auth_credential()
{
	auth__credential__free_unpacked(drpc_call_resp_return_auth_cred,
					NULL);
}

/*
 * Unit test setup and teardown
 */

static int
setup_security_mocks(void **state)
{
	/* Initialize mock values to something sane */
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
	assert_rc_equal(dc_sec_request_creds(NULL), -DER_INVAL);
}

static void
test_request_credentials_succeeds_with_good_values(void **state)
{
	d_iov_t creds;

	memset(&creds, 0, sizeof(d_iov_t));

	assert_rc_equal(dc_sec_request_creds(&creds), DER_SUCCESS);

	daos_iov_free(&creds);
}

static void
test_request_credentials_fails_if_drpc_connect_fails(void **state)
{
	d_iov_t creds;

	memset(&creds, 0, sizeof(d_iov_t));
	free_drpc_connect_return(); /* drpc_connect returns NULL on failure */

	assert_rc_equal(dc_sec_request_creds(&creds), -DER_BADPATH);

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

	assert_rc_equal(dc_sec_request_creds(&creds),
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

	assert_rc_equal(dc_sec_request_creds(&creds), -DER_NOREPLY);

	daos_iov_free(&creds);
}

static void
test_request_credentials_fails_if_reply_status_failure(void **state)
{
	d_iov_t creds;

	memset(&creds, 0, sizeof(d_iov_t));
	drpc_call_resp_return_content.status = DRPC__STATUS__FAILURE;

	assert_rc_equal(dc_sec_request_creds(&creds), -DER_MISC);

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

	assert_rc_equal(dc_sec_request_creds(&creds), -DER_PROTO);

	daos_iov_free(&creds);
}

static void
test_request_credentials_fails_if_reply_cred_missing(void **state)
{
	d_iov_t creds;

	memset(&creds, 0, sizeof(d_iov_t));
	init_drpc_resp_with_cred(NULL);

	assert_rc_equal(dc_sec_request_creds(&creds), -DER_PROTO);

	daos_iov_free(&creds);
}

static void
test_request_credentials_fails_if_reply_token_missing(void **state)
{
	d_iov_t creds;

	memset(&creds, 0, sizeof(d_iov_t));
	auth__token__free_unpacked(drpc_call_resp_return_auth_cred->token,
				   NULL);
	drpc_call_resp_return_auth_cred->token = NULL;
	init_drpc_resp_with_cred(drpc_call_resp_return_auth_cred);

	assert_rc_equal(dc_sec_request_creds(&creds), -DER_PROTO);

	daos_iov_free(&creds);
}

static void
test_request_cred_fails_if_reply_verifier_missing(void **state)
{
	d_iov_t creds;

	memset(&creds, 0, sizeof(d_iov_t));
	auth__token__free_unpacked(drpc_call_resp_return_auth_cred->verifier,
				   NULL);
	drpc_call_resp_return_auth_cred->verifier = NULL;
	init_drpc_resp_with_cred(drpc_call_resp_return_auth_cred);

	assert_int_equal(dc_sec_request_creds(&creds), -DER_PROTO);

	daos_iov_free(&creds);
}
static void
test_request_credentials_fails_if_reply_cred_status(void **state)
{
	d_iov_t			creds;
	Auth__GetCredResp	resp = AUTH__GET_CRED_RESP__INIT;

	resp.status = -DER_UNKNOWN;
	pack_get_cred_resp_in_drpc_call_resp_body(&resp);
	memset(&creds, 0, sizeof(d_iov_t));

	assert_rc_equal(dc_sec_request_creds(&creds), -DER_UNKNOWN);
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
			drpc_call_resp_return_auth_cred);
	D_ALLOC(expected_data, expected_len);
	auth__credential__pack(drpc_call_resp_return_auth_cred,
			expected_data);

	assert_rc_equal(dc_sec_request_creds(&creds), DER_SUCCESS);

	assert_int_equal(creds.iov_buf_len, expected_len);
	assert_int_equal(creds.iov_len, expected_len);
	assert_memory_equal(creds.iov_buf, expected_data, expected_len);

	D_FREE(expected_data);
	daos_iov_free(&creds);
}

static daos_prop_t *
get_acl_prop(uint32_t owner_type, char *owner_user, uint32_t group_type, char *owner_group,
	     uint32_t acl_type, struct daos_acl *acl)
{
	daos_prop_t	*prop;
	size_t		nr_props = 0;
	size_t		i = 0;

	if (owner_user != NULL)
		nr_props++;
	if (owner_group != NULL)
		nr_props++;
	if (acl != NULL)
		nr_props++;

	prop = daos_prop_alloc(nr_props);
	assert_non_null(prop);

	if (owner_user != NULL) {
		prop->dpp_entries[i].dpe_type = owner_type;
		D_STRNDUP(prop->dpp_entries[i].dpe_str, owner_user, DAOS_ACL_MAX_PRINCIPAL_LEN);
		assert_non_null(prop->dpp_entries[i].dpe_str);
		i++;
	}

	if (owner_group != NULL) {
		prop->dpp_entries[i].dpe_type = group_type;
		D_STRNDUP(prop->dpp_entries[i].dpe_str, owner_group, DAOS_ACL_MAX_PRINCIPAL_LEN);
		assert_non_null(prop->dpp_entries[i].dpe_str);
		i++;
	}

	if (acl != NULL) {
		prop->dpp_entries[i].dpe_type = acl_type;
		prop->dpp_entries[i].dpe_val_ptr = (void *)daos_acl_dup(acl);
		assert_non_null(prop->dpp_entries[i].dpe_val_ptr);
		i++;
	}

	return prop;
}

static daos_prop_t *
get_cont_acl_prop(char *owner_user, char *owner_group, struct daos_acl *acl)
{
	return get_acl_prop(DAOS_PROP_CO_OWNER, owner_user, DAOS_PROP_CO_OWNER_GROUP, owner_group,
			    DAOS_PROP_CO_ACL, acl);
}

static daos_prop_t *
get_pool_acl_prop(char *owner_user, char *owner_group, struct daos_acl *acl)
{
	return get_acl_prop(DAOS_PROP_PO_OWNER, owner_user, DAOS_PROP_PO_OWNER_GROUP, owner_group,
			    DAOS_PROP_PO_ACL, acl);
}

static void
test_get_pool_perms_invalid_input(void **state)
{
	daos_prop_t	*pool_prop;
	daos_prop_t	*prop_no_owner;
	daos_prop_t	*prop_no_group;
	daos_prop_t	*prop_no_acl;
	struct daos_acl	*acl;
	uid_t		uid = geteuid();
	gid_t		gid = getegid();
	gid_t		bad_gids[2] = {getegid(), (gid_t)-1};
	uint64_t	perms;

	acl = daos_acl_create(NULL, 0);
	assert_non_null(acl);

	pool_prop = get_pool_acl_prop("user@", "group@", acl);

	D_PRINT("= NULL pool prop\n");
	assert_rc_equal(dc_sec_get_pool_permissions(NULL, uid, &gid, 1, &perms),
			-DER_INVAL);

	D_PRINT("= NULL perms param\n");
	assert_rc_equal(dc_sec_get_pool_permissions(pool_prop, uid, &gid, 1, NULL),
			-DER_INVAL);

	D_PRINT("= NULL gids with num gids > 0\n");
	assert_rc_equal(dc_sec_get_pool_permissions(pool_prop, uid, NULL, 1, NULL),
			-DER_INVAL);

	D_PRINT("= bad uid\n");
	assert_rc_equal(dc_sec_get_pool_permissions(pool_prop, (uid_t)-1, &gid, 1, &perms),
			-DER_NONEXIST);

	D_PRINT("= bad gid in list\n");
	assert_rc_equal(dc_sec_get_pool_permissions(pool_prop, uid, bad_gids,
						    ARRAY_SIZE(bad_gids), &perms),
			-DER_NONEXIST);

	D_PRINT("= no owner in prop\n");
	prop_no_owner = get_pool_acl_prop(NULL, "group@", acl);
	assert_rc_equal(dc_sec_get_pool_permissions(prop_no_owner, uid, &gid, 1, &perms),
			-DER_INVAL);
	daos_prop_free(prop_no_owner);

	D_PRINT("= no owner-group in prop\n");
	prop_no_group = get_pool_acl_prop("user@", NULL, acl);
	assert_rc_equal(dc_sec_get_pool_permissions(prop_no_group, uid, &gid, 1, &perms),
			-DER_INVAL);
	daos_prop_free(prop_no_group);

	D_PRINT("= no ACL in prop\n");
	prop_no_acl = get_pool_acl_prop("user@", "group@", NULL);
	assert_rc_equal(dc_sec_get_pool_permissions(prop_no_acl, uid, &gid, 1, &perms),
			-DER_INVAL);
	daos_prop_free(prop_no_acl);

	daos_prop_free(pool_prop);
	daos_acl_free(acl);
}

static void
test_get_cont_perms_invalid_input(void **state)
{
	daos_prop_t	*prop_no_owner;
	daos_prop_t	*prop_no_group;
	daos_prop_t	*prop_no_acl;
	daos_prop_t	*cont_prop;
	struct daos_acl	*acl;
	uid_t		uid = geteuid();
	gid_t		gid = getegid();
	gid_t		bad_gids[2] = {getegid(), (gid_t)-1};
	uint64_t	perms;

	acl = daos_acl_create(NULL, 0);
	assert_non_null(acl);

	cont_prop = get_cont_acl_prop("user@", "group@", acl);

	D_PRINT("= NULL cont prop\n");
	assert_rc_equal(dc_sec_get_cont_permissions(NULL, uid, &gid, 1, &perms),
			-DER_INVAL);

	D_PRINT("= NULL perms param\n");
	assert_rc_equal(dc_sec_get_cont_permissions(cont_prop, uid, &gid, 1, NULL),
			-DER_INVAL);

	D_PRINT("= NULL gids with num gids > 0\n");
	assert_rc_equal(dc_sec_get_cont_permissions(cont_prop, uid, NULL, 1, NULL),
			-DER_INVAL);

	D_PRINT("= bad uid\n");
	assert_rc_equal(dc_sec_get_cont_permissions(cont_prop, (uid_t)-1, &gid, 1, &perms),
			-DER_NONEXIST);

	D_PRINT("= bad gid in list\n");
	assert_rc_equal(dc_sec_get_cont_permissions(cont_prop, uid, bad_gids,
						    ARRAY_SIZE(bad_gids), &perms),
			-DER_NONEXIST);

	D_PRINT("= no owner in prop\n");
	prop_no_owner = get_cont_acl_prop(NULL, "group@", acl);
	assert_rc_equal(dc_sec_get_cont_permissions(prop_no_owner, uid, &gid, 1, &perms),
			-DER_INVAL);
	daos_prop_free(prop_no_owner);

	D_PRINT("= no owner-group in prop\n");
	prop_no_group = get_cont_acl_prop("user@", NULL, acl);
	assert_rc_equal(dc_sec_get_cont_permissions(prop_no_group, uid, &gid, 1, &perms),
			-DER_INVAL);
	daos_prop_free(prop_no_group);

	D_PRINT("= no ACL in prop\n");
	prop_no_acl = get_cont_acl_prop("user@", "group@", NULL);
	assert_rc_equal(dc_sec_get_cont_permissions(prop_no_acl, uid, &gid, 1, &perms),
			-DER_INVAL);
	daos_prop_free(prop_no_acl);

	daos_prop_free(cont_prop);
	daos_acl_free(acl);
}

static void
alloc_group_list(uid_t uid, gid_t gid, gid_t **groups, size_t *nr_groups)
{
	struct passwd	*pw;
	int		tmp = 0;
	int		rc;

	pw = getpwuid(uid);
	assert_non_null(pw);

	rc = getgrouplist(pw->pw_name, gid, NULL, &tmp);
	if (rc != -1) {
		D_PRINT("getting the number of groups failed\n");
		assert_true(false);
	}

	*nr_groups = (size_t)tmp;
	D_ALLOC_ARRAY(*groups, *nr_groups);
	assert_non_null(*groups);

	rc = getgrouplist(pw->pw_name, gid, *groups, &tmp);
	assert_true(rc > 0);
}

static void
expect_pool_perms(uid_t uid, gid_t *gids, size_t nr_gids, struct daos_ace **aces, size_t nr_aces,
		  uint64_t exp_perms)
{
	daos_prop_t	*prop;
	struct daos_acl	*acl;
	uint64_t	perms = 0;

	acl = daos_acl_create(aces, nr_aces);
	assert_non_null(acl);
	prop = get_pool_acl_prop("user@", "group@", acl);

	assert_rc_equal(dc_sec_get_pool_permissions(prop, uid, gids, nr_gids, &perms), 0);
	assert_int_equal(perms, exp_perms);

	daos_prop_free(prop);
	daos_acl_free(acl);
}

static void
test_get_pool_perms_valid(void **state)
{
	uid_t		uid = geteuid();
	gid_t		gid = getegid();
	gid_t		*gids = NULL;
	size_t		nr_gids = 0;
	char		*current_user;
	char		*current_grp;
	struct daos_ace	*pool_ace;
	struct daos_ace	*grp_aces[2];
	size_t		nr_grp_aces = 1;
	uint64_t	pool_perms;
	uint64_t	grp_perms;
	size_t		i;

	pool_perms = DAOS_ACL_PERM_GET_PROP;
	grp_perms = DAOS_ACL_PERM_GET_PROP;

	alloc_group_list(uid, gid, &gids, &nr_gids);

	assert_rc_equal(daos_acl_uid_to_principal(uid, &current_user), 0);

	pool_ace = daos_ace_create(DAOS_ACL_USER, current_user);
	pool_ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	pool_ace->dae_allow_perms = pool_perms;

	D_PRINT("= No perms from pool ACL\n");
	expect_pool_perms(uid, gids, nr_gids, NULL, 0, 0);

	D_PRINT("= Get user perms\n");
	expect_pool_perms(uid, gids, nr_gids, &pool_ace, 1, pool_perms);

	D_PRINT("= Get group perms\n");
	assert_rc_equal(daos_acl_gid_to_principal(gid, &current_grp), 0);
	grp_aces[0] = daos_ace_create(DAOS_ACL_GROUP, current_grp);
	grp_aces[0]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	grp_aces[0]->dae_allow_perms = grp_perms;
	if (nr_gids > 1) { /* include supplementary if we have any */
		char *grp;

		assert_rc_equal(daos_acl_gid_to_principal(gids[1], &grp), 0);
		grp_aces[1] = daos_ace_create(DAOS_ACL_GROUP, grp);
		grp_aces[1]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
		grp_aces[1]->dae_allow_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;

		grp_perms |= grp_aces[1]->dae_allow_perms;
		nr_grp_aces++;

		D_FREE(grp);
	}
	expect_pool_perms(uid, gids, nr_gids, grp_aces, nr_grp_aces, grp_perms);

	D_FREE(current_grp);
	for (i = 0; i < nr_grp_aces; i++)
		daos_ace_free(grp_aces[i]);
	daos_ace_free(pool_ace);
	D_FREE(current_grp);
	D_FREE(current_user);
	D_FREE(gids);
}

static void
expect_cont_perms(uid_t uid, gid_t *gids, size_t nr_gids,
		  struct daos_ace **aces, size_t nr_aces, uint64_t exp_perms)
{
	daos_prop_t	*prop;
	struct daos_acl	*acl;
	uint64_t	perms = 0;

	acl = daos_acl_create(aces, nr_aces);
	assert_non_null(acl);
	prop = get_cont_acl_prop("user@", "group@", acl);

	assert_rc_equal(dc_sec_get_cont_permissions(prop, uid, gids, nr_gids, &perms), 0);
	assert_int_equal(perms, exp_perms);

	daos_prop_free(prop);
	daos_acl_free(acl);
}

static void
test_get_cont_perms_valid(void **state)
{
	uid_t		uid = geteuid();
	gid_t		gid = getegid();
	gid_t		*gids = NULL;
	size_t		nr_gids = 0;
	char		*current_user;
	char		*current_grp;
	struct daos_ace	*user_ace;
	struct daos_ace	*grp_aces[2];
	size_t		nr_grp_aces = 1;
	uint64_t	cont_perms;
	uint64_t	grp_perms;
	size_t		i;

	cont_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;
	grp_perms = DAOS_ACL_PERM_GET_PROP;

	alloc_group_list(uid, gid, &gids, &nr_gids);

	assert_rc_equal(daos_acl_uid_to_principal(uid, &current_user), 0);

	user_ace = daos_ace_create(DAOS_ACL_USER, current_user);
	user_ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	user_ace->dae_allow_perms = cont_perms;

	D_PRINT("= No perms from cont ACL\n");
	expect_cont_perms(uid, gids, nr_gids, NULL, 0, 0);

	D_PRINT("= Get user perms\n");
	expect_cont_perms(uid, gids, nr_gids, &user_ace, 1, cont_perms);

	D_PRINT("= Get group perms\n");
	assert_rc_equal(daos_acl_gid_to_principal(gid, &current_grp), 0);
	grp_aces[0] = daos_ace_create(DAOS_ACL_GROUP, current_grp);
	grp_aces[0]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	grp_aces[0]->dae_allow_perms = grp_perms;
	if (nr_gids > 1) { /* include supplementary if we have any */
		char *grp;

		assert_rc_equal(daos_acl_gid_to_principal(gids[1], &grp), 0);
		grp_aces[1] = daos_ace_create(DAOS_ACL_GROUP, grp);
		grp_aces[1]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
		grp_aces[1]->dae_allow_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;

		grp_perms |= grp_aces[1]->dae_allow_perms;
		nr_grp_aces++;

		D_FREE(grp);
	}
	expect_cont_perms(uid, gids, nr_gids, grp_aces, nr_grp_aces, grp_perms);

	D_FREE(current_grp);
	for (i = 0; i < nr_grp_aces; i++)
		daos_ace_free(grp_aces[i]);
	daos_ace_free(user_ace);
	D_FREE(current_grp);
	D_FREE(current_user);
	D_FREE(gids);
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
			test_request_credentials_fails_if_reply_cred_missing),
		SECURITY_UTEST(
			test_request_cred_fails_if_reply_verifier_missing),
		SECURITY_UTEST(
			test_request_credentials_fails_if_reply_cred_status),
		SECURITY_UTEST(
			test_request_credentials_returns_raw_bytes),
		cmocka_unit_test(test_get_pool_perms_invalid_input),
		cmocka_unit_test(test_get_cont_perms_invalid_input),
		cmocka_unit_test(test_get_pool_perms_valid),
		cmocka_unit_test(test_get_cont_perms_valid),
	};

	return cmocka_run_group_tests_name("security_cli_security",
					   tests, NULL, NULL);
}

#undef SECURITY_UTEST
