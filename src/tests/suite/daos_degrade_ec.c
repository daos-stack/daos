/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * This file is for simple tests of degraded EC object.
 *
 * tests/suite/daos_degraded_ec.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

static void
degrade_ec_internal(void **state, int *shards, int shards_nr, int write_type)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	d_rank_t	ranks[4] = { -1 };
	int		idx = 0;

	if (!test_runable(arg, 6))
		return;

	oid = dts_oid_gen(OC_EC_4P2G1, 0, arg->myrank);
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

	while (shards_nr-- > 0) {
		ranks[idx] = get_rank_by_oid_shard(arg, oid, shards[idx]);
		idx++;
	}
	rebuild_pools_ranks(&arg, 1, ranks, idx, false);

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
#if 0
	while (idx > 0)
		rebuild_add_back_tgts(arg, ranks[--idx], NULL, 1);
#endif
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

#define DEGRADE_SMALL_POOL_SIZE (1ULL << 28)
int
degrade_small_sub_setup(void **state)
{
	int rc;

	rc = test_setup(state, SETUP_CONT_CONNECT, true,
			DEGRADE_SMALL_POOL_SIZE, 6, NULL);
	return rc;
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
	run_daos_sub_tests_only("DAOS degrade ec tests", degrade_tests,
				ARRAY_SIZE(degrade_tests), sub_tests,
				sub_tests_size);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
