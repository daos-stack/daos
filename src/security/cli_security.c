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
static Drpc__Call *new_credential_request(void);
static int send_drpc_message(Drpc__Call *message, Drpc__Response **response);
static char *get_agent_socket_path(void);
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

	drpc__response__free_unpacked(response, NULL);
	return rc;
}

static int
request_credentials_via_drpc(Drpc__Response **response)
{
	Drpc__Call	*request = new_credential_request();
	int		rc;

	if (request == NULL) {
		return -DER_NOMEM;
	}

	rc = send_drpc_message(request, response);

	drpc__call__free_unpacked(request, NULL);
	return rc;
}

static Drpc__Call *
new_credential_request(void)
{
	Drpc__Call *request;

	D_ALLOC_PTR(request);
	if (request != NULL) {
		drpc__call__init(request);

		request->module = DRPC_MODULE_SECURITY_AGENT;
		request->method =
				DRPC_METHOD_SECURITY_AGENT_REQUEST_CREDENTIALS;

		/* No body needed - agent knows what auth flavor to give us */
	}

	return request;
}

static int
send_drpc_message(Drpc__Call *message, Drpc__Response **response)
{
	struct drpc	*agent_socket;
	int		rc;

	agent_socket = drpc_connect(get_agent_socket_path());
	if (agent_socket == NULL) {
		/* can't connect to agent socket */
		return -DER_BADPATH;
	}

	rc = drpc_call(agent_socket, R_SYNC, message, response);
	drpc_close(agent_socket);

	return rc;
}


static char *
get_agent_socket_path(void)
{
	/*
	 * UDS path may be set in an environment variable.
	 */
	char *path = getenv(DAOS_AGENT_DRPC_SOCK_ENV);

	if (path == NULL) {
		path = DEFAULT_DAOS_AGENT_DRPC_SOCK;
	}

	return path;
}

static int
process_credential_response(Drpc__Response *response,
		daos_iov_t *creds)
{
	int rc = DER_SUCCESS;

	if (response == NULL) {
		return -DER_NOREPLY;
	}

	if (response->status != DRPC__STATUS__SUCCESS) {
		/* Recipient could not parse our message */
		D_ERROR("Response status is: %d\n", response->status);
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
		/* Malformed body */
		D_ERROR("Unable to unmarshal credential: body.len: %zu\n",
			response->body.len);
		return -DER_MISC;
	}

	/* Not super useful if we didn't get a token */
	if (pb_cred->token == NULL) {
		D_ERROR("Credential has no token\n");
		rc = -DER_MISC;
	}

	security_credential__free_unpacked(pb_cred, NULL);
	return rc;
}

