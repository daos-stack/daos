/*
 * (C) Copyright 2018 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/*
 * Unit tests for the drpc module
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <stdlib.h>
#include <gurt/errno.h>
#include <errno.h>
#include <daos/drpc.h>

/* None of these tests depend on a real socket existing */
static char *TEST_SOCK_ADDR = "/good/socket.sock";

/* Test factory and cleanup methods for convenience */

static struct drpc*
new_drpc_with_fd(int fd)
{
	struct drpc *ctx = calloc(1, sizeof(struct drpc));

	ctx->comm = calloc(1, sizeof(struct unixcomm));
	ctx->comm->fd = fd;

	return ctx;
}

static void
free_drpc(struct drpc *ctx)
{
	free(ctx->comm);
	free(ctx);
}

static Drpc__Call*
new_drpc_call()
{
	Drpc__Call *call = calloc(1, sizeof(Drpc__Call));

	drpc__call__init(call);
	call->module = 1;
	call->method = 2;

	return call;
}

/*
 * Mocks
 */

static int socket_return; /* value to be returned by socket() */
static int socket_family; /* saved input */
static int socket_type; /* saved input */
static int socket_protocol; /* saved input */
int
socket(int family, int type, int protocol)
{
	socket_family = family;
	socket_type = type;
	socket_protocol = protocol;
	return socket_return;
}

static int connect_return; /* value to be returned by connect() */
static int connect_sockfd; /* saved input */
static const struct sockaddr *connect_addr_ptr; /* for nullcheck */
static struct sockaddr_un connect_addr; /* saved copy of input value */
static socklen_t connect_addrlen; /* saved input */
int
connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	connect_sockfd = sockfd;
	connect_addr_ptr = addr;
	if (addr != NULL) {
		memcpy(&connect_addr, (struct sockaddr_un *)addr,
				sizeof(struct sockaddr_un));
	}
	connect_addrlen = addrlen;
	return connect_return;
}

static int close_return; /* value to be returned by close() */
static int close_fd; /* saved input */
int
close(int fd)
{
	close_fd = fd;
	return close_return;
}

static ssize_t sendmsg_return; /* to be returned by sendmsg() */
static int sendmsg_sockfd; /* saved input */
static const struct msghdr *sendmsg_msg_ptr; /* saved address */
static struct msghdr sendmsg_msg; /* saved header */
static struct iovec sendmsg_msg_iov; /* actual message iov */
static int sendmsg_flags; /* saved input */
ssize_t
sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
	sendmsg_sockfd = sockfd;
	sendmsg_msg_ptr = msg;
	if (msg != NULL) {
		memcpy(&sendmsg_msg, msg, sizeof(struct msghdr));

		if (msg->msg_iov != NULL) {
			sendmsg_msg_iov.iov_len = msg->msg_iov[0].iov_len;
			sendmsg_msg_iov.iov_base =
					calloc(1, sendmsg_msg_iov.iov_len);
			memcpy(sendmsg_msg_iov.iov_base,
					msg->msg_iov[0].iov_base,
					msg->msg_iov[0].iov_len);
		}
	}
	sendmsg_flags = flags;
	return sendmsg_return;
}

static int recvmsg_call_count; /* find out if it was called */
static ssize_t recvmsg_return; /* value to be returned */
static int recvmsg_sockfd; /* saved input */
static struct msghdr *recvmsg_msg_ptr; /* saved ptr address */
static Drpc__Response *recvmsg_msg_content_resp; /* to be packed into iov */
static int recvmsg_flags; /* saved input */
ssize_t
recvmsg(int sockfd, struct msghdr *msg, int flags)
{
	recvmsg_call_count++;

	recvmsg_sockfd = sockfd;
	recvmsg_msg_ptr = msg;
	if (msg != NULL && recvmsg_msg_content_resp != NULL) {
		/* Making an assumption that the size of the IOV
		 * provided is big enough to hold this struct.
		 */
		drpc__response__pack(recvmsg_msg_content_resp,
				msg->msg_iov[0].iov_base);
	}
	recvmsg_flags = flags;
	return recvmsg_return;
}

/*
 * Test setup and teardown
 */
static int
setup_drpc_mocks(void **state)
{
	/* Init spy variables and set up default return values for mocks */

	socket_return = 25; /* arbitrary default - it's a fd */
	socket_family = 0;
	socket_type = 0;
	socket_protocol = 0;

	connect_return = 0; /* success */
	connect_sockfd = 0;
	connect_addr_ptr = NULL;
	memset(&connect_addr, 0, sizeof(connect_addr));
	connect_addrlen = 0;

	close_return = 0; /* success */
	close_fd = 0;

	sendmsg_return = 5; /* greater than 0 is success */
	sendmsg_sockfd = 0;
	sendmsg_msg_ptr = NULL;
	memset(&sendmsg_msg, 0, sizeof(sendmsg_msg));
	memset(&sendmsg_msg_iov, 0, sizeof(sendmsg_msg_iov));
	sendmsg_flags = 0;

	recvmsg_call_count = 0;
	recvmsg_return = 5; /* greater than 0 is success */
	recvmsg_sockfd = 0;
	recvmsg_msg_ptr = NULL;
	recvmsg_msg_content_resp = NULL;
	recvmsg_flags = 0;

	return 0;
}

static int
teardown_drpc_mocks(void **state)
{
	free(sendmsg_msg_iov.iov_base);

	return 0;
}

/*
 * drpc_connect unit tests
 */

static void
test_drpc_connect_returns_null_if_socket_fails(void **state)
{
	socket_return = -1; /* < 0 indicates failure */

	assert_null(drpc_connect(TEST_SOCK_ADDR));
}

static void
test_drpc_connect_returns_null_if_connect_fails(void **state)
{
	connect_return = -1; /* < 0 indicates failure */

	assert_null(drpc_connect(TEST_SOCK_ADDR));
}

static void
test_drpc_connect_success(void **state)
{
	struct drpc *ctx = drpc_connect(TEST_SOCK_ADDR);

	/* created socket with correct input params */
	assert_int_equal(socket_family, AF_UNIX);
	assert_int_equal(socket_type, SOCK_SEQPACKET);
	assert_int_equal(socket_protocol, 0);

	/* connected to socket with correct input params */
	assert_int_equal(connect_sockfd, socket_return);
	assert_non_null(connect_addr_ptr);
	assert_int_equal(connect_addr.sun_family, socket_family);
	assert_string_equal(connect_addr.sun_path, TEST_SOCK_ADDR);
	assert_int_equal(connect_addrlen, sizeof(struct sockaddr_un));

	/* returned correct ctx */
	assert_non_null(ctx);
	assert_int_equal(ctx->sequence, 0);
	assert_int_equal(ctx->comm->fd, socket_return);
	assert_int_equal(ctx->comm->flags, 0);

	free_drpc(ctx);
}

/*
 * drpc_close unit tests
 */
static void
test_drpc_close_fails_if_ctx_null(void **state)
{
	assert_int_equal(drpc_close(NULL), -DER_INVAL);
}

static void
test_drpc_close_fails_if_ctx_comm_null(void **state)
{
	struct drpc *ctx = calloc(1, sizeof(struct drpc));

	assert_int_equal(drpc_close(ctx), -DER_INVAL);
}

static void
test_drpc_close_success(void **state)
{
	int		expected_fd = 123;
	struct drpc	*ctx = new_drpc_with_fd(expected_fd);

	assert_int_equal(drpc_close(ctx), DER_SUCCESS);

	/* called close() with the ctx fd */
	assert_int_equal(close_fd, expected_fd);
}

/*
 * drpc_call unit tests
 */

static void
test_drpc_call_fails_if_sendmsg_fails(void **state)
{
	struct drpc	*ctx = new_drpc_with_fd(2);
	Drpc__Response	*resp = NULL;
	Drpc__Call	*call = new_drpc_call();

	sendmsg_return = -1;
	errno = EINVAL; /* translates to -DER_INVAL */

	assert_int_equal(drpc_call(ctx, 0, call, &resp), -DER_INVAL);
	assert_null(resp);

	drpc__call__free_unpacked(call, NULL);
	free_drpc(ctx);
}

static void
test_drpc_call_sends_call_as_mesg(void **state)
{
	int		expected_fd = 3;
	struct drpc	*ctx = new_drpc_with_fd(expected_fd);
	Drpc__Response	*resp = NULL;
	Drpc__Call	*call = new_drpc_call();
	size_t		expected_msg_size;
	uint8_t		*expected_msg;

	ctx->sequence = 10; /* arbitrary but nonzero */
	call->sequence = 0;

	drpc_call(ctx, 0, call, &resp);

	/* drpc_call updated call seq number and incremented ctx seq num */
	assert_int_equal(ctx->sequence, call->sequence + 1);

	/* Packed message is the call struct updated by drpc_call */
	expected_msg_size = drpc__call__get_packed_size(call);
	expected_msg = calloc(1, expected_msg_size);
	drpc__call__pack(call, expected_msg);

	/* Sent to the proper socket */
	assert_int_equal(sendmsg_sockfd, expected_fd);

	/* Check structure and contents of the message */
	assert_non_null(sendmsg_msg_ptr);
	assert_non_null(sendmsg_msg.msg_iov);
	assert_int_equal(sendmsg_msg.msg_iovlen, 1);
	assert_non_null(sendmsg_msg_iov.iov_base);
	assert_int_equal(sendmsg_msg_iov.iov_len,
			expected_msg_size);
	assert_memory_equal(sendmsg_msg_iov.iov_base,
			expected_msg, expected_msg_size);

	/* No flags */
	assert_int_equal(sendmsg_flags, 0);

	free(expected_msg);
	drpc__response__free_unpacked(resp, NULL);
	drpc__call__free_unpacked(call, NULL);
	free_drpc(ctx);
}

static void
test_drpc_call_with_no_flags_returns_async(void **state)
{
	struct drpc	*ctx = new_drpc_with_fd(1);
	Drpc__Response	*resp = NULL;
	Drpc__Call	*call = new_drpc_call();

	assert_int_equal(drpc_call(ctx, 0, call, &resp), DER_SUCCESS);

	assert_int_equal(resp->sequence, call->sequence);
	assert_int_equal(resp->status, DRPC__STATUS__SUBMITTED);

	/* ensure recvmsg not called */
	assert_int_equal(recvmsg_call_count, 0);

	drpc__response__free_unpacked(resp, NULL);
	drpc__call__free_unpacked(call, NULL);
	free_drpc(ctx);
}

static void
test_drpc_call_with_sync_flag_gets_socket_response(void **state)
{
	struct drpc	*ctx = new_drpc_with_fd(1);
	Drpc__Response	*resp = NULL;
	Drpc__Call	*call = new_drpc_call();
	Drpc__Response	*expected_resp;

	/* Actual contents of the message are arbitrary - just needs to be
	 * identifiable.
	 */
	expected_resp = calloc(1, sizeof(Drpc__Response));
	drpc__response__init(expected_resp);
	expected_resp->sequence = 12345;
	expected_resp->status = DRPC__STATUS__FAILURE;

	recvmsg_msg_content_resp = expected_resp;

	assert_int_equal(drpc_call(ctx, R_SYNC, call, &resp), DER_SUCCESS);

	assert_int_equal(resp->sequence, expected_resp->sequence);
	assert_int_equal(resp->status, expected_resp->status);
	assert_int_equal(resp->body.len, expected_resp->body.len);

	drpc__response__free_unpacked(resp, NULL);
	drpc__response__free_unpacked(expected_resp, NULL);
	drpc__call__free_unpacked(call, NULL);
	free_drpc(ctx);
}

static void
test_drpc_call_with_sync_flag_fails_on_recvmsg_fail(void **state)
{
	struct drpc	*ctx = new_drpc_with_fd(1);
	Drpc__Response	*resp = NULL;
	Drpc__Call	*call = new_drpc_call();

	recvmsg_return = -1;
	errno = EINVAL;

	assert_int_equal(drpc_call(ctx, R_SYNC, call, &resp), -DER_INVAL);
	assert_null(resp);

	drpc__call__free_unpacked(call, NULL);
	free_drpc(ctx);
}

/* Convenience macro for tests in this file */
#define DRPC_UTEST(X)						\
	cmocka_unit_test_setup_teardown(X, setup_drpc_mocks,	\
			teardown_drpc_mocks)

int
main(void)
{
	const struct CMUnitTest tests[] = {
		DRPC_UTEST(test_drpc_connect_returns_null_if_socket_fails),
		DRPC_UTEST(test_drpc_connect_returns_null_if_connect_fails),
		DRPC_UTEST(test_drpc_connect_success),
		DRPC_UTEST(test_drpc_close_fails_if_ctx_null),
		DRPC_UTEST(test_drpc_close_fails_if_ctx_comm_null),
		DRPC_UTEST(test_drpc_close_success),
		DRPC_UTEST(test_drpc_call_fails_if_sendmsg_fails),
		DRPC_UTEST(test_drpc_call_sends_call_as_mesg),
		DRPC_UTEST(test_drpc_call_with_no_flags_returns_async),
		DRPC_UTEST(test_drpc_call_with_sync_flag_gets_socket_response),
		DRPC_UTEST(test_drpc_call_with_sync_flag_fails_on_recvmsg_fail)
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}

#undef DRPC_UTEST
