/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * Simplified dRPC listener implementation for testing integration of the dRPC communications stack
 * with cmocka.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <pthread.h>
#include <abt.h>
#include <sys/wait.h>
#include <signal.h>
#include <daos/tests_lib.h>
#include <daos/drpc_test.pb-c.h>
#include "drpc_test_listener.h"
#include "../drpc_internal.h"

#define GREETING_STR "Hello"

char *
get_greeting(const char *name)
{
	char *greeting = NULL;

	/* Greeting takes the form: "Hello name" */
	D_ASPRINTF(greeting, "%s %s", GREETING_STR, name);

	return greeting;
}

static void
hello_handler(Drpc__Call *call, Drpc__Response *resp)
{
	Hello__Hello        *hello_req  = NULL;
	Hello__HelloResponse hello_resp = HELLO__HELLO_RESPONSE__INIT;
	struct drpc_alloc    alloc      = PROTO_ALLOCATOR_INIT(alloc);

	assert_int_equal(HELLO__MODULE__HELLO, call->module);
	assert_int_equal(HELLO__FUNCTION__GREETING, call->method);

	D_PRINT("hello_handler: unpacking request\n");
	hello_req = hello__hello__unpack(&alloc.alloc, call->body.len, call->body.data);
	assert_non_null(hello_req);

	D_PRINT("hello_handler: got name with length=%lu\n", strlen(hello_req->name));
	hello_resp.greeting = get_greeting(hello_req->name);

	D_PRINT("hello_handler: packing response\n");
	resp->body.len = hello__hello_response__get_packed_size(&hello_resp);
	D_ALLOC(resp->body.data, resp->body.len);
	assert_non_null(resp->body.data);
	hello__hello_response__pack(&hello_resp, resp->body.data);

	D_FREE(hello_resp.greeting);
	hello__hello__free_unpacked(hello_req, &alloc.alloc);
}

static void
drpc_test_state_free(struct drpc_test_state *dts)
{
	D_FREE(dts->sock_path);
	D_FREE(dts->test_dir);
	D_FREE(dts);
}

static bool
is_listening(struct drpc_test_state *dts)
{
	bool result;

	pthread_mutex_lock(&dts->listener_running_mutex);
	result = dts->listener_running;
	pthread_mutex_unlock(&dts->listener_running_mutex);

	return result;
}

static void
set_listening(struct drpc_test_state *dts, bool listening)
{
	pthread_mutex_lock(&dts->listener_running_mutex);
	dts->listener_running = listening;
	pthread_mutex_unlock(&dts->listener_running_mutex);
}

static void *
run_test_listener(void *arg)
{
	struct drpc_test_state *dts = arg;
	int                     rc  = 0;

	while (is_listening(dts)) {
		D_PRINT("dRPC listener loop\n");
		rc = drpc_progress(dts->progress_ctx, 500);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			D_PRINT_ERR("drpc_progress failed: " DF_RC "\n", DP_RC(rc));
			break;
		}
	}

	pthread_exit(0);
	return NULL;
}

/*
 * Simplified implementation for testing.
 * The engine module depends on nearly the entire codebase -- which isn't necessary to test
 * simple dRPC communications functionality.
 * NB: This test implementation assumes a single-threaded client. I.e. drpc_progress does not have
 * to track multiple sessions.
 */
int
dss_ult_create(void (*func)(void *), void *arg, int xs_type, int tgt_idx, size_t stack_size,
	       ABT_thread *ult)
{
	func(arg);
	return 0;
}

static int
create_listener_thread(struct drpc_test_state *dts, void *(*func)(void *))
{
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	pthread_mutex_init(&dts->listener_running_mutex, NULL);

	D_PRINT("starting listener pthread\n");
	if (pthread_create(&dts->listener_thread, &attr, func, (void *)dts) < 0) {
		D_PRINT_ERR("test pthread create failed: %s\n", strerror(errno));
		return -DER_MISC;
	}

	return 0;
}

static int
start_drpc_listener(struct drpc_test_state *state)
{
	struct drpc *ctx;
	int          rc;

	ctx = drpc_listen(state->sock_path, hello_handler);
	assert_non_null(ctx);

	state->progress_ctx = drpc_progress_context_create(ctx);
	if (state->progress_ctx == NULL) {
		D_PRINT_ERR("failed to create progress context\n");
		drpc_close(ctx);
		return -DER_MISC;
	}

	set_listening(state, true);

	D_PRINT("kicking off listener\n");
	rc = create_listener_thread(state, run_test_listener);
	if (rc != 0) {
		D_PRINT_ERR("Failed to create listener process\n");
		drpc_progress_context_close(state->progress_ctx);
		return rc;
	}

	return 0;
}

static void
stop_drpc_listener(struct drpc_test_state *state)
{
	set_listening(state, false);
	pthread_join(state->listener_thread, NULL);
}

int
drpc_listener_setup(void **state)
{
	struct drpc_test_state *dts;
	const char             *sock_name = "test.sock";
	char *template;
	int rc = 0;

	D_PRINT("LISTENER SETUP: start\n");
	assert_non_null(state);

	D_ALLOC_PTR(dts);
	assert_non_null(dts);

	D_ASPRINTF(template, "/tmp/drpc_test.XXXXXX");
	dts->test_dir = mkdtemp(template);
	assert_non_null(dts->test_dir);
	D_PRINT("created test directory: %s\n", dts->test_dir);

	D_ASPRINTF(dts->sock_path, "%s/%s", dts->test_dir, sock_name);
	assert_non_null(dts->sock_path);

	D_PRINT("setting up dRPC listener at socket path: %s\n", dts->sock_path);
	rc = start_drpc_listener(dts);

	*state = dts;

	D_PRINT("LISTENER SETUP: done\n");
	return rc;
}

int
drpc_listener_teardown(void **state)
{
	struct drpc_test_state *dts;
	bool                    errored = false;

	D_PRINT("LISTENER TEARDOWN: start\n");

	assert_non_null(state);
	dts = *state;

	stop_drpc_listener(dts);

	drpc_progress_context_close(dts->progress_ctx);

	if (remove(dts->sock_path) < 0) {
		D_PRINT_ERR("failed to remove test socket %s: %s\n", dts->sock_path,
			    strerror(errno));
		errored = true;
	}

	if (rmdir(dts->test_dir) < 0) {
		D_PRINT_ERR("failed to remove test dir %s: %s\n", dts->test_dir, strerror(errno));
		errored = true;
	}

	drpc_test_state_free(dts);
	*state = NULL;

	D_PRINT("LISTENER TEARDOWN: done\n");

	if (errored)
		return -1;

	return 0;
}
