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

	/* Unpack the response body for a basic sanity check */
	AuthToken *pb_auth = auth_token__unpack(NULL,
			response->body.len, response->body.data);
	if (pb_auth == NULL) {
		/* Malformed body */
		return -DER_MISC;
	}

	/* Not super useful if we didn't get token data*/
	if (!pb_auth->has_data) {
		rc = -DER_MISC;
	}

	auth_token__free_unpacked(pb_auth, NULL);
	return rc;
}

static Drpc__Call *
new_validation_request(daos_iov_t *creds)
{
	uint8_t		*body;
	Drpc__Call	*request;

	D_ALLOC_PTR(request);

	if (request == NULL) {
		return NULL;
	}

	D_ALLOC(body, creds->iov_len);
	if (body == NULL) {
		return NULL;
	}

	memcpy(body, creds->iov_buf, creds->iov_len);
	drpc__call__init(request);

	request->module = DRPC_MODULE_SECURITY_SERVER;
	request->method =
		DRPC_METHOD_SECURITY_SERVER_VALIDATE_CREDENTIALS;
	request->body.len = creds->iov_len;
	request->body.data = body;

	return request;
}

static int
send_drpc_message(Drpc__Call *message, Drpc__Response **response)
{
	struct drpc	*server_socket;
	int		rc;

	server_socket = drpc_connect(ds_sec_server_socket_path);
	if (server_socket == NULL) {
		/* can't connect to agent socket */
		return -DER_BADPATH;
	}

	rc = drpc_call(server_socket, R_SYNC, message, response);
	drpc_close(server_socket);

	return rc;
}

static int
validate_credentials_via_drpc(Drpc__Response **response, daos_iov_t *creds)
{
	Drpc__Call	*request;
	int		rc;

	request = new_validation_request(creds);

	if (request == NULL) {
		return -DER_NOMEM;
	}

	rc = send_drpc_message(request, response);

	drpc__call__free_unpacked(request, NULL);
	return rc;
}

static int
process_validation_response(Drpc__Response *response,
		AuthToken **token)
{
	int		rc = DER_SUCCESS;
	AuthToken	*auth;

	if (response == NULL) {
		return -DER_NOREPLY;
	}

	if (response->status != DRPC__STATUS__SUCCESS) {
		/* Recipient could not parse our message */
		return -DER_MISC;
	}

	rc = sanity_check_validation_response(response);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	auth = auth_token__unpack(NULL, response->body.len,
					response->body.data);
	if (auth == NULL) {
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

	drpc__response__free_unpacked(response, NULL);
	return rc;
}

int
ds_sec_can_pool_connect(const struct pool_prop_ugm *ugm, d_iov_t *cred,
				uint64_t access)
{
	int		rc;
	int		shift;
	uint32_t	access_permitted;
	AuthToken	*sec_token = NULL;
	AuthSys		*sys_creds = NULL;

	if (ugm == NULL || cred == NULL) {
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
	if (sys_creds->uid == ugm->pp_uid)
		shift = DAOS_PC_NBITS * 2;	/* user */
	else if (sys_creds->gid == ugm->pp_gid)
		shift = DAOS_PC_NBITS;		/* group */
	else
		shift = 0;			/* other */

	/* Extract the applicable set of capability bits. */
	access_permitted = (ugm->pp_mode >> shift) & DAOS_PC_MASK;

	/* Only if all requested capability bits are permitted... */
	return (access & access_permitted) == access;
}
