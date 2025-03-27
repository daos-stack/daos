/*
 * Copyright (c) 2021-2022 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under the BSD license below:
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>

double when(void)
{
	struct timeval tv;
	static struct timeval tv0;
	static int first = 1;
	int err;

	err = gettimeofday(&tv, NULL);
	if (err) {
		perror("gettimeofday");
		return 0;
	}

	if (first) {
		tv0 = tv;
		first = 0;
	}
	return (double)(tv.tv_sec - tv0.tv_sec) * 1.0e6 +
	       (double)(tv.tv_usec - tv0.tv_usec);
}

int connect_tcp(char *host, int port)
{
	struct sockaddr_in sin;
	struct hostent *addr;
	int sockfd, newsockfd;
	socklen_t clen;

	memset(&sin, 0, sizeof(sin));

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		exit(-1);
	}

	if (host) {
		if (atoi(host) > 0) {
			sin.sin_family = AF_INET;
			sin.sin_addr.s_addr = inet_addr(host);
		} else {
			if ((addr = gethostbyname(host)) == NULL){
				printf("invalid hostname '%s'\n", host);
				exit(-1);
			}
			sin.sin_family = addr->h_addrtype;
			memcpy(&sin.sin_addr.s_addr, addr->h_addr, addr->h_length);
		}
		sin.sin_port = htons(port);
		if(connect(sockfd, (struct sockaddr *) &sin, sizeof(sin)) < 0){
			perror("connect");
			exit(-1);
		}
		return sockfd;
	} else {
		int one = 1;
		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int))) {
			perror("setsockopt");
			exit(-1);
		}
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = htonl(INADDR_ANY);
		sin.sin_port = htons(port);
		if (bind(sockfd, (struct sockaddr *) &sin, sizeof(sin)) < 0){
			perror("bind");
			exit(-1);
		}

		listen(sockfd, 5);
		clen = sizeof(sin);
		newsockfd = accept(sockfd, (struct sockaddr *) &sin, &clen);
		if(newsockfd < 0) {
			perror("accept");
			exit(-1);
		}

		close(sockfd);
		return newsockfd;
	}
}

void sync_tcp(int sockfd)
{
	int dummy1, dummy2;

	(void)! write(sockfd, &dummy1, sizeof dummy1);
	(void)! read(sockfd, &dummy2, sizeof dummy2);
}

int exchange_info(int sockfd, size_t size, void *me, void *peer)
{
	if (write(sockfd, me, size) != size) {
		fprintf(stderr, "Failed to send local info\n");
		return -1;
	}

	if (recv(sockfd, peer, size, MSG_WAITALL) != size) {
		fprintf(stderr, "Failed to read peer info\n");
		return -1;
	}

	return 0;
}

