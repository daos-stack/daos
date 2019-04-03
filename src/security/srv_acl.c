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
		return -DER_MISC;
	}

	if (!pb_auth->has_data) {
		D_ERROR("AuthToken did not include data\n");
		rc = -DER_MISC;
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
		return -DER_MISC;
	}
	*token = auth;

	return rc;
}

int
ds_sec_validate_credentials(daos_iov_t *creds, AuthToken **token)
{
	Drpc__Response	*response = NULL;
	int		rc;

	if (creds == NULL) {
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

int
ds_sec_can_pool_connect(const struct pool_prop_ugm *attr, d_iov_t *cred,
				uint64_t access)
{
	int		rc;
	int		shift;
	uint32_t	access_permitted;
	AuthToken	*sec_token = NULL;
	AuthSys		*sys_creds = NULL;

	if (attr == NULL || cred == NULL) {
		return -DER_INVAL;
	}

	rc = ds_sec_validate_credentials(cred, &sec_token);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	sys_creds = auth_sys__unpack(NULL, sec_token->data.len,
					sec_token->data.data);

	/*
	 * Determine which set of capability bits applies. See also the
	 * comment/diagram for ds_pool_attr_mode in src/pool/srv_layout.h.
	 */
	if (sys_creds->uid == attr->pp_uid)
		shift = DAOS_PC_NBITS * 2;	/* user */
	else if (sys_creds->gid == attr->pp_gid)
		shift = DAOS_PC_NBITS;		/* group */
	else
		shift = 0;			/* other */

	/* Extract the applicable set of capability bits. */
	access_permitted = (attr->pp_mode >> shift) & DAOS_PC_MASK;

	/* Only if all requested capability bits are permitted... */
	return (access & access_permitted) == access;
}
