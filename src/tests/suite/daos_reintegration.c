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
 * tests/suite/daos_reintegration.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

#define KEY_NR		1000
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
rebuild_add_tgt(test_arg_t **args, int args_cnt, d_rank_t rank,
		int tgt_idx)
{
	int i;

	for (i = 0; i < args_cnt; i++) {
		if (!args[i]->pool.destroyed)
			daos_add_target_force(args[i]->pool.pool_uuid,
					      args[i]->group,
					      &args[i]->pool.svc,
					      rank, tgt_idx);
	}
}

static void
rebuild_targets(test_arg_t **args, int args_cnt, d_rank_t *failed_ranks,
		int *failed_tgts, int rank_nr, bool kill)
{
	int	i;

	for (i = 0; i < args_cnt; i++)
		if (args[i]->rebuild_pre_cb)
			args[i]->rebuild_pre_cb(args[i]);

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

	if (args[0]->myrank == 0)
		test_rebuild_wait(args, args_cnt);

	MPI_Barrier(MPI_COMM_WORLD);
	for (i = 0; i < args_cnt; i++)
		if (args[i]->rebuild_post_cb)
			args[i]->rebuild_post_cb(args[i]);
}

static void
rebuild_single_pool_target(test_arg_t *arg, d_rank_t failed_rank,
			   int failed_tgt)
{
	rebuild_targets(&arg, 1, &failed_rank, &failed_tgt, 1, false);
}

static void
rebuild_add_back_tgts(test_arg_t **args, int args_nr, d_rank_t *failed_ranks,
		      int *failed_tgts, int nr)
{
	MPI_Barrier(MPI_COMM_WORLD);
	/* Add back the target if it is not being killed */
	if (args[0]->myrank == 0) {
		int i;

		for (i = 0; i < nr; i++)
			rebuild_add_tgt(args, args_nr, failed_ranks[i],
					failed_tgts ? failed_tgts[i] : -1);
	}
	MPI_Barrier(MPI_COMM_WORLD);
}

static void
reintegrate_test(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			tgt = DEFAULT_FAIL_TGT;
	int			i;

	if (!test_runable(arg, 6))
		return;

	oid = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	for (i = 0; i < KEY_NR; i++) {
		char	key[16];

		sprintf(key, "%d", i);
		insert_single(key, "a_key", 0, "data", strlen("data") + 1,
			      DAOS_TX_NONE, &req);
	}
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt);

	rebuild_add_back_tgts(&arg, 1, ranks_to_kill, &tgt, 1);
}

static const struct CMUnitTest reintegration_tests[] = {
	{"REINTEGRATE 1: TODO",
	 reintegrate_test, NULL, test_case_teardown},
};

int
reintegration_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, true, REBUILD_POOL_SIZE,
			  NULL);
}

int
run_daos_reintegration_test(int rank, int size, int *sub_tests,
			    int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(reintegration_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests(reintegration_tests,
				ARRAY_SIZE(reintegration_tests),
				REBUILD_POOL_SIZE, sub_tests, sub_tests_size,
				NULL, NULL);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
