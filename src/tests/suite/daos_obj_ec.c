/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is for simple tests of ec object
 *
 * tests/suite/daos_ec_obj.c
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"
#include "dfs_test.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>
#include <daos/event.h>
#include "../../object/obj_ec.h"

unsigned int ec_obj_class = OC_EC_4P2G1;

static int
get_dkey_cnt(struct ioreq *req)
{
	daos_anchor_t	anchor = { 0 };
	int		total = 0;
	char		buf[512];
	daos_size_t	buf_len = 512;
	int		rc;

	while (!daos_anchor_is_eof(&anchor)) {
		daos_key_desc_t kds[10];
		uint32_t number = 10;

		memset(buf, 0, buf_len);
		rc = enumerate_dkey(DAOS_TX_NONE, &number, kds, &anchor, buf,
				    buf_len, req);
		assert_rc_equal(rc, 0);
		total += number;
	}

	return total;
}

static void
ec_dkey_list_punch(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	int		num_dkey;
	int		i;

	if (!test_runable(arg, 6))
		return;

	oid = daos_test_oid_gen(arg->coh, ec_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 100; i++) {
		char dkey[32];
		daos_recx_t recx;
		char data[16];

		/* Make dkey on different shards */
		req.iod_type = DAOS_IOD_ARRAY;
		sprintf(dkey, "dkey_%d", i);
		recx.rx_nr = 5;
		recx.rx_idx = i * 1048576;
		memset(data, 'a', 16);
		insert_recxs(dkey, "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, 16, &req);
	}

	num_dkey = get_dkey_cnt(&req);
	assert_int_equal(num_dkey, 100);

	daos_fail_loc_set(DAOS_OBJ_SKIP_PARITY | DAOS_FAIL_ALWAYS);
	num_dkey = get_dkey_cnt(&req);
	assert_int_equal(num_dkey, 100);
	daos_fail_loc_set(0);

	/* punch the dkey */
	for (i = 0; i < 100; i++) {
		char dkey[32];

		/* Make dkey on different shards */
		sprintf(dkey, "dkey_%d", i);
		punch_dkey(dkey, DAOS_TX_NONE, &req);
		if (i % 10 == 0) {
			num_dkey = get_dkey_cnt(&req);
			assert_int_equal(num_dkey, 100 - i - 1);

			daos_fail_loc_set(DAOS_OBJ_SKIP_PARITY |
					  DAOS_FAIL_ALWAYS);
			num_dkey = get_dkey_cnt(&req);
			assert_int_equal(num_dkey, 100 - i - 1);
			daos_fail_loc_set(0);
		}
	}

	num_dkey = get_dkey_cnt(&req);
	assert_int_equal(num_dkey, 0);
	daos_fail_loc_set(DAOS_OBJ_SKIP_PARITY | DAOS_FAIL_ALWAYS);
	num_dkey = get_dkey_cnt(&req);
	assert_int_equal(num_dkey, 0);
	daos_fail_loc_set(0);

	ioreq_fini(&req);
}

static int
get_akey_cnt(struct ioreq *req, char *dkey)
{
	daos_anchor_t	anchor = { 0 };
	int		total = 0;
	char		buf[512];
	daos_size_t	buf_len = 512;
	int		rc;

	while (!daos_anchor_is_eof(&anchor)) {
		daos_key_desc_t kds[10];
		uint32_t number = 10;

		memset(buf, 0, buf_len);
		rc = enumerate_akey(DAOS_TX_NONE, dkey, &number, kds,
				    &anchor, buf, buf_len, req);
		assert_rc_equal(rc, 0);
		total += number;
	}

	return total;
}

static void
ec_akey_list_punch(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	int		num_akey;
	int		i;

	if (!test_runable(arg, 6))
		return;

	oid = daos_test_oid_gen(arg->coh, ec_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 100; i++) {
		char akey[32];
		daos_recx_t recx;
		char data[16];

		/* Make dkey on different shards */
		req.iod_type = DAOS_IOD_ARRAY;
		sprintf(akey, "akey_%d", i);
		recx.rx_nr = 5;
		recx.rx_idx = i * 1048576;
		memset(data, 'a', 16);
		insert_recxs("d_key", akey, 1, DAOS_TX_NONE, &recx, 1,
			     data, 16, &req);
	}

	num_akey = get_akey_cnt(&req, "d_key");
	assert_int_equal(num_akey, 100);

	daos_fail_loc_set(DAOS_OBJ_SKIP_PARITY | DAOS_FAIL_ALWAYS);
	num_akey = get_akey_cnt(&req, "d_key");
	assert_int_equal(num_akey, 100);

	/* punch the akey */
	for (i = 0; i < 100; i++) {
		char akey[32];

		/* Make dkey on different shards */
		sprintf(akey, "akey_%d", i);
		punch_akey("d_key", akey, DAOS_TX_NONE, &req);
		if (i % 10 == 0) {
			num_akey = get_akey_cnt(&req, "d_key");
			assert_int_equal(num_akey, 100 - i - 1);
			daos_fail_loc_set(DAOS_OBJ_SKIP_PARITY |
					  DAOS_FAIL_ALWAYS);
			num_akey = get_akey_cnt(&req, "d_key");
			assert_int_equal(num_akey, 100 - i - 1);
			daos_fail_loc_set(0);
		}
	}

	num_akey = get_akey_cnt(&req, "d_key");
	assert_int_equal(num_akey, 0);

	daos_fail_loc_set(DAOS_OBJ_SKIP_PARITY | DAOS_FAIL_ALWAYS);
	num_akey = get_akey_cnt(&req, "d_key");
	assert_int_equal(num_akey, 0);
	ioreq_fini(&req);
}

static int
get_rec_cnt(struct ioreq *req, char *dkey, char *akey, int start)
{
	daos_anchor_t	anchor = { 0 };
	int		total = 0;
	daos_size_t	size;
	int		idx = start;

	while (!daos_anchor_is_eof(&anchor)) {
		daos_recx_t	   recxs[10];
		daos_epoch_range_t eprs[10];
		uint32_t	   number = 10;
		int		   i;

		enumerate_rec(DAOS_TX_NONE, dkey, akey, &size,
			      &number, recxs, eprs, &anchor, true, req);
		total += number;
		for (i = 0; i < number; i++, idx++) {
			assert_int_equal((int)recxs[i].rx_idx, idx * 1048576);
			assert_int_equal((int)recxs[i].rx_nr, 5);
		}

	}

	return total;
}

static void
ec_rec_list_punch(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	int		num_rec;
	int		i;

	if (!test_runable(arg, 6))
		return;

	oid = daos_test_oid_gen(arg->coh, ec_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 100; i++) {
		daos_recx_t recx;
		char data[16];

		/* Make dkey on different shards */
		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = 5;
		recx.rx_idx = i * 1048576;
		memset(data, 'a', 16);
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, 16, &req);
	}

	num_rec = get_rec_cnt(&req, "d_key", "a_key", 0);
	assert_int_equal(num_rec, 100);

	daos_fail_loc_set(DAOS_OBJ_SKIP_PARITY | DAOS_FAIL_ALWAYS);
	num_rec = get_rec_cnt(&req, "d_key", "a_key", 0);
	assert_int_equal(num_rec, 100);

	/* punch the akey */
	for (i = 0; i < 100; i++) {
		daos_recx_t recx;

		recx.rx_nr = 5;
		recx.rx_idx = i * 1048576;

		punch_recxs("d_key", "a_key", &recx, 1, DAOS_TX_NONE, &req);
		if (i % 10 == 0) {
			num_rec = get_rec_cnt(&req, "d_key", "a_key", i + 1);
			assert_int_equal(num_rec, 100 - i - 1);

			daos_fail_loc_set(DAOS_OBJ_SKIP_PARITY |
					  DAOS_FAIL_ALWAYS);
			num_rec = get_rec_cnt(&req, "d_key", "a_key", i + 1);
			assert_int_equal(num_rec, 100 - i - 1);
			daos_fail_loc_set(0);
		}
	}

	num_rec = get_rec_cnt(&req, "d_key", "a_key", 100);
	assert_int_equal(num_rec, 0);

	daos_fail_loc_set(DAOS_OBJ_SKIP_PARITY | DAOS_FAIL_ALWAYS);
	num_rec = get_rec_cnt(&req, "d_key", "a_key", 100);
	assert_int_equal(num_rec, 0);

	ioreq_fini(&req);
}

static void
ec_agg_check_replica_on_parity(test_arg_t *arg, daos_obj_id_t oid, char *dkey,
			       char *akey, daos_off_t offset, daos_size_t size,
			       bool exist)
{
	d_sg_list_t	sgl;
	d_iov_t		sg_iov;
	d_iov_t		dkey_iov;
	daos_iod_t	iod = { 0 };
	daos_recx_t	recx;
	daos_handle_t	oh;
	char		*buf;
	struct daos_oclass_attr *oca;
	uint64_t	shard;
	int		rc;

	rc = daos_obj_open(arg->coh, oid, 0, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey_iov, dkey, strlen(dkey));

	/** init scatter/gather */
	buf = (char *)malloc(size);
	assert_true(buf != NULL);
	d_iov_set(&sg_iov, buf, size);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &sg_iov;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, akey, strlen(akey));
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx.rx_idx	= offset;
	recx.rx_nr	= size;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	assert_true(oid_is_ec(oid, &oca));
	for (shard = oca->u.ec.e_k; shard < oca->u.ec.e_k + oca->u.ec.e_p;
	     shard++) {
		tse_task_t	*task = NULL;

		iod.iod_size = 1;
		rc = dc_obj_fetch_task_create(oh, DAOS_TX_NONE, 0,
					      &dkey_iov, 1, DIOF_TO_SPEC_SHARD,
					      &iod, &sgl, NULL, &shard,
					      NULL, NULL, NULL, &task);
		assert_rc_equal(rc, 0);
		rc = dc_task_schedule(task, true);
		assert_rc_equal(rc, 0);
		if (!exist)
			assert_int_equal(iod.iod_size, 0);
		else
			assert_int_not_equal(iod.iod_size, 0);
	}
	free(buf);
	daos_obj_close(oh, NULL);
}

void
trigger_and_wait_ec_aggreation(test_arg_t *arg, daos_obj_id_t *oids,
			       int oids_nr, char *dkey, char *akey,
			       daos_off_t offset, daos_size_t size,
			       uint64_t fail_loc)
{
	d_rank_t  ec_agg_ranks[10];
	int i;

	for (i = 0; i < oids_nr; i++) {
		struct daos_oclass_attr *oca;
		int parity_nr;
		int j;

		assert_true(oid_is_ec(oids[i], &oca));
		parity_nr = oca->u.ec.e_p;
		assert_true(parity_nr < 10);

		get_killing_rank_by_oid(arg, oids[i], 0, parity_nr,
					ec_agg_ranks, NULL);
		for (j = 0; j < parity_nr; j++)
			daos_debug_set_params(arg->group, ec_agg_ranks[j],
					      DMG_KEY_FAIL_LOC,
					      fail_loc | DAOS_FAIL_ALWAYS,
					      0, NULL);
	}

	print_message("wait for 20 seconds for EC aggregation.\n");
	sleep(20);

	for (i = 0; i < oids_nr; i++) {
		struct daos_oclass_attr *oca;
		int parity_nr;
		int j;

		if (size > 0 && fail_loc == DAOS_FORCE_EC_AGG)
			ec_agg_check_replica_on_parity(arg, oids[i], dkey,
						       akey, offset, size,
						       false);
		assert_true(oid_is_ec(oids[i], &oca));
		parity_nr = oca->u.ec.e_p;
		assert_true(parity_nr < 10);

		get_killing_rank_by_oid(arg, oids[i], 0, parity_nr,
					ec_agg_ranks, NULL);
		for (j = 0; j < parity_nr; j++)
			daos_debug_set_params(arg->group, ec_agg_ranks[j],
					      DMG_KEY_FAIL_LOC, 0, 0, NULL);
	}
}

void
ec_verify_parity_data(struct ioreq *req, char *dkey, char *akey,
		      daos_off_t offset, daos_size_t size,
		      char *verify_data, daos_handle_t th)
{
	daos_recx_t	recx;
	char		*data;

	data = (char *)malloc(size);
	assert_true(data != NULL);
	memset(data, 0, size);

	req->iod_type = DAOS_IOD_ARRAY;
	recx.rx_nr = size;
	recx.rx_idx = offset;
	daos_fail_loc_set(DAOS_OBJ_FORCE_DEGRADE | DAOS_FAIL_ONCE);
	lookup_recxs(dkey, akey, 1, th, &recx, 1, data, size, req);
	assert_memory_equal(data, verify_data, size);
	daos_fail_loc_set(0);
	free(data);
}

#define EC_CELL_SIZE	1048576
static void
ec_partial_update_agg(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	int		i;
	char		*data;
	char		*verify_data;

	if (!test_runable(arg, 6))
		return;

	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "time");
	data = (char *)malloc(EC_CELL_SIZE);
	assert_true(data != NULL);
	verify_data = (char *)malloc(EC_CELL_SIZE);
	assert_true(verify_data != NULL);
	oid = daos_test_oid_gen(arg->coh, ec_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 10; i++) {
		daos_recx_t recx;

		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = EC_CELL_SIZE;
		recx.rx_idx = i * EC_CELL_SIZE;
		memset(data, 'a' + i, EC_CELL_SIZE);
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, EC_CELL_SIZE, &req);
	}

	trigger_and_wait_ec_aggreation(arg, &oid, 1, "d_key", "a_key", 0,
				       EC_CELL_SIZE * 8, DAOS_FORCE_EC_AGG);

	for (i = 0; i < 10; i++) {
		daos_off_t offset = i * EC_CELL_SIZE;

		memset(verify_data, 'a' + i, EC_CELL_SIZE);
		ec_verify_parity_data(&req, "d_key", "a_key", offset,
				      (daos_size_t)EC_CELL_SIZE, verify_data,
				      DAOS_TX_NONE);
	}
	ioreq_fini(&req);
	free(data);
	free(verify_data);
}

static void
ec_cross_cell_partial_update_agg(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	int		i;
	char		*data;
	char		*verify_data;
	daos_size_t	update_size = 500000;

	if (!test_runable(arg, 6))
		return;

	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "time");
	data = (char *)malloc(update_size);
	assert_true(data != NULL);
	verify_data = (char *)malloc(update_size);
	assert_true(verify_data != NULL);
	oid = daos_test_oid_gen(arg->coh, ec_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 20; i++) {
		char		c = 'a' + i;
		daos_off_t	offset = i * update_size;
		daos_recx_t	recx;

		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = update_size;
		recx.rx_idx = offset;
		memset(data, c, update_size);
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, update_size, &req);
	}

	trigger_and_wait_ec_aggreation(arg, &oid, 1, "d_key", "a_key", 0,
				       EC_CELL_SIZE * 8, DAOS_FORCE_EC_AGG);
	for (i = 0; i < 20; i++) {
		char		c = 'a' + i;
		daos_off_t offset = i * update_size;

		memset(verify_data, c, update_size);
		ec_verify_parity_data(&req, "d_key", "a_key", offset,
				      update_size, verify_data, DAOS_TX_NONE);
	}

	ioreq_fini(&req);
	free(data);
	free(verify_data);
}

static void
ec_full_partial_update_agg(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	daos_recx_t	recx;
	int		i;
	char		*data;
	char		*verify_data;
	int		data_nr;
	daos_size_t	full_update_size;
	daos_size_t	partial_update_size;

	if (!test_runable(arg, 6))
		return;

	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "time");
	oid = daos_test_oid_gen(arg->coh, ec_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	data_nr = ec_data_nr_get(oid);
	full_update_size = 3 * ((daos_size_t)data_nr) * EC_CELL_SIZE;
	partial_update_size = EC_CELL_SIZE / 2;

	data = (char *)malloc(full_update_size);
	assert_true(data != NULL);
	verify_data = (char *)malloc(full_update_size);
	assert_true(verify_data != NULL);

	/* 3 full stripes update */
	req.iod_type = DAOS_IOD_ARRAY;
	recx.rx_nr = full_update_size;
	recx.rx_idx =  0;
	memset(data, 'a', full_update_size);
	memcpy(verify_data, data, full_update_size);
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
		     data, full_update_size, &req);

	/* then partial stripe update */
	for (i = 0; i < 12; i++) {
		char *buffer = data + i * EC_CELL_SIZE;
		char *verify_buffer = verify_data + i * EC_CELL_SIZE;

		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = partial_update_size;
		recx.rx_idx = i * EC_CELL_SIZE;

		memset(buffer, 'a' + i, partial_update_size);
		memcpy(verify_buffer, buffer, partial_update_size);

		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     buffer, partial_update_size, &req);
	}

	trigger_and_wait_ec_aggreation(arg, &oid, 1, "d_key", "a_key", 0,
				       full_update_size, DAOS_FORCE_EC_AGG);

	ec_verify_parity_data(&req, "d_key", "a_key", (daos_size_t)0,
			      full_update_size, verify_data, DAOS_TX_NONE);
	free(data);
	free(verify_data);
}

static void
ec_partial_full_update_agg(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	daos_recx_t	recx;
	int		i;
	char		*data;
	char		*verify_data;
	int		data_nr;
	daos_size_t	full_update_size;
	daos_size_t	partial_update_size;

	if (!test_runable(arg, 6))
		return;

	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "time");
	oid = daos_test_oid_gen(arg->coh, ec_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	data_nr = ec_data_nr_get(oid);
	full_update_size = 3 * ((daos_size_t)data_nr) * EC_CELL_SIZE;
	partial_update_size = EC_CELL_SIZE / 2;

	data = (char *)malloc(full_update_size);
	assert_true(data != NULL);
	verify_data = (char *)malloc(full_update_size);
	assert_true(verify_data != NULL);

	/* partial stripe update */
	for (i = 0; i < 12; i++) {
		char *buffer = data + i * EC_CELL_SIZE;
		char *verify_buffer = verify_data + i * EC_CELL_SIZE;

		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = partial_update_size;
		recx.rx_idx = i * EC_CELL_SIZE;

		memset(buffer, 'a' + i, partial_update_size);
		memcpy(verify_buffer, buffer, partial_update_size);

		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     buffer, partial_update_size, &req);
	}

	/* then full stripes update */
	req.iod_type = DAOS_IOD_ARRAY;
	recx.rx_nr = full_update_size;
	recx.rx_idx =  0;
	memset(data, 'a', full_update_size);
	memcpy(verify_data, data, full_update_size);
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
		     data, full_update_size, &req);

	trigger_and_wait_ec_aggreation(arg, &oid, 1, "d_key", "a_key", 0,
				       full_update_size, DAOS_FORCE_EC_AGG);

	ec_verify_parity_data(&req, "d_key", "a_key", (daos_size_t)0,
			      full_update_size, verify_data, DAOS_TX_NONE);

	ioreq_fini(&req);
	free(data);
	free(verify_data);
}

void
dfs_ec_check_size_internal(void **state, unsigned fail_loc)
{
	dfs_t		*dfs_mt;
	daos_handle_t	co_hdl;
	test_arg_t	*arg = *state;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	dfs_obj_t	*obj;
	daos_size_t	buf_size = 10 * 1024;
	daos_size_t	chunk_size = 32 * 1024 * 4;
	uuid_t		co_uuid;
	char		filename[32];
	struct stat	st;
	char		*buf;
	int		i;
	daos_obj_id_t	oid;
	int		rc;

	uuid_generate(co_uuid);
	rc = dfs_cont_create(arg->pool.poh, co_uuid, NULL, &co_hdl, &dfs_mt);
	assert_int_equal(rc, 0);
	printf("Created DFS Container "DF_UUIDF"\n", DP_UUID(co_uuid));

	D_ALLOC(buf, buf_size);
	assert_true(buf != NULL);

	sprintf(filename, "ec_file");
	rc = dfs_open(dfs_mt, NULL, filename, S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, DAOS_OC_EC_K4P2_L32K, chunk_size,
		      NULL, &obj);
	assert_int_equal(rc, 0);

	dfs_obj2id(obj, &oid);
	d_iov_set(&iov, buf, buf_size);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = &iov;

	for (i = 0; i < 30; i++) {
		rc = dfs_write(dfs_mt, obj, &sgl, i * buf_size, NULL);
		assert_int_equal(rc, 0);
		daos_fail_loc_set(fail_loc);
		/* Size query for EC object actually does client dispatch
		 * if the parity(dtx leader) are gone, so it meet uncommitted
		 * DTX, size query might give incorrect size, and we do not
		 * do retry for this case at the moment. So let's do obj_verify
		 * to do dtx sync to avoid uncommitted DTX for this test case.
		 */
		daos_obj_verify(co_hdl, oid, DAOS_EPOCH_MAX);
		rc = dfs_stat(dfs_mt, NULL, filename, &st);
		assert_int_equal(rc, 0);
		assert_int_equal(st.st_size, (i + 1) * buf_size);
		daos_fail_loc_set(0);
	}

	daos_fail_loc_set(fail_loc);

	rc = dfs_stat(dfs_mt, NULL, filename, &st);
	assert_int_equal(rc, 0);
	assert_int_equal(st.st_size, i * buf_size);
	for (i = 0; i < 10; i++) {
		daos_fail_loc_set(0);
		rc = dfs_punch(dfs_mt, obj, (daos_off_t)(i * buf_size),
			       (daos_size_t)buf_size);
		assert_int_equal(rc, 0);
		daos_fail_loc_set(fail_loc);
		daos_obj_verify(co_hdl, oid, DAOS_EPOCH_MAX);
		rc = dfs_stat(dfs_mt, NULL, filename, &st);
		assert_int_equal(rc, 0);
		assert_int_equal(st.st_size, 30 * buf_size);
	}

	for (i = 30; i > 10; i--) {
		daos_fail_loc_set(0);
		rc = dfs_punch(dfs_mt, obj, (daos_off_t)((i - 1) * buf_size),
			       (daos_size_t)buf_size);
		assert_int_equal(rc, 0);
		daos_fail_loc_set(fail_loc);
		daos_obj_verify(co_hdl, oid, DAOS_EPOCH_MAX);
		rc = dfs_stat(dfs_mt, NULL, filename, &st);
		assert_int_equal(rc, 0);
		assert_int_equal(st.st_size, (i - 1) * buf_size);
	}

	rc = dfs_stat(dfs_mt, NULL, filename, &st);
	assert_int_equal(rc, 0);
	/**
	 * NB: the last dfs_punch will set the file size to 10 * buf_size,
	 * instead of 0
	 **/
	assert_int_equal(st.st_size, 10 * buf_size);

	rc = dfs_stat(dfs_mt, NULL, filename, &st);
	daos_fail_loc_set(0);

	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	D_FREE(buf);
	rc = dfs_umount(dfs_mt);
	assert_int_equal(rc, 0);

	rc = daos_cont_close(co_hdl, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_cont_destroy(arg->pool.poh, co_uuid, 1, NULL);
	assert_rc_equal(rc, 0);
}

static void
dfs_ec_check_size(void **state)
{
	dfs_ec_check_size_internal(state, 0);
}

static void
dfs_ec_check_size_nonparity(void **state)
{
	dfs_ec_check_size_internal(state, DAOS_OBJ_SKIP_PARITY |
					  DAOS_FAIL_ALWAYS);
}

static void
ec_fail_agg_internal(void **state, unsigned fail_loc)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	int		i;
	char		*data;
	char		*verify_data;

	if (!test_runable(arg, 6))
		return;

	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "time");
	data = (char *)malloc(EC_CELL_SIZE);
	assert_true(data != NULL);
	verify_data = (char *)malloc(EC_CELL_SIZE);
	assert_true(verify_data != NULL);
	oid = daos_test_oid_gen(arg->coh, ec_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 10; i++) {
		daos_recx_t recx;

		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = EC_CELL_SIZE;
		recx.rx_idx = i * EC_CELL_SIZE;
		memset(data, 'a' + i, EC_CELL_SIZE);
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, EC_CELL_SIZE, &req);
	}

	/* fail the aggregation */
	trigger_and_wait_ec_aggreation(arg, &oid, 1, "d_key", "a_key", 0,
				       EC_CELL_SIZE * 8, fail_loc);

	for (i = 0; i < 10; i++) {
		daos_off_t offset = i * EC_CELL_SIZE;

		memset(verify_data, 'a' + i, EC_CELL_SIZE);
		ec_verify_parity_data(&req, "d_key", "a_key", offset,
				      (daos_size_t)EC_CELL_SIZE, verify_data,
				      DAOS_TX_NONE);
	}
	ioreq_fini(&req);
	free(data);
	free(verify_data);
}

static void
ec_agg_fail(void **state)
{
	ec_fail_agg_internal(state, DAOS_FORCE_EC_AGG_FAIL);
}

static void
ec_agg_peer_fail(void **state)
{
	ec_fail_agg_internal(state, DAOS_FORCE_EC_AGG_PEER_FAIL);
}

static void
ec_singv_array_mixed_io(void **state)
{
#define NUM_AKEYS 6
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_sg_list_t	 sgl[NUM_AKEYS];
	d_iov_t	 sg_iov[NUM_AKEYS];
	daos_iod_t	 iod[NUM_AKEYS];
	daos_recx_t	 recx[NUM_AKEYS];
	char		*buf[NUM_AKEYS];
	char		*akey[NUM_AKEYS];
	const char	*akey_fmt = "akey%d";
	int		 i, rc;
	daos_size_t	 size = 131071;

	if (!test_runable(arg, 6))
		return;

	/** open object */
	oid = daos_test_oid_gen(arg->coh, ec_obj_class, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, 0, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	for (i = 0; i < NUM_AKEYS; i++) {
		D_ALLOC(akey[i], strlen(akey_fmt) + 1);
		sprintf(akey[i], akey_fmt, i);

		D_ALLOC(buf[i], size * (i + 1));
		assert_non_null(buf[i]);

		dts_buf_render(buf[i], size * (i + 1));

		/** init scatter/gather */
		d_iov_set(&sg_iov[i], buf[i], size * (i + 1));
		sgl[i].sg_nr		= 1;
		sgl[i].sg_nr_out	= 0;
		sgl[i].sg_iovs		= &sg_iov[i];

		/** init I/O descriptor */
		d_iov_set(&iod[i].iod_name, akey[i], strlen(akey[i]));
		iod[i].iod_nr		= 1;
		if (i % 2 == 0) {
			iod[i].iod_size		= size * (i + 1);
			iod[i].iod_recxs	= NULL;
			iod[i].iod_type		= DAOS_IOD_SINGLE;
		} else {
			iod[i].iod_size		= 1;
			recx[i].rx_idx		= 0;
			recx[i].rx_nr		= size * (i + 1);
			iod[i].iod_recxs	= &recx[i];
			iod[i].iod_type		= DAOS_IOD_ARRAY;
		}
	}

	/** update record */
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, NUM_AKEYS, iod, sgl,
			     NULL);
	assert_rc_equal(rc, 0);

	/** fetch record size */
	for (i = 0; i < NUM_AKEYS; i++)
		iod[i].iod_size	= DAOS_REC_ANY;

	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, NUM_AKEYS, iod, NULL,
			    NULL, NULL);
	assert_rc_equal(rc, 0);
	for (i = 0; i < NUM_AKEYS; i++) {
		if (i % 2 == 0)
			assert_int_equal(iod[i].iod_size, size * (i + 1));
		else
			assert_int_equal(iod[i].iod_size, 1);
	}

	for (i = 0; i < NUM_AKEYS; i++)
		d_iov_set(&sg_iov[i], buf[i], size * (i + 1));
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, NUM_AKEYS, iod, sgl,
			    NULL, NULL);
	assert_rc_equal(rc, 0);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < NUM_AKEYS; i++) {
		D_FREE(akey[i]);
		D_FREE(buf[i]);
	}
}

#define SNAP_CNT 5
static void
ec_full_stripe_snapshot(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	daos_epoch_t	snap_epoch[SNAP_CNT];
	daos_size_t	stripe_size;
	char		*data;
	char		*verify_data;
	int		i;

	if (!test_runable(arg, 6))
		return;

	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "time");
	oid = daos_test_oid_gen(arg->coh, ec_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	stripe_size = ec_data_nr_get(oid) * EC_CELL_SIZE;
	data = (char *)malloc(stripe_size);
	assert_true(data != NULL);
	verify_data = (char *)malloc(stripe_size);
	assert_true(verify_data != NULL);

	for (i = 0; i < SNAP_CNT; i++) {
		daos_recx_t recx;

		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = stripe_size;
		recx.rx_idx = 0;
		memset(data, 'a' + i, stripe_size);
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, stripe_size, &req);
		daos_cont_create_snap(arg->coh, &snap_epoch[i], NULL, NULL);
	}

	for (i = 0; i < SNAP_CNT; i++) {
		daos_handle_t	th_open;

		daos_tx_open_snap(arg->coh, snap_epoch[i], &th_open, NULL);
		memset(verify_data, 'a' + i, stripe_size);
		ec_verify_parity_data(&req, "d_key", "a_key", 0, stripe_size,
				      verify_data, th_open);
		daos_tx_close(th_open, NULL);
	}

	ioreq_fini(&req);
}

static void
ec_partial_stripe_snapshot_internal(void **state, int data_size)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	daos_epoch_t	snap_epoch[SNAP_CNT];
	daos_size_t	stripe_size;
	char		*data;
	char		*verify_data;
	int		i;

	if (!test_runable(arg, 6))
		return;

	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "time");
	oid = daos_test_oid_gen(arg->coh, ec_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	stripe_size = ec_data_nr_get(oid) * data_size;
	data = (char *)malloc(stripe_size);
	assert_true(data != NULL);
	verify_data = (char *)malloc(stripe_size);
	assert_true(verify_data != NULL);

	for (i = 0; i < SNAP_CNT; i++) {
		daos_recx_t recx;
		int	    j;

		req.iod_type = DAOS_IOD_ARRAY;

		for (j = 0; j < ec_data_nr_get(oid); j++) {
			recx.rx_nr = data_size;
			recx.rx_idx = j * data_size;
			memset(data, 'a' + i, stripe_size);
			insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx,
				     1, data, stripe_size, &req);
		}
		daos_cont_create_snap(arg->coh, &snap_epoch[i], NULL, NULL);
	}

	trigger_and_wait_ec_aggreation(arg, &oid, 1, "d_key", "a_key", 0,
				       ec_data_nr_get(oid) * EC_CELL_SIZE,
				       DAOS_FORCE_EC_AGG);
	for (i = 0; i < SNAP_CNT; i++) {
		daos_handle_t	th_open;

		daos_tx_open_snap(arg->coh, snap_epoch[i], &th_open, NULL);
		memset(verify_data, 'a' + i, stripe_size);
		ec_verify_parity_data(&req, "d_key", "a_key", 0, stripe_size,
				      verify_data, th_open);
		daos_tx_close(th_open, NULL);
	}

	free(data);
	free(verify_data);
	ioreq_fini(&req);
}

static void
ec_partial_stripe_snapshot(void **state)
{
	return ec_partial_stripe_snapshot_internal(state, EC_CELL_SIZE);
}

static void
ec_partial_stripe_cross_boundry_snapshot(void **state)
{
	return ec_partial_stripe_snapshot_internal(state, EC_CELL_SIZE + 100);
}

void
ec_punch_check_size(void **state)
{
	dfs_t		*dfs_mt;
	daos_handle_t	co_hdl;
	test_arg_t	*arg = *state;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	dfs_obj_t	*obj;
	daos_size_t	buf_size = 256 * 1024;
	daos_size_t	chunk_size = 128 * 1024;
	uuid_t		co_uuid;
	char		filename[32];
	struct stat	st;
	char		*buf;
	int		i;
	daos_obj_id_t	oid;
	int		rc;

	uuid_generate(co_uuid);
	rc = dfs_cont_create(arg->pool.poh, co_uuid, NULL, &co_hdl, &dfs_mt);
	assert_int_equal(rc, 0);
	printf("Created DFS Container "DF_UUIDF"\n", DP_UUID(co_uuid));

	D_ALLOC(buf, buf_size);
	assert_true(buf != NULL);

	sprintf(filename, "ec_file");
	rc = dfs_open(dfs_mt, NULL, filename, S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, DAOS_OC_EC_K4P2_L32K, chunk_size,
		      NULL, &obj);
	assert_int_equal(rc, 0);

	dfs_obj2id(obj, &oid);
	d_iov_set(&iov, buf, buf_size);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = &iov;

	rc = dfs_write(dfs_mt, obj, &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	for (i = 130; i < 256; i++) {
		rc = dfs_punch(dfs_mt, obj, (daos_off_t)(i * 1024), (daos_size_t)1024);
		assert_int_equal(rc, 0);

		rc = dfs_stat(dfs_mt, NULL, filename, &st);
		assert_int_equal(rc, 0);
		if (i < 255)
			assert_int_equal(st.st_size, 256 * 1024);
		else
			assert_int_equal(st.st_size, 255 * 1024); /* The last punch */
	}

	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	sprintf(filename, "ec_file1");
	rc = dfs_open(dfs_mt, NULL, filename, S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, DAOS_OC_EC_K4P2_L32K, chunk_size,
		      NULL, &obj);
	assert_int_equal(rc, 0);

	dfs_obj2id(obj, &oid);
	d_iov_set(&iov, buf, buf_size/2);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = &iov;

	rc = dfs_write(dfs_mt, obj, &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	for (i = 0; i < 120; i++) {
		rc = dfs_punch(dfs_mt, obj, (daos_off_t)(((128 - i - 1) * 1024)),
			       (daos_size_t)1024);
		assert_int_equal(rc, 0);

		rc = dfs_stat(dfs_mt, NULL, filename, &st);
		assert_int_equal(rc, 0);
		assert_int_equal(st.st_size, (128 - i - 1) * 1024);
	}

	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	D_FREE(buf);
	rc = dfs_umount(dfs_mt);
	assert_int_equal(rc, 0);

	rc = daos_cont_close(co_hdl, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_cont_destroy(arg->pool.poh, co_uuid, 1, NULL);
	assert_rc_equal(rc, 0);
}

static int
ec_setup(void  **state)
{
	int rc;

	save_group_state(state);
	rc = test_setup(state, SETUP_CONT_CONNECT, true,
			DEFAULT_POOL_SIZE, 6, NULL);
	if (rc) {
		/* Let's skip for this case, since it is possible there
		 * is not enough ranks here.
		 */
		print_message("It can not create the pool with %d ranks"
			      " probably due to not enough ranks %d\n",
			      6, rc);
		return 0;
	}

	return 0;
}

/** create a new pool/container for each test */
static const struct CMUnitTest ec_tests[] = {
	{"EC0: ec dkey list and punch test",
	 ec_dkey_list_punch, async_disable, test_case_teardown},
	{"EC1: ec akey list and punch test",
	 ec_akey_list_punch, async_disable, test_case_teardown},
	{"EC2: ec rec list and punch test",
	 ec_rec_list_punch, async_disable, test_case_teardown},
	{"EC3: ec partial update then aggregation",
	 ec_partial_update_agg, async_disable, test_case_teardown},
	{"EC4: ec cross cell partial update then aggregation",
	 ec_cross_cell_partial_update_agg, async_disable, test_case_teardown},
	{"EC5: ec full and partial update then aggregation",
	 ec_full_partial_update_agg, async_disable, test_case_teardown},
	{"EC6: ec partial and full update then aggregation",
	 ec_partial_full_update_agg, async_disable, test_case_teardown},
	{"EC7: ec file size check on parity",
	 dfs_ec_check_size, async_disable, test_case_teardown},
	{"EC8: ec file size check on non-parity",
	 dfs_ec_check_size_nonparity, async_disable, test_case_teardown},
	{"EC9: ec aggregation failed",
	 ec_agg_fail, async_disable, test_case_teardown},
	{"EC10: ec aggregation peer update failed",
	 ec_agg_peer_fail, async_disable, test_case_teardown},
	{"EC11: ec single-value array mixed IO",
	 ec_singv_array_mixed_io, async_disable, test_case_teardown},
	{"EC12: ec full stripe snapshot",
	 ec_full_stripe_snapshot, async_disable, test_case_teardown},
	{"EC13: ec partial stripe snapshot",
	 ec_partial_stripe_snapshot, async_disable, test_case_teardown},
	{"EC14: ec partial stripe cross boundary snapshot",
	 ec_partial_stripe_cross_boundry_snapshot, async_disable,
	 test_case_teardown},
	{"EC15: ec punch and check_size", ec_punch_check_size, async_disable,
	 test_case_teardown},
};

int
run_daos_ec_io_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(ec_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests("DAOS_EC", ec_tests, ARRAY_SIZE(ec_tests),
				sub_tests, sub_tests_size, ec_setup,
				test_teardown);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
