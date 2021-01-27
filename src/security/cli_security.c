/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <unistd.h>
#include <string.h>
#include <daos_errno.h>
#include <daos/drpc.h>
#include <daos/drpc_modules.h>
#include <daos/drpc.pb-c.h>
#include <daos/agent.h>
#include <daos/security.h>

#include "auth.pb-c.h"

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
		D_ERROR("Can't connect to agent socket " DF_RC "\n", DP_RC(rc));
		return rc;
	}

	rc = drpc_call_create(agent_socket,
			      DRPC_MODULE_SEC_AGENT,
			      DRPC_METHOD_SEC_AGENT_REQUEST_CREDS,
			      &request);
	if (rc != -DER_SUCCESS) {
		D_ERROR("Couldn't allocate dRPC call " DF_RC "\n", DP_RC(rc));
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

	cred_resp = auth__get_cred_resp__unpack(&alloc.alloc,
						response->body.len,
						response->body.data);
	if (alloc.oom)
		return -DER_NOMEM;
	if (cred_resp == NULL) {
		D_ERROR("Body was not a GetCredentialResp");
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

	rc = auth_cred_to_iov(cred_resp->cred, cred);
out:
	auth__get_cred_resp__free_unpacked(cred_resp, &alloc.alloc);
	return rc;
}
