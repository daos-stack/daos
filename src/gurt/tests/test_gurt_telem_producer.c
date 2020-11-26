/*
 * (C) Copyright 2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/*
 * This file tests telemetry production in GURT
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include "wrap_cmocka.h"
#include "gurt/common.h"
#include "gurt/telemetry_common.h"
#include "gurt/telemetry_producer.h"

static int
init_tests(void **state)
{
	int		simulated_rank = 99;
	int		rc;

	rc = d_tm_init(simulated_rank, D_TM_SHARED_MEMORY_SIZE);
	assert_true(rc == D_TM_SUCCESS);

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
		rc = d_tm_increment_counter(&loop, "gurt", "tests", "telem",
					    "loop counter", NULL);
		assert(rc == D_TM_SUCCESS);
	}

	/**
	 * Use the pointer without the name provided to show that it still
	 * increments the loop counter.
	 */
	rc = d_tm_increment_counter(&loop, NULL);
	assert(rc == D_TM_SUCCESS);
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

	rc = d_tm_set_gauge(&gauge, init_val, "gurt", "tests", "telem",
			    "gauge", NULL);
	assert(rc == D_TM_SUCCESS);

	for (i = 0; i < inc_count; i++) {
		rc = d_tm_increment_gauge(&gauge, 1, "gurt", "tests", "telem",
					  "gauge", NULL);
		assert(rc == D_TM_SUCCESS);
	}

	for (i = 0; i < dec_count; i++) {
		rc = d_tm_decrement_gauge(&gauge, 1, "gurt", "tests", "telem",
					  "gauge", NULL);
		assert(rc == D_TM_SUCCESS);
	}

}

static void
test_record_timestamp(void **state)
{
	static struct d_tm_node_t	*ts;
	int				rc;

	rc = d_tm_record_timestamp(&ts, "gurt", "tests", "telem",
				   "last executed", NULL);
	assert(rc == D_TM_SUCCESS);
}

static void
test_interval_timer(void **state)
{
	static struct d_tm_node_t	*timer;
	struct timespec			ts;
	int				rc;

	rc = d_tm_mark_duration_start(&timer, D_TM_CLOCK_REALTIME,
				      "gurt", "tests", "telem", "interval",
				      NULL);

	assert(rc == D_TM_SUCCESS);

	ts.tv_sec = 0;
	ts.tv_nsec = 50000000;
	nanosleep(&ts, NULL);

	rc = d_tm_mark_duration_end(&timer, NULL);
	assert(rc == D_TM_SUCCESS);
}

static void
test_timer_snapshot_sample_1(void **state)
{
	static struct d_tm_node_t	*snapshot;
	int				rc;

	rc = d_tm_take_timer_snapshot(&snapshot, D_TM_CLOCK_REALTIME,
				      "gurt", "tests", "telem",
				      "snapshot sample 1", NULL);
	assert(rc == D_TM_SUCCESS);
}

static void
test_timer_snapshot_sample_2(void **state)
{
	static struct d_tm_node_t	*snapshot;
	int				rc;

	rc = d_tm_take_timer_snapshot(&snapshot, D_TM_CLOCK_REALTIME,
				      "gurt", "tests", "telem",
				      "snapshot sample 2", NULL);
	assert(rc == D_TM_SUCCESS);
}

static void
test_input_validation(void **state)
{
	static struct d_tm_node_t	*node;
	static struct d_tm_node_t	*temp;
	char				*path;
	int				rc;
	int				i;

	/** uninitialized node ptr at initialization time */
	rc = d_tm_increment_counter(&node, "gurt", "tests", "telem",
				    "counter 1", NULL);
	assert(rc == D_TM_SUCCESS);

	/** Use the initialized node without specifying a name */
	rc = d_tm_increment_counter(&node, NULL);
	assert(rc == D_TM_SUCCESS);

	/** Provide a NULL node pointer, force the API to use the name */
	rc = d_tm_increment_counter(NULL, "gurt", "tests", "telem",
				    "counter 1", NULL);
	assert(rc == D_TM_SUCCESS);

	/** Verify correct function associated with this metric type is used */
	printf("This operation is expected to generate an error:\n");
	rc = d_tm_increment_gauge(NULL, 1, "gurt", "tests", "telem",
				  "counter 1", NULL);
	assert(rc == -DER_OP_NOT_PERMITTED);

	/** Verify correct function associated with this metric type is used */
	printf("This operation is expected to generate an error:\n");
	rc = d_tm_increment_gauge(&node, 1, NULL);
	assert(rc == -DER_OP_NOT_PERMITTED);

	/** Specifying a null pointer and no path should fail */
	rc = d_tm_increment_counter(NULL, NULL);
	assert(rc == -DER_INVAL);

	/** Specifying a null pointer and no path should fail */
	rc = d_tm_increment_counter(&temp, NULL);
	assert(rc == -DER_INVAL);

	/**
	 * The API accepts a path length that is D_TM_MAX_NAME_LEN including
	 * the null terminator.  Any path exceeding that generates an error.
	 * Intentionally fills path with D_TM_MAX_NAME_LEN characters
	 * and NULL terminates to create a path length that is 1 too large.
	 */
	D_ALLOC(path, D_TM_MAX_NAME_LEN + 1);
	assert_non_null(path);
	for (i = 0; i < D_TM_MAX_NAME_LEN; i++)
		path[i] = '0' + i % 10;
	path[D_TM_MAX_NAME_LEN] = 0;
	rc = d_tm_increment_counter(NULL, path, NULL);
	assert(rc == -DER_EXCEEDS_PATH_LEN);

	/** Now trim the path by 1 character to make it fit */
	path[D_TM_MAX_NAME_LEN - 1] = 0;
	rc = d_tm_increment_counter(NULL, path, NULL);
	assert(rc == D_TM_SUCCESS);

	/**
	 * After using "root" + "/", size the buffer 1 character too large
	 * Note that the "/" is added by the API when building the full path.
	 */
	path[D_TM_MAX_NAME_LEN - 5]  = 0;
	rc = d_tm_increment_counter(NULL, "root", path, NULL);
	assert(rc == -DER_EXCEEDS_PATH_LEN);

	/**
	 * After using "root" + "/", size the buffer correctly so it just fits.
	 * Note that the "/" is added by the API when building the full path.
	 */
	path[D_TM_MAX_NAME_LEN - 6] = 0;
	rc = d_tm_increment_counter(NULL, "root", path, NULL);
	assert(rc == D_TM_SUCCESS);
	D_FREE_PTR(path);
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
		cmocka_unit_test(test_timer_snapshot_sample_2),
		cmocka_unit_test(test_gauge),
		cmocka_unit_test(test_record_timestamp),
		cmocka_unit_test(test_interval_timer),
		cmocka_unit_test(test_input_validation),
	};

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("test_gurt_telem_producer", tests,
					   init_tests, fini_tests);
}
