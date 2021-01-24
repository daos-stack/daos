/*
 * (C) Copyright 2020-2021 Intel Corporation.
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
 * This file tests telemetry consumption in GURT.
 * It is tightly coupled to the telemetry production test application
 * that must be run first.  That application generates the metrics that are
 * read and examined by the tests here.
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include "wrap_cmocka.h"
#include "gurt/telemetry_common.h"
#include "gurt/telemetry_consumer.h"

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
	assert(num == 2);

	num = d_tm_count_metrics(shmem_root, node, D_TM_GAUGE);
	assert(num == 1);

	num = d_tm_count_metrics(shmem_root, node, D_TM_DURATION);
	assert(num == 1);

	num = d_tm_count_metrics(shmem_root, node, D_TM_TIMESTAMP);
	assert(num == 1);

	num = d_tm_count_metrics(shmem_root, node, D_TM_TIMER_SNAPSHOT);
	assert(num == 2);

	num = d_tm_count_metrics(shmem_root, node,
				 D_TM_COUNTER | D_TM_GAUGE | D_TM_DURATION |
				 D_TM_TIMESTAMP | D_TM_TIMER_SNAPSHOT);
	assert(num == 7);
}

static void
test_verify_loop_counter(void **state)
{
	uint64_t	val;
	int		rc;

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/loop counter");
	assert(rc == D_TM_SUCCESS);

	assert(val == 5000);
}

static void
test_verify_test_counter(void **state)
{
	uint64_t	val;
	int		rc;

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/counter 1");
	assert(rc == D_TM_SUCCESS);
	assert(val == 3);
}

static void
test_metric_not_found(void **state)
{
	uint64_t	val = 0;
	int		rc;

	rc = d_tm_get_counter(&val, shmem_root, NULL,
			      "gurt/tests/telem/this doesn't exist");
	assert(rc == -DER_METRIC_NOT_FOUND);
	assert(val == 0);
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
	uint64_t	val;
	int		rc;

	rc = d_tm_get_gauge(&val, shmem_root, NULL,
			    "gurt/tests/telem/gauge");
	assert(rc == D_TM_SUCCESS);

	assert(val == 1650);
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
	};

	d_register_alt_assert(mock_assert);

	return cmocka_run_group_tests_name("test_gurt_telem_consumer", tests,
					   init_tests, fini_tests);
}
