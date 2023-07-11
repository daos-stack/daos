/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * tests/suite/daos_upgrade.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

#define OBJ_NR		10

static void
upgrade_ec_parity_rotate(void **state)
{
	test_arg_t	*arg = *state;
	test_arg_t	*new_arg = NULL;
	struct ioreq	req;
	daos_obj_id_t	oid;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;
	int		rc;

	if (!test_runable(arg, 6))
		return;

	if (arg->myrank == 0) {
		rc = daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
					   DAOS_FAIL_POOL_CREATE_VERSION | DAOS_FAIL_ALWAYS,
					   0, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE,
					   0, 0, 0);
		assert_rc_equal(rc, 0);
	}

	/* create/connect another pool */
	rc = test_setup((void **)&new_arg, SETUP_CONT_CONNECT, arg->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_rc_equal(rc, 0);

	oid = daos_test_oid_gen(new_arg->coh, OC_EC_4P1G1, 0, 0, new_arg->myrank);
	ioreq_init(&req, new_arg->coh, oid, DAOS_IOD_ARRAY, new_arg);
	write_ec_full(&req, new_arg->index, 0);
	ioreq_fini(&req);

	for (i = 0; i < OBJ_NR; i++)
		oids[i] = daos_test_oid_gen(new_arg->coh, OC_RP_3G2, 0, 0, arg->myrank);

	rebuild_io(new_arg, oids, OBJ_NR);

	if (arg->myrank == 0) {
		rc = daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
					   DAOS_FORCE_OBJ_UPGRADE | DAOS_FAIL_ALWAYS,
					   0, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = daos_pool_upgrade(new_arg->pool.pool_uuid);
	assert_rc_equal(rc, 0);

	print_message("sleep 50 seconds for upgrade to finish!\n");
	sleep(50);
	rebuild_pool_connect_internal(new_arg);
	ioreq_init(&req, new_arg->coh, oid, DAOS_IOD_ARRAY, new_arg);
	verify_ec_full(&req, new_arg->index, 0);
	rebuild_io_validate(new_arg, oids, OBJ_NR);
	ioreq_fini(&req);

	if (arg->myrank == 0) {
		rc = daos_debug_set_params(new_arg->group, -1, DMG_KEY_FAIL_LOC, 0,
					   0, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_debug_set_params(new_arg->group, -1, DMG_KEY_FAIL_VALUE,
					   0, 0, 0);
		assert_rc_equal(rc, 0);
	}

	test_teardown((void **)&new_arg);
}

static void
upgrade_ec_parity_rotate_single_dkey(void **state)
{
	test_arg_t	*arg = *state;
	test_arg_t	*new_arg = NULL;
	struct ioreq	req;
	daos_obj_id_t	oid;
	char		buf[10];
	int		rc;

	if (!test_runable(arg, 6))
		return;

	if (arg->myrank == 0) {
		rc = daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
					   DAOS_FAIL_POOL_CREATE_VERSION | DAOS_FAIL_ALWAYS,
					   0, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE,
					   0, 0, 0);
		assert_rc_equal(rc, 0);
	}

	/* create/connect another pool */
	rc = test_setup((void **)&new_arg, SETUP_CONT_CONNECT, arg->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_rc_equal(rc, 0);

	oid = daos_test_oid_gen(new_arg->coh, OC_EC_4P1GX, 0, 0, new_arg->myrank);
	ioreq_init(&req, new_arg->coh, oid, DAOS_IOD_ARRAY, new_arg);

	insert_single("upgrade_dkey", "upgrade_akey", 0, "data",
		       strlen("data") + 1, DAOS_TX_NONE, &req);

	insert_single("upgrade_dkey1", "upgrade_akey1", 0, "data",
		       strlen("data") + 1, DAOS_TX_NONE, &req);


	ioreq_fini(&req);

	if (arg->myrank == 0) {
		rc = daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
					   DAOS_FORCE_OBJ_UPGRADE | DAOS_FAIL_ALWAYS,
					   0, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = daos_pool_upgrade(new_arg->pool.pool_uuid);
	assert_rc_equal(rc, 0);

	print_message("sleep 50 seconds for upgrade to finish!\n");
	sleep(50);

	rebuild_pool_connect_internal(new_arg);
	ioreq_init(&req, new_arg->coh, oid, DAOS_IOD_ARRAY, new_arg);
	memset(buf, 0, 10);
	lookup_single("upgrade_dkey", "upgrade_akey", 0,
		      buf, 10, DAOS_TX_NONE, &req);
	assert_int_equal(req.iod[0].iod_size, strlen(buf) + 1);
	assert_string_equal(buf, "data");

	lookup_single("upgrade_dkey1", "upgrade_akey1", 0,
		      buf, 10, DAOS_TX_NONE, &req);
	assert_int_equal(req.iod[0].iod_size, strlen(buf) + 1);
	assert_string_equal(buf, "data");

	ioreq_fini(&req);
	test_teardown((void **)&new_arg);
}

int
upgrade_sub_setup(void **state)
{
	test_arg_t	*arg;
	int rc;

	save_group_state(state);
	rc = test_setup(state, SETUP_CONT_CONNECT, true,
			SMALL_POOL_SIZE, 0, NULL);
	if (rc)
		return rc;

	arg = *state;
	if (dt_obj_class != DAOS_OC_UNKNOWN)
		arg->obj_class = dt_obj_class;
	else
		arg->obj_class = OC_EC_4P1G1;

	return 0;
}

/** create a new pool/container for each test */
static const struct CMUnitTest upgrade_tests[] = {
	{"UPGRADE0: upgrade object ec parity layout",
	upgrade_ec_parity_rotate, upgrade_sub_setup, test_teardown},
	{"UPGRADE1: upgrade single dkey",
	upgrade_ec_parity_rotate_single_dkey, upgrade_sub_setup, test_teardown},
};

int
run_daos_upgrade_test(int rank, int size, int *sub_tests,
		      int sub_tests_size)
{
	int rc = 0;

	par_barrier(PAR_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(upgrade_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests_only("DAOS_upgrade", upgrade_tests, ARRAY_SIZE(upgrade_tests),
				     sub_tests, sub_tests_size);

	par_barrier(PAR_COMM_WORLD);

	return rc;
}
