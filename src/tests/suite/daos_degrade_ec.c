/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is for simple tests of degraded EC object.
 *
 * tests/suite/daos_degraded_ec.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"
#include "dfs_test.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

#define DEGRADE_SMALL_POOL_SIZE	(1ULL << 28)
#define DEGRADE_POOL_SIZE	(1ULL << 32)
#define DEGRADE_RANK_SIZE	(6)

int
degrade_small_sub_setup(void **state)
{
	test_arg_t *arg;
	int rc;

	rc = test_setup(state, SETUP_CONT_CONNECT, true,
			DEGRADE_SMALL_POOL_SIZE, DEGRADE_RANK_SIZE, NULL);
	if (rc) {
		print_message("It can not create the pool with 6 ranks"
			      " probably due to not enough ranks %d\n", rc);
		return rc;
	}

	arg = *state;
	arg->no_rebuild = 1;
	rc = daos_pool_set_prop(arg->pool.pool_uuid, "self_heal",
				"exclude");
	return rc;
}

int
degrade_sub_setup(void **state)
{
	test_arg_t *arg;
	int rc;

	rc = test_setup(state, SETUP_CONT_CONNECT, true,
			DEGRADE_POOL_SIZE, DEGRADE_RANK_SIZE, NULL);
	if (rc)
		return rc;

	arg = *state;
	arg->no_rebuild = 1;
	rc = daos_pool_set_prop(arg->pool.pool_uuid, "self_heal",
				"exclude");
	return rc;
}

static void
degrade_ec_write(test_arg_t *arg, daos_obj_id_t oid, int write_type)
{
	struct ioreq	req;

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	if (write_type == PARTIAL_UPDATE)
		write_ec_partial(&req, arg->index, 0);
	else if (write_type == FULL_UPDATE)
		write_ec_full(&req, arg->index, 0);
	else if (write_type == FULL_PARTIAL_UPDATE)
		write_ec_full_partial(&req, arg->index, 0);
	else if (write_type == PARTIAL_FULL_UPDATE)
		write_ec_partial_full(&req, arg->index, 0);

	ioreq_fini(&req);
}

static void
degrade_ec_verify(test_arg_t *arg, daos_obj_id_t oid, int write_type)
{
	struct ioreq	req;
	int		rc;

	rc = daos_cont_status_clear(arg->coh, NULL);
	assert_rc_equal(rc, 0);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	if (write_type == PARTIAL_UPDATE)
		verify_ec_partial(&req, arg->index, 0);
	else if (write_type == FULL_UPDATE)
		verify_ec_full(&req, arg->index, 0);
	else if (write_type == FULL_PARTIAL_UPDATE)
		verify_ec_full_partial(&req, arg->index, 0);
	else if (write_type == PARTIAL_FULL_UPDATE)
		verify_ec_full(&req, arg->index, 0);

	ioreq_fini(&req);
}

static void
degrade_ec_internal(void **state, int *shards, int shards_nr, int write_type)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	d_rank_t	ranks[4] = { -1 };
	int		idx = 0;

	if (!test_runable(arg, DEGRADE_RANK_SIZE))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2G1, 0, 0, arg->myrank);
	degrade_ec_write(arg, oid, write_type);

	while (shards_nr-- > 0) {
		ranks[idx] = get_rank_by_oid_shard(arg, oid, shards[idx]);
		idx++;
	}
	rebuild_pools_ranks(&arg, 1, ranks, idx, false);

	degrade_ec_verify(arg, oid, write_type);
}

static void
degrade_partial_fail_data(void **state)
{
	int shard;

	shard = 1;
	degrade_ec_internal(state, &shard, 1, FULL_UPDATE);
}

static void
degrade_partial_fail_2data(void **state)
{
	int shards[2];

	shards[0] = 0;
	shards[1] = 3;
	degrade_ec_internal(state, shards, 2, PARTIAL_UPDATE);
}

static void
degrade_full_fail_data(void **state)
{
	int shard;

	shard = 3;
	degrade_ec_internal(state, &shard, 1, FULL_UPDATE);
}

static void
degrade_full_fail_2data(void **state)
{
	int shards[2];

	shards[0] = 0;
	shards[1] = 3;
	degrade_ec_internal(state, shards, 2, FULL_UPDATE);
}

static void
degrade_full_partial_fail_2data(void **state)
{
	int shards[2];

	/*
	 * Skipping test because of DAOS-6755, which seems to be related to EC
	 * aggregation
	 */
	skip();

	shards[0] = 0;
	shards[1] = 3;
	degrade_ec_internal(state, shards, 2, FULL_PARTIAL_UPDATE);
}

static void
degrade_partial_full_fail_2data(void **state)
{
	int shards[2];

	shards[0] = 0;
	shards[1] = 3;
	degrade_ec_internal(state, shards, 2, PARTIAL_FULL_UPDATE);
}

static void
degrade_partial_fail_data_parity(void **state)
{
	int shards[2];

	shards[0] = 0;
	shards[1] = 4;
	degrade_ec_internal(state, shards, 2, PARTIAL_UPDATE);
}

static void
degrade_full_fail_data_parity(void **state)
{
	int shards[2];

	shards[0] = 0;
	shards[1] = 5;
	degrade_ec_internal(state, shards, 2, FULL_UPDATE);
}

static void
degrade_dfs_fail_data_s0(void **state)
{
	int shard = 0;

	dfs_ec_rebuild_io(state, &shard, 1);
}

static void
degrade_dfs_fail_data_s1(void **state)
{
	int shard = 1;

	dfs_ec_rebuild_io(state, &shard, 1);
}

static void
degrade_dfs_fail_data_s3(void **state)
{
	int shard = 3;

	dfs_ec_rebuild_io(state, &shard, 1);
}

static void
degrade_dfs_fail_2data_s0s1(void **state)
{
	int shards[2];

	shards[0] = 0;
	shards[1] = 1;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
degrade_dfs_fail_2data_s0s2(void **state)
{
	int shards[2];

	shards[0] = 0;
	shards[1] = 2;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
degrade_dfs_fail_2data_s0s3(void **state)
{
	int shards[2];

	shards[0] = 0;
	shards[1] = 3;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
degrade_dfs_fail_2data_s1s2(void **state)
{
	int shards[2];

	shards[0] = 1;
	shards[1] = 2;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
degrade_dfs_fail_2data_s1s3(void **state)
{
	int shards[2];

	shards[0] = 1;
	shards[1] = 3;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
degrade_dfs_fail_2data_s2s3(void **state)
{
	int shards[2];

	shards[0] = 2;
	shards[1] = 3;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
degrade_dfs_fail_data_parity_s0p1(void **state)
{
	int shards[2];

	shards[0] = 0;
	shards[1] = 5;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
degrade_dfs_fail_data_parity_s3p1(void **state)
{
	int shards[2];

	shards[0] = 3;
	shards[1] = 5;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
degrade_dfs_fail_data_parity_s2p1(void **state)
{
	int shards[2];

	shards[0] = 2;
	shards[1] = 5;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
degrade_dfs_fail_data_parity_s0p0(void **state)
{
	int shards[2];

	shards[0] = 0;
	shards[1] = 4;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
degrade_dfs_fail_data_parity_s2p0(void **state)
{
	int shards[2];

	shards[0] = 2;
	shards[1] = 4;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
degrade_dfs_fail_data_parity_s3p0(void **state)
{
	int shards[2];

	shards[0] = 3;
	shards[1] = 4;
	dfs_ec_rebuild_io(state, shards, 2);
}

static void
degrade_multi_conts_agg(void **state)
{
#define CONT_PER_POOL	(8)
	test_arg_t	*arg = *state;
	test_arg_t	*args[CONT_PER_POOL] = { 0 };
	daos_obj_id_t	 oids[CONT_PER_POOL] = { 0 };
	int		 fail_shards[2];
	d_rank_t	 fail_ranks[4] = { -1 };
	int		 shards_nr = 2;
	int		 i;
	int		 rc;

	if (!test_runable(arg, DEGRADE_RANK_SIZE))
		return;

	fail_shards[0] = 0;
	fail_shards[1] = 2;

	memset(args, 0, sizeof(args[0]) * CONT_PER_POOL);
	for (i = 0; i < CONT_PER_POOL; i++) {
		rc = test_setup((void **)&args[i], SETUP_CONT_CONNECT,
				arg->multi_rank, DEGRADE_SMALL_POOL_SIZE,
				DEGRADE_RANK_SIZE, &arg->pool);
		if (rc) {
			print_message("test_setup failed: rc %d\n", rc);
			goto out;
		}

		args[i]->index = arg->index;
		assert_int_equal(args[i]->pool.slave, 1);
		/* XXX to temporarily workaround DAOS-7350, we need better
		 * error handling to fix the case if one obj's EC agg failed
		 * (for example parity shard fail cause agg_peer_update fail).
		 */
		if (i == 0)
			oids[i] = daos_test_oid_gen(arg->coh, OC_EC_4P2G1, 0, 0,
						    arg->myrank);
		else
			oids[i] = oids[0];
		args[i]->no_rebuild = 1;
	}

	for (i = 0; i < CONT_PER_POOL; i++) {
		if ((i % 3) == 0)
			degrade_ec_write(args[i], oids[i], FULL_PARTIAL_UPDATE);
		else if ((i % 3) == 1)
			degrade_ec_write(args[i], oids[i], PARTIAL_UPDATE);
		else
			degrade_ec_write(args[i], oids[i], PARTIAL_FULL_UPDATE);
	}

	/* sleep a while to make aggregation triggered */
	print_message("sleep about 25 second to wait aggregation ...\n");
	sleep(25);

	for (i = 0; i < shards_nr; i++)
		fail_ranks[i] = get_rank_by_oid_shard(args[0], oids[0],
						      fail_shards[i]);
	rebuild_pools_ranks(&args[0], 1, fail_ranks, shards_nr, false);

	for (i = 0; i < CONT_PER_POOL; i++) {
		if ((i % 3) == 0)
			degrade_ec_verify(args[i], oids[i],
					  FULL_PARTIAL_UPDATE);
		else if ((i % 3) == 1)
			degrade_ec_verify(args[i], oids[i], PARTIAL_UPDATE);
		else
			degrade_ec_verify(args[i], oids[i],
					  PARTIAL_FULL_UPDATE);
	}

out:
	for (i = CONT_PER_POOL - 1; i >= 0; i--)
		test_teardown((void **)&args[i]);
}

/** create a new pool/container for each test */
static const struct CMUnitTest degrade_tests[] = {
	{"DEGRADE0: degrade partial update with data tgt fail",
	 degrade_partial_fail_data, degrade_small_sub_setup, test_teardown},
	{"DEGRADE1: degrade partial update with 2 data tgt fail",
	 degrade_partial_fail_2data, degrade_small_sub_setup, test_teardown},
	{"DEGRADE2: degrade full stripe update with data tgt fail",
	 degrade_full_fail_data, degrade_small_sub_setup, test_teardown},
	{"DEGRADE3: degrade full stripe update with 2 data tgt fail",
	 degrade_full_fail_2data, degrade_small_sub_setup, test_teardown},
	{"DEGRADE4: degrade full then partial update with 2 data tgt fail",
	 degrade_full_partial_fail_2data, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE5: degrade partial then full update with 2 data tgt fail",
	 degrade_partial_full_fail_2data, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE6: degrade partial full update with data/parity tgt fail",
	 degrade_partial_fail_data_parity, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE7: degrade full update with data/parity tgt fail ",
	 degrade_full_fail_data_parity, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE8: degrade io with data(s0) tgt fail ",
	 degrade_dfs_fail_data_s0, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE9: degrade io with data(s1) tgt fail ",
	 degrade_dfs_fail_data_s1, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE10: degrade io with data(s1) tgt fail ",
	 degrade_dfs_fail_data_s3, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE11: degrade io with data(s0, s1) tgt fail ",
	 degrade_dfs_fail_2data_s0s1, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE12: degrade io with data(s0, s2) tgt fail ",
	 degrade_dfs_fail_2data_s0s2, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE13: degrade io with data(s0, s3) tgt fail ",
	 degrade_dfs_fail_2data_s0s3, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE14: degrade io with data(s1, s2) tgt fail ",
	 degrade_dfs_fail_2data_s1s2, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE15: degrade io with data(s1, s3) tgt fail ",
	 degrade_dfs_fail_2data_s1s3, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE16: degrade io with data(s2, s3) tgt fail ",
	 degrade_dfs_fail_2data_s2s3, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE17: degrade io with 1data 1parity(s0, p1)",
	 degrade_dfs_fail_data_parity_s0p1, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE18: degrade io with 1data 1parity(s3, p1)",
	 degrade_dfs_fail_data_parity_s3p1, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE19: degrade io with 1data 1parity(s2, p1)",
	 degrade_dfs_fail_data_parity_s2p1, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE20: degrade io with 1data 1parity(s0, p0)",
	 degrade_dfs_fail_data_parity_s0p0, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE21: degrade io with 1data 1parity(s3, p0)",
	 degrade_dfs_fail_data_parity_s3p0, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE22: degrade io with 1data 1parity(s2, p0)",
	 degrade_dfs_fail_data_parity_s2p0, degrade_small_sub_setup,
	 test_teardown},
	{"DEGRADE23: degrade io with multi-containers and aggregation",
	 degrade_multi_conts_agg, degrade_sub_setup, test_teardown},
};

int
run_daos_degrade_simple_ec_test(int rank, int size, int *sub_tests,
				int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(degrade_tests);
		sub_tests = NULL;
	}
	rc += run_daos_sub_tests_only("DAOS_Degrade_EC", degrade_tests,
				ARRAY_SIZE(degrade_tests), sub_tests,
				sub_tests_size);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
