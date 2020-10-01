/*
 * (C) Copyright 2019-2020 Intel Corporation.
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
 * Mocks of dRPC framework functions
 */

#include "drpc_mocks.h"

struct drpc *drpc_connect_return; /* value to be returned */
char drpc_connect_sockaddr[PATH_MAX + 1]; /* saved copy of input */
int
drpc_connect(char *sockaddr, struct drpc **drpcp)
{
	strncpy(drpc_connect_sockaddr, sockaddr, PATH_MAX);

	*drpcp = drpc_connect_return;
	if (drpc_connect_return)
		return -DER_SUCCESS;

	return -DER_BADPATH;
}

void
mock_drpc_connect_setup(void)
{
	D_ALLOC_PTR(drpc_connect_return);
	memset(drpc_connect_sockaddr, 0, sizeof(drpc_connect_sockaddr));
}

void mock_drpc_connect_teardown(void)
{
	free_drpc_connect_return();
}

int drpc_call_return; /* value to be returned */
struct drpc *drpc_call_ctx; /* saved input */
int drpc_call_flags; /* saved input */
Drpc__Call drpc_call_msg_content; /* saved copy of input */
/* saved input ptr address (for checking non-NULL) */
Drpc__Call *drpc_call_msg_ptr;
/* saved input ptr address (for checking non-NULL) */
Drpc__Response **drpc_call_resp_ptr;
/* ptr to content to allocate in response (can be NULL) */
Drpc__Response *drpc_call_resp_return_ptr;
/* actual content to allocate in response */
Drpc__Response drpc_call_resp_return_content;
int
drpc_call(struct drpc *ctx, int flags, Drpc__Call *msg,
		Drpc__Response **resp)
{
	/* Save off the params passed in */
	drpc_call_ctx = ctx;
	drpc_call_flags = flags;
	drpc_call_msg_ptr = msg;
	if (msg != NULL) {
		memcpy(&drpc_call_msg_content, msg, sizeof(Drpc__Call));

		/* Need a copy of the body data, it's separately allocated */
		D_ALLOC(drpc_call_msg_content.body.data, msg->body.len);
		memcpy(drpc_call_msg_content.body.data, msg->body.data,
				msg->body.len);
	}
	drpc_call_resp_ptr = resp;

	if (resp == NULL) {
		return drpc_call_return;
	}

	/* Fill out the mocked response */
	if (drpc_call_resp_return_ptr == NULL) {
		*resp = NULL;
	} else {
		size_t data_len = drpc_call_resp_return_content.body.len;

		/**
		 * Need to allocate a new copy to return - the
		 * production code will free the returned memory.
		 */
		D_ALLOC_PTR(*resp);
		memcpy(*resp, &drpc_call_resp_return_content,
				sizeof(Drpc__Response));

		D_ALLOC((*resp)->body.data, data_len);
		memcpy((*resp)->body.data,
				drpc_call_resp_return_content.body.data,
				data_len);
	}

	return drpc_call_return;
}

static void
init_drpc_call_resp(void)
{
	/* By default, return non-null response */
	drpc_call_resp_return_ptr = &drpc_call_resp_return_content;

	drpc__response__init(&drpc_call_resp_return_content);
	drpc_call_resp_return_content.status = DRPC__STATUS__SUCCESS;
}

void
mock_drpc_call_setup(void)
{
	drpc_call_return = 0;
	drpc_call_ctx = NULL;
	drpc_call_flags = 0;
	drpc_call_msg_ptr = NULL;
	memset(&drpc_call_msg_content, 0, sizeof(drpc_call_msg_content));
	drpc_call_resp_ptr = NULL;
	init_drpc_call_resp();
}

void
mock_drpc_call_teardown(void)
{
	free_drpc_call_msg_body();
	free_drpc_call_resp_body();
}

int drpc_close_return; /* value to be returned */
struct drpc *drpc_close_ctx; /* saved input ptr */
int
drpc_close(struct drpc *ctx)
{
	drpc_close_ctx = ctx;
	return drpc_close_return;
}

void
mock_drpc_close_setup(void)
{
	drpc_close_return = 0;
	drpc_close_ctx = NULL;
}

void
free_drpc_connect_return(void)
{
	D_FREE(drpc_connect_return);
}

void
free_drpc_call_msg_body(void)
{
	D_FREE(drpc_call_msg_content.body.data);
	drpc_call_msg_content.body.len = 0;
}

void
free_drpc_call_resp_body(void)
{
	D_FREE(drpc_call_resp_return_content.body.data);
	drpc_call_resp_return_content.body.len = 0;
}

void
pack_get_cred_resp_in_drpc_call_resp_body(Auth__GetCredResp *resp)
{
	size_t	len = auth__get_cred_resp__get_packed_size(resp);
	uint8_t	*body;

	D_FREE(drpc_call_resp_return_content.body.data);

	drpc_call_resp_return_content.body.len = len;
	D_ALLOC(body, len);
	auth__get_cred_resp__pack(resp, body);
	drpc_call_resp_return_content.body.data = body;
}

void
pack_validate_resp_in_drpc_call_resp_body(Auth__ValidateCredResp *resp)
{
	size_t	len = auth__validate_cred_resp__get_packed_size(resp);
	uint8_t	*body;

	D_FREE(drpc_call_resp_return_content.body.data);

	drpc_call_resp_return_content.body.len = len;
	D_ALLOC(body, len);
	auth__validate_cred_resp__pack(resp, body);
	drpc_call_resp_return_content.body.data = body;
}
