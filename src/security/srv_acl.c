/*
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <unistd.h>
#include <string.h>
#include <daos_errno.h>
#include <daos/debug.h>
#include <daos/drpc.h>
#include <daos/drpc.pb-c.h>
#include <daos/drpc_modules.h>
#include <daos/container.h>

#include <daos_srv/pool.h>
#include <daos_srv/security.h>

#include "auth.pb-c.h"
#include "srv_internal.h"
#include "acl.h"

/**
 * The default ACLs for pool and container both include ACEs for owner and the
 * assigned group. All others are denied by default.
 */
#define NUM_DEFAULT_ACES	(2)

static struct daos_ace *
alloc_ace_with_access(enum daos_acl_principal_type type, uint64_t permissions)
{
	struct daos_ace *ace;

	ace = daos_ace_create(type, NULL);
	if (ace == NULL) {
		D_ERROR("Failed to allocate default ACE type %d\n", type);
		return NULL;
	}

	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = permissions;

	return ace;
}

static struct daos_acl *
alloc_default_daos_acl_with_perms(uint64_t owner_perms,
				  uint64_t owner_grp_perms)
{
	int		i;
	struct daos_ace	*default_aces[NUM_DEFAULT_ACES];
	struct daos_acl	*default_acl;

	default_aces[0] = alloc_ace_with_access(DAOS_ACL_OWNER, owner_perms);
	default_aces[1] = alloc_ace_with_access(DAOS_ACL_OWNER_GROUP,
						owner_grp_perms);

	default_acl = daos_acl_create(default_aces, NUM_DEFAULT_ACES);

	for (i = 0; i < NUM_DEFAULT_ACES; i++) {
		daos_ace_free(default_aces[i]);
	}

	return default_acl;
}

struct daos_acl *
ds_sec_alloc_default_daos_cont_acl(void)
{
	struct daos_acl	*acl;
	uint64_t	owner_perms;
	uint64_t	grp_perms;

	/* container owner has full control */
	owner_perms = DAOS_ACL_PERM_CONT_ALL;
	/* owner-group has basic read/write access but not admin access */
	grp_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE |
		    DAOS_ACL_PERM_GET_PROP | DAOS_ACL_PERM_SET_PROP;

	acl = alloc_default_daos_acl_with_perms(owner_perms, grp_perms);
	if (acl == NULL)
		D_ERROR("Failed to allocate default ACL for cont properties\n");

	return acl;
}

struct daos_acl *
ds_sec_alloc_default_daos_pool_acl(void)
{
	struct daos_acl	*acl;
	uint64_t	owner_perms;
	uint64_t	grp_perms;

	/*
	 * TODO: modify default pool owner/group perms to the more granular
	 * create_cont/del_cont
	 */

	/* pool owner and group have full read/write access */
	owner_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;
	grp_perms = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE;

	acl = alloc_default_daos_acl_with_perms(owner_perms, grp_perms);
	if (acl == NULL)
		D_ERROR("Failed to allocate default ACL for pool properties\n");

	return acl;
}

static Auth__Token *
auth_token_dup(Auth__Token *orig)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Auth__Token		*copy;
	uint8_t			*packed;
	size_t			len;

	/*
	 * The most straightforward way to copy a protobuf struct is to pack
	 * and unpack it.
	 */
	len = auth__token__get_packed_size(orig);
	D_ALLOC(packed, len);
	if (packed == NULL)
		return NULL;

	auth__token__pack(orig, packed);
	copy = auth__token__unpack(&alloc.alloc, len, packed);
	D_FREE(packed);
	return copy;
}

static int
get_token_from_validation_response(Drpc__Response *response,
				   Auth__Token **token)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Auth__ValidateCredResp	*resp;
	int			rc = 0;

	resp = auth__validate_cred_resp__unpack(&alloc.alloc,
						response->body.len,
						response->body.data);
	if (alloc.oom)
		return -DER_NOMEM;
	if (resp == NULL) {
		D_ERROR("Response body was not a ValidateCredResp\n");
		return -DER_PROTO;
	}

	if (resp->status != 0) {
		D_ERROR("Response reported failed status: %d\n", resp->status);
		D_GOTO(out, rc = resp->status);
	}

	if (resp->token == NULL || resp->token->data.data == NULL) {
		D_ERROR("Response missing a valid auth token\n");
		D_GOTO(out, rc = -DER_PROTO);
	}

	*token = auth_token_dup(resp->token);
	if (*token == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
	}

out:
	auth__validate_cred_resp__free_unpacked(resp, &alloc.alloc);
	return rc;
}

static int
new_validation_request(struct drpc *ctx, d_iov_t *creds, Drpc__Call **callp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	uint8_t			*body;
	size_t			len;
	Drpc__Call		*request;
	Auth__ValidateCredReq	req = AUTH__VALIDATE_CRED_REQ__INIT;
	Auth__Credential	*cred;
	int			rc;

	*callp = NULL;

	rc = drpc_call_create(ctx,
			      DRPC_MODULE_SEC,
			      DRPC_METHOD_SEC_VALIDATE_CREDS,
			      &request);
	if (rc != DER_SUCCESS)
		return rc;

	cred = auth__credential__unpack(&alloc.alloc, creds->iov_buf_len,
					creds->iov_buf);
	if (alloc.oom || cred == NULL) {
		drpc_call_free(request);
		return -DER_NOMEM;
	}
	req.cred = cred;

	len = auth__validate_cred_req__get_packed_size(&req);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_call_free(request);
		auth__credential__free_unpacked(cred, &alloc.alloc);
		return -DER_NOMEM;
	}
	auth__validate_cred_req__pack(&req, body);
	request->body.len = len;
	request->body.data = body;

	auth__credential__free_unpacked(cred, &alloc.alloc);

	*callp = request;
	return DER_SUCCESS;
}

static int
validate_credentials_via_drpc(Drpc__Response **response, d_iov_t *creds)
{
	struct drpc	*server_socket;
	Drpc__Call	*request;
	int		rc;

	rc = drpc_connect(ds_sec_server_socket_path, &server_socket);
	if (rc != -DER_SUCCESS) {
		D_ERROR("Couldn't connect to daos_server socket: " DF_RC "\n", DP_RC(rc));
		return rc;
	}

	rc  = new_validation_request(server_socket, creds, &request);
	if (rc != DER_SUCCESS) {
		drpc_close(server_socket);
		return rc;
	}

	rc = drpc_call(server_socket, R_SYNC, request, response);

	drpc_close(server_socket);
	drpc_call_free(request);
	return rc;
}

static int
process_validation_response(Drpc__Response *response, Auth__Token **token)
{
	if (response == NULL) {
		D_ERROR("Response was NULL\n");
		return -DER_NOREPLY;
	}

	if (response->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("dRPC response error: %d\n", response->status);
		return -DER_MISC;
	}

	return get_token_from_validation_response(response, token);
}

int
ds_sec_validate_credentials(d_iov_t *creds, Auth__Token **token)
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

static uint64_t
pool_capas_from_perms(uint64_t perms, bool is_owner)
{
	uint64_t capas = 0;

	if ((perms & DAOS_ACL_PERM_READ) ||
	    (perms & DAOS_ACL_PERM_GET_PROP))
		capas |= POOL_CAPA_READ;
	if ((perms & DAOS_ACL_PERM_WRITE) ||
	    (perms & DAOS_ACL_PERM_CREATE_CONT))
		capas |= POOL_CAPA_CREATE_CONT;
	if ((perms & DAOS_ACL_PERM_WRITE) ||
	    (perms & DAOS_ACL_PERM_DEL_CONT))
		capas |= POOL_CAPA_DEL_CONT;

	return capas;
}

static int
get_auth_sys_payload(Auth__Token *token, Auth__Sys **payload)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);

	if (token->flavor != AUTH__FLAVOR__AUTH_SYS) {
		D_ERROR("Credential auth flavor not supported\n");
		return -DER_PROTO;
	}

	*payload = auth__sys__unpack(&alloc.alloc,
				     token->data.len, token->data.data);
	if (alloc.oom)
		return -DER_NOMEM;
	if (*payload == NULL) {
		D_ERROR("Invalid auth_sys payload\n");
		return -DER_PROTO;
	}

	return 0;
}

static void
filter_pool_capas_based_on_flags(uint64_t flags, uint64_t *capas)
{
	if (flags & DAOS_PC_RO)
		*capas &= POOL_CAPAS_RO_MASK;
	else if (!(*capas & POOL_CAPAS_RO_MASK) ||
		 !(*capas & ~POOL_CAPAS_RO_MASK))
		/*
		 * User requested RW - if they don't have permissions for both
		 * read and write capas, we shouldn't grant them any.
		 */
		*capas = 0;
}

static int
get_sec_capas_for_token(Auth__Token *token, struct d_ownership *ownership, struct daos_acl *acl,
			uint64_t owner_min_perms, uint64_t (*convert_perms)(uint64_t, bool),
			uint64_t *capas)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	int			rc;
	Auth__Sys		*authsys;
	char			**groups = NULL;
	size_t			nr_groups = 0;
	struct acl_user		user_info = {0};
	uint64_t		perms = 0;
	bool			is_owner;

	rc = get_auth_sys_payload(token, &authsys);
	if (rc != 0)
		return rc;

	nr_groups = authsys->n_groups;
	if (authsys->group != NULL)
		nr_groups++;
	if (nr_groups > 0) {
		int i = 0;

		D_ALLOC_ARRAY(groups, nr_groups);
		if (groups == NULL)
			return -DER_NOMEM;

		for (i = 0; i < authsys->n_groups; i++)
			groups[i] = authsys->groups[i];

		if (authsys->group != NULL)
			groups[i] = authsys->group;
	}

	user_info.user = authsys->user;
	user_info.groups = groups;
	user_info.nr_groups = nr_groups;

	rc = get_acl_permissions(acl, ownership, &user_info, owner_min_perms, &perms, &is_owner);
	if (rc != 0) {
		D_ERROR("failed to get user permissions: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	*capas = convert_perms(perms, is_owner);

out:
	D_FREE(groups);
	auth__sys__free_unpacked(authsys, &alloc.alloc);
	return rc;
}

static int
get_sec_origin_for_token(Auth__Token *token, char **machine)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	int			machine_size = 0;
	int			rc = 0;
	char			*mtmp;
	Auth__Sys		*authsys;

	if (token == NULL || machine == NULL) {
		D_ERROR("NULL input\n");
		return -DER_INVAL;
	}

	rc = get_auth_sys_payload(token, &authsys);
	if (rc != 0)
		return rc;

	if (authsys->machinename == protobuf_c_empty_string) {
		D_ERROR("Malformed AuthSys token missing machinename\n");
		rc = -DER_INVAL;
		goto out;
	}

	/* This should allow us to catch if we're truncating the string */
	machine_size = strnlen(authsys->machinename, MAXHOSTNAMELEN+1);

	if (machine_size > MAXHOSTNAMELEN) {
		D_ERROR("hostname provided by the agent is too large\n");
		rc = -DER_INVAL;
		goto out;
	}

	D_STRNDUP(mtmp, authsys->machinename, machine_size);

	if (mtmp) {
		*machine = mtmp;
		rc = 0;
	} else {
		rc = -DER_NOMEM;
	}

out:
	auth__sys__free_unpacked(authsys, &alloc.alloc);
	return rc;
}

int
ds_sec_cred_get_origin(d_iov_t *cred, char **machine)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	int		rc;
	Auth__Token	*token;

	if (cred == NULL || machine == NULL) {
		D_ERROR("NULL input\n");
		return -DER_INVAL;
	}

	rc = ds_sec_validate_credentials(cred, &token);
	if (rc != 0) {
		D_ERROR("Failed to validate credentials, rc="DF_RC"\n",
			DP_RC(rc));
		return rc;
	}
	rc = get_sec_origin_for_token(token, machine);

	auth__token__free_unpacked(token, &alloc.alloc);
	return rc;
}

int
ds_sec_pool_get_capabilities(uint64_t flags, d_iov_t *cred,
			     struct d_ownership *ownership,
			     struct daos_acl *acl, uint64_t *capas)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	int			rc;
	Auth__Token		*token;

	if (cred == NULL || ownership == NULL || acl == NULL ||
	    capas == NULL) {
		D_ERROR("NULL input\n");
		return -DER_INVAL;
	}

	if (!is_ownership_valid(ownership)) {
		D_ERROR("Invalid ownership\n");
		return -DER_INVAL;
	}

	/* Pool flags are mutually exclusive */
	if ((flags != DAOS_PC_RO) && (flags != DAOS_PC_RW) &&
	    (flags != DAOS_PC_EX)) {
		D_ERROR("Invalid flags\n");
		return -DER_INVAL;
	}

	rc = daos_acl_validate(acl);
	if (rc != -DER_SUCCESS) {
		D_ERROR("Invalid ACL: " DF_RC "\n", DP_RC(rc));
		return rc;
	}

	rc = ds_sec_validate_credentials(cred, &token);
	if (rc != 0) {
		D_ERROR("Failed to validate credentials, rc="DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	rc = get_sec_capas_for_token(token, ownership, acl, 0 /* no special owner perms */,
				     pool_capas_from_perms, capas);
	if (rc == 0)
		filter_pool_capas_based_on_flags(flags, capas);

	auth__token__free_unpacked(token, &alloc.alloc);
	return rc;
}

static uint64_t
cont_capas_from_perms(uint64_t perms, bool is_owner)
{
	uint64_t capas = 0;

	if (perms & DAOS_ACL_PERM_READ)
		capas |= CONT_CAPA_READ_DATA;
	if (perms & DAOS_ACL_PERM_WRITE)
		capas |= CONT_CAPA_WRITE_DATA;
	if (perms & DAOS_ACL_PERM_GET_PROP)
		capas |= CONT_CAPA_GET_PROP;
	if (perms & DAOS_ACL_PERM_SET_PROP)
		capas |= CONT_CAPA_SET_PROP;
	if (perms & DAOS_ACL_PERM_GET_ACL)
		capas |= CONT_CAPA_GET_ACL;
	if (perms & DAOS_ACL_PERM_SET_ACL)
		capas |= CONT_CAPA_SET_ACL;
	if (perms & DAOS_ACL_PERM_SET_OWNER)
		capas |= CONT_CAPA_SET_OWNER;
	if (perms & DAOS_ACL_PERM_DEL_CONT)
		capas |= CONT_CAPA_DELETE;

	if (is_owner) {
		capas |= CONT_CAPA_OPEN_EX;
		capas |= CONT_CAPA_EVICT_ALL;
	}

	return capas;
}

static void
filter_cont_capas_based_on_flags(uint64_t flags, uint64_t *capas)
{
	if (flags & DAOS_COO_RO)
		*capas &= CONT_CAPAS_RO_MASK;
	else if (!(*capas & CONT_CAPAS_RO_MASK) ||
		 !(*capas & CONT_CAPAS_W_MASK))
		/*
		 * User requested RW - if they don't have permissions for both
		 * read and write capas of some kind, we won't grant them any.
		 */
		*capas = 0;

	if (!(flags & DAOS_COO_EX))
		*capas &= ~(uint64_t)CONT_CAPA_OPEN_EX;

	if (!(flags & DAOS_COO_EVICT_ALL))
		*capas &= ~(uint64_t)CONT_CAPA_EVICT_ALL;
}

static Auth__Token *
unpack_token_from_cred(d_iov_t *cred)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Auth__Credential	*unpacked;
	Auth__Token		*token = NULL;

	unpacked = auth__credential__unpack(&alloc.alloc, cred->iov_buf_len,
					    cred->iov_buf);
	if (alloc.oom || unpacked == NULL) {
		D_ERROR("Couldn't unpack credential\n");
		return NULL;
	}

	if (unpacked->token != NULL)
		token = auth_token_dup(unpacked->token);

	auth__credential__free_unpacked(unpacked, &alloc.alloc);
	return token;
}

int
ds_sec_cont_get_capabilities(uint64_t flags, d_iov_t *cred, struct d_ownership *ownership,
			     struct daos_acl *acl, uint64_t *capas)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Auth__Token	*token;
	int		rc;
	uint64_t	owner_min_perms = CONT_OWNER_MIN_PERMS;

	if (cred == NULL || ownership == NULL || acl == NULL || capas == NULL) {
		D_ERROR("NULL input\n");
		return -DER_INVAL;
	}

	if (!is_ownership_valid(ownership)) {
		D_ERROR("Invalid ownership\n");
		return -DER_INVAL;
	}

	if (!dc_cont_open_flags_valid(flags)) {
		D_ERROR("Invalid flags\n");
		return -DER_INVAL;
	}

	if (daos_acl_validate(acl) != 0) {
		D_ERROR("Invalid ACL\n");
		return -DER_INVAL;
	}

	if (cred->iov_buf == NULL) {
		D_ERROR("Credential data is NULL\n");
		return -DER_INVAL;
	}

	/*
	 * The credential has already been validated at pool connect.
	 */
	token = unpack_token_from_cred(cred);
	if (token == NULL)
		return -DER_INVAL;

	rc = get_sec_capas_for_token(token, ownership, acl, owner_min_perms, cont_capas_from_perms,
				     capas);
	if (rc == 0)
		filter_cont_capas_based_on_flags(flags, capas);

	auth__token__free_unpacked(token, &alloc.alloc);
	return rc;
}

bool
ds_sec_pool_can_connect(uint64_t pool_capas)
{
	return (pool_capas & POOL_CAPA_READ) != 0;
}

bool
ds_sec_pool_can_create_cont(uint64_t pool_capas)
{
	return (pool_capas & POOL_CAPA_CREATE_CONT) != 0;
}
bool
ds_sec_pool_can_delete_cont(uint64_t pool_capas)
{
	return (pool_capas & POOL_CAPA_DEL_CONT) != 0;
}

bool
ds_sec_cont_can_open(uint64_t cont_capas)
{
	/*
	 * Need to have some form of read access at minimum.
	 */
	return (cont_capas & CONT_CAPAS_RO_MASK) != 0;
}

bool
ds_sec_cont_can_delete(uint64_t pool_flags, d_iov_t *cred,
		       struct d_ownership *ownership,
		       struct daos_acl *acl)
{
	int		rc;
	uint64_t	capas = 0;
	uint64_t	cont_flags = 0;

	/*
	 * Translate the pool flags to allow us to properly filter RO/RW
	 * permissions
	 */
	if (pool_flags & DAOS_PC_RO)
		cont_flags |= DAOS_COO_RO;
	if (pool_flags & DAOS_PC_RW)
		cont_flags |= DAOS_COO_RW;

	rc = ds_sec_cont_get_capabilities(cont_flags, cred, ownership, acl, &capas);
	if (rc != 0) {
		D_ERROR("failed to get container capabilities: %d\n", rc);
		return false;
	}

	return (capas & CONT_CAPA_DELETE) != 0;
}

bool
ds_sec_cont_can_get_props(uint64_t cont_capas)
{
	return (cont_capas & CONT_CAPA_GET_PROP) != 0;
}

bool
ds_sec_cont_can_set_props(uint64_t cont_capas)
{
	return (cont_capas & CONT_CAPA_SET_PROP) != 0;
}

bool
ds_sec_cont_can_get_acl(uint64_t cont_capas)
{
	return (cont_capas & CONT_CAPA_GET_ACL) != 0;
}

bool
ds_sec_cont_can_set_acl(uint64_t cont_capas)
{
	return (cont_capas & CONT_CAPA_SET_ACL) != 0;
}

bool
ds_sec_cont_can_set_owner(uint64_t cont_capas)
{
	return (cont_capas & CONT_CAPA_SET_OWNER) != 0;
}

bool
ds_sec_cont_can_write_data(uint64_t cont_capas)
{
	return (cont_capas & CONT_CAPA_WRITE_DATA) != 0;
}

uint64_t
ds_sec_cont_capa_write_data_enable(uint64_t cont_capas)
{
	return cont_capas | ((uint64_t)CONT_CAPA_WRITE_DATA);
}

uint64_t
ds_sec_cont_capa_write_data_disable(uint64_t cont_capas)
{
	return cont_capas & (~(uint64_t)CONT_CAPA_WRITE_DATA);
}

bool
ds_sec_cont_can_read_data(uint64_t cont_capas)
{
	return (cont_capas & CONT_CAPA_READ_DATA) != 0;
}

bool
ds_sec_cont_can_open_ex(uint64_t cont_capas)
{
	return (cont_capas & CONT_CAPA_OPEN_EX) != 0;
}

bool
ds_sec_cont_can_evict_all(uint64_t cont_capas)
{
	return (cont_capas & CONT_CAPA_EVICT_ALL) != 0;
}

uint64_t
ds_sec_get_rebuild_cont_capabilities(void)
{
	/*
	 * Internally generated rebuild container handles can read data or write
	 * data.
	 */
	return CONT_CAPA_READ_DATA | CONT_CAPA_WRITE_DATA;
}

uint64_t
ds_sec_get_admin_cont_capabilities(void)
{
	/*
	 * Internally generated admin container handles can do everything.
	 */
	return CONT_CAPAS_ALL;
}

int
ds_sec_creds_are_same_user(d_iov_t *cred_x, d_iov_t *cred_y)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Auth__Token		*token_x;
	Auth__Token		*token_y;
	Auth__Sys		*authsys_x;
	Auth__Sys		*authsys_y;
	int			rc;

	if (cred_x == NULL || cred_y == NULL || cred_x->iov_buf == NULL ||
	    cred_y->iov_buf == NULL) {
		D_ERROR("NULL input\n");
		rc = -DER_INVAL;
		goto out;
	}

	token_x = unpack_token_from_cred(cred_x);
	if (token_x == NULL) {
		rc = -DER_INVAL;
		goto out;
	}

	token_y = unpack_token_from_cred(cred_y);
	if (token_y == NULL) {
		rc = -DER_INVAL;
		goto out_token_x;
	}

	rc = get_auth_sys_payload(token_x, &authsys_x);
	if (rc != 0)
		goto out_token_y;

	rc = get_auth_sys_payload(token_y, &authsys_y);
	if (rc != 0)
		goto out_authsys_x;

	rc = (strncmp(authsys_x->user, authsys_y->user, DAOS_ACL_MAX_PRINCIPAL_LEN) == 0);

	auth__sys__free_unpacked(authsys_y, &alloc.alloc);
out_authsys_x:
	auth__sys__free_unpacked(authsys_x, &alloc.alloc);
out_token_y:
	auth__token__free_unpacked(token_y, &alloc.alloc);
out_token_x:
	auth__token__free_unpacked(token_x, &alloc.alloc);
out:
	return rc;
}
