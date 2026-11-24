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

#include <string.h>

#include <daos/debug.h>
#include <daos/common.h>

#include "../dlck_engine_internal.h"
#include "dlck_ut.h"
#include "ut_dlck_cmd_mock.h"

#define TARGET_COUNT 3

/** per-test context: the engine and the custom arguments passed to the exec callbacks */
struct exec_all_ut_ctx {
	struct dlck_engine engine;
	int                custom_alloc;
	int                custom_free;
};

static struct exec_all_ut_ctx Ctx;

#define EXPECT_EXEC_STATE(CTX, MOCK, TARGETS)                                                      \
	do {                                                                                       \
		expect_value(MOCK, de->targets, TARGETS);                                          \
		expect_value(MOCK, de->arg_alloc_fn_arg, &(CTX)->custom_alloc);                    \
		expect_value(MOCK, de->arg_free_fn_arg, &(CTX)->custom_free);                      \
		expect_value(MOCK, de->ck, NULL);                                                  \
	} while (0)

#define EXPECT_EXEC_INIT(CTX, ENGINE, RC)                                                          \
	do {                                                                                       \
		expect_value(__wrap_dlck_exec_init, engine, ENGINE);                               \
		EXPECT_EXEC_STATE(CTX, __wrap_dlck_exec_init, TARGET_COUNT);                       \
		will_return(__wrap_dlck_exec_init, RC);                                            \
	} while (0)

#define EXPECT_ENGINE_EXEC(CTX, ENGINE, RC, JOIN_FAIL)                                             \
	do {                                                                                       \
		expect_value(__wrap_dlck_engine_exec, engine, ENGINE);                             \
		EXPECT_EXEC_STATE(CTX, __wrap_dlck_engine_exec, TARGET_COUNT);                     \
		will_return(__wrap_dlck_engine_exec, JOIN_FAIL);                                   \
		will_return(__wrap_dlck_engine_exec, RC);                                          \
	} while (0)

#define EXPECT_EXEC_JOIN(CTX, RC, JOIN_FAIL)                                                       \
	do {                                                                                       \
		EXPECT_EXEC_STATE(CTX, __wrap_dlck_exec_join, TARGET_COUNT);                       \
		will_return(__wrap_dlck_exec_join, JOIN_FAIL);                                     \
		will_return(__wrap_dlck_exec_join, RC);                                            \
	} while (0)

#define EXPECT_EXEC_FINI(CTX) EXPECT_EXEC_STATE(CTX, __wrap_dlck_exec_fini, TARGET_COUNT)

int
__wrap_dlck_exec_init(struct dlck_engine *engine, struct dlck_exec *de)
{
	check_expected_ptr(engine);
	assert_non_null(de);
	check_expected(de->targets);
	check_expected_ptr(de->arg_alloc_fn_arg);
	check_expected_ptr(de->arg_free_fn_arg);
	check_expected_ptr(de->ck);
	assert_ptr_equal(de->arg_alloc_fn, mock_arg_alloc_fn);
	assert_ptr_equal(de->arg_free_fn, mock_arg_free_fn);
	assert_ptr_equal(de->exec_one, mock_exec_one);

	return mock_type(int);
}

int
__wrap_dlck_engine_exec(struct dlck_engine *engine, struct dlck_exec *de)
{
	check_expected_ptr(engine);
	assert_non_null(de);
	check_expected(de->targets);
	check_expected_ptr(de->arg_alloc_fn_arg);
	check_expected_ptr(de->arg_free_fn_arg);
	check_expected_ptr(de->ck);
	assert_ptr_equal(de->arg_alloc_fn, mock_arg_alloc_fn);
	assert_ptr_equal(de->arg_free_fn, mock_arg_free_fn);
	assert_ptr_equal(de->exec_one, mock_exec_one);
	de->join_fail = mock_type(bool);

	return mock_type(int);
}

int
__wrap_dlck_exec_join(struct dlck_exec *de)
{
	assert_non_null(de);
	check_expected(de->targets);
	check_expected_ptr(de->arg_alloc_fn_arg);
	check_expected_ptr(de->arg_free_fn_arg);
	check_expected_ptr(de->ck);
	assert_ptr_equal(de->arg_alloc_fn, mock_arg_alloc_fn);
	assert_ptr_equal(de->arg_free_fn, mock_arg_free_fn);
	assert_ptr_equal(de->exec_one, mock_exec_one);
	de->join_fail = mock_type(bool);

	return mock_type(int);
}

void
__wrap_dlck_exec_fini(struct dlck_exec *de)
{
	assert_non_null(de);
	check_expected(de->targets);
	check_expected_ptr(de->arg_alloc_fn_arg);
	check_expected_ptr(de->arg_free_fn_arg);
	check_expected_ptr(de->ck);
	assert_ptr_equal(de->arg_alloc_fn, mock_arg_alloc_fn);
	assert_ptr_equal(de->arg_free_fn, mock_arg_free_fn);
	assert_ptr_equal(de->exec_one, mock_exec_one);
}

static int
setup(void **state)
{
	memset(&Ctx, 0, sizeof(Ctx));
	Ctx.engine.targets = TARGET_COUNT;
	*state             = &Ctx;

	return 0;
}

/** verify initialization failure returns immediately without running later phases */
static void
test_exec_all_init_failure(void **state)
{
	struct exec_all_ut_ctx *ctx = *state;
	int                     rc;

	EXPECT_EXEC_INIT(ctx, &ctx->engine, -DER_NOMEM);

	rc = dlck_engine_exec_all(&ctx->engine, mock_arg_alloc_fn, &ctx->custom_alloc,
				  mock_arg_free_fn, &ctx->custom_free, mock_exec_one, NULL);

	assert_int_equal(rc, -DER_NOMEM);
	assert_false(ctx->engine.join_fail);
}

/** verify execution failure skips joining but finalizes the initialized batch */
static void
test_exec_all_exec_failure(void **state)
{
	struct exec_all_ut_ctx *ctx = *state;
	int                     rc;

	EXPECT_EXEC_INIT(ctx, &ctx->engine, DER_SUCCESS);
	EXPECT_ENGINE_EXEC(ctx, &ctx->engine, -DER_IO, false);
	EXPECT_EXEC_FINI(ctx);

	rc = dlck_engine_exec_all(&ctx->engine, mock_arg_alloc_fn, &ctx->custom_alloc,
				  mock_arg_free_fn, &ctx->custom_free, mock_exec_one, NULL);

	assert_int_equal(rc, -DER_IO);
	assert_false(ctx->engine.join_fail);
}

/** verify partial execution join failure is propagated before finalization */
static void
test_exec_all_exec_join_failure(void **state)
{
	struct exec_all_ut_ctx *ctx = *state;
	int                     rc;

	EXPECT_EXEC_INIT(ctx, &ctx->engine, DER_SUCCESS);
	EXPECT_ENGINE_EXEC(ctx, &ctx->engine, -DER_NOMEM, true);
	EXPECT_EXEC_FINI(ctx);

	rc = dlck_engine_exec_all(&ctx->engine, mock_arg_alloc_fn, &ctx->custom_alloc,
				  mock_arg_free_fn, &ctx->custom_free, mock_exec_one, NULL);

	assert_int_equal(rc, -DER_NOMEM);
	assert_true(ctx->engine.join_fail);
}

/** verify successful execution joins and finalizes the batch */
static void
test_exec_all_success(void **state)
{
	struct exec_all_ut_ctx *ctx = *state;
	int                     rc;

	EXPECT_EXEC_INIT(ctx, &ctx->engine, DER_SUCCESS);
	EXPECT_ENGINE_EXEC(ctx, &ctx->engine, DER_SUCCESS, false);
	EXPECT_EXEC_JOIN(ctx, DER_SUCCESS, false);
	EXPECT_EXEC_FINI(ctx);

	rc = dlck_engine_exec_all(&ctx->engine, mock_arg_alloc_fn, &ctx->custom_alloc,
				  mock_arg_free_fn, &ctx->custom_free, mock_exec_one, NULL);

	assert_int_equal(rc, DER_SUCCESS);
	assert_false(ctx->engine.join_fail);
}

/** verify join failure is returned and marks the engine as un-stoppable */
static void
test_exec_all_join_failure(void **state)
{
	struct exec_all_ut_ctx *ctx = *state;
	int                     rc;

	EXPECT_EXEC_INIT(ctx, &ctx->engine, DER_SUCCESS);
	EXPECT_ENGINE_EXEC(ctx, &ctx->engine, DER_SUCCESS, false);
	EXPECT_EXEC_JOIN(ctx, -DER_INVAL, true);
	EXPECT_EXEC_FINI(ctx);

	rc = dlck_engine_exec_all(&ctx->engine, mock_arg_alloc_fn, &ctx->custom_alloc,
				  mock_arg_free_fn, &ctx->custom_free, mock_exec_one, NULL);

	assert_int_equal(rc, -DER_INVAL);
	assert_true(ctx->engine.join_fail);
}

/** verify an earlier engine join failure remains set after a successful batch */
static void
test_exec_all_preserves_engine_join_failure(void **state)
{
	struct exec_all_ut_ctx *ctx = *state;
	int                     rc;

	ctx->engine.join_fail = true;
	EXPECT_EXEC_INIT(ctx, &ctx->engine, DER_SUCCESS);
	EXPECT_ENGINE_EXEC(ctx, &ctx->engine, DER_SUCCESS, false);
	EXPECT_EXEC_JOIN(ctx, DER_SUCCESS, false);
	EXPECT_EXEC_FINI(ctx);

	rc = dlck_engine_exec_all(&ctx->engine, mock_arg_alloc_fn, &ctx->custom_alloc,
				  mock_arg_free_fn, &ctx->custom_free, mock_exec_one, NULL);

	assert_int_equal(rc, DER_SUCCESS);
	assert_true(ctx->engine.join_fail);
}

#define TEST(ID, FUNC) {"DLCK_ENGINE_EXEC_ALL_" #ID ": " #FUNC, FUNC, setup, NULL, NULL}

static const struct CMUnitTest dlck_engine_exec_all_tests[] = {
    TEST(101, test_exec_all_init_failure),
    TEST(102, test_exec_all_exec_failure),
    TEST(103, test_exec_all_exec_join_failure),
    TEST(104, test_exec_all_success),
    TEST(105, test_exec_all_join_failure),
    TEST(106, test_exec_all_preserves_engine_join_failure),
};

int
main(int argc, char **argv)
{
	if (argc > 1) {
		dlck_ut_filter(argc, argv);
	}
	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("dlck_engine_exec_all_ut", dlck_engine_exec_all_tests,
					   NULL, NULL);
}
