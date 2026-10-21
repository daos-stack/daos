/*
 * (C) Copyright 2018-2023 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_DRPC_H__
#define __DAOS_DRPC_H__

#include <daos/drpc_types.h>
#include <daos/drpc.pb-c.h>
#include <daos/common.h>

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

struct unixcomm {
	int fd;    /** File descriptor of the unix domain socket */
	int flags; /** Flags set on unix domain socket */
};

typedef void (*drpc_handler_t)(Drpc__Call *, Drpc__Response *);
typedef int (*drpc_thread_yielder_t)(void);

/**
 * dRPC connection context. This includes all details needed to communicate
 * on the dRPC channel.
 */
struct drpc {
	struct unixcomm      *comm;      /** unix domain socket communication context */
	int                   sequence;  /** sequence number of latest message sent */
	uint32_t              ref_count; /** open refs to this ctx */

	/**
	 * Handler for messages received by a listening drpc context.
	 * For client contexts, this is NULL.
	 */
	drpc_handler_t        handler;

	/**
	 * Function to yield cycles to a non-dRPC thread, if applicable.
	 */
	drpc_thread_yielder_t yield;
};

enum rpcflags { R_SYNC = 1 };

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
int
    drpc_accept(struct drpc *listener_ctx, struct drpc **drpc);
int drpc_recv_call(struct drpc *ctx, Drpc__Call **call);
int drpc_send_response(struct drpc *ctx, Drpc__Response *resp);
int drpc_close(struct drpc *ctx);

int
drpc_add_ref(struct drpc *ctx);

#endif /* __DAOS_DRPC_H__ */
