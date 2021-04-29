/*
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * This file tests the telemetry API in GURT.
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include "tests_lib.h"
#include "wrap_cmocka.h"
#include "gurt/telemetry_common.h"
#include "gurt/telemetry_producer.h"
#include "gurt/telemetry_consumer.h"

#define STATS_EPSILON	(0.00001)
#define TEST_IDX	(99)

/* Context for checking results as a client */
static struct d_tm_context	*cli_ctx;

static int
init_tests(void **state)
{
	int	simulated_srv_idx = TEST_IDX;
	int	rc;

	rc = d_tm_init(simulated_srv_idx, D_TM_SHARED_MEMORY_SIZE,
		       D_TM_RETAIN_SHMEM);
	assert_rc_equal(rc, DER_SUCCESS);

	cli_ctx = d_tm_open(simulated_srv_idx);
	assert_non_null(cli_ctx);

	return d_log_init();
}

static void
test_increment_counter(void **state)
{
	struct d_tm_node_t	*loop;
	int			count = 5000;
	int			rc;
	int			i;
	char			*path = "gurt/tests/telem/loop counter";
	uint64_t		val;

	rc = d_tm_add_metric(&loop, D_TM_COUNTER, NULL, NULL, path);
	assert_rc_equal(rc, 0);

	for (i = 0; i < count; i++) {
		d_tm_inc_counter(loop, 1);
	}

	rc = d_tm_get_counter(cli_ctx, &val, d_tm_conv_ptr(cli_ctx, loop));
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, count);
}

static void
test_add_to_counter(void **state)
{
	struct d_tm_node_t	*loop;
	int			count = 5000;
	int			rc;
	char			*path = "gurt/tests/telem/manually_set";
	uint64_t		val;

	rc = d_tm_add_metric(&loop, D_TM_COUNTER, NULL, NULL, path);
	assert_rc_equal(rc, 0);

	d_tm_inc_counter(loop, count);
	d_tm_inc_counter(loop, 1);

	rc = d_tm_get_counter(cli_ctx, &val, d_tm_conv_ptr(cli_ctx, loop));
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, count + 1);
}

static void
test_gauge(void **state)
{
	struct d_tm_node_t	*gauge;
	int			init_val = 50;
	int			inc_count = 2700;
	int			dec_count = 1100;
	int			rc;
	int			i;
	uint64_t		val;
	char			*path = "gurt/tests/telem/gauge";

	rc = d_tm_add_metric(&gauge, D_TM_GAUGE, NULL, NULL, path);
	assert_rc_equal(rc, 0);

	d_tm_set_gauge(gauge, init_val);

	for (i = 0; i < inc_count; i++) {
		d_tm_inc_gauge(gauge, 1);
	}

	rc = d_tm_get_gauge(cli_ctx, &val, NULL, d_tm_conv_ptr(cli_ctx, gauge));
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, init_val + inc_count);

	for (i = 0; i < dec_count; i++) {
		d_tm_dec_gauge(gauge, 1);
	}

	rc = d_tm_get_gauge(cli_ctx, &val, NULL, d_tm_conv_ptr(cli_ctx, gauge));
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, init_val + inc_count - dec_count);
}

static void
test_record_timestamp(void **state)
{
	struct d_tm_node_t	*ts;
	int			rc;
	char			*path = "gurt/tests/telem/last executed";
	time_t			val = 0;

	rc = d_tm_add_metric(&ts, D_TM_TIMESTAMP, NULL, NULL, path);
	assert_rc_equal(rc, 0);

	d_tm_record_timestamp(ts);

	rc = d_tm_get_timestamp(cli_ctx, &val, d_tm_conv_ptr(cli_ctx, ts));
	assert_rc_equal(rc, DER_SUCCESS);
	/*
	 * Hard to determine exact timestamp at this point, so just verify
	 * it's nonzero.
	 */
	assert_int_not_equal(val, 0);
}

static void
test_interval_timer(void **state)
{
	struct d_tm_node_t	*timer;
	struct timespec		ts;
	struct timespec		result = {0};
	struct d_tm_stats_t	stats = {0};
	char			*path = "gurt/tests/telem/interval";
	int			rc;

	rc = d_tm_add_metric(&timer, D_TM_DURATION, NULL, NULL, path);
	assert_rc_equal(rc, 0);

	d_tm_mark_duration_start(timer, D_TM_CLOCK_REALTIME);

	ts.tv_sec = 0;
	ts.tv_nsec = 50000000;
	nanosleep(&ts, NULL);

	d_tm_mark_duration_end(timer);

	rc = d_tm_get_duration(cli_ctx, &result, &stats,
			       d_tm_conv_ptr(cli_ctx, timer));
	assert_int_equal(rc, DER_SUCCESS);
	/* very rough estimation, based on the sleep timing */
	assert_true(result.tv_nsec > ts.tv_nsec || result.tv_sec > 0);

	/* Only one sample in the stats */
	assert_int_equal(stats.sample_size, 1);
}

static void
test_timer_snapshot(void **state)
{
	struct d_tm_node_t	*snapshot1;
	char			*path1 = "gurt/tests/telem/snapshot sample 1";
	struct d_tm_node_t	*snapshot2;
	char			*path2 = "gurt/tests/telem/snapshot sample 2";
	struct timespec		tms1 = {0};
	struct timespec		tms2 = {0};
	struct timespec		tms3 = {0};
	int			rc;

	rc = d_tm_add_metric(&snapshot1, D_TM_TIMER_SNAPSHOT, NULL, NULL,
			     path1);
	assert_rc_equal(rc, 0);
	d_tm_take_timer_snapshot(snapshot1, D_TM_CLOCK_REALTIME);

	rc = d_tm_add_metric(&snapshot2, D_TM_TIMER_SNAPSHOT, NULL, NULL,
			     path2);
	assert_rc_equal(rc, 0);
	d_tm_take_timer_snapshot(snapshot2, D_TM_CLOCK_REALTIME);

	/* check values */
	rc = d_tm_get_timer_snapshot(cli_ctx, &tms1,
				     d_tm_conv_ptr(cli_ctx, snapshot1));
	assert_rc_equal(rc, 0);

	rc = d_tm_get_timer_snapshot(cli_ctx, &tms2,
				     d_tm_conv_ptr(cli_ctx, snapshot2));
	assert_rc_equal(rc, 0);

	tms3 = d_timediff(tms1, tms2);

	/**
	 * Just verifies that some amount of time elapsed because it is hard
	 * to accurately determine how long this should take on any given
	 * system under test.
	 */
	assert_true((tms3.tv_sec + tms3.tv_nsec) > 0);
}

static void
test_gauge_stats(void **state)
{
	int			rc;
	int			i;
	int			len;
	int			test_values[] = {2, 4, 6, 8, 10, 12, 14,
						 16, 18, 20, 2, 4, 6, 8,
						 10, 12, 14, 16, 18, 20};
	struct d_tm_node_t	*gauge;
	char			*path = "gurt/tests/telem/gauge-stats";
	struct d_tm_stats_t	stats;
	uint64_t		val;

	rc = d_tm_add_metric(&gauge, D_TM_GAUGE, NULL, NULL, path);
	assert_rc_equal(rc, 0);

	len =  (int)(sizeof(test_values) / sizeof(int));
	for (i = 0; i < len; i++) {
		d_tm_set_gauge(gauge, test_values[i]);
	}

	rc = d_tm_get_gauge(cli_ctx, &val, &stats,
			    d_tm_conv_ptr(cli_ctx, gauge));
	assert_rc_equal(rc, DER_SUCCESS);

	assert_int_equal(val, 20);
	assert_int_equal(stats.dtm_min, 2);
	assert_int_equal(stats.dtm_max, 20);
	assert_true(stats.mean - 11.0 < STATS_EPSILON);
	assert_true(stats.std_dev - 5.89379 < STATS_EPSILON);
}

static void
test_duration_stats(void **state)
{
	struct d_tm_node_t	*timer;
	uint64_t		microseconds;
	int			rc;
	char			*path = "gurt/tests/telem/duration-stats";
	struct d_tm_stats_t	stats;
	struct timespec		tms;

	/*
	 * Manually store timer values into the metric to avoid actually timing
	 * something for this test.  This will produce a set of known values
	 * each run.
	 *
	 * Simulate what happens when running the timer by calling the
	 * d_tm_compute_stats() each time a new duration value is created.
	 * This allows the statistics to be updated at each step, as they would
	 * be when the duration API is used normally.
	 */

	rc = d_tm_add_metric(&timer, D_TM_DURATION | D_TM_CLOCK_REALTIME,
			     D_TM_CLOCK_REALTIME_STR, D_TM_MICROSECOND, path);
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

	/* Verify the results - figured out empirically */
	rc = d_tm_get_duration(cli_ctx, &tms, &stats,
			       d_tm_conv_ptr(cli_ctx, timer));
	assert_rc_equal(rc, DER_SUCCESS);

	assert_int_equal(stats.dtm_min, 1125000);
	assert_int_equal(stats.dtm_max, 5600000);
	assert_true(stats.mean - 3250000 < STATS_EPSILON);
	assert_true(stats.std_dev - 1743290.71012 < STATS_EPSILON);
}

static void
check_bucket_counter(char *path, int bucket_id, uint64_t exp_val)
{
	struct d_tm_node_t	*node;
	uint64_t		val;
	int			rc;
	char			bucket_path[D_TM_MAX_NAME_LEN];

	snprintf(bucket_path, sizeof(bucket_path), "%s/bucket %d",
		 path, bucket_id);

	node = d_tm_find_metric(cli_ctx, bucket_path);
	assert_non_null(node);
	rc = d_tm_get_counter(cli_ctx, &val, node);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, exp_val);
}

static void
check_histogram_m1_stats(char *path)
{
	check_bucket_counter(path, 0, 3);
	check_bucket_counter(path, 1, 5);
	check_bucket_counter(path, 2, 2);
	check_bucket_counter(path, 3, 0);
	check_bucket_counter(path, 4, 4);
	check_bucket_counter(path, 5, 0);
	check_bucket_counter(path, 6, 0);
	check_bucket_counter(path, 7, 0);
	check_bucket_counter(path, 8, 0);
	check_bucket_counter(path, 9, 1);
}

static void
check_bucket_metadata(struct d_tm_node_t *node, int bucket_id)
{
	struct d_tm_bucket_t	bucket;
	char			*desc;
	char			*units;
	int			rc;
	char			exp_desc[D_TM_MAX_DESC_LEN];

	printf("Checking bucket %d\n", bucket_id);

	rc = d_tm_get_bucket_range(cli_ctx, &bucket, bucket_id, node);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_non_null(bucket.dtb_bucket);

	snprintf(exp_desc, sizeof(exp_desc),
		 "histogram bucket %d [%lu .. %lu]",
		 bucket_id, bucket.dtb_min, bucket.dtb_max);

	rc = d_tm_get_metadata(cli_ctx, &desc, &units, bucket.dtb_bucket);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(desc, exp_desc);
	free(desc);
	assert_string_equal(units, "elements");
	free(units);
}

static void
check_histogram_metadata(char *path)
{
	struct d_tm_node_t	*node;
	struct d_tm_histogram_t	histogram;
	int			rc;
	int			i;

	node = d_tm_find_metric(cli_ctx, path);
	assert_non_null(node);

	rc = d_tm_get_num_buckets(cli_ctx, &histogram, node);
	assert_rc_equal(rc, 0);

	for (i = 0; i < histogram.dth_num_buckets; i++)
		check_bucket_metadata(node, i);
}

static void
check_histogram_m1_data(char *path)
{
	struct d_tm_node_t	*gauge;
	struct d_tm_histogram_t	histogram;
	struct d_tm_bucket_t	bucket;
	int			rc;

	gauge = d_tm_find_metric(cli_ctx, path);
	assert_non_null(gauge);

	rc = d_tm_get_num_buckets(cli_ctx, &histogram, gauge);
	assert_rc_equal(rc, DER_SUCCESS);

	assert_int_equal(histogram.dth_num_buckets, 10);
	assert_int_equal(histogram.dth_initial_width, 5);
	assert_int_equal(histogram.dth_value_multiplier, 1);

	rc = d_tm_get_bucket_range(cli_ctx, &bucket, 0, gauge);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(bucket.dtb_min, 0);
	assert_int_equal(bucket.dtb_max, 4);

	rc = d_tm_get_bucket_range(cli_ctx, &bucket, 1, gauge);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(bucket.dtb_min, 5);
	assert_int_equal(bucket.dtb_max, 9);

	rc = d_tm_get_bucket_range(cli_ctx, &bucket, 2, gauge);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(bucket.dtb_min, 10);
	assert_int_equal(bucket.dtb_max, 14);

	rc = d_tm_get_bucket_range(cli_ctx, &bucket, 10, gauge);
	assert_rc_equal(rc, -DER_INVAL);
}

static void
test_gauge_with_histogram_multiplier_1(void **state)
{
	struct d_tm_node_t	*gauge;
	int			num_buckets;
	int			initial_width;
	int			multiplier;
	int			rc;
	char			*path;

	path = "gurt/tests/telem/test_gauge_m1";

	rc = d_tm_add_metric(&gauge, D_TM_GAUGE,
			     "A gauge with a histogram multiplier 1",
			     D_TM_GIGABYTE, path);
	assert_rc_equal(rc, DER_SUCCESS);

	num_buckets = 10;
	initial_width = 5;
	multiplier = 1;

	rc = d_tm_init_histogram(gauge, path, num_buckets,
				 initial_width, multiplier);
	assert_rc_equal(rc, DER_SUCCESS);

	/* bucket 0 - gets 3 values */
	d_tm_set_gauge(gauge, 2);
	d_tm_set_gauge(gauge, 0);
	d_tm_set_gauge(gauge, 4);

	/* bucket 1 - gets 5 values  */
	d_tm_set_gauge(gauge, 5);
	d_tm_set_gauge(gauge, 6);
	d_tm_set_gauge(gauge, 7);
	d_tm_set_gauge(gauge, 7);
	d_tm_set_gauge(gauge, 5);

	/* bucket 2 - gets 2 values  */
	d_tm_set_gauge(gauge, 10);
	d_tm_set_gauge(gauge, 12);

	/* bucket 4 - gets 4 values  */
	d_tm_set_gauge(gauge, 20);
	d_tm_set_gauge(gauge, 21);
	d_tm_set_gauge(gauge, 24);
	d_tm_set_gauge(gauge, 24);

	/* bucket 9 - gets 1 value */
	d_tm_set_gauge(gauge, 1900);

	/* Verify result data */
	check_histogram_m1_data(path);
	check_histogram_m1_stats(path);
	check_histogram_metadata(path);
}

static void
check_histogram_m2_stats(char *path)
{
	check_bucket_counter(path, 0, 3);
	check_bucket_counter(path, 1, 4);
	check_bucket_counter(path, 2, 2);
	check_bucket_counter(path, 3, 3);
	check_bucket_counter(path, 4, 4);
}

static void
check_histogram_m2_data(char *path)
{
	struct d_tm_node_t	*gauge;
	struct d_tm_histogram_t	histogram;
	struct d_tm_bucket_t	bucket;
	int			rc;

	gauge = d_tm_find_metric(cli_ctx, path);
	assert_non_null(gauge);

	rc = d_tm_get_num_buckets(cli_ctx, &histogram, gauge);
	assert_rc_equal(rc, DER_SUCCESS);

	assert_int_equal(histogram.dth_num_buckets, 5);
	assert_int_equal(histogram.dth_initial_width, 2048);
	assert_int_equal(histogram.dth_value_multiplier, 2);

	rc = d_tm_get_bucket_range(cli_ctx, &bucket, 0, gauge);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(bucket.dtb_min, 0);
	assert_int_equal(bucket.dtb_max, 2047);

	rc = d_tm_get_bucket_range(cli_ctx, &bucket, 1, gauge);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(bucket.dtb_min, 2048);
	assert_int_equal(bucket.dtb_max, 6143);

	rc = d_tm_get_bucket_range(cli_ctx, &bucket, 2, gauge);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(bucket.dtb_min, 6144);
	assert_int_equal(bucket.dtb_max, 14335);

	rc = d_tm_get_bucket_range(cli_ctx, &bucket, 3, gauge);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(bucket.dtb_min, 14336);
	assert_int_equal(bucket.dtb_max, 30719);

	rc = d_tm_get_bucket_range(cli_ctx, &bucket, 4, gauge);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(bucket.dtb_min, 30720);
	assert_true(bucket.dtb_max == UINT64_MAX);

	rc = d_tm_get_bucket_range(cli_ctx, &bucket, 5, gauge);
	assert_rc_equal(rc, -DER_INVAL);
}

static void
test_gauge_with_histogram_multiplier_2(void **state)
{
	struct d_tm_node_t	*gauge;
	int			num_buckets;
	int			initial_width;
	int			multiplier;
	int			rc;
	char			*path;

	path = "gurt/tests/telem/test_gauge_m2";

	rc = d_tm_add_metric(&gauge, D_TM_GAUGE,
			     "A gauge with a histogram multiplier 2",
			     D_TM_TERABYTE, path);
	assert_rc_equal(rc, DER_SUCCESS);

	num_buckets = 5;
	initial_width = 2048;
	multiplier = 2;

	rc = d_tm_init_histogram(gauge, path, num_buckets,
				 initial_width, multiplier);
	assert_rc_equal(rc, DER_SUCCESS);

	/* bucket 0 - gets 3 values */
	d_tm_set_gauge(gauge, 0);
	d_tm_set_gauge(gauge, 512);
	d_tm_set_gauge(gauge, 2047);

	/* bucket 1 - gets 4 values  */
	d_tm_set_gauge(gauge, 2048);
	d_tm_set_gauge(gauge, 2049);
	d_tm_set_gauge(gauge, 3000);
	d_tm_set_gauge(gauge, 6143);

	/* bucket 2 - gets 2 values  */
	d_tm_set_gauge(gauge, 6144);
	d_tm_set_gauge(gauge, 14335);

	/* bucket 3 - gets 3 values  */
	d_tm_set_gauge(gauge, 14336);
	d_tm_set_gauge(gauge, 16383);
	d_tm_set_gauge(gauge, 30719);

	/* bucket 4 - gets 4 values  */
	d_tm_set_gauge(gauge, 30720);
	d_tm_set_gauge(gauge, 35000);
	d_tm_set_gauge(gauge, 40000);
	d_tm_set_gauge(gauge, 65000);

	/* Verify result data */
	check_histogram_m2_data(path);
	check_histogram_m2_stats(path);
	check_histogram_metadata(path);
}

static void
test_units(void **state)
{
	struct d_tm_node_t	*counter;
	struct d_tm_node_t	*gauge;
	int			rc;
	char			*units = NULL;

	rc = d_tm_add_metric(&counter, D_TM_COUNTER, NULL, D_TM_KIBIBYTE,
			     "gurt/tests/telem/kibibyte-counter");
	assert_rc_equal(rc, DER_SUCCESS);

	rc = d_tm_get_metadata(cli_ctx, NULL, &units,
			       d_tm_conv_ptr(cli_ctx, counter));
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(units, D_TM_KIBIBYTE);
	free(units);

	rc = d_tm_add_metric(&gauge, D_TM_GAUGE, NULL, D_TM_GIGIBYTE_PER_SECOND,
			     "gurt/tests/telem/gigibyte-per-second-gauge");
	assert_rc_equal(rc, DER_SUCCESS);
	rc = d_tm_get_metadata(cli_ctx, NULL, &units,
			       d_tm_conv_ptr(cli_ctx, gauge));
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(units, D_TM_GIGIBYTE_PER_SECOND);
	free(units);
}

static void
test_find_metric(void **state)
{
	struct d_tm_node_t	*node;

	/** should find this one */
	node = d_tm_find_metric(cli_ctx, "gurt");
	assert_non_null(node);

	/** should find this one */
	node = d_tm_find_metric(cli_ctx, "gurt/tests/telem/gauge");
	assert_non_null(node);

	/** should not find this one */
	node = d_tm_find_metric(cli_ctx, "gurts");
	assert_null(node);

	/** no context */
	node = d_tm_find_metric(NULL, "gurts");
	assert_null(node);

	/** all null inputs */
	node = d_tm_find_metric(NULL, NULL);
	assert_null(node);
}

static void
test_verify_object_count(void **state)
{
	struct d_tm_node_t	*node;
	int			num;
	int			exp_num_ctr = 18;
	int			exp_num_gauge = 5;
	int			exp_num_dur = 2;
	int			exp_num_timestamp = 1;
	int			exp_num_snap = 2;
	int			exp_total;

	exp_total = exp_num_ctr + exp_num_gauge + exp_num_dur +
		    exp_num_timestamp + exp_num_snap;

	node = d_tm_find_metric(cli_ctx, "gurt/tests/telem");
	assert_non_null(node);

	num = d_tm_count_metrics(cli_ctx, node, D_TM_COUNTER);
	assert_int_equal(num, exp_num_ctr);

	num = d_tm_count_metrics(cli_ctx, node, D_TM_GAUGE);
	assert_int_equal(num, exp_num_gauge);

	num = d_tm_count_metrics(cli_ctx, node, D_TM_DURATION);
	assert_int_equal(num, exp_num_dur);

	num = d_tm_count_metrics(cli_ctx, node, D_TM_TIMESTAMP);
	assert_int_equal(num, exp_num_timestamp);

	num = d_tm_count_metrics(cli_ctx, node, D_TM_TIMER_SNAPSHOT);
	assert_int_equal(num, exp_num_snap);

	num = d_tm_count_metrics(cli_ctx, node,
				 D_TM_COUNTER | D_TM_GAUGE | D_TM_DURATION |
				 D_TM_TIMESTAMP | D_TM_TIMER_SNAPSHOT);
	assert_int_equal(num, exp_total);
}

static void
test_print_metrics(void **state)
{
	struct d_tm_node_t	*node;
	int			filter;

	node = d_tm_find_metric(cli_ctx, "gurt");
	assert_non_null(node);

	filter = (D_TM_COUNTER | D_TM_TIMESTAMP | D_TM_TIMER_SNAPSHOT |
		  D_TM_DURATION | D_TM_GAUGE | D_TM_DIRECTORY);

	d_tm_print_my_children(cli_ctx, node, 0, filter, NULL, D_TM_STANDARD,
			       D_TM_INCLUDE_METADATA, stdout);

	d_tm_print_field_descriptors(D_TM_INCLUDE_TIMESTAMP |
				     D_TM_INCLUDE_METADATA, stdout);

	filter &= ~D_TM_DIRECTORY;
	d_tm_print_my_children(cli_ctx, node, 0, filter, NULL, D_TM_CSV,
			       D_TM_INCLUDE_METADATA, stdout);
}

static void
test_shared_memory_cleanup(void **state)
{
	int			simulated_srv_idx = TEST_IDX + 1;
	int			rc;
	struct d_tm_context	*ctx2;

	/**
	 * Cleanup from all other tests
	 */
	d_tm_fini();

	/**
	 * Initialize the library as the server process would, which instructs
	 * the library to remove the shared memory segment upon process detach.
	 */

	rc = d_tm_init(simulated_srv_idx, D_TM_SHARED_MEMORY_SIZE,
		       D_TM_SERVER_PROCESS);
	assert_rc_equal(rc, DER_SUCCESS);

	/* Detach */
	d_tm_fini();

	/* Should be gone */
	printf("This operation is expected to generate an error:\n");
	ctx2 = d_tm_open(simulated_srv_idx);
	assert_null(ctx2);

	/* can still get original region */
	ctx2 = d_tm_open(TEST_IDX);
	assert_non_null(ctx2);
	d_tm_close(&ctx2);
}

static int
fini_tests(void **state)
{
	d_tm_close(&cli_ctx);
	d_tm_fini();
	d_log_fini();

	return 0;
}

int
main(int argc, char **argv)
{
	const struct CMUnitTest	tests[] = {
		cmocka_unit_test(test_timer_snapshot),
		cmocka_unit_test(test_increment_counter),
		cmocka_unit_test(test_add_to_counter),
		cmocka_unit_test(test_gauge),
		cmocka_unit_test(test_record_timestamp),
		cmocka_unit_test(test_interval_timer),
		cmocka_unit_test(test_gauge_stats),
		cmocka_unit_test(test_duration_stats),
		cmocka_unit_test(test_gauge_with_histogram_multiplier_1),
		cmocka_unit_test(test_gauge_with_histogram_multiplier_2),
		cmocka_unit_test(test_units),
		/* Run after the tests that populate the metrics */
		cmocka_unit_test(test_find_metric),
		cmocka_unit_test(test_verify_object_count),
		cmocka_unit_test(test_print_metrics),
		/* Run last since nothing can be written afterward */
		cmocka_unit_test(test_shared_memory_cleanup),
	};

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("test_gurt_telem_producer", tests,
					   init_tests, fini_tests);
}
