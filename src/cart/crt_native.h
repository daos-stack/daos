/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef CRT_NATIVE_BRIDGE_H
#define CRT_NATIVE_BRIDGE_H
#include "native/net.h"

struct crt_context;
struct crt_hg_context;
struct crt_rpc_priv;
struct crt_native_context;
struct crt_protocol_info;
struct crt_req_hdr;
int
crt_native_header_peek(const void *, size_t, struct crt_req_hdr *);
struct crt_native_event {
	struct crt_native_event *next;
	bool                     client;
};
struct crt_native_rpc_state {
	struct crt_native_event       event;
	struct crt_native_context    *context;
	struct crt_native_peer       *peer;
	struct crt_native_rpc        *engine;
	struct crt_native_rpc_state  *next;
	struct crt_native_rpc_state **previous;
	struct crt_native_rpc_state  *pending_next;
	struct crt_native_rpc_state **pending_previous;
	struct crt_rpc_priv          *rpc;
	struct crt_native_rx_lease    rx;
	struct crt_native_token       token;
	const void                   *payload;
	void                         *copy;
	size_t                        size;
	uint64_t                      id;
	int                           error;
	uint32_t                      status;
	bool submitted, sent, canceled, event_queued, reply_queued, receiving;
};
int
crt_native_ctx_init(struct crt_hg_context *, int index, bool primary, int iface_index);
int
crt_native_ctx_fini(struct crt_hg_context *);
int
crt_native_ctx_unpost(struct crt_hg_context *);
int
crt_native_ctx_progress(struct crt_hg_context *);
int
crt_native_ctx_addr(struct crt_hg_context *, char *, size_t *);
int
crt_native_addr_lookup(struct crt_hg_context *, const char *, void **);
void
crt_native_addr_free(void *);
int
crt_native_req_create(struct crt_hg_context *, struct crt_rpc_priv *);
void
crt_native_req_send(struct crt_rpc_priv *);
int
crt_native_req_cancel(struct crt_rpc_priv *);
int
crt_native_reply_send(struct crt_rpc_priv *);
void
crt_native_reply_error(struct crt_rpc_priv *, int);
void
crt_native_req_destroy(struct crt_rpc_priv *);
char *
crt_native_origin(struct crt_rpc_priv *);
int
crt_native_protocol_info(struct crt_protocol_info **);
int
crt_native_bulk_context_hold(void *context);
void
crt_native_bulk_context_drop(void *context);
#endif
