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
#include <stdint.h>
#include <string.h>

#include <abt.h>

#include <daos/debug.h>
#include <daos/common.h>
#include <engine/srv_internal.h>

#include "../dlck_engine.h"
#include "../dlck_xstream.h"
#include "dlck_ut.h"
#include "ut_abt_mock.h"
#include "ut_dlck_abt_mock.h"
#include "ut_dlck_daos_mock.h"
#include "ut_dlck_xstream_mock.h"
#include "ut_mem_mock.h"

struct xstream_ut_ctx {
	struct dlck_engine  engine;
	struct dlck_xstream xss[TARGETS_MAX];
};

#define MOCK_XSTREAM_POOL    MOCK_POOL(0)
#define MOCK_XSTREAM_XSTREAM MOCK_XSTREAM(0)

#define EXPECT_ABT_XSTREAM_CREATE(RC, XSTREAM)                                                     \
	do {                                                                                       \
		expect_value(__wrap_ABT_xstream_create, sched, ABT_SCHED_NULL);                    \
		will_return(__wrap_ABT_xstream_create, RC);                                        \
		if ((RC) == ABT_SUCCESS) {                                                         \
			will_return(__wrap_ABT_xstream_create, XSTREAM);                           \
		}                                                                                  \
	} while (0)

#define EXPECT_ABT_XSTREAM_MAIN_POOLS(XSTREAM, RC, POOL)                                           \
	do {                                                                                       \
		expect_value(__wrap_ABT_xstream_get_main_pools, xstream, XSTREAM);                 \
		expect_value(__wrap_ABT_xstream_get_main_pools, max_pools, 1);                     \
		will_return(__wrap_ABT_xstream_get_main_pools, RC);                                \
		if ((RC) == ABT_SUCCESS) {                                                         \
			will_return(__wrap_ABT_xstream_get_main_pools, POOL);                      \
		}                                                                                  \
	} while (0)

#define EXPECT_ABT_XSTREAM_FREE(XSTREAM, RC)                                                       \
	do {                                                                                       \
		expect_value(__wrap_ABT_xstream_free, xstream, XSTREAM);                           \
		will_return(__wrap_ABT_xstream_free, RC);                                          \
	} while (0)

void
ctx_reset(void **state)
{
	static struct xstream_ut_ctx ctx;
	unsigned                     targets = (unsigned)(uintptr_t)*state;

	memset(&ctx, 0, sizeof(ctx));
	memset(&Mock_dmi, 0, sizeof(Mock_dmi));
	ctx.engine.targets = targets;
	ctx.engine.xss     = ctx.xss;

	for (unsigned i = 0; i <= targets; ++i) {
		ctx.xss[i].tgt_id         = (i == targets ? -1 : (int)i);
		ctx.xss[i].pool           = MOCK_POOL(i);
		ctx.xss[i].xstream        = ABT_XSTREAM_NULL;
		ctx.xss[i].nvme_poll      = ABT_THREAD_NULL;
		ctx.xss[i].nvme_poll_done = ABT_EVENTUAL_NULL;
	}

	*state = &ctx;
}

static int
setup_xstream(void **state)
{
	ctx_reset(state);
	return 0;
}

int
__wrap_ABT_xstream_create(ABT_sched sched, ABT_xstream *newxstream)
{
	int rc;

	check_expected(sched);
	assert_non_null(newxstream);
	rc = mock_type(int);
	if (rc == ABT_SUCCESS) {
		*newxstream = mock_ptr_type(ABT_xstream);
	}

	return rc;
}

int
__wrap_ABT_xstream_get_main_pools(ABT_xstream xstream, int max_pools, ABT_pool *pools)
{
	int rc;

	check_expected(xstream);
	check_expected(max_pools);
	assert_non_null(pools);
	rc = mock_type(int);
	if (rc == ABT_SUCCESS) {
		*pools = mock_ptr_type(ABT_pool);
	}

	return rc;
}

int
__wrap_ABT_xstream_free(ABT_xstream *xstream)
{
	int rc;

	assert_non_null(xstream);
	check_expected_ptr(xstream);
	rc = mock_type(int);
	if (rc == ABT_SUCCESS) {
		*xstream = ABT_XSTREAM_NULL;
	}

	return rc;
}

/** verify an ABT stream and its main pool are both created */
static void
test_dlck_xstream_create_success(void **state)
{
	struct xstream_ut_ctx *ctx = *state;
	int                    rc;

	EXPECT_ABT_XSTREAM_CREATE(ABT_SUCCESS, MOCK_XSTREAM_XSTREAM);
	EXPECT_ABT_XSTREAM_MAIN_POOLS(MOCK_XSTREAM_XSTREAM, ABT_SUCCESS, MOCK_XSTREAM_POOL);

	rc = dlck_xstream_create(&ctx->xss[0]);

	assert_int_equal(rc, DER_SUCCESS);
	assert_ptr_equal(ctx->xss[0].xstream, MOCK_XSTREAM_XSTREAM);
	assert_ptr_equal(ctx->xss[0].pool, MOCK_XSTREAM_POOL);
}

/** verify an ABT stream creation error is converted to a DAOS error */
static void
test_dlck_xstream_create_failure(void **state)
{
	struct xstream_ut_ctx *ctx = *state;
	int                    rc;

	ctx->xss[0].xstream = MOCK_XSTREAM_XSTREAM;
	EXPECT_ABT_XSTREAM_CREATE(ABT_ERR_MEM, ABT_XSTREAM_NULL);

	rc = dlck_xstream_create(&ctx->xss[0]);

	assert_int_equal(rc, -DER_NOMEM);
	assert_ptr_equal(ctx->xss[0].xstream, MOCK_XSTREAM_XSTREAM);
}

/** verify a main-pool lookup error releases the newly created ABT stream */
static void
test_dlck_xstream_create_pool_failure(void **state)
{
	struct xstream_ut_ctx *ctx = *state;
	int                    rc;

	EXPECT_ABT_XSTREAM_CREATE(ABT_SUCCESS, MOCK_XSTREAM_XSTREAM);
	EXPECT_ABT_XSTREAM_MAIN_POOLS(MOCK_XSTREAM_XSTREAM, ABT_ERR_MEM, ABT_POOL_NULL);
	EXPECT_ABT_XSTREAM_FREE(&ctx->xss[0].xstream, ABT_SUCCESS);

	rc = dlck_xstream_create(&ctx->xss[0]);

	assert_int_equal(rc, -DER_NOMEM);
	assert_ptr_equal(ctx->xss[0].xstream, ABT_XSTREAM_NULL);
}

/** verify freeing a null stream is a no-op */
static void
test_dlck_xstream_free_null(void **state)
{
	struct xstream_ut_ctx *ctx = *state;

	ctx->xss[0].pool = MOCK_XSTREAM_POOL;
	assert_int_equal(dlck_xstream_free(&ctx->xss[0]), DER_SUCCESS);
	assert_ptr_equal(ctx->xss[0].pool, MOCK_XSTREAM_POOL);
}

/** verify freeing a stream clears its handle and preserves its pool */
static void
test_dlck_xstream_free_keeps_pool(void **state)
{
	struct xstream_ut_ctx *ctx = *state;

	ctx->xss[0].xstream = MOCK_XSTREAM_XSTREAM;
	ctx->xss[0].pool    = MOCK_XSTREAM_POOL;
	EXPECT_ABT_XSTREAM_FREE(&ctx->xss[0].xstream, ABT_SUCCESS);

	assert_int_equal(dlck_xstream_free(&ctx->xss[0]), DER_SUCCESS);
	assert_ptr_equal(ctx->xss[0].xstream, ABT_XSTREAM_NULL);
	assert_ptr_equal(ctx->xss[0].pool, MOCK_XSTREAM_POOL);
}

/** verify successful initialization of a sys stream without NVMe */
static void
test_dlck_xstream_init_sys_without_nvme(void **state)
{
	struct xstream_ut_ctx *ctx = *state;
	int                    rc;

	EXPECT_DLCK_ULT_CREATE(MOCK_XSTREAM_POOL, DER_SUCCESS, Mock_thread);
	EXPECT_DSS_TLS_INIT(SYS_TAG, 0, -1, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(false);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);

	rc = dlck_xstream_init(&ctx->xss[0]);
	assert_int_equal(rc, DER_SUCCESS);
}

/** verify a sys TLS initialization failure is returned to the caller */
static void
test_dlck_xstream_init_tls_failure(void **state)
{
	struct xstream_ut_ctx *ctx = *state;
	int                    rc;

	EXPECT_DLCK_ULT_CREATE(MOCK_XSTREAM_POOL, DER_SUCCESS, Mock_thread);
	EXPECT_DSS_TLS_INIT(SYS_TAG, 0, -1, NULL);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);

	rc = dlck_xstream_init(&ctx->xss[0]);
	assert_int_equal(rc, -DER_NOMEM);
}

/** verify an NVMe context allocation failure finalizes the stream TLS */
static void
test_dlck_xstream_init_nvme_context_failure(void **state)
{
	struct xstream_ut_ctx *ctx = *state;
	int                    rc;

	EXPECT_DLCK_ULT_CREATE(MOCK_XSTREAM_POOL, DER_SUCCESS, Mock_thread);
	EXPECT_DSS_TLS_INIT(DAOS_SERVER_TAG,
			    DSS_MAIN_XS_ID_WITH_HELPER_POOL(0, DSS_SYS_XS_NR_DEFAULT), 0,
			    &Mock_dtls);
	EXPECT_DLCK_ENGINE_GET_MODULE_INFO();
	EXPECT_BIO_XSCTXT_ALLOC(0, -DER_NOMEM, NULL);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);

	rc = dlck_xstream_init(&ctx->xss[0]);
	assert_int_equal(rc, -DER_NOMEM);
}

/** verify eventual creation failure releases the context and stream TLS */
static void
test_dlck_xstream_init_eventual_failure(void **state)
{
	struct xstream_ut_ctx *ctx = *state;
	int                    rc;

	EXPECT_DLCK_ULT_CREATE(MOCK_XSTREAM_POOL, DER_SUCCESS, Mock_thread);
	EXPECT_DSS_TLS_INIT(SYS_TAG, 0, -1, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(true);
	EXPECT_DLCK_ENGINE_GET_MODULE_INFO();
	EXPECT_BIO_XSCTXT_ALLOC(-1, DER_SUCCESS, Mock_xs_ctxt);
	EXPECT_ABT_EVENTUAL_CREATE(ABT_ERR_MEM, NULL);
	expect_value(__wrap_bio_xsctxt_free, ctxt, Mock_xs_ctxt);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);

	rc = dlck_xstream_init(&ctx->xss[0]);
	assert_int_equal(rc, -DER_NOMEM);
}

/** verify polling ULT creation failure releases the eventual, context, and stream TLS */
static void
test_dlck_xstream_init_poll_ult_failure(void **state)
{
	struct xstream_ut_ctx *ctx = *state;
	int                    rc;

	EXPECT_DLCK_ULT_CREATE(MOCK_XSTREAM_POOL, DER_SUCCESS, Mock_thread);
	EXPECT_DSS_TLS_INIT(SYS_TAG, 0, -1, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(true);
	EXPECT_DLCK_ENGINE_GET_MODULE_INFO();
	EXPECT_BIO_XSCTXT_ALLOC(-1, DER_SUCCESS, Mock_xs_ctxt);
	EXPECT_ABT_EVENTUAL_CREATE(ABT_SUCCESS, MOCK_EVENTUAL(0));
	EXPECT_DLCK_ULT_CREATE(MOCK_XSTREAM_POOL, -DER_NOMEM, NULL);
	EXPECT_ABT_EVENTUAL_FREE(MOCK_EVENTUAL(0), ABT_SUCCESS);
	expect_value(__wrap_bio_xsctxt_free, ctxt, Mock_xs_ctxt);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);

	rc = dlck_xstream_init(&ctx->xss[0]);
	assert_int_equal(rc, -DER_NOMEM);
}

/** verify a polling ULT exits when its eventual reports an error */
static void
test_dlck_xstream_init_poll_eventual_error(void **state)
{
	struct xstream_ut_ctx *ctx = *state;
	int                    rc;

	EXPECT_DLCK_ULT_CREATE(MOCK_XSTREAM_POOL, DER_SUCCESS, Mock_thread);
	EXPECT_DSS_TLS_INIT(SYS_TAG, 0, -1, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(true);
	EXPECT_DLCK_ENGINE_GET_MODULE_INFO();
	EXPECT_BIO_XSCTXT_ALLOC(-1, DER_SUCCESS, Mock_xs_ctxt);
	EXPECT_ABT_EVENTUAL_CREATE(ABT_SUCCESS, MOCK_EVENTUAL(0));
	EXPECT_DLCK_ULT_CREATE(MOCK_XSTREAM_POOL, DER_SUCCESS, MOCK_THREAD(0));
	EXPECT_DLCK_ENGINE_GET_MODULE_INFO();
	EXPECT_BIO_NVME_POLL(Mock_xs_ctxt, DER_SUCCESS);
	will_return(__wrap_ABT_thread_yield, ABT_SUCCESS);
	EXPECT_ABT_EVENTUAL_TEST(MOCK_EVENTUAL(0), ABT_ERR_INV_EVENTUAL, ABT_FALSE);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);

	rc = dlck_xstream_init(&ctx->xss[0]);
	assert_int_equal(rc, DER_SUCCESS);
}

/** verify a sys stream without NVMe is finalized and released */
static void
test_dlck_xstream_stop_sys_success(void **state)
{
	struct xstream_ut_ctx *ctx = *state;
	int                    rc;

	ctx->xss[0].xstream = MOCK_XSTREAM_XSTREAM;
	EXPECT_DLCK_ULT_CREATE(MOCK_XSTREAM_POOL, DER_SUCCESS, Mock_thread);
	will_return(__wrap_dss_tls_get, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(false);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_ABT_POOL_IS_EMPTY(MOCK_XSTREAM_POOL, ABT_SUCCESS, ABT_TRUE);
	EXPECT_ABT_XSTREAM_FREE(&ctx->xss[0].xstream, ABT_SUCCESS);

	rc = dlck_xstream_stop(&ctx->xss[0]);

	assert_int_equal(rc, DER_SUCCESS);
	assert_ptr_equal(ctx->xss[0].xstream, ABT_XSTREAM_NULL);
}

/** verify NVMe polling resources are released when stopping an IO stream */
static void
test_dlck_xstream_stop_io_success(void **state)
{
	struct xstream_ut_ctx *ctx = *state;
	int                    rc;

	ctx->xss[0].tgt_id         = 0;
	ctx->xss[0].xstream        = MOCK_XSTREAM_XSTREAM;
	ctx->xss[0].nvme_poll      = MOCK_THREAD(0);
	ctx->xss[0].nvme_poll_done = MOCK_EVENTUAL(0);
	Mock_dmi.dmi_nvme_ctxt     = Mock_xs_ctxt;
	EXPECT_DLCK_ULT_CREATE(MOCK_XSTREAM_POOL, DER_SUCCESS, Mock_thread);
	will_return(__wrap_dss_tls_get, &Mock_dtls);
	EXPECT_ABT_EVENTUAL_SET(MOCK_EVENTUAL(0), ABT_SUCCESS);
	EXPECT_ABT_THREAD_JOIN(MOCK_THREAD(0), ABT_SUCCESS);
	EXPECT_ABT_THREAD_FREE(MOCK_THREAD(0), ABT_SUCCESS);
	EXPECT_DLCK_ENGINE_GET_MODULE_INFO();
	expect_value(__wrap_bio_xsctxt_free, ctxt, Mock_xs_ctxt);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_ABT_POOL_IS_EMPTY(MOCK_XSTREAM_POOL, ABT_SUCCESS, ABT_TRUE);
	EXPECT_ABT_XSTREAM_FREE(&ctx->xss[0].xstream, ABT_SUCCESS);

	rc = dlck_xstream_stop(&ctx->xss[0]);

	assert_int_equal(rc, DER_SUCCESS);
}

/** verify a fini ULT error is returned without freeing a busy stream */
static void
test_dlck_xstream_stop_fini_failure(void **state)
{
	struct xstream_ut_ctx *ctx = *state;
	int                    rc;

	ctx->xss[0].tgt_id         = 0;
	ctx->xss[0].nvme_poll_done = MOCK_EVENTUAL(0);
	EXPECT_DLCK_ULT_CREATE(MOCK_XSTREAM_POOL, DER_SUCCESS, Mock_thread);
	will_return(__wrap_dss_tls_get, &Mock_dtls);
	EXPECT_ABT_EVENTUAL_SET(MOCK_EVENTUAL(0), ABT_ERR_INV_EVENTUAL);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);

	rc = dlck_xstream_stop(&ctx->xss[0]);

	assert_int_equal(rc, -DER_INVAL);
}

/** verify a nonempty pool prevents the stream from being freed */
static void
test_dlck_xstream_stop_pool_not_empty(void **state)
{
	struct xstream_ut_ctx *ctx = *state;
	int                    rc;

	EXPECT_DLCK_ULT_CREATE(MOCK_XSTREAM_POOL, DER_SUCCESS, Mock_thread);
	will_return(__wrap_dss_tls_get, &Mock_dtls);
	EXPECT_BIO_NVME_CONFIGURED(false);
	expect_value(__wrap_dss_tls_fini, dtls, &Mock_dtls);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_ABT_POOL_IS_EMPTY(MOCK_XSTREAM_POOL, ABT_SUCCESS, ABT_FALSE);

	rc = dlck_xstream_stop(&ctx->xss[0]);

	assert_int_equal(rc, -DER_BUSY);
}

/** verify an ABT stream free failure is returned after finalization */
static void
test_dlck_xstream_stop_free_failure(void **state)
{
	struct xstream_ut_ctx *ctx = *state;
	int                    rc;

	ctx->xss[0].xstream = MOCK_XSTREAM_XSTREAM;
	EXPECT_DLCK_ULT_CREATE(MOCK_XSTREAM_POOL, DER_SUCCESS, Mock_thread);
	will_return(__wrap_dss_tls_get, NULL);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_ABT_POOL_IS_EMPTY(MOCK_XSTREAM_POOL, ABT_SUCCESS, ABT_TRUE);
	EXPECT_ABT_XSTREAM_FREE(&ctx->xss[0].xstream, ABT_ERR_INV_ARG);

	rc = dlck_xstream_stop(&ctx->xss[0]);

	assert_int_equal(rc, -DER_INVAL);
}

/** verify best-effort cleanup finalizes and releases a stream without TLS */
static void
test_dlck_xstream_stop_all_no_error(void **state)
{
	struct xstream_ut_ctx *ctx = *state;

	ctx->xss[0].xstream = MOCK_XSTREAM_XSTREAM;
	EXPECT_DLCK_ULT_CREATE(MOCK_XSTREAM_POOL, DER_SUCCESS, Mock_thread);
	will_return(__wrap_dss_tls_get, NULL);
	EXPECT_ABT_THREAD_FREE(Mock_thread, ABT_SUCCESS);
	EXPECT_ABT_XSTREAM_FREE(&ctx->xss[0].xstream, ABT_SUCCESS);

	dlck_xstream_stop_all_no_error(ctx->xss, ctx->engine.targets);

	assert_ptr_equal(ctx->xss[0].xstream, ABT_XSTREAM_NULL);
}

/** verify allocation returns a per-target XStream argument */
static void
test_dlck_xstream_arg_alloc_success(void **state)
{
	struct xstream_ut_ctx *ctx     = *state;
	struct xstream_arg     arg_buf = {0};
	void                  *arg     = NULL;
	int                    rc;

	EXPECT_D_CALLOC(1, sizeof(struct xstream_arg), &arg_buf);

	rc = dlck_xstream_arg_alloc(&ctx->engine, 0, &ctx, &arg);

	assert_int_equal(rc, DER_SUCCESS);
	assert_ptr_equal(arg, &arg_buf);
	assert_ptr_equal(arg_buf.engine, &ctx->engine);
	assert_ptr_equal(arg_buf.xs, &ctx->xss[0]);
	assert_ptr_equal(arg_buf.ctrl, &ctx);
}

/** verify an argument allocation failure is returned */
static void
test_dlck_xstream_arg_alloc_failure(void **state)
{
	struct xstream_ut_ctx *ctx = *state;
	void                  *arg = NULL;
	int                    rc;

	EXPECT_D_CALLOC(1, sizeof(struct xstream_arg), NULL);

	rc = dlck_xstream_arg_alloc(&ctx->engine, 0, &ctx, &arg);

	assert_int_equal(rc, -DER_NOMEM);
	assert_null(arg);
}

/** verify freeing null XStream arguments is a no-op */
static void
test_dlck_xstream_arg_free_null(void **state)
{
	void *arg = NULL;

	(void)state;
	dlck_xstream_arg_free(NULL, &arg);
	assert_null(arg);
}

/** verify argument status is copied to the target slot before freeing */
static void
test_dlck_xstream_arg_free_collects_status(void **state)
{
	struct xstream_ut_ctx *ctx              = *state;
	struct checker_status  sts[TARGETS_MAX] = {{0}};
	struct xstream_arg     arg              = {.xs = &ctx->xss[0]};
	void                  *arg_ptr          = &arg;

	arg.status.rc           = -DER_IO;
	arg.status.warnings_num = 3;
	arg.status.errors_num   = 2;
	expect_value(__wrap_d_free, ptr, &arg);

	dlck_xstream_arg_free(sts, &arg_ptr);

	assert_int_equal(sts[0].rc, -DER_IO);
	assert_int_equal(sts[0].warnings_num, 3);
	assert_int_equal(sts[0].errors_num, 2);
	assert_null(arg_ptr);
}

#define TEST_TARGETS(ID, FUNC, TARGETS)                                                            \
	{"DLCK_XSTREAM_" #ID ": " #FUNC, FUNC, setup_xstream, NULL, (void *)(uintptr_t)(TARGETS)}

static const struct CMUnitTest dlck_xstream_tests[] = {
    TEST_TARGETS(101, test_dlck_xstream_create_success, 0),
    TEST_TARGETS(102, test_dlck_xstream_create_failure, 0),
    TEST_TARGETS(103, test_dlck_xstream_create_pool_failure, 0),
    TEST_TARGETS(104, test_dlck_xstream_free_null, 0),
    TEST_TARGETS(105, test_dlck_xstream_free_keeps_pool, 0),
    TEST_TARGETS(106, test_dlck_xstream_init_sys_without_nvme, 0),
    TEST_TARGETS(107, test_dlck_xstream_init_tls_failure, 0),
    TEST_TARGETS(108, test_dlck_xstream_init_nvme_context_failure, 1),
    TEST_TARGETS(109, test_dlck_xstream_init_eventual_failure, 0),
    TEST_TARGETS(110, test_dlck_xstream_init_poll_ult_failure, 0),
    TEST_TARGETS(111, test_dlck_xstream_init_poll_eventual_error, 0),
    TEST_TARGETS(112, test_dlck_xstream_stop_sys_success, 0),
    TEST_TARGETS(113, test_dlck_xstream_stop_io_success, 1),
    TEST_TARGETS(114, test_dlck_xstream_stop_fini_failure, 1),
    TEST_TARGETS(115, test_dlck_xstream_stop_pool_not_empty, 0),
    TEST_TARGETS(116, test_dlck_xstream_stop_free_failure, 0),
    TEST_TARGETS(117, test_dlck_xstream_stop_all_no_error, 0),
    TEST_TARGETS(118, test_dlck_xstream_arg_alloc_success, 1),
    TEST_TARGETS(119, test_dlck_xstream_arg_alloc_failure, 1),
    TEST_TARGETS(120, test_dlck_xstream_arg_free_null, 0),
    TEST_TARGETS(121, test_dlck_xstream_arg_free_collects_status, 1),
};

int
main(int argc, char **argv)
{
	if (argc > 1) {
		dlck_ut_filter(argc, argv);
	}
	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("dlck_xstream_ut", dlck_xstream_tests, NULL, NULL);
}
