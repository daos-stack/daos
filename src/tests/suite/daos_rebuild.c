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
#include <daos/client.h>
#include <daos/pool.h>

#define KEY_NR	1000

static bool
rebuild_runable(test_arg_t *arg, unsigned int required_tgts)
{
	daos_pool_info_t info;
	int		 rc;

	rc = daos_pool_query(arg->poh, NULL, &info, NULL);
	assert_int_equal(rc, 0);
	if (info.pi_ntargets - info.pi_ndisabled < required_tgts) {
		if (arg->myrank == 0)
			print_message("Not enough active targets, skipping "
				      "(%d/%d)\n", info.pi_ntargets,
				      info.pi_ndisabled);
		return false;
	}
	return true;
}

static void
rebuild_test_exclude_tgt(daos_handle_t poh, daos_rank_t rank)
{
	daos_rank_list_t	ranks;
	int			rc;

	/** exclude the target from the pool */
	ranks.rl_nr.num = 1;
	ranks.rl_nr.num_out = 0;
	ranks.rl_ranks = &rank;
	rc = daos_pool_exclude(poh, &ranks, NULL);
	assert_int_equal(rc, 0);
}

static void
rebuild_test_add_tgt(daos_handle_t poh, daos_rank_t rank)
{
	daos_rank_list_t	ranks;
	int			rc;

	/** exclude the target from the pool */
	ranks.rl_nr.num = 1;
	ranks.rl_nr.num_out = 0;
	ranks.rl_ranks = &rank;
	rc = daos_pool_tgt_add(poh, &ranks, NULL);
	assert_int_equal(rc, 0);
}

static int
rebuild_one_target(daos_handle_t poh, uuid_t pool_uuid,
		   daos_rank_t failed_rank, struct ioreq *req)
{
	daos_rank_list_t	ranks;
	int			rc;
	int			done = 0;
	int			status = 0;
	int			i;
	char			dkey[16];
	char			buf[10];

	/* Rebuild rank 0 */
	ranks.rl_nr.num = 1;
	ranks.rl_nr.num_out = 1;
	ranks.rl_ranks = &failed_rank;

	rc = daos_rebuild_tgt(pool_uuid, &ranks, NULL);
	assert_int_equal(rc, 0);

	/* Let's do I/O at the same time */
	print_message("insert %d dkey/rebuild_akey during rebuild\n", KEY_NR);
	for (i = 0; i < KEY_NR; i++) {
		sprintf(dkey, "%d", i);
		insert_single(dkey, "rebuild_akey", 0,
			      "data", strlen("data") + 1,
			      0, req);

		memset(buf, 0, 10);
		lookup_single(dkey, "rebuild_akey", 0, buf,
			      10, 0, req);
		assert_memory_equal(buf, "data", strlen("data"));
		assert_int_equal(req->iod[0].iod_size,
				 strlen("data") + 1);
	}

	while (!done) {
		rc = daos_rebuild_query(pool_uuid, &ranks, &done, &status,
					NULL);
		if (rc != 0)
			break;
		assert_int_equal(status, 0);
		print_message("wait for rebuild finish.\n");
		sleep(2);
	}

	assert_int_equal(rc, 0);

	for (i = 0; i < KEY_NR; i++) {
		memset(buf, 0, 10);
		sprintf(dkey, "%d", i);
		lookup_single(dkey, "rebuild_akey", 0, buf,
			      10, 0, req);
		assert_memory_equal(buf, "data", strlen("data"));
		assert_int_equal(req->iod[0].iod_size, strlen("data") + 1);
	}

	return rc;
}

static void
rebuild_targets(daos_handle_t poh, uuid_t pool_uuid,
		daos_rank_t *failed_ranks, int rank_nr,
		struct ioreq *req)
{
	int	i;

	/** exclude the target from the pool */
	for (i = 0; i < rank_nr; i++) {
		rebuild_test_exclude_tgt(poh, failed_ranks[i]);

		rebuild_one_target(poh, pool_uuid, failed_ranks[i], req);
	}

	for (i = 0; i < rank_nr; i++)
		rebuild_test_add_tgt(poh, failed_ranks[i]);
}

static void
rebuild_single_target(daos_handle_t poh, uuid_t pool_uuid,
		      daos_rank_t failed_rank, struct ioreq *req)
{
	rebuild_targets(poh, pool_uuid, &failed_rank, 1, req);
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
	rebuild_single_target(arg->poh, arg->pool_uuid, 1, &req);
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
	rebuild_single_target(arg->poh, arg->pool_uuid, 1, &req);
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
	rebuild_single_target(arg->poh, arg->pool_uuid, 1, &req);
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
	rebuild_single_target(arg->poh, arg->pool_uuid, 1, &req);
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
	rebuild_single_target(arg->poh, arg->pool_uuid, 1, &req);
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

	print_message("create 10 objects\n");
	for (i = 0; i < 10; i++) {
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
	}

	/* Rebuild rank 1 */
	rebuild_single_target(arg->poh, arg->pool_uuid, 1, &req);
}

static void
rebuild_two_failures(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	daos_rank_t		ranks[2];
	int			i;
	int			j;

	if (!rebuild_runable(arg, 4))
		skip();

	oid = dts_oid_gen(DAOS_OC_REPL_2_SMALL_RW, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      1000, DP_OID(oid));
	for (i = 0; i < 10; i++) {
		char	dkey[16];
		char	akey[16];
		int	k;

		sprintf(dkey, "dkey_%d", i);
		for (j = 0; j < 10; j++) {

			sprintf(akey, "akey_%d", j);
			for (k = 0; k < 10; k++)
				insert_single(dkey, akey, k, "data",
					      strlen("data") + 1, 0, &req);
		}
	}

	ranks[0] = 1;
	ranks[1] = 2;
	rebuild_targets(arg->poh, arg->pool_uuid, ranks, 2, &req);

	/* Verify the data being rebuilt on other target */
	for (i = 0; i < 10; i++) {
		char	dkey[16];
		char	akey[16];
		char	buf[10];
		int	k;

		sprintf(dkey, "dkey_%d", i);
		for (j = 0; j < 10; j++) {

			sprintf(akey, "akey_%d", j);
			for (k = 0; k < 10; k++) {
				memset(buf, 0, 10);
				lookup_single(dkey, akey, k, buf, 10, 0, &req);
			}
			assert_memory_equal(buf, "data", strlen("data"));
			assert_int_equal(req.iod[0].iod_size,
					 strlen("data") + 1);
		}
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
