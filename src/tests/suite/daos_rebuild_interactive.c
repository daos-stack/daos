/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is for interactive rebuild stop|start testing based on pool exclude, drain, extend,
 * and reintegrate.
 *
 * tests/suite/daos_rebuild_interactive.c
 *
 */
#define D_LOGFAC DD_FAC(tests)

#include "daos_test.h"
#include "daos_iotest.h"
#include "dfs_test.h"
#include <daos/tests_lib.h>
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

#define DEFAULT_FAIL_TGT 0
#define DRAIN_KEY_NR     50
#define KEY_NR           10
#define OBJ_NR           10
#define DATA_SIZE        (1048576 * 2 + 512)

static void
reintegrate_with_inflight_io(test_arg_t *arg, daos_obj_id_t *oid, d_rank_t rank, int tgt)
{
	daos_obj_id_t inflight_oid;

	if (oid != NULL) {
		inflight_oid = *oid;
	} else {
		inflight_oid =
		    daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0, 0, arg->myrank);
		inflight_oid = dts_oid_set_rank(inflight_oid, rank);
	}

	arg->rebuild_cb     = reintegrate_inflight_io;
	arg->rebuild_cb_arg = &inflight_oid;

	/* To make sure the IO will be done before reintegration is done */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_REBUILD_TGT_REBUILD_HANG, 0, NULL);
	reintegrate_single_pool_target(arg, rank, tgt);
	arg->rebuild_cb     = NULL;
	arg->rebuild_cb_arg = NULL;

	if (oid == NULL) {
		int rc;

		rc = daos_obj_verify(arg->coh, inflight_oid, DAOS_EPOCH_MAX);
		assert_rc_equal(rc, 0);
	}
}

#define SNAP_CNT 5
static void
int_rebuild_snap_update_recs(void **state)
{
	test_arg_t   *arg = *state;
	daos_obj_id_t oid;
	struct ioreq  req;
	daos_recx_t   recx;
	int           tgt                    = DEFAULT_FAIL_TGT;
	char          string[100 * SNAP_CNT] = {0};
	daos_epoch_t  snap_epoch[SNAP_CNT];
	int           i;
	int           rc;

	if (!test_runable(arg, 4))
		return;

	T_BEGIN();
	oid = daos_test_oid_gen(arg->coh, arg->obj_class, 0, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < SNAP_CNT; i++)
		sprintf(string + strlen(string), "old-snap%d", i);

	recx.rx_idx = 0;
	recx.rx_nr  = strlen(string);
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1, string, strlen(string) + 1, &req);

	for (i = 0; i < SNAP_CNT; i++) {
		char data[100] = {0};

		/* Update string for each snapshot */
		daos_cont_create_snap(arg->coh, &snap_epoch[i], NULL, NULL);
		sprintf(data, "new-snap%d", i);
		recx.rx_idx = i * strlen(data);
		recx.rx_nr  = strlen(data);
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1, data, strlen(data) + 1,
			     &req);
	}
	ioreq_fini(&req);

	/* insert rebuild stop|start into the exclude rebuild execution */
	arg->rebuild_cb          = rebuild_stop_with_dmg;
	arg->rebuild_post_cb     = rebuild_resume_wait;
	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);
	arg->rebuild_cb      = NULL;
	arg->rebuild_post_cb = NULL;

	for (i = 0; i < SNAP_CNT; i++) {
		rc = daos_obj_verify(arg->coh, oid, snap_epoch[i]);
		assert_rc_equal(rc, 0);
	}
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	assert_rc_equal(rc, 0);

	arg->interactive_rebuild = 0;
	reintegrate_with_inflight_io(arg, &oid, ranks_to_kill[0], tgt);
	for (i = 0; i < SNAP_CNT; i++) {
		rc = daos_obj_verify(arg->coh, oid, snap_epoch[i]);
		assert_rc_equal(rc, 0);
	}
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	assert_rc_equal(rc, 0);
	T_END();
}

static void
int_rebuild_snap_punch_recs(void **state)
{
	test_arg_t   *arg = *state;
	daos_obj_id_t oid;
	struct ioreq  req;
	daos_recx_t   recx;
	int           tgt = DEFAULT_FAIL_TGT;
	char          string[200];
	daos_epoch_t  snap_epoch[SNAP_CNT];
	int           i;
	int           rc;

	if (!test_runable(arg, 4))
		return;

	T_BEGIN();
	oid = daos_test_oid_gen(arg->coh, arg->obj_class, 0, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < SNAP_CNT; i++)
		sprintf(string + strlen(string), "old-snap%d", i);

	recx.rx_idx = 0;
	recx.rx_nr  = strlen(string);
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1, string, strlen(string) + 1, &req);

	for (i = 0; i < SNAP_CNT; i++) {
		/* punch string */
		daos_cont_create_snap(arg->coh, &snap_epoch[i], NULL, NULL);
		recx.rx_idx = i * 9; /* strlen("old-snap%d") */
		recx.rx_nr  = 9;
		punch_recxs("d_key", "a_key", &recx, 1, DAOS_TX_NONE, &req);
	}
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	for (i = 0; i < SNAP_CNT; i++) {
		rc = daos_obj_verify(arg->coh, oid, snap_epoch[i]);
		assert_rc_equal(rc, 0);
	}
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	assert_rc_equal(rc, 0);

	/* insert rebuild stop|start into the reintegrate rebuild execution */
	arg->interactive_rebuild = 1;
	reintegrate_with_inflight_io(arg, &oid, ranks_to_kill[0], tgt);
	for (i = 0; i < SNAP_CNT; i++) {
		rc = daos_obj_verify(arg->coh, oid, snap_epoch[i]);
		assert_rc_equal(rc, 0);
	}
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	assert_rc_equal(rc, 0);
	T_END();
}

static int
rebuild_wait_error_reset_fail_cb(void *data)
{
	test_arg_t *arg = data;
	int         rc;

	print_message("wait until rebuild starts erroring\n");
	test_rebuild_wait_to_error(&arg, 1);
	print_message("rebuild version %u erroring, check rs_errno=%d (expecting -DER_IO=%d)\n",
		      arg->pool.pool_info.pi_rebuild_st.rs_version,
		      arg->pool.pool_info.pi_rebuild_st.rs_errno, -DER_IO);
	assert_int_equal(arg->pool.pool_info.pi_rebuild_st.rs_errno, -DER_IO);

	print_message("clearing fault injection on all engines\n");
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE, 0, 0, NULL);
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_NUM, 0, 0, NULL);

	print_message("wait until Fail_reclaim starts\n");
	test_rebuild_wait_to_start_lower(&arg, 1);

	print_message(
	    "send rebuild stop --force request during first/only Fail_reclaim operation\n");
	rc = rebuild_force_stop_with_dmg(data);
	assert_rc_equal(rc, 0);

	/* Wait for stop, verify rs_state/rs_errno happens in rebuild_post_cb rebuild_resume_wait()
	 */

	return rc;
}

static void
int_rebuild_many_objects_with_failure(void **state)
{
	test_arg_t    *arg = *state;
	daos_obj_id_t *oids;
	const int      NUM_OBJS = 500;
	int            rc;
	int            i;

	FAULT_INJECTION_REQUIRED();

	if (!test_runable(arg, 6))
		return;

	T_BEGIN();
	D_ALLOC_ARRAY(oids, NUM_OBJS);
	for (i = 0; i < NUM_OBJS; i++) {
		char         buffer[256];
		daos_recx_t  recx;
		struct ioreq req;

		oids[i] = daos_test_oid_gen(arg->coh, OC_RP_3G1, 0, 0, arg->myrank);
		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);
		memset(buffer, 'a', 256);
		recx.rx_idx = 0;
		recx.rx_nr  = 256;
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1, buffer, 256, &req);

		ioreq_fini(&req);
	}

	/* Inject faults on engines. Special handling for interactive_rebuild case */
	if (arg->myrank == 0) {
		print_message("inject fault DAOS_REBUILD_OBJ_FAIL on all engines\n");
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_REBUILD_OBJ_FAIL | DAOS_FAIL_ALWAYS, 0, NULL);
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE, 3, 0, NULL);
	}

	/* For interactive rebuild, we need:
	 * 1. trigger rebuild (which will fail), wait until op:Fail_reclaim begins.
	 * 2. During op:Fail_reclaim, issue dmg system stop (test that stop does not interrupt
	 *    reclaim, but takes effect by deferring the rebuild retry rather than running it -
	 *    the retry is re-queued with delay=-1 and only runs if a later same-pool rebuild
	 *    merges it).
	 */
	arg->rebuild_cb      = rebuild_wait_error_reset_fail_cb;
	arg->rebuild_post_cb = rebuild_resume_wait;
	rebuild_single_pool_target(arg, 3, -1, false);

	for (i = 0; i < NUM_OBJS; i++) {
		rc = daos_obj_verify(arg->coh, oids[i], DAOS_EPOCH_MAX);
		assert_rc_equal(rc, 0);
	}
	D_FREE(oids);
	T_END();
}

static int
cont_open_and_inflight_io(void *data)
{
	test_arg_t *arg = data;
	int         rc;

	assert_int_equal(arg->setup_state, SETUP_CONT_CREATE);
	rc = test_setup_next_step((void **)&arg, NULL, NULL, NULL);
	assert_success(rc);
	assert_int_equal(arg->setup_state, SETUP_CONT_CONNECT);

	return reintegrate_inflight_io(data);
}

static void
int_cont_open_in_drain(void **state)
{
	test_arg_t   *arg = *state;
	daos_obj_id_t oid;
	struct ioreq  req;
	int           tgt = DEFAULT_FAIL_TGT;
	int           i;

	FAULT_INJECTION_REQUIRED();

	if (!test_runable(arg, 4))
		return;

	T_BEGIN();
	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R1S_SPEC_RANK, 0, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert records */
	print_message("Insert %d kv record in object " DF_OID "\n", DRAIN_KEY_NR, DP_OID(oid));
	for (i = 0; i < DRAIN_KEY_NR; i++) {
		char key[32] = {0};

		sprintf(key, "dkey_0_%d", i);
		insert_single(key, "a_key", 0, "data", strlen("data") + 1, DAOS_TX_NONE, &req);
	}
	ioreq_fini(&req);

	test_teardown_cont_hdl(arg);
	arg->interactive_rebuild = 1;
	arg->rebuild_cb          = cont_open_and_inflight_io;
	arg->rebuild_cb_arg      = &oid;
	drain_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < DRAIN_KEY_NR; i++) {
		char key[32] = {0};
		char buf[16] = {0};

		sprintf(key, "dkey_0_%d", i);
		/** Lookup */
		memset(buf, 0, 10);
		lookup_single(key, "a_key", 0, buf, 10, DAOS_TX_NONE, &req);
		assert_int_equal(req.iod[0].iod_size, strlen("data") + 1);

		/** Verify data consistency */
		assert_string_equal(buf, "data");
	}

	reintegrate_inflight_io_verify(arg);
	ioreq_fini(&req);
	T_END();
}

static void
int_drain_fail_and_retry_objects(void **state)
{
	test_arg_t   *arg = *state;
	daos_obj_id_t oids[OBJ_NR];
	int           i;

	FAULT_INJECTION_REQUIRED();

	if (!test_runable(arg, 4))
		return;

	T_BEGIN();
	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R1S_SPEC_RANK, 0, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
		oids[i] = dts_oid_set_tgt(oids[i], DEFAULT_FAIL_TGT);
	}

	rebuild_io(arg, oids, OBJ_NR);
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			      DAOS_REBUILD_OBJ_FAIL | DAOS_FAIL_ALWAYS, 0, NULL);

	arg->no_rebuild = 1;
	drain_single_pool_rank(arg, ranks_to_kill[0], false);
	arg->no_rebuild = 0;
	print_message("wait drain to fail and exit\n");
	/* NB: could be better to wait (in drain_single_pool_rank or test_rebuild_wait), but that
	 *  requires new logic in rebuild_task_complete_schedule() to update state after
	 * Fail_reclaim
	 */
	print_message("wait for drain reubild to get -DER_IO\n");
	test_rebuild_wait_to_error(&arg, 1);
	print_message("wait for op:Fail_reclaim to start\n");
	test_rebuild_wait_to_start_lower(&arg, 1);

	print_message("clear fault injection on all engines and wait for retry rebuild\n");
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
	test_rebuild_wait_to_start_next(&arg, 1);
	print_message("drain rebuild retry started, version=%u\n",
		      arg->pool.pool_info.pi_rebuild_st.rs_version);
	rebuild_io_validate(arg, oids, OBJ_NR);

	arg->interactive_rebuild = 1;
	arg->rebuild_cb          = reintegrate_inflight_io;
	arg->rebuild_cb_arg      = &oids[OBJ_NR - 1];
	print_message("inflight IO during drain (that will be stopped/restarted)\n");
	drain_single_pool_rank(arg, ranks_to_kill[0], false);
	print_message("final data verification\n");
	rebuild_io_validate(arg, oids, OBJ_NR);
	reintegrate_inflight_io_verify(arg);
	T_END();
}

/* FIXME: rename a few things - most of this code is performing drain + kill/exclude, NOT extend */

static int
int_extend_drain_cb_internal(void *arg)
{
	test_arg_t                 *test_arg = arg;
	struct extend_drain_cb_arg *cb_arg   = test_arg->rebuild_cb_arg;
	dfs_t                      *dfs_mt   = cb_arg->dfs_mt;
	daos_obj_id_t              *oids     = cb_arg->oids;
	dfs_obj_t                  *dir      = cb_arg->dir;
	uint32_t                    objclass = cb_arg->objclass;
	struct dirent               ents[10];
	int                         opc           = cb_arg->opc;
	int                         total_entries = 0;
	uint32_t                    num_ents      = 10;
	daos_anchor_t               anchor        = {0};
	int                         rc;
	int                         i;

	if (opc != EXTEND_DRAIN_WRITELOOP) {
		print_message("sleep 5 seconds first\n");
		sleep(5);
	}

	print_message("%sstart op %d (%s)\n",
		      test_arg->interactive_rebuild ? "stop rebuild before " : "", opc,
		      extend_drain_opstrs[opc]);

	if (test_arg->interactive_rebuild) {
		rc = rebuild_stop_with_dmg(arg);
		assert_rc_equal(rc, 0);
	}

	/* Kill another rank during extend */
	switch (opc) {
	case EXTEND_DRAIN_PUNCH:
		print_message("punch objects during extend & drain%s\n",
			      test_arg->interactive_rebuild ? " during stopped rebuild" : "");
		for (i = 0; i < EXTEND_DRAIN_OBJ_NR; i++) {
			char filename[32];

			sprintf(filename, "file%d", i);
			rc = dfs_remove(dfs_mt, dir, filename, true, &oids[i]);
			assert_int_equal(rc, 0);
		}
		break;
	case EXTEND_DRAIN_STAT:
		print_message("stat objects during extend & drain%s\n",
			      test_arg->interactive_rebuild ? " during stopped rebuild" : "");
		for (i = 0; i < EXTEND_DRAIN_OBJ_NR; i++) {
			char        filename[32];
			struct stat stbuf;

			sprintf(filename, "file%d", i);
			rc = dfs_stat(dfs_mt, dir, filename, &stbuf);
			assert_int_equal(rc, 0);
		}
		break;
	case EXTEND_DRAIN_ENUMERATE:
		print_message("enumerate objects during extend & drain%s]n",
			      test_arg->interactive_rebuild ? " during stopped rebuild" : "");
		while (!daos_anchor_is_eof(&anchor)) {
			num_ents = 10;
			rc       = dfs_readdir(dfs_mt, dir, &anchor, &num_ents, ents);
			assert_int_equal(rc, 0);
			total_entries += num_ents;
		}
		assert_int_equal(total_entries, EXTEND_DRAIN_OBJ_NR);
		break;
	case EXTEND_DRAIN_FETCH:
		print_message("fetch objects during extend & drain%s\n",
			      test_arg->interactive_rebuild ? " during stopped rebuild" : "");
		extend_drain_read_check(dfs_mt, dir, objclass, EXTEND_DRAIN_OBJ_NR, WRITE_SIZE,
					'a');
		break;
	case EXTEND_DRAIN_UPDATE:
		print_message("update objects during extend & drain%s\n",
			      test_arg->interactive_rebuild ? " during stopped rebuild" : "");
		extend_drain_write(dfs_mt, dir, objclass, EXTEND_DRAIN_OBJ_NR, WRITE_SIZE, 'a',
				   NULL);
		break;
	case EXTEND_DRAIN_OVERWRITE:
		print_message("overwrite objects during extend & drain%s\n",
			      test_arg->interactive_rebuild ? " during stopped rebuild" : "");
		extend_drain_write(dfs_mt, dir, objclass, EXTEND_DRAIN_OBJ_NR, WRITE_SIZE, 'b',
				   NULL);
		break;
	case EXTEND_DRAIN_WRITELOOP:
		print_message("keepwrite objects during extend & drain%s\n",
			      test_arg->interactive_rebuild ? " during stopped rebuild" : "");
		extend_drain_write(dfs_mt, dir, objclass, 1, 512 * 1048576, 'a', NULL);
		break;
	default:
		break;
	}

	daos_debug_set_params(test_arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);

	print_message("%sdone op %d (%s)\n",
		      test_arg->interactive_rebuild ? "resume rebuild after " : "", opc,
		      extend_drain_opstrs[opc]);

	if (test_arg->interactive_rebuild)
		rebuild_resume_wait_to_start(arg);

	return 0;
}

static void
int_dfs_drain_overwrite(void **state)
{
	test_arg_t *arg = *state;

	arg->interactive_rebuild = 1;
	print_message("=== Begin EXTEND_DRAIN_OVERWRITE, oclass OC_EC_4P2GX\n");
	dfs_extend_drain_common(state, EXTEND_DRAIN_OVERWRITE, OC_EC_4P2GX,
				int_extend_drain_cb_internal);
	T_END();
}

static int
int_extend_cb_internal(void *arg)
{
	test_arg_t           *test_arg = arg;
	struct extend_cb_arg *cb_arg   = test_arg->rebuild_cb_arg;
	dfs_t                *dfs_mt   = cb_arg->dfs_mt;
	daos_obj_id_t        *oids     = cb_arg->oids;
	dfs_obj_t            *dir      = cb_arg->dir;
	struct dirent         ents[10];
	int                   opc           = cb_arg->opc;
	int                   total_entries = 0;
	uint32_t              num_ents      = 10;
	daos_anchor_t         anchor        = {0};
	bool                  do_stop       = (!cb_arg->kill && test_arg->interactive_rebuild);
	const char           *pre_op        = (cb_arg->kill ? "kill" : "extend");
	int                   rc;
	int                   i;

	/* wait for first extend, and (as post-effect) get rebuild version so we can wait for
	 * the second rebuild to start (by waiting for a rebuild with version > first rs_version)
	 */
	print_message("before waiting for rebuild to start, pmap_ver=%u, rs_version=%u\n",
		      test_arg->pool.pool_info.pi_map_ver,
		      test_arg->pool.pool_info.pi_rebuild_st.rs_version);
	test_rebuild_wait_to_start_next(&test_arg, 1);
	print_message("Extending (rs_version=%u), sleep 10, %s rank %u, %sand start op %d (%s)\n",
		      test_arg->pool.pool_info.pi_rebuild_st.rs_version, pre_op, cb_arg->rank,
		      do_stop ? "stop rebuild, " : "", opc, extend_opstrs[opc]);

	sleep(5);

	if (cb_arg->kill) {
		/* Kill another rank during extend */
		daos_kill_server(test_arg, test_arg->pool.pool_uuid, test_arg->group,
				 test_arg->pool.alive_svc, cb_arg->rank);
	} else {
		/* Extend another rank during extend */
		print_message("extend pool " DF_UUID " rank %u\n",
			      DP_UUID(test_arg->pool.pool_uuid), cb_arg->rank);
		rc = dmg_pool_extend(test_arg->dmg_config, test_arg->pool.pool_uuid,
				     test_arg->group, &cb_arg->rank, 1);
		assert_int_equal(rc, 0);
	}

	if (do_stop) {
		daos_debug_set_params(test_arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
		print_message("before waiting for rebuild to start, pmap_ver=%u, rs_version=%u\n",
			      test_arg->pool.pool_info.pi_map_ver,
			      test_arg->pool.pool_info.pi_rebuild_st.rs_version);
		test_rebuild_wait_to_start_next(&test_arg, 1);
		print_message("second rebuild version=%u running\n",
			      test_arg->pool.pool_info.pi_rebuild_st.rs_version);
		rc = rebuild_stop_with_dmg(arg);
		assert_rc_equal(rc, 0);
		test_rebuild_wait_to_error(&test_arg, 1);
	}

	switch (opc) {
	case EXTEND_PUNCH:
		print_message("punch objects during extend one rank%s, %s rank %u\n",
			      do_stop ? ", stop rebuild" : "", pre_op, cb_arg->rank);
		for (i = 0; i < EXTEND_OBJ_NR; i++) {
			char filename[32];

			sprintf(filename, "file%d", i);
			rc = dfs_remove(dfs_mt, dir, filename, true, &oids[i]);
			assert_int_equal(rc, 0);
		}
		break;
	case EXTEND_STAT:
		print_message("stat objects during extend one rank%s, %s rank %u\n",
			      do_stop ? ", stop rebuild" : "", pre_op, cb_arg->rank);
		for (i = 0; i < EXTEND_OBJ_NR; i++) {
			char        filename[32];
			struct stat stbuf;

			sprintf(filename, "file%d", i);
			rc = dfs_stat(dfs_mt, dir, filename, &stbuf);
			assert_int_equal(rc, 0);
		}
		break;
	case EXTEND_ENUMERATE:
		print_message("enumerate objects during extend one rank%s, %s rank %u\n",
			      do_stop ? ", stop rebuild" : "", pre_op, cb_arg->rank);
		while (!daos_anchor_is_eof(&anchor)) {
			num_ents = 10;
			rc       = dfs_readdir(dfs_mt, dir, &anchor, &num_ents, ents);
			assert_int_equal(rc, 0);
			total_entries += num_ents;
		}
		assert_int_equal(total_entries, 1000);
		break;
	case EXTEND_FETCH:
		print_message("fetch objects during extend one rank%s, %s rank %u\n",
			      do_stop ? ", stop rebuild" : "", pre_op, cb_arg->rank);
		extend_read_check(dfs_mt, dir);
		break;
	case EXTEND_UPDATE:
		print_message("update objects during extend one rank%s, %s rank %u\n",
			      do_stop ? ", stop rebuild" : "", pre_op, cb_arg->rank);
		extend_write(dfs_mt, dir);
		break;
	default:
		break;
	}

	daos_debug_set_params(test_arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);

	if (do_stop)
		rebuild_resume_wait_to_start(arg);

	return 0;
}

static void
int_dfs_extend_enumerate_extend(void **state)
{
	test_arg_t *arg = *state;

	FAULT_INJECTION_REQUIRED();

	T_BEGIN();
	arg->interactive_rebuild = 1;
	dfs_extend_internal(state, EXTEND_ENUMERATE, int_extend_cb_internal, false);
	T_END();
}

static void
int_rebuild_dkeys_stop_failing(void **state)
{
	test_arg_t      *arg       = *state;
	d_rank_t         kill_rank = 0;
	int              kill_rank_nr;
	daos_obj_id_t    oid;
	struct ioreq     req;
	int              i;
	int              rc;

	FAULT_INJECTION_REQUIRED();

	if (!test_runable(arg, 4))
		return;

	T_BEGIN();

	oid = daos_test_oid_gen(arg->coh, arg->obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert records */
	print_message("Insert %d kv record in object " DF_OID "\n", KEY_NR, DP_OID(oid));
	for (i = 0; i < KEY_NR; i++) {
		char        key[32] = {0};
		daos_recx_t recx;
		char        data[DATA_SIZE];

		sprintf(key, "dkey_0_%d", i);
		insert_single(key, "a_key", 0, "data", strlen("data") + 1, DAOS_TX_NONE, &req);

		sprintf(key, "dkey_0_1M_%d", i);
		recx.rx_idx = 0;
		recx.rx_nr  = DATA_SIZE;

		memset(data, 'a', DATA_SIZE);
		insert_recxs(key, "a_key_1M", 1, DAOS_TX_NONE, &recx, 1, data, DATA_SIZE, &req);
	}

	/* Quick check that rebuild stop will return -DER_NONEXIST if nothing is rebuilding */
	rc = dmg_pool_rebuild_stop(arg->dmg_config, arg->pool.pool_uuid, arg->group,
				   false /* force */);
	assert_int_equal(rc, -DER_NONEXIST);

	get_killing_rank_by_oid(arg, oid, 1, 0, &kill_rank, &kill_rank_nr);
	ioreq_fini(&req);

	/* Cause first (and subsequent) rebuild attempts to fail with -DER_IO */
	if (arg->myrank == 0) {
		print_message("inject fault DAOS_REBUILD_OBJ_FAIL on all engines\n");
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_REBUILD_OBJ_FAIL | DAOS_FAIL_ALWAYS, 0, NULL);
	}

	/* Trigger exclude and rebuild, fail twice, force-stop command during second Fail_reclaim
	 * NB: stop will be deferred until after Fail_reclaim (since it did not fail); the stopped
	 *     rebuild's retry is then parked (delay=-1) and merges into the reintegrate below.
	 */
	arg->no_rebuild = 1;
	rebuild_single_pool_target(arg, kill_rank, -1, false);
	arg->no_rebuild = 0;
	print_message("before waiting for rebuild to start, pmap_ver=%u, rs_version=%u\n",
		      arg->pool.pool_info.pi_map_ver, arg->pool.pool_info.pi_rebuild_st.rs_version);
	test_rebuild_wait_to_start(&arg, 1);

	print_message("Wait for exclude rebuild ver %u to fail (and start Fail_reclaim)\n",
		      arg->pool.pool_info.pi_rebuild_st.rs_version);
	test_rebuild_wait_to_start_lower(&arg, 1);
	print_message("Wait for Fail_reclaim to finish (and start retry of exclude rebuild)\n");
	test_rebuild_wait_to_start_next(&arg, 1);
	print_message("Wait for second exclude rebuild to fail (and start Fail_reclaim)\n");
	test_rebuild_wait_to_start_lower(&arg, 1);

	print_message("Force-stop runaway failing exclude rebuild retries\n");
	rc = rebuild_force_stop_with_dmg(arg);
	assert_rc_equal(rc, 0);
	print_message("Waiting for exclude rebuild to stop\n");
	test_rebuild_wait(&arg, 1);
	assert_int_equal(arg->pool.pool_info.pi_rebuild_st.rs_state, DRS_NOT_STARTED);
	assert_int_equal(arg->pool.pool_info.pi_rebuild_st.rs_errno, -DER_OP_CANCELED);
	print_message("Exclude rebuild stopped\n");

	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);

	/* Do not explicitly restart the rebuild; the parked (deferred) retry of the stopped
	 * rebuild merges into this reintegrate of the same rank.
	 */
	reintegrate_with_inflight_io(arg, &oid, kill_rank, -1);
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	assert_rc_equal(rc, 0);
	T_END();
}

/*
 * Reproducer for DAOS-19381: engine assertion in determine_valid_spares() caused by a
 * failure-sequence (fseq) inversion between two rebuild lineages.
 *
 * Flow (mirrors the Aurora PS-leader log for pool f43bb1b7) is narrated inline in steps
 * (1)-(7) below; this header only records the non-obvious constraints that make it reproduce.
 *
 * CRITICAL #1 (kill, not exclude): the leader's cancel-on-new-DOWN path
 *      (rebuild_leader_status_check()) tests the RANK DOMAIN status (PO_COMP_ST_DOWN).  "dmg
 *      pool exclude" only marks the TARGETS down (domain stays UPIN), so the cancel never fires
 *      and rebuild-1 is never converted into a Fail_reclaim (the test hangs).  We KILL the
 *      engine so the rank domain itself goes DOWN.
 *
 * CRITICAL #2 (merge window): rank_A's rebuild must leave the ~5s scheduler-delay queue before
 *      rank_B is killed, else rebuild_try_merge_tgts() coalesces both failures into ONE task and
 *      the separate lower-fseq lineage never forms.  The pull-phase hang lets rebuild-1's scan
 *      finish (task moves to rg_running_list); we wait via test_rebuild_wait_to_scanning_next()
 *      before killing rank_B.
 *
 * WHY MANY OBJECTS: the assert only fires when the spare walk for a rank_A shard lands on
 *      rank_B.  get_target() skips domains the object already uses, so a single object with
 *      shards on both ranks always heals; across many objects some rank_A shard does not occupy
 *      rank_B and trips the assert.
 *
 * EXPECTED: WITHOUT the fix an engine aborts; WITH the fix rank_A's rebuild is re-queued and
 *      merged into the still-queued rebuild-2, so the run completes cleanly (test passes).
 *
 * NB (timing): race-sensitive; the hang/clear points may need tuning on faster/slower rigs.
 * NB: this test drives the real incident's REJECTED stop (-DER_NO_PERM), which without the fix
 *      still latched rgt_stop_admin and suppressed rebuild-1's retry, stranding rank_A.
 */
#define STRANDED_OBJ_NR 300
static void
int_rebuild_stranded_lower_fseq_target(void **state)
{
	test_arg_t   *arg = *state;
	daos_obj_id_t oids[STRANDED_OBJ_NR];
	struct ioreq  req;
	d_rank_t      rank_A;
	d_rank_t      rank_B;
	int           i;
	int           j;
	int           rc;

	FAULT_INJECTION_REQUIRED();

	/* Need >= 2 survivable rank failures: rebuild_sub_setup uses RF2 / 3-replica objects.
	 * We KILL two engines, so the pool service must have enough replicas to keep quorum
	 * (same constraint the rebuild_kill_multiple test uses).
	 */
	if (!test_runable(arg, 6) || arg->pool.alive_svc->rl_nr < 5)
		return;

	T_BEGIN();

	/* Create MANY 3-replica objects (see "why MANY objects" above). Two ranks are failed in
	 * sequence; across this many objects, at least one has a rank_A shard whose spare lands on
	 * rank_B.
	 */
	for (i = 0; i < STRANDED_OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, OC_RP_3G1, 0, 0, arg->myrank);
		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);
		for (j = 0; j < KEY_NR; j++) {
			char key[32] = {0};

			sprintf(key, "dkey_%d", j);
			insert_single(key, "a_key", 0, "data", strlen("data") + 1, DAOS_TX_NONE,
				      &req);
		}
		ioreq_fini(&req);
	}

	rank_A = get_rank_by_oid_shard(arg, oids[0], 0);
	rank_B = get_rank_by_oid_shard(arg, oids[0], 1);
	assert_int_not_equal(rank_A, rank_B);

	print_message("DAOS-19381 repro: rank_A(low fseq)=%u, rank_B(high fseq)=%u\n", rank_A,
		      rank_B);

	/* (1) Hang the rebuild PULL phase (after scan) so rebuild-1 (rank_A) completes its scan,
	 *     leaves rg_queue_list, and stays running (cancellable) but does not finish. Using the
	 *     pull-phase hang (not the scan hang) is what lets the task leave the mergeable queue
	 *     window so the later rank_B exclude forms a SEPARATE lineage instead of merging.
	 */
	if (arg->myrank == 0) {
		print_message("inject DAOS_REBUILD_TGT_REBUILD_HANG on all engines\n");
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_REBUILD_TGT_REBUILD_HANG | DAOS_FAIL_ALWAYS, 0, NULL);
	}

	/* (2) Kill engine rank_A -> rebuild-1 (lower fseq). Do not wait for completion.
	 *     NB: KILL (not exclude) so the rank DOMAIN goes DOWN - required for the leader's
	 *     cancel-on-new-DOWN check in step (3).
	 */
	arg->no_rebuild = 1;
	rebuild_single_pool_rank(arg, rank_A, true);
	arg->no_rebuild = 0;
	/* Wait until rebuild-1 is actually SCANNING (in-progress, out of the queue), NOT merely
	 * queued -- otherwise the rank_B failure below would be merged into rebuild-1's task.
	 */
	print_message("wait for rebuild-1 (rank %u) to leave the queue and start scanning\n",
		      rank_A);
	test_rebuild_wait_to_scanning_next(&arg, 1);

	/* (3) Kill engine rank_B while rebuild-1 is running -> leader cancels rebuild-1 (new DOWN
	 *     rank, fseq > rebuild-1 ver) -> op:Fail_reclaim for rebuild-1; rebuild-2 (rank_B,
	 *     higher fseq) queued.
	 */
	arg->no_rebuild = 1;
	rebuild_single_pool_rank(arg, rank_B, true);
	arg->no_rebuild = 0;
	print_message("wait for rebuild-1 Fail_reclaim (lower version) to start\n");
	test_rebuild_wait_to_start_lower(&arg, 1);

	/* (4) Stop during Fail_reclaim -> suppresses rebuild-1 auto-retry, strands rank_A DOWN. */
	print_message("issue dmg pool rebuild stop during Fail_reclaim\n");
	rc = rebuild_stop_with_dmg(arg);
	assert_rc_equal(rc, -DER_NO_PERM);

	/* (5) Release the hang so rebuild-2 (rank_B, higher fseq) can run. Clear the fault on each
	 *     surviving engine directly rather than via a rank=-1 broadcast: the broadcast is
	 *     always routed through rank 0, which may itself be a killed victim (rank_A/rank_B).
	 */
	if (arg->myrank == 0) {
		d_rank_t r;

		print_message("clear rebuild pull hang; let rebuild-2 (rank %u) run\n", rank_B);
		for (r = 0; r < (d_rank_t)arg->srv_nnodes; r++) {
			if (r == rank_A || r == rank_B)
				continue;
			daos_debug_set_params(arg->group, r, DMG_KEY_FAIL_LOC, 0, 0, NULL);
		}
	}

	/* (6) As soon as rebuild-2 (rank_B, higher version) is detected RUNNING, issue
	 *     "dmg pool rebuild start" to re-queue rank_A's stranded rebuild -- faithful to the
	 *     observed incident order (start issued WHILE rebuild-2 is still in flight, not after
	 * it finished). Either order reproduces the fseq inversion.
	 */
	print_message("wait for rebuild-2 (rank %u, higher version) to start\n", rank_B);
	test_rebuild_wait_to_start_next(&arg, 1);
	print_message("issue dmg pool rebuild start to re-queue stranded rank_A rebuild\n");
	rc = rebuild_start_with_dmg(arg);
	assert_rc_equal(rc, 0);

	/* (7) Wait for all rebuild activity to settle. As rebuild-2 completes, rank_B reaches
	 *     DOWNOUT (high fseq) while rank_A's re-queued rebuild is still DOWN (low fseq).
	 *     Bug: DOWN(rank_A, low fseq) vs DOWNOUT(rank_B, high fseq) -> engine assert.
	 *     Fixed: completes cleanly.
	 */
	test_rebuild_wait(&arg, 1);

	/* If the engine did not assert, all objects must still verify. */
	for (i = 0; i < STRANDED_OBJ_NR; i++) {
		rc = daos_obj_verify(arg->coh, oids[i], DAOS_EPOCH_MAX);
		assert_rc_equal(rc, 0);
	}

	/* Restart the killed engines so teardown's pool destroy can broadcast to every rank and
	 * reclaim their storage instead of stranding it.
	 */
	print_message("restart killed engines rank_A=%u, rank_B=%u before teardown\n", rank_A,
		      rank_B);
	daos_start_server(arg, arg->pool.pool_uuid, arg->group, arg->pool.alive_svc, rank_A);
	daos_start_server(arg, arg->pool.pool_uuid, arg->group, arg->pool.alive_svc, rank_B);
	sleep(10);
	T_END();
}

/** create a new pool/container for each test */
static const struct CMUnitTest rebuild_interactive_tests[] = {
    {"IREBUILD1: interactive exclude: records with multiple snapshots",
     int_rebuild_snap_update_recs, rebuild_small_sub_setup, test_teardown},
    {"IREBUILD2: interactive exclude: punch/records with multiple snapshots",
     int_rebuild_snap_punch_recs, rebuild_small_sub_setup, test_teardown},
    {"IREBUILD3: interactive exclude: lot of objects with failure",
     int_rebuild_many_objects_with_failure, rebuild_sub_setup, test_teardown},
    {"IREBUILD4: interactive drain: cont open and update during rebuild", int_cont_open_in_drain,
     rebuild_small_sub_rf0_setup, test_teardown},
    {"IREBUILD5: drain fail and retry", int_drain_fail_and_retry_objects, rebuild_sub_rf0_setup,
     test_teardown},
    {"IREBUILD6: interactive drain: overwrite during rebuild", int_dfs_drain_overwrite,
     rebuild_sub_rf0_setup, test_teardown},
    {"IREBUILD7: interactive extend: enumerate object during two rebuilds",
     int_dfs_extend_enumerate_extend, rebuild_sub_3nodes_rf0_setup, test_teardown},
    {"IREBUILD8: interactive exclude: stop repeatedly-failing rebuild",
     int_rebuild_dkeys_stop_failing, rebuild_small_sub_setup, test_teardown},
    {"IREBUILD9: interactive kill: stranded lower-fseq target (DAOS-19381)",
     int_rebuild_stranded_lower_fseq_target, rebuild_sub_setup, test_teardown},
};

int
run_daos_int_rebuild_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	par_barrier(PAR_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(rebuild_interactive_tests);
		sub_tests      = NULL;
	}

	rc = run_daos_sub_tests_only("DAOS_Rebuild_Interactive", rebuild_interactive_tests,
				     ARRAY_SIZE(rebuild_interactive_tests), sub_tests,
				     sub_tests_size);

	par_barrier(PAR_COMM_WORLD);

	return rc;
}
