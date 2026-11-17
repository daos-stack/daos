/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include "bootstrap.h"
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

static void
put32(uint8_t *p, uint32_t n)
{
	p[0] = n >> 24;
	p[1] = n >> 16;
	p[2] = n >> 8;
	p[3] = n;
}

static uint32_t
get32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static bool
valid(const struct crt_native_address *a, const struct crt_native_limits *l)
{
	static const uint8_t zero_gid[16];

	return a != NULL && l != NULL && a->qpn != 0 && a->qpn <= 0xffffff && a->psn <= 0xffffff &&
	       a->mtu >= 1 && a->mtu <= 5 &&
	       (a->global ? memcmp(a->gid, zero_gid, 16) != 0 : a->lid != 0) &&
	       l->frame_size >= 64 && l->frame_size <= 1048576 && l->receives >= 2 &&
	       l->receives <= 32768;
}

int
crt_native_hello_encode(const struct crt_native_address *a, const struct crt_native_limits *l,
			uint8_t out[CRT_NATIVE_HELLO_SIZE])
{
	if (out == NULL || !valid(a, l))
		return EINVAL;
	memset(out, 0, CRT_NATIVE_HELLO_SIZE);
	memcpy(out, "CRTB", 4);
	out[4] = 2;
	out[5] = a->global;
	out[6] = a->mtu;
	put32(out + 8, a->qpn);
	put32(out + 12, a->psn);
	out[16] = a->lid >> 8;
	out[17] = a->lid;
	out[18] = a->read_out ? a->read_out : 1;
	out[19] = a->read_in ? a->read_in : 1;
	memcpy(out + 20, a->gid, 16);
	put32(out + 36, l->frame_size);
	put32(out + 40, l->receives);
	return 0;
}

int
crt_native_hello_decode(const uint8_t in[CRT_NATIVE_HELLO_SIZE], struct crt_native_address *a,
			struct crt_native_limits *l)
{
	struct crt_native_address address;
	struct crt_native_limits  limits;

	if (in == NULL || a == NULL || l == NULL)
		return EINVAL;
	if (memcmp(in, "CRTB", 4) != 0 || in[4] != 2 || in[5] > 1 || in[7] != 0 || in[18] == 0 ||
	    in[19] == 0 || get32(in + 44) != 0)
		return EPROTO;
	address = (struct crt_native_address){
	    .read_out = in[18],
	    .read_in  = in[19],
	    .qpn      = get32(in + 8),
	    .psn      = get32(in + 12),
	    .mtu      = in[6],
	    .global   = in[5] != 0,
	    .lid      = (uint16_t)((uint16_t)in[16] << 8 | in[17]),
	};
	memcpy(address.gid, in + 20, 16);
	limits = (struct crt_native_limits){get32(in + 36), get32(in + 40)};
	if (!valid(&address, &limits))
		return EPROTO;
	*a = address;
	*l = limits;
	return 0;
}

static int64_t
milliseconds(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return -1;
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int
crt_native_bootstrap(const struct crt_native_ops *ops, struct crt_native_ep *ep, int fd,
		     const struct crt_native_limits *local, unsigned timeout_ms,
		     struct crt_native_limits *negotiated)
{
	uint8_t                   tx[CRT_NATIVE_HELLO_SIZE], rx[CRT_NATIVE_HELLO_SIZE];
	struct crt_native_address address;
	struct crt_native_limits  remote;
	size_t                    sent = 0, received = 0;
	int64_t                   deadline, now;
	int                       rc, send_flags = MSG_DONTWAIT;

	if (ops == NULL || ops->address == NULL || ops->connect == NULL || ep == NULL || fd < 0 ||
	    negotiated == NULL || timeout_ms == 0 || timeout_ms > INT_MAX)
		return EINVAL;
#ifdef MSG_NOSIGNAL
	send_flags |= MSG_NOSIGNAL;
#else
	int no_sigpipe = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) != 0)
		return errno;
#endif
	rc = ops->address(ep, &address);
	if (rc != 0)
		return rc;
	rc = crt_native_hello_encode(&address, local, tx);
	if (rc != 0)
		return rc;
	deadline = milliseconds();
	if (deadline < 0)
		return EIO;
	deadline += timeout_ms;
	while (sent != sizeof(tx) || received != sizeof(rx)) {
		struct pollfd pfd = {.fd = fd};
		ssize_t       n;

		if (sent != sizeof(tx))
			pfd.events |= POLLOUT;
		if (received != sizeof(rx))
			pfd.events |= POLLIN;
		now = milliseconds();
		if (now < 0)
			return EIO;
		if (now >= deadline)
			return ETIMEDOUT;
		rc = poll(&pfd, 1, (int)(deadline - now));
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			return errno;
		}
		if (rc == 0)
			return ETIMEDOUT;
		if (pfd.revents & (POLLERR | POLLNVAL))
			return ECONNRESET;
		if (sent != sizeof(tx) && (pfd.revents & POLLOUT)) {
			n = send(fd, tx + sent, sizeof(tx) - sent, send_flags);
			if (n > 0)
				sent += (size_t)n;
			else if (n == 0)
				return ECONNRESET;
			else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
				return errno;
		}
		if (received != sizeof(rx) && (pfd.revents & (POLLIN | POLLHUP))) {
			n = recv(fd, rx + received, sizeof(rx) - received, MSG_DONTWAIT);
			if (n > 0)
				received += (size_t)n;
			else if (n == 0)
				return ECONNRESET;
			else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
				return errno;
		}
		if (pfd.revents & POLLHUP && received != sizeof(rx))
			return ECONNRESET;
	}
	rc = crt_native_hello_decode(rx, &address, &remote);
	if (rc != 0)
		return rc;
	rc = ops->connect(ep, &address);
	if (rc != 0)
		return rc;
	*negotiated = (struct crt_native_limits){
	    .frame_size =
		local->frame_size < remote.frame_size ? local->frame_size : remote.frame_size,
	    .receives = local->receives < remote.receives ? local->receives : remote.receives,
	};
	return 0;
}
