/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

#include "dfs_test.h"
#include "dfs_internal.h"
#include <pthread.h>

/** global DFS mount used for all tests */
static uuid_t		co_uuid;
static daos_handle_t	co_hdl;
static dfs_sys_t	*dfs_sys_mt;

static void
dfs_sys_test_mount(void **state)
{
	test_arg_t		*arg = *state;
	uuid_t			cuuid;
	daos_cont_info_t	co_info;
	daos_handle_t		coh;
	dfs_sys_t		*dfs_sys;
	int			rc;

	if (arg->myrank != 0)
		return;

	/** create a DFS container with POSIX layout */
	uuid_generate(cuuid);
	rc = dfs_cont_create(arg->pool.poh, cuuid, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("Created POSIX Container "DF_UUIDF"\n", DP_UUID(cuuid));
	rc = daos_cont_open(arg->pool.poh, cuuid, DAOS_COO_RW,
			    &coh, &co_info, NULL);
	assert_rc_equal(rc, 0);

	rc = dfs_sys_mount(arg->pool.poh, coh, O_RDWR, 0, &dfs_sys);
	assert_int_equal(rc, 0);

	rc = dfs_sys_umount(dfs_sys);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, cuuid, 1, NULL);
	assert_rc_equal(rc, 0);
	print_message("Destroyed POSIX Container "DF_UUIDF"\n", DP_UUID(cuuid));
}

static void
dfs_sys_test_create_remove(void **state)
{
	test_arg_t	*arg = *state;
	int		rc;
	const char	*dir1 = "/dir1";
	const char	*dir2 = "/dir1/dir2";
	const char	*dir3 = "/dir1/dir2/dir3";
	const char	*file1 = "/dir1/dir2/file1";
	const char	*file2 = "/dir1/dir2/dir3/file2";
	const char	*sym1 = "/dir1/dir2/sym1";
	dfs_obj_t	*obj;

	if (arg->myrank != 0)
		return;

	print_message("Creating dirs with mkdir()\n");
	rc = dfs_sys_mkdir(dfs_sys_mt, dir1, O_RDWR, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_mkdir(dfs_sys_mt, dir2, O_RDWR, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_mkdir(dfs_sys_mt, dir3, O_RDWR, 0);
	assert_int_equal(rc, 0);

	print_message("Creating links with symlink()\n");
	rc = dfs_sys_symlink(dfs_sys_mt, file1, sym1);
	assert_int_equal(rc, 0);

	print_message("Removing dirs, links with remove()\n");
	rc = dfs_sys_remove(dfs_sys_mt, sym1, 0, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_remove(dfs_sys_mt, dir3, 0, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_remove(dfs_sys_mt, dir2, 0, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_remove(dfs_sys_mt, dir1, 0, 0);
	assert_int_equal(rc, 0);

	print_message("Creating dirs, files, links with open\n");
	rc = dfs_sys_open(dfs_sys_mt, dir1, S_IFDIR | S_IWUSR | S_IRUSR,
			  O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_open(dfs_sys_mt, dir2, S_IFDIR | S_IWUSR | S_IRUSR,
			  O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_open(dfs_sys_mt, dir3, S_IFDIR | S_IWUSR | S_IRUSR,
			  O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_open(dfs_sys_mt, file1, S_IFREG | S_IWUSR | S_IRUSR,
			  O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_open(dfs_sys_mt, file2, S_IFREG | S_IWUSR | S_IRUSR,
			  O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_open(dfs_sys_mt, sym1, S_IFLNK | S_IWUSR | S_IRUSR,
			  O_RDWR | O_CREAT | O_EXCL, 0, 0, file1, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_sys_close(obj);
	assert_int_equal(rc, 0);

	print_message("Removing files with remove()\n");
	rc = dfs_sys_remove(dfs_sys_mt, file2, 0, 0);
	assert_int_equal(rc, 0);

	print_message("Removing dirs, files, links with remove_type()\n");
	rc = dfs_sys_remove_type(dfs_sys_mt, file1, false, S_IFREG, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_sys_remove_type(dfs_sys_mt, sym1, false, S_IFLNK, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_sys_remove_type(dfs_sys_mt, dir3, false, S_IFDIR, NULL);
	assert_int_equal(rc, 0);

	print_message("Removing dirs with remove_type(force)\n");
	rc = dfs_sys_remove_type(dfs_sys_mt, dir1, true, S_IFDIR, NULL);
	assert_int_equal(rc, 0);

	print_message("Creating dirs, files with mknod()\n");
	rc = dfs_sys_mknod(dfs_sys_mt, dir1, S_IFDIR | S_IWUSR | S_IRUSR,
			   0, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_mknod(dfs_sys_mt, dir2, S_IFDIR | S_IWUSR | S_IRUSR,
			   0, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_mknod(dfs_sys_mt, dir3, S_IFDIR | S_IWUSR | S_IRUSR,
			   0, 0);
	assert_int_equal(rc, 0);
	rc = dfs_sys_mknod(dfs_sys_mt, file1, S_IFREG | S_IWUSR | S_IRUSR,
			   0, 0);
	assert_int_equal(rc, 0);

	print_message("Removing tree with remove(force)\n");
	rc = dfs_sys_remove(dfs_sys_mt, dir1, true, NULL);
	assert_int_equal(rc, 0);
}

static const struct CMUnitTest dfs_sys_unit_tests[] = {
	{ "DFS_SYS_UNIT_TEST1: DFS Sys mount / umount",
	  dfs_sys_test_mount, async_disable, test_case_teardown},
	{ "DFS_SYS_UNIT_TEST2: DFS Sys create / remove",
	  dfs_sys_test_create_remove, async_disable, test_case_teardown},
};

static int
dfs_sys_setup(void **state)
{
	test_arg_t		*arg;
	int			rc = 0;

	rc = test_setup(state, SETUP_POOL_CONNECT, true, DEFAULT_POOL_SIZE,
			0, NULL);
	assert_int_equal(rc, 0);

	arg = *state;

	if (arg->myrank == 0) {
		uuid_generate(co_uuid);
		rc = dfs_cont_create(arg->pool.poh, co_uuid, NULL, &co_hdl,
				     NULL);
		assert_int_equal(rc, 0);
		printf("Created DFS Container "DF_UUIDF"\n", DP_UUID(co_uuid));
		rc = dfs_sys_mount(arg->pool.poh, co_hdl, O_RDWR, 0,
				   &dfs_sys_mt);
		assert_int_equal(rc, 0);
	}

	handle_share(&co_hdl, HANDLE_CO, arg->myrank, arg->pool.poh, 0);
	dfs_sys_test_share(arg->pool.poh, co_hdl, arg->myrank, 0, &dfs_sys_mt);

	return rc;
}

static int
dfs_sys_teardown(void **state)
{
	test_arg_t	*arg = *state;
	int		rc;

	rc = dfs_sys_umount(dfs_sys_mt);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(co_hdl, NULL);
	assert_rc_equal(rc, 0);

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = daos_cont_destroy(arg->pool.poh, co_uuid, 1, NULL);
		assert_rc_equal(rc, 0);
		print_message("Destroyed DFS Container "DF_UUIDF"\n",
			      DP_UUID(co_uuid));
	}
	MPI_Barrier(MPI_COMM_WORLD);

	return test_teardown(state);
}

int
run_dfs_sys_unit_test(int rank, int size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	rc = cmocka_run_group_tests_name("DAOS_FileSystem_DFS_Sys_Unit",
					 dfs_sys_unit_tests, dfs_sys_setup,
					 dfs_sys_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
