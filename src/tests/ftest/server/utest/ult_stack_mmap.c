/*
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This tests argobots ULT mmap()'ed stack
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <abt.h>

#include <gurt/common.h>
#include <daos/common.h>
#include <gurt/common.h>
#include <daos/daos_abt.h>
#include <daos/ult_stack_mmap.h>

static int
init_tests(void **state)
{
	int rc;

	rc = d_log_init();
	assert_int_equal(rc, 0);

	return 0;
}

static int
fini_tests(void **state)
{
	d_log_fini();

	return 0;
}

static void
abt_hello(void *arg)
{
	char *msg = (char *)arg;

	printf("Hello from mmap ULT argobot: %s\n", msg);
}

static void
test_named_thread_on_xstream(void **state)
{
	ABT_xstream xstream;
	ABT_thread  thread = {0};
	int         rc;

	(void)state; /* unused */

	printf("-- INIT of test --\n");
	rc = da_initialize(0, NULL);
	assert_int_equal(rc, 0);
	rc = ABT_self_get_xstream(&xstream);
	assert_int_equal(rc, ABT_SUCCESS);

	printf("-- START of test --\n");
	rc = da_thread_create_on_xstream(xstream, abt_hello, "test_named_thread_on_xstream",
					 ABT_THREAD_ATTR_NULL, &thread);
	assert_int_equal(rc, ABT_SUCCESS);
	rc = ABT_thread_yield();
	assert_int_equal(rc, ABT_SUCCESS);

	printf("-- END of test --\n");
	rc = ABT_thread_free(&thread);
	assert_int_equal(rc, ABT_SUCCESS);
	da_finalize();
}

static void
test_unnamed_thread_on_xstream(void **state)
{
	ABT_xstream xstream;
	int         rc;

	(void)state; /* unused */

	printf("-- INIT of test --\n");
	rc = da_initialize(0, NULL);
	assert_int_equal(rc, 0);
	rc = ABT_self_get_xstream(&xstream);
	assert_int_equal(rc, ABT_SUCCESS);

	printf("-- START of test --\n");
	rc = da_thread_create_on_xstream(xstream, abt_hello, "test_unnamed_thread_on_xstream",
					 ABT_THREAD_ATTR_NULL, NULL);
	assert_int_equal(rc, ABT_SUCCESS);
	rc = ABT_thread_yield();
	assert_int_equal(rc, ABT_SUCCESS);

	printf("-- END of test --\n");
	da_finalize();
}

static void
test_named_thread_on_pool(void **state)
{
	ABT_pool   pool;
	ABT_thread thread = {0};
	int        rc;

	(void)state; /* unused */

	printf("-- INIT of test --\n");
	rc = da_initialize(0, NULL);
	assert_int_equal(rc, 0);
	rc = ABT_self_get_last_pool(&pool);
	assert_int_equal(rc, ABT_SUCCESS);

	printf("-- START of test --\n");
	rc = da_thread_create_on_pool(pool, abt_hello, "test_named_thread_on_pool",
				      ABT_THREAD_ATTR_NULL, &thread);
	assert_int_equal(rc, ABT_SUCCESS);
	rc = ABT_thread_yield();
	assert_int_equal(rc, ABT_SUCCESS);

	printf("-- END of test --\n");
	rc = ABT_thread_free(&thread);
	assert_int_equal(rc, ABT_SUCCESS);
	da_finalize();
}

static void
test_unnamed_thread_on_pool(void **state)
{
	ABT_pool pool;
	int      rc;

	(void)state; /* unused */

	printf("-- INIT of test --\n");
	rc = da_initialize(0, NULL);
	assert_int_equal(rc, 0);
	rc = ABT_self_get_last_pool(&pool);
	assert_int_equal(rc, ABT_SUCCESS);

	printf("-- START of test --\n");
	rc = da_thread_create_on_pool(pool, abt_hello, "test_unnamed_thread_on_pool",
				      ABT_THREAD_ATTR_NULL, NULL);
	assert_int_equal(rc, ABT_SUCCESS);
	rc = ABT_thread_yield();
	assert_int_equal(rc, ABT_SUCCESS);

	printf("-- END of test --\n");
	da_finalize();
}

static void
check_stack_size(void *arg)
{
	size_t     stack_size_in = (size_t)arg;
	size_t     stack_size_out;
	ABT_thread thread;
	int        rc;

	rc = ABT_self_get_thread(&thread);
	assert_int_equal(rc, ABT_SUCCESS);
	rc = ABT_thread_get_stacksize(thread, &stack_size_out);
	assert_int_equal(rc, ABT_SUCCESS);

	assert_int_equal(stack_size_in, stack_size_out);
}

static void
test_stack_size(void **state)
{
	ABT_xstream     xstream;
	ABT_thread_attr attr;
	const size_t    stack_size = 1ull << 16;
	int             rc;

	(void)state; /* unused */

	printf("-- INIT of test --\n");
	rc = da_initialize(0, NULL);
	assert_int_equal(rc, 0);
	rc = ABT_self_get_xstream(&xstream);
	assert_int_equal(rc, ABT_SUCCESS);
	rc = ABT_thread_attr_create(&attr);
	assert_int_equal(rc, ABT_SUCCESS);
	rc = ABT_thread_attr_set_stacksize(attr, stack_size);
	assert_int_equal(rc, ABT_SUCCESS);

	printf("-- START of test --\n");
	rc = da_thread_create_on_xstream(xstream, check_stack_size, (void *)stack_size, attr, NULL);
	assert_int_equal(rc, ABT_SUCCESS);
	rc = ABT_thread_yield();
	assert_int_equal(rc, ABT_SUCCESS);

	printf("-- END of test --\n");
	rc = ABT_thread_attr_free(&attr);
	assert_int_equal(rc, ABT_SUCCESS);
	da_finalize();
}

static void
foo(void *arg)
{
	int thread_idx = (int)(intptr_t)arg;

	printf("Run foo thread %x\n", thread_idx);
}

static void
test_gc_001(void **state)
{
	ABT_xstream xstream;
	ABT_thread *threads;
	int         idx;
	int         rc;

	(void)state; /* unused */

	printf("-- INIT of test --\n");
	rc = da_initialize(0, NULL);
	assert_int_equal(rc, 0);
	rc = ABT_self_get_xstream(&xstream);
	assert_int_equal(rc, ABT_SUCCESS);
	D_ALLOC_ARRAY(threads, 0x1000);
	assert_non_null(threads);

	printf("-- START of test --\n");
	printf("---- Running 0x1000 ULTs ----\n");
	for (idx = 0; idx < 0x1000; ++idx) {
		rc = da_thread_create_on_xstream(xstream, foo, (void *)(intptr_t)idx,
						 ABT_THREAD_ATTR_NULL, &threads[idx]);
		assert_int_equal(rc, ABT_SUCCESS);
		rc = ABT_thread_join(threads[idx]);
		assert_int_equal(rc, ABT_SUCCESS);
	}
	printf("---- Destroying 0x400  ULTs----\n");
	for (idx = 0; idx < 0x400; ++idx) {
		rc = ABT_thread_free(&threads[idx]);
		assert_int_equal(rc, ABT_SUCCESS);
	}

	printf("-- END of test --\n");
	da_finalize();
}

static void
bar(void *arg)
{
	int thread_idx = (int)(intptr_t)arg;

	printf("Run bar thread %x\n", thread_idx);
}

static void
test_gc_002(void **state)
{
	ABT_xstream     xstream;
	ABT_thread     *threads;
	ABT_thread_attr attr;
	int             idx;
	int             rc;

	(void)state; /* unused */

	printf("-- INIT of test --\n");
	rc = da_initialize(0, NULL);
	assert_int_equal(rc, 0);
	rc = ABT_self_get_xstream(&xstream);
	assert_int_equal(rc, ABT_SUCCESS);
	D_ALLOC_ARRAY(threads, 0x1000);
	assert_non_null(threads);
	rc = ABT_thread_attr_create(&attr);
	assert_int_equal(rc, ABT_SUCCESS);
	rc = ABT_thread_attr_set_stacksize(attr, 1ull << 16);
	assert_int_equal(rc, ABT_SUCCESS);

	printf("-- START of test --\n");
	printf("---- Running 0x20 ULTs ----\n");
	for (idx = 0; idx < 0x20; ++idx) {
		rc = da_thread_create_on_xstream(xstream, foo, (void *)(intptr_t)idx, attr,
						 &threads[idx]);
		assert_int_equal(rc, ABT_SUCCESS);
		rc = ABT_thread_join(threads[idx]);
		assert_int_equal(rc, ABT_SUCCESS);
	}
	printf("---- Running 0x9e0 ULTs ----\n");
	for (idx = 0x20; idx < 0x1000; ++idx) {
		rc = da_thread_create_on_xstream(xstream, bar, (void *)(intptr_t)idx,
						 ABT_THREAD_ATTR_NULL, &threads[idx]);
		assert_int_equal(rc, ABT_SUCCESS);
		rc = ABT_thread_join(threads[idx]);
		assert_int_equal(rc, ABT_SUCCESS);
	}

	printf("---- Destroying 0x400  ULTs----\n");
	for (idx = 0; idx < 0x400; ++idx) {
		rc = ABT_thread_free(&threads[idx]);
		assert_int_equal(rc, ABT_SUCCESS);
	}

	printf("---- Destroying last ULTs----\n");
	for (idx = 0x400; idx < 0x1000; ++idx) {
		rc = ABT_thread_free(&threads[idx]);
		assert_int_equal(rc, ABT_SUCCESS);
	}

	printf("-- END of test --\n");
	rc = ABT_thread_attr_free(&attr);
	assert_int_equal(rc, ABT_SUCCESS);
	da_finalize();
}

int
main(int argc, char *argv[])
{
	int               test_id;
	struct CMUnitTest tests[] = {cmocka_unit_test(test_named_thread_on_xstream),
				     cmocka_unit_test(test_unnamed_thread_on_xstream),
				     cmocka_unit_test(test_named_thread_on_pool),
				     cmocka_unit_test(test_unnamed_thread_on_pool),
				     cmocka_unit_test(test_stack_size),
				     cmocka_unit_test(test_gc_001),
				     cmocka_unit_test(test_gc_002)};
	struct CMUnitTest test[1];

	d_register_alt_assert(mock_assert);

	if (argc == 1)
		return cmocka_run_group_tests_name("utest_usm", tests, init_tests, fini_tests);

	assert_int_equal(argc, 2);
	test_id = atoi(argv[1]);
	assert_true(test_id < ARRAY_SIZE(tests));
	memcpy(&test[0], tests + test_id, sizeof(struct CMUnitTest));

	return cmocka_run_group_tests_name("utest_usm", test, init_tests, fini_tests);
}
