/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <abt.h>
#include <cmocka.h>

#include <daos/debug.h>

#include <daos/checker.h>
#include <daos/common.h>
#include <engine/srv_internal.h>

#include "../dlck_args.h"
#include "../dlck_engine.h"
#include "dlck_ut.h"
#include "dlck_ut_abt_mock.h"
#include "dlck_ut_checker_mock.h"
#include "dlck_ut_daos_mock.h"
#include "dlck_ut_mem_mock.h"

#define TARGETS_MAX 3

#define SYS_TAG     (DAOS_SERVER_TAG - DAOS_TGT_TAG)

/** buffers handed out by the strict d_calloc mock or used to build engines manually */
static struct dlck_engine      Engine_buf;
static struct dlck_xstream     Xss_buf[TARGETS_MAX];
static struct dlck_ult         Ults_buf[TARGETS_MAX];
static void                   *Ult_args_buf[TARGETS_MAX];
static struct xstream_arg      Xa_buf;
static struct dlck_control     Ctrl;

/** per-target payloads and the custom argument passed to the ULT argument callbacks */
static int                     Payload[TARGETS_MAX];
static int                     Custom;

static struct dlck_args_engine Args = {.numa_node          = 1,
				       .max_dma_buf_size   = 262144,
				       .nvme_hugepage_size = 2048,
				       .targets            = 0,
				       .storage_path       = "mock-storage-path",
				       .nvme_conf          = "mock-nvme-conf"};

/** test doubles for the dlck_engine_exec_all() callbacks */

static void
test_exec_one(void *arg)
{
	check_expected_ptr(arg);
}

static int
test_arg_alloc(struct dlck_engine *engine, int idx, void *custom, void **arg)
{
	check_expected_ptr(engine);
	check_expected(idx);
	check_expected_ptr(custom);

	int rc = mock_type(int);

	if (rc == DER_SUCCESS) {
		*arg = mock_ptr_type(void *);
	}

	return rc;
}

static void
test_arg_free(void *custom, void **arg)
{
	check_expected_ptr(custom);
	assert_non_null(arg);

	void *ptr = *arg;

	check_expected_ptr(ptr);
	*arg = NULL;
}

/** composite expectation helpers */

#define EXPECT_TEST_ARG_ALLOC(ENGINE, IDX, RC, PTR)                                                \
	do {                                                                                       \
		expect_value(test_arg_alloc, engine, ENGINE);                                      \
		expect_value(test_arg_alloc, idx, IDX);                                            \
		expect_value(test_arg_alloc, custom, &Custom);                                     \
		will_return(test_arg_alloc, RC);                                                   \
		if ((RC) == DER_SUCCESS) {                                                         \
			will_return(test_arg_alloc, PTR);                                          \
		}                                                                                  \
	} while (0)

#define EXPECT_TEST_ARG_FREE(PTR)                                                                  \
	do {                                                                                       \
		expect_value(test_arg_free, custom, &Custom);                                      \
		expect_value(test_arg_free, ptr, PTR);                                             \
	} while (0)

#define EXPECT_EXEC_ARRAYS_ALLOC(T)                                                                \
	do {                                                                                       \
		EXPECT_D_CALLOC(T, sizeof(struct dlck_ult), Ults_buf);                             \
		EXPECT_D_CALLOC(T, sizeof(void *), Ult_args_buf);                                  \
	} while (0)

#define EXPECT_EXEC_ARRAYS_FREE()                                                                  \
	do {                                                                                       \
		expect_value(__wrap_d_free, ptr, Ult_args_buf);                                    \
		expect_value(__wrap_d_free, ptr, Ults_buf);                                        \
	} while (0)

#define EXPECT_ENGINE_ALLOC(T)                                                                     \
	do {                                                                                       \
		EXPECT_D_CALLOC(1, sizeof(struct dlck_engine), &Engine_buf);                       \
		EXPECT_D_CALLOC((T) + 1, sizeof(struct dlck_xstream), Xss_buf);                    \
	} while (0)

#define EXPECT_ENGINE_FREE()                                                                       \
	do {                                                                                       \
		expect_value(__wrap_d_free, ptr, Xss_buf);                                         \
		expect_value(__wrap_d_free, ptr, &Engine_buf);                                     \
	} while (0)

/** the common successful prelude of dlck_engine_start() up to vos_sys_db_init() */
#define EXPECT_START_COMMON()                                                                      \
	do {                                                                                       \
		EXPECT_ENGINE_ALLOC(Args.targets);                                                 \
		will_return(dss_register_dbtree_classes, DER_SUCCESS);                             \
		EXPECT_BIO_NVME_INIT(&Args, DER_SUCCESS);                                          \
		expect_value(__wrap_daos_register_key, key, &daos_srv_modkey);                     \
		expect_value(__wrap_daos_register_key, key, &vos_module_key);                      \
		will_return(mock_sm_init, DER_SUCCESS);                                            \
		will_return(__wrap_ds_tls_key_create, 0);                                          \
		EXPECT_VOS_STANDALONE_TLS_INIT(SYS_TAG, DER_SUCCESS);                              \
		EXPECT_VOS_SYS_DB_INIT(&Args, Args.storage_path, DER_SUCCESS);                     \
	} while (0)

#define EXPECT_XS_CREATE_ALL()                                                                     \
	do {                                                                                       \
		for (int _i = 0; _i <= Args.targets; ++_i) {                                       \
			EXPECT_DLCK_XSTREAM_CREATE(&Xss_buf[_i], DER_SUCCESS, Mock_pool);          \
		}                                                                                  \
	} while (0)

/** one full nvme_polling ULT lifetime concluding after ITERS iterations */
#define EXPECT_NVME_POLLING(ITERS)                                                                 \
	do {                                                                                       \
		EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);                                    \
		EXPECT_DSS_GET_MODULE_INFO();                                                      \
		for (int _j = 0; _j < (ITERS) - 1; ++_j) {                                         \
			EXPECT_BIO_NVME_POLL(Mock_xs_ctxt, 0);                                     \
			will_return(__wrap_ABT_thread_yield, ABT_SUCCESS);                         \
			EXPECT_ABT_EVENTUAL_TEST(Mock_eventual, ABT_SUCCESS, ABT_FALSE);           \
		}                                                                                  \
		EXPECT_BIO_NVME_POLL(Mock_xs_ctxt, 0);                                             \
		will_return(__wrap_ABT_thread_yield, ABT_SUCCESS);                                 \
		EXPECT_ABT_EVENTUAL_TEST(Mock_eventual, ABT_SUCCESS, ABT_TRUE);                    \
	} while (0)

/** successful initialization of a daos_io_* execution stream */
#define EXPECT_XS_INIT_IO(TGT_ID)                                                                  \
	do {                                                                                       \
		EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);                                    \
		EXPECT_DSS_TLS_INIT(                                                               \
		    DAOS_SERVER_TAG,                                                               \
		    DSS_MAIN_XS_ID_WITH_HELPER_POOL(TGT_ID, DSS_SYS_XS_NR_DEFAULT), TGT_ID,        \
		    &Mock_dtls);                                                                   \
		EXPECT_DSS_GET_MODULE_INFO();                                                      \
		EXPECT_BIO_XSCTXT_ALLOC(TGT_ID, DER_SUCCESS, Mock_xs_ctxt);                        \
		EXPECT_ABT_EVENTUAL_CREATE(ABT_SUCCESS, Mock_eventual);                            \
		EXPECT_NVME_POLLING(1);                                                            \
		EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);                                  \
	} while (0)

/** successful initialization of the daos_sys_0 execution stream */
#define EXPECT_XS_INIT_SYS(CONFIGURED, POLL_ITERS)                                                 \
	do {                                                                                       \
		EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);                                    \
		EXPECT_DSS_TLS_INIT(SYS_TAG, 0, -1, &Mock_dtls);                                   \
		EXPECT_BIO_NVME_CONFIGURED(CONFIGURED);                                            \
		if (CONFIGURED) {                                                                  \
			EXPECT_DSS_GET_MODULE_INFO();                                              \
			EXPECT_BIO_XSCTXT_ALLOC(-1, DER_SUCCESS, Mock_xs_ctxt);                    \
			EXPECT_ABT_EVENTUAL_CREATE(ABT_SUCCESS, Mock_eventual);                    \
			EXPECT_NVME_POLLING(POLL_ITERS);                                           \
		}                                                                                  \
		EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);                                  \
	} while (0)

/** a fini ULT finding no TLS (nothing to finalize) followed by freeing the execution stream */
#define EXPECT_XS_STOP_NO_TLS(XS)                                                                  \
	do {                                                                                       \
		EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);                                    \
		will_return(__wrap_dss_tls_get, NULL);                                             \
		EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);                                  \
		EXPECT_DLCK_XSTREAM_FREE(XS, DER_SUCCESS);                                         \
	} while (0)

/** the finalization body of an execution stream owning an NVMe context */
#define EXPECT_XS_FINI_NVME_BODY()                                                                 \
	do {                                                                                       \
		EXPECT_ABT_EVENTUAL_SET(Mock_eventual, ABT_SUCCESS);                               \
		EXPECT_ABT_THREAD_JOIN(Mock_thread, ABT_SUCCESS);                                  \
		EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);                                  \
		EXPECT_DSS_GET_MODULE_INFO();                                                      \
		expect_value(bio_xsctxt_free, ctxt, Mock_xs_ctxt);                                 \
		expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);                               \
	} while (0)

/** stopping an idle execution stream after a successful fini ULT */
#define EXPECT_XS_STOPPED(XS)                                                                      \
	do {                                                                                       \
		EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);                                  \
		EXPECT_ABT_POOL_IS_EMPTY(Mock_pool, ABT_TRUE);                                     \
		EXPECT_DLCK_XSTREAM_FREE(XS, DER_SUCCESS);                                         \
	} while (0)

/** the unwinding tail shared by dlck_engine_start() failures past vos_sys_db_init() */
#define EXPECT_START_UNWIND_TAIL()                                                                 \
	do {                                                                                       \
		expect_function_call(vos_db_fini);                                                 \
		expect_function_call(vos_standalone_tls_fini);                                     \
		expect_function_call(__wrap_ds_tls_key_delete);                                    \
		will_return(mock_sm_fini, DER_SUCCESS);                                            \
		expect_value(__wrap_daos_unregister_key, key, &vos_module_key);                    \
		expect_value(__wrap_daos_unregister_key, key, &daos_srv_modkey);                   \
		expect_function_call(bio_nvme_fini);                                               \
		EXPECT_ENGINE_FREE();                                                              \
	} while (0)

/** engines built manually for the exec and stop suites */

static void
engine_build(unsigned targets)
{
	Engine_buf.targets   = targets;
	Engine_buf.xss       = Xss_buf;
	Engine_buf.join_fail = false;

	for (unsigned i = 0; i <= targets; ++i) {
		Xss_buf[i].tgt_id = (i == targets ? -1 : (int)i);
		Xss_buf[i].pool   = Mock_pool;
	}
}

static void
engine_build_nvme(unsigned targets)
{
	engine_build(targets);

	for (unsigned i = 0; i <= targets; ++i) {
		Xss_buf[i].nvme_poll.thread = Mock_thread;
		Xss_buf[i].nvme_poll_done   = Mock_eventual;
	}

	Mock_dmi.dmi_nvme_ctxt = Mock_xs_ctxt;
}

/** setups & teardowns */

static int
setup(void **unused)
{
	memset(&Engine_buf, 0, sizeof(Engine_buf));
	memset(Xss_buf, 0, sizeof(Xss_buf));
	memset(Ults_buf, 0, sizeof(Ults_buf));
	memset(Ult_args_buf, 0, sizeof(Ult_args_buf));
	memset(&Xa_buf, 0, sizeof(Xa_buf));
	memset(&Ctrl, 0, sizeof(Ctrl));
	memset(&Mock_dmi, 0, sizeof(Mock_dmi));
	mock_ck_vprintf_check_output = false;

	return 0;
}

static int
teardown(void **unused)
{
	daos_fail_loc_set(0);
	daos_fail_value_set(0);

	return 0;
}

/** tests - dlck_engine_xstream_arg_alloc/free */

/** verify a successfully allocated argument points at the right execution stream */
static void
test_arg_alloc_success(void **unused)
{
	void *arg = NULL;
	int   rc;

	engine_build(2);
	EXPECT_D_CALLOC(1, sizeof(struct xstream_arg), &Xa_buf);

	rc = dlck_engine_xstream_arg_alloc(&Engine_buf, 1, &Ctrl, &arg);

	assert_int_equal(rc, DER_SUCCESS);
	assert_ptr_equal(arg, &Xa_buf);
	assert_ptr_equal(Xa_buf.ctrl, &Ctrl);
	assert_ptr_equal(Xa_buf.engine, &Engine_buf);
	assert_ptr_equal(Xa_buf.xs, &Xss_buf[1]);
	assert_int_equal(Xa_buf.status.rc, DER_SUCCESS);
}

/** verify an argument allocation failure is reported as -DER_NOMEM */
static void
test_arg_alloc_failure(void **unused)
{
	void *arg = NULL;
	int   rc;

	engine_build(2);
	EXPECT_D_CALLOC(1, sizeof(struct xstream_arg), NULL);

	rc = dlck_engine_xstream_arg_alloc(&Engine_buf, 1, &Ctrl, &arg);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(arg);
}

/** verify freeing a NULL argument is a no-op */
static void
test_arg_free_null(void **unused)
{
	struct checker_status sts[1] = {{0}};
	void                 *arg    = NULL;

	dlck_engine_xstream_arg_free(sts, &arg);

	assert_null(arg);
}

/** verify the ULT's status is collected into its target's slot while freeing its argument */
static void
test_arg_free_collects_outputs(void **unused)
{
	struct checker_status sts[2] = {{0}};
	void                 *arg    = &Xa_buf;

	engine_build(2);
	Xa_buf.xs                  = &Xss_buf[1];
	Xa_buf.status.rc           = -DER_IO;
	Xa_buf.status.warnings_num = 3;
	Xa_buf.status.errors_num   = 2;
	expect_value(__wrap_d_free, ptr, &Xa_buf);

	dlck_engine_xstream_arg_free(sts, &arg);

	assert_int_equal(sts[1].rc, -DER_IO);
	assert_int_equal(sts[1].warnings_num, 3);
	assert_int_equal(sts[1].errors_num, 2);
	assert_null(arg);
}

/** tests - dlck_engine_exec_all */

/** verify all targets start, execute, and stop in order on the happy path */
static void
test_exec_all_success(void **unused)
{
	int rc;

	engine_build(2);
	EXPECT_EXEC_ARRAYS_ALLOC(2);
	for (int i = 0; i < 2; ++i) {
		EXPECT_TEST_ARG_ALLOC(&Engine_buf, i, DER_SUCCESS, &Payload[i]);
		EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
		expect_value(test_exec_one, arg, &Payload[i]);
	}
	for (int i = 0; i < 2; ++i) {
		EXPECT_ABT_THREAD_JOIN(Mock_thread, ABT_SUCCESS);
		EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
		EXPECT_TEST_ARG_FREE(&Payload[i]);
	}
	EXPECT_EXEC_ARRAYS_FREE();

	rc = dlck_engine_exec_all(&Engine_buf, test_exec_one, test_arg_alloc, &Custom,
				  test_arg_free, &Custom, NULL);

	assert_int_equal(rc, DER_SUCCESS);
	assert_false(Engine_buf.join_fail);
}

/** verify the progress messages a checker receives on the happy path */
static void
test_exec_all_success_with_checker(void **unused)
{
	struct checker ck = {.ck_vprintf = mock_ck_vprintf, .ck_prefix = "- "};
	int            rc;

	engine_build(1);
	EXPECT_MOCK_CK_VPRINTF(&ck, "%sStart targets... ", DER_SUCCESS);
	EXPECT_EXEC_ARRAYS_ALLOC(1);
	EXPECT_TEST_ARG_ALLOC(&Engine_buf, 0, DER_SUCCESS, &Payload[0]);
	EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
	expect_value(test_exec_one, arg, &Payload[0]);
	EXPECT_MOCK_CK_VPRINTF(&ck, CHECKER_OK_INFIX ".\n", DER_SUCCESS);
	EXPECT_MOCK_CK_VPRINTF(&ck, "%sWait for targets to stop...\n", DER_SUCCESS);
	EXPECT_ABT_THREAD_JOIN(Mock_thread, ABT_SUCCESS);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_TEST_ARG_FREE(&Payload[0]);
	EXPECT_EXEC_ARRAYS_FREE();
	EXPECT_MOCK_CK_VPRINTF(&ck, "%sWait for targets to stop: " CHECKER_OK_INFIX ".\n",
			       DER_SUCCESS);

	rc = dlck_engine_exec_all(&Engine_buf, test_exec_one, test_arg_alloc, &Custom,
				  test_arg_free, &Custom, &ck);

	assert_int_equal(rc, DER_SUCCESS);
}

/** verify the DLCK_FAULT_ENGINE_EXEC fault injection stops the execution before it starts */
static void
test_exec_all_fault_exec(void **unused)
{
	int rc;

	engine_build(2);
	daos_fail_loc_set(DLCK_FAULT_ENGINE_EXEC | DAOS_FAIL_ONCE);
	daos_fail_value_set(ENOMEM);

	rc = dlck_engine_exec_all(&Engine_buf, test_exec_one, test_arg_alloc, &Custom,
				  test_arg_free, &Custom, NULL);

	assert_int_equal(rc, -DER_NOMEM);
}

/** verify an allocation failure of the ULTs array is reported as -DER_NOMEM */
static void
test_exec_all_ults_alloc_failure(void **unused)
{
	int rc;

	engine_build(2);
	EXPECT_D_CALLOC(2, sizeof(struct dlck_ult), NULL);

	rc = dlck_engine_exec_all(&Engine_buf, test_exec_one, test_arg_alloc, &Custom,
				  test_arg_free, &Custom, NULL);

	assert_int_equal(rc, -DER_NOMEM);
}

/** verify an allocation failure of the arguments array frees the ULTs array */
static void
test_exec_all_args_alloc_failure(void **unused)
{
	int rc;

	engine_build(2);
	EXPECT_D_CALLOC(2, sizeof(struct dlck_ult), Ults_buf);
	EXPECT_D_CALLOC(2, sizeof(void *), NULL);
	expect_value(__wrap_d_free, ptr, Ults_buf);

	rc = dlck_engine_exec_all(&Engine_buf, test_exec_one, test_arg_alloc, &Custom,
				  test_arg_free, &Custom, NULL);

	assert_int_equal(rc, -DER_NOMEM);
}

/** verify an argument allocation failure joins already started ULTs and frees their arguments */
static void
test_exec_all_arg_alloc_failure(void **unused)
{
	int rc;

	engine_build(2);
	EXPECT_EXEC_ARRAYS_ALLOC(2);
	EXPECT_TEST_ARG_ALLOC(&Engine_buf, 0, DER_SUCCESS, &Payload[0]);
	EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
	expect_value(test_exec_one, arg, &Payload[0]);
	EXPECT_TEST_ARG_ALLOC(&Engine_buf, 1, -DER_IO, NULL);
	/** cleanup: the started ULT is joined and freed; both arguments are released */
	EXPECT_ABT_THREAD_JOIN(Mock_thread, ABT_SUCCESS);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_TEST_ARG_FREE(&Payload[0]);
	EXPECT_TEST_ARG_FREE(NULL);
	EXPECT_EXEC_ARRAYS_FREE();

	rc = dlck_engine_exec_all(&Engine_buf, test_exec_one, test_arg_alloc, &Custom,
				  test_arg_free, &Custom, NULL);

	assert_int_equal(rc, -DER_IO);
}

/** verify a ULT creation failure frees the just allocated argument as well */
static void
test_exec_all_ult_create_failure(void **unused)
{
	int rc;

	engine_build(2);
	EXPECT_EXEC_ARRAYS_ALLOC(2);
	EXPECT_TEST_ARG_ALLOC(&Engine_buf, 0, DER_SUCCESS, &Payload[0]);
	EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
	expect_value(test_exec_one, arg, &Payload[0]);
	EXPECT_TEST_ARG_ALLOC(&Engine_buf, 1, DER_SUCCESS, &Payload[1]);
	EXPECT_DLCK_ULT_CREATE(Mock_pool, -DER_NOMEM);
	/** cleanup */
	EXPECT_ABT_THREAD_JOIN(Mock_thread, ABT_SUCCESS);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_TEST_ARG_FREE(&Payload[0]);
	EXPECT_TEST_ARG_FREE(&Payload[1]);
	EXPECT_EXEC_ARRAYS_FREE();

	rc = dlck_engine_exec_all(&Engine_buf, test_exec_one, test_arg_alloc, &Custom,
				  test_arg_free, &Custom, NULL);

	assert_int_equal(rc, -DER_NOMEM);
}

/** verify a join failure marks the engine as unstoppable and cleans up the rest */
static void
test_exec_all_join_failure(void **unused)
{
	int rc;

	engine_build(2);
	EXPECT_EXEC_ARRAYS_ALLOC(2);
	for (int i = 0; i < 2; ++i) {
		EXPECT_TEST_ARG_ALLOC(&Engine_buf, i, DER_SUCCESS, &Payload[i]);
		EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
		expect_value(test_exec_one, arg, &Payload[i]);
	}
	EXPECT_ABT_THREAD_JOIN(Mock_thread, ABT_ERR_INV_THREAD);
	/** cleanup joins both ULTs again */
	for (int i = 0; i < 2; ++i) {
		EXPECT_ABT_THREAD_JOIN(Mock_thread, ABT_SUCCESS);
		EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
		EXPECT_TEST_ARG_FREE(&Payload[i]);
	}
	EXPECT_EXEC_ARRAYS_FREE();

	rc = dlck_engine_exec_all(&Engine_buf, test_exec_one, test_arg_alloc, &Custom,
				  test_arg_free, &Custom, NULL);

	assert_int_equal(rc, -DER_INVAL);
	assert_true(Engine_buf.join_fail);
}

/** verify a thread free failure is propagated without marking a join failure */
static void
test_exec_all_thread_free_failure(void **unused)
{
	int rc;

	engine_build(2);
	EXPECT_EXEC_ARRAYS_ALLOC(2);
	for (int i = 0; i < 2; ++i) {
		EXPECT_TEST_ARG_ALLOC(&Engine_buf, i, DER_SUCCESS, &Payload[i]);
		EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
		expect_value(test_exec_one, arg, &Payload[i]);
	}
	EXPECT_ABT_THREAD_JOIN(Mock_thread, ABT_SUCCESS);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_ERR_INV_THREAD);
	/** cleanup */
	for (int i = 0; i < 2; ++i) {
		EXPECT_ABT_THREAD_JOIN(Mock_thread, ABT_SUCCESS);
		EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
		EXPECT_TEST_ARG_FREE(&Payload[i]);
	}
	EXPECT_EXEC_ARRAYS_FREE();

	rc = dlck_engine_exec_all(&Engine_buf, test_exec_one, test_arg_alloc, &Custom,
				  test_arg_free, &Custom, NULL);

	assert_int_equal(rc, -DER_INVAL);
	assert_false(Engine_buf.join_fail);
}

/** verify the DLCK_FAULT_ENGINE_JOIN fault injection marks the engine as unstoppable */
static void
test_exec_all_fault_join(void **unused)
{
	int rc;

	engine_build(1);
	daos_fail_loc_set(DLCK_FAULT_ENGINE_JOIN | DAOS_FAIL_ONCE);
	daos_fail_value_set(EBUSY);
	EXPECT_EXEC_ARRAYS_ALLOC(1);
	EXPECT_TEST_ARG_ALLOC(&Engine_buf, 0, DER_SUCCESS, &Payload[0]);
	EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
	expect_value(test_exec_one, arg, &Payload[0]);
	/** cleanup */
	EXPECT_ABT_THREAD_JOIN(Mock_thread, ABT_SUCCESS);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_TEST_ARG_FREE(&Payload[0]);
	EXPECT_EXEC_ARRAYS_FREE();

	rc = dlck_engine_exec_all(&Engine_buf, test_exec_one, test_arg_alloc, &Custom,
				  test_arg_free, &Custom, NULL);

	assert_int_equal(rc, -DER_BUSY);
	assert_true(Engine_buf.join_fail);
}

/** tests - dlck_engine_start */

/** verify the full startup sequence with two targets and NVMe configured */
static void
test_start_success(void **unused)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	Args.targets = 2;
	EXPECT_START_COMMON();
	EXPECT_XS_CREATE_ALL();
	EXPECT_XS_INIT_SYS(true, 2);
	EXPECT_XS_INIT_IO(0);
	EXPECT_XS_INIT_IO(1);

	rc = dlck_engine_start(&Args, &engine);

	assert_int_equal(rc, DER_SUCCESS);
	assert_ptr_equal(engine, &Engine_buf);
	assert_int_equal(engine->targets, 2);
	assert_ptr_equal(engine->xss, Xss_buf);
	assert_false(engine->join_fail);
}

/** verify the sys execution stream skips the NVMe setup when NVMe is not configured */
static void
test_start_success_no_nvme(void **unused)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	Args.targets = 0;
	EXPECT_START_COMMON();
	EXPECT_XS_CREATE_ALL();
	EXPECT_XS_INIT_SYS(false, 0);

	rc = dlck_engine_start(&Args, &engine);

	assert_int_equal(rc, DER_SUCCESS);
	assert_ptr_equal(engine, &Engine_buf);
}

/** verify the DLCK_FAULT_ENGINE_START fault injection stops the startup before it begins */
static void
test_start_fault(void **unused)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	Args.targets = 2;
	daos_fail_loc_set(DLCK_FAULT_ENGINE_START | DAOS_FAIL_ONCE);
	daos_fail_value_set(EIO);

	rc = dlck_engine_start(&Args, &engine);

	assert_int_equal(rc, -DER_IO);
	assert_null(engine);
}

/** verify an engine allocation failure is reported as -DER_NOMEM */
static void
test_start_engine_alloc_failure(void **unused)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	Args.targets = 2;
	EXPECT_D_CALLOC(1, sizeof(struct dlck_engine), NULL);

	rc = dlck_engine_start(&Args, &engine);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(engine);
}

/** verify an execution streams array allocation failure frees the engine */
static void
test_start_xss_alloc_failure(void **unused)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	Args.targets = 2;
	EXPECT_D_CALLOC(1, sizeof(struct dlck_engine), &Engine_buf);
	EXPECT_D_CALLOC(3, sizeof(struct dlck_xstream), NULL);
	expect_value(__wrap_d_free, ptr, &Engine_buf);

	rc = dlck_engine_start(&Args, &engine);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(engine);
}

/** verify a dbtree classes registration failure frees the engine */
static void
test_start_dbtree_classes_failure(void **unused)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	Args.targets = 2;
	EXPECT_ENGINE_ALLOC(2);
	will_return(dss_register_dbtree_classes, -DER_NOMEM);
	EXPECT_ENGINE_FREE();

	rc = dlck_engine_start(&Args, &engine);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(engine);
}

/** verify a bio_nvme_init() failure frees the engine */
static void
test_start_bio_nvme_init_failure(void **unused)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	Args.targets = 2;
	EXPECT_ENGINE_ALLOC(2);
	will_return(dss_register_dbtree_classes, DER_SUCCESS);
	EXPECT_BIO_NVME_INIT(&Args, -DER_INVAL);
	EXPECT_ENGINE_FREE();

	rc = dlck_engine_start(&Args, &engine);

	assert_int_equal(rc, -DER_INVAL);
	assert_null(engine);
}

/** verify a VOS module init failure unregisters the keys and finalizes NVMe */
static void
test_start_sm_init_failure(void **unused)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	Args.targets = 2;
	EXPECT_ENGINE_ALLOC(2);
	will_return(dss_register_dbtree_classes, DER_SUCCESS);
	EXPECT_BIO_NVME_INIT(&Args, DER_SUCCESS);
	expect_value(__wrap_daos_register_key, key, &daos_srv_modkey);
	expect_value(__wrap_daos_register_key, key, &vos_module_key);
	will_return(mock_sm_init, -DER_NOMEM);
	expect_value(__wrap_daos_unregister_key, key, &vos_module_key);
	expect_value(__wrap_daos_unregister_key, key, &daos_srv_modkey);
	expect_function_call(bio_nvme_fini);
	EXPECT_ENGINE_FREE();

	rc = dlck_engine_start(&Args, &engine);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(engine);
}

/** verify a TLS key creation failure maps errno and finalizes the VOS module */
static void
test_start_tls_key_create_failure(void **unused)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	Args.targets = 2;
	EXPECT_ENGINE_ALLOC(2);
	will_return(dss_register_dbtree_classes, DER_SUCCESS);
	EXPECT_BIO_NVME_INIT(&Args, DER_SUCCESS);
	expect_value(__wrap_daos_register_key, key, &daos_srv_modkey);
	expect_value(__wrap_daos_register_key, key, &vos_module_key);
	will_return(mock_sm_init, DER_SUCCESS);
	will_return(__wrap_ds_tls_key_create, ENOMEM);
	will_return(mock_sm_fini, DER_SUCCESS);
	expect_value(__wrap_daos_unregister_key, key, &vos_module_key);
	expect_value(__wrap_daos_unregister_key, key, &daos_srv_modkey);
	expect_function_call(bio_nvme_fini);
	EXPECT_ENGINE_FREE();

	rc = dlck_engine_start(&Args, &engine);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(engine);
}

/** verify a standalone VOS TLS init failure deletes the TLS key as well */
static void
test_start_vos_tls_init_failure(void **unused)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	Args.targets = 2;
	EXPECT_ENGINE_ALLOC(2);
	will_return(dss_register_dbtree_classes, DER_SUCCESS);
	EXPECT_BIO_NVME_INIT(&Args, DER_SUCCESS);
	expect_value(__wrap_daos_register_key, key, &daos_srv_modkey);
	expect_value(__wrap_daos_register_key, key, &vos_module_key);
	will_return(mock_sm_init, DER_SUCCESS);
	will_return(__wrap_ds_tls_key_create, 0);
	EXPECT_VOS_STANDALONE_TLS_INIT(SYS_TAG, -DER_NOMEM);
	expect_function_call(__wrap_ds_tls_key_delete);
	will_return(mock_sm_fini, DER_SUCCESS);
	expect_value(__wrap_daos_unregister_key, key, &vos_module_key);
	expect_value(__wrap_daos_unregister_key, key, &daos_srv_modkey);
	expect_function_call(bio_nvme_fini);
	EXPECT_ENGINE_FREE();

	rc = dlck_engine_start(&Args, &engine);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(engine);
}

/** verify a system DB init failure finalizes the standalone VOS TLS as well */
static void
test_start_vos_sys_db_init_failure(void **unused)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	Args.targets = 2;
	EXPECT_ENGINE_ALLOC(2);
	will_return(dss_register_dbtree_classes, DER_SUCCESS);
	EXPECT_BIO_NVME_INIT(&Args, DER_SUCCESS);
	expect_value(__wrap_daos_register_key, key, &daos_srv_modkey);
	expect_value(__wrap_daos_register_key, key, &vos_module_key);
	will_return(mock_sm_init, DER_SUCCESS);
	will_return(__wrap_ds_tls_key_create, 0);
	EXPECT_VOS_STANDALONE_TLS_INIT(SYS_TAG, DER_SUCCESS);
	EXPECT_VOS_SYS_DB_INIT(&Args, Args.storage_path, -DER_IO);
	expect_function_call(vos_standalone_tls_fini);
	expect_function_call(__wrap_ds_tls_key_delete);
	will_return(mock_sm_fini, DER_SUCCESS);
	expect_value(__wrap_daos_unregister_key, key, &vos_module_key);
	expect_value(__wrap_daos_unregister_key, key, &daos_srv_modkey);
	expect_function_call(bio_nvme_fini);
	EXPECT_ENGINE_FREE();

	rc = dlck_engine_start(&Args, &engine);

	assert_int_equal(rc, -DER_IO);
	assert_null(engine);
}

/** verify an execution stream creation failure unwinds everything */
static void
test_start_xs_create_failure(void **unused)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	Args.targets = 0;
	EXPECT_START_COMMON();
	EXPECT_DLCK_XSTREAM_CREATE(&Xss_buf[0], -DER_NOMEM, NULL);
	/** the no-error stop still runs a fini ULT; there is no TLS to finalize */
	EXPECT_DLCK_ULT_CREATE(NULL, DER_SUCCESS);
	will_return(__wrap_dss_tls_get, NULL);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_DLCK_XSTREAM_FREE(&Xss_buf[0], DER_SUCCESS);
	EXPECT_START_UNWIND_TAIL();

	rc = dlck_engine_start(&Args, &engine);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(engine);
}

/** verify a TLS initialization failure of the sys execution stream unwinds everything */
static void
test_start_xs_init_tls_failure(void **unused)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	Args.targets = 0;
	EXPECT_START_COMMON();
	EXPECT_XS_CREATE_ALL();
	EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
	EXPECT_DSS_TLS_INIT(SYS_TAG, 0, -1, NULL);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_XS_STOP_NO_TLS(&Xss_buf[0]);
	EXPECT_START_UNWIND_TAIL();

	rc = dlck_engine_start(&Args, &engine);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(engine);
}

/** verify an NVMe context allocation failure finalizes the TLS of the failed stream */
static void
test_start_xs_init_xsctxt_failure(void **unused)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	Args.targets = 1;
	EXPECT_START_COMMON();
	EXPECT_XS_CREATE_ALL();
	EXPECT_XS_INIT_SYS(true, 1);
	/** the io execution stream fails to allocate its NVMe context */
	EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
	EXPECT_DSS_TLS_INIT(DAOS_SERVER_TAG,
			    DSS_MAIN_XS_ID_WITH_HELPER_POOL(0, DSS_SYS_XS_NR_DEFAULT), 0,
			    &Mock_dtls);
	EXPECT_DSS_GET_MODULE_INFO();
	EXPECT_BIO_XSCTXT_ALLOC(0, -DER_NOMEM, NULL);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	/** the no-error stop finalizes the io stream (no TLS) and the fully set up sys stream */
	EXPECT_XS_STOP_NO_TLS(&Xss_buf[0]);
	EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
	will_return(__wrap_dss_tls_get, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(true);
	EXPECT_XS_FINI_NVME_BODY();
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_DLCK_XSTREAM_FREE(&Xss_buf[1], DER_SUCCESS);
	EXPECT_START_UNWIND_TAIL();

	rc = dlck_engine_start(&Args, &engine);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(engine);
}

/** verify an eventual creation failure frees the NVMe context and finalizes the TLS */
static void
test_start_xs_init_eventual_failure(void **unused)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	Args.targets = 0;
	EXPECT_START_COMMON();
	EXPECT_XS_CREATE_ALL();
	EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
	EXPECT_DSS_TLS_INIT(SYS_TAG, 0, -1, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(true);
	EXPECT_DSS_GET_MODULE_INFO();
	EXPECT_BIO_XSCTXT_ALLOC(-1, DER_SUCCESS, Mock_xs_ctxt);
	EXPECT_ABT_EVENTUAL_CREATE(ABT_ERR_MEM, NULL);
	expect_value(bio_xsctxt_free, ctxt, Mock_xs_ctxt);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_XS_STOP_NO_TLS(&Xss_buf[0]);
	EXPECT_START_UNWIND_TAIL();

	rc = dlck_engine_start(&Args, &engine);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(engine);
}

/** verify an NVMe polling ULT creation failure frees the eventual, the context, and the TLS */
static void
test_start_xs_init_polling_ult_failure(void **unused)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	Args.targets = 0;
	EXPECT_START_COMMON();
	EXPECT_XS_CREATE_ALL();
	EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
	EXPECT_DSS_TLS_INIT(SYS_TAG, 0, -1, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(true);
	EXPECT_DSS_GET_MODULE_INFO();
	EXPECT_BIO_XSCTXT_ALLOC(-1, DER_SUCCESS, Mock_xs_ctxt);
	EXPECT_ABT_EVENTUAL_CREATE(ABT_SUCCESS, Mock_eventual);
	EXPECT_DLCK_ULT_CREATE(Mock_pool, -DER_NOMEM);
	EXPECT_ABT_EVENTUAL_FREE(Mock_eventual, ABT_SUCCESS);
	expect_value(bio_xsctxt_free, ctxt, Mock_xs_ctxt);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_XS_STOP_NO_TLS(&Xss_buf[0]);
	EXPECT_START_UNWIND_TAIL();

	rc = dlck_engine_start(&Args, &engine);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(engine);
}

/** verify the NVMe polling ULT stops polling when the eventual test fails */
static void
test_start_polling_eventual_test_failure(void **unused)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	Args.targets = 0;
	EXPECT_START_COMMON();
	EXPECT_XS_CREATE_ALL();
	EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
	EXPECT_DSS_TLS_INIT(SYS_TAG, 0, -1, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(true);
	EXPECT_DSS_GET_MODULE_INFO();
	EXPECT_BIO_XSCTXT_ALLOC(-1, DER_SUCCESS, Mock_xs_ctxt);
	EXPECT_ABT_EVENTUAL_CREATE(ABT_SUCCESS, Mock_eventual);
	/** the polling ULT concludes on the eventual test error instead of the readiness */
	EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
	EXPECT_DSS_GET_MODULE_INFO();
	EXPECT_BIO_NVME_POLL(Mock_xs_ctxt, 0);
	will_return(__wrap_ABT_thread_yield, ABT_SUCCESS);
	EXPECT_ABT_EVENTUAL_TEST(Mock_eventual, ABT_ERR_INV_EVENTUAL, ABT_FALSE);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);

	rc = dlck_engine_start(&Args, &engine);

	assert_int_equal(rc, DER_SUCCESS);
	assert_ptr_equal(engine, &Engine_buf);
}

/** tests - dlck_engine_stop */

/** verify the full stop sequence finalizes NVMe and non-NVMe execution streams */
static void
test_stop_success(void **unused)
{
	int rc;

	engine_build_nvme(1);
	/** the io execution stream owns an NVMe context */
	EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
	will_return(__wrap_dss_tls_get, &Mock_dtls);
	EXPECT_XS_FINI_NVME_BODY();
	EXPECT_XS_STOPPED(&Xss_buf[0]);
	/** the sys execution stream has no NVMe configured */
	EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
	will_return(__wrap_dss_tls_get, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(false);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_XS_STOPPED(&Xss_buf[1]);
	expect_function_call(vos_db_fini);
	expect_function_call(vos_standalone_tls_fini);
	expect_function_call(__wrap_ds_tls_key_delete);
	will_return(mock_sm_fini, DER_SUCCESS);
	expect_value(__wrap_daos_unregister_key, key, &vos_module_key);
	expect_value(__wrap_daos_unregister_key, key, &daos_srv_modkey);
	expect_function_call(bio_nvme_fini);
	EXPECT_ENGINE_FREE();

	rc = dlck_engine_stop(&Engine_buf);

	assert_int_equal(rc, DER_SUCCESS);
}

/** verify a stop attempt with a failed join reports -DER_BUSY without touching anything */
static void
test_stop_join_fail(void **unused)
{
	int rc;

	engine_build(0);
	Engine_buf.join_fail = true;

	rc = dlck_engine_stop(&Engine_buf);

	assert_int_equal(rc, -DER_BUSY);
}

/** verify a fini ULT failure stops the engine teardown and frees the streams */
static void
test_stop_fini_failure(void **unused)
{
	int rc;

	engine_build_nvme(0);
	EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
	will_return(__wrap_dss_tls_get, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(true);
	EXPECT_ABT_EVENTUAL_SET(Mock_eventual, ABT_ERR_INV_EVENTUAL);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	/** the no-error stop retries the fini ULT */
	EXPECT_XS_STOP_NO_TLS(&Xss_buf[0]);

	rc = dlck_engine_stop(&Engine_buf);

	assert_int_equal(rc, -DER_INVAL);
}

/** verify a busy pool prevents the execution stream from being freed */
static void
test_stop_pool_not_empty(void **unused)
{
	int rc;

	engine_build(0);
	EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
	will_return(__wrap_dss_tls_get, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(false);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_ABT_POOL_IS_EMPTY(Mock_pool, ABT_FALSE);
	/** the no-error stop retries the fini ULT */
	EXPECT_XS_STOP_NO_TLS(&Xss_buf[0]);

	rc = dlck_engine_stop(&Engine_buf);

	assert_int_equal(rc, -DER_BUSY);
}

/** verify an execution stream free failure falls back to the no-error stop */
static void
test_stop_xs_free_failure(void **unused)
{
	int rc;

	engine_build(0);
	EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
	will_return(__wrap_dss_tls_get, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(false);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_ABT_POOL_IS_EMPTY(Mock_pool, ABT_TRUE);
	EXPECT_DLCK_XSTREAM_FREE(&Xss_buf[0], -DER_INVAL);
	/** the no-error stop retries the fini ULT */
	EXPECT_XS_STOP_NO_TLS(&Xss_buf[0]);

	rc = dlck_engine_stop(&Engine_buf);

	assert_int_equal(rc, -DER_INVAL);
}

/** verify a VOS module fini failure stops the teardown without freeing the engine */
static void
test_stop_sm_fini_failure(void **unused)
{
	int rc;

	engine_build(0);
	EXPECT_DLCK_ULT_CREATE(Mock_pool, DER_SUCCESS);
	will_return(__wrap_dss_tls_get, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(false);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_ABT_POOL_IS_EMPTY(Mock_pool, ABT_TRUE);
	EXPECT_DLCK_XSTREAM_FREE(&Xss_buf[0], DER_SUCCESS);
	expect_function_call(vos_db_fini);
	expect_function_call(vos_standalone_tls_fini);
	expect_function_call(__wrap_ds_tls_key_delete);
	will_return(mock_sm_fini, -DER_BUSY);

	rc = dlck_engine_stop(&Engine_buf);

	assert_int_equal(rc, -DER_BUSY);
}

/** verify a fini ULT creation failure leaves the execution streams untouched */
static void
test_stop_fini_ult_create_failure(void **unused)
{
	int rc;

	engine_build(0);
	EXPECT_DLCK_ULT_CREATE(Mock_pool, -DER_NOMEM);
	/** the no-error stop cannot create the fini ULT either */
	EXPECT_DLCK_ULT_CREATE(Mock_pool, -DER_NOMEM);

	rc = dlck_engine_stop(&Engine_buf);

	assert_int_equal(rc, -DER_NOMEM);
}

#define TEST(ID, FUNC) {"DLCK_ENGINE_" #ID ": " #FUNC, FUNC, setup, teardown, NULL}

static const struct CMUnitTest dlck_engine_tests[] = {
    TEST(101, test_arg_alloc_success),
    TEST(102, test_arg_alloc_failure),
    TEST(103, test_arg_free_null),
    TEST(104, test_arg_free_collects_outputs),
    TEST(105, test_exec_all_success),
    TEST(106, test_exec_all_success_with_checker),
    TEST(107, test_exec_all_fault_exec),
    TEST(108, test_exec_all_ults_alloc_failure),
    TEST(109, test_exec_all_args_alloc_failure),
    TEST(110, test_exec_all_arg_alloc_failure),
    TEST(111, test_exec_all_ult_create_failure),
    TEST(112, test_exec_all_join_failure),
    TEST(113, test_exec_all_thread_free_failure),
    TEST(114, test_exec_all_fault_join),
    TEST(115, test_start_success),
    TEST(116, test_start_success_no_nvme),
    TEST(117, test_start_fault),
    TEST(118, test_start_engine_alloc_failure),
    TEST(119, test_start_xss_alloc_failure),
    TEST(120, test_start_dbtree_classes_failure),
    TEST(121, test_start_bio_nvme_init_failure),
    TEST(122, test_start_sm_init_failure),
    TEST(123, test_start_tls_key_create_failure),
    TEST(124, test_start_vos_tls_init_failure),
    TEST(125, test_start_vos_sys_db_init_failure),
    TEST(126, test_start_xs_create_failure),
    TEST(127, test_start_xs_init_tls_failure),
    TEST(128, test_start_xs_init_xsctxt_failure),
    TEST(129, test_start_xs_init_eventual_failure),
    TEST(130, test_start_xs_init_polling_ult_failure),
    TEST(131, test_start_polling_eventual_test_failure),
    TEST(132, test_stop_success),
    TEST(133, test_stop_join_fail),
    TEST(134, test_stop_fini_failure),
    TEST(135, test_stop_pool_not_empty),
    TEST(136, test_stop_xs_free_failure),
    TEST(137, test_stop_sm_fini_failure),
    TEST(138, test_stop_fini_ult_create_failure),
};

int
main(int argc, char **argv)
{
	int rc;

	d_register_alt_assert(mock_assert);

	rc = dlck_ut_filter(argc, argv);
	if (rc != 0) {
		return rc;
	}

	/** required by daos_fail_loc_set() to store the fault attributes */
	rc = daos_fail_init();
	if (rc != 0) {
		fprintf(stderr, "ERROR: daos_fail_init() failed: %d\n", rc);
		return 1;
	}

	rc = cmocka_run_group_tests_name("dlck_engine_ut", dlck_engine_tests, NULL, NULL);

	daos_fail_fini();

	return rc;
}
