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
#include <math.h>
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

	shmem = d_tm_get_shared_memory(simulated_srv_idx);
	assert_null(shmem);
}

static void
test_print_metrics(void **state)
{
	struct d_tm_node_t	*node;

	node = d_tm_find_metric(shmem_root, "gurt");
	assert_non_null(node);
	d_tm_print_my_children(shmem_root, node, 0, stdout);
}

static void
test_verify_object_count(void **state)
{
	struct d_tm_node_t	*node;
	int			num;

	node = d_tm_find_metric(shmem_root, "gurt/tests/telem");
	assert_non_null(node);

	num = d_tm_count_metrics(shmem_root, node, D_TM_COUNTER);
	assert_int_equal(num, 17);

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
	assert_int_equal(num, 26);
}

static void
test_verify_loop_counter(void **state)
{
	uint64_t	val;
	int		rc;

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/loop counter");
	assert(rc == D_TM_SUCCESS);

	assert_int_equal(val, 5000);
}

static void
test_verify_test_counter(void **state)
{
	uint64_t	val;
	int		rc;

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/counter 1");
	assert(rc == D_TM_SUCCESS);
	assert_int_equal(val, 3);
}

static void
test_metric_not_found(void **state)
{
	uint64_t	val = 0;
	int		rc;

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/this doesn't exist");
	assert(rc == -DER_METRIC_NOT_FOUND);
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
	assert(rc == D_TM_SUCCESS);

	rc = d_tm_get_gauge(&val, NULL, shmem_root, NULL,
			    "gurt/tests/telem/gauge");
	assert(rc == D_TM_SUCCESS);

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
	assert(rc == D_TM_SUCCESS);

	rc = d_tm_get_timer_snapshot(&tms2, shmem_root, NULL,
				     "gurt/tests/telem/snapshot sample 2");
	assert(rc == D_TM_SUCCESS);

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
	assert(rc == D_TM_SUCCESS);

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
	assert(rc == D_TM_SUCCESS);

	assert_int_equal(stats.dtm_min, 1125000);
	assert_int_equal(stats.dtm_max, 5600000);
	assert(stats.mean - 3250000 < STATS_EPSILON);
	assert(stats.std_dev - 1743290.71012 < STATS_EPSILON);
}

static void
test_histogram_stats(void **state)
{
	uint64_t	val;
	int		rc;

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 0");
	assert(rc == D_TM_SUCCESS);
	assert_int_equal(val, 3);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 1");
	assert(rc == D_TM_SUCCESS);
	assert_int_equal(val, 5);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 2");
	assert(rc == D_TM_SUCCESS);
	assert_int_equal(val, 2);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 3");
	assert(rc == D_TM_SUCCESS);
	assert_int_equal(val, 0);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 4");
	assert(rc == D_TM_SUCCESS);
	assert_int_equal(val, 4);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 5");
	assert(rc == D_TM_SUCCESS);
	assert_int_equal(val, 0);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 6");
	assert(rc == D_TM_SUCCESS);
	assert_int_equal(val, 0);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 7");
	assert(rc == D_TM_SUCCESS);
	assert_int_equal(val, 0);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 8");
	assert(rc == D_TM_SUCCESS);
	assert_int_equal(val, 0);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m1/bucket 9");
	assert(rc == D_TM_SUCCESS);
	assert_int_equal(val, 1);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m2/bucket 0");
	assert(rc == D_TM_SUCCESS);
	assert_int_equal(val, 3);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m2/bucket 1");
	assert(rc == D_TM_SUCCESS);
	assert_int_equal(val, 4);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m2/bucket 2");
	assert(rc == D_TM_SUCCESS);
	assert_int_equal(val, 2);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m2/bucket 3");
	assert(rc == D_TM_SUCCESS);
	assert_int_equal(val, 3);

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/test_gauge_m2/bucket 4");
	assert(rc == D_TM_SUCCESS);
	assert_int_equal(val, 4);
}

static void
test_histogram_metadata(void **state)
{
	char	*metadata;
	int	rc;

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 0");
	assert(rc == D_TM_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 0 [0 .. 4]");
	D_FREE(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 1");
	assert(rc == D_TM_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 1 [5 .. 9]");
	D_FREE(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 2");
	assert(rc == D_TM_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 2 [10 .. 14]");
	D_FREE(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 3");
	assert(rc == D_TM_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 3 [15 .. 19]");
	D_FREE(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 4");
	assert(rc == D_TM_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 4 [20 .. 24]");
	D_FREE(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 5");
	assert(rc == D_TM_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 5 [25 .. 29]");
	D_FREE(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 6");
	assert(rc == D_TM_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 6 [30 .. 34]");
	D_FREE(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 7");
	assert(rc == D_TM_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 7 [35 .. 39]");
	D_FREE(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 8");
	assert(rc == D_TM_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 8 [40 .. 44]");
	D_FREE(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m1/bucket 9");
	assert(rc == D_TM_SUCCESS);
	assert_string_equal(metadata,
			    "histogram bucket 9 [45 .. 18446744073709551615]");
	D_FREE(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m2/bucket 0");
	assert(rc == D_TM_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 0 [0 .. 2047]");
	D_FREE(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m2/bucket 1");
	assert(rc == D_TM_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 1 [2048 .. 4095]");
	D_FREE(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m2/bucket 2");
	assert(rc == D_TM_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 2 [4096 .. 8191]");
	D_FREE(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m2/bucket 3");
	assert(rc == D_TM_SUCCESS);
	assert_string_equal(metadata, "histogram bucket 3 [8192 .. 16383]");
	D_FREE(metadata);

	rc = d_tm_get_metadata(&metadata, NULL, shmem_root, NULL,
			       "gurt/tests/telem/test_gauge_m2/bucket 4");
	assert(rc == D_TM_SUCCESS);
	assert_string_equal(metadata,
			    "histogram bucket 4 [16384 .. "
			    "18446744073709551615]");
	D_FREE(metadata);


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
		cmocka_unit_test(test_shmem_removed),
	};

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("test_gurt_telem_consumer", tests,
					   init_tests, fini_tests);
}
