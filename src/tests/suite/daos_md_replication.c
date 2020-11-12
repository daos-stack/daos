/**
 * (C) Copyright 2017-2020 Intel Corporation.
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
 * DAOS Metadata Replication Tests
 */
#define D_LOGFAC	DD_FAC(tests)

#include <daos/pool.h>
#include "daos_test.h"

static void
mdr_stop_pool_svc(void **argv)
{
	test_arg_t	       *arg = *argv;
	uuid_t			uuid;
	daos_handle_t		poh;
	daos_pool_info_t	info = {0};
	bool			skip = false;
	int			rc;

	/* Create the pool. */
	if (arg->myrank == 0) {
		print_message("creating pool\n");
		rc = dmg_pool_create(dmg_config_file,
				     geteuid(), getegid(), arg->group,
				     NULL, 128 * 1024 * 1024, 0,
				     NULL, arg->pool.svc, uuid);
	}
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	assert_int_equal(rc, 0);
	MPI_Bcast(uuid, 16, MPI_CHAR, 0, MPI_COMM_WORLD);
	MPI_Bcast(&arg->pool.svc->rl_nr, sizeof(arg->pool.svc->rl_nr), MPI_CHAR,
		  0, MPI_COMM_WORLD);
	MPI_Bcast(arg->pool.ranks,
		  sizeof(arg->pool.ranks[0]) * arg->pool.svc->rl_nr,
		  MPI_CHAR, 0, MPI_COMM_WORLD);

	/* Check the number of pool service replicas. */
	if (arg->pool.svc->rl_nr < 3) {
		if (arg->myrank == 0)
			print_message(">= 3 pool service replicas needed; ");
		skip = true;
		goto destroy;
	}

	/* Connect to the pool. */
	if (arg->myrank == 0) {
		print_message("connecting to pool\n");
		rc = daos_pool_connect(uuid, arg->group, arg->pool.svc,
				       DAOS_PC_RW, &poh, NULL /* info */,
				       NULL /* ev */);
	}
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	assert_int_equal(rc, 0);
	handle_share(&poh, HANDLE_POOL, arg->myrank, DAOS_HDL_INVAL, 0);

	if (arg->myrank == 0) {
		/* Let other ranks get started. */
		sleep(1);

		print_message("stopping pool service leader\n");
		rc = daos_pool_stop_svc(poh, NULL /* ev */);
		assert_int_equal(rc, 0);

		/* Verify the connection is still usable. */
		print_message("querying pool info\n");
		memset(&info, 'D', sizeof(info));
		rc = daos_pool_query(poh, NULL /* tgts */, &info,
				     NULL /* properties */,
				     NULL /* ev */);
		assert_int_equal(rc, 0);
	} else {
		int n = 10000 / arg->rank_size;
		int i;

		/* Generate some concurrent requests. */
		print_message("repeating %d queries: begin\n", n);
		for (i = 0; i < n; i++) {
			memset(&info, 'D', sizeof(info));
			rc = daos_pool_query(poh, NULL /* tgts */, &info,
					     NULL /* properties */,
					     NULL /* ev */);
			assert_int_equal(rc, 0);
		}
		print_message("repeating %d queries: end\n", n);
	}

	MPI_Barrier(MPI_COMM_WORLD);

	print_message("disconnecting from pool\n");
	rc = daos_pool_disconnect(poh, NULL /* ev */);
	assert_int_equal(rc, 0);

destroy:
	if (arg->myrank == 0) {
		if (skip)
			print_message("skipping\n");
		print_message("destroying pool\n");
		rc = dmg_pool_destroy(dmg_config_file,
				      uuid, arg->group, 1);
		assert_int_equal(rc, 0);
	}
	if (skip)
		skip();
}

static void
mdr_stop_cont_svc(void **argv)
{
	test_arg_t	       *arg = *argv;
	uuid_t			pool_uuid;
	uuid_t			uuid;
	daos_handle_t		poh;
	daos_handle_t		coh;
	bool			skip = false;
	int			rc;

	print_message("creating pool\n");
	rc = dmg_pool_create(dmg_config_file,
			     geteuid(), getegid(), arg->group,
			     NULL, 128 * 1024 * 1024, 0,
			     NULL, arg->pool.svc, pool_uuid);
	assert_int_equal(rc, 0);

	if (arg->pool.svc->rl_nr < 3) {
		if (arg->myrank == 0)
			print_message(">= 3 pool service replicas needed; ");
		skip = true;
		goto destroy;
	}

	print_message("connecting to pool\n");
	rc = daos_pool_connect(pool_uuid, arg->group, arg->pool.svc,
			       DAOS_PC_RW, &poh, NULL, NULL /* ev */);
	assert_int_equal(rc, 0);
	print_message("creating container\n");
	uuid_generate(uuid);
	rc = daos_cont_create(poh, uuid, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("opening container\n");
	rc = daos_cont_open(poh, uuid, DAOS_COO_RW, &coh, NULL, NULL);
	assert_int_equal(rc, 0);

	print_message("stopping container service leader\n");
	/* Currently, the pool and container services are combined. */
	rc = daos_pool_stop_svc(poh, NULL /* ev */);
	assert_int_equal(rc, 0);

	print_message("closing container\n");
	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);
	print_message("destroying container\n");
	rc = daos_cont_destroy(poh, uuid, 1 /* force */, NULL);
	assert_int_equal(rc, 0);
	print_message("disconnecting from pool\n");
	rc = daos_pool_disconnect(poh, NULL /* ev */);
	assert_int_equal(rc, 0);

destroy:
	if (arg->myrank == 0) {
		if (skip)
			print_message("skipping\n");
		print_message("destroying pool\n");
		rc = dmg_pool_destroy(dmg_config_file,
				      pool_uuid, arg->group, 1);
		assert_int_equal(rc, 0);
	}
	if (skip)
		skip();
}

static const struct CMUnitTest mdr_tests[] = {
	{ "MDR1: stop pool service leader",
	  mdr_stop_pool_svc, NULL, test_case_teardown},
	{ "MDR2: stop container service leader",
	  mdr_stop_cont_svc, NULL, test_case_teardown},
};

static int
setup(void **state)
{
	return test_setup(state, SETUP_EQ, false, DEFAULT_POOL_SIZE, 0, NULL);
}

int
run_daos_md_replication_test(int rank, int size)
{
	int rc;

	rc = cmocka_run_group_tests_name("DAOS MD repliation tests", mdr_tests,
					 setup, test_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
