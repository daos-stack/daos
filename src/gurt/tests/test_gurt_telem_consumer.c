/*
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * This file tests telemetry consumption in GURT.
 * It is tightly coupled to the telemetry production test application
 * that must be run first.  That application generates the metrics that are
 * read and examined by the tests here.
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include "tests_lib.h"
#include "wrap_cmocka.h"
#include "gurt/telemetry_common.h"
#include "gurt/telemetry_consumer.h"

#define STATS_EPSILON	0.00001

struct d_tm_node_t	*root;
uint64_t		*shmem_root;

static int
init_tests(void **state)
{
	int	simulated_srv_idx = 99;

	shmem_root = d_tm_get_shared_memory(simulated_srv_idx);
	assert_non_null(shmem_root);

	root = d_tm_get_root(shmem_root);
	assert_non_null(root);

	return d_log_init();
}

static void
test_shmem_removed(void **state)
{
	uint64_t	*shmem;
	int		simulated_srv_idx = 100;

	printf("This operation is expected to generate an error:\n");
	shmem = d_tm_get_shared_memory(simulated_srv_idx);
	assert_null(shmem);
}

static void
test_print_metrics(void **state)
{
	struct d_tm_node_t	*node;
	int			filter;

	node = d_tm_find_metric(shmem_root, "gurt");
	assert_non_null(node);

	filter = (D_TM_COUNTER | D_TM_TIMESTAMP | D_TM_TIMER_SNAPSHOT |
		  D_TM_DURATION | D_TM_GAUGE | D_TM_DIRECTORY);

	d_tm_print_my_children(shmem_root, node, 0, filter, NULL, D_TM_STANDARD,
			       true, true, stdout);
}

static void
test_verify_object_count(void **state)
{
	struct d_tm_node_t	*node;
	int			num;

	node = d_tm_find_metric(shmem_root, "gurt/tests/telem");
	assert_non_null(node);

	num = d_tm_count_metrics(shmem_root, node, D_TM_COUNTER);
	assert_int_equal(num, 18);

	num = d_tm_count_metrics(shmem_root, node, D_TM_GAUGE);
	assert_int_equal(num, 4);

	num = d_tm_count_metrics(shmem_root, node, D_TM_DURATION);
	assert_int_equal(num, 2);

	num = d_tm_count_metrics(shmem_root, node, D_TM_TIMESTAMP);
	assert_int_equal(num, 1);

	num = d_tm_count_metrics(shmem_root, node, D_TM_TIMER_SNAPSHOT);
	assert_int_equal(num, 2);

	num = d_tm_count_metrics(shmem_root, node,
				 D_TM_COUNTER | D_TM_GAUGE | D_TM_DURATION |
				 D_TM_TIMESTAMP | D_TM_TIMER_SNAPSHOT);
	assert_int_equal(num, 27);
}

static void
test_verify_loop_counter(void **state)
{
	uint64_t	val;
	int		rc;

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/loop counter");
	assert_rc_equal(rc, DER_SUCCESS);

	assert_int_equal(val, 5000);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/manually_set");
	assert_rc_equal(rc, DER_SUCCESS);

	assert_int_equal(val, 5001);
}

static void
test_verify_test_counter(void **state)
{
	uint64_t	val;
	int		rc;

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/counter 1");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, 3);
}

static void
test_metric_not_found(void **state)
{
	uint64_t	val = 0;
	int		rc;

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/this doesn't exist");
	assert_rc_equal(rc, -DER_METRIC_NOT_FOUND);
	assert_int_equal(val, 0);
}

static void
test_find_metric(void **state)
{
	struct d_tm_node_t	*node;

	/** should find this one */
	node = d_tm_find_metric(shmem_root, "gurt");
	assert_non_null(node);

	/** should find this one */
	node = d_tm_find_metric(shmem_root, "gurt/tests/telem/gauge");
	assert_non_null(node);

	/** should not find this one */
	node = d_tm_find_metric(shmem_root, "gurts");
	assert_null(node);

	/** should not find this one */
	node = d_tm_find_metric(NULL, "gurts");
	assert_null(node);

	/** should not find this one */
	node = d_tm_find_metric(NULL, "gurt");
	assert_null(node);

	/** should not find this one */
	node = d_tm_find_metric(NULL, NULL);
	assert_null(node);
}

static void
test_verify_gauge(void **state)
{
	struct d_tm_stats_t	stats;
	uint64_t		val;
	int			rc;

	rc = d_tm_get_gauge(&val, &stats, shmem_root, NULL,
			    "gurt/tests/telem/gauge");
	assert_rc_equal(rc, DER_SUCCESS);

	rc = d_tm_get_gauge(&val, NULL, shmem_root, NULL,
			    "gurt/tests/telem/gauge");
	assert_rc_equal(rc, DER_SUCCESS);

	assert_int_equal(val, 1650);
}

static void
test_timer_snapshot(void **state)
{
	struct timespec	tms1;
	struct timespec	tms2;
	struct timespec tms3;
	int		rc;

	rc = d_tm_get_timer_snapshot(&tms1, shmem_root, NULL,
				     "gurt/tests/telem/snapshot sample 1");
	assert_rc_equal(rc, DER_SUCCESS);

	rc = d_tm_get_timer_snapshot(&tms2, shmem_root, NULL,
				     "gurt/tests/telem/snapshot sample 2");
	assert_rc_equal(rc, DER_SUCCESS);

	tms3 = d_timediff(tms1, tms2);

	/**
	 * Just verifies that some amount of time elapsed because it is hard
	 * to accurately determine how long this should take on any given
	 * system under test.  The first snapshot was taken prior to executing
	 * test_increment_counter() that performs 5000 increment operations.
	 * The second snapshot occurs after the increments complete.
	 */
	assert((tms3.tv_sec + tms3.tv_nsec) > 0);
}

static void
test_gauge_stats(void **state)
{
	struct d_tm_stats_t	stats;
	uint64_t		val;
	int			rc;

	rc = d_tm_get_gauge(&val, &stats, shmem_root, NULL,
			    "gurt/tests/telem/gauge-stats");
	assert_rc_equal(rc, DER_SUCCESS);

	assert_int_equal(val, 20);
	assert_int_equal(stats.dtm_min, 2);
	assert_int_equal(stats.dtm_max, 20);
	assert(stats.mean - 11.0 < STATS_EPSILON);
	assert(stats.std_dev - 5.89379 < STATS_EPSILON);

}

static void
test_duration_stats(void **state)
{
	struct d_tm_stats_t	stats;
	struct timespec		tms;
	int			rc;

	rc = d_tm_get_duration(&tms, &stats, shmem_root, NULL,
			       "gurt/tests/telem/duration-stats");
	assert_rc_equal(rc, DER_SUCCESS);

	assert_int_equal(stats.dtm_min, 1125000);
	assert_int_equal(stats.dtm_max, 5600000);
	assert(stats.mean - 3250000 < STATS_EPSILON);
	assert(stats.std_dev - 1743290.71012 < STATS_EPSILON);

	/**
	 * This duration was initialized with one good interval, and one
	 * failed interval.  Therefore, there should be one item in the stats.
	 */
	rc = d_tm_get_duration(&tms, &stats, shmem_root, NULL,
			       "gurt/tests/telem/interval");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(stats.sample_size, 1);
}

static void
test_histogram_stats(void **state)
{
	uint64_t	val;
	int		rc;

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 0");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, 3);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 1");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, 5);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 2");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, 2);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 3");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, 0);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 4");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, 4);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 5");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, 0);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 6");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, 0);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 7");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, 0);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 8");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, 0);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 9");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, 1);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m2/bucket 0");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, 3);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m2/bucket 1");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, 4);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m2/bucket 2");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, 2);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m2/bucket 3");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, 3);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m2/bucket 4");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(val, 4);
}

static void
test_histogram_metadata(void **state)
{
	char	*metadata;
	int	rc;

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 0");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 0 [0 .. 4]");
	free(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 1");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 1 [5 .. 9]");
	free(metadata);
	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 2");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 2 [10 .. 14]");
	free(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 3");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 3 [15 .. 19]");
	free(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 4");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 4 [20 .. 24]");
	free(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 5");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 5 [25 .. 29]");
	free(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 6");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 6 [30 .. 34]");
	free(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 7");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 7 [35 .. 39]");
	free(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 8");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 8 [40 .. 44]");
	free(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 9");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(metadata,
			    "histogram bucket 9 [45 .. 18446744073709551615]");
	free(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m2/bucket 0");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 0 [0 .. 2047]");
	free(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m2/bucket 1");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 1 [2048 .. 6143]");
	free(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m2/bucket 2");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 2 [6144 .. 14335]");
	free(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m2/bucket 3");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 3 [14336 .. 30719]");
	free(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m2/bucket 4");
	assert_rc_equal(rc, DER_SUCCESS);
	assert_string_equal(metadata,
			    "histogram bucket 4 [30720 .. "
			    "18446744073709551615]");
	free(metadata);
}

static void
test_histogram_bucket_data(void **state)
{
	struct d_tm_histogram_t	histogram;
	struct d_tm_bucket_t	bucket;
	struct d_tm_node_t	*node;
	int			rc;

	node = d_tm_find_metric(shmem_root, "gurt/tests/telem/test_gauge_m1");
	assert_non_null(node);

	rc = d_tm_get_num_buckets(&histogram, shmem_root, node);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_rc_equal(rc, DER_SUCCESS);

	assert_int_equal(histogram.dth_num_buckets, 10);
	assert_int_equal(histogram.dth_initial_width, 5);
	assert_int_equal(histogram.dth_value_multiplier, 1);

	rc = d_tm_get_bucket_range(&bucket, 0, shmem_root, node);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(bucket.dtb_min, 0);
	assert_int_equal(bucket.dtb_max, 4);

	rc = d_tm_get_bucket_range(&bucket, 1, shmem_root, node);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(bucket.dtb_min, 5);
	assert_int_equal(bucket.dtb_max, 9);

	rc = d_tm_get_bucket_range(&bucket, 2, shmem_root, node);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(bucket.dtb_min, 10);
	assert_int_equal(bucket.dtb_max, 14);

	rc = d_tm_get_bucket_range(&bucket, 10, shmem_root, node);
	assert_rc_equal(rc, -DER_INVAL);

	node = d_tm_find_metric(shmem_root, "gurt/tests/telem/test_gauge_m2");
	assert_non_null(node);

	rc = d_tm_get_num_buckets(&histogram, shmem_root, node);
	assert_rc_equal(rc, DER_SUCCESS);

	assert_int_equal(histogram.dth_num_buckets, 5);
	assert_int_equal(histogram.dth_initial_width, 2048);
	assert_int_equal(histogram.dth_value_multiplier, 2);

	rc = d_tm_get_bucket_range(&bucket, 0, shmem_root, node);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(bucket.dtb_min, 0);
	assert_int_equal(bucket.dtb_max, 2047);

	rc = d_tm_get_bucket_range(&bucket, 1, shmem_root, node);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(bucket.dtb_min, 2048);
	assert_int_equal(bucket.dtb_max, 6143);

	rc = d_tm_get_bucket_range(&bucket, 2, shmem_root, node);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(bucket.dtb_min, 6144);
	assert_int_equal(bucket.dtb_max, 14335);

	rc = d_tm_get_bucket_range(&bucket, 3, shmem_root, node);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(bucket.dtb_min, 14336);
	assert_int_equal(bucket.dtb_max, 30719);

	rc = d_tm_get_bucket_range(&bucket, 4, shmem_root, node);
	assert_rc_equal(rc, DER_SUCCESS);
	assert_int_equal(bucket.dtb_min, 30720);
	assert_true(bucket.dtb_max == UINT64_MAX);

	rc = d_tm_get_bucket_range(&bucket, 5, shmem_root, node);
	assert_rc_equal(rc, -DER_INVAL);
}

static int
fini_tests(void **state)
{
	d_log_fini();

	return 0;
}

int
main(int argc, char **argv)
{
	const struct CMUnitTest	tests[] = {
		cmocka_unit_test(test_print_metrics),
		cmocka_unit_test(test_verify_object_count),
		cmocka_unit_test(test_verify_loop_counter),
		cmocka_unit_test(test_verify_test_counter),
		cmocka_unit_test(test_metric_not_found),
		cmocka_unit_test(test_find_metric),
		cmocka_unit_test(test_verify_gauge),
		cmocka_unit_test(test_timer_snapshot),
		cmocka_unit_test(test_gauge_stats),
		cmocka_unit_test(test_duration_stats),
		cmocka_unit_test(test_histogram_stats),
		cmocka_unit_test(test_histogram_metadata),
		cmocka_unit_test(test_histogram_bucket_data),
		cmocka_unit_test(test_shmem_removed),
	};

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("test_gurt_telem_consumer", tests,
					   init_tests, fini_tests);
}
