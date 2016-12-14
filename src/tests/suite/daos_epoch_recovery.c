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

#define DD_SUBSYS	DD_FAC(tests)
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
}

static const struct CMUnitTest epoch_recovery_tests[] = {
	{"ERECOV1: pool evict discards uncommitted data",
	 pool_evict_discard, NULL, NULL}
};

static int
setup(void **state)
{
	return test_setup(state, SETUP_POOL_CONNECT, true);
}

int
run_daos_epoch_recovery_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("Epoch recovery tests",
					 epoch_recovery_tests, setup,
					 test_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
