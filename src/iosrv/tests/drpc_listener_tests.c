/*
 * (C) Copyright 2019-2020 Intel Corporation.
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
 * Unit tests for the drpc_listener thread
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "../drpc_internal.h"
#include <daos/test_mocks.h>
#include <daos/test_utils.h>

/*
 * Mocks of DAOS internals
 */

/* Location of the socket - arbitrary, these tests don't create a real one */
const char *dss_socket_dir = "/my/fake/path";

static int dss_ult_create_return;
static void (*dss_ult_create_func)(void *); /* saved fcn ptr */
static void *dss_ult_create_arg_ptr; /* saved ptr addr */
static int dss_ult_create_stream_id; /* saved input */
static size_t dss_ult_create_stack_size; /* saved input */
static ABT_thread *dss_ult_create_ult_ptr; /* saved ptr addr */
int
dss_ult_create(void (*func)(void *), void *arg, int ult_type, int tgt_id,
		size_t stack_size, ABT_thread *ult)
{
	dss_ult_create_func = func;
	dss_ult_create_arg_ptr = arg;
	dss_ult_create_stream_id = tgt_id;
	dss_ult_create_stack_size = stack_size;
	dss_ult_create_ult_ptr = ult;

	return dss_ult_create_return;
}

static void
mock_dss_ult_create_setup(void)
{
	dss_ult_create_return = 0;
	dss_ult_create_func = NULL;
	dss_ult_create_arg_ptr = NULL;
	dss_ult_create_stream_id = -1;
	dss_ult_create_stack_size = -1;
	dss_ult_create_ult_ptr = NULL;
}

static struct drpc_progress_context *drpc_progress_context_create_return;
static struct drpc *drpc_progress_context_create_listener_ptr; /* saved ptr */
static int drpc_progress_context_create_listener_fd; /* saved fd value */
static drpc_handler_t drpc_progress_context_create_listener_handler; /* saved */
struct drpc_progress_context *
drpc_progress_context_create(struct drpc *listener)
{
	drpc_progress_context_create_listener_ptr = listener;
	if (listener != NULL) {
		drpc_progress_context_create_listener_fd = listener->comm->fd;
		drpc_progress_context_create_listener_handler =
				listener->handler;
	}
	return drpc_progress_context_create_return;
}

static void
mock_drpc_progress_context_create_setup()
{
	D_ALLOC_PTR(drpc_progress_context_create_return);
	drpc_progress_context_create_listener_ptr = NULL;
	drpc_progress_context_create_listener_fd = 0;
	drpc_progress_context_create_listener_handler = NULL;
}

static void
mock_drpc_progress_context_create_teardown()
{
	D_FREE(drpc_progress_context_create_return);

	/* if non-null, listener was allocated by drpc_listen */
	free_drpc(drpc_progress_context_create_listener_ptr);
	drpc_progress_context_create_listener_ptr = NULL;
}

static struct drpc_progress_context *drpc_progress_context_close_ctx_ptr;
void
drpc_progress_context_close(struct drpc_progress_context *ctx)
{
	drpc_progress_context_close_ctx_ptr = ctx;
}

/*
 * Stubs - just needed to make it build
 */

int
drpc_progress(struct drpc_progress_context *ctx, int timeout_ms)
{
	return 0;
}

drpc_handler_t
drpc_hdlr_get_handler(int module_id)
{
	return NULL;
}

void
drpc_hdlr_process_msg(Drpc__Call *request, Drpc__Response **resp)
{
}

int
ABT_thread_yield(void)
{
	return 0;
}

/*
 * Test setup and teardown
 */
static int
drpc_listener_test_setup(void **state)
{
	mock_socket_setup();
	mock_bind_setup();
	mock_fcntl_setup();
	mock_listen_setup();
	mock_unlink_setup();
	mock_ABT_mutex_create_setup();
	mock_ABT_mutex_free_setup();
	mock_ABT_thread_join_setup();
	mock_ABT_thread_free_setup();

	mock_drpc_progress_context_create_setup();
	mock_dss_ult_create_setup();

	drpc_progress_context_close_ctx_ptr = NULL;

	unlink_call_count = 0;
	unlink_name = NULL;

	return 0;
}

static int
drpc_listener_test_teardown(void **state)
{
	mock_drpc_progress_context_create_teardown();
	D_FREE(drpc_listener_socket_path); /* may not be freed by tests */

	return 0;
}

/*
 * Unit tests
 */

static void
test_drpc_listener_init_cant_create_socket(void **state)
{
	socket_return = -1; /* Make the drpc_listen call fail */

	assert_int_equal(drpc_listener_init(), -DER_MISC);
}

static void
test_drpc_listener_init_success(void **state)
{
	char expected_socket_path[256];

	assert_int_equal(drpc_listener_init(), DER_SUCCESS);

	/* Created a valid mutex */
	assert_non_null(ABT_mutex_create_newmutex_ptr);

	/* Initialized unique socket path based on PID */
	assert_non_null(drpc_listener_socket_path);
	snprintf(expected_socket_path, sizeof(expected_socket_path),
		 "%s/daos_io_server_%d.sock", dss_socket_dir, getpid());
	assert_string_equal(drpc_listener_socket_path, expected_socket_path);

	/* called unlink on socket */
	assert_int_equal(unlink_call_count, 1);
	assert_string_equal(unlink_name, drpc_listener_socket_path);

	/*
	 * Set up the listening socket - drpc_listen is deeply tested elsewhere
	 */
	assert_int_equal(listen_sockfd, socket_return);

	/*
	 * Created a drpc_progress_context using the listener with top-level
	 * handler
	 */
	assert_non_null(drpc_progress_context_create_listener_ptr);
	assert_int_equal(drpc_progress_context_create_listener_fd,
			listen_sockfd);
	assert_ptr_equal(drpc_progress_context_create_listener_handler,
			drpc_hdlr_process_msg);

	/* Created a ULT on xstream 0 */
	assert_non_null(dss_ult_create_func);
	assert_ptr_equal(dss_ult_create_arg_ptr, /* passed in progress ctx */
			drpc_progress_context_create_return);
	assert_int_equal(dss_ult_create_stream_id, 0); /* xstream 0 */
	assert_int_equal(dss_ult_create_stack_size, 0); /* auto */
	assert_non_null(dss_ult_create_ult_ptr);
}

static void
test_drpc_listener_init_cant_create_prog_ctx(void **state)
{
	/* drpc_progress_context_create returns null */
	mock_drpc_progress_context_create_teardown();

	assert_int_equal(drpc_listener_init(), -DER_NOMEM);

	/*
	 * Listener should have been freed.
	 * Don't leave invalid ptr to be double-freed by teardown.
	 */
	drpc_progress_context_create_listener_ptr = NULL;
}

static void
test_drpc_listener_init_cant_create_mutex(void **state)
{
	ABT_mutex_create_return = ABT_ERR_MEM;

	assert_int_equal(drpc_listener_init(), -DER_NOMEM);
}

static void
test_drpc_listener_init_cant_create_ult(void **state)
{
	dss_ult_create_return = -DER_MISC;

	assert_int_equal(drpc_listener_init(), dss_ult_create_return);

	/* Context that was created was closed after ULT failed */
	assert_ptr_equal(drpc_progress_context_close_ctx_ptr,
			drpc_progress_context_create_return);
}

static void
test_drpc_listener_fini_success(void **state)
{
	assert_int_equal(drpc_listener_fini(), DER_SUCCESS);

	/* Joined ABT thread */
	assert_int_equal(ABT_thread_join_call_count, 1);

	/* Freed ABT thread */
	assert_non_null(ABT_thread_free_thread_ptr);

	/* Freed mutex */
	assert_non_null(ABT_mutex_free_mutex_ptr);
}

static void
test_drpc_listener_fini_cant_join_thread(void **state)
{
	ABT_thread_join_return = ABT_ERR_INV_THREAD;

	assert_int_equal(drpc_listener_fini(), -DER_INVAL);
}

static void
test_drpc_listener_fini_cant_free_thread(void **state)
{
	ABT_thread_free_return = ABT_ERR_INV_THREAD;

	assert_int_equal(drpc_listener_fini(), -DER_INVAL);
}

static void
test_drpc_listener_fini_cant_free_mutex(void **state)
{
	ABT_mutex_free_return = ABT_ERR_INV_MUTEX;

	assert_int_equal(drpc_listener_fini(), -DER_INVAL);
}

/* Convenience macros for unit tests */
#define UTEST(x)	cmocka_unit_test_setup_teardown(x,	\
				drpc_listener_test_setup,	\
				drpc_listener_test_teardown)

int
main(void)
{
	const struct CMUnitTest tests[] = {
		UTEST(test_drpc_listener_init_cant_create_socket),
		UTEST(test_drpc_listener_init_success),
		UTEST(test_drpc_listener_init_cant_create_prog_ctx),
		UTEST(test_drpc_listener_init_cant_create_mutex),
		UTEST(test_drpc_listener_init_cant_create_ult),
		UTEST(test_drpc_listener_fini_success),
		UTEST(test_drpc_listener_fini_cant_join_thread),
		UTEST(test_drpc_listener_fini_cant_free_thread),
		UTEST(test_drpc_listener_fini_cant_free_mutex),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

#undef UTEST
