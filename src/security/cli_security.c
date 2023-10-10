/*
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <gurt/common.h>
#include <gurt/debug.h>
#include <daos_prop.h>
#include <daos_security.h>
#include <daos_errno.h>
#include <daos/drpc.h>
#include <daos/drpc_modules.h>
#include <daos/drpc.pb-c.h>
#include <daos/agent.h>
#include <daos/security.h>

#include "auth.pb-c.h"
#include "acl.h"

/* Prototypes for static helper functions */
static int request_credentials_via_drpc(Drpc__Response **response);
static int process_credential_response(Drpc__Response *response,
				       d_iov_t *creds);
static int get_cred_from_response(Drpc__Response *response, d_iov_t *cred);

int
dc_sec_request_creds(d_iov_t *creds)
{
	Drpc__Response	*response = NULL;
	int		rc;

	if (creds == NULL) {
		return -DER_INVAL;
	}

	rc = request_credentials_via_drpc(&response);
	if (rc != DER_SUCCESS) {
		drpc_response_free(response);
		return rc;
	}

	rc = process_credential_response(response, creds);

	drpc_response_free(response);
	return rc;
}

static int
request_credentials_via_drpc(Drpc__Response **response)
{
	Drpc__Call	*request;
	struct drpc	*agent_socket;
	int		rc;

	if (dc_agent_sockpath == NULL) {
		D_ERROR("DAOS Socket Path is Uninitialized\n");
		return -DER_UNINIT;
	}

	rc = drpc_connect(dc_agent_sockpath, &agent_socket);
	if (rc != -DER_SUCCESS) {
		D_ERROR("Can't connect to agent socket "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	rc = drpc_call_create(agent_socket,
			      DRPC_MODULE_SEC_AGENT,
			      DRPC_METHOD_SEC_AGENT_REQUEST_CREDS,
			      &request);
	if (rc != -DER_SUCCESS) {
		D_ERROR("Couldn't allocate dRPC call "DF_RC"\n", DP_RC(rc));
		drpc_close(agent_socket);
		return rc;
	}

	rc = drpc_call(agent_socket, R_SYNC, request, response);

	drpc_close(agent_socket);
	drpc_call_free(request);
	return rc;
}

static int
process_credential_response(Drpc__Response *response, d_iov_t *creds)
{
	if (response == NULL) {
		D_ERROR("Response was null\n");
		return -DER_NOREPLY;
	}

	if (response->status != DRPC__STATUS__SUCCESS) {
		/* Recipient could not parse our message */
		D_ERROR("Agent credential drpc request failed: %d\n",
			response->status);
		return -DER_MISC;
	}

	return get_cred_from_response(response, creds);
}

static int
auth_cred_to_iov(Auth__Credential *cred, d_iov_t *iov)
{
	size_t	len;
	uint8_t	*packed;

	len = auth__credential__get_packed_size(cred);
	D_ALLOC(packed, len);
	if (packed == NULL)
		return -DER_NOMEM;

	auth__credential__pack(cred, packed);
	d_iov_set(iov, packed, len);

	return 0;
}

static int
get_cred_from_response(Drpc__Response *response, d_iov_t *cred)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	int			rc = 0;
	Auth__GetCredResp	*cred_resp = NULL;
	Auth__Token		*verifier = NULL;

	cred_resp = auth__get_cred_resp__unpack(&alloc.alloc,
						response->body.len,
						response->body.data);
	if (alloc.oom)
		return -DER_NOMEM;
	if (cred_resp == NULL) {
		D_ERROR("Body was not a GetCredentialResp\n");
		return -DER_PROTO;
	}

	if (cred_resp->status != 0) {
		D_ERROR("dRPC call reported failure, status=%d\n",
			cred_resp->status);
		D_GOTO(out, rc = cred_resp->status);
	}

	if (cred_resp->cred == NULL) {
		D_ERROR("No cred included\n");
		D_GOTO(out, rc = -DER_PROTO);
	}

	if (cred_resp->cred->token == NULL) {
		D_ERROR("Credential did not include token\n");
		D_GOTO(out, rc = -DER_PROTO);
	}

	if (cred_resp->cred->verifier == NULL) {
		D_ERROR("Credential did not include verifier\n");
		D_GOTO(out, rc = -DER_PROTO);
	}

	rc = auth_cred_to_iov(cred_resp->cred, cred);

	/* If present clear out the verifier (the secret part) */
	verifier = cred_resp->cred->verifier;
	explicit_bzero(verifier->data.data, verifier->data.len);
out:
	auth__get_cred_resp__free_unpacked(cred_resp, &alloc.alloc);
	return rc;
}

/*
 * Fetch the pool or container ACL from the passed-in prop. The output pointer points to the ACL
 * data in-place in the daos_prop_t, so doesn't need to be freed by the caller.
 */
static int
acl_from_prop(daos_prop_t *prop, uint32_t prop_type, struct daos_acl **acl_out)
{
	struct daos_prop_entry	*entry;
	struct daos_acl		*acl;
	char			*type_str;
	int			rc;

	D_ASSERT(acl_out != NULL);
	D_ASSERT(prop_type == DAOS_PROP_PO_ACL || prop_type == DAOS_PROP_CO_ACL);

	switch (prop_type) {
	case DAOS_PROP_PO_ACL:
		type_str = "pool";
		break;
	case DAOS_PROP_CO_ACL:
		type_str = "container";
		break;
	default:
		D_ASSERTF(false, "prop type %d", prop_type);
	}

	entry = daos_prop_entry_get(prop, prop_type);
	if (entry == NULL) {
		D_ERROR("no %s ACL in property\n", type_str);
		return -DER_INVAL;
	}
	acl = (struct daos_acl *)entry->dpe_val_ptr;

	rc = daos_acl_validate(acl);
	if (rc != 0) {
		D_ERROR("%s ACL is invalid\n", type_str);
		return rc;
	}

	*acl_out = acl;
	return 0;
}

static char *
get_owner_str_from_prop(daos_prop_t *prop, uint32_t prop_type)
{
	struct daos_prop_entry	*entry;

	D_ASSERT(prop_type == DAOS_PROP_PO_OWNER || prop_type == DAOS_PROP_PO_OWNER_GROUP ||
		 prop_type == DAOS_PROP_CO_OWNER || prop_type == DAOS_PROP_CO_OWNER_GROUP);

	entry = daos_prop_entry_get(prop, prop_type);
	if (entry == NULL) {
		D_ERROR("no entry for %d in property\n", prop_type);
		return NULL;
	}

	return entry->dpe_str;
}

static int
get_perms(daos_prop_t *prop, uint32_t acl_prop, uint32_t owner_prop, uint32_t group_prop,
	  struct acl_user *user_info, uint64_t min_owner_perms, uint64_t *perms)
{
	struct daos_acl		*acl = NULL;
	struct d_ownership	ownership = {0};
	bool			is_owner;
	int			rc;

	/* These ACL and ownership variables point to the data in-place in the prop, and thus don't
	 * need to be freed here.
	 */
	rc = acl_from_prop(prop, acl_prop, &acl);
	if (rc != 0)
		return rc;

	ownership.user = get_owner_str_from_prop(prop, owner_prop);
	if (ownership.user == NULL) {
		D_ERROR("couldn't get owner user (%d) from prop\n", owner_prop);
		return -DER_INVAL;
	}

	ownership.group = get_owner_str_from_prop(prop, group_prop);
	if (ownership.group == NULL) {
		D_ERROR("couldn't get owner group (%d) from prop\n", group_prop);
		return -DER_INVAL;
	}

	return get_acl_permissions(acl, &ownership, user_info, min_owner_perms, perms, &is_owner);
}

static void
free_user_info_strings(struct acl_user *user_info)
{
	size_t i;

	for (i = 0; i < user_info->nr_groups; i++)
		D_FREE(user_info->groups[i]);
	D_FREE(user_info->groups);
	D_FREE(user_info->user);
}

static int
fill_user_info(uid_t uid, gid_t *gids, size_t nr_gids, struct acl_user *user_info)
{
	int	rc;
	size_t	i;

	rc = daos_acl_uid_to_principal(uid, &user_info->user);
	if (rc != 0) {
		D_ERROR("failed to convert uid %d to an ACL principal: "DF_RC"\n", uid, DP_RC(rc));
		D_GOTO(err_out, rc);
	}

	D_ALLOC_ARRAY(user_info->groups, nr_gids);
	if (user_info->groups == NULL)
		D_GOTO(err_userinfo, rc = -DER_NOMEM);

	user_info->nr_groups = 0;
	for (i = 0; i < nr_gids; i++) {
		rc = daos_acl_gid_to_principal(gids[i], &user_info->groups[i]);
		if (rc != 0) {
			D_ERROR("failed to convert gid %d to an ACL principal: "DF_RC"\n", gids[i],
				DP_RC(rc));
			D_GOTO(err_userinfo, rc);
		}
		user_info->nr_groups++;
	}
	D_ASSERT(nr_gids == user_info->nr_groups);

	return rc;

err_userinfo:
	free_user_info_strings(user_info);
err_out:
	return rc;
}

static int
get_user_perms(daos_prop_t *prop, uint32_t acl_prop, uint32_t owner_prop, uint32_t group_prop,
	       uid_t uid, gid_t *gids, size_t nr_gids, uint64_t min_owner_perms,
	       uint64_t *perms)
{
	struct acl_user	user_info = {0};
	int		rc;

	if (prop == NULL) {
		D_ERROR("null property parameter\n");
		return -DER_INVAL;
	}

	if (gids == NULL && nr_gids > 0) {
		D_ERROR("null gids array with nr_gids=%lu\n", nr_gids);
		return -DER_INVAL;
	}

	if (perms == NULL) {
		D_ERROR("null perms parameter\n");
		return -DER_INVAL;
	}

	/* Default is no permissions */
	*perms = 0;

	rc = fill_user_info(uid, gids, nr_gids, &user_info);
	if (rc != 0) {
		D_ERROR("failed to convert uid/gids into ACL principals, "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = get_perms(prop, acl_prop, owner_prop, group_prop, &user_info, min_owner_perms, perms);
	if (rc != 0)
		D_ERROR("failed to collect pool permissions, "DF_RC"\n", DP_RC(rc));

	free_user_info_strings(&user_info);
out:
	return rc;
}

int
dc_sec_get_pool_permissions(daos_prop_t *pool_prop, uid_t uid, gid_t *gids, size_t nr_gids,
			    uint64_t *perms)
{
	return get_user_perms(pool_prop, DAOS_PROP_PO_ACL, DAOS_PROP_PO_OWNER,
			      DAOS_PROP_PO_OWNER_GROUP, uid, gids, nr_gids,
			      POOL_OWNER_MIN_PERMS, perms);
}

int
dc_sec_get_cont_permissions(daos_prop_t *cont_prop, uid_t uid, gid_t *gids, size_t nr_gids,
			    uint64_t *perms)
{
	return get_user_perms(cont_prop, DAOS_PROP_CO_ACL, DAOS_PROP_CO_OWNER,
			      DAOS_PROP_CO_OWNER_GROUP, uid, gids, nr_gids,
			      CONT_OWNER_MIN_PERMS, perms);
}
