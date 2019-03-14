/**
 * (C) Copyright 2019 Intel Corporation.
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
 * This file is part of daos
 *
 * tests/suite/daos_demo_rebuild.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

#define KEY_NR		1000
#define OBJ_NR		4
#define OBJ_REPLICAS	2
#define DEFAULT_FAIL_TGT 0
#define REBUILD_POOL_SIZE	(4ULL << 30)
static void
rebuild_exclude_tgt(test_arg_t **args, int arg_cnt, d_rank_t rank,
		    int tgt_idx, bool kill)
{
	int i;

	if (kill) {
		daos_kill_server(args[0], args[0]->pool.pool_uuid,
				 args[0]->group, &args[0]->pool.svc,
				 rank);
		sleep(5);
		/* If one rank is killed, then it has to exclude all
		 * targets on this rank.
		 **/
		D_ASSERT(tgt_idx == -1);
	}

	for (i = 0; i < arg_cnt; i++) {
		daos_exclude_target(args[i]->pool.pool_uuid,
				    args[i]->group, &args[i]->pool.svc,
				    rank, tgt_idx);
		sleep(2);
	}
}

static void
rebuild_targets(test_arg_t **args, int args_cnt, d_rank_t *failed_ranks,
		int *failed_tgts, int rank_nr, bool kill)
{
	int	i;

	for (i = 0; i < args_cnt; i++) {
		if (args[i]->rebuild_pre_cb)
			args[i]->rebuild_pre_cb(args[i]);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	/** exclude the target from the pool */
	if (args[0]->myrank == 0) {
		for (i = 0; i < rank_nr; i++) {
			rebuild_exclude_tgt(args, args_cnt, failed_ranks[i],
					    failed_tgts ? failed_tgts[i] : -1,
					    kill);
			/* Sleep 5 seconds to make sure the rebuild start */
			sleep(5);
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);

	for (i = 0; i < args_cnt; i++)
		if (args[i]->rebuild_cb)
			args[i]->rebuild_cb(args[i]);

	if (args[0]->myrank == 0) {
		test_rebuild_wait(args, args_cnt);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	for (i = 0; i < args_cnt; i++) {
		if (args[i]->rebuild_post_cb)
			args[i]->rebuild_post_cb(args[i]);
	}
}

static int
rebuild_io_obj_internal(struct ioreq *req, bool validate, daos_epoch_t eph,
			daos_epoch_t validate_eph)
{
#define BULK_SIZE	5000
#define REC_SIZE	64
#define LARGE_KEY_SIZE	(512 * 1024)
#define DKEY_LOOP	3
#define AKEY_LOOP	3
#define REC_LOOP	10
	char	dkey[32];
	char	akey[32];
	char	data[REC_SIZE];
	char	data_verify[REC_SIZE];
	char	*large_key;
	int	akey_punch_idx = 1;
	int	dkey_punch_idx = 1;
	int	rec_punch_idx = 2;
	int	j;
	int	k;
	int	l;

	D_ALLOC(large_key, LARGE_KEY_SIZE);
	if (large_key == NULL)
		return -DER_NOMEM;
	memset(large_key, 'L', LARGE_KEY_SIZE - 1);

	for (j = 0; j < DKEY_LOOP; j++) {
		req->iod_type = DAOS_IOD_ARRAY;
		/* small records */
		sprintf(dkey, "dkey_%d", j);
		sprintf(data, "%s_"DF_U64, "data", eph);
		sprintf(data_verify, "%s_"DF_U64, "data", validate_eph);
		for (k = 0; k < AKEY_LOOP; k++) {
			sprintf(akey, "akey_%d", k);
			for (l = 0; l < REC_LOOP; l++) {
				if (validate) {
					/* How to verify punch? XXX */
					if (k == akey_punch_idx ||
					    j == dkey_punch_idx ||
					    l == rec_punch_idx)
						continue;
					memset(data, 0, REC_SIZE);
					if (l == 7)
						lookup_single(large_key, akey,
							      l, data, REC_SIZE,
							      DAOS_TX_NONE,
							      req);
					else
						lookup_single(dkey, akey, l,
							      data, REC_SIZE,
							      DAOS_TX_NONE,
							      req);
					assert_memory_equal(data, data_verify,
						    strlen(data_verify));
				} else {
					if (l == 7)
						insert_single(large_key, akey,
							l, data,
							strlen(data) + 1,
							DAOS_TX_NONE, req);
					else if (l == rec_punch_idx)
						punch_single(dkey, akey, l,
							     DAOS_TX_NONE,
							     req);
					else
						insert_single(dkey, akey, l,
							data, strlen(data) + 1,
							DAOS_TX_NONE, req);
				}
			}

			/* Punch akey */
			if (k == akey_punch_idx && !validate)
				punch_akey(dkey, akey, DAOS_TX_NONE, req);
		}

		/* large records */
		for (k = 0; k < 2; k++) {
			char bulk[BULK_SIZE+10];
			char compare[BULK_SIZE];

			sprintf(akey, "akey_bulk_%d", k);
			memset(compare, 'a', BULK_SIZE);
			for (l = 0; l < 5; l++) {
				if (validate) {
					/* How to verify punch? XXX */
					if (k == akey_punch_idx ||
					    j == dkey_punch_idx)
						continue;
					memset(bulk, 0, BULK_SIZE);
					lookup_single(dkey, akey, l,
						      bulk, BULK_SIZE + 10,
						      DAOS_TX_NONE, req);
					assert_memory_equal(bulk, compare,
							    BULK_SIZE);
				} else {
					memset(bulk, 'a', BULK_SIZE);
					insert_single(dkey, akey, l,
						      bulk, BULK_SIZE,
						      DAOS_TX_NONE, req);
				}
			}

			/* Punch akey */
			if (k == akey_punch_idx && !validate)
				punch_akey(dkey, akey, DAOS_TX_NONE, req);
		}

		/* Punch dkey */
		if (j == dkey_punch_idx && !validate)
			punch_dkey(dkey, DAOS_TX_NONE, req);

		/* single record */
		sprintf(data, "%s_"DF_U64, "single_data", eph);
		sprintf(data_verify, "%s_"DF_U64, "single_data",
			validate_eph);
		req->iod_type = DAOS_IOD_SINGLE;
		sprintf(dkey, "dkey_single_%d", j);
		if (validate) {
			memset(data, 0, REC_SIZE);
			lookup_single(dkey, "akey_single", 0, data, REC_SIZE,
				      DAOS_TX_NONE, req);
			assert_memory_equal(data, data_verify,
					    strlen(data_verify));
		} else {
			insert_single(dkey, "akey_single", 0, data,
				      strlen(data) + 1, DAOS_TX_NONE, req);
		}
	}

	D_FREE(large_key);
	return 0;
}

static void
rebuild_io(test_arg_t *arg, daos_obj_id_t *oids, int oids_nr)
{
	struct ioreq	req;
	daos_epoch_t	eph = arg->hce + arg->index * 2 + 1;
	int		i;
	int		punch_idx = 1;

	for (i = 0; i < oids_nr; i++) {
		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);
		if (i == punch_idx) {
			print_message("punching object "DF_U64"\n", oids[i].lo);
			punch_obj(DAOS_TX_NONE, &req);
		} else {
			print_message("creating records on object "DF_U64"\n",
					oids[i].lo);
			rebuild_io_obj_internal((&req), false, eph, -1);
		}
		ioreq_fini(&req);
	}
}

static void
rebuild_io_validate(test_arg_t *arg, daos_obj_id_t *oids, int oids_nr,
		    bool discard)
{
	struct ioreq	req;
	daos_epoch_t	eph = arg->hce + arg->index * 2 + 1;
	int		i;
	int		punch_idx = 1;

	arg->fail_loc = DAOS_OBJ_SPECIAL_SHARD;
	/* Validate data for each shard */
	for (i = 0; i < OBJ_REPLICAS; i++) {
		int j;

		arg->fail_value = i;
		for (j = 0; j < oids_nr; j++) {
			ioreq_init(&req, arg->coh, oids[j], DAOS_IOD_ARRAY,
				   arg);

			/* how to validate punch object XXX */
			if (j != punch_idx)
				/* Validate eph data */
				rebuild_io_obj_internal((&req), true, eph, eph);

			ioreq_fini(&req);
		}
	}

	arg->fail_loc = 0;
	arg->fail_value = 0;
}


static int
rebuild_pool_disconnect_internal(void *data)
{
	test_arg_t	*arg = data;
	int		rc = 0;
	int		rc_reduce = 0;

	/* Close cont and disconnect pool */
	rc = daos_cont_close(arg->coh, NULL);
	if (arg->multi_rank) {
		MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN,
			      MPI_COMM_WORLD);
		rc = rc_reduce;
	}
	print_message("container close "DF_UUIDF"\n",
		      DP_UUID(arg->co_uuid));
	if (rc) {
		print_message("failed to close container "DF_UUIDF
			      ": %d\n", DP_UUID(arg->co_uuid), rc);
		return rc;
	}

	arg->coh = DAOS_HDL_INVAL;
	rc = daos_pool_disconnect(arg->pool.poh, NULL /* ev */);
	if (rc)
		print_message("failed to disconnect pool "DF_UUIDF
			      ": %d\n", DP_UUID(arg->pool.pool_uuid), rc);

	print_message("pool disconnect "DF_UUIDF"\n",
		      DP_UUID(arg->pool.pool_uuid));

	arg->pool.poh = DAOS_HDL_INVAL;
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}


static int
rebuild_pool_connect_internal(void *data)
{
	test_arg_t	*arg = data;
	int		rc;

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
				       &arg->pool.svc, DAOS_PC_RW,
				       &arg->pool.poh, &arg->pool.pool_info,
				       NULL /* ev */);
		if (rc)
			print_message("daos_pool_connect failed, rc: %d\n", rc);

		print_message("pool connect "DF_UUIDF"\n",
			       DP_UUID(arg->pool.pool_uuid));
	}
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** broadcast pool info */
	if (arg->multi_rank) {
		MPI_Bcast(&arg->pool.pool_info, sizeof(arg->pool.pool_info),
			  MPI_CHAR, 0, MPI_COMM_WORLD);
		handle_share(&arg->pool.poh, HANDLE_POOL, arg->myrank,
			     arg->pool.poh, 0);
	}

	/** open container */
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = daos_cont_open(arg->pool.poh, arg->co_uuid, DAOS_COO_RW,
				    &arg->coh, &arg->co_info, NULL);
		if (rc)
			print_message("daos_cont_open failed, rc: %d\n", rc);

		print_message("container open "DF_UUIDF"\n",
			       DP_UUID(arg->co_uuid));
	}
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** broadcast container info */
	if (arg->multi_rank) {
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		handle_share(&arg->coh, HANDLE_CO, arg->myrank, arg->pool.poh,
			     0);
	}

	return 0;
}

static void
rebuild_get_obj_layout(test_arg_t *arg, daos_obj_id_t oid)
{
	struct daos_obj_layout	*layout;
	int			rc;
	int			i;
	int			j;


	rc = daos_obj_layout_get(arg->coh, oid, &layout);
	if (rc) {
		print_message("daos_obj_layout_get failed, rc: %d\n", rc);
		return;
	}

	/* Print the object layout */
	for (i = 0; i < layout->ol_nr; i++) {
		struct daos_obj_shard *shard;

		shard = layout->ol_shards[i];
		for (j = 0; j < shard->os_replica_nr; j++)
			print_message("i:%d rank: %d tgt_id: %d\n",
				      j, shard->os_ids[j].ti_rank,
				      shard->os_ids[j].ti_tgt);
	}

	daos_obj_layout_free(layout);
}

static void
rebuild_full_node(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	unsigned int	required_nodes = 3;
	int		failed_tgt = 0;
	int		i;

	if (!test_runable(arg, required_nodes))
		return;

	print_message("%u server nodes, 2-way object replica, %d objects\n",
		      required_nodes, OBJ_NR);

	for (i = 0; i < OBJ_NR; i++) {
		/* Alternate targets for object creation */
		if (i % 2 == 0)
			failed_tgt = 0;
		else
			failed_tgt = 1;
		oids[i] = dts_oid_gen(DAOS_OC_R2S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
		oids[i] = dts_oid_set_tgt(oids[i], failed_tgt);
		print_message("Object %d created\n", i);
		print_message("oid:"DF_U64"."DF_U64", rank:%d, tgt:%d\n",
			      oids[i].hi, oids[i].lo, ranks_to_kill[0],
			      failed_tgt);
		/* Get object layout */
		rebuild_get_obj_layout(arg, oids[i]);
	}
	rebuild_io(arg, oids, OBJ_NR);

	arg->rebuild_pre_cb = rebuild_pool_disconnect_internal;
	arg->rebuild_post_cb = rebuild_pool_connect_internal;

	rebuild_targets(&arg, 1, ranks_to_kill, NULL, 1, true);

	arg->rebuild_pre_cb = NULL;
	arg->rebuild_post_cb = NULL;

	rebuild_io_validate(arg, oids, OBJ_NR, false);

	/* Get final object layout */
	for (i = 0; i < OBJ_NR; i++) {
		print_message("oid:"DF_U64"."DF_U64" layout:\n",
			      oids[i].hi, oids[i].lo);
		rebuild_get_obj_layout(arg, oids[i]);
	}
}

static void
rebuild_partial_node(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	unsigned int	required_nodes = 3;
	int		failed_tgt = 0;
	int		i;

	if (!test_runable(arg, required_nodes))
		return;

	print_message("%u server nodes, 2-way object replica, %d objects\n",
		      required_nodes, OBJ_NR);

	for (i = 0; i < OBJ_NR; i++) {
		/* Alternate targets for object creation */
		if (i % 2 == 0)
			failed_tgt = 0;
		else
			failed_tgt = 1;
		oids[i] = dts_oid_gen(DAOS_OC_R2S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
		oids[i] = dts_oid_set_tgt(oids[i], failed_tgt);
		print_message("Object %d created\n", i);
		print_message("oid:"DF_U64"."DF_U64", rank:%d, tgt:%d\n",
			      oids[i].hi, oids[i].lo, ranks_to_kill[0],
			      failed_tgt);
		/* Get object layout */
		rebuild_get_obj_layout(arg, oids[i]);
	}
	rebuild_io(arg, oids, OBJ_NR);

	arg->rebuild_pre_cb = rebuild_pool_disconnect_internal;
	arg->rebuild_post_cb = rebuild_pool_connect_internal;

	rebuild_targets(&arg, 1, ranks_to_kill, &failed_tgt, 1, false);

	arg->rebuild_pre_cb = NULL;
	arg->rebuild_post_cb = NULL;

	rebuild_io_validate(arg, oids, OBJ_NR, false);

	/* Get final object layout */
	for (i = 0; i < OBJ_NR; i++) {
		print_message("oid:"DF_U64"."DF_U64" layout:\n",
			      oids[i].hi, oids[i].lo);
		rebuild_get_obj_layout(arg, oids[i]);
	}
}

/** create a new pool/container for each test */
static const struct CMUnitTest demo_rebuild_tests[] = {
	{"REBUILD_DEMO1: single storage target failure rebuild",
	rebuild_partial_node, NULL, test_case_teardown},
	{"REBUILD_DEMO2: full server node failure rebuild",
	rebuild_full_node, NULL, test_case_teardown},

};

int
demo_rebuild_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, true, REBUILD_POOL_SIZE,
			  NULL);
}

int
run_daos_demo_rebuild_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(demo_rebuild_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests(demo_rebuild_tests, ARRAY_SIZE(demo_rebuild_tests),
				REBUILD_POOL_SIZE, sub_tests, sub_tests_size,
				NULL, NULL);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
