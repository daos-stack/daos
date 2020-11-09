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
#define D_LOGFAC	DD_FAC(tests)

#include "dfs_test.h"

/** global DFS mount used for all tests */
static uuid_t		co_uuid;
static daos_handle_t	co_hdl;
static dfs_t		*dfs_mt;

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
dfs_test_cond(void **state)
{
	test_arg_t		*arg = *state;
	dfs_obj_t		*file;
	char			*filename = "cond_testfile";
	char			*dirname = "cond_testdir";
	int			rc, op_rc;

	if (arg->myrank == 0)
		print_message("All ranks create the same file with O_EXCL\n");
	MPI_Barrier(MPI_COMM_WORLD);
	op_rc = dfs_open(dfs_mt, NULL, filename, S_IFREG | S_IWUSR | S_IRUSR,
			 O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file);
	rc = check_one_success(op_rc, EEXIST, MPI_COMM_WORLD);
	assert_int_equal(rc, 0);
	if (op_rc == 0) {
		rc = dfs_release(file);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	if (arg->myrank == 0)
		print_message("All ranks unlink the same file\n");
	MPI_Barrier(MPI_COMM_WORLD);
	op_rc = dfs_remove(dfs_mt, NULL, filename, true, NULL);
	rc = check_one_success(op_rc, ENOENT, MPI_COMM_WORLD);
	if (rc)
		print_error("Failed concurrent file unlink\n");
	assert_int_equal(rc, 0);
	MPI_Barrier(MPI_COMM_WORLD);

	if (arg->myrank == 0)
		print_message("All ranks create the same directory\n");
	MPI_Barrier(MPI_COMM_WORLD);
	op_rc = dfs_mkdir(dfs_mt, NULL, dirname, S_IWUSR | S_IRUSR, 0);
	rc = check_one_success(op_rc, EEXIST, MPI_COMM_WORLD);
	if (rc)
		print_error("Failed concurrent dir creation\n");
	assert_int_equal(rc, 0);
	MPI_Barrier(MPI_COMM_WORLD);

	if (arg->myrank == 0)
		print_message("All ranks remove the same directory\n");
	MPI_Barrier(MPI_COMM_WORLD);
	op_rc = dfs_remove(dfs_mt, NULL, dirname, true, NULL);
	rc = check_one_success(op_rc, ENOENT, MPI_COMM_WORLD);
	if (rc)
		print_error("Failed concurrent rmdir\n");
	assert_int_equal(rc, 0);
	MPI_Barrier(MPI_COMM_WORLD);

	/** test atomic rename with DFS DTX mode */
	bool use_dtx;

	d_getenv_bool("DFS_USE_DTX", &use_dtx);
	if (!use_dtx)
		return;
	if (arg->myrank == 0) {
		print_message("All ranks rename the same file\n");
		rc = dfs_open(dfs_mt, NULL, filename,
			      S_IFREG | S_IWUSR | S_IRUSR,
			      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file);
		if (rc)
			print_error("Failed creating file for rename\n");
		rc = dfs_release(file);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	char newfilename[1024];

	sprintf(newfilename, "%s_new.%d", filename, arg->myrank);
	op_rc = dfs_move(dfs_mt, NULL, filename, NULL, newfilename, NULL);
	rc = check_one_success(op_rc, ENOENT, MPI_COMM_WORLD);
	if (rc)
		print_error("Failed concurrent rename\n");
	assert_int_equal(rc, 0);
	MPI_Barrier(MPI_COMM_WORLD);
}

static const struct CMUnitTest dfs_par_tests[] = {
	{ "DFS_PAR_TEST1: Conditional OPs",
	  dfs_test_cond, async_disable, test_case_teardown},
};

static int
dfs_setup(void **state)
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
	test_arg_t	*arg = *state;
	int		rc;

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
run_dfs_par_test(int rank, int size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	rc = cmocka_run_group_tests_name("DAOS FileSystem (DFS) parallel tests",
					 dfs_par_tests, dfs_setup,
					 dfs_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
