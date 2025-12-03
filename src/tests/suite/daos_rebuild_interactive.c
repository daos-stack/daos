/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
#define OBJ_NR           10

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
		if (rc != 0)
			assert_rc_equal(rc, -DER_NOSYS);
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
	arg->interactive_rebuild = 1;
	arg->rebuild_cb          = rebuild_stop_with_dmg;
	arg->rebuild_post_cb     = rebuild_resume_wait;
	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);
	arg->rebuild_cb      = NULL;
	arg->rebuild_post_cb = NULL;

	for (i = 0; i < SNAP_CNT; i++) {
		rc = daos_obj_verify(arg->coh, oid, snap_epoch[i]);
		if (rc != 0)
			assert_rc_equal(rc, -DER_NOSYS);
	}
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);

	arg->interactive_rebuild = 0;
	reintegrate_with_inflight_io(arg, &oid, ranks_to_kill[0], tgt);
	for (i = 0; i < SNAP_CNT; i++) {
		rc = daos_obj_verify(arg->coh, oid, snap_epoch[i]);
		if (rc != 0)
			assert_rc_equal(rc, -DER_NOSYS);
	}
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);
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
		if (rc != 0)
			assert_rc_equal(rc, -DER_NOSYS);
	}
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);

	/* insert rebuild stop|start into the reintegrate rebuild execution */
	arg->interactive_rebuild = 1;
	reintegrate_with_inflight_io(arg, &oid, ranks_to_kill[0], tgt);
	for (i = 0; i < SNAP_CNT; i++) {
		rc = daos_obj_verify(arg->coh, oid, snap_epoch[i]);
		if (rc != 0)
			assert_rc_equal(rc, -DER_NOSYS);
	}
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);
	T_END();
}

static int
rebuild_wait_error_reset_fail_cb(void *data)
{
	test_arg_t *arg = data;
	int         rc;

	print_message("wait until rebuild errors (and starts Fail_reclaim)\n");
	test_rebuild_wait_to_error(&arg, 1);
	print_message("check rebuild errored, rs_errno=%d (expecting -DER_IO=%d)\n",
		      arg->pool.pool_info.pi_rebuild_st.rs_errno, -DER_IO);
	assert_int_equal(arg->pool.pool_info.pi_rebuild_st.rs_errno, -DER_IO);
	print_message("rebuild error code check passed\n");

	print_message("clearing fault injection on all engines\n");
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE, 0, 0, NULL);
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_NUM, 0, 0, NULL);

	/* Give time for transition from op:Rebuild into op:Fail_reclaim */
	sleep(2);

	print_message(
	    "send rebuild stop --force request during first/only Fail_reclaim operation\n");
	rc = rebuild_force_stop_with_dmg(data);
	if (rc != 0)
		print_message("rebuild_force_stop_with_dmg failed, rc=%d\n", rc);

	print_message("wait for rebuild to be stopped\n");
	test_rebuild_wait(&arg, 1);
	/* Verifying rs_state/rs_errno will happen in post_cb rebuild_resume_wait() */

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

	if (!test_runable(arg, 6))
		return;

	T_BEGIN();
	arg->interactive_rebuild = 1;
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
	 * 1. trigger rebuild (which will fail), query pool reubild state until op:Rebuild fails
	 *    and op:Fail_reclaim begins. See test_rebuild_wait_to_error().
	 * 2. Then, while rebuild is in op:Fail_reclaim, issue dmg system stop to test that you
	 * can't stop during Fail_reclaim (though the command will take effect by not retrying
	 * rebuild).
	 */
	arg->rebuild_cb      = rebuild_wait_error_reset_fail_cb;
	arg->rebuild_post_cb = rebuild_resume_wait;
	rebuild_single_pool_target(arg, 3, -1, false);

	for (i = 0; i < NUM_OBJS; i++) {
		rc = daos_obj_verify(arg->coh, oids[i], DAOS_EPOCH_MAX);
		if (rc != 0)
			assert_rc_equal(rc, -DER_NOSYS);
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
	print_message("wait drain to fail and exit\n");
	/* NB: could be better to wait (in drain_single_pool_rank or test_rebuild_wait), but that
	 *  requires new logic in rebuild_task_complete_schedule() to update state after
	 * Fail_reclaim
	 */
	print_message("wait for op:Reclaim to get -DER_IO\n");
	test_rebuild_wait_to_error(&arg, 1);
	print_message("sleep for op:Fail_reclaim to run\n");
	sleep(30);
	arg->no_rebuild = 0;

	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
	rebuild_io_validate(arg, oids, OBJ_NR);

	arg->interactive_rebuild = 1;
	arg->rebuild_cb          = reintegrate_inflight_io;
	arg->rebuild_cb_arg      = &oids[OBJ_NR - 1];
	drain_single_pool_rank(arg, ranks_to_kill[0], false);
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

	if (test_arg->interactive_rebuild)
		rebuild_stop_with_dmg(arg);

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
	daos_pool_info_t      pinfo         = {0};
	int                   rc;
	int                   i;

	/* get rebuild version for first extend, so we can wait for second rebuild to start
	 * (by waiting for an in-progress rebuild with version > pinfo.pi_rebuild_st.rs_version)
	 */
	pinfo.pi_bits = DPI_REBUILD_STATUS;
	rc            = test_pool_get_info(test_arg, &pinfo, NULL /* engine_ranks */);
	assert_rc_equal(rc, 0);

	print_message("Extending (rs_version=%u), sleep 10, %s rank %u, %sand start op %d (%s)\n",
		      pinfo.pi_rebuild_st.rs_version, pre_op, cb_arg->rank,
		      do_stop ? "stop rebuild, " : "", opc, extend_opstrs[opc]);

	sleep(10);

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
		test_rebuild_wait_to_start_after_ver(
		    &test_arg, 1,
		    pinfo.pi_rebuild_st.rs_version /* original extend rebuild version */);
		rebuild_stop_with_dmg(arg); /* then stop the new rebuild */
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
