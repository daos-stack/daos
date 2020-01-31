/*
 * (C) Copyright 2018-2020 Intel Corporation.
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

/*
 * Some convenience functions for unit tests
 */

#include <daos/test_mocks.h>
#include <daos/test_utils.h>

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

void
mock_valid_drpc_call_in_recvmsg(void)
{
	Drpc__Call *call = new_drpc_call();

	/* Mock a valid DRPC call coming in */
	recvmsg_return = drpc__call__get_packed_size(call);
	drpc__call__pack(call, recvmsg_msg_content);

	drpc__call__free_unpacked(call, NULL);
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
mock_valid_drpc_resp_in_recvmsg(Drpc__Status status)
{
	Drpc__Response *resp = new_drpc_response();

	resp->status = status;

	/* Mock a valid DRPC response coming in */
	recvmsg_return = drpc__response__get_packed_size(resp);
	drpc__response__pack(resp, recvmsg_msg_content);

	drpc__response__free_unpacked(resp, NULL);
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

