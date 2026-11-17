/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "../net.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
struct crt_native_group {
	unsigned polls;
};

struct crt_native_ep {
	struct crt_native_config config;
	struct crt_native_ep    *peer;
	struct queued            cq[SLOTS];
	struct receive           rx[SLOTS];
	unsigned                 id, head, tail, rx_head, rx_tail, registered;
	bool                     closing, closed;
};
static struct crt_native_ep *endpoints[2048];
static unsigned              endpoint_count;
static int                   dereg_error;
static int
open_ep(const struct crt_native_config *config, struct crt_native_ep **out)
{
	assert(endpoint_count < 2048);
	struct crt_native_ep *ep = calloc(1, sizeof(*ep));
	assert(ep != NULL);
	ep->config                  = *config;
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
static unsigned endpoint_polls;
static int
progress(struct crt_native_ep *ep, struct crt_native_completion *out, unsigned capacity,
	 unsigned *count)
{
	endpoint_polls++;
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
static unsigned grouped_polls;
static int
group_create(const struct crt_native_config *config, unsigned capacity,
	     struct crt_native_group **out)
{
	(void)config;
	(void)capacity;
	*out = calloc(1, sizeof(**out));
	return *out ? 0 : ENOMEM;
}
static int
group_progress(struct crt_native_group *g)
{
	grouped_polls++;
	for (unsigned i = 0; i < endpoint_count; i++) {
		struct crt_native_ep *ep = endpoints[i];
		if (!ep->closed && ep->config.group == g && ep->head != ep->tail)
			ep->config.wake(ep->config.wake_arg);
	}
	return 0;
}
static int
group_destroy(struct crt_native_group *g)
{
	for (unsigned i = 0; i < endpoint_count; i++)
		assert(endpoints[i]->closed || endpoints[i]->config.group != g);
	free(g);
	return 0;
}
static void
wake(struct crt_native_ep *ep)
{
	if (ep->config.wake)
		ep->config.wake(ep->config.wake_arg);
}

static struct crt_native_ops ops = {.group_create      = group_create,
				    .group_progress    = group_progress,
				    .group_destroy     = group_destroy,
				    .wake              = wake,
				    .open              = open_ep,
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
	/* TCP readiness intentionally runs on a millisecond maintenance cadence. */
	struct timespec pause = {.tv_nsec = 50000};
	nanosleep(&pause, NULL);
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

static void
test_many_ready_peers(void)
{
	enum { CLIENTS = 70 };
	struct client {
		struct crt_native_net  *net;
		struct crt_native_peer *peer;
		struct stats            stats;
	} *clients = calloc(CLIENTS, sizeof(*clients));
	assert(clients);
	struct crt_native_config config = {
	    .device = "fake", .port = 1, .gid_index = -1, .depth = 32};
	struct crt_native_limits        limits = {.frame_size = 128, .receives = 8};
	struct crt_native_net_callbacks cb = {.ready = ready, .request = request, .failed = failed};
	struct stats                    ss = {0};
	struct crt_native_net          *server;
	assert(crt_native_net_create(&ops, &config, &limits, "127.0.0.1", 0, 96, 1000, &cb, &ss,
				     &server) == 0);
	for (unsigned i = 0; i < CLIENTS; i++) {
		struct client *c = &clients[i];
		assert(crt_native_net_create(&ops, &config, &limits, "127.0.0.1", 0, 2, 1000, &cb,
					     &c->stats, &c->net) == 0);
		assert(crt_native_net_lookup(c->net, crt_native_net_uri(server), &c->peer) == 0);
		for (unsigned j = 0; j < 20000 && crt_native_peer_status(c->peer); j++)
			drive(c->net, server);
		assert(crt_native_peer_status(c->peer) == 0);
	}
	/* More simultaneously runnable peers than the 32-peer progress budget,
	 * spanning both sides of the bitmap's 64-bit word boundary.
	 */
	for (unsigned i = 0; i < CLIENTS; i++) {
		uint64_t id;
		assert(crt_native_channel_call(crt_native_peer_channel(clients[i].peer), 17,
					       "hello", 5, done, &clients[i].stats, &id) == 0);
	}
	unsigned completed = 0;
	for (unsigned round = 0; round < 20000 && completed < CLIENTS; round++) {
		unsigned work;
		completed = 0;
		for (unsigned i = 0; i < CLIENTS; i++) {
			assert(crt_native_net_progress(clients[i].net, &work) == 0);
			completed += clients[i].stats.completed;
		}
		assert(crt_native_net_progress(server, &work) == 0);
	}
	assert(completed == CLIENTS && ss.received == CLIENTS && ss.failed == 0);
	for (unsigned i = 0; i < CLIENTS; i++) {
		assert(clients[i].stats.completed == 1);
		crt_native_peer_put(clients[i].peer);
		assert(crt_native_net_shutdown(clients[i].net) == 0);
	}
	for (unsigned round = 0; round < 128; round++) {
		for (unsigned i = 0; i < CLIENTS; i++)
			drive(clients[i].net, server);
	}
	for (unsigned i = 0; i < CLIENTS; i++)
		assert(crt_native_net_destroy(clients[i].net) == 0);
	assert(crt_native_net_destroy(server) == 0);
	free(clients);
}

int
main(int argc, char **argv)
{
	bool grouped = argc == 1;
	if (!grouped) {
		assert(argc == 2 && strcmp(argv[1], "--ungrouped") == 0);
		ops.group_create   = NULL;
		ops.group_progress = NULL;
		ops.group_destroy  = NULL;
	}
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
	/* Quiescent progress polls each context's group once, without polling
	 * every peer's endpoint queues on every pass. Maintenance is still live.
	 */
	for (unsigned i = 0; i < 32; i++)
		drive(a, b);
	extern unsigned crt_test_accepts, crt_test_peeks, crt_test_polls;
	unsigned        accepts_before = crt_test_accepts, peeks_before = crt_test_peeks;
	unsigned        polls_before = crt_test_polls;
	unsigned        before = endpoint_polls, groups_before = grouped_polls;
	for (unsigned i = 0; i < 1000; i++) {
		unsigned work;
		assert(crt_native_net_progress(a, &work) == 0);
		assert(crt_native_net_progress(b, &work) == 0);
	}
	assert(crt_test_accepts == accepts_before && crt_test_peeks == peeks_before);
	assert(crt_test_polls - polls_before < 1000);
	printf("Idle 1000 ticks/context (%s): group polls=%u endpoint polls=%u "
	       "TCP polls=%u accepts=%u peeks=%u\n",
	       grouped ? "grouped" : "standalone", grouped_polls - groups_before,
	       endpoint_polls - before, crt_test_polls - polls_before,
	       crt_test_accepts - accepts_before, crt_test_peeks - peeks_before);
	if (grouped) {
		assert(grouped_polls - groups_before == 2000);
		assert(endpoint_polls - before < 1000);
	}
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
	test_many_ready_peers();
	for (unsigned i = 0; i < endpoint_count; i++) {
		assert(endpoints[i]->closed && endpoints[i]->registered == 0);
		free(endpoints[i]);
	}
	puts("Native network: loopback bootstrap, full-duplex RPC, cached address lifetime, "
	     "worker release, grouped idle polling, 70-peer fairness and 128-client reclamation "
	     "passed");
	return 0;
}
