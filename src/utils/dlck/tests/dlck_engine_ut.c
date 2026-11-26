/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <abt.h>

#include <daos/debug.h>
#include <daos/common.h>
#include <engine/srv_internal.h>

#include "../dlck_args.h"
#include "../dlck_engine.h"
#include "../dlck_engine_internal.h"
#include "dlck_ut.h"
#include "ut_abt_mock.h"
#include "ut_dlck_abt_mock.h"
#include "ut_dlck_cmd_mock.h"
#include "ut_dlck_daos_mock.h"
#include "ut_mem_mock.h"

#define TARGETS_MAX        3

#define SYS_TAG            (DAOS_SERVER_TAG - DAOS_TGT_TAG)

/** per-execution-stream sentinels so that resource mix-ups between the streams are caught */
#define MOCK_POOL(IDX)     ((ABT_pool)(uintptr_t)(0x1000 + (IDX)))
#define MOCK_EVENTUAL(IDX) ((ABT_eventual)(uintptr_t)(0x2000 + (IDX)))
#define MOCK_THREAD(IDX)   ((ABT_thread)(uintptr_t)(0x3000 + (IDX)))

/**
 * per-test context: the engine arguments, the engine and its execution streams, and the
 * buffers handed out by the strict d_calloc mock;
 */
struct engine_ut_ctx {
	struct dlck_args_engine args;
	struct dlck_engine      engine;
	struct dlck_xstream     xss[TARGETS_MAX];
	struct dlck_exec        exec;
	struct dlck_ult         ults[TARGETS_MAX];
	void                   *ult_args[TARGETS_MAX];
	/** per-target payloads */
	int                     payload[TARGETS_MAX];
	/** custom arguments passed to the ULT argument callbacks */
	int                     custom_alloc;
	int                     custom_free;
};

static struct engine_ut_ctx Ctx;

/** composite expectation helpers */

#define EXPECT_EXEC_INIT_ALLOC(ctx, T)                                                             \
	do {                                                                                       \
		EXPECT_D_CALLOC(T, sizeof(struct dlck_ult), (ctx)->ults);                          \
		EXPECT_D_CALLOC(T, sizeof(void *), (ctx)->ult_args);                               \
	} while (0)

#define EXPECT_EXEC_FINI_FREE(ctx)                                                                 \
	do {                                                                                       \
		expect_value(__wrap_d_free, ptr, (ctx)->ult_args);                                 \
		expect_value(__wrap_d_free, ptr, (ctx)->ults);                                     \
	} while (0)

#define EXPECT_ENGINE_ALLOC(ctx, T)                                                                \
	do {                                                                                       \
		EXPECT_D_CALLOC(1, sizeof(struct dlck_engine), &(ctx)->engine);                    \
		EXPECT_D_CALLOC((T) + 1, sizeof(struct dlck_xstream), (ctx)->xss);                 \
	} while (0)

#define EXPECT_ENGINE_FREE(ctx)                                                                    \
	do {                                                                                       \
		expect_value(__wrap_d_free, ptr, (ctx)->xss);                                      \
		expect_value(__wrap_d_free, ptr, &(ctx)->engine);                                  \
	} while (0)

/** the common successful prelude of dlck_engine_init() up to vos_sys_db_init() */
#define EXPECT_ENGINE_INIT(ctx)                                                                    \
	do {                                                                                       \
		EXPECT_ENGINE_ALLOC(ctx, (ctx)->args.targets);                                     \
		will_return(__wrap_dss_register_dbtree_classes, DER_SUCCESS);                      \
		EXPECT_BIO_NVME_INIT(&(ctx)->args, DER_SUCCESS);                                   \
		expect_value(__wrap_daos_register_key, key, &daos_srv_modkey);                     \
		expect_value(__wrap_daos_register_key, key, &vos_module_key);                      \
		will_return(mock_sm_init, DER_SUCCESS);                                            \
		will_return(__wrap_ds_tls_key_create, 0);                                          \
		EXPECT_VOS_STANDALONE_TLS_INIT(SYS_TAG, DER_SUCCESS);                              \
		EXPECT_VOS_SYS_DB_INIT(&(ctx)->args, DER_SUCCESS);                                 \
	} while (0)

#define EXPECT_XS_CREATE_ALL(ctx)                                                                  \
	do {                                                                                       \
		for (int ii = 0; ii <= (ctx)->args.targets; ++ii) {                                \
			EXPECT_DLCK_XSTREAM_CREATE(&(ctx)->xss[ii], DER_SUCCESS, MOCK_POOL(ii));   \
		}                                                                                  \
	} while (0)

/** one full nvme_polling ULT lifetime on the IDX stream concluding after ITERS iterations */
#define EXPECT_NVME_POLLING(IDX, ITERS)                                                            \
	do {                                                                                       \
		EXPECT_DLCK_ULT_CREATE(MOCK_POOL(IDX), DER_SUCCESS, MOCK_THREAD(IDX));             \
		EXPECT_DLCK_ENGINE_GET_MODULE_INFO();                                              \
		for (int jj = 0; jj < (ITERS) - 1; ++jj) {                                         \
			EXPECT_BIO_NVME_POLL(Mock_xs_ctxt, 0);                                     \
			will_return(__wrap_ABT_thread_yield, ABT_SUCCESS);                         \
			EXPECT_ABT_EVENTUAL_TEST(MOCK_EVENTUAL(IDX), ABT_SUCCESS, ABT_FALSE);      \
		}                                                                                  \
		EXPECT_BIO_NVME_POLL(Mock_xs_ctxt, 0);                                             \
		will_return(__wrap_ABT_thread_yield, ABT_SUCCESS);                                 \
		EXPECT_ABT_EVENTUAL_TEST(MOCK_EVENTUAL(IDX), ABT_SUCCESS, ABT_TRUE);               \
	} while (0)

/** successful initialization of the daos_sys_0 execution stream (at the IDX position) */
#define EXPECT_XS_INIT_SYS(IDX, CONFIGURED, POLL_ITERS)                                            \
	do {                                                                                       \
		EXPECT_DLCK_ULT_CREATE(MOCK_POOL(IDX), DER_SUCCESS, Mock_thread);                  \
		EXPECT_DSS_TLS_INIT(SYS_TAG, 0, -1, &Mock_dtls);                                   \
		EXPECT_BIO_NVME_CONFIGURED(CONFIGURED);                                            \
		if (CONFIGURED) {                                                                  \
			EXPECT_DLCK_ENGINE_GET_MODULE_INFO();                                      \
			EXPECT_BIO_XSCTXT_ALLOC(-1, DER_SUCCESS, Mock_xs_ctxt);                    \
			EXPECT_ABT_EVENTUAL_CREATE(ABT_SUCCESS, MOCK_EVENTUAL(IDX));               \
			EXPECT_NVME_POLLING(IDX, POLL_ITERS);                                      \
		}                                                                                  \
		EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);                                  \
	} while (0)

/** a fini ULT finding no TLS (nothing to finalize) followed by freeing the IDX stream */
#define EXPECT_XS_STOP_NO_TLS(ctx, IDX, POOL)                                                      \
	do {                                                                                       \
		EXPECT_DLCK_ULT_CREATE(POOL, DER_SUCCESS, Mock_thread);                            \
		will_return(__wrap_dss_tls_get, NULL);                                             \
		EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);                                  \
		EXPECT_DLCK_XSTREAM_FREE(&(ctx)->xss[IDX], DER_SUCCESS);                           \
	} while (0)

/** the finalization body of the IDX execution stream owning an NVMe context */
#define EXPECT_XS_FINI_NVME_BODY(IDX)                                                              \
	do {                                                                                       \
		EXPECT_ABT_EVENTUAL_SET(MOCK_EVENTUAL(IDX), ABT_SUCCESS);                          \
		EXPECT_ABT_THREAD_JOIN(MOCK_THREAD(IDX), ABT_SUCCESS);                             \
		EXPECT_ABT_THREAD_FREE(MOCK_THREAD(IDX), ABT_SUCCESS);                             \
		EXPECT_DLCK_ENGINE_GET_MODULE_INFO();                                              \
		expect_value(__wrap_bio_xsctxt_free, ctxt, Mock_xs_ctxt);                          \
		expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);                               \
	} while (0)

/** stopping the idle IDX execution stream after a successful fini ULT */
#define EXPECT_XS_STOPPED(ctx, IDX)                                                                \
	do {                                                                                       \
		EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);                                  \
		EXPECT_ABT_POOL_IS_EMPTY(MOCK_POOL(IDX), ABT_SUCCESS, ABT_TRUE);                   \
		EXPECT_DLCK_XSTREAM_FREE(&(ctx)->xss[IDX], DER_SUCCESS);                           \
	} while (0)

/** common expectations for finalizing engine-level resources */
#define EXPECT_ENGINE_FINI(ctx)                                                                    \
	do {                                                                                       \
		expect_function_call(__wrap_vos_db_fini);                                          \
		expect_function_call(__wrap_vos_standalone_tls_fini);                              \
		expect_function_call(__wrap_ds_tls_key_delete);                                    \
		will_return(mock_sm_fini, DER_SUCCESS);                                            \
		expect_value(__wrap_daos_unregister_key, key, &vos_module_key);                    \
		expect_value(__wrap_daos_unregister_key, key, &daos_srv_modkey);                   \
		expect_function_call(__wrap_bio_nvme_fini);                                        \
		EXPECT_ENGINE_FREE(ctx);                                                           \
	} while (0)

/** engines built manually for the exec and stop suites */

static void
engine_build(struct engine_ut_ctx *ctx)
{
	ctx->engine.targets   = ctx->args.targets;
	ctx->engine.xss       = ctx->xss;
	ctx->engine.join_fail = false;

	for (unsigned i = 0; i <= ctx->args.targets; ++i) {
		ctx->xss[i].tgt_id = (i == ctx->args.targets ? -1 : (int)i);
		ctx->xss[i].pool   = MOCK_POOL(i);
	}
}

static void
engine_build_nvme(struct engine_ut_ctx *ctx)
{
	engine_build(ctx);

	for (unsigned i = 0; i <= ctx->args.targets; ++i) {
		ctx->xss[i].nvme_poll.thread = MOCK_THREAD(i);
		ctx->xss[i].nvme_poll_done   = MOCK_EVENTUAL(i);
	}

	Mock_dmi.dmi_nvme_ctxt = Mock_xs_ctxt;
}

static void
exec_prepare(struct engine_ut_ctx *ctx)
{
	ctx->exec.arg_alloc_fn     = mock_arg_alloc_fn;
	ctx->exec.arg_alloc_fn_arg = &ctx->custom_alloc;
	ctx->exec.arg_free_fn      = mock_arg_free_fn;
	ctx->exec.arg_free_fn_arg  = &ctx->custom_free;
	ctx->exec.exec_one         = mock_exec_one;
	ctx->exec.ck               = NULL;
	ctx->exec.targets          = ctx->args.targets;
	ctx->exec.join_fail        = false;
}

/** build a job batch manually for the exec, join, and fini suites */
static void
exec_build(struct engine_ut_ctx *ctx)
{
	exec_prepare(ctx);
	ctx->exec.ults     = ctx->ults;
	ctx->exec.ult_args = ctx->ult_args;

	for (unsigned i = 0; i < ctx->args.targets; ++i) {
		ctx->ults[i].thread = ABT_THREAD_NULL;
		ctx->ult_args[i]    = &ctx->payload[i];
	}
}

/** setups & teardowns */

/** reset the context to the common defaults; the setups then set the target count and the rest */
static struct engine_ut_ctx *
ctx_reset(void **state)
{
	struct engine_ut_ctx *ctx     = &Ctx;
	unsigned              targets = (unsigned)(uintptr_t)*state;

	memset(ctx, 0, sizeof(*ctx));
	memset(&Mock_dmi, 0, sizeof(Mock_dmi));

	ctx->args.targets            = targets;
	ctx->args.numa_node          = 1;
	ctx->args.max_dma_buf_size   = 262144;
	ctx->args.nvme_hugepage_size = 2048;
	ctx->args.storage_path       = "mock-storage-path";
	ctx->args.nvme_conf          = "mock-nvme-conf";

	*state = ctx;

	return ctx;
}

static int
setup_reset(void **state)
{
	(void)ctx_reset(state);

	return 0;
}

static int
setup_engine_exec_build_prep(void **state)
{
	struct engine_ut_ctx *ctx = ctx_reset(state);

	engine_build(ctx);
	exec_prepare(ctx);

	return 0;
}

static int
setup_engine_exec_build(void **state)
{
	struct engine_ut_ctx *ctx = ctx_reset(state);

	engine_build(ctx);
	exec_build(ctx);

	return 0;
}

static int
setup_engine_build(void **state)
{
	struct engine_ut_ctx *ctx = ctx_reset(state);

	engine_build(ctx);

	return 0;
}

static int
setup_engine_build_nvme(void **state)
{
	struct engine_ut_ctx *ctx = ctx_reset(state);

	engine_build_nvme(ctx);

	return 0;
}

/** tests - dlck_engine_xstream_arg_alloc/free */

/** verify a successfully allocated argument points at the right execution stream */
static void
test_dlck_engine_xstream_arg_alloc_success(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	struct xstream_arg    xa  = {0};
	int                   ctrl;
	void                 *arg = NULL;
	int                   rc;

	EXPECT_D_CALLOC(1, sizeof(struct xstream_arg), &xa);

	rc = dlck_engine_xstream_arg_alloc(&ctx->engine, 1, &ctrl, &arg);

	assert_int_equal(rc, DER_SUCCESS);
	assert_ptr_equal(arg, &xa);
	assert_ptr_equal(xa.ctrl, &ctrl);
	assert_ptr_equal(xa.engine, &ctx->engine);
	assert_ptr_equal(xa.xs, &ctx->xss[1]);
	assert_int_equal(xa.status.rc, DER_SUCCESS);
}

/** verify an argument allocation failure is reported as -DER_NOMEM */
static void
test_dlck_engine_xstream_arg_alloc_failure(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   ctrl;
	void                 *arg = NULL;
	int                   rc;

	EXPECT_D_CALLOC(1, sizeof(struct xstream_arg), NULL);

	rc = dlck_engine_xstream_arg_alloc(&ctx->engine, 1, &ctrl, &arg);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(arg);
}

/** verify freeing a NULL argument is a no-op */
static void
test_dlck_engine_xstream_arg_free_null(void **unused)
{
	struct checker_status sts[1] = {{0}};
	void                 *arg    = NULL;

	dlck_engine_xstream_arg_free(sts, &arg);

	assert_null(arg);
}

/** verify the ULT's status is collected into its target's slot while freeing its argument */
static void
test_dlck_engine_xstream_arg_free_collects_outputs(void **state)
{
	struct engine_ut_ctx *ctx    = *state;
	struct xstream_arg    xa     = {.xs = &ctx->xss[1]};
	struct checker_status sts[2] = {{0}};
	void                 *arg    = &xa;

	xa.status.rc           = -DER_IO;
	xa.status.warnings_num = 3;
	xa.status.errors_num   = 2;
	expect_value(__wrap_d_free, ptr, &xa);

	dlck_engine_xstream_arg_free(sts, &arg);

	assert_int_equal(sts[1].rc, -DER_IO);
	assert_int_equal(sts[1].warnings_num, 3);
	assert_int_equal(sts[1].errors_num, 2);
	assert_null(arg);
}

/** tests - dlck_exec_init */

/** verify the batch allocation prepares the arguments of all the targets */
static void
test_dlck_exec_init_success(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	struct dlck_exec     *de  = &ctx->exec;
	int                   rc;

	EXPECT_EXEC_INIT_ALLOC(ctx, 2);
	for (int i = 0; i < 2; ++i) {
		EXPECT_mock_arg_alloc_fn(&ctx->engine, i, &ctx->custom_alloc, DER_SUCCESS,
					 &ctx->payload[i]);
	}

	rc = dlck_exec_init(&ctx->engine, de);

	assert_int_equal(rc, DER_SUCCESS);
	assert_ptr_equal(de, &ctx->exec);
	assert_int_equal(de->targets, 2);
	assert_ptr_equal(de->exec_one, mock_exec_one);
	assert_ptr_equal(de->arg_alloc_fn, mock_arg_alloc_fn);
	assert_ptr_equal(de->arg_alloc_fn_arg, &ctx->custom_alloc);
	assert_ptr_equal(de->arg_free_fn, mock_arg_free_fn);
	assert_ptr_equal(de->arg_free_fn_arg, &ctx->custom_free);
	assert_ptr_equal(de->ults, ctx->ults);
	assert_ptr_equal(de->ult_args, ctx->ult_args);
	for (int i = 0; i < 2; ++i) {
		assert_ptr_equal(de->ult_args[i], &ctx->payload[i]);
		assert_ptr_equal(de->ults[i].thread, ABT_THREAD_NULL);
	}
	assert_false(de->join_fail);
}

/** verify an allocation failure of the ULTs array frees the batch */
static void
test_dlck_exec_init_ults_alloc_failure(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	struct dlck_exec     *de  = &ctx->exec;
	int                   rc;

	EXPECT_D_CALLOC(2, sizeof(struct dlck_ult), NULL);

	rc = dlck_exec_init(&ctx->engine, de);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(de->ults);
}

/** verify an allocation failure of the arguments array frees the ULTs array and the batch */
static void
test_dlck_exec_init_args_alloc_failure(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	struct dlck_exec     *de  = &ctx->exec;
	int                   rc;

	EXPECT_D_CALLOC(2, sizeof(struct dlck_ult), ctx->ults);
	EXPECT_D_CALLOC(2, sizeof(void *), NULL);
	expect_value(__wrap_d_free, ptr, ctx->ults);

	rc = dlck_exec_init(&ctx->engine, de);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(de->ults);
	assert_null(de->ult_args);
}

/** verify an argument allocation failure frees the already allocated arguments and the batch */
static void
test_dlck_exec_init_arg_alloc_failure(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	struct dlck_exec     *de  = &ctx->exec;
	int                   rc;

	EXPECT_EXEC_INIT_ALLOC(ctx, 2);
	EXPECT_mock_arg_alloc_fn(&ctx->engine, 0, &ctx->custom_alloc, DER_SUCCESS,
				 &ctx->payload[0]);
	EXPECT_mock_arg_alloc_fn(&ctx->engine, 1, &ctx->custom_alloc, -DER_IO, NULL);
	EXPECT_mock_arg_free_fn(&ctx->custom_free, &ctx->payload[0]);
	EXPECT_mock_arg_free_fn(&ctx->custom_free, NULL);
	EXPECT_EXEC_FINI_FREE(ctx);

	rc = dlck_exec_init(&ctx->engine, de);

	assert_int_equal(rc, -DER_IO);
	assert_null(de->ults);
	assert_null(de->ult_args);
}

/** tests - dlck_engine_exec */

/** verify each successful ULT handle is retained in its target's batch entry */
static void
test_dlck_engine_exec_success(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	for (int i = 0; i < 2; ++i) {
		EXPECT_DLCK_ULT_CREATE(MOCK_POOL(i), DER_SUCCESS, MOCK_THREAD(i));
		expect_value(mock_exec_one, arg, &ctx->payload[i]);
	}

	rc = dlck_engine_exec(&ctx->engine, &ctx->exec);

	assert_int_equal(rc, DER_SUCCESS);
	for (int i = 0; i < 2; ++i) {
		assert_ptr_equal(ctx->exec.ults[i].thread, MOCK_THREAD(i));
	}
	assert_false(ctx->exec.join_fail);
}

/** verify a ULT creation failure joins the already started ULTs */
static void
test_dlck_engine_exec_ult_create_failure(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	EXPECT_DLCK_ULT_CREATE(MOCK_POOL(0), DER_SUCCESS, MOCK_THREAD(0));
	expect_value(mock_exec_one, arg, &ctx->payload[0]);
	EXPECT_DLCK_ULT_CREATE(MOCK_POOL(1), -DER_NOMEM, NULL);
	/** cleanup: the started ULT is joined and freed */
	EXPECT_ABT_THREAD_JOIN(MOCK_THREAD(0), ABT_SUCCESS);
	EXPECT_ABT_THREAD_FREE(MOCK_THREAD(0), ABT_SUCCESS);

	rc = dlck_engine_exec(&ctx->engine, &ctx->exec);

	assert_int_equal(rc, -DER_NOMEM);
	for (int i = 0; i < 2; ++i) {
		assert_ptr_equal(ctx->exec.ults[i].thread, ABT_THREAD_NULL);
	}
	assert_false(ctx->exec.join_fail);
}

/** tests - dlck_exec_join_no_error */

/** verify successfully joined ULTs are freed without setting join_fail */
static void
test_dlck_exec_join_no_error_success(void **state)
{
	struct engine_ut_ctx *ctx = *state;

	for (int i = 0; i < 2; ++i) {
		ctx->exec.ults[i].thread = MOCK_THREAD(i);
		EXPECT_ABT_THREAD_JOIN(MOCK_THREAD(i), ABT_SUCCESS);
		EXPECT_ABT_THREAD_FREE(MOCK_THREAD(i), ABT_SUCCESS);
	}

	dlck_exec_join_no_error(&ctx->exec);

	for (int i = 0; i < 2; ++i) {
		assert_ptr_equal(ctx->exec.ults[i].thread, ABT_THREAD_NULL);
	}
	assert_false(ctx->exec.join_fail);
}

/** verify a failed join preserves its thread while later ULTs are still joined */
static void
test_dlck_exec_join_no_error_join_failure(void **state)
{
	struct engine_ut_ctx *ctx = *state;

	ctx->exec.ults[0].thread = MOCK_THREAD(0);
	ctx->exec.ults[1].thread = MOCK_THREAD(1);
	EXPECT_ABT_THREAD_JOIN(MOCK_THREAD(0), ABT_ERR_INV_THREAD);
	EXPECT_ABT_THREAD_JOIN(MOCK_THREAD(1), ABT_SUCCESS);
	EXPECT_ABT_THREAD_FREE(MOCK_THREAD(1), ABT_SUCCESS);

	dlck_exec_join_no_error(&ctx->exec);

	assert_ptr_equal(ctx->exec.ults[0].thread, MOCK_THREAD(0));
	assert_ptr_equal(ctx->exec.ults[1].thread, ABT_THREAD_NULL);
	assert_true(ctx->exec.join_fail);
}

/** tests - dlck_exec_join */

/** verify all the ULTs are joined and freed on the happy path */
static void
test_dlck_exec_join_success(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	for (int i = 0; i < 2; ++i) {
		ctx->exec.ults[i].thread = MOCK_THREAD(i);
		EXPECT_ABT_THREAD_JOIN(MOCK_THREAD(i), ABT_SUCCESS);
		EXPECT_ABT_THREAD_FREE(MOCK_THREAD(i), ABT_SUCCESS);
	}

	rc = dlck_exec_join(&ctx->exec);

	assert_int_equal(rc, DER_SUCCESS);
	for (int i = 0; i < 2; ++i) {
		assert_ptr_equal(ctx->exec.ults[i].thread, ABT_THREAD_NULL);
	}
	assert_false(ctx->exec.join_fail);
}

/** verify a join failure keeps the thread handle and the remaining ULTs are still joined */
static void
test_dlck_exec_join_join_failure(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	ctx->exec.ults[0].thread = MOCK_THREAD(0);
	ctx->exec.ults[1].thread = MOCK_THREAD(1);
	EXPECT_ABT_THREAD_JOIN(MOCK_THREAD(0), ABT_ERR_INV_THREAD);
	EXPECT_ABT_THREAD_JOIN(MOCK_THREAD(1), ABT_SUCCESS);
	EXPECT_ABT_THREAD_FREE(MOCK_THREAD(1), ABT_SUCCESS);

	rc = dlck_exec_join(&ctx->exec);

	assert_int_equal(rc, -DER_INVAL);
	assert_ptr_equal(ctx->exec.ults[0].thread, MOCK_THREAD(0));
	assert_ptr_equal(ctx->exec.ults[1].thread, ABT_THREAD_NULL);
	assert_true(ctx->exec.join_fail);
}

/** verify a thread free failure is reported but the joined ULT's handle is abandoned */
static void
test_dlck_exec_join_thread_free_failure(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	ctx->exec.ults[0].thread = MOCK_THREAD(0);
	ctx->exec.ults[1].thread = MOCK_THREAD(1);
	EXPECT_ABT_THREAD_JOIN(MOCK_THREAD(0), ABT_SUCCESS);
	EXPECT_ABT_THREAD_FREE(MOCK_THREAD(0), ABT_ERR_INV_THREAD);
	EXPECT_ABT_THREAD_JOIN(MOCK_THREAD(1), ABT_SUCCESS);
	EXPECT_ABT_THREAD_FREE(MOCK_THREAD(1), ABT_SUCCESS);

	rc = dlck_exec_join(&ctx->exec);

	assert_int_equal(rc, -DER_INVAL);
	assert_false(ctx->exec.join_fail);
	assert_ptr_equal(ctx->exec.ults[0].thread, ABT_THREAD_NULL);
}

/** tests - dlck_exec_fini */

/** verify all the arguments are freed and the batch is released */
static void
test_dlck_exec_fini_success(void **state)
{
	struct engine_ut_ctx *ctx = *state;

	EXPECT_mock_arg_free_fn(&ctx->custom_free, &ctx->payload[0]);
	EXPECT_mock_arg_free_fn(&ctx->custom_free, &ctx->payload[1]);
	EXPECT_EXEC_FINI_FREE(ctx);

	dlck_exec_fini(&ctx->exec);
}

/** verify the argument of a ULT which failed to join is left out */
static void
test_dlck_exec_fini_skips_unjoined(void **state)
{
	struct engine_ut_ctx *ctx = *state;

	ctx->exec.ults[0].thread = MOCK_THREAD(0);
	EXPECT_mock_arg_free_fn(&ctx->custom_free, &ctx->payload[1]);
	EXPECT_EXEC_FINI_FREE(ctx);

	dlck_exec_fini(&ctx->exec);
}

/** tests - dlck_engine_start */

/** verify the full initialization sequence with two targets and NVMe configured */
static void
test_dlck_engine_init_success(void **state)
{
	struct engine_ut_ctx *ctx    = *state;
	struct dlck_engine   *engine = NULL;
	int                   rc;

	EXPECT_ENGINE_INIT(ctx);

	rc = dlck_engine_init(&ctx->args, &engine);

	assert_int_equal(rc, DER_SUCCESS);
	assert_ptr_equal(engine, &ctx->engine);
	assert_int_equal(engine->targets, 2);
	assert_ptr_equal(engine->xss, ctx->xss);
	assert_false(engine->join_fail);
}

/** verify initialization succeeds with zero targets */
static void
test_dlck_engine_init_zero_targets(void **state)
{
	struct engine_ut_ctx *ctx    = *state;
	struct dlck_engine   *engine = NULL;
	int                   rc;

	EXPECT_ENGINE_INIT(ctx);

	rc = dlck_engine_init(&ctx->args, &engine);

	assert_int_equal(rc, DER_SUCCESS);
	assert_ptr_equal(engine, &ctx->engine);
	assert_int_equal(engine->targets, 0);
	assert_ptr_equal(engine->xss, ctx->xss);
	assert_false(engine->join_fail);
}

/** verify an engine allocation failure is reported as -DER_NOMEM */
static void
test_dlck_engine_init_engine_alloc_failure(void **state)
{
	struct engine_ut_ctx *ctx    = *state;
	struct dlck_engine   *engine = NULL;
	int                   rc;

	EXPECT_D_CALLOC(1, sizeof(struct dlck_engine), NULL);

	rc = dlck_engine_init(&ctx->args, &engine);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(engine);
}

/** verify an execution streams array allocation failure frees the engine */
static void
test_dlck_engine_init_xss_alloc_failure(void **state)
{
	struct engine_ut_ctx *ctx    = *state;
	struct dlck_engine   *engine = NULL;
	int                   rc;

	EXPECT_D_CALLOC(1, sizeof(struct dlck_engine), &ctx->engine);
	EXPECT_D_CALLOC(ctx->args.targets + 1, sizeof(struct dlck_xstream), NULL);
	expect_value(__wrap_d_free, ptr, &ctx->engine);

	rc = dlck_engine_init(&ctx->args, &engine);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(engine);
}

/** verify a dbtree classes registration failure frees the engine */
static void
test_dlck_engine_init_dbtree_classes_failure(void **state)
{
	struct engine_ut_ctx *ctx    = *state;
	struct dlck_engine   *engine = NULL;
	int                   rc;

	EXPECT_ENGINE_ALLOC(ctx, 2);
	will_return(__wrap_dss_register_dbtree_classes, -DER_NOMEM);
	EXPECT_ENGINE_FREE(ctx);

	rc = dlck_engine_init(&ctx->args, &engine);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(engine);
}

/** verify a bio_nvme_init() failure frees the engine */
static void
test_dlck_engine_init_bio_nvme_init_failure(void **state)
{
	struct engine_ut_ctx *ctx    = *state;
	struct dlck_engine   *engine = NULL;
	int                   rc;

	EXPECT_ENGINE_ALLOC(ctx, 2);
	will_return(__wrap_dss_register_dbtree_classes, DER_SUCCESS);
	EXPECT_BIO_NVME_INIT(&ctx->args, -DER_INVAL);
	EXPECT_ENGINE_FREE(ctx);

	rc = dlck_engine_init(&ctx->args, &engine);

	assert_int_equal(rc, -DER_INVAL);
	assert_null(engine);
}

/** verify a VOS module init failure unregisters the keys and finalizes NVMe */
static void
test_dlck_engine_init_sm_init_failure(void **state)
{
	struct engine_ut_ctx *ctx    = *state;
	struct dlck_engine   *engine = NULL;
	int                   rc;

	EXPECT_ENGINE_ALLOC(ctx, 2);
	will_return(__wrap_dss_register_dbtree_classes, DER_SUCCESS);
	EXPECT_BIO_NVME_INIT(&ctx->args, DER_SUCCESS);
	expect_value(__wrap_daos_register_key, key, &daos_srv_modkey);
	expect_value(__wrap_daos_register_key, key, &vos_module_key);
	will_return(mock_sm_init, -DER_NOMEM);
	expect_value(__wrap_daos_unregister_key, key, &vos_module_key);
	expect_value(__wrap_daos_unregister_key, key, &daos_srv_modkey);
	expect_function_call(__wrap_bio_nvme_fini);
	EXPECT_ENGINE_FREE(ctx);

	rc = dlck_engine_init(&ctx->args, &engine);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(engine);
}

/** verify a TLS key creation failure maps errno and finalizes the VOS module */
static void
test_dlck_engine_init_tls_key_create_failure(void **state)
{
	struct engine_ut_ctx *ctx    = *state;
	struct dlck_engine   *engine = NULL;
	int                   rc;

	EXPECT_ENGINE_ALLOC(ctx, 2);
	will_return(__wrap_dss_register_dbtree_classes, DER_SUCCESS);
	EXPECT_BIO_NVME_INIT(&ctx->args, DER_SUCCESS);
	expect_value(__wrap_daos_register_key, key, &daos_srv_modkey);
	expect_value(__wrap_daos_register_key, key, &vos_module_key);
	will_return(mock_sm_init, DER_SUCCESS);
	will_return(__wrap_ds_tls_key_create, ENOMEM);
	will_return(mock_sm_fini, DER_SUCCESS);
	expect_value(__wrap_daos_unregister_key, key, &vos_module_key);
	expect_value(__wrap_daos_unregister_key, key, &daos_srv_modkey);
	expect_function_call(__wrap_bio_nvme_fini);
	EXPECT_ENGINE_FREE(ctx);

	rc = dlck_engine_init(&ctx->args, &engine);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(engine);
}

/** verify a standalone VOS TLS init failure deletes the TLS key as well */
static void
test_dlck_engine_init_vos_tls_init_failure(void **state)
{
	struct engine_ut_ctx *ctx    = *state;
	struct dlck_engine   *engine = NULL;
	int                   rc;

	EXPECT_ENGINE_ALLOC(ctx, 2);
	will_return(__wrap_dss_register_dbtree_classes, DER_SUCCESS);
	EXPECT_BIO_NVME_INIT(&ctx->args, DER_SUCCESS);
	expect_value(__wrap_daos_register_key, key, &daos_srv_modkey);
	expect_value(__wrap_daos_register_key, key, &vos_module_key);
	will_return(mock_sm_init, DER_SUCCESS);
	will_return(__wrap_ds_tls_key_create, 0);
	EXPECT_VOS_STANDALONE_TLS_INIT(SYS_TAG, -DER_NOMEM);
	expect_function_call(__wrap_ds_tls_key_delete);
	will_return(mock_sm_fini, DER_SUCCESS);
	expect_value(__wrap_daos_unregister_key, key, &vos_module_key);
	expect_value(__wrap_daos_unregister_key, key, &daos_srv_modkey);
	expect_function_call(__wrap_bio_nvme_fini);
	EXPECT_ENGINE_FREE(ctx);

	rc = dlck_engine_init(&ctx->args, &engine);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(engine);
}

/** verify a system DB init failure finalizes the standalone VOS TLS as well */
static void
test_dlck_engine_init_vos_sys_db_init_failure(void **state)
{
	struct engine_ut_ctx *ctx    = *state;
	struct dlck_engine   *engine = NULL;
	int                   rc;

	EXPECT_ENGINE_ALLOC(ctx, 2);
	will_return(__wrap_dss_register_dbtree_classes, DER_SUCCESS);
	EXPECT_BIO_NVME_INIT(&ctx->args, DER_SUCCESS);
	expect_value(__wrap_daos_register_key, key, &daos_srv_modkey);
	expect_value(__wrap_daos_register_key, key, &vos_module_key);
	will_return(mock_sm_init, DER_SUCCESS);
	will_return(__wrap_ds_tls_key_create, 0);
	EXPECT_VOS_STANDALONE_TLS_INIT(SYS_TAG, DER_SUCCESS);
	EXPECT_VOS_SYS_DB_INIT(&ctx->args, -DER_IO);
	expect_function_call(__wrap_vos_standalone_tls_fini);
	expect_function_call(__wrap_ds_tls_key_delete);
	will_return(mock_sm_fini, DER_SUCCESS);
	expect_value(__wrap_daos_unregister_key, key, &vos_module_key);
	expect_value(__wrap_daos_unregister_key, key, &daos_srv_modkey);
	expect_function_call(__wrap_bio_nvme_fini);
	EXPECT_ENGINE_FREE(ctx);

	rc = dlck_engine_init(&ctx->args, &engine);

	assert_int_equal(rc, -DER_IO);
	assert_null(engine);
}

/** verify an execution stream creation failure stops previously created streams */
static void
test_dlck_engine_start_xs_create_failure(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	EXPECT_DLCK_XSTREAM_CREATE(&ctx->xss[0], -DER_NOMEM, NULL);
	/** the no-error stop still runs a fini ULT; there is no TLS to finalize */
	EXPECT_XS_STOP_NO_TLS(ctx, 0, MOCK_POOL(0));

	rc = dlck_engine_start(&ctx->engine);
	assert_int_equal(rc, -DER_NOMEM);
}

/** verify a sys execution stream TLS initialization failure stops created streams */
static void
test_dlck_engine_start_xs_init_tls_failure(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	EXPECT_XS_CREATE_ALL(ctx);
	EXPECT_DLCK_ULT_CREATE(MOCK_POOL(0), DER_SUCCESS, Mock_thread);
	EXPECT_DSS_TLS_INIT(SYS_TAG, 0, -1, NULL);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_XS_STOP_NO_TLS(ctx, 0, MOCK_POOL(0));

	rc = dlck_engine_start(&ctx->engine);
	assert_int_equal(rc, -DER_NOMEM);
}

/** verify an NVMe context allocation failure finalizes the TLS of the failed stream */
static void
test_dlck_engine_start_xs_init_xsctxt_failure(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	EXPECT_XS_CREATE_ALL(ctx);
	EXPECT_XS_INIT_SYS(1, true, 1);
	/** the io execution stream fails to allocate its NVMe context */
	EXPECT_DLCK_ULT_CREATE(MOCK_POOL(0), DER_SUCCESS, Mock_thread);
	EXPECT_DSS_TLS_INIT(DAOS_SERVER_TAG,
			    DSS_MAIN_XS_ID_WITH_HELPER_POOL(0, DSS_SYS_XS_NR_DEFAULT), 0,
			    &Mock_dtls);
	EXPECT_DLCK_ENGINE_GET_MODULE_INFO();
	EXPECT_BIO_XSCTXT_ALLOC(0, -DER_NOMEM, NULL);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	/** the no-error stop finalizes the io stream (no TLS) and the fully set up sys stream */
	EXPECT_XS_STOP_NO_TLS(ctx, 0, MOCK_POOL(0));
	EXPECT_DLCK_ULT_CREATE(MOCK_POOL(1), DER_SUCCESS, Mock_thread);
	will_return(__wrap_dss_tls_get, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(true);
	EXPECT_XS_FINI_NVME_BODY(1);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_DLCK_XSTREAM_FREE(&ctx->xss[1], DER_SUCCESS);

	rc = dlck_engine_start(&ctx->engine);
	assert_int_equal(rc, -DER_NOMEM);
}

/** verify an eventual creation failure frees the NVMe context and finalizes the TLS */
static void
test_dlck_engine_start_xs_init_eventual_failure(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	EXPECT_XS_CREATE_ALL(ctx);
	EXPECT_DLCK_ULT_CREATE(MOCK_POOL(0), DER_SUCCESS, Mock_thread);
	EXPECT_DSS_TLS_INIT(SYS_TAG, 0, -1, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(true);
	EXPECT_DLCK_ENGINE_GET_MODULE_INFO();
	EXPECT_BIO_XSCTXT_ALLOC(-1, DER_SUCCESS, Mock_xs_ctxt);
	EXPECT_ABT_EVENTUAL_CREATE(ABT_ERR_MEM, NULL);
	expect_value(__wrap_bio_xsctxt_free, ctxt, Mock_xs_ctxt);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_XS_STOP_NO_TLS(ctx, 0, MOCK_POOL(0));

	rc = dlck_engine_start(&ctx->engine);
	assert_int_equal(rc, -DER_NOMEM);
}

/** verify an NVMe polling ULT creation failure frees the eventual, the context, and the TLS */
static void
test_dlck_engine_start_xs_init_polling_ult_failure(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	EXPECT_XS_CREATE_ALL(ctx);
	EXPECT_DLCK_ULT_CREATE(MOCK_POOL(0), DER_SUCCESS, Mock_thread);
	EXPECT_DSS_TLS_INIT(SYS_TAG, 0, -1, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(true);
	EXPECT_DLCK_ENGINE_GET_MODULE_INFO();
	EXPECT_BIO_XSCTXT_ALLOC(-1, DER_SUCCESS, Mock_xs_ctxt);
	EXPECT_ABT_EVENTUAL_CREATE(ABT_SUCCESS, MOCK_EVENTUAL(0));
	EXPECT_DLCK_ULT_CREATE(MOCK_POOL(0), -DER_NOMEM, NULL);
	EXPECT_ABT_EVENTUAL_FREE(MOCK_EVENTUAL(0), ABT_SUCCESS);
	expect_value(__wrap_bio_xsctxt_free, ctxt, Mock_xs_ctxt);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_XS_STOP_NO_TLS(ctx, 0, MOCK_POOL(0));

	rc = dlck_engine_start(&ctx->engine);
	assert_int_equal(rc, -DER_NOMEM);
}

/** verify the NVMe polling ULT stops polling when the eventual test fails */
static void
test_dlck_engine_start_polling_eventual_test_success(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	EXPECT_XS_CREATE_ALL(ctx);
	EXPECT_DLCK_ULT_CREATE(MOCK_POOL(0), DER_SUCCESS, Mock_thread);
	EXPECT_DSS_TLS_INIT(SYS_TAG, 0, -1, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(true);
	EXPECT_DLCK_ENGINE_GET_MODULE_INFO();
	EXPECT_BIO_XSCTXT_ALLOC(-1, DER_SUCCESS, Mock_xs_ctxt);
	EXPECT_ABT_EVENTUAL_CREATE(ABT_SUCCESS, MOCK_EVENTUAL(0));
	/** the polling ULT concludes on the eventual test error instead of the readiness */
	EXPECT_DLCK_ULT_CREATE(MOCK_POOL(0), DER_SUCCESS, MOCK_THREAD(0));
	EXPECT_DLCK_ENGINE_GET_MODULE_INFO();
	EXPECT_BIO_NVME_POLL(Mock_xs_ctxt, 0);
	will_return(__wrap_ABT_thread_yield, ABT_SUCCESS);
	EXPECT_ABT_EVENTUAL_TEST(MOCK_EVENTUAL(0), ABT_ERR_INV_EVENTUAL, ABT_FALSE);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);

	rc = dlck_engine_start(&ctx->engine);
	assert_int_equal(rc, DER_SUCCESS);
}

/** tests - dlck_engine_stop */

/** verify the stop sequence finalizes both sys and io streams */
static void
test_dlck_engine_stop_success(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	/** the io execution stream owns an NVMe context */
	EXPECT_DLCK_ULT_CREATE(MOCK_POOL(0), DER_SUCCESS, Mock_thread);
	will_return(__wrap_dss_tls_get, &Mock_dtls);
	EXPECT_XS_FINI_NVME_BODY(0);
	EXPECT_XS_STOPPED(ctx, 0);
	/** the sys execution stream has no NVMe configured */
	EXPECT_DLCK_ULT_CREATE(MOCK_POOL(1), DER_SUCCESS, Mock_thread);
	will_return(__wrap_dss_tls_get, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(false);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_XS_STOPPED(ctx, 1);

	rc = dlck_engine_stop(&ctx->engine);

	assert_int_equal(rc, DER_SUCCESS);
}

/** verify a stop attempt with a failed join reports -DER_BUSY without touching anything */
static void
test_dlck_engine_stop_join_fail(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	ctx->engine.join_fail = true;

	rc = dlck_engine_stop(&ctx->engine);

	assert_int_equal(rc, -DER_BUSY);
}

/** verify a fini ULT failure stops the engine teardown and frees the streams */
static void
test_dlck_engine_stop_fini_failure(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	EXPECT_DLCK_ULT_CREATE(MOCK_POOL(0), DER_SUCCESS, Mock_thread);
	will_return(__wrap_dss_tls_get, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(true);
	EXPECT_ABT_EVENTUAL_SET(MOCK_EVENTUAL(0), ABT_ERR_INV_EVENTUAL);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	/** the no-error stop retries the fini ULT */
	EXPECT_XS_STOP_NO_TLS(ctx, 0, MOCK_POOL(0));

	rc = dlck_engine_stop(&ctx->engine);

	assert_int_equal(rc, -DER_INVAL);
}

/** verify a busy pool prevents the execution stream from being freed */
static void
test_dlck_engine_stop_pool_not_empty(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	EXPECT_DLCK_ULT_CREATE(MOCK_POOL(0), DER_SUCCESS, Mock_thread);
	will_return(__wrap_dss_tls_get, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(false);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_ABT_POOL_IS_EMPTY(MOCK_POOL(0), ABT_SUCCESS, ABT_FALSE);
	/** the no-error stop retries the fini ULT */
	EXPECT_XS_STOP_NO_TLS(ctx, 0, MOCK_POOL(0));

	rc = dlck_engine_stop(&ctx->engine);

	assert_int_equal(rc, -DER_BUSY);
}

/** verify an execution stream free failure falls back to the no-error stop */
static void
test_dlck_engine_stop_xs_free_failure(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	EXPECT_DLCK_ULT_CREATE(MOCK_POOL(0), DER_SUCCESS, Mock_thread);
	will_return(__wrap_dss_tls_get, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(false);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_ABT_POOL_IS_EMPTY(MOCK_POOL(0), ABT_SUCCESS, ABT_TRUE);
	EXPECT_DLCK_XSTREAM_FREE(&ctx->xss[0], -DER_INVAL);
	/** the no-error stop retries the fini ULT */
	EXPECT_XS_STOP_NO_TLS(ctx, 0, MOCK_POOL(0));

	rc = dlck_engine_stop(&ctx->engine);

	assert_int_equal(rc, -DER_INVAL);
}

/** verify a successful engine fini tears down all the modules and frees the engine */
static void
test_dlck_engine_fini_success(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	EXPECT_ENGINE_FINI(ctx);

	rc = dlck_engine_fini(&ctx->engine);
	assert_int_equal(rc, DER_SUCCESS);
}

/** verify a VOS module fini failure stops the teardown without freeing the engine */
static void
test_dlck_engine_fini_sm_fini_failure(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	expect_function_call(__wrap_vos_db_fini);
	expect_function_call(__wrap_vos_standalone_tls_fini);
	expect_function_call(__wrap_ds_tls_key_delete);
	will_return(mock_sm_fini, -DER_BUSY);

	rc = dlck_engine_fini(&ctx->engine);

	assert_int_equal(rc, -DER_BUSY);
}

/** verify a fini ULT creation failure leaves the execution streams untouched */
static void
test_dlck_engine_stop_fini_ult_create_failure(void **state)
{
	struct engine_ut_ctx *ctx = *state;
	int                   rc;

	EXPECT_DLCK_ULT_CREATE(MOCK_POOL(0), -DER_NOMEM, NULL);
	/** the no-error stop cannot create the fini ULT either */
	EXPECT_DLCK_ULT_CREATE(MOCK_POOL(0), -DER_NOMEM, NULL);

	rc = dlck_engine_stop(&ctx->engine);

	assert_int_equal(rc, -DER_NOMEM);
}

/** the target count is passed as the CMocka initial state and read by the setups */
#define TEST_TARGETS(ID, FUNC, TEST_SETUP, TARGETS)                                                \
	{"DLCK_ENGINE_" #ID ": " #FUNC, FUNC, TEST_SETUP, NULL, (void *)(uintptr_t)(TARGETS)}

static const struct CMUnitTest dlck_engine_tests[] = {
    TEST_TARGETS(101, test_dlck_engine_xstream_arg_alloc_success, setup_engine_build, 2),
    TEST_TARGETS(102, test_dlck_engine_xstream_arg_alloc_failure, setup_engine_build, 2),
    TEST_TARGETS(103, test_dlck_engine_xstream_arg_free_null, NULL, 0),
    TEST_TARGETS(104, test_dlck_engine_xstream_arg_free_collects_outputs, setup_engine_build, 2),
    TEST_TARGETS(105, test_dlck_exec_init_success, setup_engine_exec_build_prep, 2),
    TEST_TARGETS(106, test_dlck_exec_init_ults_alloc_failure, setup_engine_exec_build_prep, 2),
    TEST_TARGETS(107, test_dlck_exec_init_args_alloc_failure, setup_engine_exec_build_prep, 2),
    TEST_TARGETS(108, test_dlck_exec_init_arg_alloc_failure, setup_engine_exec_build_prep, 2),
    TEST_TARGETS(109, test_dlck_engine_exec_success, setup_engine_exec_build, 2),
    TEST_TARGETS(110, test_dlck_engine_exec_ult_create_failure, setup_engine_exec_build, 2),
    TEST_TARGETS(111, test_dlck_exec_join_no_error_success, setup_engine_exec_build, 2),
    TEST_TARGETS(112, test_dlck_exec_join_no_error_join_failure, setup_engine_exec_build, 2),
    TEST_TARGETS(113, test_dlck_exec_join_success, setup_engine_exec_build, 2),
    TEST_TARGETS(114, test_dlck_exec_join_join_failure, setup_engine_exec_build, 2),
    TEST_TARGETS(115, test_dlck_exec_join_thread_free_failure, setup_engine_exec_build, 2),
    TEST_TARGETS(116, test_dlck_exec_fini_success, setup_engine_exec_build, 2),
    TEST_TARGETS(117, test_dlck_exec_fini_skips_unjoined, setup_engine_exec_build, 2),
    TEST_TARGETS(118, test_dlck_engine_init_success, setup_reset, 2),
    TEST_TARGETS(119, test_dlck_engine_init_zero_targets, setup_reset, 0),
    TEST_TARGETS(120, test_dlck_engine_init_engine_alloc_failure, setup_reset, 2),
    TEST_TARGETS(121, test_dlck_engine_init_xss_alloc_failure, setup_reset, 2),
    TEST_TARGETS(122, test_dlck_engine_init_dbtree_classes_failure, setup_reset, 2),
    TEST_TARGETS(123, test_dlck_engine_init_bio_nvme_init_failure, setup_reset, 2),
    TEST_TARGETS(124, test_dlck_engine_init_sm_init_failure, setup_reset, 2),
    TEST_TARGETS(125, test_dlck_engine_init_tls_key_create_failure, setup_reset, 2),
    TEST_TARGETS(126, test_dlck_engine_init_vos_tls_init_failure, setup_reset, 2),
    TEST_TARGETS(127, test_dlck_engine_init_vos_sys_db_init_failure, setup_reset, 2),
    TEST_TARGETS(128, test_dlck_engine_start_xs_create_failure, setup_engine_build, 0),
    TEST_TARGETS(129, test_dlck_engine_start_xs_init_tls_failure, setup_engine_build, 0),
    TEST_TARGETS(130, test_dlck_engine_start_xs_init_xsctxt_failure, setup_engine_build, 1),
    TEST_TARGETS(131, test_dlck_engine_start_xs_init_eventual_failure, setup_engine_build, 0),
    TEST_TARGETS(132, test_dlck_engine_start_xs_init_polling_ult_failure, setup_engine_build, 0),
    TEST_TARGETS(133, test_dlck_engine_start_polling_eventual_test_success, setup_engine_build, 0),
    TEST_TARGETS(134, test_dlck_engine_stop_success, setup_engine_build_nvme, 1),
    TEST_TARGETS(135, test_dlck_engine_stop_join_fail, setup_engine_build, 0),
    TEST_TARGETS(136, test_dlck_engine_stop_fini_failure, setup_engine_build_nvme, 0),
    TEST_TARGETS(137, test_dlck_engine_stop_pool_not_empty, setup_engine_build, 0),
    TEST_TARGETS(138, test_dlck_engine_stop_xs_free_failure, setup_engine_build, 0),
    TEST_TARGETS(139, test_dlck_engine_fini_success, setup_engine_build, 0),
    TEST_TARGETS(140, test_dlck_engine_fini_sm_fini_failure, setup_engine_build, 0),
    TEST_TARGETS(141, test_dlck_engine_stop_fini_ult_create_failure, setup_engine_build, 0),
};

int
main(int argc, char **argv)
{
	if (argc > 1) {
		dlck_ut_filter(argc, argv);
	}
	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("dlck_engine_ut", dlck_engine_tests, NULL, NULL);
}
