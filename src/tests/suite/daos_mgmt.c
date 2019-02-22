/**
 * (C) Copyright 2016-2018 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of daos, basic testing for the management API
 */
#define D_LOGFAC	DD_FAC(tests)
#include "daos_test.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_mgmt.h>
#include <daos_event.h>

/** create/destroy pool on all tgts */
static void
pool_create_all(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		 uuid;
	char		 uuid_str[64];
	daos_event_t	 ev;
	daos_event_t	*evp;
	int		 rc;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}


	/** create container */
	print_message("creating pool %ssynchronously ... ",
		      arg->async ? "a" : "");
	rc = daos_pool_create(0700 /* mode */, 0 /* uid */, 0 /* gid */,
			      arg->group, NULL /* tgts */, "pmem" /* dev */,
			      0 /* minimal size */, 0 /* nvme size */,
			      NULL /* properties */, &arg->pool.svc /* svc */,
			      uuid, arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		/** wait for container creation */
		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(ev.ev_error, 0);
	}

	uuid_unparse_lower(uuid, uuid_str);
	print_message("success uuid = %s\n", uuid_str);

	/** destroy container */
	print_message("destroying pool %ssynchronously ... ",
		      arg->async ? "a" : "");
	rc = daos_pool_destroy(uuid, arg->group, 1, arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		/** for container destroy */
		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(ev.ev_error, 0);

		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}
	print_message("success\n");
}

static const struct CMUnitTest tests[] = {
	{ "MGMT1: create/destroy pool on all tgts",
	  pool_create_all, async_disable, test_case_teardown},
	{ "MGMT2: create/destroy pool on all tgts (async)",
	  pool_create_all, async_enable, test_case_teardown},
};

static int
setup(void **state)
{
	return test_setup(state, SETUP_EQ, false, DEFAULT_POOL_SIZE, NULL);
}

int
run_daos_mgmt_test(int rank, int size)
{
	int	rc;

	if (rank == 0)
		rc = cmocka_run_group_tests_name("Management tests", tests,
						 setup, test_teardown);

	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	return rc;
}
