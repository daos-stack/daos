/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * Simple dRPC integration tests
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <daos_errno.h>
#include <daos/drpc_types.h>
#include <daos/drpc.h>
#include <daos/tests_lib.h>
#include <daos/test_utils.h>
#include "drpc_test_listener.h"
#include <daos/drpc_test.pb-c.h>

#define CHUNK_SIZE (1 << 17) /* dRPC chunk size = 1 MB */

static int
init_logging(void)
{
	int rc;

	rc = d_log_init();
	if (rc != 0) {
		D_PRINT_ERR("failed d_log_init: " DF_RC "\n", DP_RC(rc));
		return rc;
	}

	rc = D_LOG_REGISTER_FAC(DAOS_FOREACH_LOG_FAC);
	if (rc != 0)
		D_PRINT_ERR("Failed to register daos log facilities: " DF_RC "\n", DP_RC(rc));

	rc = D_LOG_REGISTER_DB(DAOS_FOREACH_DB);
	if (rc != 0)
		D_PRINT_ERR("Failed to register daos debug bits: " DF_RC "\n", DP_RC(rc));

	d_log_sync_mask();

	return 0;
}

static int
test_suite_init(void **arg)
{
	return init_logging();
}

static int
test_suite_fini(void **arg)
{
	d_log_fini();

	return 0;
}

static void
run_hello_test(void **state, char *name)
{
	struct drpc_test_state *dts;
	struct drpc_alloc       alloc      = PROTO_ALLOCATOR_INIT(alloc);
	struct drpc            *ctx        = NULL;
	Drpc__Call             *call       = NULL;
	Drpc__Response         *resp       = NULL;
	Hello__Hello            hello_req  = HELLO__HELLO__INIT;
	Hello__HelloResponse   *hello_resp = NULL;
	char                   *exp_greeting;
	int                     rc;

	assert_non_null(state);
	dts = *state;

	D_PRINT("initializing dRPC connection on socket %s\n", dts->sock_path);
	rc = drpc_connect(dts->sock_path, &ctx);
	assert_rc_equal(rc, 0);

	D_PRINT("initializing dRPC call\n");
	rc = drpc_call_create(ctx, HELLO__MODULE__HELLO, HELLO__FUNCTION__GREETING, &call);
	assert_rc_equal(rc, 0);

	hello_req.name = name;

	call->body.len = hello__hello__get_packed_size(&hello_req);
	D_PRINT("serializing message with size=%lu into dRPC call\n", call->body.len);

	D_ALLOC(call->body.data, call->body.len);
	assert_non_null(call->body.data);
	hello__hello__pack(&hello_req, call->body.data);

	D_PRINT("calling dRPC server\n");
	rc = drpc_call(ctx, R_SYNC, call, &resp);
	assert_rc_equal(rc, 0);
	assert_int_equal(resp->status, DRPC__STATUS__SUCCESS);

	D_PRINT("verifying response\n");
	exp_greeting = get_greeting(name);
	hello_resp   = hello__hello_response__unpack(&alloc.alloc, resp->body.len, resp->body.data);
	assert_non_null(hello_resp);
	assert_string_equal(hello_resp->greeting, exp_greeting);

	D_FREE(exp_greeting);
	hello__hello_response__free_unpacked(hello_resp, &alloc.alloc);
	drpc_call_free(call);
	drpc_response_free(resp);

	drpc_close(ctx);
}

static void
test_drpc_basic(void **state)
{
	run_hello_test(state, "Bilbo");
}

static char *
gen_str(size_t len)
{
	char  *str;
	char   letter = 'a';
	size_t i;

	D_ALLOC(str, len);
	assert_non_null(str);

	for (i = 0; i < (len - 1); i++) {
		str[i] = letter;
		if (letter == 'z')
			letter = 'a';
		else
			letter++;
	}
	str[len - 1] = '\0';
	return str;
}

static void
test_drpc_long_single_chunk(void **state)
{
	char *name;

	name = gen_str(CHUNK_SIZE / 2);
	run_hello_test(state, name);
	D_FREE(name);
}

static void
test_drpc_chunked(void **state)
{
	char *name;

	name = gen_str(CHUNK_SIZE);
	run_hello_test(state, name);
	D_FREE(name);
}

#define DRPC_COMM_TEST(X)                                                                          \
	cmocka_unit_test_setup_teardown(X, drpc_listener_setup, drpc_listener_teardown)
int
main(void)
{
	const struct CMUnitTest tests[] = {
	    DRPC_COMM_TEST(test_drpc_basic),
	    DRPC_COMM_TEST(test_drpc_long_single_chunk),
	    DRPC_COMM_TEST(test_drpc_chunked),
	};
	return cmocka_run_group_tests_name("drpc_comms", tests, test_suite_init, test_suite_fini);
}

#undef DRPC_COMM_TEST
