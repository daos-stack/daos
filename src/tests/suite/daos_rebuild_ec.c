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

#define MAX_SIZE		1048576 * 3
#define DATA_SIZE		(1048576 * 2 + 512)
#define PARTIAL_DATA_SIZE	1024	

#define KEY_NR	5
static void
write_ec(struct ioreq *req, int index, char *data, daos_off_t off, int size)
{
	char		key[32];
	daos_recx_t	recx;
	int		i;
	char		single_data[8192];

	for (i = 0; i < KEY_NR; i++) {
		req->iod_type = DAOS_IOD_ARRAY;
		sprintf(key, "dkey_%d", index);
		recx.rx_nr = size;
		recx.rx_idx = off + i * 10485760;
		insert_recxs(key, "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, size, req);

		req->iod_type = DAOS_IOD_SINGLE;
		memset(single_data, 'a'+i, 8192);
		sprintf(key, "dkey_single_small_%d_%d", index, i);
		insert_single(key, "a_key", 0, single_data, 32, DAOS_TX_NONE,
			      req);
		sprintf(key, "dkey_single_large_%d_%d", index, i);
		insert_single(key, "a_key", 0, single_data, 8192, DAOS_TX_NONE,
			      req);
	}
}

static void
verify_ec(struct ioreq *req, int index, char *verify_data, daos_off_t off,
	  int size)
{
	char	key[32];
	char	read_data[MAX_SIZE];
	char	single_data[8192];
	char	verify_single_data[8192];
	int	i;

	for (i = 0; i < KEY_NR; i++) {
		uint64_t offset = off + i * 10485760;

		req->iod_type = DAOS_IOD_ARRAY;
		sprintf(key, "dkey_%d", index);
		memset(read_data, 0, size);
		lookup_single_with_rxnr(key, "a_key", offset, read_data,
					1, size, DAOS_TX_NONE, req);
		assert_memory_equal(read_data, verify_data, size);

		req->iod_type = DAOS_IOD_SINGLE;
		memset(single_data, 0, 8192);	
		memset(verify_single_data, 'a'+i, 8192);
		sprintf(key, "dkey_single_small_%d_%d", index, i);
		lookup_single(key, "a_key", 0, single_data, 32, DAOS_TX_NONE,
			      req);
		assert_memory_equal(single_data, verify_single_data, 32);

		memset(single_data, 0, 8192);	
		sprintf(key, "dkey_single_large_%d_%d", index, i);
		lookup_single(key, "a_key", 0, single_data, 8192, DAOS_TX_NONE,
			      req);
		assert_memory_equal(single_data, verify_single_data, 8192);
	}
}

static void
write_ec_partial(struct ioreq *req, int test_idx, daos_off_t off)
{
	char	buffer[PARTIAL_DATA_SIZE];

	memset(buffer, 'a', PARTIAL_DATA_SIZE);
	write_ec(req, test_idx, buffer, off, PARTIAL_DATA_SIZE); 
}

static void
verify_ec_partial(struct ioreq *req, int test_idx, daos_off_t off)
{
	char	buffer[PARTIAL_DATA_SIZE];

	memset(buffer, 'a', PARTIAL_DATA_SIZE);
	verify_ec(req, test_idx, buffer, off, PARTIAL_DATA_SIZE); 
}

static void
write_ec_full(struct ioreq *req, int test_idx, daos_off_t off)
{
	char	buffer[DATA_SIZE];

	memset(buffer, 'b', DATA_SIZE);
	write_ec(req, test_idx, buffer, off, DATA_SIZE); 
}

static void
verify_ec_full(struct ioreq *req, int test_idx, daos_off_t off)
{
	char	buffer[DATA_SIZE];

	memset(buffer, 'b', DATA_SIZE);
	verify_ec(req, test_idx, buffer, off, DATA_SIZE); 
}

static void
write_ec_full_partial(struct ioreq *req, int test_idx, daos_off_t off)
{
	write_ec_full(req, test_idx, off);
	write_ec_partial(req, test_idx, off);
}

static void
write_ec_partial_full(struct ioreq *req, int test_idx, daos_off_t off)
{
	write_ec_partial(req, test_idx, off);
	write_ec_full(req, test_idx, off);
}

static void
verify_ec_full_partial(struct ioreq *req, int test_idx, daos_off_t off)
{
	char	buffer[DATA_SIZE];

	memset(buffer, 'b', DATA_SIZE);
	memset(buffer, 'a', PARTIAL_DATA_SIZE);
	verify_ec(req, test_idx, buffer, off, DATA_SIZE); 
}

enum op_type {
	PARTIAL_UPDATE	=	1,
	FULL_UPDATE,
	FULL_PARTIAL_UPDATE,
	PARTIAL_FULL_UPDATE
};

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

	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));

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
	if (kill_parity_nr > 0)
		reintegrate_pools_ranks(&arg, 1, &kill_data_rank, 1);

	reintegrate_pools_ranks(&arg, 1, kill_ranks, kill_ranks_num);
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
	rebuild_ec_internal(state, OC_EC_4P2G1, 1, 0, PARTIAL_UPDATE);
}

static void
rebuild2p_partial_fail_2data(void **state)
{
	rebuild_ec_internal(state, OC_EC_4P2G1, 2, 0, PARTIAL_UPDATE);
}

static void
rebuild2p_partial_fail_data_parity(void **state)
{
	rebuild_ec_internal(state, OC_EC_4P2G1, 1, 1, PARTIAL_UPDATE);
}

static void
rebuild2p_partial_fail_parity(void **state)
{
	rebuild_ec_internal(state, OC_EC_4P2G1, 0, 1, PARTIAL_UPDATE);
}

static void
rebuild2p_partial_fail_2parity(void **state)
{
	rebuild_ec_internal(state, OC_EC_4P2G1, 0, 2, PARTIAL_UPDATE);
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
