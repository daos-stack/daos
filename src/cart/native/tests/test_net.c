/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "../net.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOTS 256
struct crt_native_mr {
	struct crt_native_ep *ep;
	uint8_t              *base;
	size_t                size;
	unsigned              refs;
};
struct queued {
	struct crt_native_completion completion;
	struct crt_native_mr        *mr;
};
struct receive {
	struct crt_native_mr *mr;
	size_t                offset, size;
	void                 *cookie;
};
struct crt_native_ep {
	struct crt_native_ep *peer;
	struct queued         cq[SLOTS];
	struct receive        rx[SLOTS];
	unsigned              id, head, tail, rx_head, rx_tail, registered;
	bool                  closing, closed;
};
static struct crt_native_ep *endpoints[2048];
static unsigned              endpoint_count;
static int                   dereg_error;
static int
open_ep(const struct crt_native_config *config, struct crt_native_ep **out)
{
	(void)config;
	assert(endpoint_count < 2048);
	struct crt_native_ep *ep = calloc(1, sizeof(*ep));
	assert(ep != NULL);
	ep->id                      = endpoint_count + 1;
	endpoints[endpoint_count++] = ep;
	*out                        = ep;
	return 0;
}
static unsigned
capacity(struct crt_native_ep *ep)
{
	(void)ep;
	return 64;
}
static int
address(struct crt_native_ep *ep, struct crt_native_address *out)
{
	*out = (struct crt_native_address){.qpn = ep->id, .psn = 1, .mtu = 4, .lid = 1};
	return 0;
}
static int
connect_ep(struct crt_native_ep *ep, const struct crt_native_address *remote)
{
	if (remote->qpn == 0 || remote->qpn > endpoint_count)
		return EINVAL;
	ep->peer = endpoints[remote->qpn - 1];
	return 0;
}
static int
reg(struct crt_native_ep *ep, void *data, size_t size, unsigned access, struct crt_native_mr **out,
    struct crt_native_remote_memory *remote)
{
	(void)access;
	*out = calloc(1, sizeof(**out));
	assert(*out != NULL);
	(*out)->ep   = ep;
	(*out)->base = data;
	(*out)->size = size;
	ep->registered++;
	*remote = (struct crt_native_remote_memory){(uintptr_t)data, size, 7};
	return 0;
}
static int
dereg(struct crt_native_mr *mr)
{
	assert(mr->refs == 0);
	if (dereg_error)
		return dereg_error;
	mr->ep->registered--;
	free(mr);
	return 0;
}
static void
complete(struct crt_native_ep *ep, struct crt_native_mr *mr, enum crt_native_opcode opcode,
	 void *cookie, size_t bytes, int error)
{
	assert(ep->tail - ep->head < SLOTS);
	ep->cq[ep->tail++ % SLOTS] = (struct queued){{cookie, opcode, error, bytes}, mr};
}
static int
post(struct crt_native_ep *ep, enum crt_native_opcode opcode, struct crt_native_mr *mr,
     size_t offset, size_t size, const struct crt_native_remote_memory *remote, size_t within,
     void *cookie)
{
	(void)remote;
	(void)within;
	assert(offset <= mr->size && size <= mr->size - offset);
	if (ep->closing)
		return ESHUTDOWN;
	if (opcode == CRT_NATIVE_RECV) {
		assert(ep->rx_tail - ep->rx_head < SLOTS);
		ep->rx[ep->rx_tail++ % SLOTS] = (struct receive){mr, offset, size, cookie};
		mr->refs++;
		return 0;
	}
	assert(opcode == CRT_NATIVE_SEND && ep->peer != NULL);
	if (ep->peer->closing)
		return ESHUTDOWN;
	if (ep->peer->rx_tail == ep->peer->rx_head)
		return EAGAIN;
	struct receive rx = ep->peer->rx[ep->peer->rx_head++ % SLOTS];
	assert(size <= rx.size);
	memcpy(rx.mr->base + rx.offset, mr->base + offset, size);
	mr->refs++;
	complete(ep, mr, CRT_NATIVE_SEND, cookie, size, 0);
	complete(ep->peer, rx.mr, CRT_NATIVE_RECV, rx.cookie, size, 0);
	return 0;
}
static int
progress(struct crt_native_ep *ep, struct crt_native_completion *out, unsigned capacity,
	 unsigned *count)
{
	*count = 0;
	while (*count < capacity && ep->head != ep->tail) {
		struct queued q = ep->cq[ep->head++ % SLOTS];
		assert(q.mr->refs);
		q.mr->refs--;
		out[(*count)++] = q.completion;
	}
	return 0;
}
static int
close_begin(struct crt_native_ep *ep)
{
	ep->closing = true;
	while (ep->rx_head != ep->rx_tail) {
		struct receive rx = ep->rx[ep->rx_head++ % SLOTS];
		complete(ep, rx.mr, CRT_NATIVE_RECV, rx.cookie, 0, ECANCELED);
	}
	return 0;
}
static int
quiesce(struct crt_native_ep *ep)
{
	return ep->head == ep->tail && ep->rx_head == ep->rx_tail ? 0 : EBUSY;
}
static int
close_ep(struct crt_native_ep *ep)
{
	if (ep->registered || quiesce(ep))
		return EBUSY;
	ep->closed = true;
	return 0;
}
static const struct crt_native_ops ops = {.open              = open_ep,
					  .capacity          = capacity,
					  .address           = address,
					  .connect           = connect_ep,
					  .register_memory   = reg,
					  .deregister_memory = dereg,
					  .post              = post,
					  .progress          = progress,
					  .close_begin       = close_begin,
					  .quiesce           = quiesce,
					  .close             = close_ep};

struct stats {
	unsigned                   ready, received, completed, failed;
	bool                       hold_receive;
	bool                       hold_destroy;
	struct crt_native_rx_lease lease;
	struct crt_native_peer    *retained_peer;
};
static int
ready(void *arg, struct crt_native_peer *peer)
{
	struct stats *s = arg;
	assert(crt_native_peer_session(peer));
	s->ready++;
	return 0;
}
static void
request(void *arg, struct crt_native_peer *peer, struct crt_native_rpc *rpc,
	struct crt_native_token token, uint32_t opcode, const void *payload, size_t size)
{
	(void)peer;
	struct stats *s = arg;
	s->received++;
	assert(opcode == 17 && size == 5 && memcmp(payload, "hello", 5) == 0);
	if (s->hold_receive) {
		assert(crt_native_channel_retain(crt_native_peer_channel(peer), payload, size,
						 &s->lease) == 0);
		s->retained_peer = peer;
	}
	if (s->hold_destroy)
		s->retained_peer = peer;
	assert(crt_native_rpc_reply(rpc, token, 0, payload, size) == 0);
}
static void
done(void *arg, int error, uint32_t status, const void *payload, size_t size)
{
	struct stats *s = arg;
	s->completed++;
	assert(error == 0 && status == 0 && size == 5 && memcmp(payload, "hello", 5) == 0);
}
static void
failed(void *arg, struct crt_native_peer *peer, int error)
{
	(void)peer;
	assert(error);
	((struct stats *)arg)->failed++;
}
static void
failed_done(void *arg, int error, uint32_t status, const void *payload, size_t size)
{
	(void)status;
	(void)payload;
	assert(error == ECONNRESET && size == 0);
	(*(unsigned *)arg)++;
}
static int
destroy_data(void *arg, struct crt_native_peer *peer)
{
	(void)peer;
	return ((struct stats *)arg)->hold_destroy ? EBUSY : 0;
}
static void
drive(struct crt_native_net *a, struct crt_native_net *b)
{
	unsigned work;
	assert(crt_native_net_progress(a, &work) == 0);
	assert(crt_native_net_progress(b, &work) == 0);
}

static void *
release_address(void *arg)
{
	crt_native_peer_put(arg);
	return NULL;
}

static void
test_connection_churn(void)
{
	struct crt_native_net   *server;
	struct crt_native_config config = {
	    .device = "fake", .port = 1, .gid_index = -1, .depth = 32};
	struct crt_native_limits        limits = {.frame_size = 128, .receives = 8};
	struct crt_native_net_callbacks cb     = {
		.ready = ready, .request = request, .failed = failed, .destroy = destroy_data};
	struct stats ss = {0};
	assert(crt_native_net_create(&ops, &config, &limits, "127.0.0.1", 0, 2, 1000, &cb, &ss,
				     &server) == 0);
	/* Many more clients than available server slots, without restarting it. */
	for (unsigned round = 0; round < 128; round++) {
		struct crt_native_net  *client;
		struct crt_native_peer *peer;
		struct stats            cs = {0};
		ss.hold_receive            = round == 0;
		ss.hold_destroy            = round == 2;
		assert(crt_native_net_create(&ops, &config, &limits, "127.0.0.1", 0, 2, 1000, &cb,
					     &cs, &client) == 0);
		assert(crt_native_net_lookup(client, crt_native_net_uri(server), &peer) == 0);
		for (unsigned i = 0; i < 20000 && crt_native_peer_status(peer); i++)
			drive(client, server);
		assert(crt_native_peer_status(peer) == 0);
		uint64_t id;
		assert(crt_native_channel_call(crt_native_peer_channel(peer), 17, "hello", 5, done,
					       &cs, &id) == 0);
		for (unsigned i = 0; i < 128 && !cs.completed; i++)
			drive(client, server);
		assert(cs.completed == 1);
		/* Failed deregistration must retain resources and retry later. */
		dereg_error = round == 1 ? EBUSY : 0;
		crt_native_peer_put(peer);
		assert(crt_native_net_shutdown(client) == 0);
		for (unsigned i = 0; i < 64; i++)
			drive(client, server);
		if (ss.hold_receive) {
			assert(crt_native_peer_channel(ss.retained_peer) != NULL);
			assert(crt_native_channel_release(&ss.lease) == 0);
			ss.retained_peer = NULL;
		}
		if (ss.hold_destroy) {
			assert(crt_native_peer_bulk_endpoint(ss.retained_peer) != NULL);
			ss.hold_destroy  = false;
			ss.retained_peer = NULL;
		}
		if (dereg_error)
			assert(crt_native_net_destroy(client) == EBUSY);
		dereg_error = 0;
		for (unsigned i = 0; i < 64; i++)
			drive(client, server);
		assert(crt_native_net_destroy(client) == 0);
		/* QPs/CQs and registered memory must return to baseline each round. */
		for (unsigned i = 0; i < endpoint_count; i++)
			assert(endpoints[i]->closed && endpoints[i]->registered == 0);
	}
	assert(ss.received == 128);
	assert(crt_native_net_destroy(server) == 0);
}

int
main(void)
{
	struct crt_native_net   *a, *b;
	struct crt_native_peer  *ab, *ba, *again;
	struct crt_native_config config = {
	    .device = "fake", .port = 1, .gid_index = -1, .depth = 32};
	struct crt_native_limits        limits = {.frame_size = 128, .receives = 8};
	struct crt_native_net_callbacks cb = {.ready = ready, .request = request, .failed = failed};
	struct stats                    sa = {0}, sb = {0};
	assert(crt_native_net_create(&ops, &config, &limits, "127.0.0.1", 0, 4, 1000, &cb, &sa,
				     &a) == 0);
	assert(crt_native_net_create(&ops, &config, &limits, "127.0.0.1", 0, 4, 1000, &cb, &sb,
				     &b) == 0);
	assert(crt_native_net_lookup(a, "doca+rc://127.0.0.1:65536", &again) == EINVAL);
	assert(crt_native_net_lookup(a, "doca+rc://127.0.0.1:1junk", &again) == EINVAL);
	assert(crt_native_net_lookup(a, "ucx://127.0.0.1:1", &again) == EINVAL);
	/* Both directions begin before either context progresses. */
	assert(crt_native_net_lookup(a, crt_native_net_uri(b), &ab) == 0);
	assert(crt_native_net_lookup(b, crt_native_net_uri(a), &ba) == 0);
	assert(crt_native_net_lookup(a, crt_native_net_uri(b), &again) == 0 && again == ab);
	crt_native_peer_put(again);
	for (unsigned i = 0;
	     i < 20000 && (crt_native_peer_status(ab) || crt_native_peer_status(ba)); i++)
		drive(a, b);
	assert(crt_native_peer_status(ab) == 0 && crt_native_peer_status(ba) == 0);
	assert(sa.ready >= 1 && sb.ready >= 1 && sa.failed == 0 && sb.failed == 0);
	for (unsigned round = 0; round < 128; round++) {
		uint64_t id;
		for (unsigned i = 0; i < 4; i++) {
			assert(crt_native_channel_call(crt_native_peer_channel(ab), 17, "hello", 5,
						       done, &sa, &id) == 0);
			assert(crt_native_channel_call(crt_native_peer_channel(ba), 17, "hello", 5,
						       done, &sb, &id) == 0);
		}
		for (unsigned i = 0; i < 128 && (sa.completed != (round + 1) * 4 ||
						 sb.completed != (round + 1) * 4);
		     i++)
			drive(a, b);
		assert(sa.completed == (round + 1) * 4 && sb.completed == (round + 1) * 4);
	}
	/* Fail a connection with queued work: exact-once callback, socket EOF at
	 * its peer, and all registered memory retained until NIC drain/quiesce.
	 */
	unsigned failure_completions = 0;
	uint64_t id;
	assert(crt_native_channel_call(crt_native_peer_channel(ab), 17, "hello", 5, failed_done,
				       &failure_completions, &id) == 0);
	crt_native_peer_fail(ab, ECONNRESET);
	for (unsigned i = 0; i < 128; i++)
		drive(a, b);
	assert(failure_completions == 1 && sa.failed && sb.failed);
	/* A cached address shell is safe after its hardware has been reclaimed. */
	assert(crt_native_peer_status(ab) == ECONNRESET);
	assert(crt_native_peer_channel(ab) == NULL && crt_native_peer_bulk_endpoint(ab) == NULL);
	assert(crt_native_net_shutdown(a) == 0 && crt_native_net_shutdown(b) == 0);
	for (unsigned i = 0; i < 8; i++)
		drive(a, b);
	assert(crt_native_net_destroy(a) == EBUSY);
	pthread_t releaser;
	assert(pthread_create(&releaser, NULL, release_address, ab) == 0);
	for (unsigned i = 0; i < 8; i++)
		drive(a, b);
	assert(pthread_join(releaser, NULL) == 0);
	crt_native_peer_put(ba);
	assert(crt_native_net_destroy(a) == 0 && crt_native_net_destroy(b) == 0);
	test_connection_churn();
	for (unsigned i = 0; i < endpoint_count; i++) {
		assert(endpoints[i]->closed && endpoints[i]->registered == 0);
		free(endpoints[i]);
	}
	puts("Native network: loopback bootstrap, full-duplex RPC, cached address lifetime, "
	     "worker release and 128-client resource/slot reclamation passed");
	return 0;
}
