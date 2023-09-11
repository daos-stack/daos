/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is for simple tests of drain, which does not need to kill the
 * rank, and only used to verify the consistency after different data model
 * drains.
 *
 * tests/suite/daos_drain_simple.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"
#include "dfs_test.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

#define KEY_NR		100
#define OBJ_NR		10
#define OBJ_CLS		OC_RP_3G1
#define OBJ_REPLICAS	3
#define DEFAULT_FAIL_TGT 0
#define DRAIN_POOL_SIZE	(4ULL << 30)
#define DRAIN_SUBTEST_POOL_SIZE (1ULL << 30)
#define DRAIN_SMALL_POOL_SIZE (1ULL << 28)

static void
drain_dkeys(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			tgt = DEFAULT_FAIL_TGT;
	int			i;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R1S_SPEC_RANK, 0, 0,
				arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	for (i = 0; i < KEY_NR; i++) {
		char	key[32] = {0};

		sprintf(key, "dkey_0_%d", i);
		insert_single(key, "a_key", 0, "data", strlen("data") + 1,
			      DAOS_TX_NONE, &req);
	}

	arg->rebuild_cb = reintegrate_inflight_io;
	arg->rebuild_cb_arg = &oid;
	drain_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	for (i = 0; i < KEY_NR; i++) {
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
}

static int
cont_open_and_inflight_io(void *data)
{
	test_arg_t	*arg = data;
	int		 rc;

	assert_int_equal(arg->setup_state, SETUP_CONT_CREATE);
	rc = test_setup_next_step((void **)&arg, NULL, NULL, NULL);
	assert_success(rc);
	assert_int_equal(arg->setup_state, SETUP_CONT_CONNECT);

	return reintegrate_inflight_io(data);
}

static void
cont_open_in_drain(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			tgt = DEFAULT_FAIL_TGT;
	int			i;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R1S_SPEC_RANK, 0, 0,
				arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	for (i = 0; i < KEY_NR; i++) {
		char	key[32] = {0};

		sprintf(key, "dkey_0_%d", i);
		insert_single(key, "a_key", 0, "data", strlen("data") + 1,
			      DAOS_TX_NONE, &req);
	}
	ioreq_fini(&req);

	test_teardown_cont_hdl(arg);
	arg->rebuild_cb = cont_open_and_inflight_io;
	arg->rebuild_cb_arg = &oid;
	drain_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < KEY_NR; i++) {
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
}

static void
drain_akeys(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			tgt = DEFAULT_FAIL_TGT;
	int			i;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R1S_SPEC_RANK, 0, 0,
				arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	for (i = 0; i < KEY_NR; i++) {
		char	akey[16];

		sprintf(akey, "%d", i);
		insert_single("dkey_1_0", akey, 0, "data", strlen("data") + 1,
			      DAOS_TX_NONE, &req);
	}

	arg->rebuild_cb = reintegrate_inflight_io;
	arg->rebuild_cb_arg = &oid;
	drain_single_pool_target(arg, ranks_to_kill[0], tgt, false);
	for (i = 0; i < KEY_NR; i++) {
		char akey[32] = {0};
		char buf[16];

		sprintf(akey, "%d", i);
		/** Lookup */
		memset(buf, 0, 10);
		lookup_single("dkey_1_0", akey, 0, buf, 10, DAOS_TX_NONE, &req);
		assert_int_equal(req.iod[0].iod_size, strlen("data") + 1);

		/** Verify data consistency */
		assert_string_equal(buf, "data");
	}
	reintegrate_inflight_io_verify(arg);

	ioreq_fini(&req);
}

static void
drain_indexes(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			tgt = DEFAULT_FAIL_TGT;
	int			i;
	int			j;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R1S_SPEC_RANK, 0, 0,
				arg->myrank);
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

	/* Drain rank 1 */
	arg->rebuild_cb = reintegrate_inflight_io;
	arg->rebuild_cb_arg = &oid;
	drain_single_pool_target(arg, ranks_to_kill[0], tgt, false);
	for (i = 0; i < KEY_NR; i++) {
		char	key[32] = {0};
		char	buf[16];

		sprintf(key, "dkey_2_%d", i);
		for (j = 0; j < 20; j++) {
			memset(buf, 0, 10);
			lookup_single(key, "a_key", j, buf, 10, DAOS_TX_NONE,
				      &req);
			assert_int_equal(req.iod[0].iod_size,
					 strlen("data") + 1);
			assert_string_equal(buf, "data");
		}
	}

	reintegrate_inflight_io_verify(arg);
	ioreq_fini(&req);
}

static void
drain_snap_update_keys(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		tgt = DEFAULT_FAIL_TGT;
	daos_epoch_t	snap_epoch[5];
	int		i;
	uint32_t	number;
	daos_key_desc_t kds[10];
	daos_anchor_t	anchor = { 0 };
	char		buf[256];
	int		buf_len = 256;


	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R1S_SPEC_RANK, 0, 0,
				arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	/* Insert dkey/akey by different snapshot */
	for (i = 0; i < 5; i++) {
		char dkey[20] = { 0 };
		char akey[20] = { 0 };

		/* Update string for each snapshot */
		daos_cont_create_snap(arg->coh, &snap_epoch[i], NULL, NULL);
		sprintf(dkey, "dkey_%d", i);
		sprintf(akey, "akey_%d", i);
		insert_single(dkey, "a_key", 0, "data", 1, DAOS_TX_NONE, &req);
		insert_single("dkey", akey, 0, "data", 1, DAOS_TX_NONE, &req);
	}

	arg->rebuild_cb = reintegrate_inflight_io;
	arg->rebuild_cb_arg = &oid;
	drain_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	for (i = 0; i < 5; i++) {
		daos_handle_t	th_open;

		memset(&anchor, 0, sizeof(anchor));
		daos_tx_open_snap(arg->coh, snap_epoch[i], &th_open, NULL);
		number = 10;
		enumerate_dkey(th_open, &number, kds, &anchor, buf,
			       buf_len, &req);

		assert_int_equal(number, i > 0 ? i + 1 : 0);

		number = 10;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_akey(th_open, "dkey", &number, kds, &anchor,
			       buf, buf_len, &req);

		assert_int_equal(number, i);
		daos_tx_close(th_open, NULL);
	}
	number = 10;
	memset(&anchor, 0, sizeof(anchor));
	enumerate_dkey(DAOS_TX_NONE, &number, kds, &anchor, buf, buf_len, &req);
	assert_int_equal(number, 10);

	number = 10;
	memset(&anchor, 0, sizeof(anchor));
	enumerate_akey(DAOS_TX_NONE, "dkey", &number, kds, &anchor,
		       buf, buf_len, &req);
	assert_int_equal(number, 5);

	reintegrate_inflight_io_verify(arg);

	ioreq_fini(&req);
}

static void
drain_snap_punch_keys(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		tgt = DEFAULT_FAIL_TGT;
	daos_epoch_t	snap_epoch[5];
	int		i;
	daos_key_desc_t  kds[10];
	daos_anchor_t	 anchor;
	char		 buf[256];
	int		 buf_len = 256;
	uint32_t	 number;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0, 0,
				arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	/* Insert dkey/akey */
	for (i = 0; i < 5; i++) {
		char dkey[20] = { 0 };
		char akey[20] = { 0 };
		char akey2[20] = { 0 };

		/* Update string for each snapshot */
		sprintf(dkey, "dkey_%d", i);
		sprintf(akey, "akey_%d", i);
		sprintf(akey2, "akey_%d", 100 + i);
		insert_single(dkey, "a_key", 0, "data", 1, DAOS_TX_NONE, &req);
		insert_single("dkey", akey, 0, "data", 1, DAOS_TX_NONE, &req);
		insert_single("dkey", akey2, 0, "data", 1, DAOS_TX_NONE, &req);
	}

	/* Insert dkey/akey by different epoch */
	for (i = 0; i < 5; i++) {
		char dkey[20] = { 0 };
		char akey[20] = { 0 };

		daos_cont_create_snap(arg->coh, &snap_epoch[i], NULL, NULL);

		sprintf(dkey, "dkey_%d", i);
		sprintf(akey, "akey_%d", i);
		punch_dkey(dkey, DAOS_TX_NONE, &req);
		punch_akey("dkey", akey, DAOS_TX_NONE, &req);
	}

	arg->rebuild_cb = reintegrate_inflight_io;
	arg->rebuild_cb_arg = &oid;
	drain_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	for (i = 0; i < 5; i++) {
		daos_handle_t th_open;

		daos_tx_open_snap(arg->coh, snap_epoch[i], &th_open, NULL);
		number = 10;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_dkey(th_open, &number, kds, &anchor, buf,
			       buf_len, &req);
		assert_int_equal(number, 6 - i);

		number = 10;
		memset(&anchor, 0, sizeof(anchor));
		enumerate_akey(th_open, "dkey", &number, kds,
			       &anchor, buf, buf_len, &req);
		assert_int_equal(number, 10 - i);

		daos_tx_close(th_open, NULL);
	}

	number = 10;
	memset(&anchor, 0, sizeof(anchor));
	enumerate_dkey(DAOS_TX_NONE, &number, kds, &anchor, buf,
		       buf_len, &req);
	assert_int_equal(number, 10);

	number = 10;
	memset(&anchor, 0, sizeof(anchor));
	enumerate_akey(DAOS_TX_NONE, "dkey", &number, kds, &anchor,
		       buf, buf_len, &req);
	assert_int_equal(number, 5);
	reintegrate_inflight_io_verify(arg);

	ioreq_fini(&req);
}

static void
drain_multiple(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		tgt = DEFAULT_FAIL_TGT;
	int		i;
	int		j;
	int		k;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R1S_SPEC_RANK, 0, 0,
				arg->myrank);
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

	arg->rebuild_cb = reintegrate_inflight_io;
	arg->rebuild_cb_arg = &oid;
	drain_single_pool_target(arg, ranks_to_kill[0], tgt, false);
	for (i = 0; i < 10; i++) {
		char	dkey[32] = {0};

		sprintf(dkey, "dkey_3_%d", i);
		for (j = 0; j < 10; j++) {
			char	akey[32] = {0};
			char	buf[10];

			memset(buf, 0, 10);
			sprintf(akey, "akey_%d", j);
			for (k = 0; k < 10; k++) {
				lookup_single(dkey, akey, k, buf,
					      strlen("data") + 1,
					      DAOS_TX_NONE, &req);
				assert_int_equal(req.iod[0].iod_size,
						 strlen("data") + 1);
				assert_string_equal(buf, "data");
			}
		}
	}
	reintegrate_inflight_io_verify(arg);

	ioreq_fini(&req);
}

static void
drain_large_rec(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			tgt = DEFAULT_FAIL_TGT;
	int			i;
	char			buffer[5000];
	char			v_buffer[5000];

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R1S_SPEC_RANK, 0, 0,
				arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	oid = dts_oid_set_tgt(oid, tgt);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	memset(buffer, 'a', 5000);
	for (i = 0; i < KEY_NR; i++) {
		char	key[32] = {0};

		sprintf(key, "dkey_4_%d", i);
		insert_single(key, "a_key", 0, buffer, 5000, DAOS_TX_NONE,
			      &req);
	}

	arg->rebuild_cb = reintegrate_inflight_io;
	arg->rebuild_cb_arg = &oid;
	drain_single_pool_target(arg, ranks_to_kill[0], tgt, false);
	memset(v_buffer, 'a', 5000);
	for (i = 0; i < KEY_NR; i++) {
		char	key[32] = {0};

		sprintf(key, "dkey_4_%d", i);
		memset(buffer, 0, 5000);
		lookup_single(key, "a_key", 0, buffer, 5000, DAOS_TX_NONE,
			      &req);
		assert_memory_equal(v_buffer, buffer, 5000);
	}

	reintegrate_inflight_io_verify(arg);

	ioreq_fini(&req);
}

static void
drain_objects(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		tgt = DEFAULT_FAIL_TGT;
	int		i;

	if (!test_runable(arg, 4))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R1S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
		oids[i] = dts_oid_set_tgt(oids[i], DEFAULT_FAIL_TGT);
	}

	rebuild_io(arg, oids, OBJ_NR);
	arg->rebuild_cb = reintegrate_inflight_io;
	arg->rebuild_cb_arg = &oids[0];
	drain_single_pool_target(arg, ranks_to_kill[0], tgt, false);

	rebuild_io_validate(arg, oids, OBJ_NR);
	reintegrate_inflight_io_verify(arg);
}

static void
drain_fail_and_retry_objects(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 4))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, DAOS_OC_R1S_SPEC_RANK, 0,
					    0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
		oids[i] = dts_oid_set_tgt(oids[i], DEFAULT_FAIL_TGT);
	}

	rebuild_io(arg, oids, OBJ_NR);
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			      DAOS_REBUILD_OBJ_FAIL | DAOS_FAIL_ALWAYS, 0, NULL);

	drain_single_pool_rank(arg, ranks_to_kill[0], false);

	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
	rebuild_io_validate(arg, oids, OBJ_NR);

	drain_single_pool_rank(arg, ranks_to_kill[0], false);
	rebuild_io_validate(arg, oids, OBJ_NR);
}

static void
drain_then_exclude(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;

	if (!test_runable(arg, 4))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_2P1GX, 0, 0, arg->myrank);
	rebuild_io(arg, &oid, 1);

	drain_single_pool_rank(arg, ranks_to_kill[0], false);

	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
	rebuild_io_validate(arg, &oid, 1);

	daos_kill_server(arg, arg->pool.pool_uuid, arg->group, arg->pool.alive_svc,
			 ranks_to_kill[0]);

	reintegrate_single_pool_rank(arg, ranks_to_kill[0], true);
	rebuild_io_validate(arg, &oid, 1);
}

#define EXTEND_DRAIN_OBJ_NR	5
#define WRITE_SIZE		(1048576 * 5)
struct extend_drain_cb_arg{
	daos_obj_id_t	*oids;
	dfs_t		*dfs_mt;
	dfs_obj_t	*dir;
	d_rank_t	rank;
	uint32_t	objclass;
	int		opc;
};

enum extend_drain_opc {
	EXTEND_DRAIN_PUNCH,
	EXTEND_DRAIN_STAT,
	EXTEND_DRAIN_ENUMERATE,
	EXTEND_DRAIN_FETCH,
	EXTEND_DRAIN_UPDATE,
	EXTEND_DRAIN_OVERWRITE,
	EXTEND_DRAIN_WRITELOOP,
};

static void
extend_drain_read_check(dfs_t *dfs_mt, dfs_obj_t *dir, uint32_t objclass, uint32_t objcnt,
			daos_size_t total_size, char start_char)
{
	char		*buf = NULL;
	char		*verify_buf = NULL;
	daos_size_t	buf_size = 512 * 1024;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	d_iov_t		verify_iov;
	int		i;

	buf = malloc(buf_size);
	verify_buf = malloc(buf_size);
	assert_non_null(buf);
	assert_non_null(verify_buf);
	d_iov_set(&iov, buf, buf_size);
	d_iov_set(&verify_iov, buf, buf_size);
	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;

	for (i = 0; i < objcnt; i++) {
		char filename[32];
		daos_size_t read_size = buf_size;
		dfs_obj_t *obj;
		daos_off_t offset = 0;
		daos_size_t total = total_size;
		int rc;

		sprintf(filename, "file%d", i);
		rc = dfs_open(dfs_mt, dir, filename, S_IFREG | S_IWUSR | S_IRUSR,
			      O_RDWR, objclass, 1048576, NULL, &obj);
		assert_int_equal(rc, 0);

		memset(verify_buf, start_char + i, buf_size);

		while (total > 0) {
			memset(buf, 0, buf_size);
			rc = dfs_read(dfs_mt, obj, &sgl, offset, &read_size, NULL);
			assert_int_equal(rc, 0);
			assert_memory_equal(buf, verify_buf, read_size);
			offset += read_size;
			total -= read_size;
		}

		rc = dfs_release(obj);
		assert_int_equal(rc, 0);
	}
	free(buf);
	free(verify_buf);
}

static void
extend_drain_write(dfs_t *dfs_mt, dfs_obj_t *dir, uint32_t objclass, uint32_t objcnt,
		   daos_size_t total_size, char write_char, daos_obj_id_t *oids)
{
	char		*buf = NULL;
	daos_size_t	buf_size = 512 * 1024;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	int		i;

	buf = malloc(buf_size);
	assert_non_null(buf);
	d_iov_set(&iov, buf, buf_size);
	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;

	for (i = 0; i < objcnt; i++) {
		char filename[32];
		dfs_obj_t *obj;
		daos_size_t total = total_size;
		daos_off_t offset = 0;
		int rc;

		sprintf(filename, "file%d", i);
		rc = dfs_open(dfs_mt, dir, filename, S_IFREG | S_IWUSR | S_IRUSR,
			      O_RDWR | O_CREAT, OC_EC_2P1GX, 1048576, NULL, &obj);
		assert_int_equal(rc, 0);
		if (oids != NULL)
			dfs_obj2id(obj, &oids[i]);

		memset(buf, write_char + i, buf_size);
		while (total > 0) {
			rc = dfs_write(dfs_mt, obj, &sgl, offset, NULL);
			assert_int_equal(rc, 0);
			offset += buf_size;
			total -= buf_size;
		}
		rc = dfs_release(obj);
		assert_int_equal(rc, 0);
	}
	free(buf);
}

static int
extend_drain_cb_internal(void *arg)
{
	test_arg_t		*test_arg = arg;
	struct extend_drain_cb_arg	*cb_arg = test_arg->rebuild_cb_arg;
	dfs_t			*dfs_mt = cb_arg->dfs_mt;
	daos_obj_id_t		*oids = cb_arg->oids;
	dfs_obj_t		*dir = cb_arg->dir;
	uint32_t		objclass = cb_arg->objclass;
	struct dirent		ents[10];
	int			opc = cb_arg->opc;
	int			total_entries = 0;
	uint32_t		num_ents = 10;
	daos_anchor_t		anchor = { 0 };
	int			rc;
	int			i;

	if (opc != EXTEND_DRAIN_WRITELOOP) {
		print_message("sleep 5 seconds then start op %d\n", opc);
		sleep(5);
	}

	/* Kill another rank during extend */
	switch(opc) {
	case EXTEND_DRAIN_PUNCH:
		print_message("punch objects during extend & drain\n");
		for (i = 0; i < EXTEND_DRAIN_OBJ_NR; i++) {
			char filename[32];

			sprintf(filename, "file%d", i);
			rc = dfs_remove(dfs_mt, dir, filename, true, &oids[i]);
			assert_int_equal(rc, 0);
		}
		break;
	case EXTEND_DRAIN_STAT:
		print_message("stat objects during extend & drain\n");
		for (i = 0; i < EXTEND_DRAIN_OBJ_NR; i++) {
			char		filename[32];
			struct stat	stbuf;

			sprintf(filename, "file%d", i);
			rc = dfs_stat(dfs_mt, dir, filename, &stbuf);
			assert_int_equal(rc, 0);
		}
		break;
	case EXTEND_DRAIN_ENUMERATE:
		print_message("enumerate objects during extend & drain\n");
		while (!daos_anchor_is_eof(&anchor)) {
			num_ents = 10;
			rc = dfs_readdir(dfs_mt, dir, &anchor, &num_ents, ents);
			assert_int_equal(rc, 0);
			total_entries += num_ents;
		}
		assert_int_equal(total_entries, EXTEND_DRAIN_OBJ_NR);
		break;
	case EXTEND_DRAIN_FETCH:
		print_message("fetch objects during extend & drain\n");
		extend_drain_read_check(dfs_mt, dir, objclass, EXTEND_DRAIN_OBJ_NR, WRITE_SIZE,
					'a');
		break;
	case EXTEND_DRAIN_UPDATE:
		print_message("update objects during extend & drain\n");
		extend_drain_write(dfs_mt, dir, objclass, EXTEND_DRAIN_OBJ_NR, WRITE_SIZE, 'a',
				   NULL);
		break;
	case EXTEND_DRAIN_OVERWRITE:
		print_message("overwrite objects during extend & drain\n");
		extend_drain_write(dfs_mt, dir, objclass, EXTEND_DRAIN_OBJ_NR, WRITE_SIZE, 'b',
				   NULL);
		break;
	case EXTEND_DRAIN_WRITELOOP:
		print_message("keepwrite objects during extend & drain\n");
		extend_drain_write(dfs_mt, dir, objclass, 1, 512 * 1048576, 'a', NULL);
		break;
	default:
		break;
	}

	daos_debug_set_params(test_arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);

	return 0;
}

static void
extend_drain_check(dfs_t *dfs_mt, dfs_obj_t *dir, int objclass, int opc)
{
	switch (opc) {
	case EXTEND_DRAIN_PUNCH:
		break;
	case EXTEND_DRAIN_OVERWRITE:
		extend_drain_read_check(dfs_mt, dir, objclass, EXTEND_DRAIN_OBJ_NR,
					WRITE_SIZE, 'b');
		break;
	case EXTEND_DRAIN_WRITELOOP:
		extend_drain_read_check(dfs_mt, dir, objclass, 1, 512 * 1048576, 'a');
		break;
	default:
		extend_drain_read_check(dfs_mt, dir, objclass, EXTEND_DRAIN_OBJ_NR,
					WRITE_SIZE, 'a');
		break;
	}
}

void
dfs_extend_drain_common(void **state, int opc, uint32_t objclass)
{
	test_arg_t	*arg = *state;
	dfs_t		*dfs_mt;
	daos_handle_t	co_hdl;
	dfs_obj_t	*dir;
	uuid_t		co_uuid;
	char		str[37];
	daos_obj_id_t	oids[EXTEND_DRAIN_OBJ_NR];
	struct extend_drain_cb_arg cb_arg;
	dfs_attr_t attr = {};
	int		rc;

	if (!test_runable(arg, 4))
		return;

	attr.da_props = daos_prop_alloc(2);
	assert_non_null(attr.da_props);
	attr.da_props->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_LVL;
	attr.da_props->dpp_entries[0].dpe_val = DAOS_PROP_CO_REDUN_RANK;
	attr.da_props->dpp_entries[1].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	attr.da_props->dpp_entries[1].dpe_val = DAOS_PROP_CO_REDUN_RF1;
	rc = dfs_cont_create(arg->pool.poh, &co_uuid, &attr, &co_hdl, &dfs_mt);
	daos_prop_free(attr.da_props);
	assert_int_equal(rc, 0);
	print_message("Created DFS Container "DF_UUIDF"\n", DP_UUID(co_uuid));

	rc = dfs_open(dfs_mt, NULL, "dir", S_IFDIR | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, objclass, 0, NULL, &dir);
	assert_int_equal(rc, 0);

	/* Create 10 files */
	if (opc != EXTEND_DRAIN_UPDATE)
		extend_drain_write(dfs_mt, dir, objclass, EXTEND_DRAIN_OBJ_NR, WRITE_SIZE,
				   'a', oids);

	cb_arg.oids = oids;
	cb_arg.dfs_mt = dfs_mt;
	cb_arg.dir = dir;
	cb_arg.opc = opc;
	cb_arg.objclass = objclass;
	arg->rebuild_cb = extend_drain_cb_internal;
	arg->rebuild_cb_arg = &cb_arg;

	/* HOLD rebuild ULT */
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			      DAOS_REBUILD_TGT_SCAN_HANG | DAOS_FAIL_ALWAYS, 0, NULL);
	drain_single_pool_rank(arg, ranks_to_kill[0], false);

	extend_drain_check(dfs_mt, dir, objclass, opc);

	daos_kill_server(arg, arg->pool.pool_uuid, arg->group, arg->pool.alive_svc,
			 ranks_to_kill[0]);
	arg->rebuild_cb = NULL;
	arg->rebuild_cb_arg = NULL;
	reintegrate_single_pool_rank(arg, ranks_to_kill[0], true);

	extend_drain_check(dfs_mt, dir, objclass, opc);

	rc = dfs_release(dir);
	assert_int_equal(rc, 0);
	rc = dfs_umount(dfs_mt);
	assert_int_equal(rc, 0);

	rc = daos_cont_close(co_hdl, NULL);
	assert_rc_equal(rc, 0);

	uuid_unparse(co_uuid, str);
	rc = daos_cont_destroy(arg->pool.poh, str, 1, NULL);
	assert_rc_equal(rc, 0);
}

void
dfs_drain_punch(void **state)
{
	dfs_extend_drain_common(state, EXTEND_DRAIN_PUNCH, OC_EC_2P1GX);
	dfs_extend_drain_common(state, EXTEND_DRAIN_PUNCH, OC_EC_4P2GX);
}

void
dfs_drain_stat(void **state)
{
	dfs_extend_drain_common(state, EXTEND_DRAIN_STAT, OC_EC_2P1GX);
	dfs_extend_drain_common(state, EXTEND_DRAIN_STAT, OC_EC_4P2GX);
}

void
dfs_drain_enumerate(void **state)
{
	dfs_extend_drain_common(state, EXTEND_DRAIN_ENUMERATE, OC_EC_2P1GX);
	dfs_extend_drain_common(state, EXTEND_DRAIN_ENUMERATE, OC_EC_4P2GX);
}

void
dfs_drain_fetch(void **state)
{
	dfs_extend_drain_common(state, EXTEND_DRAIN_FETCH, OC_EC_2P1GX);
	dfs_extend_drain_common(state, EXTEND_DRAIN_FETCH, OC_EC_4P2GX);
}

void
dfs_drain_update(void **state)
{
	dfs_extend_drain_common(state, EXTEND_DRAIN_UPDATE, OC_EC_2P1GX);
	dfs_extend_drain_common(state, EXTEND_DRAIN_UPDATE, OC_EC_4P2GX);
}

void
dfs_drain_overwrite(void **state)
{
	dfs_extend_drain_common(state, EXTEND_DRAIN_OVERWRITE, OC_EC_2P1GX);
	dfs_extend_drain_common(state, EXTEND_DRAIN_OVERWRITE, OC_EC_4P2GX);
}

void
dfs_drain_writeloop(void **state)
{
	dfs_extend_drain_common(state, EXTEND_DRAIN_WRITELOOP, OC_EC_2P1GX);
	dfs_extend_drain_common(state, EXTEND_DRAIN_WRITELOOP, OC_EC_4P2GX);
}

/** create a new pool/container for each test */
static const struct CMUnitTest drain_tests[] = {
	{"DRAIN0: drain small rec multiple dkeys",
	 drain_dkeys, rebuild_small_sub_rf0_setup, test_teardown},
	{"DRAIN1: cont open and update during drain",
	 cont_open_in_drain, rebuild_small_sub_rf0_setup, test_teardown},
	{"DRAIN2: drain small rec multiple akeys",
	 drain_akeys, rebuild_small_sub_rf0_setup, test_teardown},
	{"DRAIN3: drain small rec multiple indexes",
	 drain_indexes, rebuild_small_sub_rf0_setup, test_teardown},
	{"DRAIN4: drain small rec multiple keys/indexes",
	 drain_multiple, rebuild_small_sub_rf0_setup, test_teardown},
	{"DRAIN5: drain large rec single index",
	 drain_large_rec, rebuild_small_sub_rf0_setup, test_teardown},
	{"DRAIN6: drain keys with multiple snapshots",
	 drain_snap_update_keys, rebuild_small_sub_rf0_setup, test_teardown},
	{"DRAIN7: drain keys/punch with multiple snapshots",
	 drain_snap_punch_keys, rebuild_small_sub_setup, test_teardown},
	{"DRAIN8: drain multiple objects",
	 drain_objects, rebuild_sub_rf0_setup, test_teardown},
	{"DRAIN9: drain fail and retry",
	 drain_fail_and_retry_objects, rebuild_sub_rf0_setup, test_teardown},
	{"DRAIN10: drain then exclude",
	 drain_then_exclude, rebuild_sub_rf0_setup, test_teardown},
	{"DRAIN11: punch during drain",
	 dfs_drain_punch, rebuild_sub_rf0_setup, test_teardown},
	{"DRAIN12: stat during drain",
	 dfs_drain_stat, rebuild_sub_rf0_setup, test_teardown},
	{"DRAIN13: enumerate during drain",
	 dfs_drain_enumerate, rebuild_sub_rf0_setup, test_teardown},
	{"DRAIN14: fetch during drain",
	 dfs_drain_fetch, rebuild_sub_rf0_setup, test_teardown},
	{"DRAIN15: update during drain",
	 dfs_drain_update, rebuild_sub_rf0_setup, test_teardown},
	{"DRAIN16: overwrite during drain",
	 dfs_drain_overwrite, rebuild_sub_rf0_setup, test_teardown},
	{"DRAIN17: keep write during drain",
	 dfs_drain_writeloop, rebuild_sub_rf0_setup, test_teardown},
};

int
run_daos_drain_simple_test(int rank, int size, int *sub_tests,
			     int sub_tests_size)
{
	int rc = 0;

	par_barrier(PAR_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(drain_tests);
		sub_tests = NULL;
	}

	run_daos_sub_tests_only("DAOS_Drain_Simple", drain_tests,
				ARRAY_SIZE(drain_tests), sub_tests,
				sub_tests_size);

	par_barrier(PAR_COMM_WORLD);

	return rc;
}
