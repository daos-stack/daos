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
#define DDSUBSYS	DDFAC(tests)
#include "daos_iotest.h"
#include <daos/pool.h>

#define KEY_NR		1000
#define OBJ_NR		10
#define OBJ_CLS		DAOS_OC_R3S_RW

#define MAX_KILLS	3

static d_rank_t ranks_to_kill[MAX_KILLS];

static bool
rebuild_runable(test_arg_t *arg, unsigned int required_tgts,
		bool kill_master)
{
	daos_pool_info_t info;
	int		 i;
	int		 start = 0;
	bool		 runable = true;

	if (arg->myrank == 0) {
		if (arg->srv_ntgts - arg->srv_disabled_ntgts < required_tgts) {
			if (arg->myrank == 0)
				print_message("Not enough targets, skipping "
					      "(%d/%d)\n", info.pi_ntargets,
					      info.pi_ndisabled);
			runable = false;
		}

		/* XXX let's assume master rank is 1 for now */
		if (kill_master) {
			ranks_to_kill[0] = 1;
			start = 1;
		}

		for (i = start; i < MAX_KILLS; i++)
			ranks_to_kill[i] = arg->srv_ntgts -
					   arg->srv_disabled_ntgts - i - 1;
	}

	MPI_Bcast(&runable, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Barrier(MPI_COMM_WORLD);
	return runable;
}

static void
rebuild_test_exclude_tgt(test_arg_t **args, int arg_cnt, d_rank_t rank,
			 bool kill)
{
	if (args[0]->myrank == 0) {
		int i;

		if (kill) {
			daos_kill_server(args[0], args[0]->pool_uuid,
					 args[0]->group, &args[0]->svc, rank);
			sleep(5);
		}

		for (i = 0; i < arg_cnt; i++) {
			daos_exclude_server(args[i]->pool_uuid, args[i]->group,
					    &args[i]->svc, rank);
			sleep(2);
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);
}

static void
rebuild_test_add_tgt(test_arg_t **args, int args_cnt, d_rank_t rank)
{
	d_rank_list_t	ranks;
	int		rc;

	/** exclude the target from the pool */
	if (args[0]->myrank == 0) {
		int i;

		ranks.rl_nr = 1;
		ranks.rl_ranks = &rank;
		for (i = 0; i < args_cnt; i++) {
			rc = daos_pool_tgt_add(args[i]->pool_uuid,
					       args[i]->group,
					       &args[i]->svc, &ranks,
					       NULL);
			assert_int_equal(rc, 0);
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);
}

static int
rebuild_io_internal(test_arg_t *arg, daos_obj_id_t *oids, int oids_nr,
		    bool update)
{
	struct ioreq	req;
	int		i;
	int		j;
	char		dkey[16];
	char		buf[10];

	print_message("insert obj %d rec %d for rebuild test\n",
		      oids_nr, KEY_NR);

	for (j = 0; j < oids_nr; j++) {
		ioreq_init(&req, arg->coh, oids[j], DAOS_IOD_ARRAY, arg);
		/* Let's do I/O at the same time */
		for (i = 0; i < KEY_NR; i++) {
			sprintf(dkey, "%d", i);
			memset(buf, 0, 10);
			if (update)
				insert_single(dkey, "rebuild_akey_in", 0,
					      "data", strlen("data") + 1,
					      0, &req);
			lookup_single(dkey, "rebuild_akey_in", 0, buf,
				      10, 0, &req);
			assert_memory_equal(buf, "data", strlen("data"));
			assert_int_equal(req.iod[0].iod_size,
					 strlen("data") + 1);
		}
		ioreq_fini(&req);
	}

	return 0;
}

static void
rebuild_io(test_arg_t *arg, daos_obj_id_t *oids, int oids_nr)
{
	rebuild_io_internal(arg, oids, oids_nr, true);
}

static void
rebuild_io_validate(test_arg_t *arg, daos_obj_id_t *oids, int oids_nr)
{
	rebuild_io_internal(arg, oids, oids_nr, false);
}

static void
rebuild_pool_wait(test_arg_t *arg)
{
	daos_pool_info_t	   pinfo;
	struct daos_rebuild_status *rst = &pinfo.pi_rebuild_st;
	bool			   connect_pool = false;
	int			   rc;

	while (1) {
		if (daos_handle_is_inval(arg->poh)) {
			rc = daos_pool_connect(arg->pool_uuid, arg->group,
					       &arg->svc, DAOS_PC_RW,
					       &arg->poh, &pinfo, NULL);
			if (rc) {
				print_message("pool_connect failed, rc: %d\n",
					      rc);
				break;
			}
			connect_pool = true;
		}

		memset(&pinfo, 0, sizeof(pinfo));
		rc = daos_pool_query(arg->poh, NULL, &pinfo, NULL);
		if (rst->rs_done || rc != 0 || rst->rs_errno) {
			print_message("Rebuild (ver=%d) is done %d/%d\n",
				       rst->rs_version, rc, rst->rs_errno);
			if (connect_pool) {
				rc = daos_pool_disconnect(arg->poh, NULL);
				if (rc)
					print_message("disconnect failed: %d\n",
						      rc);
				arg->poh = DAOS_HDL_INVAL;
			}
			break;
		}

		print_message("wait for rebuild (ver=%u), "
			      "already rebuilt obj="DF_U64", rec="DF_U64"\n",
			      rst->rs_version, rst->rs_obj_nr, rst->rs_rec_nr);
		sleep(2);
	}
}

static void
rebuild_wait(test_arg_t **args, int args_cnt, d_rank_t failed_rank,
	     bool concurrent_io)
{
	daos_obj_id_t	oid;
	int		i;

	oid = dts_oid_gen(OBJ_CLS, args[0]->myrank);
	if (concurrent_io) {
		for (i = 0; i < args_cnt; i++) {
			if (!daos_handle_is_inval(args[i]->coh))
				rebuild_io(args[i], &oid, 1);
		}
	}

	if (args[0]->myrank == 0) {
		for (i = 0; i < args_cnt; i++)
			rebuild_pool_wait(args[i]);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	if (concurrent_io) {
		for (i = 0; i < args_cnt; i++) {
			/* validate the data */
			if (!daos_handle_is_inval(args[i]->coh))
				rebuild_io_validate(args[i], &oid, 1);
		}
	}
}

static void
rebuild_targets(test_arg_t **args, int args_cnt, d_rank_t *failed_ranks,
		int rank_nr, bool kill, bool concurrent_io)
{
	int	i;

	/** exclude the target from the pool */
	for (i = 0; i < rank_nr; i++) {
		rebuild_test_exclude_tgt(args, args_cnt, failed_ranks[i], kill);
		/* Sleep 5 seconds to make sure the rebuild start */
		sleep(5);
	}

	rebuild_wait(args, args_cnt, failed_ranks[i], concurrent_io);

	/* XXX sigh, we do not support restart service after killing yet */
	if (kill)
		return;

	/* Add back those targets for future test */
	for (i = 0; i < rank_nr; i++)
		rebuild_test_add_tgt(args, args_cnt, failed_ranks[i]);
}

static void
rebuild_single_pool_target(test_arg_t *arg, d_rank_t failed_rank,
			   bool concurrent_io)
{
	rebuild_targets(&arg, 1, &failed_rank, 1, false, concurrent_io);
}

static void
rebuild_pools_targets(test_arg_t **args, int args_cnt, d_rank_t *failed_ranks,
		      int ranks_nr, bool concurrent_io)
{
	rebuild_targets(args, args_cnt, failed_ranks, ranks_nr, false,
			concurrent_io);
}

static void
rebuild_dkeys(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;

	if (!rebuild_runable(arg, 3, false))
		skip();

	oid = dts_oid_gen(OBJ_CLS, arg->myrank);
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

	rebuild_single_pool_target(arg, ranks_to_kill[0], false);
	ioreq_fini(&req);
}

static void
rebuild_akeys(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;

	if (!rebuild_runable(arg, 3, false))
		skip();

	oid = dts_oid_gen(OBJ_CLS, arg->myrank);
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

	rebuild_single_pool_target(arg, ranks_to_kill[0], false);
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

	if (!rebuild_runable(arg, 3, false))
		skip();

	oid = dts_oid_gen(OBJ_CLS, arg->myrank);
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
	rebuild_single_pool_target(arg, ranks_to_kill[0], false);
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

	if (!rebuild_runable(arg, 3, false))
		skip();

	oid = dts_oid_gen(OBJ_CLS, arg->myrank);
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

	rebuild_single_pool_target(arg, ranks_to_kill[0], false);
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

	if (!rebuild_runable(arg, 3, false))
		skip();

	oid = dts_oid_gen(OBJ_CLS, arg->myrank);
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

	rebuild_single_pool_target(arg, ranks_to_kill[0], false);
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

	if (!rebuild_runable(arg, 3, false))
		skip();

	print_message("create %d objects\n", OBJ_NR);
	for (i = 0; i < OBJ_NR; i++) {
		oid = dts_oid_gen(OBJ_CLS, arg->myrank);
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

	rebuild_single_pool_target(arg, ranks_to_kill[0], false);
}

static void
rebuild_drop_scan(void **state)
{
	test_arg_t	*arg = *state;

	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;
	int			j;

	if (!rebuild_runable(arg, 3, false))
		skip();

	print_message("create %d objects\n", OBJ_NR);
	for (i = 0; i < OBJ_NR; i++) {
		oid = dts_oid_gen(OBJ_CLS, arg->myrank);
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

	/* Set drop scan fail_loc on server 0 */
	daos_mgmt_params_set(arg->group, 0, DSS_KEY_FAIL_LOC,
			     DAOS_REBUILD_NO_HDL | DAOS_FAIL_ONCE,
			     NULL);
	rebuild_single_pool_target(arg, ranks_to_kill[0], false);
}

static void
rebuild_retry_rebuild(void **state)
{
	test_arg_t	*arg = *state;

	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;
	int			j;

	if (!rebuild_runable(arg, 3, false))
		skip();

	print_message("create %d objects\n", OBJ_NR);
	for (i = 0; i < OBJ_NR; i++) {
		oid = dts_oid_gen(OBJ_CLS, arg->myrank);
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

	/* Set no hdl fail_loc on all servers */
	daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC,
			     DAOS_REBUILD_NO_HDL | DAOS_FAIL_ONCE,
			     NULL);
	rebuild_single_pool_target(arg, ranks_to_kill[0], false);
}

static void
rebuild_multiple_pools(void **state)
{
	test_arg_t	*arg = *state;
	test_arg_t	*args[2];
	daos_obj_id_t	oids[OBJ_NR];
	int		i;
	int		rc;

	if (!rebuild_runable(arg, 3, false))
		skip();

	args[0] = arg;
	/* create/connect another pool */
	rc = test_setup((void **)&args[1], SETUP_CONT_CONNECT, arg->multi_rank);
	if (rc) {
		print_message("open/connect another pool failed: rc %d\n", rc);
		return;
	}

	for (i = 0; i < OBJ_NR; i++)
		oids[i] = dts_oid_gen(OBJ_CLS, arg->myrank);
	rebuild_io(args[0], oids, OBJ_NR);
	rebuild_io(args[1], oids, OBJ_NR);

	rebuild_pools_targets(args, 2, ranks_to_kill, 1, false);

	rebuild_io_validate(args[0], oids, OBJ_NR);
	rebuild_io_validate(args[1], oids, OBJ_NR);

	test_teardown((void **)&args[1]);
}

static void
rebuild_offline(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		i;
	int		j;
	int		rc;

	if (!rebuild_runable(arg, 3, false))
		skip();

	print_message("create %d objects\n", OBJ_NR);
	for (i = 0; i < OBJ_NR; i++) {
		oid = dts_oid_gen(OBJ_CLS, arg->myrank);
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

	/* Close cont and disconnect pool */
	MPI_Barrier(MPI_COMM_WORLD);
	rc = daos_cont_close(arg->coh, NULL);
	if (rc) {
		print_message("failed to close container "DF_UUIDF
			      ": %d\n", DP_UUID(arg->co_uuid), rc);
		return;
	}
	arg->coh = DAOS_HDL_INVAL;

	rc = daos_pool_disconnect(arg->poh, NULL /* ev */);
	if (rc) {
		print_message("failed to disconnect pool "DF_UUIDF
			      ": %d\n", DP_UUID(arg->pool_uuid), rc);
		return;
	}
	arg->poh = DAOS_HDL_INVAL;

	MPI_Barrier(MPI_COMM_WORLD);

	rebuild_targets(&arg, 1, ranks_to_kill, 1, true, false);

	/* reconnect the pool again */
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = daos_pool_connect(arg->pool_uuid, arg->group, &arg->svc,
				       DAOS_PC_RW, &arg->poh, &arg->pool_info,
				       NULL /* ev */);
		if (rc) {
			print_message("daos_pool_connect failed, rc: %d\n", rc);
			return;
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);

	/** broadcast pool info */
	if (arg->multi_rank) {
		MPI_Bcast(&arg->pool_info, sizeof(arg->pool_info), MPI_CHAR, 0,
			  MPI_COMM_WORLD);
		handle_share(&arg->poh, HANDLE_POOL, arg->myrank, arg->poh, 0);
	}

	/** open container */
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = daos_cont_open(arg->poh, arg->co_uuid, DAOS_COO_RW,
				    &arg->coh, &arg->co_info, NULL);
		if (rc)
			print_message("daos_cont_open failed, rc: %d\n", rc);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	/** broadcast container info */
	if (arg->multi_rank) {
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		handle_share(&arg->coh, HANDLE_CO, arg->myrank, arg->poh, 0);
	}
}

static void
rebuild_two_failures(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid[OBJ_NR];
	struct ioreq		req;
	int			i;
	int			j;

	if (!rebuild_runable(arg, 4, false))
		skip();

	for (i = 0; i < OBJ_NR; i++) {
		oid[i] = dts_oid_gen(OBJ_CLS, arg->myrank);
		ioreq_init(&req, arg->coh, oid[i], DAOS_IOD_ARRAY, arg);
		/* Insert small size records */
		for (j = 0; j < 5; j++) {
			char	dkey[20];
			char	akey[20];
			int	k;
			int	l;

			req.iod_type = DAOS_IOD_ARRAY;
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

	rebuild_targets(&arg, 1, ranks_to_kill, 2, true, true);

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
			req.iod_type = DAOS_IOD_ARRAY;
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
	 rebuild_dkeys, NULL, test_case_teardown},
	{"REBUILD2: rebuild small rec multiple akeys",
	 rebuild_akeys, NULL, test_case_teardown},
	{"REBUILD3: rebuild small rec multiple indexes",
	 rebuild_indexes, NULL, test_case_teardown},
	{"REBUILD4: rebuild small rec multiple keys/indexes",
	 rebuild_multiple, NULL, test_case_teardown},
	{"REBUILD5: rebuild large rec single index",
	 rebuild_large_rec, NULL, test_case_teardown},
	{"REBUILD6: rebuild multiple objects",
	 rebuild_objects, NULL, test_case_teardown},
	{"REBUILD7: drop rebuild scan reply",
	rebuild_drop_scan, NULL, test_case_teardown},
	{"REBUILD8: retry rebuild",
	rebuild_retry_rebuild, NULL, test_case_teardown},
	{"REBUILD9: rebuild multiple pools",
	rebuild_multiple_pools, NULL, test_case_teardown},
	{"REBUILD10: offline rebuild",
	rebuild_offline, NULL, test_case_teardown},
	{"REBUILD11: rebuild with two failures",
	 rebuild_two_failures, NULL, test_case_teardown},
};

int
rebuild_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, true);
}

int
run_daos_rebuild_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	void *state;
	int i;
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		rc = cmocka_run_group_tests_name("DAOS rebuild tests",
						 rebuild_tests, rebuild_setup,
						 test_teardown);
		MPI_Barrier(MPI_COMM_WORLD);
		return rc;
	}

	rebuild_setup(&state);
	for (i = 0; i < sub_tests_size; i++) {
		int idx = sub_tests[i];

		if (ARRAY_SIZE(rebuild_tests) <= idx) {
			print_message("No test %d\n", idx);
			continue;
		}

		print_message("%s\n", rebuild_tests[idx].name);
		if (rebuild_tests[idx].setup_func) {
			if (state != NULL)
				test_teardown(&state);
			rebuild_tests[idx].setup_func(&state);
		}
		rebuild_tests[idx].test_func(&state);
		if (rebuild_tests[idx].teardown_func)
			rebuild_tests[idx].teardown_func(&state);
	}
	test_teardown(&state);
	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
