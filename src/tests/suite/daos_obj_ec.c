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
ec_dkey_list(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	int		num_dkey;
	int		i;

	if (!test_runable(arg, 6))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2G1, 0, 0, arg->myrank);
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
ec_akey_list(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	int		num_akey;
	int		i;

	if (!test_runable(arg, 6))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2G1, 0, 0, arg->myrank);
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
	ioreq_fini(&req);
}

static int
get_rec_cnt(struct ioreq *req, char *dkey, char *akey)
{
	daos_anchor_t	anchor = { 0 };
	int		total = 0;
	daos_size_t	size;

	while (!daos_anchor_is_eof(&anchor)) {
		daos_recx_t	   recxs[10];
		daos_epoch_range_t eprs[10];
		uint32_t	   number = 10;

		enumerate_rec(DAOS_TX_NONE, dkey, akey, &size,
			      &number, recxs, eprs, &anchor, true, req);
		total += number;
	}

	return total;
}

static void
ec_rec_list(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oid;
	int		num_rec;
	int		i;

	if (!test_runable(arg, 6))
		return;

	oid = daos_test_oid_gen(arg->coh, OC_EC_4P2G1, 0, 0, arg->myrank);
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

	num_rec = get_rec_cnt(&req, "d_key", "a_key");
	assert_int_equal(num_rec, 100);

	daos_fail_loc_set(DAOS_OBJ_SKIP_PARITY | DAOS_FAIL_ALWAYS);
	num_rec = get_rec_cnt(&req, "d_key", "a_key");
	assert_int_equal(num_rec, 100);
	ioreq_fini(&req);
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
	{"EC0: ec dkey test",
	 ec_dkey_list, async_disable, test_case_teardown},
	{"EC1: ec akey test",
	 ec_akey_list, async_disable, test_case_teardown},
	{"EC1: ec rec test",
	 ec_rec_list, async_disable, test_case_teardown},
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
