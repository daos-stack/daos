/*
 * Copyright (c) 2021-2022 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHWARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. const NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER const AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS const THE
 * SOFTWARE.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <netdb.h>
#include <time.h>
#include <poll.h>
#if HAVE_EPOLL == 1
#include <sys/epoll.h>
#endif
#include <errno.h>

#include <shared.h>


static int *fds;
static int connections = 1000;
uint64_t starttime, endtime;

static struct pollfd *poll_set;
#if HAVE_EPOLL == 1
static struct epoll_event *ep_events;
static int epfd = -1;
#endif


static void show_header(void)
{
	printf("connections: %d, iterations: %d\n", connections, opts.iterations);
	if (connections > FD_SETSIZE)
		printf("* select tests limited to %d sockets\n", FD_SETSIZE);

	printf("%-20s : usec/call\n", "test");
	printf("%-20s : ---------\n", "----");
}

static void show_result(const char *test, uint64_t starttime, uint64_t endtime)
{
	printf("%-20s : %.2f\n", test,
	       (float) (endtime - starttime) / opts.iterations);
}

static int start_server(void)
{
	struct addrinfo *ai = NULL;
	int optval = 1;
	int ret;

	ret = getaddrinfo(opts.src_addr, opts.src_port, NULL, &ai);
	if (ret) {
		FT_PRINTERR("getaddrinfo", ret);
		return -ret;
	}

	listen_sock = socket(ai->ai_family, SOCK_STREAM, 0);
	if (listen_sock < 0) {
		FT_PRINTERR("socket", -errno);
		goto free;
	}

	ret = setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
			 (char *) &optval, sizeof(optval));
	if (ret) {
		FT_PRINTERR("setsockopt", -errno);
		goto close;
	}

	ret = bind(listen_sock, ai->ai_addr, ai->ai_addrlen);
	if (ret) {
		FT_PRINTERR("bind", -errno);
		goto close;
	}

	ret = listen(listen_sock, 511);
	if (ret) {
		FT_PRINTERR("listen", -errno);
		goto close;
	}

	freeaddrinfo(ai);
	return 0;

close:
	close(listen_sock);
free:
	freeaddrinfo(ai);
	return -errno;
}

static int server_connect(void)
{
	int i, ret;

	ret = start_server();
	if (ret)
		return ret;

	for (i = 0; i < connections; i++) {
		fds[i] = accept(listen_sock, NULL, NULL);
		if (fds[i] < 0) {
			FT_PRINTERR("accept", -errno);
			ret = -errno;
			goto close;
		}
	}
	close(listen_sock);
	return 0;

close:
	while (i--) {
		close(fds[i]);
		fds[i] = -1;
	}
	close(listen_sock);
	return ret;
}

static int run_client(void)
{
	struct addrinfo *res;
	ssize_t bytes;
	int i, ret;
	char c;

	ret = getaddrinfo(opts.dst_addr, opts.dst_port, NULL, &res);
	if (ret) {
		FT_PRINTERR("getaddrinfo", ret);
		return -ret;
	}

	for (i = 0; i < connections; i++) {
		fds[i] = socket(res->ai_family, SOCK_STREAM, 0);
		if (fds[i] < 0) {
			FT_PRINTERR("socket", -errno);
			goto close;
		}

		ret = connect(fds[i], res->ai_addr, res->ai_addrlen);
		if (ret) {
			FT_PRINTERR("connect", -errno);
			ret = -errno;
			goto close;
		}
	}

	/* wait for server to finish */
	bytes = recv(fds[0], &c, 1, 0);
	if (bytes < 0)
		FT_PRINTERR("recv", -errno);
	freeaddrinfo(res);
	return 0;

close:
	while (i--) {
		close(fds[i]);
		fds[i] = -1;
	}
	freeaddrinfo(res);
	return ret;
}

static int
time_select(const char *test, fd_set *readfds, fd_set *writefds, int stride)
{
	int i, j, ret, max_sock = 0;
	struct timeval timeout = {0};

	if (readfds)
		FD_ZERO(readfds);
	if (writefds)
		FD_ZERO(writefds);

	starttime = ft_gettime_us();
	for (i = 0; i < opts.iterations; i++) {
		for (j = 0; j < connections && fds[j] < FD_SETSIZE; j++) {
			if (readfds && ((j % stride) == 0))
				FD_SET(fds[j], readfds);
			if (writefds && ((j % stride) == 0))
				FD_SET(fds[j], writefds);

			if ((fds[j] > max_sock) && ((j % stride) == 0))
				max_sock = fds[j];
		}
		ret = select(max_sock + 1, readfds, writefds, NULL, &timeout);
		if (ret < 0) {
			FT_PRINTERR("select", -errno);
			return -errno;
		}
	}
	endtime = ft_gettime_us();
	show_result(test, starttime, endtime);
	return 0;
}

static int test_select(void)
{
	fd_set readfds, writefds;
	int ret;

	ret = time_select("select(read)", &readfds, NULL, 1);
	if (ret)
		return ret;

	ret = time_select("select(write)", NULL, &writefds, 1);
	if (ret)
		return ret;

	ret = time_select("select(rd/wr)", &readfds, &writefds, 1);
	if (ret)
		return ret;

	ret = time_select("select(1/2 rd/wr)", &readfds, &writefds, 2);
	if (ret)
		return ret;

	ret = time_select("select(1/4 rd/wr)", &readfds, &writefds, 4);
	if (ret)
		return ret;

	ret = time_select("select(1/100 rd/wr)", &readfds, &writefds, 100);
	if (ret)
		return ret;

	return 0;
}

static int time_poll(const char *test, short events, int stride)
{
	int i, ret = 0;

	poll_set = calloc(connections, sizeof(*poll_set));
	if (!poll_set)
		return -FI_ENOMEM;

	for (i = 0; i < connections; i++) {
		if ((i % stride) == 0) {
			poll_set[i].fd = fds[i];
			poll_set[i].events = events;
		} else {
			poll_set[i].fd = -fds[i];
		}
	}

	starttime = ft_gettime_us();
	for (i = 0; i < opts.iterations; i++) {
		ret = poll(poll_set, connections, 0);
		if (ret < 0) {
			FT_PRINTERR("poll", -errno);
			ret = -errno;
			goto out;
		}
	}
	endtime = ft_gettime_us();
	show_result(test, starttime, endtime);

out:
	free(poll_set);
	return ret < 0 ? ret : 0;
}

static int test_poll(void)
{
	int ret;

	ret = time_poll("poll(read)", POLLIN, 1);
	if (ret)
		return ret;

	ret = time_poll("poll(write)", POLLOUT, 1);
	if (ret)
		return ret;

	ret = time_poll("poll(rd/wr)", POLLIN | POLLOUT, 1);
	if (ret)
		return ret;

	ret = time_poll("poll(1/2 rd/wr)", POLLIN | POLLOUT, 2);
	if (ret)
		return ret;

	ret = time_poll("poll(1/4 rd/wr)", POLLIN | POLLOUT, 4);
	if (ret)
		return ret;

	ret = time_poll("poll(1/100 rd/wr)", POLLIN | POLLOUT, 100);
	if (ret)
		return ret;

	return 0;
}

#if HAVE_EPOLL == 1
static int init_epoll(uint32_t events, int stride)
{
	struct epoll_event event;
	int i, ret;

	epfd = epoll_create1(0);
	if (epfd < 0) {
		FT_PRINTERR("epoll_create1", -errno);
		return -errno;
	}

	ep_events = calloc(connections, sizeof(*ep_events));
	if (!ep_events) {
		ret = -errno;
		goto close;
	}

	for (i = 0; i < connections; i++) {
		if (i % stride)
			continue;

		event.events = events;
		ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fds[i], &event);
		if (ret) {
			FT_PRINTERR("epoll_ctl", -errno);
			ret = -errno;
			goto free;
		}
	}

	return 0;

free:
	free(ep_events);
close:
	close(epfd);
	return ret;
}

static int time_epoll(const char *test, uint32_t events, int stride)
{
	int i, ret;

	ret = init_epoll(events, stride);
	if (ret)
		return ret;

	starttime = ft_gettime_us();
	for (i = 0; i < opts.iterations; i++) {
		ret = epoll_wait(epfd, ep_events, connections, 0);
		if (ret < 0) {
			FT_PRINTERR("epoll_wait", -errno);
			ret = -errno;
			goto out;
		}
	}
	endtime = ft_gettime_us();
	show_result(test, starttime, endtime);

out:
	free(ep_events);
	close(epfd);
	return ret < 0 ? ret : 0;
}

static int test_epoll(void)
{
	int ret;

	ret = time_epoll("epoll(read)", EPOLLIN, 1);
	if (ret)
		return ret;

	ret = time_epoll("epoll(write)", EPOLLOUT, 1);
	if (ret)
		return ret;

	ret = time_epoll("epoll(rd/wr)", EPOLLIN | EPOLLOUT, 1);
	if (ret)
		return ret;

	ret = time_epoll("epoll(1/2 rd/wr)", EPOLLIN | EPOLLOUT, 2);
	if (ret)
		return ret;

	ret = time_epoll("epoll(1/4 rd/wr)", EPOLLIN | EPOLLOUT, 4);
	if (ret)
		return ret;

	ret = time_epoll("epoll(1/100 rd/wr)", EPOLLIN | EPOLLOUT, 100);
	if (ret)
		return ret;

	return 0;
}
#else
static int test_epoll()
{
    return 0;
}
#endif

static void close_conns(void)
{
	int i;

	for (i = 0; i < connections; i++) {
		if (fds[i] < 0)
			continue;
		shutdown(fds[i], SHUT_RDWR);
		close(fds[i]);
	}
	free(fds);
}

static int run_server(void)
{
	char c = 'a';
	int ret;

	show_header();

	ret = server_connect();
	if (ret)
		return ret;

	ret = test_select();
	if (ret)
		return ret;

	ret = test_poll();
	if (ret)
		return ret;

	ret = test_epoll();
	if (ret)
		return ret;

	ret = send(fds[0], &c, 1, 0);
	if (ret < 0)
		return -errno;
	return 0;
}

int main(int argc, char **argv)
{
	extern char *optarg;
	int c, ret;

	opts.iterations = 100;
	opts.src_port = default_port;
	opts.dst_port = default_port;

	while ((c = getopt(argc, argv, "n:" ADDR_OPTS)) != -1) {
		switch (c) {
		case 'n':
			connections = atoi(optarg);
			break;
		default:
			ft_parse_addr_opts(c, optarg, &opts);
			break;
		}
	}

	if (optind < argc)
		opts.dst_addr = argv[optind];

	fds = calloc(connections, sizeof(*fds));
	if (!fds)
		return -FI_ENOMEM;

	ret = opts.dst_addr ? run_client() : run_server();

	close_conns();
	return ret;
}
