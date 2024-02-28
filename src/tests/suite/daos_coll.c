/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * tests/suite/daos_coll.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_test.h"
#include "daos_iotest.h"

#define DTS_DKEY_CNT	8
#define DTS_DKEY_SIZE	16
#define DTS_IOSIZE	64

static void
obj_coll_punch(test_arg_t *arg, daos_oclass_id_t oclass)
{
	char		 buf[DTS_IOSIZE];
	char		 dkeys[DTS_DKEY_CNT][DTS_DKEY_SIZE];
	const char	*akey = "daos_coll_akey";
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	int		 i;

	oid = daos_test_oid_gen(arg->coh, oclass, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	for (i = 0; i < DTS_DKEY_CNT; i++) {
		dts_buf_render(dkeys[i], DTS_DKEY_SIZE);
		dts_buf_render(buf, DTS_IOSIZE);
		insert_single(dkeys[i], akey, 0, buf, DTS_IOSIZE, DAOS_TX_NONE, &req);
	}

	print_message("Collective punch object\n");
	punch_obj(DAOS_TX_NONE, &req);

	print_message("Fetch after punch\n");
	arg->expect_result = -DER_NONEXIST;
	for (i = 0; i < DTS_DKEY_CNT; i++)
		lookup_empty_single(dkeys[i], akey, 0, buf, DTS_IOSIZE, DAOS_TX_NONE, &req);

	ioreq_fini(&req);
}

static void
coll_1(void **state)
{
	test_arg_t	*arg = *state;

	print_message("Collective punch object - OC_SX\n");

	if (!test_runable(arg, 2))
		return;

	obj_coll_punch(arg, OC_SX);
}

static void
coll_2(void **state)
{
	test_arg_t	*arg = *state;

	print_message("Collective punch object - OC_EC_2P1G2\n");

	if (!test_runable(arg, 3))
		return;

	obj_coll_punch(arg, OC_EC_2P1G2);
}

static void
coll_3(void **state)
{
	test_arg_t	*arg = *state;

	print_message("Collective punch object - OC_EC_4P1GX\n");

	if (!test_runable(arg, 5))
		return;

	obj_coll_punch(arg, OC_EC_4P1GX);
}

static void
obj_coll_query(test_arg_t *arg, daos_oclass_id_t oclass, bool sparse)
{
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	daos_iod_t	iod = { 0 };
	d_sg_list_t	sgl = { 0 };
	daos_recx_t	recx = { 0 };
	d_iov_t		val_iov;
	d_iov_t		dkey;
	d_iov_t		akey;
	uint64_t	dkey_val;
	uint64_t	akey_val;
	uint32_t	update_var = 0xdeadbeef;
	uint32_t	flags;
	int		rc;

	/** init dkey, akey */
	dkey_val = akey_val = 0;
	d_iov_set(&dkey, &dkey_val, sizeof(uint64_t));
	d_iov_set(&akey, &akey_val, sizeof(uint64_t));

	oid = daos_test_oid_gen(arg->coh, oclass, DAOS_OT_MULTI_UINT64, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	dkey_val = 5;
	akey_val = 10;
	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_name = akey;
	iod.iod_recxs = &recx;
	iod.iod_nr = 1;
	iod.iod_size = sizeof(update_var);

	d_iov_set(&val_iov, &update_var, sizeof(update_var));
	sgl.sg_iovs = &val_iov;
	sgl.sg_nr = 1;

	recx.rx_idx = 5;
	recx.rx_nr = 1;

	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	dkey_val = 10;
	val_iov.iov_buf_len += 1024;
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);
	d_iov_set(&val_iov, &update_var, sizeof(update_var));

	recx.rx_idx = 50;
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	if (sparse) {
		par_barrier(PAR_COMM_WORLD);
		daos_fail_num_set(2);
		daos_fail_loc_set(DAOS_OBJ_COLL_SPARSE | DAOS_FAIL_SOME);
		par_barrier(PAR_COMM_WORLD);
	}

	flags = DAOS_GET_DKEY | DAOS_GET_AKEY | DAOS_GET_RECX | DAOS_GET_MAX;
	rc = daos_obj_query_key(oh, DAOS_TX_NONE, flags, &dkey, &akey, &recx, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(*(uint64_t *)dkey.iov_buf, 10);
	assert_int_equal(*(uint64_t *)akey.iov_buf, 10);
	assert_int_equal(recx.rx_idx, 50);
	assert_int_equal(recx.rx_nr, 1);

	flags = DAOS_GET_AKEY | DAOS_GET_RECX | DAOS_GET_MAX;
	rc = daos_obj_query_key(oh, DAOS_TX_NONE, flags, &dkey, &akey, &recx, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(*(uint64_t *)akey.iov_buf, 10);
	assert_int_equal(recx.rx_idx, 50);
	assert_int_equal(recx.rx_nr, 1);

	flags = DAOS_GET_RECX | DAOS_GET_MAX;
	rc = daos_obj_query_key(oh, DAOS_TX_NONE, flags, &dkey, &akey, &recx, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(recx.rx_idx, 50);
	assert_int_equal(recx.rx_nr, 1);

	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	if (sparse) {
		par_barrier(PAR_COMM_WORLD);
		daos_fail_loc_set(0);
		daos_fail_num_set(0);
		par_barrier(PAR_COMM_WORLD);
	}
}

static void
coll_4(void **state)
{
	test_arg_t	*arg = *state;

	print_message("Collective object query - OC_SX\n");

	if (!test_runable(arg, 2))
		return;

	obj_coll_query(arg, OC_SX, false);
}

static void
coll_5(void **state)
{
	test_arg_t	*arg = *state;

	print_message("Collective object query - OC_EC_2P1G2\n");

	if (!test_runable(arg, 3))
		return;

	obj_coll_query(arg, OC_EC_2P1G2, false);
}

static void
coll_6(void **state)
{
	test_arg_t	*arg = *state;

	print_message("Collective object query - OC_EC_4P1GX\n");

	if (!test_runable(arg, 5))
		return;

	obj_coll_query(arg, OC_EC_4P1GX, false);
}

static void
coll_7(void **state)
{
	test_arg_t	*arg = *state;

	print_message("Collective object query with sparse ranks\n");

	if (!test_runable(arg, 3))
		return;

	obj_coll_query(arg, OC_RP_3GX, true);
}

static void
coll_8(void **state)
{
	test_arg_t	*arg = *state;

	print_message("Collective object query with rank_0 excluded\n");

	if (!test_runable(arg, 4))
		return;

	rebuild_single_pool_rank(arg, 0, false);
	obj_coll_query(arg, OC_EC_2P1GX, false);
	reintegrate_single_pool_rank(arg, 0, false);
}

static const struct CMUnitTest coll_tests[] = {
	{ "COLL_1: collective punch object - OC_SX",
	  coll_1, NULL, test_case_teardown},
	{ "COLL_2: collective punch object - OC_EC_2P1G2",
	  coll_2, NULL, test_case_teardown},
	{ "COLL_3: collective punch object - OC_EC_4P1GX",
	  coll_3, NULL, test_case_teardown},
	{ "COLL_4: collective object query - OC_SX",
	  coll_4, async_disable, test_case_teardown},
	{ "COLL_5: collective object query - OC_EC_2P1G2",
	  coll_5, async_disable, test_case_teardown},
	{ "COLL_6: collective object query - OC_EC_4P1GX",
	  coll_6, async_disable, test_case_teardown},
	{ "COLL_7: collective object query with sparse ranks",
	  coll_7, async_disable, test_case_teardown},
	{ "COLL_8: collective object query with rank_0 excluded",
	  coll_8, rebuild_sub_6nodes_rf1_setup, test_teardown},
};

static int
coll_test_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE, 0, NULL);
}

int
run_daos_coll_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int	rc;

	par_barrier(PAR_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(coll_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests("DAOS_COLL", coll_tests, ARRAY_SIZE(coll_tests),
				sub_tests, sub_tests_size, coll_test_setup, test_teardown);
	par_barrier(PAR_COMM_WORLD);

	return rc;
}
