/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef CRT_NATIVE_RPC_H
#define CRT_NATIVE_RPC_H
#include "wire.h"
#include <stdbool.h>

struct crt_native_rpc;
struct crt_native_token {
	uint64_t id;
	bool     reply;
};
struct crt_native_tx {
	struct crt_native_token token;
	const void             *data;
	size_t                  length;
};

/* Single-owner eager RPC engine; no CaRT, verbs or CUDA dependencies. All
 * buffers and request records are allocated at create time. Payload pointers
 * passed to handlers/completions are borrowed for the duration of the callback.
 * Callbacks may call call()/reply(), but must not cancel, recursively drive
 * receive()/tx_complete() or destroy the session.
 *
 * next_tx() transfers a stable frame to the driver. The driver MUST call
 * tx_complete() exactly once, including for a rejected transport post. It must
 * delay that call for an accepted post until DMA completion, including errors.
 * Failed next_tx()/call()/reply() calls produce no callbacks.
 *
 * The driver preposts at least 2*depth receives and reserves capacity for both
 * request and reply sends. receive()==EAGAIN means retain the frame and retry;
 * no handler has run. Repost receive buffers before admitting more requests.
 */
typedef void (*crt_native_done_t)(void *arg, int error, uint32_t remote_status, const void *payload,
				  size_t size);
typedef void (*crt_native_handler_t)(void    *arg, struct crt_native_rpc *, struct crt_native_token,
				     uint32_t opcode, const void *payload, size_t size);

int
crt_native_rpc_create(unsigned depth, size_t frame_size, crt_native_handler_t, void *,
		      struct crt_native_rpc **);
int
crt_native_rpc_call(struct crt_native_rpc *, uint32_t opcode, const void *, size_t,
		    crt_native_done_t, void *, uint64_t *id);
int
crt_native_rpc_reply(struct crt_native_rpc *, struct crt_native_token, uint32_t status,
		     const void *, size_t);
int
crt_native_rpc_next_tx(struct crt_native_rpc *, struct crt_native_tx *);
int
crt_native_rpc_tx_complete(struct crt_native_rpc *, struct crt_native_token, int error);
int
crt_native_rpc_receive(struct crt_native_rpc *, const void *, size_t);
int
crt_native_rpc_cancel(struct crt_native_rpc *, uint64_t id, int error);
/* Fail all calls; accepted send buffers stay pinned until tx_complete(). */
int
crt_native_rpc_fail(struct crt_native_rpc *, int error);
/* EBUSY while calls, handlers or driver-owned send frames remain outstanding. */
int
crt_native_rpc_destroy(struct crt_native_rpc *);
/* Stable backing slab, for one-time driver registration. The slab cannot be
 * deregistered while next_tx() frames remain driver-owned. Registration and
 * deregistration are the driver's responsibility, not the RPC engine's.
 */
int
crt_native_rpc_buffers(struct crt_native_rpc *, void **base, size_t *size);
#endif
