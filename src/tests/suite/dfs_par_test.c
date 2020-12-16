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

	/* Verify TX consistency semantics. */
	if (op_rc == 0 && arg->myrank == 0) {
		/* New name entry should have been removed. */
		rc = dfs_open(dfs_mt, NULL, filename,
			      S_IFREG | S_IWUSR | S_IRUSR, O_RDONLY,
			      0, 0, NULL, &file);
		if (rc != ENOENT)
			print_error("Open old name %s after rename got %d\n",
				    filename, rc);
		assert_int_equal(rc, ENOENT);
		dfs_release(file);

		/* New name entry should have been created. */
		rc = dfs_open(dfs_mt, NULL, newfilename,
			      S_IFREG | S_IWUSR | S_IRUSR, O_RDONLY,
			      0, 0, NULL, &file);
		if (rc != 0)
			print_error("Open new name %s after rename got %d\n",
				    newfilename, rc);
		assert_int_equal(rc, 0);
		dfs_release(file);
	}
}

#define NUM_SEGS 10

static void
dfs_test_short_read_internal(void **state, daos_oclass_id_t cid,
			     daos_size_t chunk_size, daos_size_t buf_size)
{
	test_arg_t		*arg = *state;
	dfs_obj_t		*obj;
	daos_size_t		read_size = 0;
	int			*wbuf, *rbuf[NUM_SEGS];
	char			*name = "short_read_file";
	d_sg_list_t		wsgl, rsgl;
	d_iov_t			iov;
	int			i, rc;

	MPI_Barrier(MPI_COMM_WORLD);
	D_ALLOC(wbuf, buf_size);
	assert_non_null(wbuf);
	for (i = 0; i < buf_size / sizeof(int); i++)
		wbuf[i] = i + 1;

	for (i = 0; i < NUM_SEGS; i++) {
		D_ALLOC_ARRAY(rbuf[i], buf_size + 100);
		assert_non_null(rbuf[i]);
	}

	d_iov_set(&iov, wbuf, buf_size);
	wsgl.sg_nr = 1;
	wsgl.sg_iovs = &iov;

	if (arg->myrank == 0) {
		rc = dfs_open(dfs_mt, NULL, name, S_IFREG | S_IWUSR | S_IRUSR,
			      O_RDWR | O_CREAT, cid, chunk_size, NULL, &obj);
		assert_int_equal(rc, 0);
	}

	dfs_test_obj_share(dfs_mt, O_RDONLY, arg->myrank, &obj);

	/** reading empty file should return 0 */
	rsgl.sg_nr = 1;
	d_iov_set(&iov, rbuf[0], buf_size);
	rsgl.sg_iovs = &iov;
	rc = dfs_read(dfs_mt, obj, &rsgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(read_size, 0);

	/** write strided pattern and check read size with segmented buffers */
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = dfs_write(dfs_mt, obj, &wsgl, 0, NULL);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	/** set contig mem location */
	rsgl.sg_nr = 1;
	d_iov_set(&iov, rbuf[0], buf_size + 100);
	rsgl.sg_iovs = &iov;
	rc = dfs_read(dfs_mt, obj, &rsgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(read_size, buf_size);

	/* reset write iov */
	d_iov_set(&iov, wbuf, buf_size);

	/** set strided memory location */
	rsgl.sg_nr = NUM_SEGS;
	D_ALLOC_ARRAY(rsgl.sg_iovs, NUM_SEGS);
	assert_non_null(rsgl.sg_iovs);
	for (i = 0; i < NUM_SEGS; i++)
		d_iov_set(&rsgl.sg_iovs[i], rbuf[i], buf_size);

	rc = dfs_read(dfs_mt, obj, &rsgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(read_size, buf_size);

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = dfs_write(dfs_mt, obj, &wsgl, 2 * buf_size, NULL);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	rc = dfs_read(dfs_mt, obj, &rsgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(read_size, buf_size * 3);

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = dfs_write(dfs_mt, obj, &wsgl, 5 * buf_size, NULL);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	rc = dfs_read(dfs_mt, obj, &rsgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(read_size, buf_size * 6);

	/** truncate the buffer to a large size, read should return all */
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = dfs_punch(dfs_mt, obj, 1048576 * 2, 0);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	rc = dfs_read(dfs_mt, obj, &rsgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(read_size, buf_size * NUM_SEGS);

	/** punch all the data, read should return 0 */
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = dfs_punch(dfs_mt, obj, 0, DFS_MAX_FSIZE);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	rc = dfs_read(dfs_mt, obj, &rsgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(read_size, 0);

	/** write to 2 chunks with a large gap in the middle */
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = dfs_write(dfs_mt, obj, &wsgl, 0, NULL);
		assert_int_equal(rc, 0);
		rc = dfs_write(dfs_mt, obj, &wsgl, 1048576 * 3, NULL);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	/** reading in between, even holes should not be a short read */
	rc = dfs_read(dfs_mt, obj, &rsgl, 1048576, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(read_size, buf_size * NUM_SEGS);

	rc = dfs_release(obj);
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = dfs_remove(dfs_mt, NULL, name, 0, NULL);
		assert_int_equal(rc, 0);
	}

	D_FREE(wbuf);
	for (i = 0; i < NUM_SEGS; i++)
		D_FREE(rbuf[i]);
	D_FREE(rsgl.sg_iovs);
}

static void
dfs_test_short_read(void **state)
{
	dfs_test_short_read_internal(state, 0, 2000, 1024);
}

static void
dfs_test_ec_short_read(void **state)
{
	test_arg_t	*arg = *state;

	if (!test_runable(arg, 6))
		return;

	/* less than 1 EC stripe */
	dfs_test_short_read_internal(state, DAOS_OC_EC_K4P2_L32K,
				     32 * 1024 * 8, 2000);

	/* partial EC stripe */
	dfs_test_short_read_internal(state, DAOS_OC_EC_K4P2_L32K,
				     32 * 1024 * 8, 32 * 1024 * 2);

	/* full EC stripe */
	dfs_test_short_read_internal(state, DAOS_OC_EC_K4P2_L32K,
				     32 * 1024 * 8, 32 * 1024 * 4);

	/* one full EC stripe + partial EC stripe */
	dfs_test_short_read_internal(state, DAOS_OC_EC_K4P2_L32K,
				     32 * 1024 * 8, 32 * 1024 * 6);

	/* 2 full stripe */
	dfs_test_short_read_internal(state, DAOS_OC_EC_K4P2_L32K,
				     32 * 1024 * 8, 32 * 1024 * 6);
}

static void
dfs_test_hole_mgmt(void **state)
{
	test_arg_t		*arg = *state;
	dfs_obj_t		*obj;
	daos_size_t		read_size;
	daos_size_t		chunk_size = 2000;
	daos_size_t		buf_size = 1024;
	char			*wbuf, *zbuf, *obuf, *rbuf[NUM_SEGS];
	char			*name = "short_read_file";
	d_sg_list_t		wsgl, rsgl;
	d_iov_t			iov;
	struct stat		stbuf;
	int			i, rc;

	MPI_Barrier(MPI_COMM_WORLD);
	D_ALLOC(wbuf, buf_size);
	assert_non_null(wbuf);
	memset(wbuf, 'c', buf_size);

	/** all 0 buf */
	D_ALLOC(zbuf, buf_size + 100);
	assert_non_null(zbuf);
	memset(zbuf, 0, buf_size + 100);

	/** buf with '-' data */
	D_ALLOC(obuf, buf_size + 100);
	assert_non_null(obuf);
	memset(obuf, '-', buf_size + 100);

	/** read buffers with orig data "-" */
	for (i = 0; i < NUM_SEGS; i++) {
		D_ALLOC(rbuf[i], buf_size + 100);
		assert_non_null(rbuf[i]);
		memset(rbuf[i], '-', buf_size + 100);
	}

	if (arg->myrank == 0) {
		rc = dfs_open(dfs_mt, NULL, name, S_IFREG | S_IWUSR | S_IRUSR,
			      O_RDWR | O_CREAT, 0, chunk_size, NULL, &obj);
		assert_int_equal(rc, 0);
	}

	dfs_test_obj_share(dfs_mt, O_RDONLY, arg->myrank, &obj);

	/** reading empty file should return 0 & not touch user buffer */
	rsgl.sg_nr = 1;
	d_iov_set(&iov, rbuf[0], buf_size);
	rsgl.sg_iovs = &iov;
	rc = dfs_read(dfs_mt, obj, &rsgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(read_size, 0);
	rc = memcmp(rbuf[0], obuf, buf_size);
	assert_int_equal(rc, 0);

	/** write 1 byte at a large offset */
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		d_iov_set(&iov, wbuf, 1);
		wsgl.sg_nr = 1;
		wsgl.sg_iovs = &iov;

		rc = dfs_write(dfs_mt, obj, &wsgl, 10485760, NULL);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	rc = dfs_ostat(dfs_mt, obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_size, 10485761);

	/** reading before the EOF should detect holes and set buf to 0 */
	d_iov_set(&iov, rbuf[0], buf_size);
	rc = dfs_read(dfs_mt, obj, &rsgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(read_size, buf_size);
	rc = memcmp(rbuf[0], zbuf, buf_size);
	assert_int_equal(rc, 0);

	/** reset */
	memset(rbuf[0], '-', buf_size + 100);

	MPI_Barrier(MPI_COMM_WORLD);
	/** truncate file back to 0 */
	if (arg->myrank == 0) {
		rc = dfs_punch(dfs_mt, obj, 0, DFS_MAX_FSIZE);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	rc = dfs_ostat(dfs_mt, obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_size, 0);

	/** reading before the EOF should detect holes and set buf to 0 */
	rc = dfs_read(dfs_mt, obj, &rsgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(read_size, 0);
	rc = memcmp(rbuf[0], obuf, buf_size);
	assert_int_equal(rc, 0);

	/** write a strided pattern, every 1k bytes */
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		d_iov_set(&iov, wbuf, buf_size);
		wsgl.sg_nr = 1;
		wsgl.sg_iovs = &iov;

		for (i = 0; i < NUM_SEGS; i++) {
			rc = dfs_write(dfs_mt, obj, &wsgl, i * 2 * buf_size,
				       NULL);
			assert_int_equal(rc, 0);
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);

	rc = dfs_ostat(dfs_mt, obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_size, buf_size * (NUM_SEGS * 2 - 1));

	/** read the first NUM_SEGS blocks. should get 1/2 data back */
	/** set strided memory location */
	rsgl.sg_nr = NUM_SEGS;
	D_ALLOC_ARRAY(rsgl.sg_iovs, NUM_SEGS);
	assert_non_null(rsgl.sg_iovs);
	for (i = 0; i < NUM_SEGS; i++)
		d_iov_set(&rsgl.sg_iovs[i], rbuf[i], buf_size);
	rc = dfs_read(dfs_mt, obj, &rsgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(read_size, buf_size * NUM_SEGS);
	/** should get written data every other block, and 0s in between */
	for (i = 0; i < NUM_SEGS; i++) {
		if (i % 2 == 0) {
			rc = memcmp(rbuf[i], wbuf, buf_size);
			assert_int_equal(rc, 0);
		} else {
			rc = memcmp(rbuf[i], zbuf, buf_size);
			assert_int_equal(rc, 0);
		}
	}

	/** reset */
	for (i = 0; i < NUM_SEGS; i++)
		memset(rbuf[i], '-', buf_size + 100);

	/** read last 2 blocks of file + the rest 8 blocks beyond EOF */
	rc = dfs_read(dfs_mt, obj, &rsgl, buf_size * (NUM_SEGS * 2 - 3),
		      &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(read_size, buf_size * 2);
	/** should get written data every other block, and 0s in between */
	for (i = 0; i < NUM_SEGS; i++) {
		if (i == 0) {
			/** first block is a hole */
			rc = memcmp(rbuf[i], zbuf, buf_size);
			assert_int_equal(rc, 0);
		} else if (i == 1) {
			/** second block is valid data */
			rc = memcmp(rbuf[i], wbuf, buf_size);
			assert_int_equal(rc, 0);
		} else {
			/** the rest are beyond EOF */
			rc = memcmp(rbuf[i], obuf, buf_size);
			assert_int_equal(rc, 0);
		}
	}

	/** reset */
	for (i = 0; i < NUM_SEGS; i++)
		memset(rbuf[i], '-', buf_size + 100);

	MPI_Barrier(MPI_COMM_WORLD);
	/** truncate file back to 0 */
	if (arg->myrank == 0) {
		rc = dfs_punch(dfs_mt, obj, 0, DFS_MAX_FSIZE);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	rc = dfs_ostat(dfs_mt, obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_size, 0);

	/** write strided 64 byte blocks */
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		char		*ptr = wbuf;
		daos_off_t	off = 0;

		for (i = 0; i < buf_size / 64; i++) {
			d_iov_set(&iov, ptr, 64);
			wsgl.sg_nr = 1;
			wsgl.sg_iovs = &iov;

			rc = dfs_write(dfs_mt, obj, &wsgl, off, NULL);
			assert_int_equal(rc, 0);
			ptr += 64;
			off += 64 * 2;
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);

	rc = dfs_ostat(dfs_mt, obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_size, buf_size * 2 - 64);

	/** read the first 2 blocks, should see a strided 64 block */
	for (i = 0; i < 2; i++)
		d_iov_set(&rsgl.sg_iovs[i], rbuf[i], buf_size);
	rsgl.sg_nr = 2;
	rc = dfs_read(dfs_mt, obj, &rsgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	/** should see a short read of the last 64 bytes */
	assert_int_equal(read_size, buf_size * 2 - 64);

	/** should get written data every other block, and 0s in between */
	char *wptr = wbuf;
	char *rptr = rbuf[0];

	for (i = 0; i < (buf_size * 2) / 64; i++) {
		if (i == buf_size / 64)
			rptr = rbuf[1];

		if (i % 2 == 0) {
			rc = memcmp(rptr, wptr, 64);
			assert_int_equal(rc, 0);
			wptr += 64;
		} else {
			/** last block is beyond EOF */
			if (i == (buf_size * 2) / 64 - 1) {
				rc = memcmp(rptr, obuf, 64);
				assert_int_equal(rc, 0);
			} else {
				rc = memcmp(rptr, zbuf, 64);
				assert_int_equal(rc, 0);
			}
		}
		rptr += 64;
	}

	rc = dfs_release(obj);
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = dfs_remove(dfs_mt, NULL, name, 0, NULL);
		assert_int_equal(rc, 0);
	}

	D_FREE(wbuf);
	D_FREE(zbuf);
	D_FREE(obuf);
	for (i = 0; i < NUM_SEGS; i++)
		D_FREE(rbuf[i]);
	D_FREE(rsgl.sg_iovs);
}

static const struct CMUnitTest dfs_par_tests[] = {
	{ "DFS_PAR_TEST1: Conditional OPs",
	  dfs_test_cond, async_disable, test_case_teardown},
	{ "DFS_PAR_TEST2: DFS short reads",
	  dfs_test_short_read, async_disable, test_case_teardown},
	{ "DFS_PAR_TEST3: DFS EC object short reads",
	  dfs_test_ec_short_read, async_disable, test_case_teardown},
	{ "DFS_PAR_TEST4: DFS hole management",
	  dfs_test_hole_mgmt, async_disable, test_case_teardown},
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
	dfs_test_share(arg->pool.poh, co_hdl, arg->myrank, &dfs_mt);

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
