/*
 * (C) Copyright 2018-2020 Intel Corporation.
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
 * Unit tests for drpc_progress
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos/test_mocks.h>
#include <daos/test_utils.h>
#include <abt.h>
#include "../drpc_internal.h"
#include "../srv_internal.h"

/*
 * Mocks
 */
int		dss_ult_create_return;
void		(*dss_ult_create_func)(void *);
void		*dss_ult_create_arg_ptr;
int		dss_ult_create_ult_type;
int		dss_ult_create_tgt_idx;
size_t		dss_ult_create_stack_size;
ABT_thread	*dss_ult_create_ult_ptr;
int
dss_ult_create(void (*func)(void *), void *arg, int ult_type, int tgt_idx,
	       size_t stack_size, ABT_thread *ult)
{
	struct drpc_call_ctx *call_ctx;

	dss_ult_create_func = func;
	dss_ult_create_arg_ptr = arg;
	dss_ult_create_ult_type = ult_type;
	dss_ult_create_tgt_idx = tgt_idx;
	dss_ult_create_stack_size = stack_size;
	dss_ult_create_ult_ptr = ult;

	/*
	 * arg is dynamically allocated and owned by the ULT. In cases where the
	 * real ULT is expected to be executed, we need to clean up the memory
	 * here.
	 */
	if (arg != NULL && dss_ult_create_return == 0) {
		call_ctx = (struct drpc_call_ctx *)arg;
		drpc_call_free(call_ctx->call);
		drpc_response_free(call_ctx->resp);
		call_ctx->session->ref_count--;
		D_FREE(call_ctx);
	}

	return dss_ult_create_return;
}

/*
 * Setup and teardown
 */
static int
drpc_progress_test_setup(void **state)
{
	mock_poll_setup();
	mock_accept_setup();
	mock_recvmsg_setup();
	mock_sendmsg_setup();
	mock_drpc_handler_setup();
	mock_close_setup();

	dss_ult_create_return = 0;
	dss_ult_create_func = NULL;
	dss_ult_create_arg_ptr = NULL;
	dss_ult_create_ult_type = -1;
	dss_ult_create_tgt_idx = -1;
	dss_ult_create_stack_size = -1;
	dss_ult_create_ult_ptr = NULL;

	return 0;
}

static int
drpc_progress_test_teardown(void **state)
{
	mock_poll_teardown();
	mock_drpc_handler_teardown();

	return 0;
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
		free_drpc(current->ctx);
		D_FREE(current);
	}
}

void
cleanup_drpc_progress_context(struct drpc_progress_context *ctx)
{
	free_drpc(ctx->listener_ctx);
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
	assert_int_equal(drpc_progress(&ctx, 100), -DER_MISC);

	cleanup_drpc_progress_context(&ctx);
}

static void
test_drpc_progress_single_session_bad_call(void **state)
{
	struct drpc_progress_context	ctx;
	struct drpc_progress_context	original_ctx;
	int				listener_fd = 13;
	int				session_fd = 12;
	size_t				i;
	size_t				bad_msg_size = 120;
	Drpc__Response			*resp = NULL;

	init_drpc_progress_context(&ctx, new_drpc_with_fd(listener_fd));
	add_new_drpc_node_to_list(&ctx.session_ctx_list,
				  new_drpc_with_fd(session_fd));
	memcpy(&original_ctx, &ctx, sizeof(struct drpc_progress_context));

	/* Get some arbitrary junk via recvmsg */
	recvmsg_return = bad_msg_size;
	for (i = 0; i < bad_msg_size; i++) {
		recvmsg_msg_content[i] = i;
	}

	/* sessions end up listed before listener in poll list */
	poll_revents_return[0] = POLLIN;

	assert_int_equal(drpc_progress(&ctx, 0), DER_SUCCESS);

	/* Session receives the garbage message */
	assert_int_equal(recvmsg_call_count, 1);
	assert_int_equal(recvmsg_sockfd, session_fd);

	/* Sent response indicating bad message */
	assert_int_equal(sendmsg_call_count, 1);
	assert_int_equal(sendmsg_sockfd, session_fd);

	resp = drpc__response__unpack(NULL, sendmsg_msg_iov_len,
				      sendmsg_msg_content);
	assert_non_null(resp);
	assert_int_equal(resp->status, DRPC__STATUS__FAILED_UNMARSHAL_CALL);

	drpc_response_free(resp);
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

	/* ULT spawned to deal with the message */
	assert_non_null(dss_ult_create_func);
	assert_non_null(dss_ult_create_arg_ptr);
	assert_int_equal(dss_ult_create_ult_type, DSS_XS_SYS);
	assert_int_equal(dss_ult_create_tgt_idx, 0);
	assert_int_equal(dss_ult_create_stack_size, 0);
	assert_null(dss_ult_create_ult_ptr); /* self-freeing ULT */

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
test_drpc_progress_session_cleanup_if_ult_fails(void **state)
{
	struct drpc_progress_context	ctx;
	int				session_fds[] = { 36, 37 };
	int				num_sessions = 2;

	init_drpc_progress_context(&ctx, new_drpc_with_fd(25));
	add_sessions_to_drpc_progress_context(&ctx, session_fds, num_sessions);
	set_poll_revents_for_sessions(POLLIN, num_sessions);
	mock_valid_drpc_call_in_recvmsg();

	poll_revents_return[num_sessions] = POLLIN; /* listener */

	dss_ult_create_return = -DER_MISC;

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

	assert_int_equal(drpc_progress(&ctx, 1), -DER_MISC);

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

	assert_int_equal(drpc_progress(&ctx, 1), -DER_MISC);

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

static void
test_drpc_progress_context_create_with_bad_input(void **state)
{
	assert_null(drpc_progress_context_create(NULL));
}

static void
test_drpc_progress_context_create_success(void **state)
{
	struct drpc_progress_context	*ctx;
	struct drpc			*listener = new_drpc_with_fd(16);

	ctx = drpc_progress_context_create(listener);

	assert_non_null(ctx);
	assert_ptr_equal(ctx->listener_ctx, listener);
	assert_true(d_list_empty(&ctx->session_ctx_list));

	cleanup_drpc_progress_context(ctx); /* cleans up listener too */
	D_FREE(ctx);
}

static void
test_drpc_progress_context_close_with_null(void **state)
{
	drpc_progress_context_close(NULL);

	/* doesn't segfault, but nothing should happen */
	assert_int_equal(close_call_count, 0);
}

static void
test_drpc_progress_context_close_with_listener_only(void **state)
{
	struct drpc_progress_context	*ctx;
	int				listener_fd = 16;

	D_ALLOC_PTR(ctx);
	init_drpc_progress_context(ctx, new_drpc_with_fd(listener_fd));

	drpc_progress_context_close(ctx); /* should clean up everything */

	/* listener should have been closed */
	assert_int_equal(close_call_count, 1);
	assert_int_equal(close_fd, listener_fd);
}

static void
test_drpc_progress_context_close_with_one_session(void **state)
{
	struct drpc_progress_context	*ctx;
	int				listener_fd = 29;
	int				session_fds[] = { 2 };

	D_ALLOC_PTR(ctx);
	init_drpc_progress_context(ctx, new_drpc_with_fd(listener_fd));
	add_sessions_to_drpc_progress_context(ctx, session_fds, 1);

	drpc_progress_context_close(ctx); /* should clean up everything */

	/* listener and session should have been closed */
	assert_int_equal(close_call_count, 2);
}

static void
test_drpc_progress_context_close_with_multi_session(void **state)
{
	struct drpc_progress_context	*ctx;
	int				listener_fd = 29;
	int				session_fds[] = { 2, 3, 4 };
	int				num_sessions = 3;

	D_ALLOC_PTR(ctx);
	init_drpc_progress_context(ctx, new_drpc_with_fd(listener_fd));
	add_sessions_to_drpc_progress_context(ctx, session_fds, num_sessions);

	drpc_progress_context_close(ctx); /* should clean up everything */

	/* listener and all sessions should have been closed */
	assert_int_equal(close_call_count, num_sessions + 1);
}

/* Convenience macros for unit tests */
#define DRPC_UTEST(x)	cmocka_unit_test_setup_teardown(x,	\
		drpc_progress_test_setup, drpc_progress_test_teardown)

int
main(void)
{
	const struct CMUnitTest tests[] = {
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
		DRPC_UTEST(test_drpc_progress_single_session_bad_call),
		DRPC_UTEST(test_drpc_progress_single_session_success),
		DRPC_UTEST(test_drpc_progress_session_cleanup_if_recv_fails),
		DRPC_UTEST(test_drpc_progress_session_fails_if_no_data),
		DRPC_UTEST(test_drpc_progress_session_cleanup_if_pollerr),
		DRPC_UTEST(test_drpc_progress_session_cleanup_if_pollhup),
		DRPC_UTEST(test_drpc_progress_session_cleanup_if_ult_fails),
		DRPC_UTEST(test_drpc_progress_listener_fails_if_pollerr),
		DRPC_UTEST(test_drpc_progress_listener_fails_if_pollhup),
		DRPC_UTEST(test_drpc_progress_context_create_with_bad_input),
		DRPC_UTEST(test_drpc_progress_context_create_success),
		DRPC_UTEST(test_drpc_progress_context_close_with_null),
		DRPC_UTEST(test_drpc_progress_context_close_with_listener_only),
		DRPC_UTEST(test_drpc_progress_context_close_with_one_session),
		DRPC_UTEST(test_drpc_progress_context_close_with_multi_session)
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

#undef DRPC_UTEST
