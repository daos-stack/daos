/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define _POSIX_C_SOURCE 200809L
#include "net.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#define NET_HELLO (8 + 2 * CRT_NATIVE_HELLO_SIZE)
enum peer_state { CONNECTING, HELLO, PREPOST, READY_WAIT, READY, FAILED };
struct crt_native_peer {
	struct crt_native_net          *net;
	struct crt_native_peer         *next;
	struct crt_native_peer        **previous;
	struct crt_native_ep           *control_ep, *bulk_ep;
	struct crt_native_channel      *channel;
	struct crt_native_bulk_session *session;
	struct crt_native_limits        limits;
	struct sockaddr_in              address;
	char                            uri[64];
	int                             fd, error;
	short                           socket_events;
	enum peer_state                 state;
	uint8_t                         tx[NET_HELLO], rx[NET_HELLO], ready_tx, ready_rx;
	size_t                          sent, received;
	uint64_t                        deadline;
	void                           *data;
	bool                            outbound;
	bool                            addressed;
	bool                            resources_released;
	atomic_uint                     references;
	unsigned                        slot;
};
struct crt_native_net {
	const struct crt_native_ops    *ops;
	struct crt_native_config        config;
	struct crt_native_limits        limits;
	struct crt_native_net_callbacks callbacks;
	void                           *arg;
	struct crt_native_peer         *peers;
	unsigned                        count, max_peers, timeout_ms, port;
	int                             listener;
	struct pollfd                  *pollfds;
	struct crt_native_peer        **pollpeers;
	uint64_t                        next_socket_poll;
	struct crt_native_peer        **slots;
	atomic_uint_fast64_t           *runnable;
	unsigned                        words, ready_cursor;
	struct crt_native_group        *group;
	char                            uri[64];
	bool                            closing, progressing;
};

static uint64_t
now_ms(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0;
	return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

void
crt_native_peer_kick(struct crt_native_peer *p)
{
	if (p)
		atomic_fetch_or(&p->net->runnable[p->slot / 64], UINT64_C(1) << (p->slot % 64));
}

static void
wake_peer(void *arg)
{
	crt_native_peer_kick(arg);
}

static int
configure_socket(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0), one = 1;
	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
	    fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
		return errno;
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) != 0)
		return errno;
#ifdef SO_NOSIGPIPE
	if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) != 0)
		return errno;
#endif
	return 0;
}

void
crt_native_peer_fail(struct crt_native_peer *p, int error)
{
	if (p == NULL || error <= 0)
		return;
	if (p->state != FAILED) {
		p->state = FAILED;
		p->error = error;
		crt_native_peer_kick(p);
		if (p->fd >= 0) {
			close(p->fd);
			p->fd = -1;
		}
		if (p->channel)
			crt_native_channel_shutdown(p->channel, error);
		else if (p->control_ep)
			p->net->ops->close_begin(p->control_ep);
		if (p->session)
			crt_native_bulk_session_shutdown(p->session);
		else if (p->bulk_ep)
			p->net->ops->close_begin(p->bulk_ep);
		if (p->net->callbacks.failed)
			p->net->callbacks.failed(p->net->arg, p, error);
	}
}

static void
receive_request(void *arg, struct crt_native_rpc *rpc, struct crt_native_token token,
		uint32_t opcode, const void *payload, size_t size)
{
	struct crt_native_peer *p = arg;
	p->net->callbacks.request(p->net->arg, p, rpc, token, opcode, payload, size);
}
static int
send_ack(void *arg, uint64_t id)
{
	return crt_native_channel_queue_release(((struct crt_native_peer *)arg)->channel, id);
}

int
crt_native_net_create(const struct crt_native_ops *ops, const struct crt_native_config *config,
		      const struct crt_native_limits *limits, const char *ip, unsigned port,
		      unsigned max_peers, unsigned timeout_ms,
		      const struct crt_native_net_callbacks *cb, void *arg,
		      struct crt_native_net **out)
{
	if (out == NULL)
		return EINVAL;
	*out = NULL;
	if (ops == NULL || config == NULL || limits == NULL || cb == NULL || cb->request == NULL ||
	    ip == NULL || port > 65535 || max_peers == 0 || max_peers > 4096 || timeout_ms == 0 ||
	    ops->open == NULL || ops->address == NULL || ops->connect == NULL ||
	    ops->close == NULL || ops->close_begin == NULL || ops->quiesce == NULL ||
	    limits->receives < 2 || limits->receives > 8192 ||
	    config->depth < 4 * limits->receives || limits->frame_size < 64 ||
	    limits->frame_size > 1048576)
		return EINVAL;
	struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(port)};
	if (inet_pton(AF_INET, ip, &address.sin_addr) != 1)
		return EINVAL;
	struct crt_native_net *n = calloc(1, sizeof(*n));
	if (n == NULL)
		return ENOMEM;
	n->ops        = ops;
	n->config     = *config;
	n->limits     = *limits;
	n->callbacks  = *cb;
	n->arg        = arg;
	n->max_peers  = max_peers;
	n->timeout_ms = timeout_ms;
	n->words      = (max_peers + 63) / 64;
	n->slots      = calloc(max_peers, sizeof(*n->slots));
	n->runnable   = calloc(n->words, sizeof(*n->runnable));
	n->pollfds    = calloc(max_peers + 1, sizeof(*n->pollfds));
	n->pollpeers  = calloc(max_peers + 1, sizeof(*n->pollpeers));
	if (!n->pollfds || !n->pollpeers || !n->slots || !n->runnable) {
		free(n->slots);
		free(n->runnable);
		free(n->pollfds);
		free(n->pollpeers);
		free(n);
		return ENOMEM;
	}
	for (unsigned i = 0; i < n->words; i++)
		atomic_init(&n->runnable[i], 0);
	n->listener = socket(AF_INET, SOCK_STREAM, 0);
	int rc      = n->listener < 0 ? errno : configure_socket(n->listener);
	int one     = 1;
	if (!rc && setsockopt(n->listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)))
		rc = errno;
	if (rc == 0 && bind(n->listener, (void *)&address, sizeof(address)) != 0)
		rc = errno;
	if (rc == 0 && listen(n->listener, 64) != 0)
		rc = errno;
	socklen_t length = sizeof(address);
	if (rc == 0 && getsockname(n->listener, (void *)&address, &length) != 0)
		rc = errno;
	if (rc != 0) {
		if (n->listener >= 0)
			close(n->listener);
		free(n->slots);
		free(n->runnable);
		free(n->pollfds);
		free(n->pollpeers);
		free(n);
		return rc;
	}
	if (ops->group_create && ops->group_progress && ops->group_destroy) {
		rc = ops->group_create(config, 2 * max_peers, &n->group);
		if (rc) {
			/* Preserve partial group handles when cleanup needs a retry. */
			if (crt_native_net_destroy(n) != 0)
				*out = n;
			return rc;
		}
		n->config.group = n->group;
	}
	n->port = ntohs(address.sin_port);
	snprintf(n->uri, sizeof(n->uri), "doca+rc://%s:%u", ip, n->port);
	*out = n;
	return 0;
}

static int
add_peer(struct crt_native_net *n, int fd, const struct sockaddr_in *address, bool connecting,
	 struct crt_native_peer **out)
{
	if (n->count == n->max_peers)
		return ENOSPC;
	struct crt_native_peer *p = calloc(1, sizeof(*p));
	if (p == NULL)
		return ENOMEM;
	for (p->slot = 0; n->slots[p->slot]; p->slot++)
		;
	n->slots[p->slot] = p;
	p->net            = n;
	p->fd             = fd;
	p->address        = *address;
	p->state          = connecting ? CONNECTING : HELLO;
	p->deadline       = now_ms() + n->timeout_ms;
	p->next           = n->peers;
	p->previous       = &n->peers;
	if (p->next)
		p->next->previous = &p->next;
	atomic_init(&p->references, 0);
	n->peers = p;
	n->count++;
	n->next_socket_poll = 0;
	crt_native_peer_kick(p);
	struct crt_native_config config = n->config;
	config.wake                     = wake_peer;
	config.wake_arg                 = p;
	int rc                          = n->ops->open(&config, &p->control_ep);
	if (rc == 0)
		rc = n->ops->open(&config, &p->bulk_ep);
	struct crt_native_address control, bulk;
	if (rc == 0)
		rc = n->ops->address(p->control_ep, &control);
	if (rc == 0)
		rc = n->ops->address(p->bulk_ep, &bulk);
	memcpy(p->tx, "NRC2", 4);
	p->tx[4] = 1;
	p->tx[5] = CRT_NATIVE_WIRE_VERSION;
	p->tx[6] = n->port >> 8;
	p->tx[7] = n->port;
	if (rc == 0)
		rc = crt_native_hello_encode(&control, &n->limits, p->tx + 8);
	if (rc == 0)
		rc = crt_native_hello_encode(&bulk, &n->limits, p->tx + 8 + CRT_NATIVE_HELLO_SIZE);
	if (rc != 0)
		crt_native_peer_fail(p, rc);
	*out = p;
	return 0; /* retain failed endpoint handles for safe context teardown */
}

int
crt_native_net_lookup(struct crt_native_net *n, const char *uri, struct crt_native_peer **out)
{
	if (n == NULL || uri == NULL || out == NULL || n->closing)
		return EINVAL;
	for (struct crt_native_peer *p = n->peers; p != NULL; p = p->next)
		if (p->state != FAILED && strcmp(uri, p->uri) == 0) {
			p->addressed = true;
			atomic_fetch_add(&p->references, 1);
			*out = p;
			return 0;
		}
	if (n->count == n->max_peers)
		return ENOSPC;
	char ip[INET_ADDRSTRLEN];
	if (strncmp(uri, "doca+rc://", 10) != 0)
		return EINVAL;
	const char *host = uri + 10, *colon = strchr(host, ':');
	if (colon == NULL || colon == host || (size_t)(colon - host) >= sizeof(ip) ||
	    colon[1] < '0' || colon[1] > '9')
		return EINVAL;
	memcpy(ip, host, (size_t)(colon - host));
	ip[colon - host] = '\0';
	char *end;
	errno                     = 0;
	unsigned long parsed_port = strtoul(colon + 1, &end, 10);
	if (errno || *end || parsed_port == 0 || parsed_port > 65535)
		return EINVAL;
	unsigned           port    = parsed_port;
	struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(port)};
	if (inet_pton(AF_INET, ip, &address.sin_addr) != 1)
		return EINVAL;
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return errno;
	int  rc         = configure_socket(fd);
	bool connecting = false;
	if (rc == 0 && connect(fd, (void *)&address, sizeof(address)) != 0) {
		if (errno == EINPROGRESS)
			connecting = true;
		else
			rc = errno;
	}
	if (rc == 0)
		rc = add_peer(n, fd, &address, connecting, out);
	if (rc != 0) {
		close(fd);
		return rc;
	}
	snprintf((*out)->uri, sizeof((*out)->uri), "doca+rc://%s:%u", ip, port);
	(*out)->outbound  = true;
	(*out)->addressed = true;
	atomic_fetch_add(&(*out)->references, 1);
	return 0;
}

void
crt_native_peer_put(struct crt_native_peer *p)
{
	if (p)
		atomic_fetch_sub(&p->references, 1);
}

/* Keep the address shell while caches reference it, but retire every hardware
 * resource once DMA and asynchronous consumers have drained. All steps retry
 * safely after partial cleanup; the destroy callback must preserve busy data.
 */
static int
release_peer_resources(struct crt_native_peer *p)
{
	struct crt_native_net *n = p->net;
	int                    rc;
	if (p->resources_released)
		return 0;
	if (p->channel && (rc = n->ops->quiesce(p->control_ep)) != 0)
		return rc;
	if (p->session && (rc = crt_native_bulk_session_quiesce(p->session)) != 0)
		return rc;
	if (p->channel) {
		struct crt_native_rpc_stats stats;
		crt_native_channel_get_stats(p->channel, &stats);
		rc = crt_native_channel_destroy(p->channel);
		if (rc)
			return rc;
		if (n->config.stats)
			fprintf(stderr,
				"doca+rc RPC stats: early_retained=%" PRIu64
				" early_copies=%" PRIu64 " request_frames=%" PRIu64
				" reply_frames=%" PRIu64 " release_frames=%" PRIu64
				" release_ids=%" PRIu64 " piggyback_ids=%" PRIu64 "\n",
				stats.early_retained, stats.early_copies, stats.request_frames,
				stats.reply_frames, stats.release_frames, stats.release_ids,
				stats.piggyback_ids);
		p->channel = NULL;
	}
	if (n->callbacks.destroy && (rc = n->callbacks.destroy(n->arg, p)) != 0)
		return rc;
	if (p->session) {
		rc = crt_native_bulk_session_destroy(p->session);
		if (rc)
			return rc;
		p->session = NULL;
	}
	if (p->control_ep) {
		rc = n->ops->close(p->control_ep);
		if (rc)
			return rc;
		p->control_ep = NULL;
	}
	if (p->bulk_ep) {
		rc = n->ops->close(p->bulk_ep);
		if (rc)
			return rc;
		p->bulk_ep = NULL;
	}
	p->resources_released = true;
	return 0;
}

static void
remove_peer(struct crt_native_peer *p)
{
	struct crt_native_net *n = p->net;
	*p->previous             = p->next;
	if (p->next)
		p->next->previous = p->previous;
	n->count--;
	n->slots[p->slot] = NULL;
	atomic_fetch_and(&n->runnable[p->slot / 64], ~(UINT64_C(1) << (p->slot % 64)));
	free(p);
}

static int
exchange(struct crt_native_peer *p, const void *tx, void *rx, size_t length)
{
	ssize_t count;
	if (p->sent < length) {
		count = send(p->fd, (const uint8_t *)tx + p->sent, length - p->sent, MSG_NOSIGNAL);
		if (count > 0)
			p->sent += count;
		else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
			return errno;
	}
	if (p->received < length) {
		count = recv(p->fd, (uint8_t *)rx + p->received, length - p->received, 0);
		if (count == 0)
			return ECONNRESET;
		if (count > 0)
			p->received += count;
		else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
			return errno;
	}
	return p->sent == length && p->received == length ? 0 : EAGAIN;
}

static int
step(struct crt_native_peer *p)
{
	struct crt_native_net *n = p->net;
	if (p->state == FAILED) {
		if (p->resources_released)
			return 0;
		unsigned completed;
		if (p->channel) {
			crt_native_channel_shutdown(p->channel, p->error);
			crt_native_channel_progress(p->channel, &completed);
		}
		if (n->callbacks.progress)
			n->callbacks.progress(n->arg, p);
		release_peer_resources(p);
		return 0;
	}
	if (p->state != READY && now_ms() >= p->deadline)
		return ETIMEDOUT;
	if (p->state == CONNECTING) {
		int error = 0;
		if (!p->socket_events)
			return 0;
		p->socket_events = 0;
		socklen_t size   = sizeof(error);
		if (getsockopt(p->fd, SOL_SOCKET, SO_ERROR, &error, &size) != 0)
			return errno;
		if (error)
			return error;
		p->state = HELLO;
	}
	if (p->state == HELLO) {
		int rc = exchange(p, p->tx, p->rx, NET_HELLO);
		if (rc != 0)
			return rc == EAGAIN ? 0 : rc;
		if (memcmp(p->rx, "NRC2", 4) || p->rx[4] != 1 ||
		    p->rx[5] != CRT_NATIVE_WIRE_VERSION)
			return EPROTO;
		unsigned port = (unsigned)p->rx[6] << 8 | p->rx[7];
		if (port == 0)
			return EPROTO;
		if (p->outbound && port != ntohs(p->address.sin_port))
			return EPROTO;
		char ip[INET_ADDRSTRLEN];
		if (inet_ntop(AF_INET, &p->address.sin_addr, ip, sizeof(ip)) == NULL)
			return errno;
		snprintf(p->uri, sizeof(p->uri), "doca+rc://%s:%u", ip, port);
		struct crt_native_address control, bulk;
		struct crt_native_limits  a, b;
		rc = crt_native_hello_decode(p->rx + 8, &control, &a);
		if (rc == 0)
			rc = crt_native_hello_decode(p->rx + 8 + CRT_NATIVE_HELLO_SIZE, &bulk, &b);
		if (rc != 0 || a.frame_size != b.frame_size || a.receives != b.receives)
			return EPROTO;
		p->limits.frame_size =
		    a.frame_size < n->limits.frame_size ? a.frame_size : n->limits.frame_size;
		p->limits.receives =
		    a.receives < n->limits.receives ? a.receives : n->limits.receives;
		rc = n->ops->connect(p->control_ep, &control);
		if (rc == 0)
			rc = n->ops->connect(p->bulk_ep, &bulk);
		if (rc == 0)
			rc = crt_native_channel_create(n->ops, p->control_ep, &p->limits,
						       receive_request, p, &p->channel);
		if (rc == 0)
			rc = crt_native_bulk_session_create(n->ops, p->bulk_ep, p->limits.receives,
							    send_ack, p, &p->session);
		if (rc == 0)
			rc = crt_native_channel_set_release_handler(
			    p->channel, crt_native_bulk_session_ack, p->session);
		if (rc != 0)
			return rc;
		p->state = PREPOST;
	}
	if (p->state == PREPOST) {
		int rc = crt_native_channel_start(p->channel);
		if (rc != 0)
			return rc == EAGAIN ? 0 : rc;
		if (n->callbacks.ready && (rc = n->callbacks.ready(n->arg, p)) != 0)
			return rc;
		p->state = READY_WAIT;
		p->sent = p->received = 0;
		p->ready_tx           = 1;
	}
	if (p->state == READY_WAIT) {
		int rc = exchange(p, &p->ready_tx, &p->ready_rx, 1);
		if (rc != 0)
			return rc == EAGAIN ? 0 : rc;
		if (p->ready_rx != 1)
			return EPROTO;
		p->state         = READY;
		p->socket_events = 0;
		return 0;
	}
	if (p->state == READY) {
		/* Consume arrived replies before observing bootstrap-socket EOF.
		 * Shutdown may race the peer's last accepted RDMA SEND.
		 */
		unsigned completed;
		int      rc = crt_native_channel_progress(p->channel, &completed);
		if (rc != 0 && rc != EAGAIN)
			return rc;
		if (p->socket_events) {
			p->socket_events = 0;
			uint8_t extra;
			ssize_t count = recv(p->fd, &extra, 1, MSG_PEEK);
			if (count == 0)
				return ECONNRESET;
			if (count > 0)
				return EPROTO;
			if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
				return errno;
		}
		if (n->callbacks.progress)
			rc = n->callbacks.progress(n->arg, p);
		if (rc == 0)
			rc = crt_native_bulk_session_progress(p->session);
		return rc == EAGAIN ? 0 : rc;
	}
	return 0;
}

/* TCP is bootstrap/liveness only. Batch readiness checks at most once per
 * millisecond; RDMA progress never probes each idle socket on every visit.
 */
static int
progress_sockets(struct crt_native_net *n, unsigned *work)
{
	uint64_t now = now_ms();
	if (now < n->next_socket_poll)
		return 0;
	n->next_socket_poll = now + 1;
	unsigned count      = 1;
	n->pollfds[0]       = (struct pollfd){.fd = n->listener, .events = POLLIN};
	for (struct crt_native_peer *p = n->peers; p; p = p->next) {
		if (!p->resources_released)
			crt_native_peer_kick(p);
		if (p->fd < 0)
			continue;
		n->pollpeers[count] = p;
		n->pollfds[count++] = (struct pollfd){
		    .fd = p->fd, .events = p->state == CONNECTING ? POLLOUT : POLLIN};
	}
	int rc = poll(n->pollfds, count, 0);
	if (rc < 0)
		return errno == EINTR ? 0 : errno;
	for (unsigned i = 1; i < count; i++)
		n->pollpeers[i]->socket_events |= n->pollfds[i].revents;
	if (rc == 0 || !(n->pollfds[0].revents & POLLIN))
		return 0;
	for (unsigned budget = 0; budget < 16 && !n->closing; budget++) {
		struct sockaddr_in address;
		socklen_t          size = sizeof(address);
		int                fd   = accept(n->listener, (void *)&address, &size);
		if (fd < 0)
			break;
		struct crt_native_peer *p;
		if (configure_socket(fd) != 0 || add_peer(n, fd, &address, false, &p) != 0)
			close(fd);
		else
			(*work)++;
	}
	return 0;
}

int
crt_native_net_progress(struct crt_native_net *n, unsigned *work)
{
	if (n == NULL || work == NULL || n->progressing)
		return EINVAL;
	*work          = 0;
	n->progressing = true;
	int socket_rc  = progress_sockets(n, work);
	if (socket_rc) {
		n->progressing = false;
		return socket_rc;
	}
	if (n->group) {
		int rc = n->ops->group_progress(n->group);
		if (rc)
			for (struct crt_native_peer *p = n->peers; p; p = p->next)
				crt_native_peer_fail(p, rc);
	} else {
		/* Backends without grouped polling retain the portable polling path. */
		for (struct crt_native_peer *p = n->peers; p; p = p->next)
			if (!p->resources_released)
				crt_native_peer_kick(p);
	}
	uint64_t pending[64]; /* max_peers <= 4096 */
	for (unsigned i = 0; i < n->words; i++)
		pending[i] = atomic_exchange(&n->runnable[i], 0);
	unsigned budget = 32, start = n->ready_cursor;
	for (unsigned pass = 0; pass < 2 && budget; pass++) {
		unsigned begin = pass ? 0 : start;
		unsigned end   = pass ? start : n->max_peers;
		for (unsigned word = begin / 64; word * 64 < end && budget; word++) {
			uint64_t bits = pending[word];
			if (word == begin / 64)
				bits &= UINT64_MAX << (begin % 64);
			if (end / 64 == word)
				bits &= (UINT64_C(1) << (end % 64)) - 1;
			while (bits && budget) {
				unsigned bit  = (unsigned)__builtin_ctzll(bits);
				unsigned slot = word * 64 + bit;
				uint64_t mask = UINT64_C(1) << bit;
				bits &= ~mask;
				pending[word] &= ~mask;
				struct crt_native_peer *p = n->slots[slot];
				if (!p)
					continue;
				n->ready_cursor = (slot + 1) % n->max_peers;
				int rc          = step(p);
				if (rc)
					crt_native_peer_fail(p, rc);
				if (p->resources_released && !p->addressed &&
				    atomic_load(&p->references) == 0)
					remove_peer(p);
				else if (p->state != READY && !p->resources_released)
					crt_native_peer_kick(p);
				(*work)++;
				budget--;
			}
		}
	}
	for (unsigned i = 0; i < n->words; i++)
		if (pending[i])
			atomic_fetch_or(&n->runnable[i], pending[i]);
	n->progressing = false;
	return 0;
}

int
crt_native_net_shutdown(struct crt_native_net *n)
{
	if (n == NULL || n->progressing)
		return EINVAL;
	n->closing = true;
	if (n->listener >= 0) {
		close(n->listener);
		n->listener = -1;
	}
	for (struct crt_native_peer *p = n->peers; p != NULL; p = p->next)
		crt_native_peer_fail(p, ECANCELED);
	return 0;
}

int
crt_native_net_destroy(struct crt_native_net *n)
{
	if (n == NULL || n->progressing)
		return EINVAL;
	crt_native_net_shutdown(n);
	while (n->peers) {
		struct crt_native_peer *p  = n->peers;
		int                     rc = release_peer_resources(p);
		if (rc)
			return rc;
		if (atomic_load(&p->references))
			return EBUSY;
		remove_peer(p);
	}
	if (n->group) {
		int rc = n->ops->group_destroy(n->group);
		if (rc)
			return rc;
	}
	free(n->slots);
	free(n->runnable);
	free(n->pollfds);
	free(n->pollpeers);
	free(n);
	return 0;
}

const char *
crt_native_net_uri(const struct crt_native_net *n)
{
	return n->uri;
}
int
crt_native_peer_status(const struct crt_native_peer *p)
{
	return p->error ? p->error : p->state == READY ? 0 : EINPROGRESS;
}
const char *
crt_native_peer_uri(const struct crt_native_peer *p)
{
	return p->uri;
}
struct crt_native_channel *
crt_native_peer_channel(struct crt_native_peer *p)
{
	return p->channel;
}
struct crt_native_bulk_session *
crt_native_peer_session(struct crt_native_peer *p)
{
	return p->session;
}
struct crt_native_ep *
crt_native_peer_bulk_endpoint(struct crt_native_peer *p)
{
	return p->bulk_ep;
}
unsigned
crt_native_peer_capacity(const struct crt_native_peer *p)
{
	return p->limits.receives;
}
void *
crt_native_peer_data(const struct crt_native_peer *p)
{
	return p->data;
}
void
crt_native_peer_set_data(struct crt_native_peer *p, void *data)
{
	p->data = data;
}
