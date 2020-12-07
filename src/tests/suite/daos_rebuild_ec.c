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
 * This file is for simple tests of rebuild, which does not need to kill the
 * rank, and only used to verify the consistency after different data model
 * rebuild.
 *
 * tests/suite/daos_rebuild_simple.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>
static void
rebuild_ec_internal(void **state, uint16_t oclass, int kill_data_nr,
		    int kill_parity_nr, int write_type)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	d_rank_t		kill_ranks[4] = { -1 };
	int			kill_ranks_num = 0;
	d_rank_t		kill_data_rank;

	if (oclass == OC_EC_2P1G1 && !test_runable(arg, 5))
		return;
	else if (oclass == OC_EC_4P2G1 && !test_runable(arg, 7))
		return;

	oid = dts_oid_gen(oclass, 0, arg->myrank);
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

	get_killing_rank_by_oid(arg, oid, kill_data_nr, kill_parity_nr,
				kill_ranks, &kill_ranks_num);

	rebuild_pools_ranks(&arg, 1, kill_ranks, kill_ranks_num, false);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	if (kill_parity_nr > 0) {
		int tmp;

		/* To verify if the parity being rebuild correctly,
		 * let's kill another data node to to rebuild data, then
		 * verify if the rebuild data is correct.
		 */
		get_killing_rank_by_oid(arg, oid, 1, 0, &kill_data_rank, &tmp);
		rebuild_pools_ranks(&arg, 1, &kill_data_rank, 1, false);
	}

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
	/* Disable reintegrate due to DAOS-5884 */
	if (kill_parity_nr > 0)
		reintegrate_pools_ranks(&arg, 1, &kill_data_rank, 1);

	reintegrate_pools_ranks(&arg, 1, kill_ranks, kill_ranks_num);
#endif
}

static void
rebuild_partial_fail_data(void **state)
{
	rebuild_ec_internal(state, OC_EC_2P1G1, 1, 0, PARTIAL_UPDATE);
}

static void
rebuild_partial_fail_parity(void **state)
{
	rebuild_ec_internal(state, OC_EC_2P1G1, 0, 1, PARTIAL_UPDATE);
}

static void
rebuild_full_fail_data(void **state)
{
	rebuild_ec_internal(state, OC_EC_2P1G1, 1, 0, FULL_UPDATE);
}

static void
rebuild_full_fail_parity(void **state)
{
	rebuild_ec_internal(state, OC_EC_2P1G1, 0, 1, FULL_UPDATE);
}

static void
rebuild_full_partial_fail_data(void **state)
{
	rebuild_ec_internal(state, OC_EC_2P1G1, 1, 0, FULL_PARTIAL_UPDATE);
}

static void
rebuild_full_partial_fail_parity(void **state)
{
	rebuild_ec_internal(state, OC_EC_2P1G1, 0, 1, FULL_PARTIAL_UPDATE);
}

static void
rebuild_partial_full_fail_data(void **state)
{
	rebuild_ec_internal(state, OC_EC_2P1G1, 1, 0, PARTIAL_FULL_UPDATE);
}

static void
rebuild_partial_full_fail_parity(void **state)
{
	rebuild_ec_internal(state, OC_EC_2P1G1, 0, 1, PARTIAL_FULL_UPDATE);
}

static void
rebuild2p_partial_fail_data(void **state)
{
	rebuild_ec_internal(state, OC_EC_4P2G1, 1, 0, FULL_UPDATE);
}

static void
rebuild2p_partial_fail_2data(void **state)
{
	rebuild_ec_internal(state, OC_EC_4P2G1, 2, 0, FULL_UPDATE);
}

static void
rebuild2p_partial_fail_data_parity(void **state)
{
	rebuild_ec_internal(state, OC_EC_4P2G1, 1, 1, FULL_UPDATE);
}

static void
rebuild2p_partial_fail_parity(void **state)
{
	rebuild_ec_internal(state, OC_EC_4P2G1, 0, 1, FULL_UPDATE);
}

static void
rebuild2p_partial_fail_2parity(void **state)
{
	rebuild_ec_internal(state, OC_EC_4P2G1, 0, 2, FULL_UPDATE);
}

#define CELL_SIZE	1048576

static void
rebuild_mixed_stripes(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	char		*data;
	char		*verify_data;
	daos_recx_t	recxs[5];
	d_rank_t	rank = 0;
	int		size = 8 * 1048576 + 10000;

	if (!test_runable(arg, 7))
		return;

	oid = dts_oid_gen(OC_EC_4P2G1, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	recxs[0].rx_idx = 0;		/* full stripe */
	recxs[0].rx_nr = 4 * CELL_SIZE;

	recxs[1].rx_idx = 5 * CELL_SIZE; /* partial stripe */
	recxs[1].rx_nr = 2000;

	recxs[2].rx_idx = 8 * CELL_SIZE;	/* full stripe */
	recxs[2].rx_nr = 4 * 1048576;

	recxs[3].rx_idx = 12 * 1048576;	/* partial stripe */
	recxs[3].rx_nr = 5000;

	recxs[4].rx_idx = 16 * 1048576 - 3000;	/* partial stripe */
	recxs[4].rx_nr = 3000;

	data = (char *)malloc(size);
	verify_data = (char *)malloc(size);
	make_buffer(data, 'a', size);
	make_buffer(verify_data, 'a', size);

	req.iod_type = DAOS_IOD_ARRAY;
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, recxs, 5,
		     data, size, &req);

	rank = get_rank_by_oid_shard(arg, oid, 0);
	rebuild_pools_ranks(&arg, 1, &rank, 1, false);

	memset(data, 0, size);
	lookup_recxs("d_key", "a_key", 1, DAOS_TX_NONE, recxs, 5,
		     data, size, &req);
	assert_memory_equal(data, verify_data, size);

	ioreq_fini(&req);

	reintegrate_pools_ranks(&arg, 1, &rank, 1);
}

/** create a new pool/container for each test */
static const struct CMUnitTest rebuild_tests[] = {
	{"REBUILD0: rebuild partial update with data tgt fail",
	 rebuild_partial_fail_data, rebuild_small_sub_setup, test_teardown},
	{"REBUILD1: rebuild partial update with parity tgt fail",
	 rebuild_partial_fail_parity, rebuild_small_sub_setup, test_teardown},
	{"REBUILD2: rebuild full stripe update with data tgt fail",
	 rebuild_full_fail_data, rebuild_small_sub_setup, test_teardown},
	{"REBUILD3: rebuild full stripe update with parity tgt fail",
	 rebuild_full_fail_parity, rebuild_small_sub_setup, test_teardown},
	{"REBUILD4: rebuild full then partial update with data tgt fail",
	 rebuild_full_partial_fail_data, rebuild_small_sub_setup,
	 test_teardown},
	{"REBUILD5: rebuild full then partial update with parity tgt fail",
	 rebuild_full_partial_fail_parity, rebuild_small_sub_setup,
	 test_teardown},
	{"REBUILD6: rebuild partial then full update with data tgt fail",
	 rebuild_partial_full_fail_data, rebuild_small_sub_setup,
	 test_teardown},
	{"REBUILD7: rebuild partial then full update with parity tgt fail",
	 rebuild_partial_full_fail_parity, rebuild_small_sub_setup,
	 test_teardown},
	{"REBUILD8: rebuild2p partial update with data tgt fail ",
	 rebuild2p_partial_fail_data, rebuild_small_sub_setup, test_teardown},
	{"REBUILD9: rebuild2p partial update with 2 data tgt fail ",
	 rebuild2p_partial_fail_2data, rebuild_small_sub_setup, test_teardown},
	{"REBUILD10: rebuild2p partial update with data/parity tgts fail ",
	 rebuild2p_partial_fail_data_parity, rebuild_small_sub_setup,
	 test_teardown},
	{"REBUILD11: rebuild2p partial update with parity tgt fail",
	 rebuild2p_partial_fail_parity, rebuild_small_sub_setup, test_teardown},
	{"REBUILD12: rebuild2p partial update with 2 parity tgt fail",
	 rebuild2p_partial_fail_2parity, rebuild_small_sub_setup,
	 test_teardown},
	{"REBUILD13: rebuild with mixed partial/full stripe",
	 rebuild_mixed_stripes, rebuild_small_sub_setup, test_teardown},
};

int
run_daos_rebuild_simple_ec_test(int rank, int size, int *sub_tests,
				int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(rebuild_tests);
		sub_tests = NULL;
	}

	run_daos_sub_tests_only("DAOS rebuild ec tests", rebuild_tests,
				ARRAY_SIZE(rebuild_tests), sub_tests,
				sub_tests_size);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
