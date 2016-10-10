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
 * tests/suite/container.c
 */

#include "daos_test.h"

/** create/destroy container */
static void
co_create(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		 uuid;
	daos_handle_t	 coh;
	daos_cont_info_t info;
	daos_event_t	 ev;
	daos_event_t	*evp;
	int		 rc;

	if (!arg->hdl_share && arg->myrank != 0)
		return;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	/** container uuid */
	uuid_generate(uuid);

	/** create container */
	if (arg->myrank == 0) {
		print_message("creating container %ssynchronously ...\n",
			      arg->async ? "a" : "");
		rc = daos_cont_create(arg->poh, uuid, arg->async ? &ev : NULL);
		assert_int_equal(rc, 0);

		if (arg->async) {
			/** wait for container creation */
			rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
			assert_int_equal(rc, 1);
			assert_ptr_equal(evp, &ev);
			assert_int_equal(ev.ev_error, 0);
		}
		print_message("container created\n");

		print_message("opening container %ssynchronously\n",
			      arg->async ? "a" : "");
		rc = daos_cont_open(arg->poh, uuid, DAOS_COO_RW, &coh, &info,
				 arg->async ? &ev : NULL);
		assert_int_equal(rc, 0);

		if (arg->async) {
			/** wait for container open */
			rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
			assert_int_equal(rc, 1);
			assert_ptr_equal(evp, &ev);
			assert_int_equal(ev.ev_error, 0);
		}
		print_message("contained opened\n");

		print_message("container info:\n");
		print_message("  hce: "DF_U64"\n", info.ci_epoch_state.es_hce);
		print_message("  lre: "DF_U64"\n", info.ci_epoch_state.es_lre);
		print_message("  lhe: "DF_U64"\n", info.ci_epoch_state.es_lhe);
		print_message("  ghce: "DF_U64"\n",
			      info.ci_epoch_state.es_ghce);
		print_message("  glre: "DF_U64"\n",
			      info.ci_epoch_state.es_glre);
		print_message("  ghpce: "DF_U64"\n",
			      info.ci_epoch_state.es_ghpce);
	}

	if (arg->hdl_share)
		handle_share(&coh, HANDLE_CO, arg->myrank, arg->poh, 1);

	print_message("closing container %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_cont_close(coh, arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		/** wait for container close */
		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(ev.ev_error, 0);
	}
	print_message("container closed\n");

	if (arg->hdl_share)
		MPI_Barrier(MPI_COMM_WORLD);

	/** destroy container */
	if (arg->myrank == 0) {
		print_message("destroying container %ssynchronously ...\n",
			      arg->async ? "a" : "");
		rc = daos_cont_destroy(arg->poh, uuid, 1 /* force */,
				    arg->async ? &ev : NULL);
		assert_int_equal(rc, 0);

		if (arg->async) {
			/** wait for container destroy */
			rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
			assert_int_equal(rc, 1);
			assert_ptr_equal(evp, &ev);
			assert_int_equal(ev.ev_error, 0);

			rc = daos_event_fini(&ev);
			assert_int_equal(rc, 0);
		}
		print_message("container destroyed\n");
	}
}

static const struct CMUnitTest co_tests[] = {
	{ "DSM100: create/open/close/destroy container",
	  co_create, async_disable, NULL},
	{ "DSM101: create/open/close/destroy container (async)",
	  co_create, async_enable, NULL},
	{ "DSM102: container handle local2glocal and global2local",
	  co_create, hdl_share_enable, NULL},
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

	arg->hdl_share = false;
	uuid_clear(arg->pool_uuid);
	MPI_Comm_rank(MPI_COMM_WORLD, &arg->myrank);
	MPI_Comm_size(MPI_COMM_WORLD, &arg->rank_size);

	if (arg->myrank == 0) {
		/** create pool with minimal size */
		rc = daos_pool_create(0731, geteuid(), getegid(), "srv_grp",
				      NULL, "pmem", 0, &arg->svc,
				      arg->pool_uuid, NULL);
	}

	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** connect to pool */
	if (arg->myrank == 0) {
		rc = daos_pool_connect(arg->pool_uuid, NULL /* grp */,
				       &arg->svc, DAOS_PC_RW, &arg->poh,
				       NULL /* info */,
				       NULL /* ev */);
	}
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** l2g and g2l the pool handle */
	handle_share(&arg->poh, HANDLE_POOL, arg->myrank, arg->poh, 1);

	*state = arg;
	return 0;
}

static int
teardown(void **state) {
	test_arg_t	*arg = *state;
	int		 rc;

	MPI_Barrier(MPI_COMM_WORLD);

	rc = daos_pool_disconnect(arg->poh, NULL /* ev */);
	if (rc)
		return rc;

	if (arg->myrank == 0)
		rc = daos_pool_destroy(arg->pool_uuid, "srv_grp", 1, NULL);

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
run_daos_cont_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("DSM container tests",
					 co_tests, setup, teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
