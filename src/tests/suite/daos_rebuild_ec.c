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
rebuild_ec_internal(void **state, bool parity, int type)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	d_rank_t		kill_rank = 0;
	d_rank_t		kill_rank1 = 0;

	if (!test_runable(arg, 5))
		return;

	oid = dts_oid_gen(OC_EC_2P1G1, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));

	if (type == PARTIAL_UPDATE)
		write_ec_partial(&req, arg->index, 0);
	else if (type == FULL_UPDATE)
		write_ec_full(&req, arg->index, 0);
	else if (type == FULL_PARTIAL_UPDATE)
		write_ec_full_partial(&req, arg->index, 0);
	else if (type == PARTIAL_FULL_UPDATE)
		write_ec_partial_full(&req, arg->index, 0);

	ioreq_fini(&req);

	kill_rank = get_killing_rank_by_oid(arg, oid, parity);
	rebuild_single_pool_target(arg, kill_rank, -1, false);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	if (parity) {
		/* To verify if the parity being rebuild correctly,
		 * let's kill another node to to rebuild data, then
		 * verify if the rebuild data is correct.
		 */
		kill_rank1 = get_killing_rank_by_oid(arg, oid, false);
		rebuild_single_pool_target(arg, kill_rank1, -1, false);
	}

	if (type == PARTIAL_UPDATE)
		verify_ec_partial(&req, arg->index, 0);
	else if (type == FULL_UPDATE)
		verify_ec_full(&req, arg->index, 0);
	else if (type == FULL_PARTIAL_UPDATE)
		verify_ec_full_partial(&req, arg->index, 0);
	else if (type == PARTIAL_FULL_UPDATE)
		verify_ec_full(&req, arg->index, 0);

	ioreq_fini(&req);
	reintegrate_single_pool_target(arg, kill_rank, -1);
	if (parity)
		reintegrate_single_pool_target(arg, kill_rank1, -1);
}

static void
rebuild_partial_fail_data(void **state)
{
	rebuild_ec_internal(state, false, PARTIAL_UPDATE);
}

static void
rebuild_partial_fail_parity(void **state)
{
	rebuild_ec_internal(state, true, PARTIAL_UPDATE);
}

static void
rebuild_full_fail_data(void **state)
{
	rebuild_ec_internal(state, false, FULL_UPDATE);
}

static void
rebuild_full_fail_parity(void **state)
{
	rebuild_ec_internal(state, true, FULL_UPDATE);
}

static void
rebuild_full_partial_fail_data(void **state)
{
	rebuild_ec_internal(state, false, FULL_PARTIAL_UPDATE);
}

static void
rebuild_full_partial_fail_parity(void **state)
{
	rebuild_ec_internal(state, true, FULL_PARTIAL_UPDATE);
}

static void
rebuild_partial_full_fail_data(void **state)
{
	rebuild_ec_internal(state, false, PARTIAL_FULL_UPDATE);
}

static void
rebuild_partial_full_fail_parity(void **state)
{
	rebuild_ec_internal(state, true, PARTIAL_FULL_UPDATE);
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
