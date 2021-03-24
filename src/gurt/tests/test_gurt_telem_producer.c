/*
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * This file tests telemetry production in GURT
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include "tests_lib.h"
#include "wrap_cmocka.h"
#include "gurt/telemetry_common.h"
#include "gurt/telemetry_producer.h"

static int
init_tests(void **state)
{
	int	simulated_srv_idx = 99;
	int	rc;

	rc = d_tm_init(simulated_srv_idx, D_TM_SHARED_MEMORY_SIZE,
		       D_TM_RETAIN_SHMEM);
	assert_rc_equal(rc, DER_SUCCESS);

	return d_log_init();
}

static void
test_increment_counter(void **state)
{
	static struct d_tm_node_t	*loop;
	int				count = 5000;
	int				rc;
	int				i;

	for (i = 0; i < count - 1; i++) {
		rc = d_tm_increment_counter(&loop, 1,
					    "gurt/tests/telem/loop counter");
		assert_rc_equal(rc, DER_SUCCESS);
	}

	/**
	 * Use the pointer without the name provided to show that it still
	 * increments the loop counter.
	 */
	rc = d_tm_increment_counter(&loop, 1, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
}

static void
test_add_to_counter(void **state)
{
	static struct d_tm_node_t	*loop;
	int				count = 5000;
	int				rc;

	/** Create this counter, and add 'count' to it */
	rc = d_tm_increment_counter(&loop, count,
				    "gurt/tests/telem/manually_set");
	assert_rc_equal(rc, DER_SUCCESS);

	/**
	 * Counter now has value 'count'
	 * We will now increment it, and the result should be 'count + 1'.
	 */
	rc = d_tm_increment_counter(&loop, 1, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
}

static void
test_gauge(void **state)
{
	static struct d_tm_node_t	*gauge;
	int				init_val = 50;
	int				inc_count = 2700;
	int				dec_count = 1100;
	int				rc;
	int				i;

	rc = d_tm_set_gauge(&gauge, init_val, "gurt/tests/telem/gauge");
	assert_rc_equal(rc, DER_SUCCESS);

	for (i = 0; i < inc_count; i++) {
		rc = d_tm_increment_gauge(&gauge, 1, "gurt/tests/telem/gauge");
		assert_rc_equal(rc, DER_SUCCESS);
	}

	for (i = 0; i < dec_count; i++) {
		rc = d_tm_decrement_gauge(&gauge, 1, "gurt/tests/telem/gauge");
		assert_rc_equal(rc, DER_SUCCESS);
	}
}

static void
test_record_timestamp(void **state)
{
	static struct d_tm_node_t	*ts;
	int				rc;

	rc = d_tm_record_timestamp(&ts, "gurt/tests/telem/last executed");
	assert_rc_equal(rc, DER_SUCCESS);
}

static void
test_interval_timer(void **state)
{
	static struct d_tm_node_t	*timer;
	struct timespec			ts;
	int				rc;

	rc = d_tm_mark_duration_start(&timer, D_TM_CLOCK_REALTIME,
				      "gurt/tests/telem/interval");
	assert_rc_equal(rc, DER_SUCCESS);

	ts.tv_sec = 0;
	ts.tv_nsec = 50000000;
	nanosleep(&ts, NULL);

	rc = d_tm_mark_duration_end(&timer, rc, NULL);
	assert_rc_equal(rc, DER_SUCCESS);

	/**
	 * Now start a timer that will be aborted.  The consumer test will
	 * not see these intervals in the stats.
	 */
	rc = d_tm_mark_duration_start(&timer, D_TM_CLOCK_REALTIME,
				      "gurt/tests/telem/interval");
	assert_rc_equal(rc, DER_SUCCESS);

	ts.tv_sec = 0;
	ts.tv_nsec = 25000000;
	nanosleep(&ts, NULL);

	rc = d_tm_mark_duration_end(&timer, ~rc, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
}

static void
test_timer_snapshot_sample_1(void **state)
{
	static struct d_tm_node_t	*snapshot;
	int				rc;

	rc = d_tm_take_timer_snapshot(&snapshot, D_TM_CLOCK_REALTIME,
				      "gurt/tests/telem/snapshot sample 1");
	assert_rc_equal(rc, DER_SUCCESS);
}

static void
test_timer_snapshot_sample_2(void **state)
{
	static struct d_tm_node_t	*snapshot;
	int				rc;

	rc = d_tm_take_timer_snapshot(&snapshot, D_TM_CLOCK_REALTIME,
				      "gurt/tests/telem/snapshot sample 2");
	assert_rc_equal(rc, DER_SUCCESS);
}

static void
test_input_validation(void **state)
{
	static struct d_tm_node_t	*node;
	static struct d_tm_node_t	*temp;
	char				path[D_TM_MAX_NAME_LEN + 1];
	int				rc;
	int				i;

	/** uninitialized node ptr at initialization time */
	rc = d_tm_increment_counter(&node, 1, "gurt/tests/telem/counter 1");
	assert_rc_equal(rc, DER_SUCCESS);

	/** Use the initialized node without specifying a name */
	rc = d_tm_increment_counter(&node, 1, NULL);
	assert_rc_equal(rc, DER_SUCCESS);

	/** Provide a NULL node pointer, force the API to use the name */
	rc = d_tm_increment_counter(NULL, 1, "gurt/tests/telem/counter 1");
	assert_rc_equal(rc, DER_SUCCESS);

	/** Verify correct function associated with this metric type is used */
	printf("This operation is expected to generate an error:\n");
	rc = d_tm_increment_gauge(NULL, 1, "gurt/tests/telem/counter 1");
	assert_rc_equal(rc, -DER_OP_NOT_PERMITTED);

	/** Verify correct function associated with this metric type is used */
	printf("This operation is expected to generate an error:\n");
	rc = d_tm_increment_gauge(&node, 1, NULL);
	assert_rc_equal(rc, -DER_OP_NOT_PERMITTED);

	/** Specifying a null pointer and no path should fail */
	rc = d_tm_increment_counter(NULL, 1, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	/** Specifying a null pointer and no path should fail */
	rc = d_tm_increment_counter(&temp, 1, NULL);
	assert_rc_equal(rc, -DER_INVAL);


	/** format specifier with strings */
	rc = d_tm_increment_counter(NULL, 1, "%s/%s", "my", "counter");
	assert_rc_equal(rc, DER_SUCCESS);

	/** format specifier with numbers */
	rc = d_tm_increment_counter(NULL, 1, "%d", rand() % 10000);
	assert_rc_equal(rc, DER_SUCCESS);

	/** format specifier with strings and numbers */
	rc = d_tm_increment_counter(NULL, 1, "my/%s/format/type/%d",
				    "arbitrary", 7);
	assert_rc_equal(rc, DER_SUCCESS);

	/**
	 * The API accepts a path length that is D_TM_MAX_NAME_LEN including
	 * the null terminator.  Any path exceeding that generates an error.
	 * Intentionally fills path with D_TM_MAX_NAME_LEN characters
	 * and NULL terminates to create a path length that is 1 too large.
	 */
	for (i = 0; i < D_TM_MAX_NAME_LEN; i++)
		path[i] = '0' + i % 10;
	path[D_TM_MAX_NAME_LEN] = 0;
	rc = d_tm_increment_counter(NULL, 1, path);
	assert_rc_equal(rc, -DER_EXCEEDS_PATH_LEN);

	/** Now trim the path by 1 character to make it fit */
	path[D_TM_MAX_NAME_LEN - 1] = 0;
	rc = d_tm_increment_counter(NULL, 1, path);
	assert_rc_equal(rc, DER_SUCCESS);

	/**
	 * After using "root" + "/", size the buffer 1 character too large
	 */
	path[D_TM_MAX_NAME_LEN - 5]  = 0;
	rc = d_tm_increment_counter(NULL, 1, "root/%s", path);
	assert_rc_equal(rc, -DER_EXCEEDS_PATH_LEN);

	/**
	 * After using "root" + "/", size the buffer correctly so it just fits.
	 */
	path[D_TM_MAX_NAME_LEN - 6] = 0;
	rc = d_tm_increment_counter(NULL, 1, "root/%s", path);
	assert_rc_equal(rc, DER_SUCCESS);

}

static void
test_shared_memory_cleanup(void **state)
{
	int	simulated_srv_idx = 100;
	int	rc;

	/**
	 * Cleanup from all other tests
	 */
	d_tm_fini();

	/**
	 * Initialize the library as the server process would, which instructs
	 * the library to remove the shared memory segment upon process detach.
	 * The corresponding consumer test will fail to attach to the segment
	 * created here, because it was removed before the consumer executed.
	 */

	rc = d_tm_init(simulated_srv_idx, D_TM_SHARED_MEMORY_SIZE,
		       D_TM_SERVER_PROCESS);
	assert_rc_equal(rc, DER_SUCCESS);
}

static void
test_gauge_stats(void **state)
{
	int	rc;
	int	i;
	int	len;
	int	test_values[] = {2, 4, 6, 8, 10, 12, 14,
				 16, 18, 20, 2, 4, 6, 8,
				 10, 12, 14, 16, 18, 20};

	len =  (int)(sizeof(test_values) / sizeof(int));
	for (i = 0; i < len; i++) {
		rc = d_tm_set_gauge(NULL, test_values[i],
				    "gurt/tests/telem/gauge-stats");
		assert_rc_equal(rc, DER_SUCCESS);
	}
}

static void
test_duration_stats(void **state)
{
	static struct d_tm_node_t	*timer;
	uint64_t			microseconds;
	int				rc;

	/*
	 * Manually add this duration metric so that it allocates the resources
	 * for this node.  Then manually store timer values into the metric,
	 * to avoid actually timing something for this test.  This will produce
	 * a set of known values each run.  Simulate what happens when running
	 * the timer by calling the d_tm_compute_stats() each time a
	 * new duration value is created.  This allows the statistics to be
	 * updated at each step, as they would be when the duration API is
	 * used normally.  The consumer test component will read the duration
	 * stats and compare that to the expected values to determine success
	 * or failure.
	 */

	rc = d_tm_add_metric(&timer, D_TM_DURATION | D_TM_CLOCK_REALTIME,
			     NULL, NULL, "gurt/tests/telem/duration-stats");
	assert_rc_equal(rc, DER_SUCCESS);

	timer->dtn_metric->dtm_data.tms[0].tv_sec = 1;
	timer->dtn_metric->dtm_data.tms[0].tv_nsec = 125000000;
	microseconds = timer->dtn_metric->dtm_data.tms[0].tv_sec * 1000000 +
		       timer->dtn_metric->dtm_data.tms[0].tv_nsec / 1000;
	d_tm_compute_stats(timer, microseconds);

	timer->dtn_metric->dtm_data.tms[0].tv_sec = 2;
	timer->dtn_metric->dtm_data.tms[0].tv_nsec = 150000000;
	microseconds = timer->dtn_metric->dtm_data.tms[0].tv_sec * 1000000 +
		       timer->dtn_metric->dtm_data.tms[0].tv_nsec / 1000;
	d_tm_compute_stats(timer, microseconds);


	timer->dtn_metric->dtm_data.tms[0].tv_sec = 3;
	timer->dtn_metric->dtm_data.tms[0].tv_nsec = 175000000;
	microseconds = timer->dtn_metric->dtm_data.tms[0].tv_sec * 1000000 +
		       timer->dtn_metric->dtm_data.tms[0].tv_nsec / 1000;
	d_tm_compute_stats(timer, microseconds);


	timer->dtn_metric->dtm_data.tms[0].tv_sec = 4;
	timer->dtn_metric->dtm_data.tms[0].tv_nsec = 200000000;
	microseconds = timer->dtn_metric->dtm_data.tms[0].tv_sec * 1000000 +
		       timer->dtn_metric->dtm_data.tms[0].tv_nsec / 1000;
	d_tm_compute_stats(timer, microseconds);


	timer->dtn_metric->dtm_data.tms[0].tv_sec = 5;
	timer->dtn_metric->dtm_data.tms[0].tv_nsec = 600000000;
	microseconds = timer->dtn_metric->dtm_data.tms[0].tv_sec * 1000000 +
		       timer->dtn_metric->dtm_data.tms[0].tv_nsec / 1000;
	d_tm_compute_stats(timer, microseconds);
}

static void
test_gauge_with_histogram_multiplier_1(void **state)
{
	static struct d_tm_node_t	*gauge;
	int				num_buckets;
	int				initial_width;
	int				multiplier;
	int				rc;
	char				path[D_TM_MAX_NAME_LEN];

	snprintf(path, sizeof(path), "%s", "gurt/tests/telem/test_gauge_m1");

	rc = d_tm_add_metric(&gauge, D_TM_GAUGE,
			     "A gauge with a histogram",
			     "This histogram uses buckets with a multiplier "
			     "of 1", path);
	assert_rc_equal(rc, DER_SUCCESS);

	num_buckets = 10;
	initial_width = 5;
	multiplier = 1;

	rc = d_tm_init_histogram(gauge, path, num_buckets,
				 initial_width, multiplier);
	assert_rc_equal(rc, DER_SUCCESS);

	/* bucket 0 - gets 3 values */
	rc = d_tm_set_gauge(&gauge, 2, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 0, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 4, NULL);
	assert_rc_equal(rc, DER_SUCCESS);

	/* bucket 1 - gets 5 values  */
	rc = d_tm_set_gauge(&gauge, 5, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 6, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 7, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 7, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 5, NULL);

	/* bucket 2 - gets 2 values  */
	rc = d_tm_set_gauge(&gauge, 10, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 12, NULL);
	assert_rc_equal(rc, DER_SUCCESS);

	/* bucket 4 - gets 4 values  */
	rc = d_tm_set_gauge(&gauge, 20, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 21, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 24, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 24, NULL);
	assert_rc_equal(rc, DER_SUCCESS);

	/* bucket 9 - gets 1 value */
	rc = d_tm_set_gauge(&gauge, 1900, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
}

static void
test_gauge_with_histogram_multiplier_2(void **state)
{
	static struct d_tm_node_t	*gauge;
	int				num_buckets;
	int				initial_width;
	int				multiplier;
	int				rc;
	char				path[D_TM_MAX_NAME_LEN];

	snprintf(path, sizeof(path), "%s", "gurt/tests/telem/test_gauge_m2");

	rc = d_tm_add_metric(&gauge, D_TM_GAUGE,
			     NULL,
			     "A gauge with a histogram.  This gauge has no "
			     "short description metadata.  This histogram uses "
			     "buckets with a multiplier of 2", path);
	assert_rc_equal(rc, DER_SUCCESS);

	num_buckets = 5;
	initial_width = 2048;
	multiplier = 2;

	rc = d_tm_init_histogram(gauge, path, num_buckets,
				 initial_width, multiplier);
	assert_rc_equal(rc, DER_SUCCESS);

	/* bucket 0 - gets 3 values */
	rc = d_tm_set_gauge(&gauge, 0, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 512, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 2047, NULL);
	assert_rc_equal(rc, DER_SUCCESS);

	/* bucket 1 - gets 4 values  */
	rc = d_tm_set_gauge(&gauge, 2048, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 2049, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 3000, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 6143, NULL);

	/* bucket 2 - gets 2 values  */
	rc = d_tm_set_gauge(&gauge, 6144, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 14335, NULL);
	assert_rc_equal(rc, DER_SUCCESS);

	/* bucket 3 - gets 3 values  */
	rc = d_tm_set_gauge(&gauge, 14336, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 16383, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 30719, NULL);
	assert_rc_equal(rc, DER_SUCCESS);

	/* bucket 4 - gets 4 values  */
	rc = d_tm_set_gauge(&gauge, 30720, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 35000, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 40000, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_set_gauge(&gauge, 65000, NULL);
	assert_rc_equal(rc, DER_SUCCESS);
}

static int
fini_tests(void **state)
{
	d_tm_fini();
	d_log_fini();

	return 0;
}

int
main(int argc, char **argv)
{
	const struct CMUnitTest	tests[] = {
		cmocka_unit_test(test_timer_snapshot_sample_1),
		cmocka_unit_test(test_increment_counter),
		cmocka_unit_test(test_add_to_counter),
		cmocka_unit_test(test_timer_snapshot_sample_2),
		cmocka_unit_test(test_gauge),
		cmocka_unit_test(test_record_timestamp),
		cmocka_unit_test(test_interval_timer),
		cmocka_unit_test(test_input_validation),
		cmocka_unit_test(test_gauge_stats),
		cmocka_unit_test(test_duration_stats),
		cmocka_unit_test(test_gauge_with_histogram_multiplier_1),
		cmocka_unit_test(test_gauge_with_histogram_multiplier_2),
		/**
		 * Run this test last, because anything written after this test
		 * is erased.
		 */
		cmocka_unit_test(test_shared_memory_cleanup),
	};

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("test_gurt_telem_producer", tests,
					   init_tests, fini_tests);
}
