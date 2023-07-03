/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

#define KEY_NR		100
#define OBJ_NR		10
#define OBJ_CLS		OC_RP_3G1
#define OBJ_REPLICAS	3
#define DEFAULT_FAIL_TGT 0

/* Destroy the pool for the sub test */
static void
rebuild_pool_destroy(test_arg_t *arg)
{
	test_teardown((void **)&arg);
	/* make sure IV and GC release refcount on pool and free space,
	 * otherwise rebuild test might run into ENOSPACE
	 */
	sleep(1);
}

static void
rebuild_drop_scan(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		tgt = DEFAULT_FAIL_TGT;
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
		oids[i] = dts_oid_set_tgt(oids[i], tgt);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* Set drop scan fail_loc on server 0 */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, 0, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_NO_HDL | DAOS_FAIL_ONCE,
				     0, NULL);

	par_barrier(PAR_COMM_WORLD);
	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);
	rebuild_io_verify(arg, oids, OBJ_NR);

	reintegrate_single_pool_target(arg, ranks_to_kill[0], tgt);
	rebuild_io_verify(arg, oids, OBJ_NR);
}

static void
rebuild_retry_rebuild(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		tgt = DEFAULT_FAIL_TGT;
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
		oids[i] = dts_oid_set_tgt(oids[i], tgt);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* Set no hdl fail_loc on all servers */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_NO_HDL | DAOS_FAIL_ONCE,
				     0, NULL);
	par_barrier(PAR_COMM_WORLD);
	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);
	rebuild_io_verify(arg, oids, OBJ_NR);

	reintegrate_single_pool_target(arg, ranks_to_kill[0], tgt);
	rebuild_io_verify(arg, oids, OBJ_NR);
}

static void
rebuild_retry_for_stale_pool(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	if (arg->myrank == 0) {
		d_rank_t rank;

		/* make one shard to return STALE for rebuild fetch */
		rank = get_rank_by_oid_shard(arg, oids[0], 1);
		daos_debug_set_params(arg->group, rank, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_STALE_POOL | DAOS_FAIL_ONCE,
				     0, NULL);
	}

	par_barrier(PAR_COMM_WORLD);
	rebuild_single_pool_rank(arg, ranks_to_kill[0], false);
	rebuild_io_verify(arg, oids, OBJ_NR);

	reintegrate_single_pool_rank(arg, ranks_to_kill[0], false);
	rebuild_io_verify(arg, oids, OBJ_NR);
}

static void
rebuild_drop_obj(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* Set drop REBUILD_OBJECTS reply on all servers */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, 0, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_DROP_OBJ | DAOS_FAIL_ONCE,
				     0, NULL);
	par_barrier(PAR_COMM_WORLD);
	rebuild_single_pool_rank(arg, ranks_to_kill[0], false);
	rebuild_io_verify(arg, oids, OBJ_NR);

	reintegrate_single_pool_rank(arg, ranks_to_kill[0], false);
	rebuild_io_verify(arg, oids, OBJ_NR);
}

static void
rebuild_update_failed(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		tgt = DEFAULT_FAIL_TGT;
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
		oids[i] = dts_oid_set_tgt(oids[i], tgt);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* Set drop scan reply on all servers */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, 0, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_UPDATE_FAIL | DAOS_FAIL_ONCE,
				     0, NULL);
	par_barrier(PAR_COMM_WORLD);
	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);
	reintegrate_single_pool_target(arg, ranks_to_kill[0], tgt);
}

static void
rebuild_multiple_pools(void **state)
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
	rc = rebuild_pool_create(&args[1], arg, SETUP_CONT_CONNECT, NULL);
	if (rc)
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(args[0], oids, OBJ_NR);
	rebuild_io(args[1], oids, OBJ_NR);

	rebuild_pools_ranks(args, 2, ranks_to_kill, 1, false);

	rebuild_io_verify(args[0], oids, OBJ_NR);
	rebuild_io_verify(args[1], oids, OBJ_NR);

	reintegrate_pools_ranks(args, 2, ranks_to_kill, 1, false);
	rebuild_io_verify(args[0], oids, OBJ_NR);
	rebuild_io_verify(args[1], oids, OBJ_NR);

	rebuild_pool_destroy(args[1]);
}

static int
rebuild_close_container_cb(void *data)
{
	test_arg_t	*arg = data;
	int		rc = 0;
	int		rc_reduce = 0;

	if (daos_handle_is_inval(arg->coh))
		return 0;

	rc = daos_cont_close(arg->coh, NULL);
	if (arg->multi_rank) {
		par_allreduce(PAR_COMM_WORLD, &rc, &rc_reduce, 1, PAR_INT, PAR_MIN);
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
rebuild_destroy_container_cb(void *data)
{
	test_arg_t	*arg = data;
	int		rc = 0;

	if (uuid_is_null(arg->co_uuid))
		return 0;

	rc = rebuild_close_container_cb(data);
	if (rc)
		return rc;

	while (arg->myrank == 0) {
		rc = daos_cont_destroy(arg->pool.poh, arg->co_str, 1, NULL);
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
		par_bcast(PAR_COMM_WORLD, &rc, 1, PAR_INT, 0);
	if (rc)
		print_message("failed to destroy container "DF_UUIDF
			      ": %d\n", DP_UUID(arg->co_uuid), rc);
	uuid_clear(arg->co_uuid);

	return rc;
}

static void
rebuild_destroy_container(void **state)
{
	test_arg_t	*arg = *state;
	test_arg_t	*new_arg = NULL;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;
	int		rc;

	if (!test_runable(arg, 6))
		return;

	/* create/connect another pool */
	rc = rebuild_pool_create(&new_arg, arg, SETUP_CONT_CONNECT, NULL);
	if (rc)
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(new_arg, oids, OBJ_NR);

	new_arg->rebuild_cb = rebuild_destroy_container_cb;

	rebuild_single_pool_rank(new_arg, ranks_to_kill[0], false);

	rebuild_pool_destroy(new_arg);
}

static void
rebuild_close_container(void **state)
{
	test_arg_t	*arg = *state;
	test_arg_t	*new_arg = NULL;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;
	int		rc;

	if (!test_runable(arg, 6))
		return;

	/* create/connect another pool */
	rc = rebuild_pool_create(&new_arg, arg, SETUP_CONT_CONNECT, NULL);
	if (rc)
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(new_arg, oids, OBJ_NR);

	new_arg->rebuild_pre_cb = rebuild_close_container_cb;

	rebuild_single_pool_rank(new_arg, ranks_to_kill[0], false);

	rebuild_pool_destroy(new_arg);
}

static int
rebuild_destroy_pool_cb(void *data)
{
	test_arg_t	*arg = data;
	int		rc = 0;

	rebuild_pool_disconnect_internal(data);

	print_message("sleep 20 seconds to make rebuild ready\n");
	sleep(20);
	if (arg->myrank == 0) {
		/* Disable fail_loc and start rebuild */
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     0, 0, NULL);
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE,
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
	print_message("pool destroyed "DF_UUIDF"\n", DP_UUID(arg->pool.pool_uuid));

	par_barrier(PAR_COMM_WORLD);

out:
	return rc;
}

static void
rebuild_destroy_pool_internal(void **state, uint64_t fail_loc)
{
	test_arg_t	*arg = *state;
	test_arg_t	*new_arg = NULL;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;
	int		rc;

	if (!test_runable(arg, 6))
		return;

	rc = rebuild_pool_create(&new_arg, arg, SETUP_CONT_CONNECT, NULL);
	if (rc)
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(new_arg, oids, OBJ_NR);

	/* hang the rebuild */
	if (arg->myrank == 0) {
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      fail_loc, 0, NULL);
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE, 5,
				      0, NULL);
	}

	new_arg->rebuild_cb = rebuild_destroy_pool_cb;

	rebuild_single_pool_rank(new_arg, ranks_to_kill[0], false);
}

static void
rebuild_destroy_pool_during_scan(void ** state)
{
	return rebuild_destroy_pool_internal(state, DAOS_REBUILD_TGT_SCAN_HANG);
}

static void
rebuild_destroy_pool_during_rebuild(void ** state)
{
	return rebuild_destroy_pool_internal(state,
					     DAOS_REBUILD_TGT_REBUILD_HANG);
}

static void
rebuild_iv_tgt_fail(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* Set no hdl fail_loc on all servers */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_TGT_IV_UPDATE_FAIL |
				     DAOS_FAIL_ONCE, 0, NULL);
	par_barrier(PAR_COMM_WORLD);
	rebuild_single_pool_rank(arg, ranks_to_kill[0], false);
	rebuild_io_verify(arg, oids, OBJ_NR);

	reintegrate_single_pool_rank(arg, ranks_to_kill[0], false);
	rebuild_io_verify(arg, oids, OBJ_NR);
}

static void
rebuild_tgt_start_fail(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	d_rank_t	exclude_rank = 0;
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* failed to start rebuild on rank 0 */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, exclude_rank,
				     DMG_KEY_FAIL_LOC,
				  DAOS_REBUILD_TGT_START_FAIL | DAOS_FAIL_ONCE,
				  0, NULL);
	par_barrier(PAR_COMM_WORLD);

	/* Rebuild rank 1 */
	rebuild_single_pool_rank(arg, ranks_to_kill[0], false);
}

static void
rebuild_send_objects_fail(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* Skip object send on all of the targets */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_REBUILD_TGT_SEND_OBJS_FAIL |
				      DAOS_FAIL_ONCE, 0, NULL);
	par_barrier(PAR_COMM_WORLD);
	/* Even do not sending the objects, the rebuild should still be
	 * able to finish.
	 */
	rebuild_single_pool_rank(arg, ranks_to_kill[0], false);

	/* failed to start rebuild on rank 0 */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	par_barrier(PAR_COMM_WORLD);

	reintegrate_single_pool_rank(arg, ranks_to_kill[0], false);
	rebuild_add_back_tgts(arg, ranks_to_kill[0], NULL, 1);
}


static int
rebuild_pool_disconnect_cb(void *data)
{
	test_arg_t	*arg = data;

	rebuild_pool_disconnect_internal(data);

	/* Disable fail_loc and start rebuild */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     0, 0, NULL);
	par_barrier(PAR_COMM_WORLD);

	return 0;
}

static int
rebuild_add_tgt_pool_connect_internal(void *data)
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
rebuild_tgt_pool_disconnect_internal(void **state, unsigned int fail_loc)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* hang the rebuild during scan */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      fail_loc, 0, NULL);
	par_barrier(PAR_COMM_WORLD);

	/* NB: During the test, one target will be excluded from the pool map,
	 * then container/pool will be closed/disconnectd during the rebuild,
	 * i.e. before the target is added back. so the container hdl cache
	 * will be left on the excluded target after the target is added back.
	 * So the container might not be able to destroyed because of the left
	 * over container hdl. Once the container is able to evict the container
	 * hdl, then this issue can be fixed. XXX
	 */
	arg->rebuild_cb = rebuild_pool_disconnect_cb;
	arg->rebuild_post_cb = rebuild_add_tgt_pool_connect_internal;

	rebuild_single_pool_rank(arg, ranks_to_kill[0], false);

	arg->rebuild_cb = NULL;
	arg->rebuild_post_cb = NULL;

}

static void
rebuild_tgt_pool_disconnect_in_scan(void **state)
{
	rebuild_tgt_pool_disconnect_internal(state,
					     DAOS_REBUILD_TGT_SCAN_HANG);
}

static void
rebuild_tgt_pool_disconnect_in_rebuild(void **state)
{
	rebuild_tgt_pool_disconnect_internal(state,
					     DAOS_REBUILD_TGT_REBUILD_HANG);
}

static int
rebuild_pool_connect_cb(void *data)
{
	test_arg_t	*arg = data;

	rebuild_pool_connect_internal(data);
	/* Disable fail_loc and start rebuild */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     0, 0, NULL);
	par_barrier(PAR_COMM_WORLD);
	return 0;
}

static void
rebuild_offline_pool_connect_internal(void **state, unsigned int fail_loc)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* hang the rebuild */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      fail_loc, 0, NULL);
	par_barrier(PAR_COMM_WORLD);

	arg->rebuild_pre_cb = rebuild_pool_disconnect_internal;
	arg->rebuild_cb = rebuild_pool_connect_cb;

	rebuild_single_pool_rank(arg, ranks_to_kill[0], true);

	arg->rebuild_pre_cb = NULL;
	arg->rebuild_cb = NULL;

	rebuild_io_verify(arg, oids, OBJ_NR);

	reintegrate_single_pool_rank(arg, ranks_to_kill[0], true);
	rebuild_io_verify(arg, oids, OBJ_NR);
}

static void
rebuild_offline_pool_connect_in_scan(void **state)
{
	rebuild_offline_pool_connect_internal(state,
					      DAOS_REBUILD_TGT_SCAN_HANG);
}

static void
rebuild_offline_pool_connect_in_rebuild(void **state)
{
	rebuild_offline_pool_connect_internal(state,
					      DAOS_REBUILD_TGT_REBUILD_HANG);
}

static void
rebuild_offline(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}
	rebuild_io(arg, oids, OBJ_NR);

	arg->rebuild_pre_cb = rebuild_pool_disconnect_internal;
	arg->rebuild_post_cb = rebuild_pool_connect_internal;

	rebuild_single_pool_rank(arg, ranks_to_kill[0], true);

	arg->rebuild_pre_cb = NULL;
	arg->rebuild_post_cb = NULL;

	rebuild_io_verify(arg, oids, OBJ_NR);

	reintegrate_single_pool_rank(arg, ranks_to_kill[0], true);
	rebuild_io_verify(arg, oids, OBJ_NR);
}

static void
rebuild_offline_empty(void **state)
{
	test_arg_t	*arg = *state;
	test_arg_t	*new_arg = NULL;
	int		rc;

	if (!test_runable(arg, 6))
		return;

	rc = rebuild_pool_create(&new_arg, arg, SETUP_POOL_CREATE, NULL);
	if (rc)
		return;

	rebuild_single_pool_rank(new_arg, ranks_to_kill[0], false);
	rebuild_pool_destroy(new_arg);

}

static int
rebuild_change_leader_cb(void *arg)
{
	test_arg_t	*test_arg = arg;
	d_rank_t	leader;

	test_get_leader(test_arg, &leader);

	/* Skip appendentry to re-elect the leader */
	if (test_arg->myrank == 0) {
		daos_debug_set_params(test_arg->group, leader, DMG_KEY_FAIL_LOC,
				     DAOS_RDB_SKIP_APPENDENTRIES_FAIL, 0, NULL);
		print_message("sleep 15 seconds for re-election leader\n");
		/* Sleep 15 seconds to make sure the leader is changed */
		sleep(15);
		/* Continue the rebuild */
		daos_debug_set_params(test_arg->group, -1, DMG_KEY_FAIL_LOC,
				     0, 0, NULL);
	}
	par_barrier(PAR_COMM_WORLD);
	return 0;
}

static void
rebuild_master_change_during_scan(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6) || arg->pool.alive_svc->rl_nr == 1)
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* All ranks should wait before rebuild */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_TGT_SCAN_HANG, 0, NULL);
	par_barrier(PAR_COMM_WORLD);
	arg->rebuild_cb = rebuild_change_leader_cb;

	rebuild_single_pool_rank(arg, ranks_to_kill[0], false);

	/* Verify the data */
	rebuild_io_verify(arg, oids, OBJ_NR);
}

static void
rebuild_master_change_during_rebuild(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6) || arg->pool.alive_svc->rl_nr == 1)
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* All ranks should wait before rebuild */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_TGT_REBUILD_HANG, 0, NULL);
	par_barrier(PAR_COMM_WORLD);
	arg->rebuild_cb = rebuild_change_leader_cb;

	rebuild_single_pool_rank(arg, ranks_to_kill[0], false);

	/* Verify the data */
	rebuild_io_verify(arg, oids, OBJ_NR);
}

static int
rebuild_nospace_cb(void *data)
{
	test_arg_t	*arg = data;

	/* Wait for space is claimed */
	sleep(60);

	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     0, 0, NULL);

	print_message("re-enable recovery\n");

	par_barrier(PAR_COMM_WORLD);

	return 0;
}

static void
rebuild_nospace(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6) || true) /* skip for now */
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_TGT_NOSPACE, 0, NULL);

	par_barrier(PAR_COMM_WORLD);

	arg->rebuild_cb = rebuild_nospace_cb;
	rebuild_single_pool_rank(arg, ranks_to_kill[0], false);

	arg->rebuild_cb = NULL;
	rebuild_io_verify(arg, oids, OBJ_NR);

	reintegrate_single_pool_rank(arg, ranks_to_kill[0], false);
	rebuild_io_verify(arg, oids, OBJ_NR);
}

static void
rebuild_multiple_tgts(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct daos_obj_layout *layout;
	d_rank_t	leader;
	d_rank_t	exclude_ranks[2] = { 0 };
	int		rc;
	int		i;

	if (!test_runable(arg, 6))
		return;

	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0, 0,
				arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);

	rebuild_io(arg, &oid, 1);

	test_get_leader(arg, &leader);
	rc = daos_obj_layout_get(arg->coh, oid, &layout);
	assert_success(rc);
	if (arg->myrank == 0) {
		int fail_cnt = 0;

		/* All ranks should wait before rebuild */
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_REBUILD_HANG, 0, NULL);
		/* kill 2 ranks at the same time */
		D_ASSERT(layout->ol_shards[0]->os_replica_nr > 2);
		for (i = 0; i < 3; i++) {
			d_rank_t rank =
				layout->ol_shards[0]->os_shard_loc[i].sd_rank;

			if (rank != leader) {
				exclude_ranks[fail_cnt] = rank;
				rc = dmg_pool_exclude(arg->dmg_config, arg->pool.pool_uuid,
						      arg->group, rank, -1);
				assert_success(rc);
				if (++fail_cnt >= 2)
					break;
			}
		}

		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	}

	par_barrier(PAR_COMM_WORLD);

	/* Rebuild 2 ranks at the same time */
	if (arg->myrank == 0)
		test_rebuild_wait(&arg, 1);

	/* Verify the data */
	rebuild_io_verify(arg, &oid, 1);

	daos_obj_layout_free(layout);

	/* Add back the target if it is not being killed */
	if (arg->myrank == 0) {
		for (i = 0; i < 2; i++) {
			rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group,
						  exclude_ranks[i], -1);
			assert_success(rc);
		}
	}
	par_barrier(PAR_COMM_WORLD);
}

static int
rebuild_io_cb(void *arg)
{
	test_arg_t	*test_arg = arg;
	daos_obj_id_t	*oids = test_arg->rebuild_cb_arg;

	if (daos_handle_is_valid(test_arg->coh))
		rebuild_io(test_arg, oids, OBJ_NR);

	return 0;
}

static int
rebuild_io_post_cb(void *arg)
{
	test_arg_t	*test_arg = arg;
	daos_obj_id_t	*oids = test_arg->rebuild_post_cb_arg;

	if (daos_handle_is_valid(test_arg->coh))
		rebuild_io_verify(test_arg, oids, OBJ_NR);

	return 0;
}

static void
rebuild_master_failure(void **state)
{
	test_arg_t	       *arg = *state;
	daos_obj_id_t		oids[10 * OBJ_NR];
	daos_pool_info_t	pinfo = {0};
	daos_pool_info_t	pinfo_new = {0};
	d_rank_list_t	       *affected_engines = NULL;
	int			i;
	int			rc;

	/* need 5 svc replicas, as will kill the leader 2 times */
	if (!test_runable(arg, 6) || arg->pool.alive_svc->rl_nr < 5) {
		print_message("testing skipped ...\n");
		return;
	}

	test_get_leader(arg, &ranks_to_kill[0]);
	for (i = 0; i < 10 * OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	/* prepare the data */
	rebuild_io(arg, oids, 10 * OBJ_NR);

	rebuild_single_pool_rank(arg, ranks_to_kill[0], true);

	/* Verify the data */
	rebuild_io_verify(arg, oids, 10 * OBJ_NR);

	/* Verify the POOL_QUERY get same rebuild status after leader change */
	pinfo.pi_bits = DPI_REBUILD_STATUS;
	rc = test_pool_get_info(arg, &pinfo, &affected_engines);
	assert_rc_equal(rc, 0);
	assert_int_equal(pinfo.pi_rebuild_st.rs_state, 2);
	assert_true(pinfo.pi_ndisabled > 0);
	assert_true(d_rank_list_find(affected_engines, ranks_to_kill[0], NULL));
	rc = rebuild_change_leader_cb(arg);
	assert_int_equal(rc, 0);
	pinfo_new.pi_bits = DPI_REBUILD_STATUS;
	rc = test_pool_get_info(arg, &pinfo_new, NULL /* engine_ranks */);
	assert_rc_equal(rc, 0);
	assert_int_equal(pinfo_new.pi_rebuild_st.rs_state, 2);
	rc = memcmp(&pinfo.pi_rebuild_st, &pinfo_new.pi_rebuild_st,
		    sizeof(pinfo.pi_rebuild_st));
	if (rc != 0) {
		print_message("old ver %u seconds %u err %d state %d fail %d"
			      " tobeobj "DF_U64" obj "DF_U64" rec "DF_U64
			      " sz "DF_U64"\n",
			      pinfo.pi_rebuild_st.rs_version,
			      pinfo.pi_rebuild_st.rs_seconds,
			      pinfo.pi_rebuild_st.rs_errno,
			      pinfo.pi_rebuild_st.rs_state,
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
			      pinfo_new.pi_rebuild_st.rs_state,
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

	reintegrate_single_pool_rank(arg, ranks_to_kill[0], true);
	rebuild_io_verify(arg, oids, 10 * OBJ_NR);
}

static void
rebuild_multiple_failures(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	daos_obj_id_t	cb_arg_oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
		cb_arg_oids[i] = daos_test_oid_gen(arg->coh, OBJ_CLS, 0, 0,
						   arg->myrank);
	}

	/* prepare the data */
	rebuild_io(arg, oids, OBJ_NR);

	/* Remove this in-flight IO temporarily XXX */
	arg->rebuild_cb = rebuild_io_cb;
	arg->rebuild_cb_arg = cb_arg_oids;
	/* Disable data validation because of DAOS-2915. */
	arg->rebuild_post_cb = rebuild_io_post_cb;

	arg->rebuild_post_cb_arg = cb_arg_oids;

	rebuild_pools_ranks(&arg, 1, ranks_to_kill, 2, true);

	arg->rebuild_cb = NULL;
	arg->rebuild_post_cb = NULL;

	reintegrate_pools_ranks(&arg, 1, ranks_to_kill, 2, true);
}

static void
rebuild_fail_all_replicas_before_rebuild(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct daos_obj_layout *layout;
	struct daos_obj_shard *shard;
	int		rc;

	if (!test_runable(arg, 6) || arg->pool.alive_svc->rl_nr < 3)
		return;

	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R2S_SPEC_RANK, 0, 0,
				arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);

	rebuild_io(arg, &oid, 1);

	rc = daos_obj_layout_get(arg->coh, oid, &layout);
	assert_success(rc);

	/* HOLD rebuild ULT */
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			     DAOS_REBUILD_HANG, 0, NULL);

	/* Kill one replica and start rebuild */
	shard = layout->ol_shards[0];
	daos_kill_server(arg, arg->pool.pool_uuid, arg->group,
			 arg->pool.alive_svc, shard->os_shard_loc[0].sd_rank);

	/* Sleep 10 seconds after it scan finish and hang before rebuild */
	print_message("sleep 10 seconds to wait scan to be finished \n");
	sleep(10);

	/* Then kill rank on shard1 */
	/* NB: we can not kill rank 0, otherwise the following set_params
	 * will fail and also pool destroy will not work.
	 */
	if (shard->os_shard_loc[1].sd_rank != 0)
		daos_kill_server(arg, arg->pool.pool_uuid, arg->group,
				 arg->pool.alive_svc,
				 shard->os_shard_loc[1].sd_rank);

	/* Continue rebuild */
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);

	/* Sleep long enough to make sure the 2nd rebuild caused by 2nd kill is
	 * triggered.
	 */
	sleep(15);
	if (arg->myrank == 0)
		test_rebuild_wait(&arg, 1);

	par_barrier(PAR_COMM_WORLD);
	daos_obj_layout_free(layout);
}

static void
rebuild_fail_all_replicas(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct daos_obj_layout *layout;
	int		i;
	int		rc;

	/* This test will kill 3 replicas, which might include the ranks
	 * in svcs, so make sure there are at least 6 ranks in svc, so
	 * the new leader can be chosen.
	 */
	if (!test_runable(arg, 6) || arg->pool.alive_svc->rl_nr < 6) {
		print_message("need at least 6 svcs, -s6\n");
		return;
	}

	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0, 0,
				arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);

	rebuild_io(arg, &oid, 1);

	rc = daos_obj_layout_get(arg->coh, oid, &layout);
	assert_success(rc);
	for (i = 0; i < layout->ol_nr; i++) {
		int j;

		for (j = 0; j < layout->ol_shards[i]->os_replica_nr; j++) {
			d_rank_t rank =
				layout->ol_shards[i]->os_shard_loc[j].sd_rank;

			/* Skip rank 0 temporarily due to DAOS-6564 XXX */
			if (rank == 0)
				continue;
			daos_kill_server(arg, arg->pool.pool_uuid,
					 arg->group, arg->pool.alive_svc,
					 rank);
		}
	}

	sleep(15);
	if (arg->myrank == 0)
		test_rebuild_wait(&arg, 1);

	par_barrier(PAR_COMM_WORLD);
	daos_obj_layout_free(layout);
}

static void
multi_pools_rebuild_concurrently(void **state)
{
#define POOL_NUM		4
#define CONT_PER_POOL		2
#define OBJ_PER_CONT		32
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
		rc = rebuild_pool_create(&args[i], arg, SETUP_CONT_CONNECT,
					 pool);
		if (rc)
			goto out;

		if (i % CONT_PER_POOL == 0)
			assert_int_equal(args[i]->pool.slave, 0);
		else
			assert_int_equal(args[i]->pool.slave, 1);
	}

	for (i = 0; i < OBJ_PER_CONT; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	for (i = 0; i < POOL_NUM * CONT_PER_POOL; i++)
		rebuild_io(args[i], oids, OBJ_PER_CONT);

	rebuild_pools_ranks(args, POOL_NUM * CONT_PER_POOL, ranks_to_kill, 1,
			false);

	for (i = POOL_NUM * CONT_PER_POOL - 1; i >= 0; i--)
		rebuild_io_verify(args[i], oids, OBJ_PER_CONT);

out:
	for (i = POOL_NUM * CONT_PER_POOL - 1; i >= 0; i--)
		rebuild_pool_destroy(args[i]);
}

static int
rebuild_kill_cb(void *data)
{
	test_arg_t	*arg = data;

	print_message("sleep 10 seconds for rebuild to start\n");
	sleep(10);
	daos_kill_server(arg, arg->pool.pool_uuid, arg->group,
			 arg->pool.alive_svc, ranks_to_kill[0]);

	if (arg->myrank == 0) {
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      0, 0, NULL);
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE, 0,
				      0, NULL);
	}

	return 0;
}
static void
rebuild_kill_rank_during_rebuild(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* hang the rebuild */
	if (arg->myrank == 0) {
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_REBUILD_TGT_SCAN_HANG, 0, NULL);
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE, 5,
				      0, NULL);
	}

	arg->rebuild_cb = rebuild_kill_cb;

	rebuild_single_pool_rank(arg, ranks_to_kill[0], false);

	sleep(5);
	arg->rebuild_cb = NULL;
	reintegrate_single_pool_rank(arg, ranks_to_kill[0], true);
}

static void
rebuild_kill_PS_leader_during_rebuild(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	d_rank_t	leader;
	int		i;

	if (!test_runable(arg, 7) || arg->pool.alive_svc->rl_nr < 5) {
		print_message("need at least 5 svcs, -s5\n");
		return;
	}

	test_get_leader(arg, &leader);
	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], 6);
	}
	rebuild_io(arg, oids, OBJ_NR);

	daos_kill_server(arg, arg->pool.pool_uuid, arg->group,
			 arg->pool.alive_svc, 6);
	/* hang the rebuild */
	if (arg->myrank == 0) {
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_REBUILD_TGT_SCAN_HANG, 0, NULL);
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE, 5,
				      0, NULL);
	}
	sleep(2);
	rebuild_single_pool_rank(arg, leader, true);

	sleep(5);
	reintegrate_single_pool_rank(arg, leader, true);
}

static void
rebuild_pool_destroy_during_rebuild_failure(void **state)
{
	return rebuild_destroy_pool_internal(state, DAOS_REBUILD_UPDATE_FAIL |
						    DAOS_FAIL_ALWAYS);
}

static int
reintegrate_failure_cb(void *arg)
{
	test_arg_t	*test_arg = arg;

	daos_debug_set_params(test_arg->group, -1, DMG_KEY_FAIL_LOC,
			      DAOS_REBUILD_OBJ_FAIL | DAOS_FAIL_ALWAYS, 0, NULL);

	return 0;
}

static void
reintegrate_failure_and_retry(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[20];
	int		i;

	if (!test_runable(arg, 4))
		return;

	for (i = 0; i < 20; i++)
		oids[i] = daos_test_oid_gen(arg->coh, OC_RP_3GX, 0,
					    0, arg->myrank);

	rebuild_io(arg, oids, OBJ_NR);
	rebuild_single_pool_rank(arg, ranks_to_kill[0], true);

	arg->rebuild_cb = reintegrate_failure_cb;
	reintegrate_single_pool_rank(arg, ranks_to_kill[0], true);

	arg->rebuild_cb = NULL;
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
	rebuild_io_validate(arg, oids, OBJ_NR);

	reintegrate_single_pool_rank(arg, ranks_to_kill[0], false);

	rebuild_io_validate(arg, oids, OBJ_NR);
}

static void
rebuild_kill_more_RF_ranks(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	struct ioreq	req;
	d_rank_t	ranks[4] = {7, 6, 5, 4};
	int		i;

	if (!test_runable(arg, 7) || arg->pool.alive_svc->rl_nr < 5) {
		print_message("need at least 5 svcs, -s5\n");
		return;
	}

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, OC_RP_3GX, 0, 0, arg->myrank);
		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);
		insert_single("dkey", "akey", 0, "data", strlen("data") + 1,
			      DAOS_TX_NONE, &req);
		ioreq_fini(&req);
	}
	rebuild_pools_ranks(&arg, 1, ranks, 4, true);

	reintegrate_single_pool_rank(arg, 5, true);
	reintegrate_single_pool_rank(arg, 6, true);
	reintegrate_single_pool_rank(arg, 4, true);
	reintegrate_single_pool_rank(arg, 7, true);

	print_message("lookup and expect -DER_RF\n");
	for (i = 0; i < OBJ_NR; i++) {
		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);
		arg->expect_result = -DER_RF;
		D_DEBUG(DB_TRACE, "lookup single %d\n", i);
		lookup_single("dkey", "akey", 0, "data", strlen("data") + 1,
			      DAOS_TX_NONE, &req);
		ioreq_fini(&req);
	}

	D_DEBUG(DB_TRACE, "cont status clear\n");
	/* clear health status */
	daos_cont_status_clear(arg->coh, NULL);

	print_message("clear health status then retry.\n");
	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, OC_RP_3GX, 0, 0, arg->myrank);
		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);
		lookup_single("dkey", "akey", 0, "data", strlen("data") + 1,
			      DAOS_TX_NONE, &req);
		ioreq_fini(&req);
	}
}

/** create a new pool/container for each test */
static const struct CMUnitTest rebuild_tests[] = {
	{"REBUILD0: drop rebuild scan reply",
	rebuild_drop_scan, rebuild_small_sub_setup, rebuild_sub_teardown},
	{"REBUILD1: retry rebuild for not ready",
	rebuild_retry_rebuild, rebuild_small_sub_setup, rebuild_sub_teardown},
	{"REBUILD2: drop rebuild obj reply",
	rebuild_drop_obj, rebuild_small_sub_setup, rebuild_sub_teardown},
	{"REBUILD3: rebuild multiple pools",
	rebuild_multiple_pools, rebuild_sub_setup, rebuild_sub_teardown},
	{"REBUILD4: rebuild update failed",
	rebuild_update_failed, rebuild_small_sub_setup, rebuild_sub_teardown},
	{"REBUILD5: retry rebuild for pool stale",
	rebuild_retry_for_stale_pool, rebuild_small_sub_setup,
	rebuild_sub_teardown},
	{"REBUILD6: rebuild with container destroy",
	rebuild_destroy_container, rebuild_sub_setup, rebuild_sub_teardown},
	{"REBUILD7: rebuild with container close",
	rebuild_close_container, rebuild_small_sub_setup, rebuild_sub_teardown},
	{"REBUILD8: rebuild with pool destroy during scan",
	rebuild_destroy_pool_during_scan, rebuild_sub_setup,
	rebuild_sub_teardown},
	{"REBUILD9: rebuild with pool destroy during rebuild",
	rebuild_destroy_pool_during_rebuild, rebuild_sub_setup,
	rebuild_sub_teardown},
	{"REBUILD10: rebuild iv tgt fail",
	rebuild_iv_tgt_fail, rebuild_small_sub_setup, rebuild_sub_teardown},
	{"REBUILD11: rebuild tgt start fail",
	rebuild_tgt_start_fail, rebuild_small_sub_setup, rebuild_sub_teardown},
	{"REBUILD12: rebuild send objects failed",
	 rebuild_send_objects_fail, rebuild_small_sub_setup,
	rebuild_sub_teardown},
	{"REBUILD13: rebuild empty pool offline",
	rebuild_offline_empty, rebuild_small_sub_setup, rebuild_sub_teardown},
	{"REBUILD14: rebuild no space failure",
	rebuild_nospace, rebuild_small_sub_setup, rebuild_sub_teardown},
	{"REBUILD15: rebuild multiple tgts",
	rebuild_multiple_tgts, rebuild_small_sub_setup, rebuild_sub_teardown},
	{"REBUILD16: disconnect pool during scan",
	 rebuild_tgt_pool_disconnect_in_scan, rebuild_small_sub_setup,
	 rebuild_sub_teardown},
	{"REBUILD17: disconnect pool during rebuild",
	 rebuild_tgt_pool_disconnect_in_rebuild, rebuild_small_sub_setup,
	rebuild_sub_teardown},
	{"REBUILD18: multi-pools rebuild concurrently",
	 multi_pools_rebuild_concurrently, rebuild_sub_setup,
	rebuild_sub_teardown},
	{"REBUILD19: rebuild with master change during scan",
	rebuild_master_change_during_scan, rebuild_sub_setup,
	rebuild_sub_teardown},
	{"REBUILD20: rebuild with master change during rebuild",
	rebuild_master_change_during_rebuild, rebuild_sub_setup,
	rebuild_sub_teardown},
	{"REBUILD21: rebuild with master failure",
	 rebuild_master_failure, rebuild_sub_setup, rebuild_sub_teardown},
	{"REBUILD22: connect pool during scan for offline rebuild",
	 rebuild_offline_pool_connect_in_scan, rebuild_sub_setup,
	 rebuild_sub_teardown},
	{"REBUILD23: connect pool during rebuild for offline rebuild",
	 rebuild_offline_pool_connect_in_rebuild, rebuild_sub_setup,
	 rebuild_sub_teardown},
	{"REBUILD24: offline rebuild",
	rebuild_offline, rebuild_sub_setup, rebuild_sub_teardown},
	{"REBUILD25: rebuild with two failures",
	 rebuild_multiple_failures, rebuild_sub_setup, rebuild_sub_teardown},
	{"REBUILD26: rebuild fail all replicas before rebuild",
	 rebuild_fail_all_replicas_before_rebuild, rebuild_sub_rf1_setup,
	 rebuild_sub_teardown},
	{"REBUILD27: rebuild fail all replicas",
	 rebuild_fail_all_replicas, rebuild_sub_setup, rebuild_sub_teardown},
	{"REBUILD28: rebuild kill rank during rebuild",
	 rebuild_kill_rank_during_rebuild, rebuild_sub_setup,
	 rebuild_sub_teardown},
	{"REBUILD29: rebuild kill PS leader during rebuild",
	 rebuild_kill_PS_leader_during_rebuild, rebuild_sub_setup,
	 rebuild_sub_teardown},
	{"REBUILD30: destroy pool during rebuild failure and retry",
	  rebuild_pool_destroy_during_rebuild_failure, rebuild_sub_setup,
	 rebuild_sub_teardown},
	{"REBUILD31: reintegrate failure and retry",
	  reintegrate_failure_and_retry, rebuild_sub_setup,
	 rebuild_sub_teardown},
	{"REBUILD32: kill more ranks than RF, then reintegrate",
	  rebuild_kill_more_RF_ranks, rebuild_sub_setup,
	 rebuild_sub_teardown},
};

/* TODO: Enable aggregation once stable view rebuild is done. */
int
rebuild_test_setup(void **state)
{
	test_arg_t	*arg;
	int rc;

	rc = test_setup(state, SETUP_CONT_CONNECT, true, REBUILD_POOL_SIZE,
			0, NULL);
	if (rc)
		return rc;

	arg = *state;
	if (arg && arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     1, 0, NULL);
	par_barrier(PAR_COMM_WORLD);
	return 0;
}

int
rebuild_test_teardown(void **state)
{
	test_arg_t	*arg = *state;

	if (arg && arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     0, 0, NULL);
	par_barrier(PAR_COMM_WORLD);

	test_teardown(state);
	return 0;
}

int
run_daos_rebuild_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	par_barrier(PAR_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(rebuild_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests_only("DAOS_Rebuild", rebuild_tests,
				     ARRAY_SIZE(rebuild_tests), sub_tests,
				     sub_tests_size);

	par_barrier(PAR_COMM_WORLD);

	return rc;
}
