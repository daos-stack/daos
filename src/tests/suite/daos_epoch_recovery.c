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
 * tests/suite/daos_epoch_recovery.c
 */

#include "daos_test.h"

static void
pool_evict_discard(void **state)
{
	test_arg_t     *arg = *state;
	uuid_t		uuid;
	daos_handle_t	coh;
	daos_epoch_t	epoch;
	int		rc;

	MPI_Barrier(MPI_COMM_WORLD);

	uuid_generate(uuid);
	print_message("creating and opening container "DF_UUIDF"\n",
		      DP_UUID(uuid));
	rc = daos_cont_create(arg->poh, uuid, NULL /* ev */);
	assert_int_equal(rc, 0);
	rc = daos_cont_open(arg->poh, uuid, DAOS_COO_RW, &coh, NULL /* info */,
			    NULL /* ev */);
	assert_int_equal(rc, 0);

	epoch = 1;
	rc = daos_epoch_hold(coh, &epoch, NULL /* state */,
			     NULL /* ev */);
	assert_int_equal(rc, 0);
	assert_int_equal(epoch, 1);

	/* TODO: Every rank updates epoch 1. */

	MPI_Barrier(MPI_COMM_WORLD);

	print_message("evict pool connections, reconnect, and reopen cont\n");
	if (arg->myrank == 0) {
		rc = daos_pool_evict(arg->pool_uuid, "srv_grp", NULL /* ev */);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	rc = daos_cont_close(coh, NULL /* ev */);
	assert_int_equal(rc, 0);
	rc = daos_pool_disconnect(arg->poh, NULL /* ev */);
	assert_int_equal(rc, 0);
	if (arg->myrank == 0) {
		rc = daos_pool_connect(arg->pool_uuid, NULL /* grp */,
				       &arg->svc, DAOS_PC_RW, &arg->poh,
				       NULL /* info */, NULL /* ev */);
		assert_int_equal(rc, 0);
	}
	handle_share(&arg->poh, HANDLE_POOL, arg->myrank, arg->poh, 1);
	rc = daos_cont_open(arg->poh, uuid, DAOS_COO_RW, &coh, NULL /* info */,
			    NULL /* ev */);
	assert_int_equal(rc, 0);

	/* TODO: Every rank verifies that epoch 1 is discarded. */

	rc = daos_cont_close(coh, NULL /* ev */);
	assert_int_equal(rc, 0);
	MPI_Barrier(MPI_COMM_WORLD);
	rc = daos_pool_disconnect(arg->poh, NULL /* ev */);
	assert_int_equal(rc, 0);
}

static const struct CMUnitTest epoch_recovery_tests[] = {
	{"DSM401: pool evict discards uncommitted data",
	 pool_evict_discard, NULL, NULL}
};

static int
setup(void **state)
{
	test_arg_t	*arg;
	int		 rc;

	arg = malloc(sizeof(test_arg_t));
	if (arg == NULL)
		return -1;

	memset(arg, 0, sizeof(*arg));

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
run_daos_epoch_recovery_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("Epoch recovery tests",
					 epoch_recovery_tests, setup, teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
