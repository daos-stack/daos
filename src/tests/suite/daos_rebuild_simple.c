/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is for simple tests of rebuild, which does not need to kill the
 * rank, and only used to verify the consistency after different data model
 * rebuild.
 *
 * tests/suite/daos_rebuild_simple.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"
#include "dfs_test.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

#define KEY_NR		10
#define OBJ_NR		10
#define OBJ_CLS		OC_RP_3G1
#define OBJ_REPLICAS	3
#define DEFAULT_FAIL_TGT 0
#define REBUILD_POOL_SIZE	(4ULL << 30)

#define DATA_SIZE	(1048576 * 2 + 512)

static int
reintegrate_inflight_io(void *data)
{
	test_arg_t	*arg = data;
	daos_obj_id_t	oid = *(daos_obj_id_t *)arg->rebuild_cb_arg;
	struct ioreq	req;
	int		i;

	rebuild_pool_connect_internal(arg);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 5; i++) {
		char	key[32];
		daos_recx_t recx;
		char	buf[DATA_SIZE];

		sprintf(key, "d_inflight_%d", i);
		insert_single(key, "a_key", 0, "data", strlen("data") + 1,
			      DAOS_TX_NONE, &req);

		sprintf(key, "d_inflight_1M_%d", i);
		recx.rx_idx = 0;
		recx.rx_nr = DATA_SIZE;
		memset(buf, 'a', DATA_SIZE);
		insert_recxs(key, "a_key_1M", 1, DAOS_TX_NONE, &recx, 1,
			     buf, DATA_SIZE, &req);
	}
	ioreq_fini(&req);
	rebuild_pool_disconnect_internal(arg);
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0,
				      NULL);
	return 0;
}

static void
reintegrate_with_inflight_io(test_arg_t *arg, daos_obj_id_t *oid,
			     d_rank_t rank, int tgt)
{
	daos_obj_id_t inflight_oid;

#if 0
	/* Disable it due to DAOS-7420 */
	if (oid != NULL) {
		inflight_oid = *oid;
	} else {
#endif
	inflight_oid = daos_test_oid_gen(arg->coh,
					 DAOS_OC_R3S_SPEC_RANK, 0,
					 0, arg->myrank);
	inflight_oid = dts_oid_set_rank(inflight_oid, rank);

	arg->rebuild_cb = reintegrate_inflight_io;
	arg->rebuild_cb_arg = &inflight_oid;

	/* To make sure the IO will be done before reintegration is done */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_REBUILD_TGT_REBUILD_HANG, 0, NULL);
	reintegrate_single_pool_target(arg, rank, tgt);
	arg->rebuild_cb = NULL;
	arg->rebuild_cb_arg = NULL;

#if 0
	/* Disable it due to DAOS-7420 */
	if (oid == NULL) {
		int rc;

		rc = daos_obj_verify(arg->coh, inflight_oid, DAOS_EPOCH_MAX);
		if (rc != 0)
			assert_rc_equal(rc, -DER_NOSYS);
	}
#endif
}

static void
rebuild_dkeys(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	d_rank_t		kill_rank = 0;
	int			kill_rank_nr;
	int			i;
	int			rc;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, arg->obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	for (i = 0; i < 5; i++) {
		char	key[32] = {0};
		daos_recx_t recx;
		char	data[DATA_SIZE];

		sprintf(key, "dkey_0_%d", i);
		insert_single(key, "a_key", 0, "data", strlen("data") + 1,
			      DAOS_TX_NONE, &req);

		sprintf(key, "dkey_0_1M_%d", i);
		recx.rx_idx = 0;
		recx.rx_nr = DATA_SIZE;

		memset(data, 'a', DATA_SIZE);
		insert_recxs(key, "a_key_1M", 1, DAOS_TX_NONE, &recx, 1,
			     data, DATA_SIZE, &req);
	}

	get_killing_rank_by_oid(arg, oid, 1, 0, &kill_rank, &kill_rank_nr);
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, kill_rank, -1, false);
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);

	reintegrate_with_inflight_io(arg, &oid, kill_rank, -1);

	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);
}

static void
rebuild_akeys(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	d_rank_t		kill_rank = 0;
	int			kill_rank_nr;
	int			tgt = -1;
	int			i;
	int			rc;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, arg->obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	for (i = 0; i < KEY_NR; i++) {
		char		key[32] = {0};
		daos_recx_t	recx;
		char		data[DATA_SIZE];

		sprintf(key, "%d", i);
		insert_single("dkey_1_0", key, 0, "data", strlen("data") + 1,
			      DAOS_TX_NONE, &req);

		sprintf(key, "dkey_1_1M_%d", i);
		recx.rx_idx = 0;
		recx.rx_nr = DATA_SIZE;

		memset(data, 'a', DATA_SIZE);
		insert_recxs(key, "a_key_1M", 1, DAOS_TX_NONE, &recx, 1,
			     data, DATA_SIZE, &req);

	}

	get_killing_rank_by_oid(arg, oid, 1, 0, &kill_rank, &kill_rank_nr);
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, kill_rank, tgt, false);
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);

	reintegrate_with_inflight_io(arg, &oid, kill_rank, tgt);
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);
}

static void
rebuild_indexes(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			tgt = DEFAULT_FAIL_TGT;
	int			i;
	int			j;
	int			rc;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, arg->obj_class, 0, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 2000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      2000, DP_OID(oid));
	for (i = 0; i < KEY_NR; i++) {
		char	key[32] = {0};

		sprintf(key, "dkey_2_%d", i);
		for (j = 0; j < 20; j++)
			insert_single(key, "a_key", j, "data",
				      strlen("data") + 1, DAOS_TX_NONE, &req);
	}
	ioreq_fini(&req);

	/* Rebuild rank 1 */
	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);

	reintegrate_with_inflight_io(arg, &oid, ranks_to_kill[0], tgt);
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);
}

#define SNAP_CNT	20
static void
rebuild_snap_update_recs(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	daos_recx_t	recx;
	int		tgt = DEFAULT_FAIL_TGT;
	char		string[100] = { 0 };
	daos_epoch_t	snap_epoch[SNAP_CNT];
	int		i;
	int		rc;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, arg->obj_class, 0, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < SNAP_CNT; i++)
		sprintf(string + strlen(string), "old-snap%d", i);

	recx.rx_idx = 0;
	recx.rx_nr = strlen(string);
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1, string,
		     strlen(string) + 1, &req);

	for (i = 0; i < SNAP_CNT; i++) {
		char data[100] = { 0 };

		/* Update string for each snapshot */
		daos_cont_create_snap(arg->coh, &snap_epoch[i], NULL, NULL);
		sprintf(data, "new-snap%d", i);
		recx.rx_idx = i * strlen(data);
		recx.rx_nr = strlen(data);
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1, data,
			      strlen(data) + 1, &req);
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

	reintegrate_with_inflight_io(arg, &oid, ranks_to_kill[0], tgt);
	for (i = 0; i < SNAP_CNT; i++) {
		rc = daos_obj_verify(arg->coh, oid, snap_epoch[i]);
		if (rc != 0)
			assert_rc_equal(rc, -DER_NOSYS);
	}
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);
}

static void
rebuild_snap_punch_recs(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	daos_recx_t	recx;
	int		tgt = DEFAULT_FAIL_TGT;
	char		string[200];
	daos_epoch_t	snap_epoch[SNAP_CNT];
	int		i;
	int		rc;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, arg->obj_class, 0, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < SNAP_CNT; i++)
		sprintf(string + strlen(string), "old-snap%d", i);

	recx.rx_idx = 0;
	recx.rx_nr = strlen(string);
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1, string,
		     strlen(string) + 1, &req);

	for (i = 0; i < SNAP_CNT; i++) {
		/* punch string */
		daos_cont_create_snap(arg->coh, &snap_epoch[i], NULL, NULL);
		recx.rx_idx = i * 9; /* strlen("old-snap%d") */
		recx.rx_nr = 9;
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

	reintegrate_with_inflight_io(arg, &oid, ranks_to_kill[0], tgt);
	for (i = 0; i < SNAP_CNT; i++) {
		rc = daos_obj_verify(arg->coh, oid, snap_epoch[i]);
		if (rc != 0)
			assert_rc_equal(rc, -DER_NOSYS);
	}
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);
}

static void
rebuild_snap_update_keys(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		tgt = DEFAULT_FAIL_TGT;
	daos_epoch_t	snap_epoch[SNAP_CNT];
	int		i;
	int		rc;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, arg->obj_class, 0, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	/* Insert dkey/akey by different snapshot */
	for (i = 0; i < SNAP_CNT; i++) {
		char dkey[20] = { 0 };
		char akey[20] = { 0 };

		/* Update string for each snapshot */
		daos_cont_create_snap(arg->coh, &snap_epoch[i], NULL, NULL);
		sprintf(dkey, "dkey_%d", i);
		sprintf(akey, "akey_%d", i);
		insert_single(dkey, "a_key", 0, "data", 1, DAOS_TX_NONE, &req);
		insert_single("dkey", akey, 0, "data", 1, DAOS_TX_NONE, &req);
	}

	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	daos_fail_loc_set(DAOS_OBJ_SPECIAL_SHARD);
	for (i = 0; i < OBJ_REPLICAS; i++) {
		uint32_t	number;
		daos_key_desc_t kds[SNAP_CNT];
		daos_anchor_t	anchor = { 0 };
		char		buf[256];
		int		buf_len = 256;
		int		j;

		daos_fail_value_set(i);
		for (j = 0; j < SNAP_CNT; j++) {
			daos_handle_t	th_open;

			memset(&anchor, 0, sizeof(anchor));
			daos_tx_open_snap(arg->coh, snap_epoch[j], &th_open,
					  NULL);
			number = SNAP_CNT;
			enumerate_dkey(th_open, &number, kds, &anchor, buf,
				       buf_len, &req);

			assert_int_equal(number, j > 0 ? j+1 : 0);

			number = SNAP_CNT;
			memset(&anchor, 0, sizeof(anchor));
			enumerate_akey(th_open, "dkey", &number, kds, &anchor,
				       buf, buf_len, &req);

			assert_int_equal(number, j);
			daos_tx_close(th_open, NULL);
		}
		number = SNAP_CNT;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_dkey(DAOS_TX_NONE, &number, kds, &anchor, buf,
			       buf_len, &req);
		assert_int_equal(number, SNAP_CNT);

		number = SNAP_CNT;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_akey(DAOS_TX_NONE, "dkey", &number, kds, &anchor,
			       buf, buf_len, &req);
		assert_int_equal(number, SNAP_CNT);
	}

	reintegrate_with_inflight_io(arg, &oid, ranks_to_kill[0], tgt);
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);
	ioreq_fini(&req);
}

static void
rebuild_snap_punch_keys(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		tgt = DEFAULT_FAIL_TGT;
	daos_epoch_t	snap_epoch[SNAP_CNT];
	int		i;
	int		rc;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, arg->obj_class, 0, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	/* Insert dkey/akey */
	for (i = 0; i < SNAP_CNT; i++) {
		char dkey[20] = { 0 };
		char akey[20] = { 0 };
		char akey2[20] = { 0 };

		/* Update string for each snapshot */
		sprintf(dkey, "dkey_%d", i);
		sprintf(akey, "akey_%d", i);
		sprintf(akey2, "akey_%d", i + 100);
		insert_single(dkey, "a_key", 0, "data", 1, DAOS_TX_NONE, &req);
		insert_single("dkey", akey, 0, "data", 1, DAOS_TX_NONE, &req);
		/* Add an extra akey so punch propagation doesn't get rid of
		 * the dkey on the punch below
		 */
		insert_single("dkey", akey2, 0, "data", 1, DAOS_TX_NONE, &req);
	}

	/* punch dkey/akey by different epoch */
	for (i = 0; i < SNAP_CNT; i++) {
		char dkey[20] = { 0 };
		char akey[20] = { 0 };

		daos_cont_create_snap(arg->coh, &snap_epoch[i], NULL, NULL);

		sprintf(dkey, "dkey_%d", i);
		sprintf(akey, "akey_%d", i);
		punch_dkey(dkey, DAOS_TX_NONE, &req);
		punch_akey("dkey", akey, DAOS_TX_NONE, &req);
	}

	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	daos_fail_loc_set(DAOS_OBJ_SPECIAL_SHARD);
	for (i = 0; i < OBJ_REPLICAS; i++) {
		daos_key_desc_t  kds[2 * SNAP_CNT];
		daos_anchor_t	 anchor;
		char		 buf[512];
		int		 buf_len = 512;
		uint32_t	 number;
		int		 j;

		daos_fail_value_set(i);
		for (j = 0; j < SNAP_CNT; j++) {
			daos_handle_t th_open;

			rc = daos_tx_open_snap(arg->coh, snap_epoch[j],
					       &th_open, NULL);
			assert_int_equal(rc, 0);
			number = 2 * SNAP_CNT;
			memset(&anchor, 0, sizeof(anchor));
			enumerate_dkey(th_open, &number, kds, &anchor, buf,
				       buf_len, &req);
			assert_int_equal(number, 21 - j);

			number = 2 * SNAP_CNT;
			memset(&anchor, 0, sizeof(anchor));
			enumerate_akey(th_open, "dkey", &number, kds,
				       &anchor, buf, buf_len, &req);
			assert_int_equal(number, 2 * SNAP_CNT - j);

			daos_tx_close(th_open, NULL);
		}

		number =  2 * SNAP_CNT;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_dkey(DAOS_TX_NONE, &number, kds, &anchor, buf,
			       buf_len, &req);
		assert_int_equal(number, 1);

		number = 2 * SNAP_CNT;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_akey(DAOS_TX_NONE, "dkey", &number, kds, &anchor,
			       buf, buf_len, &req);
		assert_int_equal(number, SNAP_CNT);
	}

	reintegrate_with_inflight_io(arg, &oid, ranks_to_kill[0], tgt);
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);
	ioreq_fini(&req);
}

static void
rebuild_snap_punch_empty(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		tgt = DEFAULT_FAIL_TGT;
	daos_epoch_t	snap_epoch;
	int		i;
	int		rc;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0, 0,
				arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/* insert a record */
	insert_single("d_key", "a_key", 0, "data", 1, DAOS_TX_NONE, &req);

	daos_cont_create_snap(arg->coh, &snap_epoch, NULL, NULL);

	punch_obj(DAOS_TX_NONE, &req);

	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	daos_fail_loc_set(DAOS_OBJ_SPECIAL_SHARD);
	for (i = 0; i < OBJ_REPLICAS; i++) {
		daos_key_desc_t  kds[10];
		daos_anchor_t	 anchor;
		daos_handle_t	 th_open;
		char		 buf[256];
		int		 buf_len = 256;
		uint32_t	 number;

		daos_fail_value_set(i);
		daos_tx_open_snap(arg->coh, snap_epoch, &th_open, NULL);
		number = 10;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_dkey(th_open, &number, kds, &anchor, buf, buf_len,
			       &req);
		assert_int_equal(number, 1);

		number = 10;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_akey(th_open, "d_key", &number, kds, &anchor, buf,
			       buf_len, &req);
		assert_int_equal(number, 1);

		daos_tx_close(th_open, NULL);

		number = 10;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_dkey(DAOS_TX_NONE, &number, kds, &anchor, buf,
			       buf_len, &req);
		assert_int_equal(number, 0);

		number = 10;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_akey(DAOS_TX_NONE, "d_key", &number, kds, &anchor,
			       buf, buf_len, &req);
		assert_int_equal(number, 0);
	}

	reintegrate_single_pool_target(arg, ranks_to_kill[0], tgt);
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);
	ioreq_fini(&req);
}

static void
rebuild_multiple(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		tgt = DEFAULT_FAIL_TGT;
	int		i;
	int		j;
	int		k;
	int		rc;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, arg->obj_class, 0, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      1000, DP_OID(oid));
	for (i = 0; i < 10; i++) {
		char	dkey[32] = {0};

		sprintf(dkey, "dkey_3_%d", i);
		for (j = 0; j < 10; j++) {
			char	akey[32] = {0};

			sprintf(akey, "akey_%d", j);
			for (k = 0; k < 10; k++)
				insert_single(dkey, akey, k, "data",
					      strlen("data") + 1,
					      DAOS_TX_NONE, &req);
		}
	}
	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);
	ioreq_fini(&req);
}

#define LARGE_BUFFER_SIZE	(32 * 1024 * 4)
static void
rebuild_large_rec(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			tgt = DEFAULT_FAIL_TGT;
	int			i;
	char			buffer[LARGE_BUFFER_SIZE];
	int			rc;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, arg->obj_class, 0, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	memset(buffer, 'a', LARGE_BUFFER_SIZE);
	for (i = 0; i < KEY_NR; i++) {
		char	key[32] = {0};
		const char *lakey = "a_key_L";
		daos_size_t iod_size = 1;
		int	rx_nr = LARGE_BUFFER_SIZE;
		uint64_t offset = 0;
		void	*data = buffer;

		sprintf(key, "dkey_4_%d", i);

		insert_single(key, "a_key", 0, buffer, 5000, DAOS_TX_NONE,
			      &req);

		insert(key, 1, &lakey, &iod_size, &rx_nr,
		       &offset, &data, DAOS_TX_NONE, &req, 0);
	}

	ioreq_fini(&req);

	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);

	reintegrate_with_inflight_io(arg, &oid, ranks_to_kill[0], tgt);
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);
}

static void
rebuild_objects(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		tgt = DEFAULT_FAIL_TGT;
	int		i;
	int		rc;

	if (!test_runable(arg, 4))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, arg->obj_class, 0, 0,
				      arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
		oids[i] = dts_oid_set_tgt(oids[i], DEFAULT_FAIL_TGT);
	}

	rebuild_io(arg, oids, OBJ_NR);

	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	for (i = 0; i < OBJ_NR; i++) {
		rc = daos_obj_verify(arg->coh, oids[i], DAOS_EPOCH_MAX);
		if (rc != 0)
			assert_rc_equal(rc, -DER_NOSYS);
	}

	reintegrate_with_inflight_io(arg, NULL, ranks_to_kill[0], tgt);
	for (i = 0; i < OBJ_NR; i++) {
		rc = daos_obj_verify(arg->coh, oids[i], DAOS_EPOCH_MAX);
		if (rc != 0)
			assert_rc_equal(rc, -DER_NOSYS);
	}
}

static void
rebuild_sx_object_internal(void **state, uint16_t oclass)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	char		dkey[32];
	const char	akey[] = "test_update akey";
	const char	rec[]  = "test_update record";
	d_rank_t	rank = 2;
	int		rank_nr = 1;
	int		i;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, oclass, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	print_message("insert 100 dkeys\n");
	for (i = 0; i < 100; i++) {
		sprintf(dkey, "dkey_%d\n", i);
		insert_single(dkey, akey, 0, (void *)rec, strlen(rec),
			      DAOS_TX_NONE, &req);
	}

	get_killing_rank_by_oid(arg, oid, 1, 0, &rank, &rank_nr);
	/** exclude the target of this obj's replicas */
	daos_exclude_server(arg->pool.pool_uuid, arg->group,
			    arg->dmg_config, rank);

	/* wait until rebuild done */
	test_rebuild_wait(&arg, 1);

	/* add back the excluded targets */
	daos_reint_server(arg->pool.pool_uuid, arg->group,
			  arg->dmg_config, rank);

	/* wait until reintegration is done */
	test_rebuild_wait(&arg, 1);

	print_message("lookup 100 dkeys\n");
	for (i = 0; i < 100; i++) {
		char buffer[32];

		memset(buffer, 0, 32);
		sprintf(dkey, "dkey_%d\n", i);
		lookup_single(dkey, akey, 0, buffer, 32, DAOS_TX_NONE, &req);
		assert_string_equal(buffer, rec);
	}
	ioreq_fini(&req);
}

static void
rebuild_sx_object(void **state)
{
	rebuild_sx_object_internal(state, OC_SX);
}

static void
rebuild_xsf_object(void **state)
{
	rebuild_sx_object_internal(state, OC_RP_XSF);
}

static void
rebuild_large_object(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	char		dkey[32];
	const char	akey[] = "test_update akey";
	const char	rec[]  = "test_update record";
	d_rank_t	rank = 2;
	int		i;
	int		j;

	if (!test_runable(arg, 4))
		return;

	for (i = 0; i < 5; i++) {
		oid = daos_test_oid_gen(arg->coh, OC_RP_2G8, 0, 0, arg->myrank);
		ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
		for (j = 0; j < 10; j++) {
			sprintf(dkey, "dkey_%d\n", j);
			insert_single(dkey, akey, 0, (void *)rec, strlen(rec),
				      DAOS_TX_NONE, &req);
		}
		ioreq_fini(&req);
	}

	/** exclude the target of this obj's replicas */
	daos_exclude_server(arg->pool.pool_uuid, arg->group,
			    arg->dmg_config, rank);

	/* wait until rebuild done */
	test_rebuild_wait(&arg, 1);

	/* add back the excluded targets */
	daos_reint_server(arg->pool.pool_uuid, arg->group,
			  arg->dmg_config, rank);

	/* wait until reintegration is done */
	test_rebuild_wait(&arg, 1);
}

int
rebuild_small_pool_n4_setup(void **state)
{
	test_arg_t	*arg;
	int rc;

	save_group_state(state);
	rc = test_setup(state, SETUP_CONT_CONNECT, true,
			REBUILD_SMALL_POOL_SIZE, 4, NULL);
	if (rc) {
		/* Let's skip for this case, since it is possible there
		 * is not enough ranks here.
		 */
		print_message("It can not create the pool with 4 ranks"
			      " probably due to not enough ranks %d\n", rc);
		return 0;
	}

	arg = *state;
	if (dt_obj_class != DAOS_OC_UNKNOWN)
		arg->obj_class = dt_obj_class;
	else
		arg->obj_class = DAOS_OC_R3S_SPEC_RANK;

	return 0;
}

static void
rebuild_large_snap(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		tgt = DEFAULT_FAIL_TGT;
	daos_epoch_t	snap_epoch[100];
	int		i;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, arg->obj_class, 0, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	/* Insert dkey/akey by different snapshot */
	for (i = 0; i < 100; i++) {
		char dkey[20] = { 0 };
		char akey[20] = { 0 };

		/* Update string for each snapshot */
		daos_cont_create_snap(arg->coh, &snap_epoch[i], NULL, NULL);
		sprintf(dkey, "dkey_%d", i);
		sprintf(akey, "akey_%d", i);
		insert_single(dkey, "a_key", 0, "data", 1, DAOS_TX_NONE, &req);
		insert_single("dkey", akey, 0, "data", 1, DAOS_TX_NONE, &req);
	}

	rebuild_single_pool_target(arg, ranks_to_kill[0], tgt, false);
	ioreq_fini(&req);
	reintegrate_single_pool_target(arg, ranks_to_kill[0], tgt);
}

static void
rebuild_full_shards(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		i;

	if (!test_runable(arg, 4))
		return;

	/* require 4 nodes and 8 targets per node */
	if (arg->myrank == 0 &&
	    arg->srv_ntgts / arg->srv_nnodes != 8)
		return;

	oid = daos_test_oid_gen(arg->coh, OC_RP_2G8, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	/* Insert dkey/akey by different snapshot */
	for (i = 0; i < 100; i++) {
		char dkey[20] = { 0 };
		char akey[20] = { 0 };

		/* Update string for each snapshot */
		sprintf(dkey, "dkey_%d", i);
		sprintf(akey, "akey_%d", i);
		insert_single(dkey, "a_key", 0, "data", 1, DAOS_TX_NONE, &req);
		insert_single("dkey", akey, 0, "data", 1, DAOS_TX_NONE, &req);
	}

	ioreq_fini(&req);

	/* rebuild and reintegration to use full shards */
	rebuild_single_pool_target(arg, 0, -1, false);
	rebuild_single_pool_target(arg, 3, -1, false);
	reintegrate_single_pool_target(arg, 0, -1);
	reintegrate_single_pool_target(arg, 3, -1);
}

static void
rebuild_punch_recs(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	daos_recx_t	recx;
	char		buffer[1001] = { 0 };
	int		i;
	int		rc;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, arg->obj_class, 0, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	memset(buffer, 'a', 1000);
	recx.rx_idx = 0;
	recx.rx_nr = 1000;
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1, buffer,
		     1000, &req);

	for (i = 0; i < 5; i++) {
		/* punch string */
		recx.rx_idx = i * 100;
		recx.rx_nr = 50;
		punch_recxs("d_key", "a_key", &recx, 1, DAOS_TX_NONE, &req);
	}
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, ranks_to_kill[0], -1, false);

	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);
}

static void
rebuild_multiple_group(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	d_rank_t		kill_rank = 0;
	int			kill_rank_nr;
	int			i;
	int			rc;

	if (!test_runable(arg, 7))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_RP_2G3, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	for (i = 0; i < 50; i++) {
		char	key[32] = {0};
		daos_recx_t recx;
		char	data[10];
		char	akey[32] = {0};
		int	j;

		sprintf(key, "dkey_0_%d", i);
		recx.rx_idx = 0;
		recx.rx_nr = 10;
		memset(data, 'a', 10);
		for (j = 0; j < 10; j++) {
			sprintf(akey, "a_key_s_%d", j);
			insert_single(key, akey, 0, "data", strlen("data") + 1,
				      DAOS_TX_NONE, &req);
			sprintf(akey, "a_key_a_%d", j);
			insert_recxs(key, akey, 1, DAOS_TX_NONE, &recx, 1,
				     data, 10, &req);
		}
	}

	get_killing_rank_by_oid(arg, oid, 1, 0, &kill_rank, &kill_rank_nr);
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, kill_rank, -1, false);
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);

	reintegrate_with_inflight_io(arg, &oid, kill_rank, -1);

	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);
}

/** i/o to variable idx offset */
static void
rebuild_with_large_offset(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	daos_off_t	offset;
	d_rank_t	kill_rank = 0;
	int		kill_rank_nr;
	daos_recx_t	recxs[IOREQ_IOD_NR] = { 0 };
	char		data[128];
	int		i;
	int		rc;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, arg->obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	memset(data, 'a', 128);
	for (offset = (UINT64_MAX >> 1), i = 0;
	     offset > 0 && i < IOREQ_IOD_NR;
	     offset >>= 16, i++) {
		recxs[i].rx_idx = offset;
		recxs[i].rx_nr = 5;
	}

	insert_recxs("large_idx_dkey", "large_idx_akey", 1, DAOS_TX_NONE,
		     recxs, i, data, i * 5, &req);

	get_killing_rank_by_oid(arg, oid, 1, 0, &kill_rank, &kill_rank_nr);
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, kill_rank, -1, false);
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);

	reintegrate_with_inflight_io(arg, &oid, kill_rank, -1);

	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);
}

#define LARGE_KEY_SIZE	1048576
/** i/o to variable idx offset */
static void
rebuild_with_large_key(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	char		*dkey;
	char		*akey;
	d_rank_t	kill_rank = 0;
	int		kill_rank_nr;
	int		i;
	int		rc;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, arg->obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	dkey = calloc(LARGE_KEY_SIZE, 1);
	akey = calloc(LARGE_KEY_SIZE, 1);
	memset(dkey, 'd', LARGE_KEY_SIZE - 1);
	for (i = 0; i < 10; i++) {
		memset(akey, 'a' + i, LARGE_KEY_SIZE - 1);
		insert_single(dkey, akey, 0, "data", strlen("data") + 1,
			      DAOS_TX_NONE, &req);
	}

	get_killing_rank_by_oid(arg, oid, 1, 0, &kill_rank, &kill_rank_nr);
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, kill_rank, -1, false);
	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);

	reintegrate_with_inflight_io(arg, &oid, kill_rank, -1);

	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	if (rc != 0)
		assert_rc_equal(rc, -DER_NOSYS);
	free(dkey);
	free(akey);
}

void
rebuild_with_dfs_open_create_punch(void **state)
{
	dfs_t		*dfs_mt;
	daos_handle_t	co_hdl;
	test_arg_t	*arg = *state;
	dfs_obj_t	*obj;
	dfs_obj_t	*dir;
	uuid_t		co_uuid;
	char		filename[32];
	d_rank_t	rank;
	daos_obj_id_t	oid;
	int		i;
	daos_size_t	chunk_size = 1048576;
	int		rc;

	if (!test_runable(arg, 6))
		return;

	uuid_generate(co_uuid);
	rc = dfs_cont_create(arg->pool.poh, co_uuid, NULL, &co_hdl, &dfs_mt);
	assert_int_equal(rc, 0);
	printf("Created DFS Container "DF_UUIDF"\n", DP_UUID(co_uuid));

	rc = dfs_open(dfs_mt, NULL, "dir1", S_IWUSR | S_IRUSR | S_IFDIR,
		      O_RDWR | O_CREAT, OC_RP_2G1, 0, NULL, &dir);
	assert_int_equal(rc, 0);

	for (i = 0; i < 20; i++) {
		sprintf(filename, "degrade_file_%d", i);
		rc = dfs_open(dfs_mt, dir, filename, S_IFREG | S_IWUSR | S_IRUSR,
			      O_RDWR | O_CREAT | O_EXCL, OC_RP_3G6, chunk_size, NULL, &obj);
		assert_int_equal(rc, 0);

		rc = dfs_release(obj);
		assert_int_equal(rc, 0);
	}

	dfs_obj2id(dir, &oid);

	rank = get_rank_by_oid_shard(arg, oid, 0);
	rebuild_single_pool_rank(arg, rank, false);
	reintegrate_single_pool_rank_no_disconnect(arg, rank);

	for (i = 0; i < 20; i++) {
		sprintf(filename, "degrade_file_%d", i);
		rc = dfs_remove(dfs_mt, dir, filename, 0, NULL);
		assert_int_equal(rc, 0);

		rc = dfs_open(dfs_mt, dir, filename, S_IFREG | S_IWUSR | S_IRUSR,
			      O_RDWR | O_CREAT | O_EXCL, OC_RP_3G6, chunk_size, NULL, &obj);
		assert_int_equal(rc, 0);
		rc = dfs_release(obj);
		assert_int_equal(rc, 0);
	}

	daos_cont_status_clear(co_hdl, NULL);

	rc = dfs_release(dir);
	assert_int_equal(rc, 0);

	rc = dfs_umount(dfs_mt);
	assert_int_equal(rc, 0);

	rc = daos_cont_close(co_hdl, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_cont_destroy(arg->pool.poh, co_uuid, 1, NULL);
	assert_rc_equal(rc, 0);
}

/** create a new pool/container for each test */
static const struct CMUnitTest rebuild_tests[] = {
	{"REBUILD1: rebuild small rec multiple dkeys",
	 rebuild_dkeys, rebuild_small_sub_setup, test_teardown},
	{"REBUILD2: rebuild small rec multiple akeys",
	 rebuild_akeys, rebuild_small_sub_setup, test_teardown},
	{"REBUILD3: rebuild small rec multiple indexes",
	 rebuild_indexes, rebuild_small_sub_setup, test_teardown},
	{"REBUILD4: rebuild small rec multiple keys/indexes",
	 rebuild_multiple, rebuild_small_sub_setup, test_teardown},
	{"REBUILD5: rebuild large rec single index",
	 rebuild_large_rec, rebuild_small_sub_setup, test_teardown},
	{"REBUILD6: rebuild records with multiple snapshots",
	 rebuild_snap_update_recs, rebuild_small_sub_setup, test_teardown},
	{"REBUILD7: rebuild punch/records with multiple snapshots",
	 rebuild_snap_punch_recs, rebuild_small_sub_setup, test_teardown},
	{"REBUILD8: rebuild keys with multiple snapshots",
	 rebuild_snap_update_keys, rebuild_small_sub_setup, test_teardown},
	{"REBUILD9: rebuild keys/punch with multiple snapshots",
	 rebuild_snap_punch_keys, rebuild_small_sub_setup, test_teardown},
	{"REBUILD10: rebuild multiple objects",
	 rebuild_objects, rebuild_sub_setup, test_teardown},
	{"REBUILD11: rebuild snapshotted punched object",
	 rebuild_snap_punch_empty, rebuild_small_sub_setup, test_teardown},
	{"REBUILD12: rebuild sx object",
	 rebuild_sx_object, rebuild_small_sub_setup, test_teardown},
	{"REBUILD13: rebuild xsf object",
	 rebuild_xsf_object, rebuild_small_sub_setup, test_teardown},
	{"REBUILD14: rebuild large stripe object",
	 rebuild_large_object, rebuild_small_pool_n4_setup, test_teardown},
	{"REBUILD15: rebuild with 100 snapshot",
	 rebuild_large_snap, rebuild_small_sub_setup, test_teardown},
	{"REBUILD16: rebuild with full stripe",
	 rebuild_full_shards, rebuild_small_pool_n4_setup, test_teardown},
	{"REBUILD17: rebuild with punch recxs",
	 rebuild_punch_recs, rebuild_small_sub_setup, test_teardown},
	{"REBUILD18: rebuild with multiple group",
	 rebuild_multiple_group, rebuild_small_sub_setup, test_teardown},
	{"REBUILD19: rebuild with large offset",
	 rebuild_with_large_offset, rebuild_small_sub_setup, test_teardown},
	{"REBUILD20: rebuild with large key",
	 rebuild_with_large_key, rebuild_small_sub_setup, test_teardown},
	{"REBUILD21: rebuild with dfs open create punch",
	 rebuild_with_dfs_open_create_punch, rebuild_small_sub_setup, test_teardown},
};

int
run_daos_rebuild_simple_test(int rank, int size, int *sub_tests,
			     int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(rebuild_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests_only("DAOS_Rebuild_Simple", rebuild_tests,
				     ARRAY_SIZE(rebuild_tests), sub_tests,
				     sub_tests_size);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
