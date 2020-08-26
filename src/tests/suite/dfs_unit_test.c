/**
 * (C) Copyright 2019-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#include "daos_iotest.h"
#include <pthread.h>
#include <daos_fs.h>

/** global DFS mount used for all tests */
static uuid_t           co_uuid;
static daos_handle_t    co_hdl;
static dfs_t		*dfs_mt;

static void
dfs_share(daos_handle_t poh, daos_handle_t coh, int rank, dfs_t **dfs)
{
	d_iov_t ghdl = { NULL, 0, 0 };
	int     rc;

	if (rank == 0) {
		/** fetch size of global handle */
		rc = dfs_local2global(*dfs, &ghdl);
		assert_int_equal(rc, 0);
	}

	/** broadcast size of global handle to all peers */
	rc = MPI_Bcast(&ghdl.iov_buf_len, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
	assert_int_equal(rc, MPI_SUCCESS);

	/** allocate buffer for global pool handle */
	D_ALLOC(ghdl.iov_buf, ghdl.iov_buf_len);
	ghdl.iov_len = ghdl.iov_buf_len;

	if (rank == 0) {
		/** generate actual global handle to share with peer tasks */
		rc = dfs_local2global(*dfs, &ghdl);
		assert_int_equal(rc, 0);
	}

	/** broadcast global handle to all peers */
	rc = MPI_Bcast(ghdl.iov_buf, ghdl.iov_len, MPI_BYTE, 0, 
		       MPI_COMM_WORLD);
	assert_int_equal(rc, MPI_SUCCESS);

	if (rank != 0) {
		/** unpack global handle */
		rc = dfs_global2local(poh, coh, 0, ghdl, dfs);
		assert_int_equal(rc, 0);
	}

	D_FREE(ghdl.iov_buf);

	MPI_Barrier(MPI_COMM_WORLD);
}

static int
check_one_success(int rc, int err, MPI_Comm comm)
{
	int *rc_arr;
	int mpi_size, mpi_rank, i;
	int passed, expect_fail, failed;

	MPI_Comm_size(comm, &mpi_size);
	MPI_Comm_rank(comm, &mpi_rank);

	D_ALLOC_ARRAY(rc_arr, mpi_size);
	assert_non_null(rc_arr);

	MPI_Allgather(&rc, 1, MPI_INT, rc_arr, 1, MPI_INT, comm);
	passed = expect_fail = failed = 0;
	for (i = 0; i < mpi_size; i++) {
		if (rc_arr[i] == 0)
			passed++;
		else if (rc_arr[i] == err)
			expect_fail++;
		else
			failed++;
	}

	free(rc_arr);

	if (failed || passed != 1)
		return -1;
	if ((expect_fail + passed) != mpi_size)
		return -1;
	return 0;
}

static void
dfs_test_mount(void **state)
{
	test_arg_t		*arg = *state;
	uuid_t			cuuid;
	daos_cont_info_t	co_info;
	daos_handle_t		coh;
	dfs_t			*dfs;
	int			rc;

	if (arg->myrank != 0)
		return;

	/** create & open a non-posix container */
	uuid_generate(cuuid);
	rc = daos_cont_create(arg->pool.poh, cuuid, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("Created non-POSIX Container "DF_UUIDF"\n",
		      DP_UUID(cuuid));
	rc = daos_cont_open(arg->pool.poh, cuuid, DAOS_COO_RW,
			    &coh, &co_info, NULL);
	assert_int_equal(rc, 0);

	/** try to mount DFS on it, should fail. */
	rc = dfs_mount(arg->pool.poh, coh, O_RDWR, &dfs);
	assert_int_equal(rc, EINVAL);

	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, cuuid, 1, NULL);
	assert_int_equal(rc, 0);
	print_message("Destroyed non-POSIX Container "DF_UUIDF"\n",
		      DP_UUID(cuuid));

	/** create a DFS container with POSIX layout */
	rc = dfs_cont_create(arg->pool.poh, cuuid, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("Created POSIX Container "DF_UUIDF"\n", DP_UUID(cuuid));
	rc = daos_cont_open(arg->pool.poh, cuuid, DAOS_COO_RW,
			    &coh, &co_info, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_mount(arg->pool.poh, coh, O_RDWR, &dfs);
	assert_int_equal(rc, 0);

	rc = dfs_umount(dfs);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, cuuid, 1, NULL);
	assert_int_equal(rc, 0);
	print_message("Destroyed POSIX Container "DF_UUIDF"\n", DP_UUID(cuuid));

	/** create a DFS container with POSIX layout */
	rc = dfs_cont_create(arg->pool.poh, cuuid, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
        print_message("Created POSIX Container "DF_UUIDF"\n", DP_UUID(cuuid));
	rc = daos_cont_open(arg->pool.poh, cuuid, DAOS_COO_RW,
			    &coh, &co_info, NULL);
	assert_int_equal(rc, 0);
	print_message("Mounting readonly\n");
	rc = dfs_mount(arg->pool.poh, coh, O_RDONLY, &dfs);
	assert_int_equal(rc, 0);
	print_message("Unmounting readonly\n");
	rc = dfs_umount(dfs);
	assert_int_equal(rc, 0);
	print_message("Container closing\n");
	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);
	print_message("Container destroying\n");
	rc = daos_cont_destroy(arg->pool.poh, cuuid, 1, NULL);
	assert_int_equal(rc, 0);
	print_message("Destroyed POSIX Container "DF_UUIDF"\n", 
		      DP_UUID(cuuid));

	/** create a DFS container with POSIX layout */
	rc = dfs_cont_create(arg->pool.poh, cuuid, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("Created POSIX Container "DF_UUIDF"\n", DP_UUID(cuuid));
	rc = daos_cont_open(arg->pool.poh, cuuid, DAOS_COO_RW,
			    &coh, &co_info, NULL);
	assert_int_equal(rc, 0);
	print_message("Mounting wrong parameters\n");
	rc = dfs_mount(arg->pool.poh, coh, -1, &dfs);
	assert_int_equal(rc, EINVAL);
	print_message("Container closing\n");
	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);
	print_message("Container destroying\n");
	rc = daos_cont_destroy(arg->pool.poh, cuuid, 1, NULL);
	assert_int_equal(rc, 0);
	print_message("Destroyed POSIX Container "DF_UUIDF"\n", 
		      DP_UUID(cuuid));

        /** create a DFS container with POSIX layout */
	rc = dfs_cont_create(arg->pool.poh, cuuid, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("Created POSIX Container "DF_UUIDF"\n", DP_UUID(cuuid));
	rc = daos_cont_open(arg->pool.poh, cuuid, DAOS_COO_RW,
			    &coh, &co_info, NULL);
	assert_int_equal(rc, 0);
	print_message("Mounting NULL dfs\n");
	rc = dfs_mount(arg->pool.poh, coh, O_RDWR, /*&dfs*/ NULL);
	assert_int_equal(rc, EINVAL);
	print_message("Unmount NULL dfs\n");
	rc = dfs_umount(/*dfs*/ NULL);
	assert_int_equal(rc, EINVAL);
	print_message("Container closing\n");
	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);
	print_message("Container destroying\n");
	rc = daos_cont_destroy(arg->pool.poh, cuuid, 1, NULL);
	assert_int_equal(rc, 0);
	print_message("Destroyed POSIX Container "DF_UUIDF"\n", 
		      DP_UUID(cuuid));
}

static void
dfs_test_syml(void **state)
{
	dfs_obj_t		*sym;
	char			*filename = "syml_file";
	char			*val = "SYMLINK VAL 1";
	char			tmp_buf[64];
	struct stat		stbuf;
	daos_size_t		size = 0;
	int			rc, op_rc;

	op_rc = dfs_open(dfs_mt, NULL, filename, S_IFLNK | S_IWUSR | S_IRUSR,
			 O_RDWR | O_CREAT | O_EXCL, 0, 0, val, &sym);
	rc = check_one_success(op_rc, EEXIST, MPI_COMM_WORLD);
	assert_int_equal(rc, 0);
	if (op_rc != 0)
		goto syml_stat;

	rc = dfs_get_symlink_value(sym, NULL, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, strlen(val) + 1);

	rc = dfs_get_symlink_value(sym, tmp_buf, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, strlen(val) + 1);
	assert_string_equal(val, tmp_buf);

	rc = dfs_release(sym);
	assert_int_equal(rc, 0);

syml_stat:
	rc = dfs_stat(dfs_mt, NULL, filename, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_size, strlen(val));

	MPI_Barrier(MPI_COMM_WORLD);
}

static const struct CMUnitTest dfs_unit_tests[] = {
	{ "DFS_UNIT_TEST1: DFS mount / umount",
	  dfs_test_mount, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST2: Simple Symlinks",
	  dfs_test_syml,  async_disable, test_case_teardown},
};


static int
dfs_setup(void **state)
{
	test_arg_t              *arg;
	int                     rc = 0;

	rc = test_setup(state, SETUP_POOL_CONNECT, true, DEFAULT_POOL_SIZE,
			NULL);
	assert_int_equal(rc, 0);

	arg = *state;

	if (arg->myrank == 0) {
		uuid_generate(co_uuid);
		rc = dfs_cont_create(arg->pool.poh, co_uuid, NULL, &co_hdl,
				     &dfs_mt);
		assert_int_equal(rc, 0);
		printf("Created DFS Container "DF_UUIDF"\n", DP_UUID(co_uuid));
	}

	handle_share(&co_hdl, HANDLE_CO, arg->myrank, arg->pool.poh, 0);
	dfs_share(arg->pool.poh, co_hdl, arg->myrank, &dfs_mt);

	return rc;

}

static int
dfs_teardown(void **state)
{
	test_arg_t      *arg = *state;
	int             rc;

	rc = dfs_umount(dfs_mt);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(co_hdl, NULL);
	assert_int_equal(rc, 0);

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = daos_cont_destroy(arg->pool.poh, co_uuid, 1, NULL);
		assert_int_equal(rc, 0);
		printf("Destroyed DFS Container "DF_UUIDF"\n",
		       DP_UUID(co_uuid));
	}
	MPI_Barrier(MPI_COMM_WORLD);

	return test_teardown(state);
}


int
run_daos_fs_unit_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	rc = cmocka_run_group_tests_name("DAOS FileSystem (DFS) unit tests",
					 dfs_unit_tests, dfs_setup,
					 dfs_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
