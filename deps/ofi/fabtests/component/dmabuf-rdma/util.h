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

#ifndef _DMABUF_RDMA_TESTS_UTIL_H_
#define _DMABUF_RDMA_TESTS_UTIL_H_

#define EXIT_ON_ERROR(stmt) \
	do { \
		int err = (stmt); \
		if (err) { \
			perror(#stmt); \
			printf("%s returned error %d\n", #stmt, (err)); \
			exit(err); \
		} \
	} while (0)

#define CHECK_ERROR(stmt) \
	do { \
		int err = (stmt); \
		if (err) { \
			perror(#stmt); \
			printf("%s returned error %d\n", #stmt, (err)); \
			goto err_out; \
		} \
	} while (0)

#define EXIT_ON_NEG_ERROR(stmt) \
	do { \
		int err = (stmt); \
		if (err < 0) { \
			perror(#stmt); \
			printf("%s returned error %d\n", #stmt, (err)); \
			exit(err); \
		} \
	} while (0)

#define CHECK_NEG_ERROR(stmt) \
	do { \
		int err = (stmt); \
		if (err < 0) { \
			perror(#stmt); \
			printf("%s returned error %d\n", #stmt, (err)); \
			goto err_out; \
		} \
	} while (0)

#define EXIT_ON_NULL(stmt) \
	do { \
		if (!(stmt)) { \
			perror(#stmt); \
			printf("%s returned NULL\n", #stmt); \
			exit(-1); \
		} \
	} while (0)

#define CHECK_NULL(stmt) \
	do { \
		if (!(stmt)) { \
			perror(#stmt); \
			printf("%s returned NULL\n", #stmt); \
			goto err_out; \
		} \
	} while (0)

/*
 * Get the wall time since the first call (in microsecodns).
 * Can be used for performance calculation.
 */
double	when(void);

/*
 * Set up tcp connection for OOB communication, the side with NULL host is the
 * server. Return socked fd.
 */
int	connect_tcp(char *host, int port);

/*
 * Perform a bi-directional send/recv over the socket. Can be used
 * as a barrier.
 */
void	sync_tcp(int sockfd);

/*
 * Exchange information over the socket. Can be used as a barrier.
 */
int	exchange_info(int sockfd, size_t size, void *me, void *peer);

#endif /* _DMABUF_RDMA_TESTS_UTIL_H_ */

