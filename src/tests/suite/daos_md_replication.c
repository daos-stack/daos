/**
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
				     NULL, 256 * 1024 * 1024, 0,
				     NULL, arg->pool.svc, uuid);
	}
	par_bcast(PAR_COMM_WORLD, &rc, 1, PAR_INT, 0);
	assert_rc_equal(rc, 0);
	par_bcast(PAR_COMM_WORLD, uuid, 16, PAR_CHAR, 0);
	par_bcast(PAR_COMM_WORLD, &arg->pool.svc->rl_nr, sizeof(arg->pool.svc->rl_nr), PAR_CHAR, 0);
	par_bcast(PAR_COMM_WORLD, arg->pool.ranks,
		  sizeof(arg->pool.ranks[0]) * arg->pool.svc->rl_nr, PAR_CHAR, 0);

	/* Check the number of pool service replicas. */
	if (arg->pool.svc->rl_nr < 3) {
		if (arg->myrank == 0)
			print_message(">= 3 pool service replicas needed; ");
		skip = true;
		goto destroy;
	}

	/* Connect to the pool. */
	if (arg->myrank == 0) {
		char	str[37];

		uuid_unparse(uuid, str);
		print_message("connecting to pool\n");
		rc = daos_pool_connect(str, arg->group,
				       DAOS_PC_RW, &poh, NULL /* info */,
				       NULL /* ev */);
	}
	par_bcast(PAR_COMM_WORLD, &rc, 1, PAR_INT, 0);
	assert_rc_equal(rc, 0);
	handle_share(&poh, HANDLE_POOL, arg->myrank, DAOS_HDL_INVAL, 0);

	if (arg->myrank == 0) {
		/* Let other ranks get started. */
		sleep(1);

		print_message("stopping pool service leader\n");
		rc = daos_pool_stop_svc(poh, NULL /* ev */);
		assert_rc_equal(rc, 0);

		/* Verify the connection is still usable. */
		print_message("querying pool info\n");
		memset(&info, 'D', sizeof(info));
		rc = daos_pool_query(poh, NULL /* tgts */, &info,
				     NULL /* properties */,
				     NULL /* ev */);
		assert_rc_equal(rc, 0);
	} else {
		int n = 10000 / arg->rank_size;
		int i;
		uint64_t t_start, t_end, duration = 0;

		t_start = daos_get_ntime();
		/* Generate some concurrent requests. */
		print_message("repeating %d queries: begin\n", n);
		for (i = 0; i < n; i++) {
			memset(&info, 'D', sizeof(info));
			rc = daos_pool_query(poh, NULL /* tgts */, &info,
					     NULL /* properties */,
					     NULL /* ev */);
			assert_rc_equal(rc, 0);
			if (i % 10 == 0) {
				t_end = daos_get_ntime();
				duration = (t_end - t_start) / NSEC_PER_SEC;
				if (duration >= 15)
					break;
			}
		}
		if (duration == 0) {
			t_end = daos_get_ntime();
			duration = (t_end - t_start) / NSEC_PER_SEC;
		}
		print_message("repeating %d queries, duration: %lu seconds end\n",
			      i, duration);
	}

	par_barrier(PAR_COMM_WORLD);

	print_message("disconnecting from pool\n");
	rc = daos_pool_disconnect(poh, NULL /* ev */);
	assert_rc_equal(rc, 0);

destroy:
	if (arg->myrank == 0) {
		if (skip)
			print_message("skipping\n");
		print_message("destroying pool\n");
		rc = dmg_pool_destroy(dmg_config_file,
				      uuid, arg->group, 1);
		assert_rc_equal(rc, 0);
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
	char			str[37];
	bool			skip = false;
	int			rc;

	print_message("creating pool\n");
	rc = dmg_pool_create(dmg_config_file,
			     geteuid(), getegid(), arg->group,
			     NULL, 256 * 1024 * 1024, 0,
			     NULL, arg->pool.svc, pool_uuid);
	assert_rc_equal(rc, 0);

	if (arg->pool.svc->rl_nr < 3) {
		if (arg->myrank == 0)
			print_message(">= 3 pool service replicas needed; ");
		skip = true;
		goto destroy;
	}

	print_message("connecting to pool\n");
	uuid_unparse(pool_uuid, str);
	rc = daos_pool_connect(str, arg->group,
			       DAOS_PC_RW, &poh, NULL, NULL /* ev */);
	assert_rc_equal(rc, 0);
	print_message("creating container\n");
	rc = daos_cont_create(poh, &uuid, NULL, NULL);
	assert_rc_equal(rc, 0);
	print_message("opening container\n");
	uuid_unparse(uuid, str);
	rc = daos_cont_open(poh, str, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	print_message("stopping container service leader\n");
	/* Currently, the pool and container services are combined. */
	rc = daos_pool_stop_svc(poh, NULL /* ev */);
	assert_rc_equal(rc, 0);

	print_message("closing container\n");
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
	print_message("destroying container\n");
	rc = daos_cont_destroy(poh, str, 1 /* force */, NULL);
	assert_rc_equal(rc, 0);
	print_message("disconnecting from pool\n");
	rc = daos_pool_disconnect(poh, NULL /* ev */);
	assert_rc_equal(rc, 0);

destroy:
	if (arg->myrank == 0) {
		if (skip)
			print_message("skipping\n");
		print_message("destroying pool\n");
		rc = dmg_pool_destroy(dmg_config_file,
				      pool_uuid, arg->group, 1);
		assert_rc_equal(rc, 0);
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

	rc = cmocka_run_group_tests_name("DAOS_MD_Replication", mdr_tests,
					 setup, test_teardown);
	par_barrier(PAR_COMM_WORLD);
	return rc;
}
