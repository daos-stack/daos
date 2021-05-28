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
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

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
trigger_and_wait_ec_aggreation(test_arg_t *arg, daos_obj_id_t oid)
{
	d_rank_t  ec_agg_rank;

	get_killing_rank_by_oid(arg, oid, 0, 1, &ec_agg_rank, NULL);
	daos_debug_set_params(arg->group, ec_agg_rank, DMG_KEY_FAIL_LOC,
			      DAOS_FORCE_EC_AGG | DAOS_FAIL_ALWAYS,
			      0, NULL);

	print_message("wait for 5 seconds for EC aggregation.\n");
	sleep(5);
	daos_debug_set_params(arg->group, ec_agg_rank, DMG_KEY_FAIL_LOC,
			      0, 0, NULL);
}

static void
ec_verify_parity_data(struct ioreq *req, char *dkey, char *akey,
		      daos_off_t offset, daos_size_t size,
		      char *verify_data)
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
	lookup_recxs(dkey, akey, 1, DAOS_TX_NONE, &recx, 1,
		     data, size, req);
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

	trigger_and_wait_ec_aggreation(arg, oid);

	for (i = 0; i < 10; i++) {
		daos_off_t offset = i * EC_CELL_SIZE;

		memset(verify_data, 'a' + i, EC_CELL_SIZE);
		ec_verify_parity_data(&req, "d_key", "a_key", offset,
				      (daos_size_t)EC_CELL_SIZE, verify_data);
	}
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

	trigger_and_wait_ec_aggreation(arg, oid);

	for (i = 0; i < 20; i++) {
		char		c = 'a' + i;
		daos_off_t offset = i * update_size;

		memset(verify_data, c, update_size);
		ec_verify_parity_data(&req, "d_key", "a_key", offset,
				      update_size, verify_data);
	}
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
	full_update_size = 3 * data_nr * EC_CELL_SIZE;
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

	trigger_and_wait_ec_aggreation(arg, oid);

	ec_verify_parity_data(&req, "d_key", "a_key", (daos_size_t)0,
			      full_update_size, verify_data);
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
	full_update_size = 3 * data_nr * EC_CELL_SIZE;
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

	trigger_and_wait_ec_aggreation(arg, oid);

	ec_verify_parity_data(&req, "d_key", "a_key", (daos_size_t)0,
			      full_update_size, verify_data);

	free(data);
	free(verify_data);
}

static int
ec_setup(void  **state)
{
	int rc;

	save_group_state(state);
	rc = test_setup(state, SETUP_CONT_CONNECT, true,
			SMALL_POOL_SIZE, 6, NULL);
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
