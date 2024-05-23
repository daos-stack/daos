/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * Some convenience functions for unit tests
 */

#include <daos/test_utils.h>

#if D_HAS_WARNING(4, "-Wframe-larger-than=")
	#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif

struct drpc*
new_drpc_with_fd(int fd)
{
	struct drpc *ctx;

	D_ALLOC_PTR(ctx);
	D_ALLOC_PTR(ctx->comm);

	ctx->comm->fd = fd;
	ctx->comm->flags = R_SYNC;

	ctx->sequence = 1;
	ctx->handler = mock_drpc_handler;
	ctx->ref_count = 1;

	return ctx;
}

void
free_drpc(struct drpc *ctx)
{
	if (ctx) {
		D_FREE(ctx->comm);
		D_FREE(ctx);
	}
}

Drpc__Call*
new_drpc_call(void)
{
	return new_drpc_call_with_module(1);
}

Drpc__Call*
new_drpc_call_with_module(int module_id)
{
	Drpc__Call *call;

	D_ALLOC_PTR(call);

	drpc__call__init(call);
	call->module = module_id;
	call->method = 2;
	call->sequence = 3;

	return call;
}

Drpc__Response*
new_drpc_response(void)
{
	Drpc__Response *resp;

	D_ALLOC_PTR(resp);

	drpc__response__init(resp);
	resp->status = DRPC__STATUS__FAILURE;

	return resp;
}

void
fill_ace_list_with_users(struct daos_ace *ace[], size_t num_aces)
{
	int i;

	for (i = 0; i < num_aces; i++) {
		char name[256];

		snprintf(name, sizeof(name), "user%d@", i + 1);
		ace[i] = daos_ace_create(DAOS_ACL_USER, name);
		ace[i]->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	}
}

void
free_all_aces(struct daos_ace *ace[], size_t num_aces)
{
	int i;

	for (i = 0; i < num_aces; i++) {
		daos_ace_free(ace[i]);
	}
}

/* Mock for the drpc->handler function pointer */
void
mock_drpc_handler_setup(void)
{
	mock_drpc_handler_call_count = 0;
	mock_drpc_handler_call = NULL;
	mock_drpc_handler_resp_ptr = NULL;
	mock_drpc_handler_resp_return = new_drpc_response();
}

void
mock_drpc_handler_teardown(void)
{
	if (mock_drpc_handler_call != NULL) {
		drpc__call__free_unpacked(mock_drpc_handler_call, NULL);
	}

	drpc__response__free_unpacked(mock_drpc_handler_resp_return, NULL);
}

int mock_drpc_handler_call_count; /* how many times it was called */
Drpc__Call *mock_drpc_handler_call; /* alloc copy of the structure passed in */
void *mock_drpc_handler_resp_ptr; /* saved value of resp ptr */
Drpc__Response *mock_drpc_handler_resp_return; /* to be returned in *resp */
void
mock_drpc_handler(Drpc__Call *call, Drpc__Response *resp)
{
	uint8_t buffer[UNIXCOMM_MAXMSGSIZE];

	mock_drpc_handler_call_count++;

	if (call == NULL) {
		mock_drpc_handler_call = NULL;
	} else {
		/*
		 * Caller will free the original so we want to make a copy.
		 * Keep only the latest call.
		 */
		if (mock_drpc_handler_call != NULL) {
			drpc__call__free_unpacked(mock_drpc_handler_call, NULL);
		}

		/*
		 * Drpc__Call has hierarchy of pointers - easiest way to
		 * copy is to pack and unpack.
		 */
		drpc__call__pack(call, buffer);
		mock_drpc_handler_call = drpc__call__unpack(NULL,
				drpc__call__get_packed_size(call),
				buffer);
	}

	mock_drpc_handler_resp_ptr = (void *)resp;

	if (resp != NULL && mock_drpc_handler_resp_return != NULL) {
		size_t len;

		len = mock_drpc_handler_resp_return->body.len;
		memcpy(resp, mock_drpc_handler_resp_return,
				sizeof(Drpc__Response));
		resp->body.len = len;
		if (len > 0) {
			D_ALLOC(resp->body.data, len);
			memcpy(resp->body.data,
				mock_drpc_handler_resp_return->body.data, len);
		}
	}
}

