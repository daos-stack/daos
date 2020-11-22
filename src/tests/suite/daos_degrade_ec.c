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
#include "dfs_test.h"
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

	while (idx > 0)
		rebuild_add_back_tgts(arg, ranks[--idx], NULL, 1);
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
degrade_dfs_internal(void **state, int *shards, int shards_nr)
{
	dfs_t		*dfs_mt;
	daos_handle_t	co_hdl;
	test_arg_t	*arg = *state;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	dfs_obj_t	*obj;
	daos_size_t	buf_size = 32 * 1024 * 32;
	daos_size_t	partial_size = 32 * 1024 * 2;
	daos_size_t	chunk_size = 32 * 1024 * 4;
	daos_size_t	fetch_size = 0;
	uuid_t		co_uuid;
	char		filename[32];
	d_rank_t	ranks[4] = { -1 };
	int		idx = 0;
	daos_obj_id_t	oid;
	char		*buf;
	char		*vbuf;
	int		i;
	int		rc;

	uuid_generate(co_uuid);
	rc = dfs_cont_create(arg->pool.poh, co_uuid, NULL, &co_hdl,
			     &dfs_mt);
	assert_int_equal(rc, 0);
	printf("Created DFS Container "DF_UUIDF"\n", DP_UUID(co_uuid));

	D_ALLOC(buf, buf_size);
	assert_true(buf != NULL);
	D_ALLOC(vbuf, buf_size);
	assert_true(vbuf != NULL);

	d_iov_set(&iov, buf, buf_size);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = &iov;

	dts_buf_render(buf, buf_size);
	memcpy(vbuf, buf, buf_size);

	/* Full stripe update */
	sprintf(filename, "degrade_file");
	rc = dfs_open(dfs_mt, NULL, filename, S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, DAOS_OC_EC_K4P2_L32K, chunk_size,
		      NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_write(dfs_mt, obj, &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	/* Partial update after that */
	d_iov_set(&iov, buf, partial_size);
	for (i = 0; i < 10; i++) {
		dfs_write(dfs_mt, obj, &sgl, buf_size + i * 100 * 1024, NULL);
		assert_int_equal(rc, 0);
	}

	dfs_obj2id(obj, &oid);
	while (shards_nr-- > 0) {
		ranks[idx] = get_rank_by_oid_shard(arg, oid, shards[idx]);
		idx++;
	}
	rebuild_pools_ranks(&arg, 1, ranks, idx, false);

	/* Verify full stripe */
	d_iov_set(&iov, buf, buf_size);
	fetch_size = 0;
	rc = dfs_read(dfs_mt, obj, &sgl, 0, &fetch_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(fetch_size, buf_size);
	assert_memory_equal(buf, vbuf, buf_size);

	/* Verify partial stripe */
	d_iov_set(&iov, buf, partial_size);
	for (i = 0; i < 10; i++) {
		memset(buf, 0, buf_size);
		fetch_size = 0;
		dfs_read(dfs_mt, obj, &sgl, buf_size + i * 100 * 1024,
			 &fetch_size, NULL);
		assert_int_equal(rc, 0);
		assert_int_equal(fetch_size, partial_size);
		assert_memory_equal(buf, vbuf, partial_size);
	}

	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	D_FREE(buf);
	rc = dfs_umount(dfs_mt);
	assert_int_equal(rc, 0);

	rc = daos_cont_close(co_hdl, NULL);
	assert_int_equal(rc, 0);

	rc = daos_cont_destroy(arg->pool.poh, co_uuid, 1, NULL);
	assert_int_equal(rc, 0);

	while (idx > 0)
		rebuild_add_back_tgts(arg, ranks[--idx], NULL, 1);
}

static void
degrade_dfs_fail_data_s0(void **state)
{
	int shard = 0;

	degrade_dfs_internal(state, &shard, 1);
}

static void
degrade_dfs_fail_data_s1(void **state)
{
	int shard = 1;

	degrade_dfs_internal(state, &shard, 1);
}

static void
degrade_dfs_fail_data_s3(void **state)
{
	int shard = 3;

	degrade_dfs_internal(state, &shard, 1);
}

static void
degrade_dfs_fail_2data_s0s1(void **state)
{
	int shards[2];

	shards[0] = 0;
	shards[1] = 1;
	degrade_dfs_internal(state, shards, 2);
}

static void
degrade_dfs_fail_2data_s0s2(void **state)
{
	int shards[2];

	shards[0] = 0;
	shards[1] = 2;
	degrade_dfs_internal(state, shards, 2);
}

static void
degrade_dfs_fail_2data_s0s3(void **state)
{
	int shards[2];

	shards[0] = 0;
	shards[1] = 3;
	degrade_dfs_internal(state, shards, 2);
}

static void
degrade_dfs_fail_2data_s1s2(void **state)
{
	int shards[2];

	shards[0] = 1;
	shards[1] = 2;
	degrade_dfs_internal(state, shards, 2);
}

static void
degrade_dfs_fail_2data_s1s3(void **state)
{
	int shards[2];

	shards[0] = 1;
	shards[1] = 3;
	degrade_dfs_internal(state, shards, 2);
}

static void
degrade_dfs_fail_2data_s2s3(void **state)
{
	int shards[2];

	shards[0] = 2;
	shards[1] = 3;
	degrade_dfs_internal(state, shards, 2);
}

static void
degrade_dfs_fail_data_parity_s0p1(void **state)
{
	int shards[2];

	shards[0] = 0;
	shards[1] = 5;
	degrade_dfs_internal(state, shards, 2);
}

static void
degrade_dfs_fail_data_parity_s3p1(void **state)
{
	int shards[2];

	shards[0] = 3;
	shards[1] = 5;
	degrade_dfs_internal(state, shards, 2);
}

static void
degrade_dfs_fail_data_parity_s2p1(void **state)
{
	int shards[2];

	shards[0] = 2;
	shards[1] = 5;
	degrade_dfs_internal(state, shards, 2);
}

static void
degrade_dfs_fail_data_parity_s0p0(void **state)
{
	int shards[2];

	shards[0] = 0;
	shards[1] = 4;
	degrade_dfs_internal(state, shards, 2);
}

static void
degrade_dfs_fail_data_parity_s2p0(void **state)
{
	int shards[2];

	shards[0] = 2;
	shards[1] = 4;
	degrade_dfs_internal(state, shards, 2);
}

static void
degrade_dfs_fail_data_parity_s3p0(void **state)
{
	int shards[2];

	shards[0] = 3;
	shards[1] = 4;
	degrade_dfs_internal(state, shards, 2);
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
