/**
 * (C) Copyright 2017 Intel Corporation.
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

#define DD_SUBSYS	DD_FAC(tests)

#include <daos/pool.h>
#include "daos_test.h"

static void
mdr_stop_pool_svc(void **argv)
{
	test_arg_t	       *arg = *argv;
	uuid_t			uuid;
	daos_handle_t		poh;
	daos_pool_info_t	info;
	int			rc;

	print_message("creating pool\n");
	rc = daos_pool_create(0731, geteuid(), getegid(), arg->group, NULL,
			      "pmem", 128*1024*1024, &arg->svc, uuid, NULL);
	assert_int_equal(rc, 0);
	arg->svc.rl_nr.num = arg->svc.rl_nr.num_out;

	if (arg->svc.rl_nr.num < 3) {
		print_message(">= 3 pool service replicas needed; skipping\n");
		goto destroy;
	}

	print_message("connecting to pool\n");
	rc = daos_pool_connect(uuid, arg->group, &arg->svc,
			       DAOS_PC_RW, &poh, &info, NULL /* ev */);
	assert_int_equal(rc, 0);

	print_message("stopping pool service leader\n");
	rc = daos_pool_svc_stop(poh, NULL /* ev */);
	assert_int_equal(rc, 0);

	/* Verify the connection is still usable. */
	print_message("querying pool info\n");
	memset(&info, 'D', sizeof(info));
	rc = daos_pool_query(poh, NULL /* tgts */, &info, NULL /* ev */);
	assert_int_equal(rc, 0);

	print_message("disconnecting from pool\n");
	rc = daos_pool_disconnect(poh, NULL /* ev */);
	assert_int_equal(rc, 0);

destroy:
	print_message("destroying pool\n");
	rc = daos_pool_destroy(uuid, arg->group, 1, NULL);
	assert_int_equal(rc, 0);
}

static void
mdr_stop_cont_svc(void **argv)
{
	test_arg_t	       *arg = *argv;
	uuid_t			pool_uuid;
	uuid_t			uuid;
	daos_handle_t		poh;
	daos_handle_t		coh;
	daos_epoch_state_t	state;
	daos_epoch_t		epoch;
	int			rc;

	print_message("creating pool\n");
	rc = daos_pool_create(0731, geteuid(), getegid(), arg->group, NULL,
			      "pmem", 128*1024*1024, &arg->svc, pool_uuid,
			      NULL);
	assert_int_equal(rc, 0);
	arg->svc.rl_nr.num = arg->svc.rl_nr.num_out;

	if (arg->svc.rl_nr.num < 3) {
		print_message(">= 3 pool service replicas needed; skipping\n");
		goto destroy;
	}

	print_message("connecting to pool\n");
	rc = daos_pool_connect(pool_uuid, arg->group, &arg->svc,
			       DAOS_PC_RW, &poh, NULL, NULL /* ev */);
	assert_int_equal(rc, 0);
	print_message("creating container\n");
	uuid_generate(uuid);
	rc = daos_cont_create(poh, uuid, NULL);
	assert_int_equal(rc, 0);
	print_message("opening container\n");
	rc = daos_cont_open(poh, uuid, DAOS_COO_RW, &coh, NULL, NULL);
	assert_int_equal(rc, 0);

	epoch = 31415926;
	print_message("holding epoch "DF_U64"\n", epoch);
	rc = daos_epoch_hold(coh, &epoch, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("committing epoch "DF_U64"\n", epoch);
	rc = daos_epoch_commit(coh, epoch, NULL, NULL);
	assert_int_equal(rc, 0);

	print_message("stopping container service leader\n");
	/* Currently, the pool and container services are combined. */
	rc = daos_pool_svc_stop(poh, NULL /* ev */);
	assert_int_equal(rc, 0);

	print_message("verifying epoch state\n");
	rc = daos_epoch_query(coh, &state, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(state.es_hce, epoch);

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
	print_message("destroying pool\n");
	rc = daos_pool_destroy(pool_uuid, arg->group, 1, NULL);
	assert_int_equal(rc, 0);
}

static const struct CMUnitTest mdr_tests[] = {
	{ "MDR1: stop pool service leader",
	  mdr_stop_pool_svc, NULL, NULL},
	{ "MDR2: stop container service leader",
	  mdr_stop_cont_svc, NULL, NULL},
};

static int
setup(void **state)
{
	return test_setup(state, SETUP_EQ, false);
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
