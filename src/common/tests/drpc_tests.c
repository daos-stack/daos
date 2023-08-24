/*
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * Unit tests for the drpc module
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <daos_errno.h>
#include <daos/drpc.h>
#include <daos/test_mocks.h>
#include <daos/tests_lib.h>
#include <daos/test_utils.h>

#if D_HAS_WARNING(4, "-Wframe-larger-than=")
	#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif

/* None of these tests depend on a real socket existing */
static char *TEST_SOCK_ADDR = "/good/socket.sock";

/*
 * Test setup and teardown
 */
static int
setup_drpc_mocks(void **state)
{
	/* Init spy variables and set up default return values for mocks */

	mock_socket_setup();
	mock_connect_setup();
	mock_bind_setup();
	mock_fcntl_setup();
	mock_listen_setup();
	mock_accept_setup();
	mock_close_setup();
	mock_sendmsg_setup();
	mock_recvmsg_setup();

	mock_drpc_handler_setup();

	return 0;
}

static int
teardown_drpc_mocks(void **state)
{
	mock_drpc_handler_teardown();

	return 0;
}

/*
 * drpc_connect unit tests
 */

static void
test_drpc_connect_returns_null_if_socket_fails(void **state)
{
	socket_return = -ENOENT; /* < 0 indicates failure */
	struct drpc *drpc;

	assert_rc_equal(drpc_connect(TEST_SOCK_ADDR, &drpc), -DER_NONEXIST);
	assert_null(drpc);
}

static void
test_drpc_connect_returns_null_if_connect_fails(void **state)
{
	connect_return = -ENOENT; /* < 0 indicates failure */
	struct drpc *drpc;

	assert_rc_equal(drpc_connect(TEST_SOCK_ADDR, &drpc), -DER_NONEXIST);
	assert_null(drpc);

	/* Closed the socket */
	assert_int_equal(close_fd, socket_return);
}

static void
test_drpc_connect_success(void **state)
{
	int rc;
	struct drpc *ctx;

	rc = drpc_connect(TEST_SOCK_ADDR, &ctx);
	assert_rc_equal(rc, 0);
	assert_non_null(ctx);

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
	assert_int_equal(ctx->ref_count, 1);

	free_drpc(ctx);
}

/*
 * drpc_close unit tests
 */
static void
test_drpc_close_fails_if_ctx_null(void **state)
{
	assert_rc_equal(drpc_close(NULL), -DER_INVAL);
}

static void
test_drpc_close_fails_if_ctx_comm_null(void **state)
{
	struct drpc *ctx;

	D_ALLOC_PTR(ctx);

	assert_rc_equal(drpc_close(ctx), -DER_INVAL);

	D_FREE(ctx);
}

static void
test_drpc_close_closing_socket_fails(void **state)
{
	int		expected_fd = 123;
	struct drpc	*ctx = new_drpc_with_fd(expected_fd);

	close_return = -ENOMEM;

	/* error is logged but ignored */
	assert_rc_equal(drpc_close(ctx), 0);

	/* called close() */
	assert_int_equal(close_fd, expected_fd);
}

static void
test_drpc_close_success(void **state)
{
	int		expected_fd = 123;
	struct drpc	*ctx = new_drpc_with_fd(expected_fd);

	ctx->ref_count = 1;

	assert_rc_equal(drpc_close(ctx), 0);

	/* called close() with the ctx fd */
	assert_int_equal(close_fd, expected_fd);
}

static void
test_drpc_close_with_unexpected_ref_count(void **state)
{
	struct drpc	*ctx = new_drpc_with_fd(123);

	ctx->ref_count = 0;

	assert_rc_equal(drpc_close(ctx), -DER_INVAL);

	free_drpc(ctx);
}

static void
test_drpc_close_with_multiple_refs(void **state)
{
	struct drpc *ctx = new_drpc_with_fd(123);

	ctx->ref_count = 2;

	assert_rc_equal(drpc_close(ctx), 0);

	assert_int_equal(close_fd, 0); /* close() wasn't called */
	assert_int_equal(ctx->ref_count, 1);

	free_drpc(ctx);
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

	sendmsg_return = -EINVAL; /* translates to -DER_INVAL */

	assert_rc_equal(drpc_call(ctx, 0, call, &resp), -DER_INVAL);
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

	assert_rc_equal(drpc_call(ctx, 0, call, &resp), 0);

	/* drpc_call updated call seq number and incremented ctx seq num */
	assert_int_equal(ctx->sequence, call->sequence + 1);

	/* Packed message is the call struct updated by drpc_call */
	expected_msg_size = drpc__call__get_packed_size(call);
	D_ALLOC(expected_msg, expected_msg_size);
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

	D_FREE(expected_msg);
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

	assert_rc_equal(drpc_call(ctx, 0, call, &resp), DER_SUCCESS);

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

	assert_rc_equal(drpc_call(ctx, R_SYNC, call, &resp), DER_SUCCESS);

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

	recvmsg_return = -EINVAL;

	assert_rc_equal(drpc_call(ctx, R_SYNC, call, &resp), -DER_INVAL);
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
	assert_int_equal(ctx->ref_count, 1);

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
	bind_return = -ENOENT;

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
	assert_ptr_equal(session_ctx->handler, ctx->handler);
	assert_int_equal(session_ctx->ref_count, 1);

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
 * drpc_recv_call unit tests
 */
static void
test_drpc_recv_call_null_ctx(void **state)
{
	Drpc__Call *msg = NULL;

	assert_rc_equal(drpc_recv_call(NULL, &msg), -DER_INVAL);
}

static void
test_drpc_recv_call_bad_handler(void **state)
{
	struct drpc	*ctx = new_drpc_with_fd(12);
	Drpc__Call	*call = NULL;

	ctx->handler = NULL;

	assert_rc_equal(drpc_recv_call(ctx, &call), -DER_INVAL);
	assert_null(call);

	free_drpc(ctx);
}

static void
test_drpc_recv_call_null_call(void **state)
{
	struct drpc *ctx = new_drpc_with_fd(12);

	assert_rc_equal(drpc_recv_call(ctx, NULL), -DER_INVAL);

	free_drpc(ctx);
}

static void
assert_drpc_recv_call_fails_with_recvmsg_errno(int recvmsg_errno,
		int expected_retval)
{
	struct drpc	*ctx = new_drpc_with_fd(3);
	Drpc__Call	*call = NULL;

	mock_valid_drpc_call_in_recvmsg();

	recvmsg_call_count = 0;
	recvmsg_return = -recvmsg_errno;

	assert_rc_equal(drpc_recv_call(ctx, &call), expected_retval);

	assert_null(call);
	assert_int_equal(recvmsg_call_count, 1);

	free_drpc(ctx);
}

static void
test_drpc_recv_call_recvmsg_fails(void **state)
{
	assert_drpc_recv_call_fails_with_recvmsg_errno(ENOMEM, -DER_NOMEM);
}

static void
test_drpc_recv_call_recvmsg_would_block(void **state)
{
	assert_drpc_recv_call_fails_with_recvmsg_errno(EWOULDBLOCK, -DER_AGAIN);
	assert_drpc_recv_call_fails_with_recvmsg_errno(EAGAIN, -DER_AGAIN);
}

static void
test_drpc_recv_call_malformed(void **state)
{
	struct drpc	*ctx = new_drpc_with_fd(6);
	Drpc__Call	*call = NULL;

	/* Incoming message is weird garbage */
	recvmsg_return = sizeof(recvmsg_msg_content);
	memset(recvmsg_msg_content, 1, sizeof(recvmsg_msg_content));

	assert_rc_equal(drpc_recv_call(ctx, &call), -DER_PROTO);

	assert_null(call);

	free_drpc(ctx);
}

static void
test_drpc_recv_call_success(void **state)
{
	struct drpc	*ctx = new_drpc_with_fd(6);
	Drpc__Call	*call = NULL;
	Drpc__Call	*expected_call = new_drpc_call();

	mock_valid_drpc_call_in_recvmsg();

	assert_rc_equal(drpc_recv_call(ctx, &call), 0);

	assert_int_equal(call->module, expected_call->module);
	assert_int_equal(call->method, expected_call->method);
	assert_int_equal(call->sequence, expected_call->sequence);
	assert_int_equal(call->body.len, expected_call->body.len);

	/*
	 * Called recvmsg()
	 */
	assert_int_equal(recvmsg_call_count, 1);
	assert_int_equal(recvmsg_sockfd, ctx->comm->fd);
	assert_non_null(recvmsg_msg_ptr);
	assert_non_null(recvmsg_msg_iov_base_ptr);
	assert_int_equal(recvmsg_msg_iov_len, UNIXCOMM_MAXMSGSIZE);
	assert_int_equal(recvmsg_flags, 0);

	free_drpc(ctx);
	drpc_call_free(call);
	drpc_call_free(expected_call);
}

/*
 * drpc_send_resp unit tests
 */
static void
test_drpc_send_response_null_ctx(void **state)
{
	Drpc__Response *resp = new_drpc_response();

	assert_rc_equal(drpc_send_response(NULL, resp), -DER_INVAL);

	drpc_response_free(resp);
}

static void
test_drpc_send_response_bad_handler(void **state)
{
	struct drpc	*ctx = new_drpc_with_fd(12);
	Drpc__Response	*resp = new_drpc_response();

	ctx->handler = NULL;

	assert_rc_equal(drpc_send_response(ctx, resp), -DER_INVAL);

	free_drpc(ctx);
	drpc_response_free(resp);
}

static void
test_drpc_send_response_null_resp(void **state)
{
	struct drpc *ctx = new_drpc_with_fd(12);

	assert_rc_equal(drpc_send_response(ctx, NULL), -DER_INVAL);

	free_drpc(ctx);
}

static void
test_drpc_send_response_sendmsg_fails(void **state)
{
	struct drpc	*ctx = new_drpc_with_fd(122);
	Drpc__Response	*resp = new_drpc_response();

	sendmsg_return = -ENOMEM;

	assert_rc_equal(drpc_send_response(ctx, resp), -DER_NOMEM);

	free_drpc(ctx);
	drpc_response_free(resp);
}

static void
test_drpc_send_response_success(void **state)
{
	struct drpc	*ctx = new_drpc_with_fd(6);
	Drpc__Response	*resp = new_drpc_response();
	uint8_t		expected_response[UNIXCOMM_MAXMSGSIZE];
	size_t		expected_response_size;

	assert_rc_equal(drpc_send_response(ctx, resp), DER_SUCCESS);

	/*
	 * Sent response message - should be the one returned from
	 * the handler
	 */
	memset(expected_response, 0, sizeof(expected_response));
	drpc__response__pack(resp, expected_response);
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
	drpc_response_free(resp);
}

/*
 * drpc_call_create/free tests
 */
static void
test_drpc_call_create_null_ctx(void **state)
{
	Drpc__Call	*call;

	assert_rc_equal(drpc_call_create(NULL, 1, 2, &call), -DER_INVAL);
	assert_null(call);
}

static void
test_drpc_call_create_free(void **state)
{
	struct drpc	*ctx = new_drpc_with_fd(2);
	int32_t		module = 3;
	int32_t		method = 25;
	uint64_t	sequence = 203;
	Drpc__Call	*call;
	int		rc;

	ctx->sequence = sequence;

	rc = drpc_call_create(ctx, module, method, &call);

	assert_rc_equal(rc, DER_SUCCESS);
	assert_non_null(call);
	assert_memory_equal(call->base.descriptor, &drpc__call__descriptor,
			sizeof(ProtobufCMessageDescriptor));
	assert_int_equal(call->sequence, sequence);
	assert_int_equal(call->module, module);
	assert_int_equal(call->method, method);
	assert_int_equal(call->body.len, 0);
	assert_null(call->body.data);

	drpc_call_free(call);
	free_drpc(ctx);
}

static void
test_drpc_call_free_null(void **state)
{
	/* NULL input is a noop - just make sure no segfault */
	drpc_call_free(NULL);
}

static void
test_drpc_response_create_null_call(void **state)
{
	Drpc__Response	*resp;

	resp = drpc_response_create(NULL);

	assert_non_null(resp);
	assert_memory_equal(resp->base.descriptor, &drpc__response__descriptor,
			sizeof(ProtobufCMessageDescriptor));
	assert_int_equal(resp->sequence, -1);
	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);

	drpc_response_free(resp);
}

static void
test_drpc_response_create_free_success(void **state)
{
	Drpc__Call	*call;
	uint64_t	sequence = 12;
	Drpc__Response	*resp;

	call = new_drpc_call();
	call->sequence = sequence;

	resp = drpc_response_create(call);

	assert_non_null(resp);
	assert_memory_equal(resp->base.descriptor, &drpc__response__descriptor,
			sizeof(ProtobufCMessageDescriptor));
	assert_int_equal(resp->sequence, sequence);
	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);

	drpc_call_free(call);
	drpc_response_free(resp);
}

static void
test_drpc_response_free_null(void **state)
{
	/* NULL input is a noop - just make sure no segfault */
	drpc_response_free(NULL);
}

static void
test_drpc_add_ref_null(void **state)
{
	assert_rc_equal(drpc_add_ref(NULL), -DER_INVAL);
}

static void
test_drpc_add_ref_success(void **state)
{
	struct drpc	*ctx = new_drpc_with_fd(100);
	int		i;

	ctx->ref_count = 0;

	/* Add a bunch of refs just to see how it goes */
	for (i = 0; i < 125; i++) {
		assert_rc_equal(drpc_add_ref(ctx), 0);
		assert_int_equal(ctx->ref_count, i + 1);
	}

	free_drpc(ctx);
}

static void
test_drpc_add_ref_doesnt_update_max_count(void **state)
{
	struct drpc *ctx = new_drpc_with_fd(100);

	ctx->ref_count = UINT_MAX;

	assert_rc_equal(drpc_add_ref(ctx), -DER_INVAL);

	assert_int_equal(ctx->ref_count, UINT_MAX);

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
		DRPC_UTEST(test_drpc_close_closing_socket_fails),
		DRPC_UTEST(test_drpc_close_success),
		DRPC_UTEST(test_drpc_close_with_unexpected_ref_count),
		DRPC_UTEST(test_drpc_close_with_multiple_refs),
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
		DRPC_UTEST(test_drpc_recv_call_null_ctx),
		DRPC_UTEST(test_drpc_recv_call_bad_handler),
		DRPC_UTEST(test_drpc_recv_call_null_call),
		DRPC_UTEST(test_drpc_recv_call_recvmsg_fails),
		DRPC_UTEST(test_drpc_recv_call_recvmsg_would_block),
		DRPC_UTEST(test_drpc_recv_call_malformed),
		DRPC_UTEST(test_drpc_recv_call_success),
		DRPC_UTEST(test_drpc_send_response_null_ctx),
		DRPC_UTEST(test_drpc_send_response_bad_handler),
		DRPC_UTEST(test_drpc_send_response_null_resp),
		DRPC_UTEST(test_drpc_send_response_sendmsg_fails),
		DRPC_UTEST(test_drpc_send_response_success),
		cmocka_unit_test(test_drpc_call_create_null_ctx),
		cmocka_unit_test(test_drpc_call_create_free),
		cmocka_unit_test(test_drpc_call_free_null),
		cmocka_unit_test(test_drpc_response_create_null_call),
		cmocka_unit_test(test_drpc_response_create_free_success),
		cmocka_unit_test(test_drpc_response_free_null),
		cmocka_unit_test(test_drpc_add_ref_null),
		cmocka_unit_test(test_drpc_add_ref_success),
		cmocka_unit_test(test_drpc_add_ref_doesnt_update_max_count)
	};
	return cmocka_run_group_tests_name("common_drpc", tests, NULL, NULL);
}

#undef DRPC_UTEST
