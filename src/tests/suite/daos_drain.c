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
 * This file is part of daos
 *
 * tests/suite/daos_drain.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

#define KEY_NR		100
#define OBJ_NR		10
#define OBJ_CLS		OC_RP_3G1
#define OBJ_REPLICAS	3
#define DEFAULT_FAIL_TGT 0
#define REBUILD_POOL_SIZE	(4ULL << 30)

#define drain_io rebuild_io
#define drain_io_validate rebuild_io_validate
#define drain_sub_teardown rebuild_sub_teardown
#define drain_small_sub_setup rebuild_small_sub_setup
#define drain_pool_create rebuild_pool_create
#define drain_cb rebuild_cb
#define drain_post_cb_arg rebuild_post_cb_arg
#define drain_post_cb rebuild_post_cb
#define drain_pre_cb rebuild_pre_cb

/* Destroy the pool for the sub test */
static void
drain_pool_destroy(test_arg_t *arg)
{
	test_teardown((void **)&arg);
	/* make sure IV and GC release refcount on pool and free space,
	 * otherwise test might run into ENOSPACE
	 */
	sleep(1);
}
/** Drain with a failure condition
 * Start
 */
static void
drain_drop_scan(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		tgt = DEFAULT_FAIL_TGT;
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
		oids[i] = dts_oid_set_tgt(oids[i], tgt);
	}

	drain_io(arg, oids, OBJ_NR);

	/* Set drop scan fail_loc on server 0 */
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, 0, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_NO_HDL | DAOS_FAIL_ONCE,
				     0, NULL);

	MPI_Barrier(MPI_COMM_WORLD);
	drain_single_pool_target(arg, ranks_to_kill[0], tgt);
	drain_io_validate(arg, oids, OBJ_NR, true);

	reintegrate_single_pool_target(arg, ranks_to_kill[0], tgt);
	drain_io_validate(arg, oids, OBJ_NR, true);
}

static void
retry_drain(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		tgt = DEFAULT_FAIL_TGT;
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
		oids[i] = dts_oid_set_tgt(oids[i], tgt);
	}

	drain_io(arg, oids, OBJ_NR);

	/* Set no hdl fail_loc on all servers */
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_NO_HDL | DAOS_FAIL_ONCE,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	drain_single_pool_target(arg, ranks_to_kill[0], tgt);
	drain_io_validate(arg, oids, OBJ_NR, true);

	reintegrate_single_pool_target(arg, ranks_to_kill[0], tgt);
	drain_io_validate(arg, oids, OBJ_NR, true);
}

static void
drain_retry_for_stale_pool(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	drain_io(arg, oids, OBJ_NR);

	if (arg->myrank == 0) {
		d_rank_t rank;

		/* make one shard to return STALE for drain fetch */
		rank = get_rank_by_oid_shard(arg, oids[0], 1);
		daos_mgmt_set_params(arg->group, rank, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_STALE_POOL | DAOS_FAIL_ONCE,
				     0, NULL);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	drain_single_pool_rank(arg, ranks_to_kill[0]);
	drain_io_validate(arg, oids, OBJ_NR, true);

	reintegrate_single_pool_rank(arg, ranks_to_kill[0]);
	drain_io_validate(arg, oids, OBJ_NR, true);
}

static void
drain_drop_obj(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	drain_io(arg, oids, OBJ_NR);

	/* Set drop REBUILD_OBJECTS reply on all servers */
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, 0, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_DROP_OBJ | DAOS_FAIL_ONCE,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	drain_single_pool_rank(arg, ranks_to_kill[0]);
	drain_io_validate(arg, oids, OBJ_NR, true);

	reintegrate_single_pool_rank(arg, ranks_to_kill[0]);
	drain_io_validate(arg, oids, OBJ_NR, true);
}

static void
drain_update_failed(void **state)
{
	print_message("Skipping until fixed");
	skip();
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		tgt = DEFAULT_FAIL_TGT;
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
		oids[i] = dts_oid_set_tgt(oids[i], tgt);
	}

	drain_io(arg, oids, OBJ_NR);

	/* Set drop scan reply on all servers */
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, 0, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_UPDATE_FAIL | DAOS_FAIL_ONCE,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	print_message("Draining pool target, rank: %d, target: %d",
		      ranks_to_kill[0], tgt);
	drain_single_pool_target(arg, ranks_to_kill[0], tgt);
	drain_io_validate(arg, oids, OBJ_NR, true);

	print_message("Reintegrating pool target, rank: %d, target: %d",
		      ranks_to_kill[0], tgt);
	reintegrate_single_pool_target(arg, ranks_to_kill[0], tgt);
	drain_io_validate(arg, oids, OBJ_NR, true);
}

/** End */


/** Drain multiple pools - no errors  */
static void
drain_multiple_pools(void **state)
{
	test_arg_t	*arg = *state;
	test_arg_t	*args[2] = { 0 };
	daos_obj_id_t	oids[OBJ_NR];
	int		i;
	int		rc;

	if (!test_runable(arg, 6))
		return;

	args[0] = arg;
	/* create/connect another pool */
	rc = drain_pool_create(&args[1], arg, SETUP_CONT_CONNECT, NULL);
	if (rc)
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	drain_io(args[0], oids, OBJ_NR);
	drain_io(args[1], oids, OBJ_NR);

	drain_pools_ranks(args, 2, ranks_to_kill, 1);

	drain_io_validate(args[0], oids, OBJ_NR, true);
	drain_io_validate(args[1], oids, OBJ_NR, true);

	reintegrate_pools_ranks(args, 2, ranks_to_kill, 1);
	drain_io_validate(args[0], oids, OBJ_NR, true);
	drain_io_validate(args[1], oids, OBJ_NR, true);

	drain_pool_destroy(args[1]);
}

static int
drain_close_container_cb(void *data)
{
	test_arg_t	*arg = data;
	int		rc = 0;
	int		rc_reduce = 0;

	if (daos_handle_is_inval(arg->coh))
		return 0;

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

	return 0;
}

static int
drain_destroy_container_cb(void *data)
{
	test_arg_t	*arg = data;
	int		rc = 0;

	if (uuid_is_null(arg->co_uuid))
		return 0;

	rc = drain_close_container_cb(data);
	if (rc)
		return rc;

	while (arg->myrank == 0) {
		rc = daos_cont_destroy(arg->pool.poh, arg->co_uuid, 1, NULL);
		if (rc == -DER_BUSY || rc == -DER_IO) {
			print_message("Container is busy, wait\n");
			sleep(1);
			continue;
		}
		break;
	}
	print_message("container "DF_UUIDF"/"DF_UUIDF" destroyed\n",
		      DP_UUID(arg->pool.pool_uuid), DP_UUID(arg->co_uuid));
	if (arg->multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		print_message("failed to destroy container "DF_UUIDF
			      ": %d\n", DP_UUID(arg->co_uuid), rc);
	uuid_clear(arg->co_uuid);

	return rc;
}

static void
drain_destroy_container(void **state)
{
	test_arg_t	*arg = *state;
	test_arg_t	*new_arg = NULL;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;
	int		rc;

	if (!test_runable(arg, 6))
		return;

	/* create/connect another pool */
	rc = drain_pool_create(&new_arg, arg, SETUP_CONT_CONNECT, NULL);
	if (rc)
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	drain_io(new_arg, oids, OBJ_NR);

	new_arg->drain_cb = drain_destroy_container_cb;

	drain_single_pool_rank(new_arg, ranks_to_kill[0]);


	drain_pool_destroy(new_arg);
}

static void
drain_close_container(void **state)
{
	test_arg_t	*arg = *state;
	test_arg_t	*new_arg = NULL;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;
	int		rc;

	if (!test_runable(arg, 6))
		return;

	/* create/connect another pool */
	rc = drain_pool_create(&new_arg, arg, SETUP_CONT_CONNECT, NULL);
	if (rc)
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	drain_io(new_arg, oids, OBJ_NR);

	new_arg->rebuild_pre_cb = drain_close_container_cb;

	rebuild_single_pool_rank(new_arg, ranks_to_kill[0], false);

	drain_pool_destroy(new_arg);
}

static int
drain_destroy_pool_cb(void *data)
{
	test_arg_t	*arg = data;
	int		rc = 0;

	rebuild_pool_disconnect_internal(data);

	if (arg->myrank == 0) {
		/* Disable fail_loc and start rebuild */
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     0, 0, NULL);
		rc = dmg_pool_destroy(dmg_config_file, arg->pool.pool_uuid,
				      NULL, true);
		if (rc) {
			print_message("failed to destroy pool"DF_UUIDF" %d\n",
				      DP_UUID(arg->pool.pool_uuid), rc);
			goto out;
		}
	}

	arg->pool.destroyed = true;
	print_message("pool destroyed "DF_UUIDF"\n",
		      DP_UUID(arg->pool.pool_uuid));

	MPI_Barrier(MPI_COMM_WORLD);

out:
	return rc;
}

static void
drain_destroy_pool_internal(void **state, uint64_t fail_loc)
{
	test_arg_t	*arg = *state;
	test_arg_t	*new_arg = NULL;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;
	int		rc;

	if (!test_runable(arg, 6))
		return;

	rc = drain_pool_create(&new_arg, arg, SETUP_CONT_CONNECT, NULL);
	if (rc)
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	drain_io(new_arg, oids, OBJ_NR);

	/* hang the rebuild */
	if (arg->myrank == 0) {
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, fail_loc,
				     0, NULL);
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE, 5,
				     0, NULL);
	}

	new_arg->drain_cb = drain_destroy_pool_cb;

	rebuild_single_pool_rank(new_arg, ranks_to_kill[0], false);
}

static void
drain_destroy_pool_during_scan(void ** state)
{
	return drain_destroy_pool_internal(state, DAOS_REBUILD_TGT_SCAN_HANG);
}

static void
drain_destroy_pool_during_rebuild(void ** state)
{
	return drain_destroy_pool_internal(state,
					     DAOS_REBUILD_TGT_REBUILD_HANG);
}

static void
drain_iv_tgt_fail(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	drain_io(arg, oids, OBJ_NR);

	/* Set no hdl fail_loc on all servers */
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_TGT_IV_UPDATE_FAIL |
				     DAOS_FAIL_ONCE, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	rebuild_single_pool_rank(arg, ranks_to_kill[0], false);
	drain_io_validate(arg, oids, OBJ_NR, true);

	reintegrate_single_pool_rank(arg, ranks_to_kill[0]);
	drain_io_validate(arg, oids, OBJ_NR, true);
}

static void
drain_tgt_start_fail(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	d_rank_t	exclude_rank = 0;
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	drain_io(arg, oids, OBJ_NR);

	/* failed to start rebuild on rank 0 */
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, exclude_rank,
				     DMG_KEY_FAIL_LOC,
				  DAOS_REBUILD_TGT_START_FAIL | DAOS_FAIL_ONCE,
				  0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	/* Rebuild rank 1 */
	drain_single_pool_rank(arg, ranks_to_kill[0]);
}

static void
drain_send_objects_fail(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	drain_io(arg, oids, OBJ_NR);

	/* Skip object send on all of the targets */
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_TGT_SEND_OBJS_FAIL, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	/* Even do not sending the objects, the rebuild should still be
	 * able to finish.
	 */
	rebuild_single_pool_rank(arg, ranks_to_kill[0], false);

	/* failed to start rebuild on rank 0 */
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	reintegrate_single_pool_rank(arg, ranks_to_kill[0]);
	rebuild_add_back_tgts(arg, ranks_to_kill[0], NULL, 1);
}


static int
drain_pool_disconnect_cb(void *data)
{
	test_arg_t	*arg = data;

	rebuild_pool_disconnect_internal(data);

	/* Disable fail_loc and start rebuild */
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     0, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	return 0;
}

static int
drain_add_tgt_pool_connect_internal(void *data)
{
	test_arg_t *arg = data;

	/**
	 * add targets before pool connect to make sure container is opened
	 * on all servers.
	 */
	rebuild_add_back_tgts(arg, ranks_to_kill[0], NULL, 1);

	return rebuild_pool_connect_internal(data);
}

static void
drain_tgt_pool_disconnect_internal(void **state, unsigned int fail_loc)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	drain_io(arg, oids, OBJ_NR);

	/* hang the rebuild during scan */
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, fail_loc,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	/* NB: During the test, one target will be excluded from the pool map,
	 * then container/pool will be closed/disconnectd during the rebuild,
	 * i.e. before the target is added back. so the container hdl cache
	 * will be left on the excluded target after the target is added back.
	 * So the container might not be able to destroyed because of the left
	 * over container hdl. Once the container is able to evict the container
	 * hdl, then this issue can be fixed. XXX
	 */
	arg->drain_cb = drain_pool_disconnect_cb;
	arg->drain_post_cb = drain_add_tgt_pool_connect_internal;

	rebuild_single_pool_rank(arg, ranks_to_kill[0], false);

	arg->drain_cb = NULL;
	arg->drain_post_cb = NULL;

}

static void
drain_tgt_pool_disconnect_in_scan(void **state)
{
	drain_tgt_pool_disconnect_internal(state,
					     DAOS_REBUILD_TGT_SCAN_HANG);
}

static void
drain_tgt_pool_disconnect_in_rebuild(void **state)
{
	drain_tgt_pool_disconnect_internal(state,
					     DAOS_REBUILD_TGT_REBUILD_HANG);
}

static int
drain_pool_connect_cb(void *data)
{
	test_arg_t	*arg = data;

	rebuild_pool_connect_internal(data);
	/* Disable fail_loc and start rebuild */
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     0, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	return 0;
}

static void
drain_offline_pool_connect_internal(void **state, unsigned int fail_loc)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	drain_io(arg, oids, OBJ_NR);

	/* hang the rebuild */
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, fail_loc,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	arg->rebuild_pre_cb = rebuild_pool_disconnect_internal;
	arg->drain_cb = drain_pool_connect_cb;

	drain_single_pool_rank(arg, ranks_to_kill[0]);

	arg->rebuild_pre_cb = NULL;
	arg->drain_cb = NULL;

	drain_io_validate(arg, oids, OBJ_NR, true);

	reintegrate_single_pool_rank(arg, ranks_to_kill[0]);
}

static void
drain_offline_pool_connect_in_scan(void **state)
{
	drain_offline_pool_connect_internal(state,
					      DAOS_REBUILD_TGT_SCAN_HANG);
}

static void
drain_offline_pool_connect_in_rebuild(void **state)
{
	drain_offline_pool_connect_internal(state,
					      DAOS_REBUILD_TGT_REBUILD_HANG);
}

static void
drain_offline(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6)) {
		fail_msg("Should be enough right now");
		return;
	}

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}
	drain_io(arg, oids, OBJ_NR);

	arg->drain_pre_cb = rebuild_pool_disconnect_internal;
	arg->drain_post_cb = rebuild_pool_connect_internal;

	drain_single_pool_rank(arg, ranks_to_kill[0]);

	arg->drain_pre_cb = NULL;
	arg->drain_post_cb = NULL;

	drain_io_validate(arg, oids, OBJ_NR, true);

	reintegrate_pools_ranks(&arg, 1, ranks_to_kill, MAX_KILLS);
}

static void
drain_offline_empty(void **state)
{
	test_arg_t	*arg = *state;
	test_arg_t	*new_arg = NULL;
	int		rc;

	if (!test_runable(arg, 6))
		return;

	rc = drain_pool_create(&new_arg, arg, SETUP_POOL_CREATE, NULL);
	if (rc)
		return;

	rebuild_single_pool_rank(new_arg, ranks_to_kill[0], false);
	drain_pool_destroy(new_arg);

}

static int
drain_change_leader_cb(void *arg)
{
	test_arg_t	*test_arg = arg;
	d_rank_t	leader;

	test_get_leader(test_arg, &leader);

	/* Skip appendentry to re-elect the leader */
	if (test_arg->myrank == 0) {
		daos_mgmt_set_params(test_arg->group, leader, DMG_KEY_FAIL_LOC,
				     DAOS_RDB_SKIP_APPENDENTRIES_FAIL, 0, NULL);
		print_message("sleep 15 seconds for re-election leader\n");
		/* Sleep 15 seconds to make sure the leader is changed */
		sleep(15);
		/* Continue the rebuild */
		daos_mgmt_set_params(test_arg->group, -1, DMG_KEY_FAIL_LOC,
				     0, 0, NULL);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	return 0;
}

static void
drain_master_change_during_scan(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6) || arg->pool.alive_svc->rl_nr == 1)
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	drain_io(arg, oids, OBJ_NR);

	/* All ranks should wait before rebuild */
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_TGT_SCAN_HANG, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	arg->drain_cb = drain_change_leader_cb;

	rebuild_single_pool_rank(arg, ranks_to_kill[0], false);

	/* Verify the data */
	drain_io_validate(arg, oids, OBJ_NR, true);
}

static void
drain_master_change_during_rebuild(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6) || arg->pool.alive_svc->rl_nr == 1)
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	drain_io(arg, oids, OBJ_NR);

	/* All ranks should wait before rebuild */
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_TGT_REBUILD_HANG, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	arg->drain_cb = drain_change_leader_cb;

	rebuild_single_pool_rank(arg, ranks_to_kill[0], false);

	/* Verify the data */
	drain_io_validate(arg, oids, OBJ_NR, true);
}

static int
drain_nospace_cb(void *data)
{
	test_arg_t	*arg = data;

	/* Wait for space is claimed */
	sleep(60);

	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     0, 0, NULL);

	print_message("re-enable recovery\n");
	if (arg->myrank == 0)
		/* Resume the rebuild. FIXME: fix this once we have better
		 * way to resume rebuild through mgmt cmd.
		 */
		daos_mgmt_set_params(arg->group, -1,
				     DMG_KEY_REBUILD_THROTTLING, 30, 0, NULL);

	MPI_Barrier(MPI_COMM_WORLD);

	return 0;
}

static void
drain_nospace(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6) || true) /* skip for now */
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	drain_io(arg, oids, OBJ_NR);

	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_TGT_NOSPACE, 0, NULL);

	MPI_Barrier(MPI_COMM_WORLD);

	arg->drain_cb = drain_nospace_cb;
	rebuild_single_pool_rank(arg, ranks_to_kill[0], false);

	arg->drain_cb = NULL;
	drain_io_validate(arg, oids, OBJ_NR, true);

	reintegrate_single_pool_rank(arg, ranks_to_kill[0]);
	drain_io_validate(arg, oids, OBJ_NR, true);
}

static void
drain_multiple_tgts(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct daos_obj_layout *layout;
	d_rank_t	leader;
	d_rank_t	exclude_ranks[2] = { 0 };
	int		i;

	if (!test_runable(arg, 6))
		return;

	oid = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);

	drain_io(arg, &oid, 1);

	test_get_leader(arg, &leader);
	daos_obj_layout_get(arg->coh, oid, &layout);

	if (arg->myrank == 0) {
		int fail_cnt = 0;

		/* All ranks should wait before rebuild */
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_HANG, 0, NULL);
		/* kill 2 ranks at the same time */
		D_ASSERT(layout->ol_shards[0]->os_replica_nr > 2);
		for (i = 0; i < 3; i++) {
			d_rank_t rank = layout->ol_shards[0]->os_ranks[i];

			if (rank != leader) {
				exclude_ranks[fail_cnt] = rank;
				daos_exclude_server(arg->pool.pool_uuid,
						    arg->group,
						    arg->dmg_config,
						    arg->pool.svc,
						    rank);
				if (++fail_cnt >= 2)
					break;
			}
		}

		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	}

	MPI_Barrier(MPI_COMM_WORLD);

	/* Rebuild 2 ranks at the same time */
	if (arg->myrank == 0)
		test_rebuild_wait(&arg, 1);

	/* Verify the data */
	drain_io_validate(arg, &oid, 1, true);

	daos_obj_layout_free(layout);

	/* Add back the target if it is not being killed */
	if (arg->myrank == 0) {
		for (i = 0; i < 2; i++)
			daos_reint_server(arg->pool.pool_uuid, arg->group,
					  arg->dmg_config, arg->pool.svc,
					  exclude_ranks[i]);
	}
	MPI_Barrier(MPI_COMM_WORLD);
}

#if 0
static int
drain_io_cb(void *arg)
{
	test_arg_t	*test_arg = arg;
	daos_obj_id_t	*oids = test_arg->drain_cb_arg;

	if (!daos_handle_is_inval(test_arg->coh))
		drain_io(test_arg, oids, OBJ_NR);

	return 0;
}

static int
drain_io_post_cb(void *arg)
{
	test_arg_t	*test_arg = arg;
	daos_obj_id_t	*oids = test_arg->drain_post_cb_arg;

	if (!daos_handle_is_inval(test_arg->coh))
		drain_io_validate(test_arg, oids, OBJ_NR, true);

	return 0;
}
#endif

static void
drain_master_failure(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oids[OBJ_NR];
	daos_pool_info_t	pinfo = {0};
	daos_pool_info_t	pinfo_new = {0};
	int			i;
	int			rc;

	/* need 5 svc replicas, as will kill the leader 2 times */
	if (!test_runable(arg, 6) || arg->pool.alive_svc->rl_nr < 5) {
		print_message("testing skipped ...\n");
		return;
	}

	test_get_leader(arg, &ranks_to_kill[0]);
	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	/* prepare the data */
	drain_io(arg, oids, OBJ_NR);

	drain_single_pool_rank(arg, ranks_to_kill[0]);

	/* Verify the data */
	drain_io_validate(arg, oids, OBJ_NR, true);

	/* Verify the POOL_QUERY get same status after leader change */
	pinfo.pi_bits = DPI_REBUILD_STATUS;
	rc = test_pool_get_info(arg, &pinfo);
	assert_int_equal(rc, 0);
	assert_int_equal(pinfo.pi_rebuild_st.rs_done, 1);
	rc = drain_change_leader_cb(arg);
	assert_int_equal(rc, 0);
	pinfo_new.pi_bits = DPI_REBUILD_STATUS;
	rc = test_pool_get_info(arg, &pinfo_new);
	assert_int_equal(rc, 0);
	assert_int_equal(pinfo_new.pi_rebuild_st.rs_done, 1);
	rc = memcmp(&pinfo.pi_rebuild_st, &pinfo_new.pi_rebuild_st,
		    sizeof(pinfo.pi_rebuild_st));
	if (rc != 0) {
		print_message("old ver %u seconds %u err %d done %d fail %d"
			      " tobeobj "DF_U64" obj "DF_U64" rec "DF_U64
			      " sz "DF_U64"\n",
			      pinfo.pi_rebuild_st.rs_version,
			      pinfo.pi_rebuild_st.rs_seconds,
			      pinfo.pi_rebuild_st.rs_errno,
			      pinfo.pi_rebuild_st.rs_done,
			      pinfo.pi_rebuild_st.rs_fail_rank,
			      pinfo.pi_rebuild_st.rs_toberb_obj_nr,
			      pinfo.pi_rebuild_st.rs_obj_nr,
			      pinfo.pi_rebuild_st.rs_rec_nr,
			      pinfo.pi_rebuild_st.rs_size);
		print_message("new ver %u seconds %u err %d done %d fail %d"
			      " tobeobj "DF_U64" obj "DF_U64" rec "DF_U64
			      " sz "DF_U64"\n",
			      pinfo_new.pi_rebuild_st.rs_version,
			      pinfo_new.pi_rebuild_st.rs_seconds,
			      pinfo_new.pi_rebuild_st.rs_errno,
			      pinfo_new.pi_rebuild_st.rs_done,
			      pinfo_new.pi_rebuild_st.rs_fail_rank,
			      pinfo_new.pi_rebuild_st.rs_toberb_obj_nr,
			      pinfo_new.pi_rebuild_st.rs_obj_nr,
			      pinfo_new.pi_rebuild_st.rs_rec_nr,
			      pinfo_new.pi_rebuild_st.rs_size);
	}

	print_message("svc leader changed from %d to %d, should get same "
		      "rebuild status (memcmp result %d).\n", pinfo.pi_leader,
		      pinfo_new.pi_leader, rc);
	assert_int_equal(rc, 0);
}

static void
drain_multiple_failures(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	daos_obj_id_t	cb_arg_oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6)) {
		fail_msg("Should be enough right now");
		return;
	}

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
		cb_arg_oids[i] = dts_oid_gen(OBJ_CLS, 0, arg->myrank);
	}

	/* prepare the data */
	drain_io(arg, oids, OBJ_NR);

#if 0
	/* Remove this inflight IO temporarily XXX */
	arg->drain_cb = drain_io_cb;
	arg->drain_cb_arg = cb_arg_oids;
	/* Disable data validation because of DAOS-2915. */
	arg->drain_post_cb = drain_io_post_cb;
#else
	arg->drain_post_cb = NULL;
#endif
	arg->drain_post_cb_arg = cb_arg_oids;

	drain_pools_ranks(&arg, 1, ranks_to_kill, MAX_KILLS);

	arg->drain_cb = NULL;
	arg->drain_post_cb = NULL;
	reintegrate_pools_ranks(&arg, 1, ranks_to_kill, MAX_KILLS);
}

static void
drain_fail_all_replicas_before_drain(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct daos_obj_layout *layout;
	struct daos_obj_shard *shard;

	if (!test_runable(arg, 6) || arg->pool.alive_svc->rl_nr < 3)
		return;

	oid = dts_oid_gen(DAOS_OC_R2S_SPEC_RANK, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);

	drain_io(arg, &oid, 1);

	daos_obj_layout_get(arg->coh, oid, &layout);

	/* HOLD rebuild ULT */
	daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			     DAOS_REBUILD_HANG, 0, NULL);

	/* Kill one replica and start rebuild */
	shard = layout->ol_shards[0];
	daos_kill_server(arg, arg->pool.pool_uuid, arg->group,
			 arg->pool.alive_svc, shard->os_ranks[0]);

	/* Sleep 10 seconds after it scan finish and hang before rebuild */
	print_message("sleep 10 seconds to wait scan to be finished \n");
	sleep(10);

	/* Then kill rank on shard1 */
	/* NB: we can not kill rank 0, otherwise the following set_params
	 * will fail and also pool destroy will not work.
	 */
	if (shard->os_ranks[1] != 0)
		daos_kill_server(arg, arg->pool.pool_uuid, arg->group,
				 arg->pool.alive_svc, shard->os_ranks[1]);

	/* Continue rebuild */
	daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);

	/* Sleep long enough to make sure the 2nd rebuild caused by 2nd kill is
	 * triggered.
	 */
	sleep(15);
	if (arg->myrank == 0)
		test_rebuild_wait(&arg, 1);

	MPI_Barrier(MPI_COMM_WORLD);
	daos_obj_layout_free(layout);
}

static void
drain_fail_all_replicas(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct daos_obj_layout *layout;
	int		i;

	/* This test will kill 3 replicas, which might include the ranks
	 * in svcs, so make sure there are at least 6 ranks in svc, so
	 * the new leader can be chosen.
	 */
	if (!test_runable(arg, 6) || arg->pool.alive_svc->rl_nr < 6) {
		print_message("need at least 6 svcs, -s5\n");
		return;
	}

	oid = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);

	drain_io(arg, &oid, 1);

	daos_obj_layout_get(arg->coh, oid, &layout);

	for (i = 0; i < layout->ol_nr; i++) {
		int j;

		for (j = 0; j < layout->ol_shards[i]->os_replica_nr; j++) {
			d_rank_t rank = layout->ol_shards[i]->os_ranks[j];

			daos_kill_server(arg, arg->pool.pool_uuid,
					 arg->group, arg->pool.alive_svc,
					 rank);
		}
	}

	sleep(15);
	if (arg->myrank == 0)
		test_rebuild_wait(&arg, 1);

	MPI_Barrier(MPI_COMM_WORLD);
	daos_obj_layout_free(layout);
}

static void
multi_pools_rebuild_concurrently(void **state)
{
#define POOL_NUM		4
#define CONT_PER_POOL		2
#define OBJ_PER_CONT		8
	test_arg_t		*arg = *state;
	test_arg_t		*args[POOL_NUM * CONT_PER_POOL] = { 0 };
	daos_obj_id_t		oids[OBJ_PER_CONT];
	struct test_pool	*pool;
	int			i;
	int			rc;

	if (!test_runable(arg, 6))
		return;

	memset(args, 0, sizeof(args[0]) * POOL_NUM * CONT_PER_POOL);
	for (i = 0; i < POOL_NUM * CONT_PER_POOL; i++) {
		pool = (i % CONT_PER_POOL == 0) ? NULL :
				&args[(i/CONT_PER_POOL) * CONT_PER_POOL]->pool;
		rc = drain_pool_create(&args[i], arg, SETUP_CONT_CONNECT,
					 pool);
		if (rc)
			goto out;

		if (i % CONT_PER_POOL == 0)
			assert_int_equal(args[i]->pool.slave, 0);
		else
			assert_int_equal(args[i]->pool.slave, 1);
	}

	for (i = 0; i < OBJ_PER_CONT; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	for (i = 0; i < POOL_NUM * CONT_PER_POOL; i++)
		drain_io(args[i], oids, OBJ_PER_CONT);

	rebuild_pools_ranks(args, POOL_NUM * CONT_PER_POOL, ranks_to_kill, 1,
			false);

	for (i = POOL_NUM * CONT_PER_POOL - 1; i >= 0; i--)
		drain_io_validate(args[i], oids, OBJ_PER_CONT, true);

out:
	for (i = POOL_NUM * CONT_PER_POOL - 1; i >= 0; i--)
		drain_pool_destroy(args[i]);
}

static void
make_fail(void **state)
{
	test_arg_t *arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}


//	drain_single_pool_rank(arg, 0, false);
//	drain_single_pool_rank(arg, 0, false);
//	drain_single_pool_rank(arg, 0, false);
//	drain_single_pool_rank(arg, 0, false);

	for (i = 0; i < 20; i++) {
		d_rank_t rank_to_drain = i % (arg->srv_nnodes);

		print_message("Iteration %d (srv_nodes: %d)\n", i, arg->srv_nnodes);
		drain_io(arg, oids, OBJ_NR);
		print_message("Draining rank: %d\n", rank_to_drain);
		drain_single_pool_rank(arg, rank_to_drain);
		drain_io_validate(arg, oids, OBJ_NR, false);
		reintegrate_single_pool_rank(arg, rank_to_drain);
		drain_io_validate(arg, oids, OBJ_NR, false);
	}
}

/* [todo-ryon]: Tests ...
 * - reintegration pool rank that doesn't need it
 * - drain same rank multiple times
 *
 */

/** create a new pool/container for each test */
static const struct CMUnitTest rebuild_tests[] = {
	{"DRAIN0: drop rebuild scan reply",
		drain_drop_scan,            drain_small_sub_setup, drain_sub_teardown},
	{"DRAIN1: retry rebuild for not ready",
		retry_drain,                drain_small_sub_setup, drain_sub_teardown},
	{"DRAIN2: drop rebuild obj reply",
		drain_drop_obj,             drain_small_sub_setup, drain_sub_teardown},
	{"DRAIN3: rebuild multiple pools",
		drain_multiple_pools,       rebuild_sub_setup,       drain_sub_teardown},
	{"DRAIN4: rebuild update failed",
		drain_update_failed,        drain_small_sub_setup, drain_sub_teardown},
	{"DRAIN5: retry rebuild for pool stale",
		drain_retry_for_stale_pool, drain_small_sub_setup, drain_sub_teardown},
	{"DRAIN6: rebuild with container destroy",
		drain_destroy_container,    rebuild_sub_setup,       drain_sub_teardown},
	{"DRAIN7: rebuild with container close",
		drain_close_container, drain_small_sub_setup, drain_sub_teardown},
	{"DRAIN8: rebuild with pool destroy during scan",
		drain_destroy_pool_during_scan, rebuild_sub_setup,
		drain_sub_teardown},
	{"DRAIN9: rebuild with pool destroy during rebuild",
		drain_destroy_pool_during_rebuild, rebuild_sub_setup,
		drain_sub_teardown},
	{"DRAIN10: rebuild iv tgt fail",
		drain_iv_tgt_fail, drain_small_sub_setup, drain_sub_teardown},
	{"DRAIN11: rebuild tgt start fail",
		drain_tgt_start_fail, drain_small_sub_setup, drain_sub_teardown},
	{"DRAIN12: rebuild send objects failed",
		drain_send_objects_fail, drain_small_sub_setup,
		drain_sub_teardown},
	{"DRAIN13: rebuild empty pool offline",
		drain_offline_empty, drain_small_sub_setup, drain_sub_teardown},
	{"DRAIN14: rebuild no space failure",
		drain_nospace, drain_small_sub_setup, drain_sub_teardown},
	{"DRAIN15: rebuild multiple tgts",
		drain_multiple_tgts, drain_small_sub_setup, drain_sub_teardown},
	{"DRAIN16: disconnect pool during scan",
		drain_tgt_pool_disconnect_in_scan, drain_small_sub_setup,
		drain_sub_teardown},
	{"DRAIN17: disconnect pool during rebuild",
		drain_tgt_pool_disconnect_in_rebuild, drain_small_sub_setup,
		drain_sub_teardown},
	{"DRAIN18: multi-pools rebuild concurrently",
		multi_pools_rebuild_concurrently, rebuild_sub_setup,
		drain_sub_teardown},
	{"DRAIN19: rebuild with master change during scan",
		drain_master_change_during_scan, rebuild_sub_setup,
		drain_sub_teardown},
	{"DRAIN20: rebuild with master change during rebuild",
		drain_master_change_during_rebuild, rebuild_sub_setup,
		drain_sub_teardown},
	{"DRAIN21: rebuild with master failure",
		drain_master_failure, rebuild_sub_setup, drain_sub_teardown},
	{"DRAIN22: connect pool during scan for offline rebuild",
		drain_offline_pool_connect_in_scan,    rebuild_sub_setup,
		drain_sub_teardown},
	{"DRAIN23: connect pool during rebuild for offline rebuild",
		drain_offline_pool_connect_in_rebuild, rebuild_sub_setup,
		drain_sub_teardown},
	{"DRAIN24: offline rebuild",
		drain_offline,                         rebuild_sub_setup, drain_sub_teardown},
	{"DRAIN25: rebuild with two failures",
		drain_multiple_failures,               rebuild_sub_setup, drain_sub_teardown},
	{"DRAIN26: rebuild fail all replicas before rebuild",
		drain_fail_all_replicas_before_drain,  rebuild_sub_setup,
		drain_sub_teardown},
	{"DRAIN27: rebuild fail all replicas",
		drain_fail_all_replicas,               rebuild_sub_setup, drain_sub_teardown},
//	{"WIP: make fail",
//		make_fail, rebuild_sub_setup, drain_sub_teardown},

};

/* TODO: Enable aggregation once stable view rebuild is done. */
//int
//rebuild_test_setup(void **state)
//{
//	test_arg_t	*arg;
//	int rc;
//
//	rc = test_setup(state, SETUP_CONT_CONNECT, true, REBUILD_POOL_SIZE,
//			0, NULL);
//	if (rc)
//		return rc;
//
//	arg = *state;
//	if (arg && arg->myrank == 0)
//		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
//				     1, 0, NULL);
//	MPI_Barrier(MPI_COMM_WORLD);
//	return 0;
//}
//
//int
//rebuild_test_teardown(void **state)
//{
//	test_arg_t	*arg = *state;
//
//	if (arg && arg->myrank == 0)
//		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
//				     0, 0, NULL);
//	MPI_Barrier(MPI_COMM_WORLD);
//
//	test_teardown(state);
//	return 0;
//}

int
run_daos_drain_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(rebuild_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests_only("DAOS rebuild tests", rebuild_tests,
				     ARRAY_SIZE(rebuild_tests), sub_tests,
				     sub_tests_size);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
