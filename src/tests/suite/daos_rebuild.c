/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
 * tests/suite/daos_rebuild.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

#define KEY_NR		1000
#define OBJ_NR		10
#define OBJ_CLS		DAOS_OC_R3S_RW
#define OBJ_REPLICAS	3
#define DEFAULT_FAIL_TGT 0
#define REBUILD_POOL_SIZE	(4ULL << 30)
static void
rebuild_exclude_tgt(test_arg_t **args, int arg_cnt, d_rank_t rank,
		    int tgt_idx, bool kill)
{
	int i;

	if (kill) {
		print_message("calling daos_kill_server()\n");
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
		print_message("calling daos_exclude_target()\n");
		daos_exclude_target(args[i]->pool.pool_uuid,
				    args[i]->group, &args[i]->pool.svc,
				    rank, tgt_idx);
		sleep(2);
	}
}

static void
rebuild_add_tgt(test_arg_t **args, int args_cnt, d_rank_t rank,
		int tgt_idx)
{
	int i;

	for (i = 0; i < args_cnt; i++) {
		if (!args[i]->pool.destroyed) {
			print_message("daos_add_target()\n");
			daos_add_target(args[i]->pool.pool_uuid,
					args[i]->group,
					&args[i]->pool.svc,
					rank, tgt_idx);
			}
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
			print_message("calling rebuild_exclude_tgt()\n");
			rebuild_exclude_tgt(args, args_cnt, failed_ranks[i],
					    failed_tgts ? failed_tgts[i] : -1,
					    kill);
			/* Sleep 5 seconds to make sure the rebuild start */
			sleep(5);
			print_message("rebuild should have already started\n");
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
	/* Add back the target if it is not being killed */
	if (!kill && args[0]->myrank == 0) {
		for (i = 0; i < rank_nr; i++) {
			print_message("rebuild_add_tgt()\n");
			rebuild_add_tgt(args, args_cnt, failed_ranks[i],
					failed_tgts ? failed_tgts[i] : -1);
		}
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

	print_message("update obj %d eph "DF_U64" before rebuild\n", oids_nr,
		      eph);

	for (i = 0; i < oids_nr; i++) {
		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);
		if (i == punch_idx) {
			print_message("punching obj "DF_U64"\n", oids[i].lo);
			punch_obj(DAOS_TX_NONE, &req);
		} else {
			print_message("create records on obj "DF_U64"\n",
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

	print_message("rebuild_io_validate\n");
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

	print_message("Pause to check object layout before server eviction\n");
	sleep(30);

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
		print_message("daos_cont_open()\n");
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
rebuild_offline(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;
	unsigned int	required_nodes = 3;

	print_message("rebuild_offline test - %u server nodes, 2-way replica\n",
			required_nodes);
	if (!test_runable(arg, required_nodes))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R2S_SPEC_RANK, 0, arg->myrank);
		print_message("rank %d set for oid:"DF_U64"."DF_U64"\n",
				ranks_to_kill[0], oids[i].hi, oids[i].lo);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}
	rebuild_io(arg, oids, OBJ_NR);

	arg->rebuild_pre_cb = rebuild_pool_disconnect_internal;
	arg->rebuild_post_cb = rebuild_pool_connect_internal;

	rebuild_targets(&arg, 1, ranks_to_kill, NULL, 1, true);

	arg->rebuild_pre_cb = NULL;
	arg->rebuild_post_cb = NULL;

	rebuild_io_validate(arg, oids, OBJ_NR, false);
}


/** create a new pool/container for each test */
static const struct CMUnitTest rebuild_tests[] = {
//	{"REBUILD1: rebuild small rec mulitple dkeys",
//	 rebuild_dkeys, NULL, test_case_teardown},
//	{"REBUILD2: rebuild small rec multiple akeys",
//	 rebuild_akeys, NULL, test_case_teardown},
//	{"REBUILD3: rebuild small rec multiple indexes",
//	 rebuild_indexes, NULL, test_case_teardown},
//	{"REBUILD4: rebuild small rec multiple keys/indexes",
//	 rebuild_multiple, NULL, test_case_teardown},
//	{"REBUILD5: rebuild large rec single index",
//	 rebuild_large_rec, NULL, test_case_teardown},
//	{"REBUILD6: rebuild multiple objects",
//	 rebuild_objects, NULL, test_case_teardown},
//	{"REBUILD7: drop rebuild scan reply",
//	rebuild_drop_scan, NULL, test_case_teardown},
//	{"REBUILD8: retry rebuild for not ready",
//	rebuild_retry_rebuild, NULL, test_case_teardown},
//	{"REBUILD9: drop rebuild obj reply",
//	rebuild_drop_obj, NULL, test_case_teardown},
//	{"REBUILD10: rebuild multiple pools",
//	rebuild_multiple_pools, NULL, test_case_teardown},
//	{"REBUILD11: rebuild update failed",
//	rebuild_update_failed, NULL, test_case_teardown},
//	{"REBUILD12: retry rebuild for pool stale",
//	rebuild_retry_for_stale_pool, NULL, test_case_teardown},
//	{"REBUILD13: rebuild with container destroy",
//	rebuild_destroy_container, NULL, test_case_teardown},
//	{"REBUILD14: rebuild with container close",
//	rebuild_close_container, NULL, test_case_teardown},
//	{"REBUILD15: rebuild with pool destroy during scan",
//	rebuild_destroy_pool_during_scan, NULL, test_case_teardown},
//	{"REBUILD16: rebuild with pool destroy during rebuild",
//	rebuild_destroy_pool_during_rebuild, NULL, test_case_teardown},
//	{"REBUILD17: rebuild iv tgt fail",
//	rebuild_iv_tgt_fail, NULL, test_case_teardown},
//	{"REBUILD18: rebuild tgt start fail",
//	rebuild_tgt_start_fail, NULL, test_case_teardown},
//	{"REBUILD19: rebuild send objects failed",
//	 rebuild_send_objects_fail, NULL, test_case_teardown},
//	{"REBUILD20: rebuild with master change during scan",
//	rebuild_master_change_during_scan, NULL, test_case_teardown},
//	{"REBUILD21: rebuild with master change during rebuild",
//	rebuild_master_change_during_rebuild, NULL, test_case_teardown},
//	{"REBUILD22: rebuild no space failure",
//	rebuild_nospace, NULL, test_case_teardown},
//	{"REBUILD23: rebuild multiple tgts",
//	rebuild_multiple_tgts, NULL, test_case_teardown},
//	{"REBUILD24: disconnect pool during scan",
//	 rebuild_tgt_pool_disconnect_in_scan, NULL, test_case_teardown},
//	{"REBUILD25: disconnect pool during rebuild",
//	 rebuild_tgt_pool_disconnect_in_rebuild, NULL, test_case_teardown},
//	{"REBUILD26: connect pool during scan for offline rebuild",
//	 rebuild_offline_pool_connect_in_scan, NULL, test_case_teardown},
//	{"REBUILD27: connect pool during rebuild for offline rebuild",
//	 rebuild_offline_pool_connect_in_rebuild, NULL, test_case_teardown},
	{"REBUILD28: offline rebuild",
	rebuild_offline, NULL, test_case_teardown},
//	{"REBUILD29: rebuild with master failure",
//	 rebuild_master_failure, NULL, test_case_teardown},
//	{"REBUILD30: rebuild with two failures",
//	 rebuild_multiple_failures, NULL, test_case_teardown},
//	{"REBUILD31: rebuild fail all replicas before rebuild",
//	 rebuild_fail_all_replicas_before_rebuild, NULL, test_case_teardown},
//	{"REBUILD32: rebuild fail all replicas",
//	 rebuild_fail_all_replicas, NULL, test_case_teardown},
//	{"REBUILD33: multi-pools rebuild concurrently",
//	 multi_pools_rebuild_concurrently, NULL, test_case_teardown},
};

int
rebuild_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, true, REBUILD_POOL_SIZE,
			  NULL);
}

int
run_daos_rebuild_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(rebuild_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests(rebuild_tests, ARRAY_SIZE(rebuild_tests),
				REBUILD_POOL_SIZE, sub_tests, sub_tests_size,
				NULL, NULL);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
