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
#include "dfs_test.h"
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

void
dfs_ec_upgrade(void **state)
{
	test_arg_t	*new_arg = NULL;
	dfs_t		*dfs_mt;
	daos_handle_t	co_hdl;
	test_arg_t	*arg = *state;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	dfs_obj_t	*obj;
	daos_size_t	buf_size = 1048576;
	uuid_t		co_uuid;
	char		filename[32];
	char		*buf;
	char		*vbuf;
	int		i;
	int		rc;
	dfs_attr_t	attr = {};

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
	rc = test_setup((void **)&new_arg, SETUP_POOL_CONNECT, arg->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_rc_equal(rc, 0);

	attr.da_props = daos_prop_alloc(1);
	assert_non_null(attr.da_props);
	attr.da_props->dpp_entries[0].dpe_type = DAOS_PROP_CO_EC_CELL_SZ;
	attr.da_props->dpp_entries[0].dpe_val = 64 * 1024;
	rc = dfs_cont_create(new_arg->pool.poh, &co_uuid, &attr, &co_hdl, &dfs_mt);
	daos_prop_free(attr.da_props);

	uuid_unparse(co_uuid, new_arg->co_str);
	assert_int_equal(rc, 0);
	printf("Created DFS Container "DF_UUIDF"\n", DP_UUID(co_uuid));

	D_ALLOC(buf, buf_size);
	assert_true(buf != NULL);
	D_ALLOC(vbuf, buf_size);
	assert_true(vbuf != NULL);

	sprintf(filename, "ec_file");
	rc = dfs_open(dfs_mt, NULL, filename, S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, OC_EC_2P1GX, 1048576, NULL, &obj);
	assert_int_equal(rc, 0);

	d_iov_set(&iov, buf, buf_size);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = &iov;

	dts_buf_render(buf, buf_size);
	memcpy(vbuf, buf, buf_size);

	for (i = 0; i < 50; i++) {
		rc = dfs_write(dfs_mt, obj, &sgl, i * buf_size, NULL);
		assert_int_equal(rc, 0);
	}

	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	rc = dfs_umount(dfs_mt);
	assert_int_equal(rc, 0);

	rc = daos_cont_close(co_hdl, NULL);
	assert_rc_equal(rc, 0);

	if (arg->myrank == 0) {
		rc = daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
					   DAOS_FORCE_OBJ_UPGRADE | DAOS_FAIL_ALWAYS,
					   0, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = daos_pool_upgrade(new_arg->pool.pool_uuid);
	assert_rc_equal(rc, 0);

	print_message("sleep 80 seconds for upgrade to finish!\n");
	sleep(80);

	rebuild_pool_connect_internal(new_arg);
	/** mount in Relaxed mode should succeed */
	rc = dfs_mount(new_arg->pool.poh, new_arg->coh, O_RDWR | DFS_RELAXED, &dfs_mt);
	assert_int_equal(rc, 0);

	rc = dfs_open(dfs_mt, NULL, filename, S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDONLY, OC_EC_2P1GX, 1048576, NULL, &obj);
	assert_int_equal(rc, 0);

	for (i = 0; i < 50; i++) {
		daos_size_t fetch_size = 0;

		memset(buf, 0, buf_size);
		rc = dfs_read(dfs_mt, obj, &sgl, buf_size * i, &fetch_size, NULL);
		assert_int_equal(rc, 0);
		assert_int_equal(fetch_size, buf_size);
		assert_memory_equal(buf, vbuf, buf_size);
	}

	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	D_FREE(buf);
	D_FREE(vbuf);
	rc = dfs_umount(dfs_mt);
	assert_int_equal(rc, 0);

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
	{"UPGRADE2: upgrade with dfs",
	dfs_ec_upgrade, upgrade_sub_setup, test_teardown},
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
