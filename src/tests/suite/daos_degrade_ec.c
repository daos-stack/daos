/**
 * (C) Copyright 2016-2022 Intel Corporation.
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
	bool		 parity_checked = false;
	int		 shards_nr = 2;
	int		 i;
	int		 rc;

	if (!test_runable(arg, DEGRADE_RANK_SIZE))
		return;

	FAULT_INJECTION_REQUIRED();

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

		daos_pool_set_prop(args[i]->pool.pool_uuid, "reclaim", "time");
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
	trigger_and_wait_ec_aggreation(arg, oids, CONT_PER_POOL, NULL, NULL, 0,
				       0, DAOS_FORCE_EC_AGG);

	for (i = 0; i < shards_nr; i++)
		fail_ranks[i] = get_rank_by_oid_shard(args[0], oids[0],
						      fail_shards[i]);
	rebuild_pools_ranks(&args[0], 1, fail_ranks, shards_nr, false);

re_test:
	if (!parity_checked)
		daos_debug_set_params(args[0]->group, -1, DMG_KEY_FAIL_LOC,
			DAOS_FAIL_AGG_BOUNDRY_MOVED | DAOS_FAIL_ONCE, 0, NULL);

	for (i = 0; i < CONT_PER_POOL; i++) {
		if (parity_checked)
			args[i]->fail_loc = DAOS_FAIL_PARITY_EPOCH_DIFF |
					    DAOS_FAIL_ONCE;
		if ((i % 3) == 0)
			degrade_ec_verify(args[i], oids[i],
					  FULL_PARTIAL_UPDATE);
		else if ((i % 3) == 1)
			degrade_ec_verify(args[i], oids[i], PARTIAL_UPDATE);
		else
			degrade_ec_verify(args[i], oids[i],
					  PARTIAL_FULL_UPDATE);
	}

	if (!parity_checked) {
		daos_debug_set_params(args[0]->group, -1, DMG_KEY_FAIL_LOC, 0,
				      0, NULL);
		parity_checked = true;
		goto re_test;
	}
	daos_fail_loc_set(0);

out:
	for (i = CONT_PER_POOL - 1; i >= 0; i--)
		test_teardown((void **)&args[i]);
}

#define EC_CELL_SIZE	DAOS_EC_CELL_DEF
static void
degrade_ec_partial_update_agg(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	d_rank_t	rank;
	int		i;
	char		*data;
	char		*verify_data;

	FAULT_INJECTION_REQUIRED();

	if (!test_runable(arg, 6))
		return;

	data = (char *)malloc(EC_CELL_SIZE);
	assert_true(data != NULL);
	verify_data = (char *)malloc(EC_CELL_SIZE);
	assert_true(verify_data != NULL);
	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2G1, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 10; i++) {
		daos_recx_t recx;

		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = EC_CELL_SIZE;
		recx.rx_idx = i * EC_CELL_SIZE;
		memset(data, 'a' + i, EC_CELL_SIZE);
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, EC_CELL_SIZE, &req);
	}

	/* Kill one parity shard, which is the aggregate leader to verify
	 * aggregate works in degraded mode.
	 */
	rank = get_rank_by_oid_shard(arg, oid, 4);
	rebuild_pools_ranks(&arg, 1, &rank, 1, false);

	/* Trigger aggregation */
	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "time");
	trigger_and_wait_ec_aggreation(arg, &oid, 1, "d_key", "a_key", 0,
				       8 * EC_CELL_SIZE, DAOS_FORCE_EC_AGG);
	for (i = 0; i < 10; i++) {
		daos_off_t offset = i * EC_CELL_SIZE;

		memset(verify_data, 'a' + i, EC_CELL_SIZE);
		ec_verify_parity_data(&req, "d_key", "a_key", offset,
				      (daos_size_t)EC_CELL_SIZE, verify_data,
				      DAOS_TX_NONE, true);
	}
	free(data);
	free(verify_data);
}

static void
degrade_ec_agg(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	d_rank_t	rank;
	int		i;
	char		*data;
	char		*verify_data;

	if (!test_runable(arg, 6))
		return;

	data = (char *)malloc(EC_CELL_SIZE);
	assert_true(data != NULL);
	verify_data = (char *)malloc(EC_CELL_SIZE);
	assert_true(verify_data != NULL);
	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2G1, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 4; i++) {
		daos_recx_t recx;

		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = EC_CELL_SIZE;
		recx.rx_idx = i * EC_CELL_SIZE;
		memset(data, 'a' + i, EC_CELL_SIZE);
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, EC_CELL_SIZE, &req);
	}

	for (i = 7; i >= 4; i--) {
		daos_recx_t recx;

		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = EC_CELL_SIZE;
		recx.rx_idx = i * EC_CELL_SIZE;
		memset(data, 'a' + i, EC_CELL_SIZE);
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, EC_CELL_SIZE, &req);
	}


	/* Degrade EC aggregation. */
	rank = get_rank_by_oid_shard(arg, oid, 2);
	rebuild_pools_ranks(&arg, 1, &rank, 1, false);
	print_message("sleep 30 seconds");
	sleep(30);

	/* Trigger VOS aggregation */
	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "time");
	trigger_and_wait_ec_aggreation(arg, &oid, 1, NULL, NULL, 0, 0,
				       DAOS_FORCE_EC_AGG);

	for (i = 0; i < 8; i++) {
		daos_off_t offset = i * EC_CELL_SIZE;

		memset(verify_data, 'a' + i, EC_CELL_SIZE);
		ec_verify_parity_data(&req, "d_key", "a_key", offset,
				      (daos_size_t)EC_CELL_SIZE, verify_data,
				      DAOS_TX_NONE, true);
	}
	free(data);
	free(verify_data);
}

static void
degrade_ec_update(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	uint64_t	dkey_int;
	uint16_t	fail_shards[2];
	int		i;
	char		*data;
#define TEST_EC_STRIPE_SIZE	(1 * 1024 * 1024 * 4)

	if (!test_runable(arg, 6) || (arg->srv_ntgts / arg->srv_nnodes) < 2)
		return;

	print_message("test 1 - DER_NEED_TX case\n");
	data = (char *)malloc(TEST_EC_STRIPE_SIZE);
	assert_true(data != NULL);
	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2G2, DAOS_OF_DKEY_UINT64, 0, arg->myrank);
	dkey_int = 4;

	arg->fail_loc = DAOS_FAIL_SHARD_NONEXIST | DAOS_FAIL_ONCE;
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 4; i++) {
		daos_recx_t recx;

		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = TEST_EC_STRIPE_SIZE;
		recx.rx_idx = i * TEST_EC_STRIPE_SIZE;
		memset(data, 'a' + i, TEST_EC_STRIPE_SIZE);
		inset_recxs_dkey_uint64(&dkey_int, "a_key", 1, DAOS_TX_NONE, &recx, 1,
					data, TEST_EC_STRIPE_SIZE, &req);
	}
	ioreq_fini(&req);

	print_message("test 2 - DAOS_FAIL_SHARD_OPEN case\n");
	fail_shards[0] = 7;
	fail_shards[1] = 8;
	arg->fail_loc = DAOS_FAIL_SHARD_OPEN | DAOS_FAIL_ALWAYS;
	arg->fail_value = daos_shard_fail_value(fail_shards, 2);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 1; i++) {
		daos_recx_t recx;

		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = TEST_EC_STRIPE_SIZE;
		recx.rx_idx = i * TEST_EC_STRIPE_SIZE;
		memset(data, 'a' + i, TEST_EC_STRIPE_SIZE);
		inset_recxs_dkey_uint64(&dkey_int, "a_key_1", 1, DAOS_TX_NONE, &recx, 1,
			     data, TEST_EC_STRIPE_SIZE, &req);
	}
	ioreq_fini(&req);

	print_message("test 3 - partial update only one leader alive case\n");
	oid = daos_test_oid_gen(arg->coh, OC_EC_4P1G1, DAOS_OF_DKEY_UINT64, 0, arg->myrank);
	/* simulate shard 0's failure, then partial update [0, 4096] will need to update
	 * data shard 0 and parity shard 4, so only the leader shard 4 alive.
	 */
	fail_shards[0] = 1;
	arg->fail_loc = DAOS_FAIL_SHARD_OPEN | DAOS_FAIL_ALWAYS;
	arg->fail_value = daos_shard_fail_value(fail_shards, 1);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 1; i++) {
		daos_recx_t recx;

		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = 4096;
		recx.rx_idx = i * TEST_EC_STRIPE_SIZE;
		memset(data, 'a' + i, 4096);
		inset_recxs_dkey_uint64(&dkey_int, "a_key_2", 1, DAOS_TX_NONE, &recx, 1,
					data, 4096, &req);
	}
	ioreq_fini(&req);

	free(data);
}

static void
degrade_ec_agg_punch(void **state, int shard)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	d_rank_t	rank;
	int		i;
	char		*data;
	char		*verify_data;

	if (!test_runable(arg, 6))
		return;

	data = (char *)malloc(EC_CELL_SIZE);
	assert_true(data != NULL);
	verify_data = (char *)malloc(EC_CELL_SIZE);
	assert_true(verify_data != NULL);
	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2G1, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 12; i++) {
		daos_recx_t recx;
		char	    dkey[32];

		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = EC_CELL_SIZE;
		recx.rx_idx = i * EC_CELL_SIZE;
		memset(data, 'a' + i, EC_CELL_SIZE);
		sprintf(dkey, "dkey_%d", i);
		insert_recxs(dkey, "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, EC_CELL_SIZE, &req);
		if (i == 2)
			punch_dkey(dkey, DAOS_TX_NONE, &req);
		if (i == 3)
			punch_akey(dkey, "a_key", DAOS_TX_NONE, &req);
		if (i == 4) {
			recx.rx_nr = EC_CELL_SIZE;
			recx.rx_idx = i * EC_CELL_SIZE;
			punch_recxs(dkey, "a_key", &recx, 1, DAOS_TX_NONE, &req);
		}
	}

	/* Trigger aggregation */
	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "time");
	trigger_and_wait_ec_aggreation(arg, &oid, 1, "d_key", "a_key", 0,
				       8 * EC_CELL_SIZE, DAOS_FORCE_EC_AGG);

	rank = get_rank_by_oid_shard(arg, oid, shard);
	rebuild_pools_ranks(&arg, 1, &rank, 1, false);

	for (i = 0; i < 12; i++) {
		char	    dkey[32];
		daos_off_t offset = i * EC_CELL_SIZE;

		sprintf(dkey, "dkey_%d", i);
		memset(data, 0, EC_CELL_SIZE);
		memset(verify_data, 'a' + i, EC_CELL_SIZE);
		if (i != 2 && i != 3 && i != 4)
			ec_verify_parity_data(&req, dkey, "a_key", offset,
					      (daos_size_t)EC_CELL_SIZE, verify_data,
					      DAOS_TX_NONE, true);
	}

	free(data);
	free(verify_data);
}

static void
degrade_ec_agg_punch_fail_parity(void **state)
{
	degrade_ec_agg_punch(state, 5);
}

static void
degrade_ec_agg_punch_fail_data(void **state)
{
	degrade_ec_agg_punch(state, 1);
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
	{"DEGRADE24: degrade ec aggregation partial update",
	 degrade_ec_partial_update_agg, degrade_sub_setup, test_teardown},
	{"DEGRADE25: degrade ec aggregation",
	 degrade_ec_agg, degrade_sub_setup, test_teardown},
	{"DEGRADE26: degrade ec update",
	 degrade_ec_update, degrade_sub_setup, test_teardown},
	{"DEGRADE27: degrade ec update punch aggregation parity fail",
	 degrade_ec_agg_punch_fail_parity, degrade_sub_setup, test_teardown},
	{"DEGRADE28: degrade ec update punch aggregation data fail",
	 degrade_ec_agg_punch_fail_data, degrade_sub_setup, test_teardown},
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
