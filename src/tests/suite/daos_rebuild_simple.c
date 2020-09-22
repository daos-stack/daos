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

#define KEY_NR		10
#define OBJ_NR		10
#define OBJ_CLS		OC_RP_3G1
#define OBJ_REPLICAS	3
#define DEFAULT_FAIL_TGT 0
#define REBUILD_POOL_SIZE	(4ULL << 30)
#define REBUILD_SUBTEST_POOL_SIZE (1ULL << 30)
#define REBUILD_SMALL_POOL_SIZE (1ULL << 28)

#define DATA_SIZE	(1048576 * 2 + 512)
static void
rebuild_dkeys(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	d_rank_t		kill_rank = 0;
	int			i;

	if (!test_runable(arg, 4))
		return;

	oid = dts_oid_gen(arg->obj_class, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	for (i = 0; i < 5; i++) {
		char	key[16];
		daos_recx_t recx;
		char	data[DATA_SIZE];

		sprintf(key, "dkey_0_%d", i);
		insert_single(key, "a_key", 0, "data", strlen("data") + 1,
			      DAOS_TX_NONE, &req);

		sprintf(key, "dkey_0_1M_%d", i);
		recx.rx_idx = 0;
		recx.rx_nr = DATA_SIZE;

		memset(data, 'a', DATA_SIZE);
		insert_recxs(key, "a_key_1M", 1, DAOS_TX_NONE, &recx, 1,
			     data, DATA_SIZE, &req);
	}

	kill_rank = get_killing_rank_by_oid(arg, oid, true);
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, kill_rank, -1, false);

	reintegrate_single_pool_target(arg, kill_rank, -1);
}

static void
rebuild_akeys(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	d_rank_t		kill_rank = 0;
	int			tgt = -1;
	int			i;

	if (!test_runable(arg, 4))
		return;

	oid = dts_oid_gen(arg->obj_class, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	for (i = 0; i < KEY_NR; i++) {
		char		key[16];
		daos_recx_t	recx;
		char		data[DATA_SIZE];

		sprintf(key, "%d", i);
		insert_single("dkey_1_0", key, 0, "data", strlen("data") + 1,
			      DAOS_TX_NONE, &req);

		sprintf(key, "dkey_1_1M_%d", i);
		recx.rx_idx = 0;
		recx.rx_nr = DATA_SIZE;

		memset(data, 'a', DATA_SIZE);
		insert_recxs(key, "a_key_1M", 1, DAOS_TX_NONE, &recx, 1,
			     data, DATA_SIZE, &req);

	}
	kill_rank = get_killing_rank_by_oid(arg, oid, false);
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, kill_rank, tgt, false);

	reintegrate_single_pool_target(arg, kill_rank, tgt);
}

static void
rebuild_indexes(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			tgt = DEFAULT_FAIL_TGT;
	int			i;
	int			j;

	if (!test_runable(arg, 4))
		return;

	oid = dts_oid_gen(arg->obj_class, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 2000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      2000, DP_OID(oid));
	for (i = 0; i < KEY_NR; i++) {
		char	key[16];

		sprintf(key, "dkey_2_%d", i);
		for (j = 0; j < 20; j++)
			insert_single(key, "a_key", j, "data",
				      strlen("data") + 1, DAOS_TX_NONE, &req);
	}
	ioreq_fini(&req);

	/* Rebuild rank 1 */
	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	reintegrate_single_pool_target(arg, ranks_to_kill[0], tgt);
}

static void
rebuild_snap_update_recs(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	daos_recx_t	recx;
	int		tgt = DEFAULT_FAIL_TGT;
	char		string[100] = { 0 };
	daos_epoch_t	snap_epoch[5];
	int		i;
	int		rc;

	if (!test_runable(arg, 4))
		return;

	oid = dts_oid_gen(arg->obj_class, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 5; i++)
		sprintf(string + strlen(string), "old-snap%d", i);

	recx.rx_idx = 0;
	recx.rx_nr = strlen(string);
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1, string,
		     strlen(string) + 1, &req);

	for (i = 0; i < 5; i++) {
		char data[20] = { 0 };

		/* Update string for each snapshot */
		daos_cont_create_snap(arg->coh, &snap_epoch[i], NULL, NULL);
		sprintf(data, "new-snap%d", i);
		recx.rx_idx = i * strlen(data);
		recx.rx_nr = strlen(data);
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1, data,
			      strlen(data) + 1, &req);
	}

	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	for (i = 0; i < 5; i++) {
		rc = daos_obj_verify(arg->coh, oid, snap_epoch[i]);
		assert_true(rc == 0 || rc == -DER_NOSYS);
	}

	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	assert_true(rc == 0 || rc == -DER_NOSYS);

	ioreq_fini(&req);

	reintegrate_single_pool_target(arg, ranks_to_kill[0], tgt);
}

static void
rebuild_snap_punch_recs(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	daos_recx_t	recx;
	int		tgt = DEFAULT_FAIL_TGT;
	char		string[100];
	daos_epoch_t	snap_epoch[5];
	int		i;
	int		rc;

	if (!test_runable(arg, 4))
		return;

	oid = dts_oid_gen(arg->obj_class, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 5; i++)
		sprintf(string + strlen(string), "old-snap%d", i);

	recx.rx_idx = 0;
	recx.rx_nr = strlen(string);
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1, string,
		     strlen(string) + 1, &req);

	for (i = 0; i < 5; i++) {
		/* punch string */
		daos_cont_create_snap(arg->coh, &snap_epoch[i], NULL, NULL);
		recx.rx_idx = i * 9; /* strlen("old-snap%d") */
		recx.rx_nr = 9;
		punch_recxs("d_key", "a_key", &recx, 1, DAOS_TX_NONE, &req);
	}

	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	for (i = 0; i < 5; i++) {
		rc = daos_obj_verify(arg->coh, oid, snap_epoch[i]);
		assert_true(rc == 0 || rc == -DER_NOSYS);
	}

	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	assert_true(rc == 0 || rc == -DER_NOSYS);

	ioreq_fini(&req);

	reintegrate_single_pool_target(arg, ranks_to_kill[0], tgt);
}

static void
rebuild_snap_update_keys(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		tgt = DEFAULT_FAIL_TGT;
	daos_epoch_t	snap_epoch[5];
	int		i;

	if (!test_runable(arg, 4))
		return;

	oid = dts_oid_gen(arg->obj_class, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	/* Insert dkey/akey by different snapshot */
	for (i = 0; i < 5; i++) {
		char dkey[20] = { 0 };
		char akey[20] = { 0 };

		/* Update string for each snapshot */
		daos_cont_create_snap(arg->coh, &snap_epoch[i], NULL, NULL);
		sprintf(dkey, "dkey_%d", i);
		sprintf(akey, "akey_%d", i);
		insert_single(dkey, "a_key", 0, "data", 1, DAOS_TX_NONE, &req);
		insert_single("dkey", akey, 0, "data", 1, DAOS_TX_NONE, &req);
	}

	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	daos_fail_loc_set(DAOS_OBJ_SPECIAL_SHARD);
	for (i = 0; i < OBJ_REPLICAS; i++) {
		uint32_t	number;
		daos_key_desc_t kds[10];
		daos_anchor_t	anchor = { 0 };
		char		buf[256];
		int		buf_len = 256;
		int		j;

		daos_fail_value_set(i);
		for (j = 0; j < 5; j++) {
			daos_handle_t	th_open;

			memset(&anchor, 0, sizeof(anchor));
			daos_tx_open_snap(arg->coh, snap_epoch[j], &th_open,
					  NULL);
			number = 10;
			enumerate_dkey(th_open, &number, kds, &anchor, buf,
				       buf_len, &req);

			assert_int_equal(number, j > 0 ? j+1 : 0);

			number = 10;
			memset(&anchor, 0, sizeof(anchor));
			enumerate_akey(th_open, "dkey", &number, kds, &anchor,
				       buf, buf_len, &req);

			assert_int_equal(number, j);
			daos_tx_close(th_open, NULL);
		}
		number = 10;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_dkey(DAOS_TX_NONE, &number, kds, &anchor, buf,
			       buf_len, &req);
		assert_int_equal(number, 6);

		number = 10;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_akey(DAOS_TX_NONE, "dkey", &number, kds, &anchor,
			       buf, buf_len, &req);
		assert_int_equal(number, 5);
	}

	ioreq_fini(&req);
	reintegrate_single_pool_target(arg, ranks_to_kill[0], tgt);
}

static void
rebuild_snap_punch_keys(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		tgt = DEFAULT_FAIL_TGT;
	daos_epoch_t	snap_epoch[5];
	int		i;

	if (!test_runable(arg, 4))
		return;

	oid = dts_oid_gen(arg->obj_class, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	/* Insert dkey/akey */
	for (i = 0; i < 5; i++) {
		char dkey[20] = { 0 };
		char akey[20] = { 0 };

		/* Update string for each snapshot */
		sprintf(dkey, "dkey_%d", i);
		sprintf(akey, "akey_%d", i);
		insert_single(dkey, "a_key", 0, "data", 1, DAOS_TX_NONE, &req);
		insert_single("dkey", akey, 0, "data", 1, DAOS_TX_NONE, &req);
	}

	/* Insert dkey/akey by different epoch */
	for (i = 0; i < 5; i++) {
		char dkey[20] = { 0 };
		char akey[20] = { 0 };

		daos_cont_create_snap(arg->coh, &snap_epoch[i], NULL, NULL);

		sprintf(dkey, "dkey_%d", i);
		sprintf(akey, "akey_%d", i);
		punch_dkey(dkey, DAOS_TX_NONE, &req);
		punch_akey("dkey", akey, DAOS_TX_NONE, &req);
	}

	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	daos_fail_loc_set(DAOS_OBJ_SPECIAL_SHARD);
	for (i = 0; i < OBJ_REPLICAS; i++) {
		daos_key_desc_t  kds[10];
		daos_anchor_t	 anchor;
		char		 buf[256];
		int		 buf_len = 256;
		uint32_t	 number;
		int		 j;

		daos_fail_value_set(i);
		for (j = 0; j < 5; j++) {
			daos_handle_t th_open;

			daos_tx_open_snap(arg->coh, snap_epoch[j], &th_open,
					  NULL);
			number = 10;
			memset(&anchor, 0, sizeof(anchor));
			enumerate_dkey(th_open, &number, kds, &anchor, buf,
				       buf_len, &req);
			assert_int_equal(number, 6 - j);

			number = 10;
			memset(&anchor, 0, sizeof(anchor));
			enumerate_akey(th_open, "dkey", &number, kds,
				       &anchor, buf, buf_len, &req);
			assert_int_equal(number, 5 - j);

			daos_tx_close(th_open, NULL);
		}

		number = 10;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_dkey(DAOS_TX_NONE, &number, kds, &anchor, buf,
			       buf_len, &req);
		assert_int_equal(number, 1);

		number = 10;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_akey(DAOS_TX_NONE, "dkey", &number, kds, &anchor,
			       buf, buf_len, &req);
		assert_int_equal(number, 0);
	}

	ioreq_fini(&req);
	reintegrate_single_pool_target(arg, ranks_to_kill[0], tgt);
}

static void
rebuild_snap_punch_empty(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		tgt = DEFAULT_FAIL_TGT;
	daos_epoch_t	snap_epoch;
	int		i;

	skip(); /** DAOS-4698 */

	if (!test_runable(arg, 4))
		return;

	oid = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/* insert a record */
	insert_single("d_key", "a_key", 0, "data", 1, DAOS_TX_NONE, &req);

	daos_cont_create_snap(arg->coh, &snap_epoch, NULL, NULL);

	punch_obj(DAOS_TX_NONE, &req);

	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	daos_fail_loc_set(DAOS_OBJ_SPECIAL_SHARD);
	for (i = 0; i < OBJ_REPLICAS; i++) {
		daos_key_desc_t  kds[10];
		daos_anchor_t	 anchor;
		daos_handle_t	 th_open;
		char		 buf[256];
		int		 buf_len = 256;
		uint32_t	 number;

		daos_fail_value_set(i);
		daos_tx_open_snap(arg->coh, snap_epoch, &th_open, NULL);
		number = 10;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_dkey(th_open, &number, kds, &anchor, buf, buf_len,
			       &req);
		assert_int_equal(number, 1);

		number = 10;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_akey(th_open, "d_key", &number, kds, &anchor, buf,
			       buf_len, &req);
		assert_int_equal(number, 1);

		daos_tx_close(th_open, NULL);

		number = 10;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_dkey(DAOS_TX_NONE, &number, kds, &anchor, buf,
			       buf_len, &req);
		assert_int_equal(number, 0);

		number = 10;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_akey(DAOS_TX_NONE, "d_key", &number, kds, &anchor,
			       buf, buf_len, &req);
		assert_int_equal(number, 0);
	}

	ioreq_fini(&req);
	reintegrate_single_pool_target(arg, ranks_to_kill[0], tgt);
}

static void
rebuild_multiple(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		tgt = DEFAULT_FAIL_TGT;
	int		i;
	int		j;
	int		k;

	if (!test_runable(arg, 4))
		return;

	oid = dts_oid_gen(arg->obj_class, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      1000, DP_OID(oid));
	for (i = 0; i < 10; i++) {
		char	dkey[16];

		sprintf(dkey, "dkey_3_%d", i);
		for (j = 0; j < 10; j++) {
			char	akey[16];

			sprintf(akey, "akey_%d", j);
			for (k = 0; k < 10; k++)
				insert_single(dkey, akey, k, "data",
					      strlen("data") + 1,
					      DAOS_TX_NONE, &req);
		}
	}
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);
}

#define LARGE_BUFFER_SIZE	(32 * 1024 * 4)
static void
rebuild_large_rec(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			tgt = DEFAULT_FAIL_TGT;
	int			i;
	char			buffer[LARGE_BUFFER_SIZE];

	if (!test_runable(arg, 4))
		return;

	oid = dts_oid_gen(arg->obj_class, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	memset(buffer, 'a', LARGE_BUFFER_SIZE);
	for (i = 0; i < KEY_NR; i++) {
		char	key[16];
		const char *lakey = "a_key_L";
		daos_size_t iod_size = 1;
		int	rx_nr = LARGE_BUFFER_SIZE;
		uint64_t offset = 0;
		void	*data = buffer;

		sprintf(key, "dkey_4_%d", i);

		insert_single(key, "a_key", 0, buffer, 5000, DAOS_TX_NONE,
			      &req);

		insert(key, 1, &lakey, &iod_size, &rx_nr,
		       &offset, &data, DAOS_TX_NONE, &req, 0);
	}
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);
}

static void
rebuild_objects(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		tgt = DEFAULT_FAIL_TGT;
	int		i;

	if (!test_runable(arg, 4))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(arg->obj_class, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
		oids[i] = dts_oid_set_tgt(oids[i], DEFAULT_FAIL_TGT);
	}

	rebuild_io(arg, oids, OBJ_NR);

	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	reintegrate_single_pool_target(arg, ranks_to_kill[0], tgt);
}

/** create a new pool/container for each test */
static const struct CMUnitTest rebuild_tests[] = {
	{"REBUILD1: rebuild small rec multiple dkeys",
	 rebuild_dkeys, rebuild_small_sub_setup, test_teardown},
	{"REBUILD2: rebuild small rec multiple akeys",
	 rebuild_akeys, rebuild_small_sub_setup, test_teardown},
	{"REBUILD3: rebuild small rec multiple indexes",
	 rebuild_indexes, rebuild_small_sub_setup, test_teardown},
	{"REBUILD4: rebuild small rec multiple keys/indexes",
	 rebuild_multiple, rebuild_small_sub_setup, test_teardown},
	{"REBUILD5: rebuild large rec single index",
	 rebuild_large_rec, rebuild_small_sub_setup, test_teardown},
	{"REBUILD6: rebuild records with multiple snapshots",
	 rebuild_snap_update_recs, rebuild_small_sub_setup, test_teardown},
	{"REBUILD7: rebuild punch/records with multiple snapshots",
	 rebuild_snap_punch_recs, rebuild_small_sub_setup, test_teardown},
	{"REBUILD8: rebuild keys with multiple snapshots",
	 rebuild_snap_update_keys, rebuild_small_sub_setup, test_teardown},
	{"REBUILD9: rebuild keys/punch with multiple snapshots",
	 rebuild_snap_punch_keys, rebuild_small_sub_setup, test_teardown},
	{"REBUILD10: rebuild multiple objects",
	 rebuild_objects, rebuild_sub_setup, test_teardown},
	{"REBUILD11: rebuild snapshotted punched object",
	 rebuild_snap_punch_empty, rebuild_small_sub_setup, test_teardown},
};

int
run_daos_rebuild_simple_test(int rank, int size, int *sub_tests,
			     int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(rebuild_tests);
		sub_tests = NULL;
	}

	run_daos_sub_tests_only("DAOS rebuild simple tests", rebuild_tests,
				ARRAY_SIZE(rebuild_tests), sub_tests,
				sub_tests_size);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
