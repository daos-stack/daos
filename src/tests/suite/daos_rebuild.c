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
rebuild_runable(test_arg_t *arg)
{
	daos_pool_info_t info;
	int		 rc;

	rc = daos_pool_query(arg->poh, NULL, &info, NULL);
	assert_int_equal(rc, 0);
	if (info.pi_ntargets - info.pi_ndisabled < 2) {
		if (arg->myrank == 0)
			print_message("Not enough active targets, skipping "
				      "(%d/%d)\n", info.pi_ntargets,
				      info.pi_ndisabled);
		return false;
	}
	return true;
}

static int
rebuild_srv_target(uuid_t pool_uuid, daos_rank_t failed_rank)
{
	daos_rank_list_t	ranks;
	int			rc;

	/* Rebuild rank 0 */
	ranks.rl_nr.num = 1;
	ranks.rl_nr.num_out = 1;
	ranks.rl_ranks = &failed_rank;

	rc = daos_rebuild_tgt(pool_uuid, &ranks, NULL);
	assert_int_equal(rc, 0);
	/* XXX FIXME: instead it should keep checking rebuild
	 * status and wait
	 */
	print_message("sleep 60 sends for rebuild finish.\n");
	sleep(60);
	return rc;
}

static void
rebuild_dkeys(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;

	if (!rebuild_runable(arg))
		skip();

	oid = dts_oid_gen(DAOS_OC_REPL_2_RW, arg->myrank);
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

	/* Rebuild rank 0 */
	rebuild_srv_target(arg->pool_uuid, 0);
}

static void
rebuild_akeys(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;

	if (!rebuild_runable(arg))
		skip();

	oid = dts_oid_gen(DAOS_OC_REPL_2_RW, arg->myrank);
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

	/* Rebuild rank 0 */
	rebuild_srv_target(arg->pool_uuid, 0);
}

static void
rebuild_indexes(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;
	int			j;

	if (!rebuild_runable(arg))
		skip();

	oid = dts_oid_gen(DAOS_OC_REPL_2_RW, arg->myrank);
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

	/* Rebuild rank 0 */
	rebuild_srv_target(arg->pool_uuid, 0);
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

	if (!rebuild_runable(arg))
		skip();

	oid = dts_oid_gen(DAOS_OC_REPL_2_RW, arg->myrank);
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

	/* Rebuild rank 0 */
	rebuild_srv_target(arg->pool_uuid, 0);
}

static void
rebuild_large_rec(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;
	char			buffer[4096];

	if (!rebuild_runable(arg))
		skip();

	oid = dts_oid_gen(DAOS_OC_REPL_2_RW, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	memset(buffer, 'a', 4096);
	for (i = 0; i < KEY_NR; i++) {
		char	key[16];

		sprintf(key, "%d", i);
		insert_single(key, "a_key", 0, buffer, 4097, 0, &req);
	}

	/* Rebuild rank 0 */
	rebuild_srv_target(arg->pool_uuid, 0);
}

static void
rebuild_objects(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;
	int			j;

	if (!rebuild_runable(arg))
		skip();

	print_message("create 10 objects\n");
	for (i = 0; i < 10; i++) {
		oid = dts_oid_gen(DAOS_OC_REPL_2_RW, arg->myrank);
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

	/* Rebuild rank 0 */
	rebuild_srv_target(arg->pool_uuid, 0);
}

/** create a new pool/container for each test */
static const struct CMUnitTest rebuild_tests[] = {
	{"REBUILD1: rebuild small rec mulitple dkeys",
	 rebuild_dkeys, NULL, NULL},
	{"REBUILD2: rebuild small rec multiple akeys",
	 rebuild_akeys, NULL, NULL},
	{"REBUILD3: rebuild small rec multiple indexes",
	 rebuild_indexes, NULL, NULL},
	{"REBUILD2: rebuild small rec multiple keys/indexes",
	 rebuild_multiple, NULL, NULL},
	{"REBUILD4: rebuild large rec single index",
	 rebuild_large_rec, NULL, NULL},
	{"REBUILD5: rebuild multiple objects",
	 rebuild_objects, NULL, NULL},
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
