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

#ifndef __DAOS_DRPC_H__
#define __DAOS_DRPC_H__

#include <daos/drpc.pb-c.h>
#include <daos/common.h>

/*
 * Using a packetsocket over the unix domain socket means that we receive
 * a whole message at a time without knowing its size. So for this reason
 * we need to restrict the maximum message size so we can preallocate a
 * buffer to put all of the information in. This value is also defined in
 * the corresponding golang file domain_socket_server.go. If changed here
 * it must be changed in that file as well
 */
#define UNIXCOMM_MAXMSGSIZE 16384

struct unixcomm {
	int fd; /** File descriptor of the unix domain socket */
	int flags; /** Flags set on unix domain socket */
};

typedef void (*drpc_handler_t)(Drpc__Call *, Drpc__Response *);

/* Define a custom allocator so we can log and use fault injection
 * in the DRPC code.
 */

struct drpc_alloc {
	ProtobufCAllocator	alloc;
	bool			oom;
};

void *
daos_drpc_alloc(void *arg, size_t size);

void
daos_drpc_free(void *allocater_data, void *pointer);

#define PROTO_ALLOCATOR_INIT(self) {.alloc.alloc = daos_drpc_alloc,	\
				    .alloc.free = daos_drpc_free,	\
				    .alloc.allocator_data = &self}

/**
 * dRPC connection context. This includes all details needed to communicate
 * on the dRPC channel.
 */
struct drpc {
	struct unixcomm	*comm; /** unix domain socket communication context */
	int		sequence; /** sequence number of latest message sent */
	uint32_t	ref_count; /** open refs to this ctx */

	/**
	 * Handler for messages received by a listening drpc context.
	 * For client contexts, this is NULL.
	 */
	drpc_handler_t	handler;
};

enum rpcflags {
	R_SYNC = 1
};

int drpc_call_create(struct drpc *ctx, int32_t module, int32_t method,
		     Drpc__Call **callp);
void drpc_call_free(Drpc__Call *call);

Drpc__Response *drpc_response_create(Drpc__Call *call);
void drpc_response_free(Drpc__Response *resp);

int drpc_call(struct drpc *ctx, int flags, Drpc__Call *msg,
		Drpc__Response **resp);
int drpc_connect(char *sockaddr, struct drpc **);
struct drpc *drpc_listen(char *sockaddr, drpc_handler_t handler);
bool drpc_is_valid_listener(struct drpc *ctx);
struct drpc *drpc_accept(struct drpc *listener_ctx);
int drpc_recv_call(struct drpc *ctx, Drpc__Call **call);
int drpc_send_response(struct drpc *ctx, Drpc__Response *resp);
int drpc_close(struct drpc *ctx);

int drpc_add_ref(struct drpc *ctx);

#endif /* __DAOS_DRPC_H__ */
