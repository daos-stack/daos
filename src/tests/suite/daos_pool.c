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

#define DD_SUBSYS	DD_FAC(tests)

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
	rc = daos_pool_connect(uuid, arg->group, &arg->svc, DAOS_PC_RW, &poh,
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
		rc = daos_pool_connect(arg->pool_uuid, arg->group, &arg->svc,
				       DAOS_PC_RW, &poh, &info,
				      arg->async ? &ev : NULL /* ev */);
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

		print_message("rank 0 querying pool info... ");
		memset(&info, 'D', sizeof(info));
		rc = daos_pool_query(poh, NULL /* tgts */, &info,
				     arg->async ? &ev : NULL /* ev */);
		assert_int_equal(rc, 0);

		if (arg->async) {
			/** wait for pool query */
			rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
			assert_int_equal(rc, 1);
			assert_ptr_equal(evp, &ev);
			assert_int_equal(ev.ev_error, 0);
		}

		assert_int_equal(info.pi_ndisabled, 0);
		print_message("success\n");
	}

	if (arg->hdl_share)
		handle_share(&poh, HANDLE_POOL, arg->myrank, poh, 1);

	/** disconnect from pool */
	print_message("rank %d disconnecting from pool %ssynchronously ... ",
		      arg->myrank, arg->async ? "a" : "");
	rc = daos_pool_disconnect(poh, arg->async ? &ev : NULL /* ev */);
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

/** connect exclusively to a pool */
static void
pool_connect_exclusively(void **state)
{
	test_arg_t	*arg = *state;
	daos_handle_t	 poh;
	daos_handle_t	 poh_ex;
	int		 rc;

	if (arg->myrank != 0)
		return;

	print_message("SUBTEST 1: other connections already exist; shall get "
		      "%d\n", -DER_BUSY);
	print_message("establishing a non-exclusive connection\n");
	rc = daos_pool_connect(arg->pool_uuid, arg->group, &arg->svc,
			       DAOS_PC_RW, &poh, NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);
	print_message("trying to establish an exclusive connection\n");
	rc = daos_pool_connect(arg->pool_uuid, arg->group, &arg->svc,
			       DAOS_PC_EX, &poh_ex, NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, -DER_BUSY);
	print_message("disconnecting the non-exclusive connection\n");
	rc = daos_pool_disconnect(poh, NULL /* ev */);
	assert_int_equal(rc, 0);

	print_message("SUBTEST 2: no other connections; shall succeed\n");
	print_message("establishing an exclusive connection\n");
	rc = daos_pool_connect(arg->pool_uuid, arg->group, &arg->svc,
			       DAOS_PC_EX, &poh_ex, NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	print_message("SUBTEST 3: shall prevent other connections (%d)\n",
		      -DER_BUSY);
	print_message("trying to establish a non-exclusive connection\n");
	rc = daos_pool_connect(arg->pool_uuid, arg->group, &arg->svc,
			       DAOS_PC_RW, &poh, NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, -DER_BUSY);
	print_message("disconnecting the exclusive connection\n");
	rc = daos_pool_disconnect(poh_ex, NULL /* ev */);
	assert_int_equal(rc, 0);
}

/** exclude a target from the pool */
static void
pool_exclude(void **state)
{
	test_arg_t	*arg = *state;
	daos_handle_t	 poh;
	daos_event_t	 ev;
	daos_event_t	*evp;
	daos_pool_info_t info;
	daos_rank_list_t ranks;
	daos_rank_t	 rank;
	int		 rc;

	if (arg->myrank != 0)
		return;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	/** connect to pool */
	print_message("rank 0 connecting to pool %ssynchronously... ",
		      arg->async ? "a" : "");
	rc = daos_pool_connect(arg->pool_uuid, arg->group, &arg->svc,
			       DAOS_PC_RW, &poh, &info,
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

	/** exclude last non-svc rank */
	if (info.pi_ntargets - 1 /* rank 0 */ <= arg->svc.rl_nr.num) {
		print_message("not enough non-svc targets; skipping\n");
		goto disconnect;
	}
	rank = info.pi_ntargets - 1;
	ranks.rl_nr.num = 1;
	ranks.rl_nr.num_out = ranks.rl_nr.num;
	ranks.rl_ranks = &rank;

	print_message("rank 0 excluding rank %u... ", rank);
	rc = daos_pool_exclude(arg->pool_uuid, arg->group, &arg->svc, &ranks,
			       arg->async ? &ev : NULL /* ev */);
	assert_int_equal(rc, 0);

	if (arg->async) {
		/** wait for pool exclusion */
		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(ev.ev_error, 0);
	}

	print_message("success\n");

	print_message("rank 0 querying pool info... ");
	memset(&info, 'D', sizeof(info));
	rc = daos_pool_query(poh, NULL /* tgts */, &info,
			     arg->async ? &ev : NULL /* ev */);
	assert_int_equal(rc, 0);

	if (arg->async) {
		/** wait for pool query */
		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(ev.ev_error, 0);
	}

	assert_int_equal(info.pi_ndisabled, 1);
	print_message("success\n");

disconnect:
	/** disconnect from pool */
	print_message("rank %d disconnecting from pool %ssynchronously ... ",
		      arg->myrank, arg->async ? "a" : "");
	rc = daos_pool_disconnect(poh, arg->async ? &ev : NULL /* ev */);
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
	{ "POOL1: connect to non-existing pool",
	  pool_connect_nonexist, NULL, NULL},
	{ "POOL2: connect/disconnect to pool",
	  pool_connect, async_disable, NULL},
	{ "POOL3: connect/disconnect to pool (async)",
	  pool_connect, async_enable, NULL},
	{ "POOL4: pool handle local2global and global2local",
	  pool_connect, hdl_share_enable, NULL},
	{ "POOL5: exclusive connection",
	  pool_connect_exclusively, NULL, NULL},
	/* Keep this one at the end, as it excludes target rank 1. */
	{ "POOL6: exclude targets and query pool info",
	  pool_exclude, async_disable, NULL}
};

static int
setup(void **state)
{
	return test_setup(state, SETUP_POOL_CREATE, true);
}

int
run_daos_pool_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("Pool tests", pool_tests,
					 setup, test_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
