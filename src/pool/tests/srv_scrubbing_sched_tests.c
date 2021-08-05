/**
 * (C) Copyright 2021 Intel Corporation.
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

static void
off_always_returns_0(void **state)
{
	struct timespec  start_time;

	d_gettime(&start_time);

	assert_int_equal(0, ds_scrub_wait_between_msec(DAOS_SCRUB_SCHED_OFF,
						       start_time, 0, 0));
	assert_int_equal(0, ds_scrub_wait_between_msec(DAOS_SCRUB_SCHED_OFF,
						       start_time, 100, 100));

}

static void
wait_always_returns_0(void **state)
{
	struct timespec  start_time;

	d_gettime(&start_time);

	assert_int_equal(0,
			 ds_scrub_wait_between_msec(DAOS_SCRUB_SCHED_RUN_WAIT,
						    start_time, 0, 0));
	assert_int_equal(0,
			 ds_scrub_wait_between_msec(DAOS_SCRUB_SCHED_RUN_WAIT,
						    start_time, 100, 100));
}

#define assert_continuous(expected, st, csum_count, freq) \
	assert_int_equal(expected, \
		ds_scrub_wait_between_msec(DAOS_SCRUB_SCHED_CONTINUOUS, \
				st, csum_count, freq))
static void
continuous_start_now_calcs_ms(void **state)
{
	struct timespec  st;

	d_gettime(&st);
	/* basic math for these is:
	 * ```freq_sec * 1000 (convert to ms) / last csum count```
	 * Plug in different numbers for freq and last csum count to see
	 * expected values. Make freq big and last csum count small, vice versa,
	 * how are really large values handled ...
	 *
	 * All these tests have a start time of now.
	 */
	assert_continuous(1000, st, 10, 10);
	assert_continuous(500, st, 10, 5);
	assert_continuous(1, st, 10000, 10);
	assert_continuous(0, st, 10001, 10); /* can't sleep less than 1 ms */
	assert_continuous(2419200, st, 250, 3600 * 24 * 7); /* 7 days */
	assert_continuous(2419200, st, 250, 3600 * 24 * 7); /* 7 days */
	/* infinite (almost) */
	assert_continuous(UINT64_MAX / 100, st, 100, UINT64_MAX);
}

static void
continuous_start_10_sec_ago_calcs_ms(void **state)
{
	struct timespec  st;

	d_gettime(&st);
	st.tv_sec -= 10;

	/* ten seconds have passed an freq is 10 seconds, so should not wait
	 * any longer in between checksums
	 */
	assert_continuous(0, st, 10, 10);
	assert_continuous(500, st, 10, 15); /* 5 seconds left */

	st.tv_sec -= 10000;
	/* Should have finished a long time ago */
	assert_continuous(0, st, 10, 15);
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

void run_sched_control(struct scrub_ctx *ctx)
{
	ds_scrub_sched_control(ctx);
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

	run_sched_control(&ctx);
	/* don't yield until all credits are consumed */
	assert_int_equal(1, ctx.sc_credits_left);
	assert_int_equal(0, test_yield_fn_call_count);

	/* credits are consumed */
	run_sched_control(&ctx);
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
		DAOS_SCRUB_SCHED_CONTINUOUS,
		DAOS_SCRUB_SCHED_RUN_ONCE,
		DAOS_SCRUB_SCHED_RUN_ONCE_NO_YIELD,
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

		run_sched_control(&ctx);
		assert_int_equal(2, ctx.sc_credits_left);

		run_sched_control(&ctx);
		assert_int_equal(1, ctx.sc_credits_left);

		run_sched_control(&ctx);
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
		.tst_pool_csum_calcs = 0,
		.tst_already_run_sec = 0,
		.tst_scrub_freq_sec = 10,
		});

	run_sched_control(&ctx);
	assert_int_equal(1, test_sleep_fn_call_count);
	assert_int_equal(1000, test_sleep_fn_msec);

	/* simulate 1 second passing and 1 csum calculated */
	ctx.sc_pool_start_scrub.tv_sec--;
	ctx.sc_pool_csum_calcs++;
	run_sched_control(&ctx);
	assert_int_equal(2, test_sleep_fn_call_count);
	assert_int_equal(1000, test_sleep_fn_msec);

	/* simulate 1 second passing and 1 csum calculated */
	ctx.sc_pool_start_scrub.tv_sec--;
	ctx.sc_pool_csum_calcs++;
	run_sched_control(&ctx);
	assert_int_equal(3, test_sleep_fn_call_count);
	assert_int_equal(1000, test_sleep_fn_msec);

	/*
	 * simulate 1 minute passing and still going (even though have
	 * calculated a lot)
	 */
	ctx.sc_pool_start_scrub.tv_sec -= 60;
	ctx.sc_pool_csum_calcs += 100;
	run_sched_control(&ctx);
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

	run_sched_control(&ctx);
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

	run_sched_control(&ctx);

	assert_int_equal(0, test_sleep_fn_msec);
	assert_int_equal(0, test_sleep_fn_call_count);
	assert_int_equal(1, test_yield_fn_call_count);

	free_ctx(&ctx);
}

/* By default, the ULT should sleep 5 seconds if the schedule is off before
 * checking again.
 */
static void
when_sched_off__should_sleep_5_sec(void **state)
{
	struct scrub_ctx	ctx = {0};

	INIT_CTX_FOR_TESTS(&ctx, {
		.tst_scrub_sched = DAOS_SCRUB_SCHED_OFF,
		.tst_scrub_status = SCRUB_STATUS_NOT_RUNNING
		});

	run_sched_control(&ctx);
	assert_int_equal(1000 * 5, test_sleep_fn_msec);

	free_ctx(&ctx);
}

static void
when_sched_is_no_yield__should_not_sleep_or_yield(void **state)
{
	struct scrub_ctx	ctx = {0};

	INIT_CTX_FOR_TESTS(&ctx, {
		.tst_scrub_sched = DAOS_SCRUB_SCHED_RUN_ONCE_NO_YIELD,
		.tst_scrub_status = SCRUB_STATUS_RUNNING,
		.tst_scrub_freq_sec = 10,
		.tst_scrub_cred = 1,
		.tst_pool_last_csum_calcs = 10,
		});

	run_sched_control(&ctx);

	assert_int_equal(0, test_yield_fn_call_count);
	assert_int_equal(0, test_sleep_fn_call_count);

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

static const struct CMUnitTest wait_between_tests[] = {
	TS(off_always_returns_0),
	TS(when_sched_off__should_sleep_5_sec),
	TS(wait_always_returns_0),
	TS(continuous_start_now_calcs_ms),
	TS(continuous_start_10_sec_ago_calcs_ms),
};

static const struct CMUnitTest scrubbing_sched_tests[] = {
	TS(when_sched_run_wait_credits_are_consumed__should_yield),
	TS(each_schedule__credits_are_consumed_and_wrap),
	TS(when_sched_continuous_credits_1__sleeps_and_yield_appropriately),
	TS(when_sched_continuous_have_run_half_freq__should_sleep),
	TS(when_sched_continuous_past_freq__should_yield),
	TS(when_sched_is_no_yield__should_not_sleep_or_yield),
};

int
run_scrubbing_sched_tests()
{
	int rc = 0;

	rc += cmocka_run_group_tests_name(
		"Test calculations and logic for how long to wait when credits "
		"are consumed.",
		wait_between_tests, NULL, NULL);
	rc += cmocka_run_group_tests_name(
		"Test logic for how the schedule is controlled (sleeping vs "
		"yield) based on schedules, where at in scrubbing process, etc",
		scrubbing_sched_tests, NULL, NULL);

	return rc;
}
