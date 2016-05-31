/**
 * (C) Copyright 2016 Intel Corporation.
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
 * This file is part of dsm
 *
 * dsm/tests/pool.c
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_mgmt.h>
#include <daos_m.h>
#include <daos_event.h>

typedef struct {
	daos_rank_t		ranks[8];
	daos_rank_list_t	svc;
	uuid_t			uuid;
	daos_handle_t		eq;
	bool			async;
} test_arg_t;

/** connect to non-existing pool */
static void
pool_connect_nonexist(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		 uuid;
	daos_handle_t	 poh;
	int		 rc;

	uuid_generate(uuid);
	rc = dsm_pool_connect(uuid, NULL /* grp */, &arg->svc,
			      DAOS_PC_RW, NULL /* failed */, &poh,
			      NULL /* ev */);
	assert_int_equal(rc, -DER_NONEXIST);
}

/** connect/disconnect to/from a valid pool */
static void
pool_connect(void **state)
{
	test_arg_t	*arg = *state;
	daos_handle_t	 poh;
	daos_event_t	 ev;
	daos_event_t	*evp;
	int		 rc;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	/** connect to pool */
	print_message("connecting to pool %ssynchronously ... ",
		      arg->async ? "a" : "");
	rc = dsm_pool_connect(arg->uuid, NULL /* grp */, &arg->svc,
			      DAOS_PC_RW, NULL /* failed */, &poh,
			      arg->async ? &ev : NULL /* ev */);
	assert_int_equal(rc, 0);

	if (arg->async) {
		/** wait for pool connection */
		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(ev.ev_error, 0);
	}
	print_message("success\n");

	/** disconnect from pool */
	print_message("disconnecting from pool %ssynchronously ... ",
		      arg->async ? "a" : "");
	rc = dsm_pool_disconnect(poh, arg->async ? &ev : NULL /* ev */);
	assert_int_equal(rc, 0);

	if (arg->async) {
		/** wait for pool disconnection */
		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(ev.ev_error, 0);

		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}
	print_message("success\n");
}

static int
async_enable(void **state)
{
	test_arg_t	*arg = *state;

	arg->async = true;
	return 0;
}

static int
async_disable(void **state)
{
	test_arg_t	*arg = *state;

	arg->async = false;
	return 0;
}

static const struct CMUnitTest pool_tests[] = {
	{ "DSM1: connect to non-existing pool",
	  pool_connect_nonexist, NULL, NULL},
	{ "DSM2: connect/disconnect to pool",
	  pool_connect, async_disable, NULL},
	{ "DSM3: connect/disconnect to pool (async)",
	  pool_connect, async_enable, NULL},
};

static int
setup(void **state)
{
	test_arg_t	*arg;
	int		 rc;

	arg = malloc(sizeof(test_arg_t));
	if (arg == NULL)
		return -1;

	rc = daos_eq_create(&arg->eq);
	if (rc)
		return rc;

	arg->svc.rl_nr.num = 8;
	arg->svc.rl_nr.num_out = 0;
	arg->svc.rl_ranks = arg->ranks;

	/** create pool with minimal size */
	rc = dmg_pool_create(0, geteuid(), getegid(), "srv_grp", NULL, "pmem",
			     0, &arg->svc, arg->uuid, NULL);
	if (rc)
		return rc;

	*state = arg;
	return 0;
}

static int
teardown(void **state) {
	test_arg_t	*arg = *state;
	int		 rc;

	rc = dmg_pool_destroy(arg->uuid, "srv_grp", 1, NULL);
	if (rc)
		return rc;

	rc = daos_eq_destroy(arg->eq, 0);
	if (rc)
		return rc;

	free(arg);
	return 0;
}

int
run_pool_test(void)
{
	return cmocka_run_group_tests_name("DSM pool tests", pool_tests,
					   setup, teardown);
}
