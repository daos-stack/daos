/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "../bootstrap.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct crt_native_ep {
	unsigned connections;
};
static int
address(struct crt_native_ep *ep, struct crt_native_address *a)
{
	(void)ep;
	*a = (struct crt_native_address){.qpn = 11, .psn = 0x123456, .lid = 7, .mtu = 3};
	return 0;
}
static int
connect_peer(struct crt_native_ep *ep, const struct crt_native_address *a)
{
	assert(a->qpn == 11 && a->lid == 7 && a->psn == 0x123456);
	ep->connections++;
	return 0;
}
static const struct crt_native_ops ops = {.address = address, .connect = connect_peer};
struct peer {
	int  fd;
	bool malformed;
};
static void *
fragmented_peer(void *arg)
{
	struct peer              *p = arg;
	uint8_t                   frame[CRT_NATIVE_HELLO_SIZE];
	struct crt_native_address a;
	struct crt_native_limits  l  = {.frame_size = 1024, .receives = 8};
	struct crt_native_ep      ep = {0};
	address(&ep, &a);
	assert(crt_native_hello_encode(&a, &l, frame) == 0);
	if (p->malformed)
		frame[4] = 9;
	for (unsigned i = 0; i < sizeof(frame); i++)
		assert(write(p->fd, frame + i, 1) == 1);
	for (unsigned i = 0; i < sizeof(frame); i++)
		assert(read(p->fd, frame + i, 1) == 1);
	return NULL;
}
int
main(void)
{
	struct crt_native_ep      ep = {0};
	struct crt_native_address a, decoded;
	struct crt_native_limits  l = {.frame_size = 4096, .receives = 16}, limits;
	uint8_t                   frame[CRT_NATIVE_HELLO_SIZE];
	int                       fds[2];
	pthread_t                 thread;

	address(&ep, &a);
	assert(crt_native_hello_encode(&a, &l, frame) == 0);
	assert(frame[12] == 0 && frame[13] == 0x12 && frame[14] == 0x34 && frame[15] == 0x56);
	assert(crt_native_hello_decode(frame, &decoded, &limits) == 0);
	frame[44] = 1;
	assert(crt_native_hello_decode(frame, &decoded, &limits) == EPROTO);
	for (unsigned malformed = 0; malformed < 2; malformed++) {
		struct peer p;
		assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
		p = (struct peer){fds[1], malformed != 0};
		assert(pthread_create(&thread, NULL, fragmented_peer, &p) == 0);
		int rc = crt_native_bootstrap(&ops, &ep, fds[0], &l, 1000, &limits);
		assert(rc == (malformed ? EPROTO : 0));
		if (!malformed)
			assert(limits.frame_size == 1024 && limits.receives == 8);
		assert(pthread_join(thread, NULL) == 0);
		close(fds[0]);
		close(fds[1]);
	}
	assert(ep.connections == 1);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
	assert(crt_native_bootstrap(&ops, &ep, fds[0], &l, 5, &limits) == ETIMEDOUT);
	close(fds[1]);
	int rc = crt_native_bootstrap(&ops, &ep, fds[0], &l, 100, &limits);
	assert(rc == ECONNRESET || rc == EPIPE);
	close(fds[0]);
	puts("bootstrap: fragmented exchange, negotiation, bad version, timeout and disconnect "
	     "passed");
	return 0;
}
