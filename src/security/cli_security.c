/*
 * (C) Copyright 2018-2019 Intel Corporation.
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
#include <daos/drpc_modules.h>
#include <daos/drpc.pb-c.h>
#include <daos/agent.h>
#include <daos/security.h>

#include "security.pb-c.h"

/* Prototypes for static helper functions */
static int request_credentials_via_drpc(Drpc__Response **response);
static int process_credential_response(Drpc__Response *response,
		daos_iov_t *creds);
static int sanity_check_credential_response(Drpc__Response *response);

int
dc_sec_request_creds(daos_iov_t *creds)
{
	Drpc__Response	*response = NULL;
	int		rc;

	if (creds == NULL) {
		return -DER_INVAL;
	}

	rc = request_credentials_via_drpc(&response);
	if (rc != DER_SUCCESS) {
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
		D_ERROR("DAOS Socket Path is Unitialized\n");
		return -DER_UNINIT;
	}

	agent_socket = drpc_connect(dc_agent_sockpath);
	if (agent_socket == NULL) {
		D_ERROR("Can't connect to agent socket\n");
		return -DER_BADPATH;
	}

	request = drpc_call_create(agent_socket,
			DRPC_MODULE_SECURITY_AGENT,
			DRPC_METHOD_SECURITY_AGENT_REQUEST_CREDENTIALS);
	if (request == NULL) {
		D_ERROR("Couldn't allocate dRPC call\n");
		drpc_close(agent_socket);
		return -DER_NOMEM;
	}

	rc = drpc_call(agent_socket, R_SYNC, request, response);

	drpc_close(agent_socket);
	drpc_call_free(request);
	return rc;
}

static int
process_credential_response(Drpc__Response *response,
		daos_iov_t *creds)
{
	int rc = DER_SUCCESS;

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

	rc = sanity_check_credential_response(response);
	if (rc == DER_SUCCESS) {
		uint8_t *bytes;

		/*
		 * Need to allocate a new buffer to return, since response->body
		 * will be freed
		 */
		D_ALLOC(bytes, response->body.len);
		if (bytes == NULL) {
			D_ERROR("Could not allocate iov buffer\n");
			return -DER_NOMEM;
		}

		memcpy(bytes, response->body.data, response->body.len);
		daos_iov_set(creds, bytes, response->body.len);
	}

	return rc;
}

static int
sanity_check_credential_response(Drpc__Response *response)
{
	int rc = DER_SUCCESS;

	/* Unpack the response body for a basic sanity check */
	SecurityCredential *pb_cred = security_credential__unpack(NULL,
			response->body.len, response->body.data);
	if (pb_cred == NULL) {
		D_ERROR("Body was not a SecurityCredential\n");
		return -DER_MISC;
	}

	if (pb_cred->token == NULL) {
		D_ERROR("Credential did not include token\n");
		rc = -DER_MISC;
	}

	security_credential__free_unpacked(pb_cred, NULL);
	return rc;
}

