/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/* Test-only syscall counters: compile the transport with redirected symbols. */
#undef accept
#undef recv
#undef poll
#include <sys/socket.h>
#include <poll.h>

unsigned crt_test_accepts, crt_test_peeks, crt_test_polls;

int
crt_test_accept(int fd, struct sockaddr *address, socklen_t *length)
{
	crt_test_accepts++;
	return accept(fd, address, length);
}

ssize_t
crt_test_recv(int fd, void *buffer, size_t size, int flags)
{
	if (flags & MSG_PEEK)
		crt_test_peeks++;
	return recv(fd, buffer, size, flags);
}

int
crt_test_poll(struct pollfd *fds, nfds_t count, int timeout)
{
	crt_test_polls++;
	return poll(fds, count, timeout);
}
