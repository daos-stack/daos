/**
 *
 * (C) Copyright 2016 Intel Corporation.
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
#define DD_SUBSYS	DD_FAC(tests)
#include "daos_iotest.h"
#include <daos/pool.h>

#define KEY_NR	10
#define OBJ_NR	10

static bool
rebuild_runable(test_arg_t *arg, unsigned int required_tgts)
{
	daos_pool_info_t info;
	int		 rc;
	bool		 runable = true;

	if (arg->myrank == 0) {
		rc = daos_pool_query(arg->poh, NULL, &info, NULL);
		assert_int_equal(rc, 0);
		print_message("targets (%d/%d)\n", info.pi_ntargets,
			      info.pi_ndisabled);
		if (info.pi_ntargets - info.pi_ndisabled < required_tgts) {
			if (arg->myrank == 0)
				print_message("Not enough targets, skipping "
					      "(%d/%d)\n", info.pi_ntargets,
					      info.pi_ndisabled);
			runable = false;
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);
	return runable;
}

static void
rebuild_test_exclude_tgt(test_arg_t *arg, daos_rank_t rank, bool kill)
{
	daos_rank_list_t	ranks;
	int			rc;

	if (arg->myrank == 0) {
		if (kill) {
			daos_kill_server(arg->pool_uuid, arg->group, &arg->svc,
					 arg->poh, rank);
			sleep(5);
		} else {
			/** exclude the target from the pool */
			ranks.rl_nr.num = 1;
			ranks.rl_nr.num_out = 0;
			ranks.rl_ranks = &rank;
			rc = daos_pool_exclude(arg->pool_uuid, arg->group,
					       &arg->svc, &ranks, NULL);
			assert_int_equal(rc, 0);
			print_message("exclude rank %d wait 5 seconds\n", rank);
			sleep(5); /* Sleep 5 for starting rebuild */
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);
}

static void
rebuild_test_add_tgt(test_arg_t *arg, daos_rank_t rank)
{
	daos_rank_list_t	ranks;
	int			rc;

	/** exclude the target from the pool */
	if (arg->myrank == 0) {
		ranks.rl_nr.num = 1;
		ranks.rl_nr.num_out = 0;
		ranks.rl_ranks = &rank;
		rc = daos_pool_tgt_add(arg->pool_uuid, arg->group, &arg->svc,
				       &ranks, NULL);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);
}

static int
rebuild_wait(test_arg_t *arg, daos_rank_t failed_rank, bool concurrent_io)
{
	struct ioreq		req;
	daos_obj_id_t		oid;
	int			rc = 0;
	int			i;
	char			dkey[16];
	char			buf[10];

	/* Rebuild rank 0 */
	if (concurrent_io) {
		oid = dts_oid_gen(DAOS_OC_REPL_2_SMALL_RW, arg->myrank);
		ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
		/* Let's do I/O at the same time */
		print_message("insert %d dkey/rebuild_akey during rebuild\n",
			      KEY_NR);
		for (i = 0; i < KEY_NR; i++) {
			sprintf(dkey, "rebuild_%d_%d", i, failed_rank);
			insert_single(dkey, "rebuild_akey_in", 0,
				      "data", strlen("data") + 1,
				      0, &req);

			memset(buf, 0, 10);
			lookup_single(dkey, "rebuild_akey_in", 0, buf,
				      10, 0, &req);

			assert_memory_equal(buf, "data", strlen("data"));
			assert_int_equal(req.iod[0].iod_size,
					 strlen("data") + 1);
		}
	}

	while (arg->myrank == 0) {
		daos_pool_info_t	    pinfo;
		struct daos_rebuild_status *rst = &pinfo.pi_rebuild_st;

		memset(&pinfo, 0, sizeof(pinfo));
		rc = daos_pool_query(arg->poh, NULL, &pinfo, NULL);
		if (rc != 0) {
			print_message("query rebuild status failed: %d\n", rc);
			break;
		}

		if (rst->rs_version == 0) {
			print_message("No more rebuild\n");
			break;
		}

		assert_int_equal(rst->rs_errno, 0);
		if (rst->rs_done) {
			print_message("Rebuild (ver=%d) is done\n",
				       rst->rs_version);
			break;
		}

		print_message("wait for rebuild (ver=%u), "
			      "already rebuilt obj="DF_U64", rec="DF_U64"\n",
			      rst->rs_version, rst->rs_obj_nr, rst->rs_rec_nr);
		sleep(2);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	if (concurrent_io) {
		for (i = 0; i < KEY_NR; i++) {
			memset(buf, 0, 10);
			sprintf(dkey, "rebuild_%d_%d", i, failed_rank);
			lookup_single(dkey, "rebuild_akey_in", 0, buf,
				      10, 0, &req);
			if (strcmp(buf, "data") != 0)
				print_message("dkey %s\n", dkey);

			assert_memory_equal(buf, "data", strlen("data"));
			assert_int_equal(req.iod[0].iod_size,
					 strlen("data") + 1);
		}
		ioreq_fini(&req);
	}

	return rc;
}

static void
rebuild_targets(test_arg_t *arg, daos_rank_t *failed_ranks, int rank_nr,
		bool kill, bool concurrent_io)
{
	int	i;

	/** exclude the target from the pool */
	for (i = 0; i < rank_nr; i++) {
		rebuild_test_exclude_tgt(arg, failed_ranks[i], kill);
		rebuild_wait(arg, failed_ranks[i], concurrent_io);
	}

	/* XXX sigh, we do not support restart service after killing yet */
	if (kill)
		return;

	/* Add back those targets for future test */
	for (i = 0; i < rank_nr; i++)
		rebuild_test_add_tgt(arg, failed_ranks[i]);
}

static void
rebuild_single_target(test_arg_t *arg, daos_rank_t failed_rank,
		      bool concurrent_io)
{
	rebuild_targets(arg, &failed_rank, 1, false, concurrent_io);
}

static void
rebuild_dkeys(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;

	if (!rebuild_runable(arg, 3))
		skip();

	oid = dts_oid_gen(DAOS_OC_REPL_2_SMALL_RW, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	for (i = 0; i < KEY_NR; i++) {
		char	key[16];

		sprintf(key, "%d", i);
		insert_single(key, "a_key", 0, "data",
			      strlen("data") + 1, 0, &req);
	}

	/* Rebuild rank 1 */
	rebuild_single_target(arg, 2, false);
	ioreq_fini(&req);
}

static void
rebuild_akeys(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;

	if (!rebuild_runable(arg, 3))
		skip();

	oid = dts_oid_gen(DAOS_OC_REPL_2_SMALL_RW, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	for (i = 0; i < KEY_NR; i++) {
		char	akey[16];

		sprintf(akey, "%d", i);
		insert_single("d_key", akey, 0, "data",
			      strlen("data") + 1, 0, &req);
	}

	/* Rebuild rank 1 */
	rebuild_single_target(arg, 2, false);
	ioreq_fini(&req);
}

static void
rebuild_indexes(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;
	int			j;

	if (!rebuild_runable(arg, 3))
		skip();

	oid = dts_oid_gen(DAOS_OC_REPL_2_SMALL_RW, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      2000, DP_OID(oid));
	for (i = 0; i < 100; i++) {
		char	key[16];

		sprintf(key, "%d", i);
		for (j = 0; j < 20; j++)
			insert_single(key, "a_key", j, "data",
				      strlen("data") + 1, 0, &req);
	}

	/* Rebuild rank 1 */
	rebuild_single_target(arg, 2, false);
	ioreq_fini(&req);
}

static void
rebuild_multiple(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;
	int			j;
	int			k;

	if (!rebuild_runable(arg, 3))
		skip();

	oid = dts_oid_gen(DAOS_OC_REPL_2_SMALL_RW, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      1000, DP_OID(oid));
	for (i = 0; i < 10; i++) {
		char	dkey[16];

		sprintf(dkey, "dkey_%d", i);
		for (j = 0; j < 10; j++) {
			char	akey[16];

			sprintf(akey, "akey_%d", j);
			for (k = 0; k < 10; k++)
				insert_single(dkey, akey, k, "data",
					      strlen("data") + 1, 0, &req);
		}
	}

	/* Rebuild rank 1 */
	rebuild_single_target(arg, 2, false);
	ioreq_fini(&req);
}

static void
rebuild_large_rec(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;
	char			buffer[5000];

	if (!rebuild_runable(arg, 3))
		skip();

	oid = dts_oid_gen(DAOS_OC_REPL_2_SMALL_RW, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	memset(buffer, 'a', 5000);
	for (i = 0; i < KEY_NR; i++) {
		char	key[16];

		sprintf(key, "%d", i);
		insert_single(key, "a_key", 0, buffer, 5000, 0, &req);
	}

	/* Rebuild rank 1 */
	rebuild_single_target(arg, 2, false);
	ioreq_fini(&req);
}

static void
rebuild_objects(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;
	int			j;

	if (!rebuild_runable(arg, 3))
		skip();

	print_message("create %d objects\n", OBJ_NR);
	for (i = 0; i < OBJ_NR; i++) {
		oid = dts_oid_gen(DAOS_OC_REPL_2_SMALL_RW, arg->myrank);
		ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

		/** Insert 1000 records */
		print_message("Insert %d kv record in object "DF_OID"\n",
			      KEY_NR, DP_OID(oid));
		for (j = 0; j < 10; j++) {
			char	key[16];

			sprintf(key, "%d", j);
			insert_single(key, "a_key", 0, "data",
				      strlen("data") + 1, 0, &req);
		}
		ioreq_fini(&req);
	}

	/* Rebuild rank 1 */
	rebuild_single_target(arg, 2, false);
}

static void
rebuild_two_failures(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid[OBJ_NR];
	struct ioreq		req;
	daos_rank_t		ranks[2];
	int			i;
	int			j;

	if (!rebuild_runable(arg, 4))
		skip();

	for (i = 0; i < OBJ_NR; i++) {
		oid[i] = dts_oid_gen(DAOS_OC_REPL_2_SMALL_RW, arg->myrank);
		ioreq_init(&req, arg->coh, oid[i], DAOS_IOD_ARRAY, arg);
		/* Insert small size records */
		for (j = 0; j < 5; j++) {
			char	dkey[20];
			char	akey[20];
			int	k;
			int	l;

			/* small records */
			sprintf(dkey, "dkey_%d", j);
			for (k = 0; k < 2; k++) {
				sprintf(akey, "akey_%d", k);
				for (l = 0; l < 5; l++)
					insert_single(dkey, akey, l, "data",
						      strlen("data") + 1, 0,
						      &req);
			}

			for (k = 0; k < 2; k++) {
				char bulk[5000];

				sprintf(akey, "akey_bulk_%d", k);
				memset(bulk, 'a', 5000);
				for (l = 0; l < 5; l++)
					insert_single(dkey, akey, l, bulk,
						      5000, 0, &req);
			}

			req.iod_type = DAOS_IOD_SINGLE;
			sprintf(dkey, "dkey_single_%d", j);
			insert_single(dkey, "akey_single", 0,
				      "single_data", strlen("single_data") + 1,
				      0, &req);
		}
		ioreq_fini(&req);
	}
	ranks[0] = 2;
	ranks[1] = 3;
	rebuild_targets(arg, ranks, 2, true, true);

	/* Verify the data being rebuilt on other target */
	for (i = 0; i < OBJ_NR; i++) {
		ioreq_init(&req, arg->coh, oid[i], DAOS_IOD_ARRAY, arg);
		/* Insert small size records */
		for (j = 0; j < 5; j++) {
			char	dkey[20];
			char	akey[20];
			char	buf[16];
			int	k;
			int	l;

			/* small records */
			sprintf(dkey, "dkey_%d", j);
			for (k = 0; k < 2; k++) {
				sprintf(akey, "akey_%d", k);
				for (l = 0; l < 5; l++) {
					memset(buf, 0, 16);
					lookup_single(dkey, akey, l, buf, 5, 0,
						      &req);
					assert_memory_equal(buf, "data",
							    strlen("data"));
				}
			}

			for (k = 0; k < 2; k++) {
				char bulk[5010];
				char compare[5000];

				sprintf(akey, "akey_bulk_%d", k);
				memset(compare, 'a', 5000);
				for (l = 0; l < 5; l++) {
					lookup_single(dkey, akey, l, bulk,
						      5010, 0, &req);
					assert_memory_equal(bulk, compare,
							    5000);
				}
			}


			memset(buf, 0, 16);
			req.iod_type = DAOS_IOD_SINGLE;
			sprintf(dkey, "dkey_single_%d", j);
			lookup_single(dkey, "akey_single", 0, buf, 16, 0, &req);
			assert_memory_equal(buf, "single_data",
					    strlen("single_data"));
		}
		ioreq_fini(&req);
	}
}

/** create a new pool/container for each test */
static const struct CMUnitTest rebuild_tests[] = {
	{"REBUILD1: rebuild small rec mulitple dkeys",
	 rebuild_dkeys, NULL, NULL},
	{"REBUILD2: rebuild small rec multiple akeys",
	 rebuild_akeys, NULL, NULL},
	{"REBUILD3: rebuild small rec multiple indexes",
	 rebuild_indexes, NULL, NULL},
	{"REBUILD4: rebuild small rec multiple keys/indexes",
	 rebuild_multiple, NULL, NULL},
	{"REBUILD5: rebuild large rec single index",
	 rebuild_large_rec, NULL, NULL},
	{"REBUILD6: rebuild multiple objects",
	 rebuild_objects, NULL, NULL},
	{"REBUILD7: rebuild with two failures",
	 rebuild_two_failures, NULL, NULL},
};

int
rebuild_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, true);
}

int
run_daos_rebuild_test(int rank, int size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	rc = cmocka_run_group_tests_name("DAOS rebuild tests",
					 rebuild_tests, rebuild_setup,
					 test_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
