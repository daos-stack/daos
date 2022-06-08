/**
 * (C) Copyright 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <daos/test_utils.h>
#include <daos/checksum.h>
#include <daos_srv/evtree.h>
#include <daos_srv/srv_csum.h>
#include <daos_srv/vos_types.h>
#include <daos_srv/vos.h>
#include <fcntl.h>
#include <daos/tests_lib.h>

#define assert_ms_eq(exp, duration, periods, curr, elapsed_ns) do {  \
	struct timespec start;                                       \
	struct timespec elapsed;                                     \
	d_gettime(&start); elapsed = start;                          \
	d_timeinc(&elapsed, elapsed_ns);                             \
	assert_int_equal(exp,                                        \
		get_ms_between_periods(start, elapsed, duration,     \
		periods, curr));                                     \
	} while (false)

static void
ms_between_periods_tests(void **state)
{
#define ONE_SECOND_NS (1e+9)
#define HALF_SECOND_NS (5e+8)

	/*
	 * ---------------------------------------------------------
	 * assert_ms_eq takes the following values in this order:
	 * Expected, duration, periods, current period, elapsed ns
	 * ---------------------------------------------------------
	 */

	/*
	 * First period, no time has elapsed, total of 10 periods in 10 seconds.
	 * Should be 1 second.
	 */
	assert_ms_eq(1000, 10, 10, 0, 0);

	/*
	 * With 10 periods and 10 second duration, then each period should
	 * take 1 second.
	 * if half a second has elapsed already for the first period, then only
	 * need to wait another half second
	 */
	assert_ms_eq(500, 10, 10, 0, HALF_SECOND_NS);


	/*
	 * With 10 periods and 10 second duration, then each period should
	 * take 1 second.
	 * if one second (or more) has elapsed already for the first period,
	 * then shouldn't wait at all
	 */
	assert_ms_eq(0, 10, 10, 0, ONE_SECOND_NS);
	assert_ms_eq(0, 10, 10, 0, ONE_SECOND_NS + HALF_SECOND_NS);

	/*
	 * With 10 periods and 10 second duration, then each period should
	 * take 1 second.
	 * if one and a half second has elapsed and in the second period,
	 * then should wait half a second
	 */
	assert_ms_eq(500, 10, 10, 1, ONE_SECOND_NS + HALF_SECOND_NS);

	/*
	 * Multiple tests with 5 periods into a 10 second duration
	 */
	assert_ms_eq(2000, 10, 5, 0, 0);
	assert_ms_eq(1750, 10, 5, 0, HALF_SECOND_NS / 2);
	assert_ms_eq(3750, 10, 5, 1, HALF_SECOND_NS / 2);

	/* No time has elapsed, but already done with all periods, plus
	 * some. Should wait full 10 seconds now, but not more
	 */
	assert_ms_eq(10000, 10, 5, 6, 0);
	assert_ms_eq(10000, 10, 5, 100, 0);

	/* What should wait be if duration isn't set and periods are not set */
	assert_ms_eq(0, 0, 0, 0, 0);

	/* periods is larger than duration in seconds */
	assert_ms_eq(908, 10, 11, 0, 1);
}

static uint32_t test_sleep_fn_call_count;
static uint32_t test_sleep_fn_msec;
static int
test_sleep_fn(void *arg, uint32_t msec)
{
	test_sleep_fn_call_count++;
	test_sleep_fn_msec = msec;
	return 0;
}

static uint32_t test_yield_fn_call_count;
static int
test_yield_fn_t(void *arg)
{
	test_yield_fn_call_count++;
	return 0;
}

/*
 * ---------------------------------------------------------------------------
 * Test how the schedule is controlled with credits, frequency, and schedule
 * ---------------------------------------------------------------------------
 */

struct test_ctx_args {
	/*
	 * These fields help set where the scrubber is in the scrubbing process
	 */
	uint32_t		tst_already_run_sec;
	uint32_t		tst_pool_last_csum_calcs;
	uint32_t		tst_pool_csum_calcs;
	/* Scrubbing properties of the pool for the test */
	uint32_t		tst_scrub_sched;
	uint32_t		tst_scrub_freq_sec;
	uint32_t		tst_scrub_cred;
	enum scrub_status	tst_scrub_status;
};

#define INIT_CTX_FOR_TESTS(ctx, ...) \
	init_ctx_for_tests(ctx, &(struct test_ctx_args)__VA_ARGS__)

#define DEFAULT_SET(var, def) if ((var) == 0) (var) = def

/*
 * setup the minimum of the context needed for
 * controlling the schedule
 */
static void
init_ctx_for_tests(struct scrub_ctx *ctx, struct test_ctx_args *args)
{
	struct ds_pool *pool;

	/* set some defaults if not set */
	DEFAULT_SET(args->tst_scrub_cred, 1);
	DEFAULT_SET(args->tst_scrub_freq_sec, 10); /* 10 seconds */
	DEFAULT_SET(args->tst_scrub_status, SCRUB_STATUS_RUNNING);

	D_ALLOC_PTR(pool);
	assert_non_null(pool);
	ctx->sc_pool = pool;
	ctx->sc_yield_fn = test_yield_fn_t;
	ctx->sc_sleep_fn = test_sleep_fn;
	d_gettime(&ctx->sc_pool_start_scrub);

	ctx->sc_pool_last_csum_calcs = args->tst_pool_last_csum_calcs;
	ctx->sc_pool_csum_calcs = args->tst_pool_csum_calcs;
	ctx->sc_pool_start_scrub.tv_sec -= args->tst_already_run_sec;
	ctx->sc_status = args->tst_scrub_status;
	pool->sp_scrub_sched = args->tst_scrub_sched;
	pool->sp_scrub_cred = args->tst_scrub_cred;
	pool->sp_scrub_freq_sec = args->tst_scrub_freq_sec;

	ctx->sc_credits_left = ctx->sc_pool->sp_scrub_cred;
}

static void
free_ctx(struct scrub_ctx *ctx)
{
	D_FREE(ctx->sc_pool);
}

static void
run_yield_or_sleep_while_running(struct scrub_ctx *ctx)
{
	sc_yield_sleep_while_running(ctx);
}

static void
run_yield_or_sleep(struct scrub_ctx *ctx)
{
	sc_yield_or_sleep(ctx);
}

static void
when_sched_run_wait_credits_are_consumed__should_yield(void **state)
{
	struct scrub_ctx	ctx = {0};
	const uint32_t		orig_credits = 2;

	INIT_CTX_FOR_TESTS(&ctx, {
		.tst_scrub_sched = DAOS_SCRUB_SCHED_RUN_WAIT,
		.tst_pool_last_csum_calcs = 10,
		.tst_pool_csum_calcs = 0,
		.tst_already_run_sec = 0,
		.tst_scrub_cred = orig_credits
	});

	run_yield_or_sleep_while_running(&ctx);
	/* don't yield until all credits are consumed */
	assert_int_equal(1, ctx.sc_credits_left);
	assert_int_equal(0, test_yield_fn_call_count);

	/* credits are consumed */
	run_yield_or_sleep_while_running(&ctx);
	/* yielded and reset credits */
	assert_int_equal(1, test_yield_fn_call_count);
	assert_int_equal(orig_credits, ctx.sc_credits_left);

	free_ctx(&ctx);
}

static void
each_schedule__credits_are_consumed_and_wrap(void **state)
{
	struct scrub_ctx	ctx = {0};
	uint32_t		i;
	uint32_t		scheds[] = {
		DAOS_SCRUB_SCHED_RUN_WAIT,
		DAOS_SCRUB_SCHED_CONTINUOUS
	};

	for (i = 0; i < ARRAY_SIZE(scheds); i++) {
		test_yield_fn_call_count = 0;
		INIT_CTX_FOR_TESTS(&ctx, {
			.tst_scrub_sched = scheds[i],
			.tst_scrub_cred = 3,
			.tst_pool_last_csum_calcs = 10,
			.tst_pool_csum_calcs = 0,
			.tst_already_run_sec = 0,
		});

		run_yield_or_sleep_while_running(&ctx);
		assert_int_equal(2, ctx.sc_credits_left);

		run_yield_or_sleep_while_running(&ctx);
		assert_int_equal(1, ctx.sc_credits_left);

		run_yield_or_sleep_while_running(&ctx);
		assert_int_equal(3, ctx.sc_credits_left);

		free_ctx(&ctx);
	}
}

static void
when_sched_continuous_credits_1__sleeps_and_yield_appropriately(void **state)
{
	struct scrub_ctx	ctx = {0};

	INIT_CTX_FOR_TESTS(&ctx, {
		.tst_scrub_sched = DAOS_SCRUB_SCHED_CONTINUOUS,
		.tst_scrub_cred = 1,
		.tst_pool_last_csum_calcs = 10,
		.tst_pool_csum_calcs = 1,
		.tst_already_run_sec = 0,
		.tst_scrub_freq_sec = 10,
		});

	run_yield_or_sleep_while_running(&ctx);
	assert_int_equal(1, test_sleep_fn_call_count);

	/* simulate 1 second passing and 1 csum calculated */
	ctx.sc_pool_csum_calcs++;
	run_yield_or_sleep_while_running(&ctx);
	assert_int_equal(2, test_sleep_fn_call_count);

	/* simulate 1 second passing and 1 csum calculated */
	ctx.sc_pool_csum_calcs++;
	run_yield_or_sleep_while_running(&ctx);
	assert_int_equal(3, test_sleep_fn_call_count);

	/*
	 * simulate 1 minute passing and still going (even though have
	 * calculated a lot)
	 */
	ctx.sc_pool_start_scrub.tv_sec -= 60;
	ctx.sc_pool_csum_calcs += 100;
	run_yield_or_sleep_while_running(&ctx);
	assert_int_equal(3, test_sleep_fn_call_count);
	assert_int_equal(1, test_yield_fn_call_count);

	free_ctx(&ctx);
}

static void
when_sched_continuous_have_run_half_freq__should_sleep(void **state)
{
	struct scrub_ctx	ctx = {0};

	INIT_CTX_FOR_TESTS(&ctx, {
		.tst_scrub_sched = DAOS_SCRUB_SCHED_CONTINUOUS,
		.tst_pool_last_csum_calcs = 10,
		.tst_pool_csum_calcs = 10,
		.tst_already_run_sec = 5,
		.tst_scrub_freq_sec = 10,
		.tst_scrub_status = SCRUB_STATUS_NOT_RUNNING
		});

	run_yield_or_sleep(&ctx);
	/*
	 * Should sleep 5 seconds because half way through the 10
	 * second frequency
	 */
	assert_int_equal(1000 * 5, test_sleep_fn_msec);
	free_ctx(&ctx);
}

static void
when_sched_continuous_past_freq__should_yield(void **state)
{
	struct scrub_ctx	ctx = {0};

	INIT_CTX_FOR_TESTS(&ctx, {
		.tst_scrub_sched = DAOS_SCRUB_SCHED_CONTINUOUS,
		.tst_pool_last_csum_calcs = 10,
		.tst_pool_csum_calcs = 10,
		.tst_already_run_sec = 15,
		.tst_scrub_freq_sec = 10,
		.tst_scrub_status = SCRUB_STATUS_NOT_RUNNING
		});

	run_yield_or_sleep(&ctx);

	assert_int_equal(0, test_sleep_fn_msec);
	assert_int_equal(0, test_sleep_fn_call_count);
	assert_int_equal(1, test_yield_fn_call_count);

	free_ctx(&ctx);
}

static int scrub_test_setup(void **state)
{
	test_yield_fn_call_count = 0;
	test_sleep_fn_call_count = 0;
	test_sleep_fn_msec = 0;

	return 0;
}

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define TS(x) { "SCRUB_SCHED_" STR(__COUNTER__) ": " #x, x, scrub_test_setup, \
		NULL, NULL }

static const struct CMUnitTest scrubbing_sched_tests[] = {
	TS(ms_between_periods_tests),
	TS(when_sched_run_wait_credits_are_consumed__should_yield),
	TS(each_schedule__credits_are_consumed_and_wrap),
	TS(when_sched_continuous_credits_1__sleeps_and_yield_appropriately),
	TS(when_sched_continuous_have_run_half_freq__should_sleep),
	TS(when_sched_continuous_past_freq__should_yield),
	TS(when_sched_continuous_past_freq__should_yield),
};

int
run_scrubbing_sched_tests()
{
	int rc = 0;

	rc += cmocka_run_group_tests_name(
		"Test logic for how the schedule is controlled (sleeping vs "
		"yield) based on schedules, where at in scrubbing process, etc",
		scrubbing_sched_tests, NULL, NULL);

	return rc;
}
