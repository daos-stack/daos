/**
 * (C) Copyright 2016-2023 Intel Corporation.
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
unsigned int ec_cell_size = 32768;

static int
sort_cmp(void *array, int a, int b)
{
	char	**char_ptr_arr = array;

	return strcmp(char_ptr_arr[a], char_ptr_arr[b]);
}

static void
sort_swap(void *array, int a, int b)
{
	char	**char_ptr_arr = array;
	char	*tmp;

	tmp = char_ptr_arr[a];
	char_ptr_arr[a] = char_ptr_arr[b];
	char_ptr_arr[b] = tmp;
}

daos_sort_ops_t sort_ops = {
	.so_cmp		= sort_cmp,
	.so_swap	= sort_swap,
};

static void
get_dkeys(struct ioreq *req, int *num_dkey, int dkey_arr_len, char **dkey_arr)
{
	daos_anchor_t	anchor = { 0 };
	int		total = 0;
	char		buf[512];
	daos_size_t	buf_len = 512;
	int		rc;

	while (!daos_anchor_is_eof(&anchor)) {
		daos_key_desc_t	kds[10];
		uint32_t	number = 10;
		int		i;
		char		*buf_ptr = buf;

		memset(buf, 0, buf_len);
		memset(kds, 0, sizeof(*kds) * number);
		rc = enumerate_dkey(DAOS_TX_NONE, &number, kds, &anchor, buf, buf_len, req);
		assert_rc_equal(rc, 0);
		for (i = 0; i < number; i++) {
			assert_int_equal(kds[i].kd_val_type, OBJ_ITER_DKEY);
			if (dkey_arr) {
				assert_true(dkey_arr_len >= total + 1);
				D_ALLOC(dkey_arr[total], kds[i].kd_key_len + 1);
				assert_non_null(dkey_arr[total]);
				snprintf(dkey_arr[total], kds[i].kd_key_len + 1, "%s", buf_ptr);
				buf_ptr += kds[i].kd_key_len;
			}
			total++;
		}
	}

	if (dkey_arr)
		daos_array_sort(dkey_arr, total, false, &sort_ops);
	*num_dkey = total;
}

#define EC_CELL_SIZE	DAOS_EC_CELL_DEF

static void
ec_dkey_list_punch(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	int		num_dkey;
	int		num_dkey_create = 10000;
	char		*dkeys[10000];
	int		i;

	if (!test_runable(arg, 6))
		return;

	oid = daos_test_oid_gen(arg->coh, ec_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < num_dkey_create; i++) {
		char dkey[32];
		daos_recx_t recx;
		char data[16];

		/* Make dkey on different shards */
		req.iod_type = DAOS_IOD_ARRAY;
		sprintf(dkey, "dkey_%0*d", 5, i);
		recx.rx_nr = 5;
		recx.rx_idx = i * EC_CELL_SIZE;
		memset(data, 'a', 16);
		insert_recxs(dkey, "a_key", 1, DAOS_TX_NONE, &recx, 1, data, 16, &req);
	}

	/* verify total number of dkeys and dkey values */
	get_dkeys(&req, &num_dkey, num_dkey_create, dkeys);
	assert_int_equal(num_dkey, num_dkey_create);
	for (i = 0; i < num_dkey_create; i++) {
		char dkey[32];

		sprintf(dkey, "dkey_%0*d", 5, i);
		assert_string_equal(dkey, dkeys[i]);
		D_FREE(dkeys[i]);
	}

	/* verify just total number of dkeys */
	daos_fail_loc_set(DAOS_OBJ_SKIP_PARITY | DAOS_FAIL_ALWAYS);
	get_dkeys(&req, &num_dkey, 0, NULL);
	assert_int_equal(num_dkey, num_dkey_create);
	daos_fail_loc_set(0);

	/* punch the dkey */
	for (i = 0; i < num_dkey_create; i++) {
		char dkey[32];

		/* Make dkey on different shards */
		sprintf(dkey, "dkey_%0*d", 5, i);
		punch_dkey(dkey, DAOS_TX_NONE, &req);
		if (i % 1000 == 0) {
			get_dkeys(&req, &num_dkey, 0, NULL);
			assert_int_equal(num_dkey, num_dkey_create - i - 1);

			daos_fail_loc_set(DAOS_OBJ_SKIP_PARITY | DAOS_FAIL_ALWAYS);
			get_dkeys(&req, &num_dkey, 0, NULL);
			assert_int_equal(num_dkey, num_dkey_create - i - 1);
			daos_fail_loc_set(0);
		}
	}

	get_dkeys(&req, &num_dkey, 0, NULL);
	assert_int_equal(num_dkey, 0);
	daos_fail_loc_set(DAOS_OBJ_SKIP_PARITY | DAOS_FAIL_ALWAYS);
	get_dkeys(&req, &num_dkey, 0, NULL);
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
		int	i;

		memset(buf, 0, buf_len);
		rc = enumerate_akey(DAOS_TX_NONE, dkey, &number, kds,
				    &anchor, buf, buf_len, req);
		assert_rc_equal(rc, 0);
		total += number;
		for (i = 0; i < number; i++)
			assert_int_equal(kds[i].kd_val_type, OBJ_ITER_AKEY);
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
		recx.rx_idx = i * EC_CELL_SIZE;
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
			assert_int_equal((int)recxs[i].rx_idx,
					 idx * EC_CELL_SIZE);
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
		recx.rx_idx = i * EC_CELL_SIZE;
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
		recx.rx_idx = i * EC_CELL_SIZE;

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

#define ec_parity_rotate	1

uint32_t
test_ec_get_parity_off(daos_key_t *dkey, struct daos_oclass_attr *oca)
{
	uint64_t dkey_hash;
	uint32_t grp_size;

	grp_size = oca->u.ec.e_p + oca->u.ec.e_k;
	if (ec_parity_rotate)
		dkey_hash = d_hash_murmur64((unsigned char *)dkey->iov_buf, dkey->iov_len, 5731);
	else
		dkey_hash = 0;

	return (dkey_hash % grp_size + oca->u.ec.e_k) % grp_size;
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
	uint32_t	shard;
	uint32_t	p_shard_off;
	uint32_t	grp_size;
	int		i;
	int		rc;

	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
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

	grp_size = oca->u.ec.e_p + oca->u.ec.e_k;
	p_shard_off = test_ec_get_parity_off(&dkey_iov, oca);
	for (i = 0, shard = p_shard_off; i < oca->u.ec.e_p;
	     shard = (shard + 1) % grp_size, i++) {
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
	int i;

	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			      fail_loc | DAOS_FAIL_ALWAYS, 0, NULL);

	print_message("wait for 30 seconds for EC aggregation.\n");
	sleep(30);

	for (i = 0; i < oids_nr; i++) {
		if (size > 0 && fail_loc == DAOS_FORCE_EC_AGG)
			ec_agg_check_replica_on_parity(arg, oids[i], dkey,
						       akey, offset, size,
						       false);
	}

	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
}

void
ec_verify_parity_data(struct ioreq *req, char *dkey, char *akey,
		      daos_off_t offset, daos_size_t size,
		      char *verify_data, daos_handle_t th, bool degraded)
{
	daos_recx_t	recx;
	char		*data;

	data = (char *)malloc(size);
	assert_true(data != NULL);
	memset(data, 0, size);

	req->iod_type = DAOS_IOD_ARRAY;
	recx.rx_nr = size;
	recx.rx_idx = offset;
	if (degraded)
		daos_fail_loc_set(DAOS_OBJ_FORCE_DEGRADE | DAOS_FAIL_ONCE);

	lookup_recxs(dkey, akey, 1, th, &recx, 1, data, size, req);
	assert_memory_equal(data, verify_data, size);
	daos_fail_loc_set(0);
	free(data);
}

static void
ec_partial_update_agg(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	int		i;
	char		*data;
	char		*verify_data;

	FAULT_INJECTION_REQUIRED();

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
				      DAOS_TX_NONE, true);
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

	FAULT_INJECTION_REQUIRED();

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
				      update_size, verify_data, DAOS_TX_NONE, true);
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

	FAULT_INJECTION_REQUIRED();

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
			      full_update_size, verify_data, DAOS_TX_NONE, true);
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

	FAULT_INJECTION_REQUIRED();

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
			      full_update_size, verify_data, DAOS_TX_NONE, true);

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
	char		str[37];
	char		filename[32];
	struct stat	st;
	char		*buf;
	int		i;
	daos_obj_id_t	oid;
	int		rc;
	dfs_attr_t	attr = {};

	attr.da_props = daos_prop_alloc(2);
	assert_non_null(attr.da_props);
	attr.da_props->dpp_entries[0].dpe_type = DAOS_PROP_CO_EC_CELL_SZ;
	attr.da_props->dpp_entries[0].dpe_val = 1 << 15;
	attr.da_props->dpp_entries[1].dpe_type = DAOS_PROP_CO_REDUN_LVL;
	attr.da_props->dpp_entries[1].dpe_val = DAOS_PROP_CO_REDUN_RANK;
	rc = dfs_cont_create(arg->pool.poh, &co_uuid, &attr, &co_hdl, &dfs_mt);
	daos_prop_free(attr.da_props);

	assert_int_equal(rc, 0);
	printf("Created DFS Container "DF_UUIDF"\n", DP_UUID(co_uuid));

	D_ALLOC(buf, buf_size);
	assert_true(buf != NULL);

	sprintf(filename, "ec_file");
	rc = dfs_open(dfs_mt, NULL, filename, S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, OC_EC_4P2G1, chunk_size,
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

	uuid_unparse(co_uuid, str);
	rc = daos_cont_destroy(arg->pool.poh, str, 1, NULL);
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
				      DAOS_TX_NONE, true);
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
	d_sg_list_t	 sgl[NUM_AKEYS + 1];
	d_iov_t	 sg_iov[NUM_AKEYS];
	daos_iod_t	 iod[NUM_AKEYS + 1];
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
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
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
			if (i == 0)
				iod[i].iod_size = 5;
			else
				iod[i].iod_size	= size * (i + 1);
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
		if (i % 2 == 0) {
			if (i == 0)
				assert_int_equal(iod[i].iod_size, 5);
			else
				assert_int_equal(iod[i].iod_size, size * (i + 1));
		} else {
			assert_int_equal(iod[i].iod_size, 1);
		}
	}

	for (i = 0; i < NUM_AKEYS; i++)
		d_iov_set(&sg_iov[i], buf[i], size * (i + 1));

	/* one more akey fetch for non-exist single-value */
	d_iov_set(&iod[NUM_AKEYS].iod_name, "non_exist_akey_111", strlen("non_exist_akey_111"));
	iod[NUM_AKEYS].iod_recxs	= NULL;
	iod[NUM_AKEYS].iod_type		= DAOS_IOD_SINGLE;
	iod[NUM_AKEYS].iod_size		= 0;
	iod[NUM_AKEYS].iod_nr		= 1;
	sgl[NUM_AKEYS]			= sgl[NUM_AKEYS - 1];

	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, NUM_AKEYS + 1, iod, sgl,
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

#define SNAP_CNT 3
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
	stripe_size = ec_data_nr_get(oid) * (daos_size_t)EC_CELL_SIZE;
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
		daos_handle_t		th_open;
		daos_epoch_range_t	epr;
		int			rc;

		daos_tx_open_snap(arg->coh, snap_epoch[i], &th_open, NULL);
		memset(verify_data, 'a' + i, stripe_size);
		ec_verify_parity_data(&req, "d_key", "a_key", 0, stripe_size,
				      verify_data, th_open, false);
		daos_tx_close(th_open, NULL);

		epr.epr_hi = epr.epr_lo = snap_epoch[i];
		trigger_and_wait_ec_aggreation(arg, &oid, 1, "d_key", "a_key", 0,
					       stripe_size, DAOS_FORCE_EC_AGG);
		rc = daos_cont_destroy_snap(arg->coh, epr, NULL);
		assert_success(rc);
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

	FAULT_INJECTION_REQUIRED();

	if (!test_runable(arg, 6))
		return;

	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "time");
	oid = daos_test_oid_gen(arg->coh, ec_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	stripe_size = ec_data_nr_get(oid) * (daos_size_t)data_size;
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
		daos_handle_t		th_open;
		daos_epoch_range_t	epr;
		int			rc;

		daos_tx_open_snap(arg->coh, snap_epoch[i], &th_open, NULL);
		memset(verify_data, 'a' + i, stripe_size);
		ec_verify_parity_data(&req, "d_key", "a_key", 0, stripe_size,
				      verify_data, th_open, true);
		daos_tx_close(th_open, NULL);

		trigger_and_wait_ec_aggreation(arg, &oid, 1, "d_key", "a_key", 0,
					       ec_data_nr_get(oid) * EC_CELL_SIZE,
					       DAOS_FORCE_EC_AGG);
		epr.epr_hi = epr.epr_lo = snap_epoch[i];
		rc = daos_cont_destroy_snap(arg->coh, epr, NULL);
		assert_success(rc);
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
	char		str[37];
	char		filename[32];
	struct stat	st;
	char		*buf;
	int		i;
	daos_obj_id_t	oid;
	int		rc;
	dfs_attr_t	attr = {};

	attr.da_props = daos_prop_alloc(2);
	assert_non_null(attr.da_props);
	attr.da_props->dpp_entries[0].dpe_type = DAOS_PROP_CO_EC_CELL_SZ;
	attr.da_props->dpp_entries[0].dpe_val = 1 << 15;
	attr.da_props->dpp_entries[1].dpe_type = DAOS_PROP_CO_REDUN_LVL;
	attr.da_props->dpp_entries[1].dpe_val = DAOS_PROP_CO_REDUN_RANK;
	rc = dfs_cont_create(arg->pool.poh, &co_uuid, &attr, &co_hdl, &dfs_mt);
	daos_prop_free(attr.da_props);
	assert_int_equal(rc, 0);
	printf("Created DFS Container "DF_UUIDF"\n", DP_UUID(co_uuid));

	D_ALLOC(buf, buf_size);
	assert_true(buf != NULL);

	sprintf(filename, "ec_file");
	rc = dfs_open(dfs_mt, NULL, filename, S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, OC_EC_4P2G1, chunk_size,
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
		      O_RDWR | O_CREAT, OC_EC_4P2G1, chunk_size,
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

	uuid_unparse(co_uuid, str);
	rc = daos_cont_destroy(arg->pool.poh, str, 1, NULL);
	assert_rc_equal(rc, 0);
}

static void
ec_singv_overwrite_oc(void **state, unsigned int ec_oc)
{
#define NUM_AKEYS	6
#define SMALL_SIZE	32
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_sg_list_t	 sgl[NUM_AKEYS];
	d_iov_t	 sg_iov[NUM_AKEYS];
	daos_iod_t	 iod[NUM_AKEYS];
	char		*buf[NUM_AKEYS];
	char		*akey[NUM_AKEYS];
	char		*fetch_buf[NUM_AKEYS];
	const char	*akey_fmt = "akey%d";
	int		 i, rc;
	daos_size_t	 size;

	if (!test_runable(arg, 6))
		return;

	/** open object */
	oid = daos_test_oid_gen(arg->coh, ec_oc, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** update record */
	for (i = 0; i < NUM_AKEYS; i++) {
		D_ALLOC(akey[i], strlen(akey_fmt) + 1);
		sprintf(akey[i], akey_fmt, i);

		if (i % 2 == 0)
			size = SMALL_SIZE;
		else
			size = SMALL_SIZE + i * 8192;
		D_ALLOC(buf[i], size);
		assert_non_null(buf[i]);

		dts_buf_render(buf[i], size);

		/** init scatter/gather */
		d_iov_set(&sg_iov[i], buf[i], size);
		sgl[i].sg_nr		= 1;
		sgl[i].sg_nr_out	= 0;
		sgl[i].sg_iovs		= &sg_iov[i];

		/** init I/O descriptor */
		d_iov_set(&iod[i].iod_name, akey[i], strlen(akey[i]));
		iod[i].iod_nr		= 1;
		iod[i].iod_size		= size;
		iod[i].iod_recxs	= NULL;
		iod[i].iod_type		= DAOS_IOD_SINGLE;
	}
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, NUM_AKEYS, iod, sgl,
			     NULL);
	assert_rc_equal(rc, 0);
	for (i = 0; i < NUM_AKEYS; i++)
		D_FREE(buf[i]);

	/** overwrite record with different size (smaller or larger) */
	for (i = 0; i < NUM_AKEYS; i++) {
		D_ALLOC(akey[i], strlen(akey_fmt) + 1);
		sprintf(akey[i], akey_fmt, i);

		if (i % 2 == 1)
			size = SMALL_SIZE;
		else
			size = SMALL_SIZE + i * 8192;
		D_ALLOC(buf[i], size);
		assert_non_null(buf[i]);

		dts_buf_render(buf[i], size);

		/** init scatter/gather */
		d_iov_set(&sg_iov[i], buf[i], size);
		sgl[i].sg_nr		= 1;
		sgl[i].sg_nr_out	= 0;
		sgl[i].sg_iovs		= &sg_iov[i];

		/** init I/O descriptor */
		d_iov_set(&iod[i].iod_name, akey[i], strlen(akey[i]));
		iod[i].iod_nr		= 1;
		iod[i].iod_size		= size;
		iod[i].iod_recxs	= NULL;
		iod[i].iod_type		= DAOS_IOD_SINGLE;
	}
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
		if (i % 2 == 1)
			assert_int_equal(iod[i].iod_size, SMALL_SIZE);
		else
			assert_int_equal(iod[i].iod_size, SMALL_SIZE + i * 8192);
	}

	/** fetch record and compare */
	for (i = 0; i < NUM_AKEYS; i++) {
		if (i % 2 == 1)
			size = SMALL_SIZE;
		else
			size = SMALL_SIZE + i * 8192;

		D_ALLOC(fetch_buf[i], size);
		assert_non_null(fetch_buf[i]);
		d_iov_set(&sg_iov[i], fetch_buf[i], size);
	}

	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, NUM_AKEYS, iod, sgl,
			    NULL, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < NUM_AKEYS; i++) {
		if (i % 2 == 1)
			size = SMALL_SIZE;
		else
			size = SMALL_SIZE + i * 8192;
		assert_memory_equal(buf[i], fetch_buf[i], size);
	}

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < NUM_AKEYS; i++) {
		D_FREE(akey[i]);
		D_FREE(buf[i]);
		D_FREE(fetch_buf[i]);
	}
}

static void
ec_singv_overwrite(void **state)
{
	test_arg_t	*arg = *state;

	if (!test_runable(arg, 6))
		return;

	ec_singv_overwrite_oc(state, OC_EC_2P1G1);
	ec_singv_overwrite_oc(state, OC_EC_2P1G2);
	ec_singv_overwrite_oc(state, OC_EC_4P2G1);
}


static void
ec_singv_size_fetch_oc(void **state, unsigned int ec_oc, uint32_t old_len, uint32_t new_len)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_sg_list_t	 sgl;
	d_iov_t		 sg_iov;
	daos_iod_t	 iod;
	char		*buf;
	char		*akey = "akey_singv_test";
	char		*fetch_buf;
	uint32_t	 length;
	daos_size_t	 size;
	bool		 degraded_test = false;
	uint16_t	 fail_shards[2];
	uint64_t	 fail_val;
	struct daos_oclass_attr	*oca;
	int		 rc;

	/** open object */
	oid = daos_test_oid_gen(arg->coh, ec_oc, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);
	oca = daos_oclass_attr_find(oid, NULL);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** firstly update with old_len, then overwrite with new_len */
	size = old_len;
	D_ALLOC(buf, size);
	assert_non_null(buf);
	dts_buf_render(buf, size);
	d_iov_set(&sg_iov, buf, size);
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= &sg_iov;
	d_iov_set(&iod.iod_name, akey, strlen(akey));
	iod.iod_nr	= 1;
	iod.iod_size	= size;
	iod.iod_recxs	= NULL;
	iod.iod_type	= DAOS_IOD_SINGLE;
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	if (new_len != 0) {
		D_FREE(buf);
		size = new_len;
		D_ALLOC(buf, size);
		assert_non_null(buf);
		dts_buf_render(buf, size);
		d_iov_set(&sg_iov, buf, size);
		iod.iod_size	= size;
		rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
		assert_rc_equal(rc, 0);
		length = new_len;
	} else {
		length = old_len;
	}

deg_test:
	size = new_len != 0 ? new_len : old_len;
	if (degraded_test) {
		fail_shards[0] = 0;
		fail_shards[1] = 3;
		if (oca->u.ec.e_p > 1)
			fail_val = daos_shard_fail_value(fail_shards, 2);
		else
			fail_val = daos_shard_fail_value(fail_shards, 1);
		daos_fail_value_set(fail_val);
		daos_fail_loc_set(DAOS_FAIL_SHARD_OPEN | DAOS_FAIL_ALWAYS);
	}

	/** fetch record size */
	print_message("fetch iod_size with NULL sgl\n");
	iod.iod_size	= DAOS_REC_ANY;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, NULL, NULL, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(iod.iod_size, size);

	/** fetch with unknown size and matched buffer */
	size = length;
	D_ALLOC(fetch_buf, size);
	assert_non_null(fetch_buf);
	print_message("fetch with unknown size and matched buffer\n");
	d_iov_set(&sg_iov, fetch_buf, size);
	iod.iod_size	= DAOS_REC_ANY;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(iod.iod_size, length);
	assert_memory_equal(buf, fetch_buf, length);
	D_FREE(fetch_buf);

	/** fetch with larger buffer */
	size = length + 17 * 1024;
	D_ALLOC(fetch_buf, size);
	assert_non_null(fetch_buf);

	/* fetch with UNKNOWN iod_size */
	print_message("fetch with zero iod_size and larger buffer\n");
	d_iov_set(&sg_iov, fetch_buf, size);
	iod.iod_size	= DAOS_REC_ANY;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(iod.iod_size, length);
	assert_memory_equal(buf, fetch_buf, length);
	/* extra buffer should not be touched by the fetch, not required for now */
	/*
	 * for (i = 0; i < size - length; i++)
	 *	D_ASSERTF(fetch_buf[length + i] == 0, "fetch_buf[%d]: %d\n",
	 *		  length + i, fetch_buf[length + i]);
	 */
	memset(fetch_buf, 0, size);

	/* fetch with incorrect smaller iod_size */
	d_iov_set(&sg_iov, fetch_buf, size);
	iod.iod_size	= 7;
	print_message("fetch with smaller incorrect iod_size\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL, NULL);
	assert_rc_equal(rc, -DER_REC2BIG);
	assert_int_equal(iod.iod_size, length);
	memset(fetch_buf, 0, size);

	print_message("fetch with replied correct iod_size\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(iod.iod_size, length);
	assert_memory_equal(buf, fetch_buf, length);
	memset(fetch_buf, 0, size);

	/* fetch with incorrect larger iod_size */
	d_iov_set(&sg_iov, fetch_buf, size);
	iod.iod_size	= length + 111;
	print_message("fetch with larger incorrect iod_size\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(iod.iod_size, length);
	assert_memory_equal(buf, fetch_buf, length);
	memset(fetch_buf, 0, size);

	if (!degraded_test) {
		degraded_test = true;
		print_message("run same tests in degraded mode ...\n");
		goto deg_test;
	}

	daos_fail_value_set(0);
	daos_fail_loc_set(0);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	D_FREE(buf);
	D_FREE(fetch_buf);
}

static void
ec_singv_diff_size_fetch(void **state)
{
	test_arg_t	*arg = *state;

	if (!test_runable(arg, 6))
		return;

	print_message("test OC_EC_2P1G1, small singv 511B\n");
	ec_singv_size_fetch_oc(state, OC_EC_2P1G1, 511, 0);
	print_message("test OC_EC_2P1G1, long singv 16KB\n");
	ec_singv_size_fetch_oc(state, OC_EC_2P1G1, 16 * 1024, 0);
	print_message("test OC_EC_2P1G1, singv 4103B overwritten by 4095B\n");
	ec_singv_size_fetch_oc(state, OC_EC_2P1G1, 4103, 4095);
	print_message("test OC_EC_2P1G1, singv 16KB overwritten by 11B\n");
	ec_singv_size_fetch_oc(state, OC_EC_2P1G1, 16 * 1024, 11);
	print_message("test OC_EC_2P1G1, singv 133B overwritten by 17KB\n");
	ec_singv_size_fetch_oc(state, OC_EC_2P1G1, 133, 17 * 1024);

	print_message("test OC_EC_4P1G1, small singv 5750B overwritten by 2290B\n");
	ec_singv_size_fetch_oc(state, OC_EC_2P1G1, 5750, 2290);

	print_message("test OC_EC_4P2G1, small singv 127B\n");
	ec_singv_size_fetch_oc(state, OC_EC_4P2G1, 127, 0);
	print_message("test OC_EC_4P2G1, long singv 16389B\n");
	ec_singv_size_fetch_oc(state, OC_EC_4P2G1, 16389, 0);
	print_message("test OC_EC_4P2G1, singv 4103B overwritten by 4095B\n");
	ec_singv_size_fetch_oc(state, OC_EC_4P2G1, 4103, 4095);
	print_message("test OC_EC_4P2G1, singv 16KB overwritten by 11B\n");
	ec_singv_size_fetch_oc(state, OC_EC_4P2G1, 16 * 1024, 11);
	print_message("test OC_EC_4P2G1, singv 133B overwritten by 17KB\n");
	ec_singv_size_fetch_oc(state, OC_EC_4P2G1, 133, 17 * 1024);
	print_message("test OC_EC_4P2G1, singv 12KB overwritten by 4000B\n");
	ec_singv_size_fetch_oc(state, OC_EC_4P2G1, 12 * 1024, 4000);
}

static void
ec_cond_fetch(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_iov_t		 non_exist_dkey;
	d_sg_list_t	 sgl[2];
	d_iov_t		 sg_iov[2];
	daos_iod_t	 iod[2];
	daos_recx_t	 recx[2];
	char		*buf[2];
	char		*akey[2];
	const char	*akey_fmt = "akey%d";
	int		 i, rc;
	daos_size_t	 size = 8192;

	if (!test_runable(arg, 6))
		return;

	/** open object */
	oid = daos_test_oid_gen(arg->coh, ec_obj_class, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));
	d_iov_set(&non_exist_dkey, "non_dkey", strlen("non_dkey"));

	for (i = 0; i < 2; i++) {
		D_ALLOC(akey[i], strlen(akey_fmt) + 1);
		sprintf(akey[i], akey_fmt, i);

		D_ALLOC(buf[i], size);
		assert_non_null(buf[i]);

		dts_buf_render(buf[i], size);

		/** init scatter/gather */
		d_iov_set(&sg_iov[i], buf[i], size);
		sgl[i].sg_nr		= 1;
		sgl[i].sg_nr_out	= 0;
		sgl[i].sg_iovs		= &sg_iov[i];

		/** init I/O descriptor */
		d_iov_set(&iod[i].iod_name, akey[i], strlen(akey[i]));
		iod[i].iod_nr		= 1;
		iod[i].iod_size		= 1;
		iod[i].iod_recxs	= &recx[i];
		iod[i].iod_type		= DAOS_IOD_ARRAY;
		if (i == 0) {
			recx[i].rx_idx		= 0;
			recx[i].rx_nr		= size;
		} else {
			recx[i].rx_idx		= ec_cell_size;
			recx[i].rx_nr		= size;
		}
	}

	/** update record */
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 2, iod, sgl,
			     NULL);
	assert_rc_equal(rc, 0);

	/** fetch with NULL sgl but iod_size is non-zero */
	print_message("negative test - fetch with non-zero iod_size and NULL sgl\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, iod, NULL,
			    NULL, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	/** normal fetch */
	for (i = 0; i < 2; i++)
		iod[i].iod_size	= DAOS_REC_ANY;

	print_message("normal fetch\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 2, iod, NULL,
			    NULL, NULL);
	assert_rc_equal(rc, 0);
	for (i = 0; i < 2; i++)
		assert_int_equal(iod[i].iod_size, 1);

	for (i = 0; i < 2; i++)
		d_iov_set(&sg_iov[i], buf[i], size);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 2, iod, sgl,
			    NULL, NULL);
	assert_rc_equal(rc, 0);

	print_message("cond_deky, fetch non-exist dkey\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_DKEY_FETCH, &non_exist_dkey, 2, iod, sgl,
			    NULL, NULL);
	assert_rc_equal(rc, -DER_NONEXIST);

	print_message("cond_dkey, dkey exist, akey non-exist...\n");
	recx[0].rx_idx	= ec_cell_size;
	recx[0].rx_nr	= size;
	d_iov_set(&iod[0].iod_name, "non-akey", strlen("non-akey"));
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_DKEY_FETCH, &dkey, 1, iod, sgl, NULL, NULL);
	assert_rc_equal(rc, 0);

	print_message("cond_akey fetch, akey exist on another data shard...\n");
	d_iov_set(&iod[0].iod_name, akey[0], strlen(akey[0]));
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_AKEY_FETCH, &dkey, 1, iod, sgl, NULL, NULL);
	assert_rc_equal(rc, 0);

	recx[1].rx_idx	= 0;
	recx[1].rx_nr	= size;
	print_message("cond_akey fetch, check exist from parity shard\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_AKEY_FETCH, &dkey, 1, &iod[1], sgl, NULL,
			    NULL);
	assert_rc_equal(rc, 0);

	print_message("cond_akey fetch, check exist from all data shards\n");
	daos_fail_loc_set(DAOS_OBJ_SKIP_PARITY | DAOS_FAIL_ALWAYS);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_AKEY_FETCH, &dkey, 1, &iod[1], sgl, NULL,
			    NULL);
	assert_rc_equal(rc, 0);
	daos_fail_loc_set(0);

	print_message("cond_akey fetch, one akey exist and another akey non-exist\n");
	d_iov_set(&iod[1].iod_name, "non-akey", strlen("non-akey"));
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_AKEY_FETCH, &dkey, 2, iod, sgl, NULL, NULL);
	assert_rc_equal(rc, -DER_NONEXIST);

	print_message("cond fetch per akey, one akey exist and another akey non-exist\n");
	iod[0].iod_flags = DAOS_COND_AKEY_FETCH;
	iod[1].iod_flags = DAOS_COND_AKEY_FETCH;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_PER_AKEY, &dkey, 2, iod, sgl, NULL, NULL);
	assert_rc_equal(rc, -DER_NONEXIST);

	print_message("cond fetch per akey, two akeys both exist\n");
	recx[0].rx_idx	= 0;
	recx[0].rx_nr	= size;
	recx[1].rx_idx	= ec_cell_size;
	recx[1].rx_nr	= size;
	d_iov_set(&iod[0].iod_name, akey[0], strlen(akey[0]));
	d_iov_set(&iod[1].iod_name, akey[1], strlen(akey[1]));
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_PER_AKEY, &dkey, 2, iod, sgl, NULL, NULL);
	assert_rc_equal(rc, 0);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		D_FREE(akey[i]);
		D_FREE(buf[i]);
	}
}

static void
ec_data_recov(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_iov_t		 akey;
	d_sg_list_t	 sgl[2];
	d_iov_t		 sg_iov[2];
	char		*buf[2];
	daos_iod_t	 iod;
	daos_recx_t	 recx;
	int		 i, rc;
	daos_size_t	 size = ec_cell_size * 4;
	uint16_t	 shard[2];
	uint64_t	 fail_val;

	if (!test_runable(arg, 6))
		return;

	/** open object */
	oid = daos_test_oid_gen(arg->coh, ec_obj_class, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey_recov", strlen("dkey_recov"));
	d_iov_set(&akey, "akey_recov", strlen("akey_recov"));

	for (i = 0; i < 2; i++) {
		D_ALLOC(buf[i], size);
		assert_non_null(buf[i]);

		dts_buf_render(buf[i], size);

		/** init scatter/gather */
		d_iov_set(&sg_iov[i], buf[i], size);
		sgl[i].sg_nr		= 1;
		sgl[i].sg_nr_out	= 0;
		sgl[i].sg_iovs		= &sg_iov[i];
	}

	/** init I/O descriptor */
	iod.iod_name		= akey;
	iod.iod_nr		= 1;
	iod.iod_size		= 1;
	iod.iod_recxs		= &recx;
	iod.iod_type		= DAOS_IOD_ARRAY;
	recx.rx_idx		= 0;
	recx.rx_nr		= size;

	/** update record */
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl[0], NULL);
	assert_rc_equal(rc, 0);

	/** normal fetch */
	iod.iod_size	= DAOS_REC_ANY;

	print_message("normal fetch\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, NULL, NULL, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(iod.iod_size, 1);

	d_iov_set(&sg_iov[1], buf[1], size);
	sg_iov[1].iov_buf = buf[1];
	sg_iov[1].iov_len = 0;
	sg_iov[1].iov_buf_len = size;
	memset(buf[1], 0, size);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl[1], NULL, NULL);
	assert_rc_equal(rc, 0);
	assert_memory_equal(buf[0], buf[1], size);
	if (sg_iov[1].iov_len != size)
		fail_msg("sg_iov[1].iov_len %zu\n", sg_iov[1].iov_len);

	print_message("degraded fetch data recovery\n");
	shard[0] = 1;
	shard[1] = 3;
	fail_val = daos_shard_fail_value(shard, 2);
	daos_fail_loc_set(DAOS_FAIL_SHARD_OPEN | DAOS_FAIL_ALWAYS);
	daos_fail_value_set(fail_val);

	sg_iov[1].iov_len = 0;
	sg_iov[1].iov_buf_len = size;
	memset(buf[1], 0, size);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl[1], NULL, NULL);
	assert_rc_equal(rc, 0);
	assert_memory_equal(buf[0], buf[1], size);
	if (sg_iov[1].iov_len != size)
		fail_msg("sg_iov[1].iov_len %zu\n", sg_iov[1].iov_len);

	daos_fail_loc_set(0);
	daos_fail_value_set(0);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		D_FREE(buf[i]);
	}
}

static void
ec_multi_singv_overwrite(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_sg_list_t	 sgl[3];
	d_iov_t		 sg_iov[3];
	daos_iod_t	 iod[3];
	char		*buf[3];
	size_t		 len[3];
	char		*new_buf[3];
	size_t		 new_len[3];
	int		 i, rc;

	if (!test_runable(arg, 4))
		return;

	for (i = 0; i < 3; i++) {
		buf[i] = NULL;
		new_buf[i] = NULL;
		len[i] = 0;
		new_len[i] = 0;
	}

	len[0] = 5822;
	len[1] = 195;
	len[2] = 6162;
	new_len[0] = 733;
	new_len[1] = 559;
	new_len[2] = 294;

	for (i = 0; i < 3; i++) {
		D_ALLOC(buf[i], len[i]);
		assert_non_null(buf[i]);
		dts_buf_render(buf[i], len[i]);
		if (new_len[i] != 0) {
			D_ALLOC(new_buf[i], new_len[i]);
			assert_non_null(new_buf[i]);
			dts_buf_render(new_buf[i], new_len[i]);
		}
	}

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_EC_2P2G1, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	for (i = 0; i < 3; i++)
		d_iov_set(&sg_iov[i], buf[i], len[i]);

	d_iov_set(&iod[0].iod_name, "akey1", strlen("akey1"));
	d_iov_set(&iod[1].iod_name, "akey2", strlen("akey2"));
	d_iov_set(&iod[2].iod_name, "akey3", strlen("akey2"));

	for (i = 0; i < 3; i++) {
		sgl[i].sg_nr = 1;
		sgl[i].sg_nr_out = 0;
		sgl[i].sg_iovs = &sg_iov[i];
		iod[i].iod_nr = 1;
		iod[i].iod_recxs = NULL;
		iod[i].iod_type = DAOS_IOD_SINGLE;
		iod[i].iod_size = len[i];
	}

	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 3, iod, sgl, NULL);
	assert_rc_equal(rc, 0);

	/* overwrite */
	for (i = 0; i < 3; i++) {
		iod[i].iod_size = new_len[i];
		d_iov_set(&sg_iov[i], new_buf[i], new_len[i]);
	}
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 3, iod, sgl, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	assert_rc_equal(rc, 0);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 3; i++) {
		D_FREE(buf[i]);
		D_FREE(new_buf[i]);
	}
}

static void
ec_multi_array(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_sg_list_t	 sgl[3];
	d_iov_t		 sg_iov[3];
	daos_iod_t	 iod[3];
	char		*buf[3];
	char		*fetch_buf[3];
	size_t		 len[3];
	daos_recx_t	 recx[3];
	int		 i, rc;

	if (!test_runable(arg, 4))
		return;

	for (i = 0; i < 3; i++) {
		buf[i] = NULL;
		len[i] = 0;
	}

	len[0] = ec_cell_size * 4;
	len[1] = 8192;
	len[2] = ec_cell_size;
	recx[0].rx_idx = 0;
	recx[0].rx_nr = len[0];
	recx[1].rx_idx = 0;
	recx[1].rx_nr = len[1];
	recx[2].rx_idx = ec_cell_size;
	recx[2].rx_nr = len[2];

	for (i = 0; i < 3; i++) {
		D_ALLOC(buf[i], len[i]);
		assert_non_null(buf[i]);
		dts_buf_render(buf[i], len[i]);
		D_ALLOC(fetch_buf[i], len[i]);
		assert_non_null(fetch_buf[i]);
	}

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_EC_2P2G1, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	for (i = 0; i < 3; i++)
		d_iov_set(&sg_iov[i], buf[i], len[i]);

	d_iov_set(&iod[0].iod_name, "akey1", strlen("akey1"));
	d_iov_set(&iod[1].iod_name, "akey2", strlen("akey2"));
	d_iov_set(&iod[2].iod_name, "akey3", strlen("akey2"));

	for (i = 0; i < 3; i++) {
		sgl[i].sg_nr = 1;
		sgl[i].sg_nr_out = 0;
		sgl[i].sg_iovs = &sg_iov[i];
		iod[i].iod_nr = 1;
		iod[i].iod_recxs = &recx[i];
		iod[i].iod_type = DAOS_IOD_ARRAY;
		iod[i].iod_size = 1;
	}

	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 3, iod, sgl, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 3; i++)
		d_iov_set(&sg_iov[i], fetch_buf[i], len[i]);

	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 3, iod, sgl, NULL, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 3; i++)
		assert_memory_equal(buf[i], fetch_buf[i], len[i]);

	rc = daos_obj_verify(arg->coh, oid, DAOS_EPOCH_MAX);
	assert_rc_equal(rc, 0);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 3; i++) {
		D_FREE(buf[i]);
		D_FREE(fetch_buf[i]);
	}
}

static int
ec_setup(void  **state)
{
	int		rc;
	unsigned int	orig_dt_cell_size;
	int		num_ranks = 6;

	orig_dt_cell_size = dt_cell_size;
	dt_cell_size = ec_cell_size;
	save_group_state(state);
	rc = test_setup(state, SETUP_CONT_CONNECT, true,
			DEFAULT_POOL_SIZE, num_ranks, NULL);
	dt_cell_size = orig_dt_cell_size;
	if (rc) {
		/* Let's skip for this case, since it is possible there
		 * is not enough ranks here.
		 */
		print_message("Failed to create pool with %d ranks: "DF_RC"\n",
			      num_ranks, DP_RC(rc));
		return 0;
	}

	return 0;
}

static void
ec_few_partial_stripe_aggregation(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	daos_size_t	stripe_size;
	char		*data;
	daos_recx_t	recx;

	if (!test_runable(arg, 6))
		return;

	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "time");
	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2GX, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	stripe_size = ec_data_nr_get(oid) * (daos_size_t)ec_cell_size;
	data = (char *)malloc(stripe_size);
	assert_true(data != NULL);

	/* full stripe update */
	req.iod_type = DAOS_IOD_ARRAY;
	recx.rx_nr = stripe_size;
	recx.rx_idx = 0;
	memset(data, 'a', stripe_size);
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
		     data, stripe_size, &req);

	/* single partial stripe update */
	req.iod_type = DAOS_IOD_ARRAY;
	recx.rx_nr = ec_cell_size;
	recx.rx_idx = ec_cell_size;
	memset(data, 'b', ec_cell_size);
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
		     data, EC_CELL_SIZE, &req);

	trigger_and_wait_ec_aggreation(arg, &oid, 1, "d_key", "a_key", 0,
				       0, DAOS_FORCE_EC_AGG);
	ioreq_fini(&req);
	free(data);
}

static void
ec_rec_parity_list(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	int		i;
	char		*data;
	int		stripe_size = 4 * ec_cell_size;
	daos_anchor_t	anchor = { 0 };
	daos_size_t	size;
	uint64_t	start = UINT64_MAX;
	uint64_t	end = 0;

	if (!test_runable(arg, 6))
		return;

	data = (char *)malloc(stripe_size);
	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2G1, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 5; i++) {
		daos_recx_t recx;

		/* Make dkey on different shards */
		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = stripe_size;  /* full stripe write */
		recx.rx_idx = i * stripe_size;
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, stripe_size, &req);
	}

	for (i = 0; i < 5; i++) {
		daos_recx_t recx;

		/* Make dkey on different shards */
		req.iod_type = DAOS_IOD_ARRAY;
		recx.rx_nr = ec_cell_size;  /* partial stripe write */
		recx.rx_idx = i * stripe_size;
		insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, stripe_size, &req);
	}

	/* Verify the recx */
	while (!daos_anchor_is_eof(&anchor)) {
		daos_recx_t	   recxs[10];
		daos_epoch_range_t eprs[10];
		uint32_t	   number = 10;

		enumerate_rec(DAOS_TX_NONE, "d_key", "a_key", &size,
			      &number, recxs, eprs, &anchor, true, &req);
		for (i = 0; i < number; i++) {
			start = min(start, recxs[i].rx_idx);
			end = max(end, recxs[i].rx_idx + recxs[i].rx_nr);
		}
	}
	assert_rc_equal((int)start, 0);
	assert_rc_equal((int)end, 5 * stripe_size);
	free(data);
	ioreq_fini(&req);
}

static void
ec_update_2akeys(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_iov_t		 non_exist_dkey;
	d_sg_list_t	 sgl[2];
	d_iov_t		 sg_iov[2];
	daos_iod_t	 iod[2];
	daos_recx_t	 recx[2];
	char		*buf[2];
	char		*buf_cmp[2];
	char		*akey[2];
	const char	*akey_fmt = "akey21_%d";
	int		 i, rc;
	daos_size_t	 size;
	uint16_t	 fail_shards[2];
	uint64_t	 fail_val;

	FAULT_INJECTION_REQUIRED();

	if (!test_runable(arg, 6))
		return;

	/** open object */
	oid = daos_test_oid_gen(arg->coh, ec_obj_class, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));
	d_iov_set(&non_exist_dkey, "non_dkey", strlen("non_dkey"));

	for (i = 0; i < 2; i++) {
		D_ALLOC(akey[i], strlen(akey_fmt) + 1);
		sprintf(akey[i], akey_fmt, i);

		if (i == 0)
			size = ec_cell_size * 4;
		else
			size = 8192;
		D_ALLOC(buf[i], size);
		assert_non_null(buf[i]);
		D_ALLOC(buf_cmp[i], size);
		assert_non_null(buf_cmp[i]);

		dts_buf_render(buf[i], size);
		memcpy(buf_cmp[i], buf[i], size);

		/** init scatter/gather */
		d_iov_set(&sg_iov[i], buf[i], size);
		sgl[i].sg_nr		= 1;
		sgl[i].sg_nr_out	= 0;
		sgl[i].sg_iovs		= &sg_iov[i];

		/** init I/O descriptor */
		d_iov_set(&iod[i].iod_name, akey[i], strlen(akey[i]));
		iod[i].iod_nr		= 1;
		iod[i].iod_size		= 1;
		iod[i].iod_recxs	= &recx[i];
		iod[i].iod_type		= DAOS_IOD_ARRAY;
		if (i == 0) {
			recx[i].rx_idx		= 0;
			recx[i].rx_nr		= size;
		} else {
			recx[i].rx_idx		= ec_cell_size;
			recx[i].rx_nr		= size;
		}
	}

	/* test the case that update 2 akeys in one IO, one akey is full-stripe update,
	 * another is partial update, fail two parity shards, then will select data shard 0
	 * as leader and 2nd akey update is empty on the leader.
	 */
	fail_shards[0] = 4;
	fail_shards[1] = 5;
	fail_val = daos_shard_fail_value(fail_shards, 2);
	daos_fail_value_set(fail_val);
	daos_fail_loc_set(DAOS_FAIL_SHARD_OPEN | DAOS_FAIL_ALWAYS);

	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 2, iod, sgl, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 2; i++)
		iod[i].iod_size	= DAOS_REC_ANY;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 2, iod, NULL, NULL, NULL);
	assert_rc_equal(rc, 0);
	for (i = 0; i < 2; i++)
		assert_int_equal(iod[i].iod_size, 1);

	for (i = 0; i < 2; i++) {
		if (i == 0)
			size = ec_cell_size * 4;
		else
			size = 8192;
		memset(buf[i], 0, size);
		d_iov_set(&sg_iov[i], buf[i], size);
	}
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 2, iod, sgl, NULL, NULL);
	assert_rc_equal(rc, 0);
	assert_memory_equal(buf[0], buf_cmp[0], ec_cell_size * 4);
	assert_memory_equal(buf[1], buf_cmp[1], 8192);

	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	daos_fail_value_set(0);
	daos_fail_loc_set(0);

	for (i = 0; i < 2; i++) {
		D_FREE(akey[i]);
		D_FREE(buf[i]);
		D_FREE(buf_cmp[i]);
	}
}

static void
ec_dkey_enum_fail(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	int		num_dkey = 1000;
	daos_anchor_t	anchor = { 0 };
	int		total = 0;
	char		buf[512];
	daos_size_t	buf_len = 512;
	int		i;
	int		rc;

	if (!test_runable(arg, 3))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_2P1G1, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < num_dkey; i++) {
		char dkey[32];
		char data[5];
		daos_recx_t recx;

		/* Make dkey on different shards */
		req.iod_type = DAOS_IOD_ARRAY;
		sprintf(dkey, "dkey_%d", i);
		recx.rx_nr = 5;
		recx.rx_idx = 0;
		memset(data, 'a', 5);
		insert_recxs(dkey, "a_key", 1, DAOS_TX_NONE, &recx, 1, data, 16, &req);
	}

	print_message("iterate dkey...\n");
	while (!daos_anchor_is_eof(&anchor)) {
		daos_key_desc_t	kds[10];
		uint32_t	number = 10;

		memset(buf, 0, buf_len);
		memset(kds, 0, sizeof(*kds) * number);
		rc = enumerate_dkey(DAOS_TX_NONE, &number, kds, &anchor, buf, buf_len, &req);
		assert_rc_equal(rc, 0);
		if (total == 0) {
			daos_fail_loc_set(DAOS_FAIL_SHARD_OPEN | DAOS_FAIL_ALWAYS);
			daos_fail_value_set(2);
		}
		total += number;
	}
	daos_fail_loc_set(0);
	daos_fail_value_set(0);

	assert_rc_equal(total, 1000);

	ioreq_fini(&req);
}

static void
ec_single_stripe_nvme_io(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	daos_size_t	stripe_size;
	char		*data;
	char		*verify_data;
	daos_recx_t	recx;
	int		i;

	if (!test_runable(arg, 6))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_2P1G1, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	stripe_size = ec_data_nr_get(oid) * (daos_size_t)EC_CELL_SIZE;
	data = (char *)malloc(stripe_size);
	assert_true(data != NULL);
	verify_data = (char *)malloc(stripe_size);
	assert_true(verify_data != NULL);

	req.iod_type = DAOS_IOD_ARRAY;
	recx.rx_nr = stripe_size;
	recx.rx_idx = 0;
	memset(data, 'a', stripe_size);
	memset(verify_data, 'a', stripe_size);
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
		     data, stripe_size, &req);

	for (i = 0; i < 3; i++) {
		uint32_t rank;

		rank = get_rank_by_oid_shard(arg, oid, i);
		daos_debug_set_params(arg->group, rank, DMG_KEY_FAIL_LOC,
				      DAOS_OBJ_FAIL_NVME_IO | DAOS_FAIL_ALWAYS, 0, NULL);

		lookup_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, stripe_size, &req);

		assert_memory_equal(data, verify_data, stripe_size);

		daos_debug_set_params(arg->group, rank, DMG_KEY_FAIL_LOC,
				      0, 0, NULL);
	}
	ioreq_fini(&req);
}

static void
ec_two_stripes_nvme_io(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	daos_size_t	stripe_size;
	char		*data;
	char		*verify_data;
	daos_recx_t	recx;
	int		i;

	if (!test_runable(arg, 6))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2G1, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	stripe_size = ec_data_nr_get(oid) * (daos_size_t)EC_CELL_SIZE;
	data = (char *)malloc(stripe_size);
	assert_true(data != NULL);
	verify_data = (char *)malloc(stripe_size);
	assert_true(verify_data != NULL);

	req.iod_type = DAOS_IOD_ARRAY;
	recx.rx_nr = stripe_size;
	recx.rx_idx = 0;
	memset(data, 'a', stripe_size);
	memset(verify_data, 'a', stripe_size);
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
		     data, stripe_size, &req);

	for (i = 0; i < ec_tgt_nr_get(oid); i++) {
		uint32_t rank1;
		uint32_t rank2;

		rank1 = get_rank_by_oid_shard(arg, oid, i);
		rank2 = get_rank_by_oid_shard(arg, oid, (i + 1) % ec_tgt_nr_get(oid));
		daos_debug_set_params(arg->group, rank1, DMG_KEY_FAIL_LOC,
				      DAOS_OBJ_FAIL_NVME_IO | DAOS_FAIL_ALWAYS, 0, NULL);
		daos_debug_set_params(arg->group, rank2, DMG_KEY_FAIL_LOC,
				      DAOS_OBJ_FAIL_NVME_IO | DAOS_FAIL_ALWAYS, 0, NULL);

		lookup_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, stripe_size, &req);

		assert_memory_equal(data, verify_data, stripe_size);

		daos_debug_set_params(arg->group, rank1, DMG_KEY_FAIL_LOC,
				      0, 0, NULL);
		daos_debug_set_params(arg->group, rank2, DMG_KEY_FAIL_LOC,
				      0, 0, NULL);
	}
	ioreq_fini(&req);
}

static void
ec_three_stripes_nvme_io(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	daos_size_t	stripe_size;
	char		*data;
	char		*verify_data;
	daos_recx_t	recx;
	int		i;

	if (!test_runable(arg, 6))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2G1, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	stripe_size = ec_data_nr_get(oid) * (daos_size_t)EC_CELL_SIZE;
	data = (char *)malloc(stripe_size);
	assert_true(data != NULL);
	verify_data = (char *)malloc(stripe_size);
	assert_true(verify_data != NULL);

	req.iod_type = DAOS_IOD_ARRAY;
	recx.rx_nr = stripe_size;
	recx.rx_idx = 0;
	memset(data, 'a', stripe_size);
	memset(verify_data, 'a', stripe_size);
	insert_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
		     data, stripe_size, &req);

	for (i = 0; i < ec_tgt_nr_get(oid); i++) {
		uint32_t rank1;
		uint32_t rank2;
		uint32_t rank3;

		rank1 = get_rank_by_oid_shard(arg, oid, i);
		rank2 = get_rank_by_oid_shard(arg, oid, (i + 1) % ec_tgt_nr_get(oid));
		rank3 = get_rank_by_oid_shard(arg, oid, (i + 2) % ec_tgt_nr_get(oid));
		daos_debug_set_params(arg->group, rank1, DMG_KEY_FAIL_LOC,
				      DAOS_OBJ_FAIL_NVME_IO | DAOS_FAIL_ALWAYS, 0, NULL);
		daos_debug_set_params(arg->group, rank2, DMG_KEY_FAIL_LOC,
				      DAOS_OBJ_FAIL_NVME_IO | DAOS_FAIL_ALWAYS, 0, NULL);
		daos_debug_set_params(arg->group, rank3, DMG_KEY_FAIL_LOC,
				      DAOS_OBJ_FAIL_NVME_IO | DAOS_FAIL_ALWAYS, 0, NULL);

		arg->expect_result = -DER_NVME_IO;
		lookup_recxs("d_key", "a_key", 1, DAOS_TX_NONE, &recx, 1,
			     data, stripe_size, &req);

		daos_debug_set_params(arg->group, rank1, DMG_KEY_FAIL_LOC,
				      0, 0, NULL);
		daos_debug_set_params(arg->group, rank2, DMG_KEY_FAIL_LOC,
				      0, 0, NULL);
		daos_debug_set_params(arg->group, rank3, DMG_KEY_FAIL_LOC,
				      0, 0, NULL);
	}
	ioreq_fini(&req);
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
	{"EC16: ec single-value overwrite", ec_singv_overwrite, async_disable,
	 test_case_teardown},
	{"EC17: ec single-value different size fetch", ec_singv_diff_size_fetch, async_disable,
	 test_case_teardown},
	{"EC18: ec conditional fetch", ec_cond_fetch, async_disable, test_case_teardown},
	{"EC19: ec few partial stripe update", ec_few_partial_stripe_aggregation, async_disable,
	 test_case_teardown},
	{"EC20: ec recx list from parity", ec_rec_parity_list, async_disable, test_case_teardown},
	{"EC21: ec update two akeys and parity shards failed", ec_update_2akeys, async_disable,
	 test_case_teardown},
	{"EC22: ec data recovery", ec_data_recov, async_disable, test_case_teardown},
	{"EC23: ec multi-singv overwrite", ec_multi_singv_overwrite, async_disable,
	test_case_teardown},
	{"EC24: ec multi-array update", ec_multi_array, async_disable,
	test_case_teardown},
	{"EC25: ec dkey enumerate with failure shard", ec_dkey_enum_fail, async_disable,
	test_case_teardown},
	{"EC26: ec single nvme io failed", ec_single_stripe_nvme_io, async_disable,
	test_case_teardown},
	{"EC27: ec double nvme io failed", ec_two_stripes_nvme_io, async_disable,
	test_case_teardown},
	{"EC28: ec three nvme io failed", ec_three_stripes_nvme_io, async_disable,
	test_case_teardown},
};

int
run_daos_ec_io_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	par_barrier(PAR_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(ec_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests("DAOS_EC", ec_tests, ARRAY_SIZE(ec_tests),
				sub_tests, sub_tests_size, ec_setup,
				test_teardown);

	par_barrier(PAR_COMM_WORLD);

	return rc;
}
