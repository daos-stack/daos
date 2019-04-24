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

#include <unistd.h>
#include <string.h>
#include <daos_errno.h>
#include <daos/drpc.h>
#include <daos/drpc.pb-c.h>

#include <daos_srv/pool.h>
#include <daos_srv/security.h>

#include "srv_internal.h"
#include "security.pb-c.h"

static int
sanity_check_validation_response(Drpc__Response *response)
{
	int rc = DER_SUCCESS;

	AuthToken *pb_auth = auth_token__unpack(NULL,
			response->body.len, response->body.data);
	if (pb_auth == NULL) {
		D_ERROR("Response body was not an AuthToken\n");
		return -DER_PROTO;
	}

	if (!pb_auth->has_data) {
		D_ERROR("AuthToken did not include data\n");
		rc = -DER_PROTO;
	}

	auth_token__free_unpacked(pb_auth, NULL);
	return rc;
}

static Drpc__Call *
new_validation_request(struct drpc *ctx, daos_iov_t *creds)
{
	uint8_t		*body;
	Drpc__Call	*request;

	request = drpc_call_create(ctx,
			DRPC_MODULE_SECURITY_SERVER,
			DRPC_METHOD_SECURITY_SERVER_VALIDATE_CREDENTIALS);
	if (request == NULL) {
		D_ERROR("Could not allocate dRPC call\n");
		return NULL;
	}

	D_ALLOC(body, creds->iov_len);
	if (body == NULL) {
		D_ERROR("Could not allocate dRPC call body\n");
		drpc_call_free(request);
		return NULL;
	}

	memcpy(body, creds->iov_buf, creds->iov_len);
	request->body.len = creds->iov_len;
	request->body.data = body;

	return request;
}

static int
validate_credentials_via_drpc(Drpc__Response **response, daos_iov_t *creds)
{
	struct drpc	*server_socket;
	Drpc__Call	*request;
	int		rc;

	server_socket = drpc_connect(ds_sec_server_socket_path);
	if (server_socket == NULL) {
		D_ERROR("Couldn't connect to daos_server socket\n");
		return -DER_BADPATH;
	}

	request = new_validation_request(server_socket, creds);
	if (request == NULL) {
		return -DER_NOMEM;
	}

	rc = drpc_call(server_socket, R_SYNC, request, response);

	drpc_close(server_socket);
	drpc_call_free(request);
	return rc;
}

static int
process_validation_response(Drpc__Response *response,
		AuthToken **token)
{
	int		rc = DER_SUCCESS;
	AuthToken	*auth;

	if (response == NULL) {
		D_ERROR("Response was NULL\n");
		return -DER_NOREPLY;
	}

	if (response->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("dRPC response error: %d\n", response->status);
		return -DER_MISC;
	}

	rc = sanity_check_validation_response(response);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	auth = auth_token__unpack(NULL, response->body.len,
					response->body.data);
	if (auth == NULL) {
		D_ERROR("Failed to unpack response body\n");
		return -DER_PROTO;
	}
	*token = auth;

	return rc;
}

int
ds_sec_validate_credentials(daos_iov_t *creds, AuthToken **token)
{
	Drpc__Response	*response = NULL;
	int		rc;

	if (creds == NULL ||
	    token == NULL ||
	    creds->iov_buf_len == 0 ||
	    creds->iov_buf == NULL) {
		D_ERROR("Credential iov invalid\n");
		return -DER_INVAL;
	}

	rc = validate_credentials_via_drpc(&response, creds);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	rc = process_validation_response(response, token);

	drpc_response_free(response);
	return rc;
}

static int
get_auth_sys_payload(AuthToken *token, AuthSys **payload)
{
	if (token->flavor != AUTH_FLAVOR__AUTH_SYS) {
		D_ERROR("Credential auth flavor not supported\n");
		return -DER_PROTO;
	}

	*payload = auth_sys__unpack(NULL, token->data.len, token->data.data);
	if (*payload == NULL) {
		D_ERROR("Invalid auth_sys payload\n");
		return -DER_PROTO;
	}

	return 0;
}

static bool
ace_allowed(struct daos_ace *ace, enum daos_acl_perm perm)
{
	if (ace->dae_allow_perms & perm)
		return true;

	return false;
}

static bool
ace_has_access(struct daos_ace *ace, uint64_t capas)
{
	D_DEBUG(DB_MGMT, "Allow Perms: 0x%lx\n", ace->dae_allow_perms);

	if ((capas & DAOS_PC_RO) &&
	    ace_allowed(ace, DAOS_ACL_PERM_READ)) {
		D_DEBUG(DB_MGMT, "Allowing read-only access\n");
		return true;
	}

	if ((capas & (DAOS_PC_RW | DAOS_PC_EX)) &&
	    ace_allowed(ace, DAOS_ACL_PERM_READ) &&
	    ace_allowed(ace, DAOS_ACL_PERM_WRITE)) {
		D_DEBUG(DB_MGMT, "Allowing RW access\n");
		return true;
	}

	return false;
}

static int
check_access_for_principal(struct daos_acl *acl,
			   enum daos_acl_principal_type type,
			   uint64_t capas)
{
	struct daos_ace *ace;
	int		rc;

	D_DEBUG(DB_MGMT, "Checking ACE for principal type %d\n", type);

	rc = daos_acl_get_ace_for_principal(acl, type, NULL, &ace);
	if (rc == 0 && ace_has_access(ace, capas))
		return 0;

	/* ACE not found */
	if (rc == -DER_NONEXIST)
		return rc;

	return -DER_NO_PERM;
}

static bool
authsys_has_owner_group(struct pool_prop_ugm *ugm, AuthSys *authsys)
{
	size_t i;

	if (authsys->has_gid && authsys->gid == ugm->pp_gid)
		return true;

	for (i = 0; i < authsys->n_gids; i++) {
		if (authsys->gids[i] == ugm->pp_gid)
			return true;
	}

	return false;
}

static int
check_authsys_permissions(struct daos_acl *acl, struct pool_prop_ugm *ugm,
			  AuthSys *authsys, uint64_t capas)
{
	int rc = -DER_NO_PERM;

	/* If this is the owner, and there's an owner entry... */
	if (authsys->has_uid && authsys->uid == ugm->pp_uid) {
		rc = check_access_for_principal(acl, DAOS_ACL_OWNER,
						capas);
		if (rc != -DER_NONEXIST)
			return rc;
	}

	/* Check all the user's groups for owner group... */
	if (authsys_has_owner_group(ugm, authsys))
		rc = check_access_for_principal(acl, DAOS_ACL_OWNER_GROUP,
						capas);

	return rc;
}

int
ds_sec_check_pool_access(struct daos_acl *acl, struct pool_prop_ugm *ugm,
			 d_iov_t *cred, uint64_t capas)
{
	int		rc = 0;
	AuthToken	*token = NULL;
	AuthSys		*authsys = NULL;

	if (acl == NULL || ugm == NULL || cred == NULL) {
		D_ERROR("An input was NULL, acl=0x%p, ugm=0x%p, cred=0x%p\n",
			acl, ugm, cred);
		return -DER_INVAL;
	}

	if (daos_acl_validate(acl) != 0) {
		D_ERROR("ACL content not valid\n");
		return -DER_INVAL;
	}

	rc = ds_sec_validate_credentials(cred, &token);
	if (rc != 0) {
		D_ERROR("Failed to validate credentials, rc=%d\n", rc);
		return rc;
	}

	rc = get_auth_sys_payload(token, &authsys);
	auth_token__free_unpacked(token, NULL);
	if (rc != 0)
		return rc;

	/*
	 * Check ACL for permission via AUTH_SYS credentials
	 */
	rc = check_authsys_permissions(acl, ugm, authsys, capas);
	if (rc == 0)
		goto access_allowed;

	/*
	 * Last resort - if we don't have access via credentials
	 */
	rc = check_access_for_principal(acl, DAOS_ACL_EVERYONE, capas);
	if (rc == 0)
		goto access_allowed;

	D_INFO("Access denied\n");
	auth_sys__free_unpacked(authsys, NULL);
	return -DER_NO_PERM;

access_allowed:
	D_INFO("Access allowed\n");
	auth_sys__free_unpacked(authsys, NULL);
	return 0;
}
