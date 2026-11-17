/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef CRT_NATIVE_NET_H
#define CRT_NATIVE_NET_H
#include "channel.h"
#include "bulk_session.h"

struct crt_native_net;
struct crt_native_peer;
struct crt_native_net_callbacks {
	int (*ready)(void *, struct crt_native_peer *);
	void (*request)(void *, struct crt_native_peer *, struct crt_native_rpc *,
			struct crt_native_token, uint32_t, const void *, size_t);
	int (*progress)(void *, struct crt_native_peer *);
	void (*failed)(void *, struct crt_native_peer *, int);
	int (*destroy)(void *, struct crt_native_peer *);
};
/* Numeric IPv4 bootstrap, separate RC control/bulk endpoints, no TCP RPC data.
 * Caller serializes these APIs; callbacks execute in the same serialization
 * domain and must not reenter progress/destroy. ready/request callbacks must
 * queue application dispatch rather than call application code under the lock.
 * Connections use bounded nonblocking hello/READY state machines, so simultaneous
 * bidirectional connects do not wait on each other's progress thread.
 */
int
crt_native_net_create(const struct crt_native_ops *, const struct crt_native_config *,
		      const struct crt_native_limits *, const char *ipv4, unsigned port,
		      unsigned max_peers, unsigned timeout_ms,
		      const struct crt_native_net_callbacks *, void *, struct crt_native_net **);
const char *
crt_native_net_uri(const struct crt_native_net *);
int
crt_native_net_lookup(struct crt_native_net *, const char *uri, struct crt_native_peer **);
/* Each successful lookup owns one address reference. Release is worker-safe.
 * Looked-up address shells stay stable until context destruction to support
 * CaRT's borrowed cache lookups; failed hardware resources are reaped earlier.
 * Uncached accepted peers return their slots once DMA and consumers drain.
 */
void
crt_native_peer_put(struct crt_native_peer *);
int
crt_native_net_progress(struct crt_native_net *, unsigned *work);
int
crt_native_net_shutdown(struct crt_native_net *);
int
crt_native_net_destroy(struct crt_native_net *);
int
crt_native_peer_status(const struct crt_native_peer *);
void
crt_native_peer_fail(struct crt_native_peer *, int);
const char *
crt_native_peer_uri(const struct crt_native_peer *);
struct crt_native_channel *
crt_native_peer_channel(struct crt_native_peer *);
struct crt_native_bulk_session *
crt_native_peer_session(struct crt_native_peer *);
struct crt_native_ep *
crt_native_peer_bulk_endpoint(struct crt_native_peer *);
unsigned
crt_native_peer_capacity(const struct crt_native_peer *);
void *
crt_native_peer_data(const struct crt_native_peer *);
void
crt_native_peer_set_data(struct crt_native_peer *, void *);
#endif
