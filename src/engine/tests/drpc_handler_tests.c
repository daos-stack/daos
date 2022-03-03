/*
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * Unit tests for the drpc handler registration system
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos/drpc.pb-c.h>
#include <daos/drpc_modules.h>
#include <daos/tests_lib.h>
#include <daos/test_mocks.h>
#include <daos/test_utils.h>
#include "../drpc_handler.h"

/*
 * Some dummy handlers so we have different ptrs for each test registration
 */
static void
dummy_drpc_handler1(Drpc__Call *request, Drpc__Response *response)
{
}

static void
dummy_drpc_handler2(Drpc__Call *request, Drpc__Response *response)
{
}

static void
dummy_drpc_handler3(Drpc__Call *request, Drpc__Response *response)
{
}

static void
dummy_drpc_handler4(Drpc__Call *request, Drpc__Response *response)
{
}

#define NUM_TEST_HANDLERS	4
static drpc_handler_t handler_funcs[NUM_TEST_HANDLERS] = {
		dummy_drpc_handler1,
		dummy_drpc_handler2,
		dummy_drpc_handler3,
		dummy_drpc_handler4
};

/*
 * Helper functions used by unit tests
 */

static struct dss_drpc_handler *
create_handler_list(int num_items)
{
	struct dss_drpc_handler	*list;
	int			i;

	D_ASSERT(num_items <= NUM_TEST_HANDLERS);

	D_ALLOC_ARRAY(list, num_items + 1);

	for (i = 0; i < num_items; i++) {
		list[i].module_id = i;
		list[i].handler = handler_funcs[i];
	}

	return list;
}

static void
destroy_handler_list(struct dss_drpc_handler *list)
{
	D_FREE(list);
}

/*
 * Test setup and teardown
 * Initializes and destroys the registry by default.
 */
static int
drpc_hdlr_test_setup(void **state)
{
	mock_drpc_handler_setup();

	return drpc_hdlr_init();
}

static int
drpc_hdlr_test_teardown(void **state)
{
	mock_drpc_handler_teardown();

	return drpc_hdlr_fini();
}

/*
 * Registration unit tests
 */
static void
drpc_hdlr_register_with_null_handler(void **state)
{
	assert_rc_equal(drpc_hdlr_register(0, NULL), -DER_INVAL);
}

static void
drpc_hdlr_register_with_good_handler(void **state)
{
	assert_rc_equal(drpc_hdlr_register(DRPC_MODULE_TEST,
					   dummy_drpc_handler1), DER_SUCCESS);

	assert_ptr_equal(drpc_hdlr_get_handler(DRPC_MODULE_TEST),
			 dummy_drpc_handler1);
}

static void
drpc_hdlr_register_same_id_twice(void **state)
{
	assert_rc_equal(drpc_hdlr_register(DRPC_MODULE_TEST,
					   dummy_drpc_handler1), DER_SUCCESS);
	assert_rc_equal(drpc_hdlr_register(DRPC_MODULE_TEST,
					   dummy_drpc_handler2), -DER_EXIST);

	/* Should be unchanged */
	assert_ptr_equal(drpc_hdlr_get_handler(0), dummy_drpc_handler1);
}

static void
drpc_hdlr_register_null_handler_after_good_one(void **state)
{
	assert_rc_equal(drpc_hdlr_register(DRPC_MODULE_TEST,
					   dummy_drpc_handler1), DER_SUCCESS);
	assert_rc_equal(drpc_hdlr_register(DRPC_MODULE_TEST, NULL),
			-DER_INVAL);

	/* Should be unchanged */
	assert_ptr_equal(drpc_hdlr_get_handler(DRPC_MODULE_TEST),
			 dummy_drpc_handler1);
}

static void
drpc_hdlr_register_bad_module_id(void **state)
{
	assert_rc_equal(drpc_hdlr_register(NUM_DRPC_MODULES,
					   dummy_drpc_handler2), -DER_INVAL);
	assert_rc_equal(drpc_hdlr_register(-1,
					   dummy_drpc_handler2), -DER_INVAL);
}

static void
drpc_hdlr_get_handler_with_unregistered_id(void **state)
{
	drpc_hdlr_register(DRPC_MODULE_TEST, dummy_drpc_handler1);

	assert_ptr_equal(drpc_hdlr_get_handler(DRPC_MODULE_TEST + 1),
			 NULL);
}

static void
drpc_hdlr_get_handler_with_invalid_id(void **state)
{
	assert_ptr_equal(drpc_hdlr_get_handler(NUM_DRPC_MODULES),
			 NULL);
}

static void
drpc_hdlr_register_multiple(void **state)
{
	assert_rc_equal(drpc_hdlr_register(DRPC_MODULE_TEST,
					   dummy_drpc_handler1), DER_SUCCESS);
	assert_rc_equal(drpc_hdlr_register(DRPC_MODULE_SEC_AGENT,
					   dummy_drpc_handler2), DER_SUCCESS);
	assert_rc_equal(drpc_hdlr_register(DRPC_MODULE_MGMT,
					   dummy_drpc_handler3), DER_SUCCESS);
	assert_rc_equal(drpc_hdlr_register(DRPC_MODULE_SRV,
					   dummy_drpc_handler4), DER_SUCCESS);

	assert_ptr_equal(drpc_hdlr_get_handler(DRPC_MODULE_TEST),
			 dummy_drpc_handler1);
	assert_ptr_equal(drpc_hdlr_get_handler(DRPC_MODULE_SEC_AGENT),
			 dummy_drpc_handler2);
	assert_ptr_equal(drpc_hdlr_get_handler(DRPC_MODULE_MGMT),
			 dummy_drpc_handler3);
	assert_ptr_equal(drpc_hdlr_get_handler(DRPC_MODULE_SRV),
			 dummy_drpc_handler4);
}

static void
drpc_hdlr_unregister_id_not_found(void **state)
{
	drpc_hdlr_register(DRPC_MODULE_TEST, dummy_drpc_handler1);

	/*
	 * It is already unregistered - We did nothing but the caller is
	 * satisfied.
	 */
	assert_rc_equal(drpc_hdlr_unregister(DRPC_MODULE_SEC_AGENT),
			DER_SUCCESS);

	/* Ensure nothing was deleted */
	assert_non_null(drpc_hdlr_get_handler(DRPC_MODULE_TEST));
}

static void
drpc_hdlr_unregister_bad_module_id(void **state)
{
	assert_rc_equal(drpc_hdlr_unregister(NUM_DRPC_MODULES),
			-DER_INVAL);
}

static void
drpc_hdlr_unregister_success(void **state)
{
	drpc_hdlr_register(DRPC_MODULE_TEST, dummy_drpc_handler1);
	drpc_hdlr_register(DRPC_MODULE_SEC_AGENT, dummy_drpc_handler2);

	assert_rc_equal(drpc_hdlr_unregister(DRPC_MODULE_TEST),
			DER_SUCCESS);

	/* Ensure only the correct item was deleted */
	assert_null(drpc_hdlr_get_handler(DRPC_MODULE_TEST));
	assert_non_null(drpc_hdlr_get_handler(DRPC_MODULE_SEC_AGENT));
}

static void
drpc_hdlr_register_all_with_null(void **state)
{
	assert_rc_equal(drpc_hdlr_register_all(NULL), DER_SUCCESS);
}

static void
drpc_hdlr_register_all_with_empty_list(void **state)
{
	struct dss_drpc_handler *empty = create_handler_list(0);

	assert_rc_equal(drpc_hdlr_register_all(empty), DER_SUCCESS);

	destroy_handler_list(empty);
}

static void
drpc_hdlr_register_all_with_one_item(void **state)
{
	struct dss_drpc_handler *handlers = create_handler_list(1);

	assert_rc_equal(drpc_hdlr_register_all(handlers), DER_SUCCESS);

	assert_ptr_equal(drpc_hdlr_get_handler(DRPC_MODULE_TEST),
			 handlers[DRPC_MODULE_TEST].handler);

	destroy_handler_list(handlers);
}

static void
drpc_hdlr_register_all_with_multiple_items(void **state)
{
	int			num_items = NUM_TEST_HANDLERS;
	int			i;
	struct dss_drpc_handler	*handlers = create_handler_list(num_items);

	assert_rc_equal(drpc_hdlr_register_all(handlers), DER_SUCCESS);

	for (i = 0; i < num_items; i++) {
		assert_ptr_equal(drpc_hdlr_get_handler(i),
				 handlers[i].handler);
	}

	destroy_handler_list(handlers);
}

static void
drpc_hdlr_register_all_with_duplicate(void **state)
{
	int			num_items = NUM_TEST_HANDLERS;
	int			dup_idx = num_items - 1;
	int			i;
	struct dss_drpc_handler	*dup_list = create_handler_list(num_items);

	/* Make one of them a duplicate module ID */
	dup_list[dup_idx].module_id = DRPC_MODULE_TEST;

	assert_rc_equal(drpc_hdlr_register_all(dup_list), -DER_EXIST);

	/* Should have registered all the ones we could */
	for (i = 0; i < num_items; i++) {
		if (i != dup_idx) { /* dup is the one that fails */
			assert_ptr_equal(drpc_hdlr_get_handler(i),
					 dup_list[i].handler);
		}
	}

	destroy_handler_list(dup_list);
}

static void
drpc_hdlr_unregister_all_with_null(void **state)
{
	assert_rc_equal(drpc_hdlr_unregister_all(NULL), DER_SUCCESS);
}

static void
drpc_hdlr_unregister_all_with_empty_list(void **state)
{
	struct dss_drpc_handler *empty = create_handler_list(0);

	assert_rc_equal(drpc_hdlr_unregister_all(empty), DER_SUCCESS);

	destroy_handler_list(empty);
}

static void
drpc_hdlr_unregister_all_with_one_item(void **state)
{
	struct dss_drpc_handler *handlers = create_handler_list(1);

	/* Register them first */
	drpc_hdlr_register_all(handlers);

	assert_rc_equal(drpc_hdlr_unregister_all(handlers), DER_SUCCESS);

	/* Make sure it was unregistered */
	assert_null(drpc_hdlr_get_handler(handlers[0].module_id));

	destroy_handler_list(handlers);
}

static void
drpc_hdlr_unregister_all_with_multiple_items(void **state)
{
	int			num_items = NUM_TEST_HANDLERS;
	int			i;
	struct dss_drpc_handler	*handlers = create_handler_list(num_items);

	/* Register them first */
	drpc_hdlr_register_all(handlers);

	assert_rc_equal(drpc_hdlr_unregister_all(handlers), DER_SUCCESS);

	/* Make sure they were all unregistered */
	for (i = 0; i < num_items; i++) {
		assert_null(drpc_hdlr_get_handler(handlers[i].module_id));
	}

	destroy_handler_list(handlers);
}

static void
drpc_hdlr_process_msg_success(void **state)
{
	Drpc__Call	*request = new_drpc_call();
	Drpc__Response	*resp = new_drpc_response();

	/*
	 * Make sure we have our mock registered as the handler for this msg.
	 * It should be called by drpc_hdlr_process_msg()
	 */
	drpc_hdlr_register(request->module, mock_drpc_handler);

	drpc_hdlr_process_msg(request, resp);

	/* correct params passed down to the registered handler */
	assert_int_equal(mock_drpc_handler_call_count, 1);
	assert_int_equal(mock_drpc_handler_call->module, request->module);
	assert_int_equal(mock_drpc_handler_call->method, request->method);
	assert_int_equal(mock_drpc_handler_call->sequence, request->sequence);
	assert_int_equal(mock_drpc_handler_call->body.len, request->body.len);
	assert_ptr_equal(mock_drpc_handler_resp_ptr, resp);

	/* Got back a copy of the mocked response */
	assert_int_equal(resp->sequence,
			 mock_drpc_handler_resp_return->sequence);
	assert_int_equal(resp->status, mock_drpc_handler_resp_return->status);
	assert_int_equal(resp->body.len,
			 mock_drpc_handler_resp_return->body.len);

	drpc__call__free_unpacked(request, NULL);
	drpc__response__free_unpacked(resp, NULL);
}

static void
drpc_hdlr_process_msg_unregistered_module(void **state)
{
	Drpc__Response	*resp = new_drpc_response();
	Drpc__Call	*request = new_drpc_call();

	/*
	 * Mock is registered for a different module...
	 */
	drpc_hdlr_register(request->module + 1, mock_drpc_handler);

	drpc_hdlr_process_msg(request, resp);

	/* handler wasn't called */
	assert_int_equal(mock_drpc_handler_call_count, 0);

	/* Response should indicate no handler for the call */
	assert_int_equal(resp->status, DRPC__STATUS__UNKNOWN_MODULE);

	drpc__call__free_unpacked(request, NULL);
	drpc__response__free_unpacked(resp, NULL);
}

/*
 * Tests for when the registry table is uninitialized.
 * Don't use the standard setup/teardown functions with these.
 */
static void
drpc_hdlr_register_uninitialized(void **state)
{
	assert_rc_equal(drpc_hdlr_register(0, dummy_drpc_handler1),
			-DER_UNINIT);
}

static void
drpc_hdlr_get_handler_uninitialized(void **state)
{
	assert_ptr_equal(drpc_hdlr_get_handler(0), NULL);
}

static void
drpc_hdlr_unregister_uninitialized(void **state)
{
	assert_rc_equal(drpc_hdlr_unregister(0), -DER_UNINIT);
}

static void
drpc_hdlr_register_all_uninitialized(void **state)
{
	struct dss_drpc_handler *list = create_handler_list(0);

	assert_rc_equal(drpc_hdlr_register_all(list), -DER_UNINIT);

	destroy_handler_list(list);
}

static void
drpc_hdlr_unregister_all_uninitialized(void **state)
{
	struct dss_drpc_handler *list = create_handler_list(0);

	assert_rc_equal(drpc_hdlr_unregister_all(list), -DER_UNINIT);

	destroy_handler_list(list);
}

/* Convenience macros for unit tests */
#define UTEST(x)	cmocka_unit_test_setup_teardown(x,	\
				drpc_hdlr_test_setup,		\
				drpc_hdlr_test_teardown)
#define UTEST_NO_INIT(x)	cmocka_unit_test(x)

int
main(void)
{
	const struct CMUnitTest tests[] = {
		UTEST(drpc_hdlr_register_with_null_handler),
		UTEST(drpc_hdlr_register_with_good_handler),
		UTEST(drpc_hdlr_register_same_id_twice),
		UTEST(drpc_hdlr_register_null_handler_after_good_one),
		UTEST(drpc_hdlr_register_bad_module_id),
		UTEST(drpc_hdlr_get_handler_with_unregistered_id),
		UTEST(drpc_hdlr_get_handler_with_invalid_id),
		UTEST(drpc_hdlr_register_multiple),
		UTEST(drpc_hdlr_unregister_id_not_found),
		UTEST(drpc_hdlr_unregister_bad_module_id),
		UTEST(drpc_hdlr_unregister_success),
		UTEST(drpc_hdlr_register_all_with_null),
		UTEST(drpc_hdlr_register_all_with_empty_list),
		UTEST(drpc_hdlr_register_all_with_one_item),
		UTEST(drpc_hdlr_register_all_with_multiple_items),
		UTEST(drpc_hdlr_register_all_with_duplicate),
		UTEST(drpc_hdlr_unregister_all_with_null),
		UTEST(drpc_hdlr_unregister_all_with_empty_list),
		UTEST(drpc_hdlr_unregister_all_with_one_item),
		UTEST(drpc_hdlr_unregister_all_with_multiple_items),
		UTEST(drpc_hdlr_process_msg_success),
		UTEST(drpc_hdlr_process_msg_unregistered_module),

		/* Uninitialized cases */
		UTEST_NO_INIT(drpc_hdlr_register_uninitialized),
		UTEST_NO_INIT(drpc_hdlr_get_handler_uninitialized),
		UTEST_NO_INIT(drpc_hdlr_unregister_uninitialized),
		UTEST_NO_INIT(drpc_hdlr_register_all_uninitialized),
		UTEST_NO_INIT(drpc_hdlr_unregister_all_uninitialized)
	};

	return cmocka_run_group_tests_name("engine_drpc_handler",
					   tests, NULL, NULL);
}

#undef UTEST_NO_INIT
#undef UTEST
