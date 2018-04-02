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
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"
#include <daos/pool.h>

#define KEY_NR		1000
#define OBJ_NR		10
#define OBJ_CLS		DAOS_OC_R3S_RW
#define OBJ_REPLICAS	3

#define MAX_KILLS	3

static d_rank_t ranks_to_kill[MAX_KILLS];

static bool
rebuild_runable(test_arg_t *arg, unsigned int required_tgts,
		bool kill_master)
{
	int		 i;
	int		 start = 0;
	bool		 runable = true;

	if (arg->myrank == 0) {
		if (arg->srv_ntgts - arg->srv_disabled_ntgts < required_tgts) {
			if (arg->myrank == 0)
				print_message("Not enough targets, skipping "
					      "(%d/%d)\n",
					      arg->srv_ntgts,
					      arg->srv_disabled_ntgts);
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
rebuild_test_add_tgt(test_arg_t *args, int args_cnt, d_rank_t rank)
{
	d_rank_list_t	ranks;
	int		rc;

	/** exclude the target from the pool */
	if (args[0].myrank == 0) {
		int i;

		ranks.rl_nr = 1;
		ranks.rl_ranks = &rank;
		for (i = 0; i < args_cnt; i++) {
			rc = daos_pool_tgt_add(args[i].pool_uuid,
					       args[i].group,
					       &args[i].svc, &ranks,
					       NULL);
			assert_int_equal(rc, 0);
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);
}

static int
rebuild_io_internal(test_arg_t *arg, daos_obj_id_t *oids, int oids_nr,
		    bool validate)
{
	struct ioreq	req;
	int		i;
	int		j;

	print_message("%s obj %d for rebuild test\n",
		      validate ? "validate" : "update", oids_nr);

	for (i = 0; i < oids_nr; i++) {
		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);
		for (j = 0; j < 5; j++) {
			char	dkey[20];
			char	akey[20];
			char	buf[16];
			int	k;
			int	l;

			req.iod_type = DAOS_IOD_ARRAY;
			/* small records */
			sprintf(dkey, "dkey_%d", j);
			for (k = 0; k < 2; k++) {
				sprintf(akey, "akey_%d", k);
				for (l = 0; l < 10; l++) {
					if (validate) {
						memset(buf, 0, 16);
						lookup_single(dkey, akey, l,
							      buf, 5, 0, &req);
						assert_memory_equal(buf, "data",
								strlen("data"));
					} else {
						insert_single(dkey, akey, l,
							"data",
							strlen("data") + 1, 0,
							&req);
					}
				}
			}

			/* large records */
			for (k = 0; k < 2; k++) {
				char bulk[5010];
				char compare[5000];

				sprintf(akey, "akey_bulk_%d", k);
				memset(compare, 'a', 5000);
				for (l = 0; l < 5; l++) {
					if (validate) {
						memset(bulk, 0, 5000);
						lookup_single(dkey, akey, l,
							      bulk, 5010, 0,
							      &req);
						assert_memory_equal(bulk,
								compare, 5000);
					} else {
						memset(bulk, 'a', 5000);
						insert_single(dkey, akey, l,
							      bulk, 5000, 0,
							      &req);
					}
				}
			}

			/* single record */
			memset(buf, 0, 16);
			req.iod_type = DAOS_IOD_SINGLE;
			sprintf(dkey, "dkey_single_%d", j);
			if (validate) {
				lookup_single(dkey, "akey_single", 0, buf, 16,
					      0, &req);
				assert_memory_equal(buf, "single_data",
						    strlen("single_data"));
			} else {
				insert_single(dkey, "akey_single", 0,
					      "single_data",
					      strlen("single_data") + 1,
					      0, &req);
			}
		}
		ioreq_fini(&req);
	}

	return 0;
}

static void
rebuild_io(test_arg_t *arg, daos_obj_id_t *oids, int oids_nr)
{
	rebuild_io_internal(arg, oids, oids_nr, false);
}

static void
rebuild_io_validate(test_arg_t *arg, daos_obj_id_t *oids, int oids_nr)
{
	int i;

	/* Validate data for each shard */
	for (i = 0; i < OBJ_REPLICAS; i++) {
		arg->fail_loc = DAOS_OBJ_SPECIAL_SHARD | DAOS_FAIL_VALUE;
		arg->fail_value = i;
		rebuild_io_internal(arg, oids, oids_nr, true);
	}

	arg->fail_loc = 0;
	arg->fail_value = 0;
}

static bool
rebuild_pool_wait(test_arg_t *arg)
{
	daos_pool_info_t	   pinfo;
	struct daos_rebuild_status *rst = &pinfo.pi_rebuild_st;
	bool			   connect_pool = false;
	int			   rc;
	bool			   done = false;

	if (daos_handle_is_inval(arg->poh)) {
		rc = daos_pool_connect(arg->pool_uuid, arg->group,
				       &arg->svc, DAOS_PC_RW,
				       &arg->poh, &pinfo, NULL);
		if (rc) {
			print_message("pool_connect failed, rc: %d\n",
				      rc);
			return done;
		}
		connect_pool = true;
	}

	memset(&pinfo, 0, sizeof(pinfo));
	rc = daos_pool_query(arg->poh, NULL, &pinfo, NULL);
	if (rst->rs_done || rc != 0) {
		print_message("Rebuild (ver=%d) is done %d/%d\n",
			       rst->rs_version, rc, rst->rs_errno);
		if (connect_pool) {
			rc = daos_pool_disconnect(arg->poh, NULL);
			if (rc)
				print_message("disconnect failed: %d\n",
					      rc);
			arg->poh = DAOS_HDL_INVAL;
		}
		done = true;
	} else {
		print_message("wait for rebuild pool "DF_UUIDF"(ver=%u), "
			      "already rebuilt obj="DF_U64", rec="DF_U64"\n",
			      DP_UUID(arg->pool_uuid), rst->rs_version,
			      rst->rs_obj_nr, rst->rs_rec_nr);
	}
	return done;
}

static bool
rebuild_wait(test_arg_t **args, int args_cnt)
{
	bool all_done = true;
	int i;

	for (i = 0; i < args_cnt; i++) {
		bool done;

		done = rebuild_pool_wait(args[i]);
		if (!done)
			all_done = false;
	}
	return all_done;
}

static void
rebuild_targets(test_arg_t **args, int args_cnt, d_rank_t *failed_ranks,
		int rank_nr, bool kill)
{
	int	i;
	bool	done = false;

	/** exclude the target from the pool */
	for (i = 0; i < rank_nr; i++) {
		rebuild_test_exclude_tgt(args, args_cnt, failed_ranks[i], kill);
		/* Sleep 5 seconds to make sure the rebuild start */
		sleep(5);
	}

	for (i = 0; i < args_cnt; i++)
		if (args[i]->rebuild_cb)
			args[i]->rebuild_cb(args[i]);

	if (args[0]->myrank == 0) {
		while (!done) {
			done = rebuild_wait(args, args_cnt);
			if (!done)
				sleep(2);
		}
	}

	MPI_Barrier(MPI_COMM_WORLD);
	for (i = 0; i < args_cnt; i++)
		if (args[i]->rebuild_post_cb)
			args[i]->rebuild_post_cb(args[i]);
}

static void
rebuild_single_pool_target(test_arg_t *arg, d_rank_t failed_rank)
{
	rebuild_targets(&arg, 1, &failed_rank, 1, false);
}

static void
rebuild_pools_targets(test_arg_t **args, int args_cnt, d_rank_t *failed_ranks,
		      int ranks_nr)
{
	rebuild_targets(args, args_cnt, failed_ranks, ranks_nr, false);
}

static void
rebuild_dkeys(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;

	if (!rebuild_runable(arg, 6, false))
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
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_test_add_tgt(arg, 1, ranks_to_kill[0]);
}

static void
rebuild_akeys(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;

	if (!rebuild_runable(arg, 6, false))
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
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_test_add_tgt(arg, 1, ranks_to_kill[0]);
}

static void
rebuild_indexes(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;
	int			j;

	if (!rebuild_runable(arg, 6, false))
		skip();

	oid = dts_oid_gen(OBJ_CLS, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 2000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      2000, DP_OID(oid));
	for (i = 0; i < 100; i++) {
		char	key[16];

		sprintf(key, "%d", i);
		for (j = 0; j < 20; j++)
			insert_single(key, "a_key", j, "data",
				      strlen("data") + 1, 0, &req);
	}
	ioreq_fini(&req);

	/* Rebuild rank 1 */
	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_test_add_tgt(arg, 1, ranks_to_kill[0]);
}

static void
rebuild_multiple(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		i;
	int		j;
	int		k;

	if (!rebuild_runable(arg, 6, false))
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
					      strlen("data") + 1, 0,
					      &req);
		}
	}
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_test_add_tgt(arg, 1, ranks_to_kill[0]);
}

static void
rebuild_large_rec(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;
	char			buffer[5000];

	if (!rebuild_runable(arg, 6, false))
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
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_test_add_tgt(arg, 1, ranks_to_kill[0]);
}

static void
rebuild_objects(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!rebuild_runable(arg, 6, false))
		skip();

	for (i = 0; i < OBJ_NR; i++)
		oids[i] = dts_oid_gen(OBJ_CLS, arg->myrank);

	rebuild_io(arg, oids, OBJ_NR);

	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_io_validate(arg, oids, OBJ_NR);

	rebuild_test_add_tgt(arg, 1, ranks_to_kill[0]);
}

static void
rebuild_drop_scan(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!rebuild_runable(arg, 6, false))
		skip();

	for (i = 0; i < OBJ_NR; i++)
		oids[i] = dts_oid_gen(OBJ_CLS, arg->myrank);

	rebuild_io(arg, oids, OBJ_NR);

	/* Set drop scan fail_loc on server 0 */
	daos_mgmt_params_set(arg->group, 0, DSS_KEY_FAIL_LOC,
			     DAOS_REBUILD_NO_HDL | DAOS_FAIL_ONCE,
			     NULL);
	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_io_validate(arg, oids, OBJ_NR);

	rebuild_test_add_tgt(arg, 1, ranks_to_kill[0]);
}

static void
rebuild_retry_rebuild(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!rebuild_runable(arg, 6, false))
		skip();

	for (i = 0; i < OBJ_NR; i++)
		oids[i] = dts_oid_gen(OBJ_CLS, arg->myrank);

	rebuild_io(arg, oids, OBJ_NR);

	/* Set no hdl fail_loc on all servers */
	daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC,
			     DAOS_REBUILD_NO_HDL | DAOS_FAIL_ONCE,
			     NULL);
	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_io_validate(arg, oids, OBJ_NR);

	rebuild_test_add_tgt(arg, 1, ranks_to_kill[0]);
}

static void
rebuild_retry_for_stale_pool(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!rebuild_runable(arg, 6, false))
		skip();

	for (i = 0; i < OBJ_NR; i++)
		oids[i] = dts_oid_gen(OBJ_CLS, arg->myrank);

	rebuild_io(arg, oids, OBJ_NR);

	/* Set no hdl fail_loc on all servers */
	daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC,
			     DAOS_REBUILD_STALE_POOL | DAOS_FAIL_ONCE,
			     NULL);
	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_io_validate(arg, oids, OBJ_NR);

	rebuild_test_add_tgt(arg, 1, ranks_to_kill[0]);
}

static void
rebuild_drop_obj(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!rebuild_runable(arg, 6, false))
		skip();

	for (i = 0; i < OBJ_NR; i++)
		oids[i] = dts_oid_gen(OBJ_CLS, arg->myrank);

	rebuild_io(arg, oids, OBJ_NR);

	/* Set drop scan reply on all servers */
	daos_mgmt_params_set(arg->group, 0, DSS_KEY_FAIL_LOC,
			     DAOS_REBUILD_DROP_OBJ | DAOS_FAIL_ONCE,
			     NULL);
	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_io_validate(arg, oids, OBJ_NR);

	rebuild_test_add_tgt(arg, 1, ranks_to_kill[0]);
}

static void
rebuild_update_failed(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!rebuild_runable(arg, 6, false))
		skip();

	for (i = 0; i < OBJ_NR; i++)
		oids[i] = dts_oid_gen(OBJ_CLS, arg->myrank);

	rebuild_io(arg, oids, OBJ_NR);

	/* Set drop scan reply on all servers */
	daos_mgmt_params_set(arg->group, 0, DSS_KEY_FAIL_LOC,
			     DAOS_REBUILD_UPDATE_FAIL | DAOS_FAIL_ONCE,
			     NULL);
	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_test_add_tgt(arg, 1, ranks_to_kill[0]);
}

static void
rebuild_multiple_pools(void **state)
{
	test_arg_t	*arg = *state;
	test_arg_t	*args[2];
	daos_obj_id_t	oids[OBJ_NR];
	int		i;
	int		rc;

	if (!rebuild_runable(arg, 6, false))
		skip();

	args[0] = arg;
	/* create/connect another pool */
	rc = test_setup((void **)&args[1], SETUP_CONT_CONNECT, arg->multi_rank,
			DEFAULT_POOL_SIZE);
	if (rc) {
		print_message("open/connect another pool failed: rc %d\n", rc);
		return;
	}

	for (i = 0; i < OBJ_NR; i++)
		oids[i] = dts_oid_gen(OBJ_CLS, arg->myrank);
	rebuild_io(args[0], oids, OBJ_NR);
	rebuild_io(args[1], oids, OBJ_NR);

	rebuild_pools_targets(args, 2, ranks_to_kill, 1);

	rebuild_io_validate(args[0], oids, OBJ_NR);
	rebuild_io_validate(args[1], oids, OBJ_NR);

	test_teardown((void **)&args[1]);

	rebuild_test_add_tgt(arg, 1, ranks_to_kill[0]);
}

static int
rebuild_destroy_container_cb(void *data)
{
	test_arg_t	*arg = data;
	int		rc = 0;
	int		rc_reduce = 0;

	if (!daos_handle_is_inval(arg->coh)) {
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
	}

	if (!uuid_is_null(arg->co_uuid)) {
		while (arg->myrank == 0) {
			rc = daos_cont_destroy(arg->poh, arg->co_uuid, 1, NULL);
			if (rc == -DER_BUSY || rc == -DER_IO) {
				print_message("Container is busy, wait\n");
				sleep(1);
				continue;
			}
			break;
		}
		print_message("container "DF_UUIDF"/"DF_UUIDF" destroyed\n",
			      DP_UUID(arg->pool_uuid), DP_UUID(arg->co_uuid));
		if (arg->multi_rank)
			MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		if (rc)
			print_message("failed to destroy container "DF_UUIDF
				      ": %d\n", DP_UUID(arg->co_uuid), rc);
		uuid_clear(arg->co_uuid);
	}
	return rc;
}

static void
rebuild_destroy_container(void **state)
{
	test_arg_t	*arg = *state;
	test_arg_t	*args[2] = { 0 };
	daos_obj_id_t	oids[OBJ_NR * 100];
	int		i;
	int		rc;

	if (!rebuild_runable(arg, 6, false))
		skip();

	args[0] = arg;
	/* create/connect another pool */
	rc = test_setup((void **)&args[1], SETUP_CONT_CONNECT, arg->multi_rank,
			DEFAULT_POOL_SIZE);
	if (rc) {
		print_message("open/connect another pool failed: rc %d\n", rc);
		return;
	}

	for (i = 0; i < OBJ_NR * 100; i++)
		oids[i] = dts_oid_gen(OBJ_CLS, arg->myrank);

	rebuild_io(args[1], oids, OBJ_NR * 100);

	args[1]->rebuild_cb = rebuild_destroy_container_cb;

	rebuild_pools_targets(args, 2, ranks_to_kill, 1);

	test_teardown((void **)&args[1]);

	rebuild_test_add_tgt(arg, 1, ranks_to_kill[0]);
}

static void
rebuild_iv_tgt_fail(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!rebuild_runable(arg, 6, false))
		skip();

	for (i = 0; i < OBJ_NR; i++)
		oids[i] = dts_oid_gen(OBJ_CLS, arg->myrank);

	rebuild_io(arg, oids, OBJ_NR);

	/* Set no hdl fail_loc on all servers */
	daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC,
			     DAOS_REBUILD_TGT_IV_UPDATE_FAIL | DAOS_FAIL_ONCE,
			     NULL);
	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_io_validate(arg, oids, OBJ_NR);
	rebuild_test_add_tgt(arg, 1, ranks_to_kill[0]);
}

static void
rebuild_tgt_start_fail(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!rebuild_runable(arg, 6, false))
		skip();

	for (i = 0; i < OBJ_NR; i++)
		oids[i] = dts_oid_gen(OBJ_CLS, arg->myrank);

	rebuild_io(arg, oids, OBJ_NR);

	/* failed to start rebuild on rank 0 */
	daos_mgmt_params_set(arg->group, 0, DSS_KEY_FAIL_LOC,
			     DAOS_REBUILD_TGT_START_FAIL | DAOS_FAIL_ONCE,
			     NULL);
	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_io_validate(arg, oids, OBJ_NR);
	rebuild_test_add_tgt(arg, 1, ranks_to_kill[0]);
	rebuild_test_add_tgt(arg, 1, 0);
}

static void
rebuild_offline(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;
	int		rc;

	if (!rebuild_runable(arg, 6, false))
		skip();

	for (i = 0; i < OBJ_NR; i++)
		oids[i] = dts_oid_gen(OBJ_CLS, arg->myrank);

	rebuild_io(arg, oids, OBJ_NR);

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

	rebuild_targets(&arg, 1, ranks_to_kill, 1, true);

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

	rebuild_io_validate(arg, oids, OBJ_NR);
}

static int
rebuild_io_cb(void *arg)
{
	test_arg_t	*test_arg = arg;
	daos_obj_id_t	*oids = test_arg->rebuild_cb_arg;

	if (!daos_handle_is_inval(test_arg->coh))
		rebuild_io(test_arg, oids, OBJ_NR);

	return 0;
}

static int
rebuild_io_post_cb(void *arg)
{
	test_arg_t	*test_arg = arg;
	daos_obj_id_t	*oids = test_arg->rebuild_post_cb_arg;

	if (!daos_handle_is_inval(test_arg->coh))
		rebuild_io_validate(test_arg, oids, OBJ_NR);

	return 0;
}

static void
rebuild_two_failures(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	daos_obj_id_t	cb_arg_oids[OBJ_NR];
	int		i;

	if (!rebuild_runable(arg, 6, false))
		skip();

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(OBJ_CLS, arg->myrank);
		cb_arg_oids[i] = dts_oid_gen(OBJ_CLS, arg->myrank);
	}

	/* prepare the data */
	rebuild_io(arg, oids, OBJ_NR);

	arg->rebuild_cb = rebuild_io_cb;
	arg->rebuild_cb_arg = cb_arg_oids;
	arg->rebuild_post_cb = rebuild_io_post_cb;
	arg->rebuild_post_cb_arg = cb_arg_oids;

	rebuild_targets(&arg, 1, ranks_to_kill, 2, true);

	arg->rebuild_cb = NULL;
	arg->rebuild_post_cb = NULL;

	/* Verify the data */
	rebuild_io_validate(arg, oids, OBJ_NR);
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
	{"REBUILD8: retry rebuild for not ready",
	rebuild_retry_rebuild, NULL, test_case_teardown},
	{"REBUILD9: drop rebuild obj reply",
	rebuild_drop_obj, NULL, test_case_teardown},
	{"REBUILD10: rebuild multiple pools",
	rebuild_multiple_pools, NULL, test_case_teardown},
	{"REBUILD11: rebuild update failed",
	rebuild_update_failed, NULL, test_case_teardown},
	{"REBUILD12: retry rebuild for pool stale",
	rebuild_retry_for_stale_pool, NULL, test_case_teardown},
	{"REBUILD13: rebuild with container destroy",
	rebuild_destroy_container, NULL, test_case_teardown},
	{"REBUILD14: rebuild iv tgt fail",
	rebuild_iv_tgt_fail, NULL, test_case_teardown},
	{"REBUILD15: rebuild tgt start fail",
	rebuild_tgt_start_fail, NULL, test_case_teardown},
	{"REBUILD16: offline rebuild",
	rebuild_offline, NULL, test_case_teardown},
	{"REBUILD17: rebuild with two failures",
	 rebuild_two_failures, NULL, test_case_teardown},
};

#define REBUILD_POOL_SIZE	(10ULL << 30)
int
rebuild_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, true, REBUILD_POOL_SIZE);
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
