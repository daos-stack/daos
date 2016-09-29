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
 * This file is part of daos
 *
 * tests/suite/pool.c
 */

#include "daos_test.h"

/** connect to non-existing pool */
static void
pool_connect_nonexist(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		 uuid;
	daos_handle_t	 poh;
	int		 rc;

	if (arg->myrank != 0)
		return;

	uuid_generate(uuid);
	rc = dsm_pool_connect(uuid, NULL /* grp */, &arg->svc,
			      DAOS_PC_RW, NULL /* failed */, &poh,
			      NULL /* info */, NULL /* ev */);
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
	daos_pool_info_t info;
	int		 rc;

	if (!arg->hdl_share && arg->myrank != 0)
		return;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	if (arg->myrank == 0) {
		/** connect to pool */
		print_message("rank 0 connecting to pool %ssynchronously ... ",
			      arg->async ? "a" : "");
		rc = dsm_pool_connect(arg->pool_uuid, NULL /* grp */, &arg->svc,
				      DAOS_PC_RW, NULL /* failed */, &poh,
				      &info, arg->async ? &ev : NULL /* ev */);
		assert_int_equal(rc, 0);

		if (arg->async) {
			/** wait for pool connection */
			rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
			assert_int_equal(rc, 1);
			assert_ptr_equal(evp, &ev);
			assert_int_equal(ev.ev_error, 0);
		}
		assert_memory_equal(info.pi_uuid, arg->pool_uuid,
				    sizeof(info.pi_uuid));
		/** TODO: assert_int_equal(info.pi_ntargets, arg->...); */
		assert_int_equal(info.pi_ndisabled, 0);
		assert_int_equal(info.pi_mode, arg->mode);
		print_message("success\n");
	}

	if (arg->hdl_share)
		handle_share(&poh, HANDLE_POOL, arg->myrank, poh,
			     HANDLE_SHARE_DSM, 1);

	/** disconnect from pool */
	print_message("rank %d disconnecting from pool %ssynchronously ... ",
		      arg->myrank, arg->async ? "a" : "");
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
		/* disable the async after testing done */
		arg->async = false;
	}
	print_message("rank %d success\n", arg->myrank);
}

static const struct CMUnitTest pool_tests[] = {
	{ "DSM1: connect to non-existing pool",
	  pool_connect_nonexist, NULL, NULL},
	{ "DSM2: connect/disconnect to pool",
	  pool_connect, async_disable, NULL},
	{ "DSM3: connect/disconnect to pool (async)",
	  pool_connect, async_enable, NULL},
	{ "DSM4: pool handle local2global and global2local",
	  pool_connect, hdl_share_enable, NULL},
};

static int
setup(void **state)
{
	test_arg_t	*arg;
	int		 rc = 0;

	arg = malloc(sizeof(test_arg_t));
	if (arg == NULL)
		return -1;

	rc = daos_eq_create(&arg->eq);
	if (rc)
		return rc;

	arg->svc.rl_nr.num = 8;
	arg->svc.rl_nr.num_out = 0;
	arg->svc.rl_ranks = arg->ranks;
	arg->mode = 0731;
	arg->uid = geteuid();
	arg->gid = getegid();
	arg->hdl_share = false;
	uuid_clear(arg->pool_uuid);
	MPI_Comm_rank(MPI_COMM_WORLD, &arg->myrank);
	MPI_Comm_size(MPI_COMM_WORLD, &arg->rank_size);

	if (arg->myrank == 0) {
		/** create pool with minimal size */
		rc = dmg_pool_create(arg->mode, arg->uid, arg->gid, "srv_grp",
				     NULL, "pmem", 0, &arg->svc, arg->pool_uuid,
				     NULL);
		if (rc)
			print_message("dmg_pool_create failed, rc: %d.\n", rc);
	}

	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	MPI_Bcast(arg->pool_uuid, 16, MPI_CHAR, 0, MPI_COMM_WORLD);

	*state = arg;
	return 0;
}

static int
teardown(void **state) {
	test_arg_t	*arg = *state;
	int		 rc;

	MPI_Barrier(MPI_COMM_WORLD);

	if (arg->myrank == 0) {
		rc = dmg_pool_destroy(arg->pool_uuid, "srv_grp", 1, NULL);
		if (rc)
			print_message("dmg_pool_destroy failed, rc: %d.\n", rc);
	}
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	rc = daos_eq_destroy(arg->eq, 0);
	if (rc)
		return rc;

	free(arg);
	return 0;
}

int
run_dsm_pool_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("DSM pool tests", pool_tests,
					 setup, teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
