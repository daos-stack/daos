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
#include <gurt/common.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <daos/drpc.h>

/* None of these tests depend on a real socket existing */
static char *TEST_SOCK_ADDR = "/good/socket.sock";

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

static int bind_return; /* value to be returned by bind() */
static int bind_sockfd; /* saved input */
static const struct sockaddr *bind_addr_ptr; /* for nullcheck */
static struct sockaddr_un bind_addr; /* saved copy of input value */
static socklen_t bind_addrlen; /* saved input */
int
bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	bind_sockfd = sockfd;
	bind_addr_ptr = addr;
	if (addr != NULL) {
		memcpy(&bind_addr, (struct sockaddr_un *)addr,
				sizeof(struct sockaddr_un));
	}
	bind_addrlen = addrlen;
	return bind_return;
}

static int fcntl_return; /* value to be returned by fcntl() */
static int fcntl_fd; /* saved input */
static int fcntl_cmd; /* saved input */
static int fcntl_arg; /* saved input */
int
fcntl(int fd, int cmd, ...)
{
	va_list arglist;

	/*
	 * Assuming only one arg for these tests
	 */
	va_start(arglist, cmd);
	fcntl_arg = va_arg(arglist, int);
	va_end(arglist);

	fcntl_fd = fd;
	fcntl_cmd = cmd;
	return fcntl_return;
}

static int listen_return; /* value to be returned by listen() */
static int listen_sockfd; /* saved input */
static int listen_backlog; /* saved input */
int
listen(int sockfd, int backlog)
{
	listen_sockfd = sockfd;
	listen_backlog = backlog;
	return listen_return;
}

static int accept_call_count;
static int accept_return; /* value to be returned by accept() */
static int accept_sockfd; /* saved input */
static struct sockaddr *accept_addr_ptr; /* saved input ptr */
static socklen_t *accept_addrlen_ptr; /* saved input ptr */
int
accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	accept_sockfd = sockfd;
	accept_addr_ptr = addr;
	accept_addrlen_ptr = addrlen;
	accept_call_count++;
	return accept_return;
}

static int close_call_count;
static int close_return; /* value to be returned by close() */
static int close_fd; /* saved input */
int
close(int fd)
{
	close_fd = fd;
	close_call_count++;
	return close_return;
}

static int sendmsg_call_count; /* how many times it was called */
static ssize_t sendmsg_return; /* to be returned by sendmsg() */
static int sendmsg_sockfd; /* saved input */
static const struct msghdr *sendmsg_msg_ptr; /* saved address */
static void *sendmsg_msg_iov_base_ptr; /* saved ptr address */
static size_t sendmsg_msg_iov_len; /* saved iov len */
static uint8_t sendmsg_msg_content[UNIXCOMM_MAXMSGSIZE]; /* copied into iov */
static int sendmsg_flags; /* saved input */
ssize_t
sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
	sendmsg_call_count++;

	sendmsg_sockfd = sockfd;
	sendmsg_msg_ptr = msg;
	if (msg != NULL) {
		memcpy(sendmsg_msg_content, msg->msg_iov[0].iov_base,
				msg->msg_iov[0].iov_len);
		sendmsg_msg_iov_base_ptr = msg->msg_iov[0].iov_base;
		sendmsg_msg_iov_len = msg->msg_iov[0].iov_len;
	}
	sendmsg_flags = flags;
	return sendmsg_return;
}

static int recvmsg_call_count; /* how many times it was called */
static ssize_t recvmsg_return; /* value to be returned */
static int recvmsg_sockfd; /* saved input */
static struct msghdr *recvmsg_msg_ptr; /* saved ptr address */
static void *recvmsg_msg_iov_base_ptr; /* saved ptr address */
static size_t recvmsg_msg_iov_len; /* saved iov len */
static uint8_t recvmsg_msg_content[UNIXCOMM_MAXMSGSIZE]; /* copied into iov */
static int recvmsg_flags; /* saved input */
ssize_t
recvmsg(int sockfd, struct msghdr *msg, int flags)
{
	recvmsg_call_count++;

	recvmsg_sockfd = sockfd;
	recvmsg_msg_ptr = msg;
	if (msg != NULL) {
		/*
		 * Making an assumption that the size of the IOV
		 * provided is big enough to hold the message
		 */
		memcpy(msg->msg_iov[0].iov_base, recvmsg_msg_content,
			msg->msg_iov[0].iov_len);
		recvmsg_msg_iov_base_ptr = msg->msg_iov[0].iov_base;
		recvmsg_msg_iov_len = msg->msg_iov[0].iov_len;
	}
	recvmsg_flags = flags;
	return recvmsg_return;
}

static int poll_return; /* value to be returned */
static void *poll_fds_ptr; /* saved ptr address */
static struct pollfd *poll_fds; /* saved copy of input */
static nfds_t poll_nfds; /* saved input */
static int poll_timeout; /* saved input */
static int poll_revents_return[1024]; /* to be returned in revents struct */
int
poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	poll_fds_ptr = (void *)fds;
	if (fds != NULL) {
		nfds_t i;

		/* Make a copy */
		D_ALLOC_ARRAY(poll_fds, (int)nfds);
		memcpy(poll_fds, fds, nfds * sizeof(struct pollfd));

		/* return events for the fds */
		for (i = 0; i < nfds; i++) {
			fds[i].revents = poll_revents_return[i];
		}
	}
	poll_nfds = nfds;
	poll_timeout = timeout;
	return poll_return;
}


/* Mock for the drpc->handler function pointer */
int mock_drpc_handler_call_count; /* how many times it was called */
Drpc__Call *mock_drpc_handler_call; /* alloc copy of the structure passed in */
void *mock_drpc_handler_resp_ptr; /* saved value of resp ptr */
Drpc__Response *mock_drpc_handler_resp_return; /* to be returned in *resp */
void
mock_drpc_handler(Drpc__Call *call, Drpc__Response **resp)
{
	uint8_t buffer[UNIXCOMM_MAXMSGSIZE];

	mock_drpc_handler_call_count++;

	if (call == NULL) {
		mock_drpc_handler_call = NULL;
	} else {
		/*
		 * Caller will free the original so we want to make a copy.
		 * Easiest way to copy is to pack and unpack.
		 */
		drpc__call__pack(call, buffer);
		mock_drpc_handler_call = drpc__call__unpack(NULL,
				drpc__call__get_packed_size(call),
				buffer);
	}

	mock_drpc_handler_resp_ptr = (void *)resp;

	if (resp != NULL && mock_drpc_handler_resp_return != NULL) {
		/*
		 * Caller will free the copy.
		 */
		size_t response_size = drpc__response__get_packed_size(
				mock_drpc_handler_resp_return);

		drpc__response__pack(mock_drpc_handler_resp_return,
				buffer);
		*resp = drpc__response__unpack(NULL, response_size,
				buffer);
	}
}

/*
 * Test factory and cleanup methods for convenience
 */

static struct drpc*
new_drpc_with_fd(int fd)
{
	struct drpc *ctx;

	D_ALLOC_PTR(ctx);
	D_ALLOC_PTR(ctx->comm);

	ctx->comm->fd = fd;

	ctx->handler = mock_drpc_handler;

	return ctx;
}

static void
free_drpc(struct drpc *ctx)
{
	if (ctx) {
		D_FREE(ctx->comm);
		D_FREE(ctx);
	}
}

static Drpc__Call*
new_drpc_call(void)
{
	Drpc__Call *call;

	D_ALLOC_PTR(call);

	drpc__call__init(call);
	call->module = 1;
	call->method = 2;

	return call;
}

static Drpc__Response*
new_drpc_response(void)
{
	Drpc__Response *resp;

	D_ALLOC_PTR(resp);

	drpc__response__init(resp);
	resp->status = DRPC__STATUS__FAILURE;

	return resp;
}

static void
mock_valid_drpc_call_in_recvmsg(void)
{
	Drpc__Call *call = new_drpc_call();

	/* Mock a valid DRPC call coming in */
	recvmsg_return = drpc__call__get_packed_size(call);
	drpc__call__pack(call, recvmsg_msg_content);

	drpc__call__free_unpacked(call, NULL);
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

	bind_return = 0; /* success */
	bind_sockfd = 0;
	bind_addr_ptr = NULL;
	memset(&bind_addr, 0, sizeof(bind_addr));
	bind_addrlen = 0;

	fcntl_return = 0; /* greater than 0 is success */
	fcntl_fd = 0;
	fcntl_cmd = 0;
	fcntl_arg = 0;

	listen_return = 0; /* success */
	listen_sockfd = 0;
	listen_backlog = 0;

	accept_call_count = 0;
	accept_return = 50; /* arbitrary default -- it's a fd */
	accept_sockfd = 0;
	accept_addr_ptr = NULL;
	accept_addrlen_ptr = NULL;

	close_call_count = 0;
	close_return = 0; /* success */
	close_fd = 0;

	sendmsg_call_count = 0;
	sendmsg_return = 5; /* greater than 0 is success */
	sendmsg_sockfd = 0;
	sendmsg_msg_ptr = NULL;
	sendmsg_msg_iov_base_ptr = NULL;
	sendmsg_msg_iov_len = 0;
	memset(&sendmsg_msg_content, 0, sizeof(sendmsg_msg_content));
	sendmsg_flags = 0;

	recvmsg_call_count = 0;
	recvmsg_return = 5; /* greater than 0 is success */
	recvmsg_sockfd = 0;
	recvmsg_msg_ptr = NULL;
	recvmsg_msg_iov_base_ptr = NULL;
	recvmsg_msg_iov_len = 0;
	memset(recvmsg_msg_content, 0, sizeof(recvmsg_msg_content));
	recvmsg_flags = 0;

	poll_return = 1; /* greater than 0 is success */
	poll_fds_ptr = NULL;
	poll_fds = NULL;
	poll_nfds = 0;
	poll_timeout = 0;
	memset(poll_revents_return, 0, sizeof(poll_revents_return));

	mock_drpc_handler_call_count = 0;
	mock_drpc_handler_call = NULL;
	mock_drpc_handler_resp_ptr = NULL;
	mock_drpc_handler_resp_return = new_drpc_response();

	return 0;
}

static int
teardown_drpc_mocks(void **state)
{
	D_FREE(poll_fds);

	drpc__call__free_unpacked(mock_drpc_handler_call, NULL);
	drpc__response__free_unpacked(mock_drpc_handler_resp_return, NULL);

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

	/* Closed the socket */
	assert_int_equal(close_fd, socket_return);
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
	assert_null(ctx->handler);

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
	assert_non_null(sendmsg_msg_iov_base_ptr);
	assert_int_equal(sendmsg_msg_iov_len,
			expected_msg_size);
	assert_memory_equal(sendmsg_msg_content, expected_msg,
			expected_msg_size);

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

	drpc__response__pack(expected_resp, recvmsg_msg_content);

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

/*
 * drpc_listen unit tests
 */
static void
test_drpc_listen_fails_with_null_path(void **state)
{
	assert_null(drpc_listen(NULL, mock_drpc_handler));
}

static void
test_drpc_listen_fails_with_null_handler(void **state)
{
	assert_null(drpc_listen(TEST_SOCK_ADDR, NULL));
}

static void
test_drpc_listen_success(void **state)
{
	struct drpc *ctx = drpc_listen(TEST_SOCK_ADDR, mock_drpc_handler);

	/*
	 * Valid ctx was returned for socket
	 */
	assert_non_null(ctx);
	assert_non_null(ctx->comm);
	assert_int_equal(ctx->comm->fd, socket_return);
	assert_int_equal(ctx->comm->flags, O_NONBLOCK);
	assert_int_equal(ctx->sequence, 0);
	assert_ptr_equal(ctx->handler, mock_drpc_handler);

	/*
	 * Called socket() with correct params
	 */
	assert_int_equal(socket_family, AF_UNIX);
	assert_int_equal(socket_type, SOCK_SEQPACKET);
	assert_int_equal(socket_protocol, 0);

	/*
	 * Called bind() with the socket we got back
	 */
	assert_int_equal(bind_sockfd, socket_return);
	assert_non_null(bind_addr_ptr);
	assert_int_equal(bind_addr.sun_family, socket_family);
	assert_string_equal(bind_addr.sun_path, TEST_SOCK_ADDR);
	assert_int_equal(bind_addrlen, sizeof(struct sockaddr_un));

	/*
	 * Called fcntl to set nonblocking flag
	 */
	assert_int_equal(fcntl_fd, socket_return);
	assert_int_equal(fcntl_cmd, F_SETFL);
	assert_int_equal(fcntl_arg, O_NONBLOCK);

	/*
	 * Called listen() on the bound socket
	 */
	assert_int_equal(listen_sockfd, socket_return);
	assert_int_equal(listen_backlog, SOMAXCONN);

	free_drpc(ctx);
}

static void
test_drpc_listen_fails_if_socket_fails(void **state)
{
	socket_return = -1;

	assert_null(drpc_listen(TEST_SOCK_ADDR, mock_drpc_handler));
}

static void
test_drpc_listen_fails_if_fcntl_fails(void **state)
{
	fcntl_return = -1;

	assert_null(drpc_listen(TEST_SOCK_ADDR, mock_drpc_handler));

	/* Socket was closed */
	assert_int_equal(close_fd, socket_return);
}

static void
test_drpc_listen_fails_if_bind_fails(void **state)
{
	bind_return = -1;

	assert_null(drpc_listen(TEST_SOCK_ADDR, mock_drpc_handler));

	/* Socket was closed */
	assert_int_equal(close_fd, socket_return);
}

static void
test_drpc_listen_fails_if_listen_fails(void **state)
{
	listen_return = -1;

	assert_null(drpc_listen(TEST_SOCK_ADDR, mock_drpc_handler));

	/* Socket was closed */
	assert_int_equal(close_fd, socket_return);
}

/*
 * drpc_accept unit tests
 */
static void
test_drpc_accept_fails_with_null_ctx(void **state)
{
	assert_null(drpc_accept(NULL));
}

static void
test_drpc_accept_fails_with_null_handler(void **state)
{
	struct drpc *ctx = new_drpc_with_fd(15);

	ctx->handler = NULL;

	assert_null(drpc_accept(ctx));

	free_drpc(ctx);
}

static void
test_drpc_accept_success(void **state)
{
	struct drpc	*ctx = new_drpc_with_fd(15);
	struct drpc	*session_ctx;

	session_ctx = drpc_accept(ctx);

	/* got context back for the new accepted connection */
	assert_non_null(session_ctx);
	assert_non_null(session_ctx->comm);
	assert_int_equal(session_ctx->comm->fd, accept_return);
	assert_int_equal(session_ctx->comm->flags, 0);
	assert_int_equal(session_ctx->sequence, 0);

	/* called accept() on parent ctx */
	assert_int_equal(accept_sockfd, ctx->comm->fd);
	assert_null(accept_addr_ptr);
	assert_null(accept_addrlen_ptr);

	free_drpc(session_ctx);
	free_drpc(ctx);
}

static void
test_drpc_accept_fails_if_accept_fails(void **state)
{
	struct drpc *ctx = new_drpc_with_fd(15);

	accept_return = -1;

	assert_null(drpc_accept(ctx));

	free_drpc(ctx);
}

/*
 * drpc_recv unit tests
 */
static void
test_drpc_recv_fails_if_ctx_is_null(void **state)
{
	assert_int_equal(drpc_recv(NULL), -DER_INVAL);
}

static void
test_drpc_recv_fails_if_handler_is_null(void **state)
{
	struct drpc *ctx = new_drpc_with_fd(12);

	ctx->handler = NULL;

	assert_int_equal(drpc_recv(ctx), -DER_INVAL);

	free_drpc(ctx);
}

static void
test_drpc_recv_success(void **state)
{
	struct drpc	*ctx = new_drpc_with_fd(6);
	uint8_t		expected_response[UNIXCOMM_MAXMSGSIZE];
	size_t		expected_response_size;

	mock_valid_drpc_call_in_recvmsg();

	assert_int_equal(drpc_recv(ctx), DER_SUCCESS);

	/*
	 * Called recvmsg()
	 */
	assert_int_equal(recvmsg_call_count, 1);
	assert_int_equal(recvmsg_sockfd, ctx->comm->fd);
	assert_non_null(recvmsg_msg_ptr);
	assert_non_null(recvmsg_msg_iov_base_ptr);
	assert_int_equal(recvmsg_msg_iov_len, UNIXCOMM_MAXMSGSIZE);
	assert_int_equal(recvmsg_flags, 0);

	/*
	 * Called handler with appropriate inputs
	 */
	assert_int_equal(mock_drpc_handler_call_count, 1);
	assert_non_null(mock_drpc_handler_call);
	assert_non_null(mock_drpc_handler_resp_ptr);

	/*
	 * Sent response message - should be the one returned from
	 * the handler
	 */
	memset(expected_response, 0, sizeof(expected_response));
	drpc__response__pack(mock_drpc_handler_resp_return, expected_response);
	expected_response_size =
			drpc__response__get_packed_size(
					mock_drpc_handler_resp_return);

	assert_int_equal(sendmsg_call_count, 1);
	assert_int_equal(sendmsg_sockfd, ctx->comm->fd);
	assert_non_null(sendmsg_msg_ptr);
	assert_non_null(sendmsg_msg_iov_base_ptr);
	assert_int_equal(sendmsg_msg_iov_len, expected_response_size);
	assert_memory_equal(sendmsg_msg_content, expected_response,
			expected_response_size);

	free_drpc(ctx);
}

static void
assert_drpc_recv_fails_with_recvmsg_errno(int recvmsg_errno,
		int expected_retval)
{
	struct drpc *ctx = new_drpc_with_fd(3);

	mock_valid_drpc_call_in_recvmsg();

	recvmsg_call_count = 0;
	recvmsg_return = -1;
	errno = recvmsg_errno;

	assert_int_equal(drpc_recv(ctx), expected_retval);

	/* Didn't call subsequent methods after recvmsg */
	assert_int_equal(recvmsg_call_count, 1);
	assert_int_equal(mock_drpc_handler_call_count, 0);
	assert_int_equal(sendmsg_call_count, 0);

	free_drpc(ctx);
}

static void
test_drpc_recv_fails_if_recvmsg_fails(void **state)
{
	assert_drpc_recv_fails_with_recvmsg_errno(ENOMEM, -DER_NOMEM);
}

static void
test_drpc_recv_fails_if_recvmsg_would_block(void **state)
{
	assert_drpc_recv_fails_with_recvmsg_errno(EWOULDBLOCK, -DER_AGAIN);
	assert_drpc_recv_fails_with_recvmsg_errno(EAGAIN, -DER_AGAIN);
}

static void
test_drpc_recv_fails_if_incoming_call_malformed(void **state)
{
	struct drpc *ctx = new_drpc_with_fd(6);

	/* Incoming message is weird garbage */
	recvmsg_return = sizeof(recvmsg_msg_content);
	memset(recvmsg_msg_content, 1, sizeof(recvmsg_msg_content));

	assert_int_equal(drpc_recv(ctx), -DER_MISC);

	free_drpc(ctx);
}

static void
test_drpc_recv_fails_if_sendmsg_fails(void **state)
{
	struct drpc *ctx = new_drpc_with_fd(122);

	mock_valid_drpc_call_in_recvmsg();
	sendmsg_return = -1;
	errno = EINVAL;

	assert_int_equal(drpc_recv(ctx), -DER_INVAL);

	free_drpc(ctx);
}

static void
test_drpc_recv_fails_if_handler_response_null(void **state)
{
	struct drpc *ctx = new_drpc_with_fd(200);

	mock_valid_drpc_call_in_recvmsg();
	drpc__response__free_unpacked(mock_drpc_handler_resp_return, NULL);
	mock_drpc_handler_resp_return = NULL;

	assert_int_equal(drpc_recv(ctx), -DER_NOMEM);

	free_drpc(ctx);
}

/*
 * drpc_progress unit test helpers
 */
static struct drpc_list *
new_drpc_list_node(struct drpc *ctx)
{
	struct drpc_list *list;

	D_ALLOC_PTR(list);
	list->ctx = ctx;

	return list;
}

static void
add_new_drpc_node_to_list(d_list_t *list, struct drpc *ctx)
{
	struct drpc_list *node = new_drpc_list_node(ctx);

	d_list_add_tail(&node->link, list);
}

void
cleanup_drpc_list(d_list_t *list)
{
	struct drpc_list *current;
	struct drpc_list *next;

	d_list_for_each_entry_safe(current, next, list, link) {
		d_list_del(&current->link);
		D_FREE(current);
	}
}

void
cleanup_drpc_progress_context(struct drpc_progress_context *ctx)
{
	D_FREE(ctx->listener_ctx);
	cleanup_drpc_list(&ctx->session_ctx_list);
}

void
init_drpc_progress_context(struct drpc_progress_context *ctx,
		struct drpc *listener)
{
	ctx->listener_ctx = listener;
	D_INIT_LIST_HEAD(&ctx->session_ctx_list);
}

void
add_sessions_to_drpc_progress_context(struct drpc_progress_context *ctx,
		int *session_fds, int num_sessions)
{
	int i;

	for (i = 0; i < num_sessions; i++) {
		add_new_drpc_node_to_list(&ctx->session_ctx_list,
				new_drpc_with_fd(session_fds[i]));
	}
}

void
set_poll_revents_for_sessions(int revents, int num_sessions)
{
	int i;

	for (i = 0; i < num_sessions; i++) {
		poll_revents_return[i] = revents;
	}
}

void
expect_sessions_in_drpc_progress_session_list(struct drpc_progress_context *ctx,
		int *session_fds, int num_sessions)
{
	struct drpc_list	*current;
	int			num_found = 0;
	int			i;

	d_list_for_each_entry(current, &ctx->session_ctx_list, link) {
		for (i = 0; i < num_sessions; i++) {
			if (current->ctx->comm->fd == session_fds[i]) {
				num_found++;
			}
		}
	}
	assert_int_equal(num_found, num_sessions);
}

void
expect_sessions_missing_from_drpc_progress_session_list(
		struct drpc_progress_context *ctx,
		int *session_fds, int num_sessions)
{
	struct drpc_list	*current;
	int			i;

	d_list_for_each_entry(current, &ctx->session_ctx_list, link) {
		for (i = 0; i < num_sessions; i++) {
			assert_int_not_equal(current->ctx->comm->fd,
					session_fds[i]);
		}
	}
}

/*
 * drpc_progress unit tests
 */
static void
test_drpc_progress_fails_if_ctx_null(void **state)
{
	assert_int_equal(drpc_progress(NULL, 15), -DER_INVAL);
}

static void
test_drpc_progress_fails_if_listener_null(void **state)
{
	struct drpc_progress_context ctx;

	init_drpc_progress_context(&ctx, NULL);

	assert_int_equal(drpc_progress(&ctx, 15), -DER_INVAL);
}

static void
test_drpc_progress_fails_if_node_ctx_null(void **state)
{
	struct drpc_progress_context ctx;

	init_drpc_progress_context(&ctx, new_drpc_with_fd(12));
	add_new_drpc_node_to_list(&ctx.session_ctx_list, NULL);

	assert_int_equal(drpc_progress(&ctx, 10), -DER_INVAL);

	cleanup_drpc_progress_context(&ctx);
}

static void
test_drpc_progress_fails_if_later_node_ctx_null(void **state)
{
	struct drpc_progress_context ctx;

	init_drpc_progress_context(&ctx, new_drpc_with_fd(12));
	add_new_drpc_node_to_list(&ctx.session_ctx_list, new_drpc_with_fd(15));
	add_new_drpc_node_to_list(&ctx.session_ctx_list, NULL);

	assert_int_equal(drpc_progress(&ctx, 10), -DER_INVAL);

	cleanup_drpc_progress_context(&ctx);
}

static void
test_drpc_progress_fails_if_node_comm_null(void **state)
{
	struct drpc_progress_context	ctx;
	struct drpc			*bad_drpc = new_drpc_with_fd(20);

	init_drpc_progress_context(&ctx, new_drpc_with_fd(12));
	D_FREE(bad_drpc->comm);
	add_new_drpc_node_to_list(&ctx.session_ctx_list, bad_drpc);

	assert_int_equal(drpc_progress(&ctx, 10), -DER_INVAL);

	cleanup_drpc_progress_context(&ctx);
}

static void
test_drpc_progress_accepts_timeout_0(void **state)
{
	struct drpc_progress_context ctx;

	init_drpc_progress_context(&ctx, new_drpc_with_fd(12));

	assert_int_equal(drpc_progress(&ctx, 0), DER_SUCCESS);

	/* zero timeout to poll is valid - means don't block */
	assert_int_equal(poll_timeout, 0);

	cleanup_drpc_progress_context(&ctx);
}

static void
test_drpc_progress_accepts_timeout_negative(void **state)
{
	struct drpc_progress_context ctx;

	init_drpc_progress_context(&ctx, new_drpc_with_fd(12));

	assert_int_equal(drpc_progress(&ctx, -1), DER_SUCCESS);

	/* negative timeout to poll is valid - means wait forever */
	assert_int_equal(poll_timeout, -1);

	cleanup_drpc_progress_context(&ctx);
}

static void
test_drpc_progress_listener_only_success(void **state)
{
	struct drpc_progress_context	ctx;
	int				expected_fd = 12;
	int				expected_timeout_ms = 105;
	struct drpc_list		*node;

	init_drpc_progress_context(&ctx, new_drpc_with_fd(expected_fd));
	poll_revents_return[0] = POLLIN;

	assert_int_equal(drpc_progress(&ctx, expected_timeout_ms), DER_SUCCESS);

	/* Check that poll() was called with properly translated inputs */
	assert_int_equal(poll_timeout, expected_timeout_ms);
	assert_int_equal(poll_nfds, 1);
	assert_non_null(poll_fds_ptr);
	assert_int_equal(poll_fds[0].fd, expected_fd);
	assert_int_equal(poll_fds[0].events, POLLIN | POLLPRI);

	/* revents is a return field - shouldn't be in input */
	assert_int_equal(poll_fds[0].revents, 0);

	/*
	 * Listener can only accept new connections.
	 */
	assert_int_equal(accept_call_count, 1);
	assert_int_equal(accept_sockfd, expected_fd);

	/* Listener can't recvmsg */
	assert_int_equal(recvmsg_call_count, 0);

	/* ctx should be updated with the new accepted session */
	assert_false(d_list_empty(&ctx.session_ctx_list));
	node = d_list_entry(ctx.session_ctx_list.next, struct drpc_list, link);
	assert_non_null(node->ctx);
	assert_int_equal(node->ctx->comm->fd, accept_return);

	cleanup_drpc_progress_context(&ctx);
}

static void
test_drpc_progress_poll_timed_out(void **state)
{
	struct drpc_progress_context ctx;

	init_drpc_progress_context(&ctx, new_drpc_with_fd(15));
	poll_return = 0;

	assert_int_equal(drpc_progress(&ctx, 20), -DER_TIMEDOUT);

	assert_int_equal(accept_call_count, 0); /* shouldn't be called */
	assert_int_equal(recvmsg_call_count, 0); /* shouldn't be called */

	cleanup_drpc_progress_context(&ctx);
}

static void
test_drpc_progress_poll_failed(void **state)
{
	struct drpc_progress_context ctx;

	init_drpc_progress_context(&ctx, new_drpc_with_fd(15));
	poll_return = -1;
	errno = ENOMEM;

	assert_int_equal(drpc_progress(&ctx, 20), -DER_NOMEM);

	assert_int_equal(accept_call_count, 0); /* shouldn't be called */
	assert_int_equal(recvmsg_call_count, 0); /* shouldn't be called */

	cleanup_drpc_progress_context(&ctx);
}

static void
test_drpc_progress_listener_accept_failed(void **state)
{
	struct drpc_progress_context ctx;

	init_drpc_progress_context(&ctx, new_drpc_with_fd(15));
	poll_revents_return[0] = POLLIN;
	accept_return = -1;

	/* No clear reason why accept would fail if we got data on it */
	assert_int_equal(drpc_progress(&ctx, 100), -DER_UNKNOWN);

	cleanup_drpc_progress_context(&ctx);
}

static void
test_drpc_progress_single_session_success(void **state)
{
	struct drpc_progress_context	ctx;
	struct drpc_progress_context	original_ctx;
	int				listener_fd = 13;
	int				session_fd = 12;
	int				expected_timeout_ms = 10;
	nfds_t				i;

	init_drpc_progress_context(&ctx, new_drpc_with_fd(listener_fd));
	add_new_drpc_node_to_list(&ctx.session_ctx_list,
			new_drpc_with_fd(session_fd));
	memcpy(&original_ctx, &ctx, sizeof(struct drpc_progress_context));
	mock_valid_drpc_call_in_recvmsg();

	/* sessions end up listed before listener in poll list */
	poll_revents_return[0] = POLLIN;

	assert_int_equal(drpc_progress(&ctx, expected_timeout_ms), DER_SUCCESS);

	/* Check that poll() was called with both session and listener */
	assert_int_equal(poll_timeout, expected_timeout_ms);
	assert_int_equal(poll_nfds, 2);
	assert_non_null(poll_fds_ptr);
	assert_int_equal(poll_fds[0].fd, session_fd);
	assert_int_equal(poll_fds[1].fd, listener_fd);
	for (i = 0; i < poll_nfds; i++) {
		assert_int_equal(poll_fds[i].events, POLLIN | POLLPRI);

		/* revents is a return field - shouldn't be in input */
		assert_int_equal(poll_fds[0].revents, 0);
	}

	/* No activity on listener */
	assert_int_equal(accept_call_count, 0);

	/* Session receives a message */
	assert_int_equal(recvmsg_call_count, 1);
	assert_int_equal(recvmsg_sockfd, session_fd);

	/* Session calls handler */
	assert_int_equal(mock_drpc_handler_call_count, 1);

	/* Session sends response */
	assert_int_equal(sendmsg_call_count, 1);
	assert_int_equal(sendmsg_sockfd, session_fd);

	/* Final ctx should be unchanged */
	assert_memory_equal(&ctx, &original_ctx,
			sizeof(struct drpc_progress_context));

	cleanup_drpc_progress_context(&ctx);
}

static void
test_drpc_progress_session_cleanup_if_recv_fails(void **state)
{
	struct drpc_progress_context	ctx;
	int				session_fds[] = { 36, 37 };
	int				num_sessions = 2;

	init_drpc_progress_context(&ctx, new_drpc_with_fd(25));
	add_sessions_to_drpc_progress_context(&ctx, session_fds, num_sessions);
	set_poll_revents_for_sessions(POLLIN, num_sessions);
	mock_valid_drpc_call_in_recvmsg();

	poll_revents_return[num_sessions] = POLLIN; /* listener */

	recvmsg_return = -1;
	errno = ENOMEM;

	/* the error was handled by closing the sessions */
	assert_int_equal(drpc_progress(&ctx, 1), DER_SUCCESS);

	/* Don't give up after failure - try them all */
	assert_int_equal(recvmsg_call_count, 2);

	/* Handled listener activity even if sessions failed */
	assert_int_equal(accept_call_count, 1);

	/* Failed sessions should have been closed */
	assert_int_equal(close_call_count, 2);

	/* Failed sessions should be removed */
	expect_sessions_missing_from_drpc_progress_session_list(&ctx,
			session_fds, num_sessions);

	cleanup_drpc_progress_context(&ctx);
}

static void
test_drpc_progress_session_fails_if_no_data(void **state)
{
	struct drpc_progress_context	ctx;
	int				session_fds[] = { 36, 37 };
	int				num_sessions = 2;

	init_drpc_progress_context(&ctx, new_drpc_with_fd(25));
	add_sessions_to_drpc_progress_context(&ctx, session_fds, num_sessions);
	set_poll_revents_for_sessions(POLLIN, num_sessions);
	mock_valid_drpc_call_in_recvmsg();

	poll_revents_return[num_sessions] = POLLIN; /* listener */

	recvmsg_return = -1;
	errno = EAGAIN; /* No data to fetch */

	/* Pass up the error this time - we didn't do anything with it */
	assert_int_equal(drpc_progress(&ctx, 1), -DER_AGAIN);

	/* Try all the sessions even if one fails */
	assert_int_equal(recvmsg_call_count, 2);

	/* Handle listener activity even if sessions fail */
	assert_int_equal(accept_call_count, 1);

	/* Don't close anything over missing data - connection still good */
	assert_int_equal(close_call_count, 0);

	/* Make sure our old sessions are still there */
	expect_sessions_in_drpc_progress_session_list(&ctx, session_fds,
			num_sessions);

	cleanup_drpc_progress_context(&ctx);
}

static void
test_drpc_progress_session_cleanup_if_pollerr(void **state)
{
	struct drpc_progress_context	ctx;
	int				session_fds[] = { 36, 37, 38 };
	int				num_sessions = 3;
	int				bad_session_idx = 1;

	init_drpc_progress_context(&ctx, new_drpc_with_fd(25));
	add_sessions_to_drpc_progress_context(&ctx, session_fds, num_sessions);
	set_poll_revents_for_sessions(POLLIN, num_sessions);
	mock_valid_drpc_call_in_recvmsg();

	/* Only mark one session bad */
	poll_revents_return[bad_session_idx] = POLLERR;
	poll_revents_return[num_sessions] = POLLIN; /* listener */

	/* the error was handled by closing the bad session */
	assert_int_equal(drpc_progress(&ctx, 1), DER_SUCCESS);

	/* Tried all the sessions with data, even if one failed */
	assert_int_equal(recvmsg_call_count, num_sessions - 1);

	/* Handled listener activity even if sessions failed */
	assert_int_equal(accept_call_count, 1);

	/* Failed session should have been closed */
	assert_int_equal(close_call_count, 1);

	/* failed session should be removed */
	expect_sessions_missing_from_drpc_progress_session_list(&ctx,
			&session_fds[bad_session_idx], 1);

	cleanup_drpc_progress_context(&ctx);
}

static void
test_drpc_progress_session_cleanup_if_pollhup(void **state)
{
	struct drpc_progress_context	ctx;
	int				session_fds[] = { 36, 37, 38 };
	int				num_sessions = 3;
	int				dead_session_idx = 0;

	init_drpc_progress_context(&ctx, new_drpc_with_fd(25));
	add_sessions_to_drpc_progress_context(&ctx, session_fds, num_sessions);
	set_poll_revents_for_sessions(POLLIN, num_sessions);
	mock_valid_drpc_call_in_recvmsg();

	/* Only mark one session disconnected */
	poll_revents_return[dead_session_idx] = POLLIN | POLLHUP;
	poll_revents_return[num_sessions] = POLLIN; /* listener */

	/* the error was handled by closing the bad session */
	assert_int_equal(drpc_progress(&ctx, 1), DER_SUCCESS);

	/* Tried all the sessions with data, even if one failed */
	assert_int_equal(recvmsg_call_count, num_sessions - 1);

	/* Handled listener activity after dealing with session */
	assert_int_equal(accept_call_count, 1);

	/* disconnected session should have been closed */
	assert_int_equal(close_call_count, 1);

	/* disconnected session should be removed */
	expect_sessions_missing_from_drpc_progress_session_list(&ctx,
			&session_fds[dead_session_idx], 1);

	cleanup_drpc_progress_context(&ctx);
}

static void
test_drpc_progress_listener_fails_if_pollerr(void **state)
{
	struct drpc_progress_context	ctx;
	int				session_fds[] = { 36, 37, 38 };
	int				num_sessions = 3;

	init_drpc_progress_context(&ctx, new_drpc_with_fd(25));
	add_sessions_to_drpc_progress_context(&ctx, session_fds, num_sessions);
	set_poll_revents_for_sessions(POLLIN, num_sessions);
	mock_valid_drpc_call_in_recvmsg();

	/* Listener has an error */
	poll_revents_return[num_sessions] = POLLERR;

	assert_int_equal(drpc_progress(&ctx, 1), -DER_UNKNOWN);

	/* Tried all the sessions with data */
	assert_int_equal(recvmsg_call_count, num_sessions);

	/* Did nothing with listener - due to the error */
	assert_int_equal(accept_call_count, 0);

	/* left the sessions open */
	assert_int_equal(close_call_count, 0);

	/* Make sure our old sessions are still there */
	expect_sessions_in_drpc_progress_session_list(&ctx, session_fds,
			num_sessions);

	cleanup_drpc_progress_context(&ctx);
}

static void
test_drpc_progress_listener_fails_if_pollhup(void **state)
{
	struct drpc_progress_context	ctx;
	int				session_fds[] = { 36, 37, 38 };
	int				num_sessions = 3;

	init_drpc_progress_context(&ctx, new_drpc_with_fd(25));
	add_sessions_to_drpc_progress_context(&ctx, session_fds, num_sessions);
	set_poll_revents_for_sessions(POLLIN, num_sessions);
	mock_valid_drpc_call_in_recvmsg();

	/* Unexpected event, in theory listener shouldn't get hangup */
	poll_revents_return[num_sessions] = POLLIN | POLLHUP;

	assert_int_equal(drpc_progress(&ctx, 1), -DER_UNKNOWN);

	/* Tried all the sessions with data */
	assert_int_equal(recvmsg_call_count, num_sessions);

	/* Did nothing with listener - due to the unexpected event */
	assert_int_equal(accept_call_count, 0);

	/* left the sessions open */
	assert_int_equal(close_call_count, 0);

	/* Make sure our old sessions are still there */
	expect_sessions_in_drpc_progress_session_list(&ctx, session_fds,
			num_sessions);

	cleanup_drpc_progress_context(&ctx);
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
		DRPC_UTEST(test_drpc_call_with_sync_flag_fails_on_recvmsg_fail),
		DRPC_UTEST(test_drpc_listen_fails_with_null_path),
		DRPC_UTEST(test_drpc_listen_fails_with_null_handler),
		DRPC_UTEST(test_drpc_listen_success),
		DRPC_UTEST(test_drpc_listen_fails_if_socket_fails),
		DRPC_UTEST(test_drpc_listen_fails_if_fcntl_fails),
		DRPC_UTEST(test_drpc_listen_fails_if_bind_fails),
		DRPC_UTEST(test_drpc_listen_fails_if_listen_fails),
		DRPC_UTEST(test_drpc_accept_fails_with_null_ctx),
		DRPC_UTEST(test_drpc_accept_fails_with_null_handler),
		DRPC_UTEST(test_drpc_accept_success),
		DRPC_UTEST(test_drpc_accept_fails_if_accept_fails),
		DRPC_UTEST(test_drpc_recv_fails_if_ctx_is_null),
		DRPC_UTEST(test_drpc_recv_fails_if_handler_is_null),
		DRPC_UTEST(test_drpc_recv_success),
		DRPC_UTEST(test_drpc_recv_fails_if_recvmsg_fails),
		DRPC_UTEST(test_drpc_recv_fails_if_recvmsg_would_block),
		DRPC_UTEST(test_drpc_recv_fails_if_incoming_call_malformed),
		DRPC_UTEST(test_drpc_recv_fails_if_sendmsg_fails),
		DRPC_UTEST(test_drpc_recv_fails_if_handler_response_null),
		DRPC_UTEST(test_drpc_progress_fails_if_ctx_null),
		DRPC_UTEST(test_drpc_progress_fails_if_listener_null),
		DRPC_UTEST(test_drpc_progress_fails_if_node_ctx_null),
		DRPC_UTEST(test_drpc_progress_fails_if_node_comm_null),
		DRPC_UTEST(test_drpc_progress_fails_if_later_node_ctx_null),
		DRPC_UTEST(test_drpc_progress_accepts_timeout_0),
		DRPC_UTEST(test_drpc_progress_accepts_timeout_negative),
		DRPC_UTEST(test_drpc_progress_listener_only_success),
		DRPC_UTEST(test_drpc_progress_poll_timed_out),
		DRPC_UTEST(test_drpc_progress_poll_failed),
		DRPC_UTEST(test_drpc_progress_listener_accept_failed),
		DRPC_UTEST(test_drpc_progress_single_session_success),
		DRPC_UTEST(test_drpc_progress_session_cleanup_if_recv_fails),
		DRPC_UTEST(test_drpc_progress_session_fails_if_no_data),
		DRPC_UTEST(test_drpc_progress_session_cleanup_if_pollerr),
		DRPC_UTEST(test_drpc_progress_session_cleanup_if_pollhup),
		DRPC_UTEST(test_drpc_progress_listener_fails_if_pollerr),
		DRPC_UTEST(test_drpc_progress_listener_fails_if_pollhup)
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}

#undef DRPC_UTEST
