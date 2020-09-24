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

#include "daos_iotest.h"
#include <pthread.h>
#include <daos_fs.h>

/** global DFS mount used for all tests */
static uuid_t		co_uuid;
static daos_handle_t	co_hdl;
static dfs_t		*dfs_mt;

static void
dfs_share(daos_handle_t poh, daos_handle_t coh, int rank, dfs_t **dfs)
{
	d_iov_t	ghdl = { NULL, 0, 0 };
	int	rc;

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
	rc = MPI_Bcast(ghdl.iov_buf, ghdl.iov_len, MPI_BYTE, 0, MPI_COMM_WORLD);
	assert_int_equal(rc, MPI_SUCCESS);

	if (rank != 0) {
		/** unpack global handle */
		rc = dfs_global2local(poh, coh, 0, ghdl, dfs);
		assert_int_equal(rc, 0);
	}

	D_FREE(ghdl.iov_buf);

	MPI_Barrier(MPI_COMM_WORLD);
}

static void
dfs_obj_share(dfs_t *dfs, int flags, int rank, dfs_obj_t **obj)
{
	d_iov_t	ghdl = { NULL, 0, 0 };
	int	rc;

	if (rank == 0) {
		/** fetch size of global handle */
		rc = dfs_obj_local2global(dfs, *obj, &ghdl);
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
		rc = dfs_obj_local2global(dfs, *obj, &ghdl);
		assert_int_equal(rc, 0);
	}

	/** broadcast global handle to all peers */
	rc = MPI_Bcast(ghdl.iov_buf, ghdl.iov_len, MPI_BYTE, 0, MPI_COMM_WORLD);
	assert_int_equal(rc, MPI_SUCCESS);

	if (rank != 0) {
		/** unpack global handle */
		rc = dfs_obj_global2local(dfs, flags, ghdl, obj);
		assert_int_equal(rc, 0);
	}

	D_FREE(ghdl.iov_buf);
	MPI_Barrier(MPI_COMM_WORLD);
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
}

static int
dfs_test_file_gen(const char *name, daos_size_t chunk_size,
		  daos_size_t file_size)
{
	dfs_obj_t	*obj;
	char		*buf;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	daos_size_t	buf_size = 128 * 1024;
	daos_size_t	io_size;
	daos_size_t	size = 0;
	int		rc = 0;

	D_ALLOC(buf, buf_size);
	if (buf == NULL)
		return -DER_NOMEM;
	d_iov_set(&iov, buf, buf_size);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = &iov;

	rc = dfs_open(dfs_mt, NULL, name, S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, OC_S1, chunk_size, NULL, &obj);
	assert_int_equal(rc, 0);

	while (size < file_size) {
		io_size = file_size - size;
		io_size = min(io_size, buf_size);

		sgl.sg_iovs[0].iov_len = io_size;
		dts_buf_render(buf, io_size);
		rc = dfs_write(dfs_mt, obj, &sgl, size, NULL);
		assert_int_equal(rc, 0);
		size += io_size;
	}

	rc = dfs_release(obj);
	D_FREE(buf);
	return rc;
}

static void
dfs_test_file_del(const char *name)
{
	int	rc;

	rc = dfs_remove(dfs_mt, NULL, name, 0, NULL);
	assert_int_equal(rc, 0);
}

int dfs_test_thread_nr		= 8;
#define DFS_TEST_MAX_THREAD_NR	(32)
pthread_t dfs_test_tid[DFS_TEST_MAX_THREAD_NR];
struct dfs_test_thread_arg {
	int			thread_idx;
	pthread_barrier_t	*barrier;
	char			*name;
	daos_size_t		total_size;
	daos_size_t		stride;
};

struct dfs_test_thread_arg dfs_test_targ[DFS_TEST_MAX_THREAD_NR];

static void *
dfs_test_read_thread(void *arg)
{
	struct dfs_test_thread_arg	*targ = arg;
	dfs_obj_t			*obj;
	char				*buf;
	d_sg_list_t			sgl;
	d_iov_t				iov;
	daos_size_t			buf_size;
	daos_size_t			read_size, got_size;
	daos_size_t			off = 0;
	int				rc;

	print_message("dfs_test_read_thread %d\n", targ->thread_idx);

	buf_size = targ->stride;
	D_ALLOC(buf, buf_size);
	D_ASSERT(buf != NULL);
	d_iov_set(&iov, buf, buf_size);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = &iov;

	pthread_barrier_wait(targ->barrier);
	rc = dfs_open(dfs_mt, NULL, targ->name, S_IFREG, O_RDONLY, 0, 0, NULL,
		      &obj);
	print_message("dfs_test_read_thread %d, dfs_open rc %d.\n",
		      targ->thread_idx, rc);
	assert_int_equal(rc, 0);

	off = targ->thread_idx * targ->stride;
	while (off < targ->total_size) {
		read_size = min(targ->total_size - off, targ->stride);
		sgl.sg_iovs[0].iov_len = read_size;

		rc = dfs_read(dfs_mt, obj, &sgl, off, &got_size, NULL);
		if (rc || read_size != got_size)
			print_message("thread %d: rc %d, got_size %d.\n",
				      targ->thread_idx, rc, (int)got_size);
		assert_int_equal(rc, 0);
		assert_int_equal(read_size, got_size);
		off += targ->stride * dfs_test_thread_nr;
	}

	rc = dfs_release(obj);
	assert_int_equal(rc, 0);
	D_FREE(buf);

	print_message("dfs_test_read_thread %d succeed.\n", targ->thread_idx);
	pthread_exit(NULL);
}


static void
dfs_test_read_shared_file(void **state)
{
	test_arg_t		*arg = *state;
	daos_size_t		chunk_size = 64;
	daos_size_t		file_size = 256000;
	pthread_barrier_t	barrier;
	char			name[16];
	int			i;
	int			rc;

	MPI_Barrier(MPI_COMM_WORLD);

	sprintf(name, "MTA_file_%d\n", arg->myrank);
	rc = dfs_test_file_gen(name, chunk_size, file_size);
	assert_int_equal(rc, 0);

	/* usr barrier to all threads start at the same time and start
	 * concurrent test.
	 */
	pthread_barrier_init(&barrier, NULL, dfs_test_thread_nr + 1);
	for (i = 0; i < dfs_test_thread_nr; i++) {
		dfs_test_targ[i].thread_idx = i;
		dfs_test_targ[i].stride = 77;
		dfs_test_targ[i].name = name;
		dfs_test_targ[i].total_size = file_size;
		dfs_test_targ[i].barrier = &barrier;
		rc = pthread_create(&dfs_test_tid[i], NULL,
				    dfs_test_read_thread, &dfs_test_targ[i]);
		assert_int_equal(rc, 0);
	}

	pthread_barrier_wait(&barrier);
	for (i = 0; i < dfs_test_thread_nr; i++) {
		rc = pthread_join(dfs_test_tid[i], NULL);
		assert_int_equal(rc, 0);
	}

	dfs_test_file_del(name);
	MPI_Barrier(MPI_COMM_WORLD);
}

#define NUM_SEGS 10

static void
dfs_test_short_read(void **state)
{
	test_arg_t		*arg = *state;
	dfs_obj_t		*obj;
	daos_size_t		read_size;
	daos_size_t		chunk_size = 2000;
	daos_size_t		buf_size = 1024;
	int			*wbuf, *rbuf[NUM_SEGS];
	char			*name = "short_read_file";
	d_sg_list_t		wsgl, rsgl;
	d_iov_t			iov;
	int			i, rc;

	MPI_Barrier(MPI_COMM_WORLD);
	D_ALLOC(wbuf, buf_size);
	assert_non_null(wbuf);
	for (i = 0; i < buf_size/sizeof(int); i++)
		wbuf[i] = i+1;

	for (i = 0; i < NUM_SEGS; i++) {
		D_ALLOC_ARRAY(rbuf[i], buf_size + 100);
		assert_non_null(rbuf[i]);
	}

	d_iov_set(&iov, wbuf, buf_size);
	wsgl.sg_nr = 1;
	wsgl.sg_iovs = &iov;

	if (arg->myrank == 0) {
		rc = dfs_open(dfs_mt, NULL, name, S_IFREG | S_IWUSR | S_IRUSR,
			      O_RDWR | O_CREAT, 0, chunk_size, NULL, &obj);
		assert_int_equal(rc, 0);
	}

	dfs_obj_share(dfs_mt, O_RDONLY, arg->myrank, &obj);

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
		rc = dfs_punch(dfs_mt, obj, 1048576*2, 0);
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
		rc = dfs_write(dfs_mt, obj, &wsgl, 1048576*2, NULL);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	/** reading in between, even holes should not be a short read */
	rc = dfs_read(dfs_mt, obj, &rsgl, 1048576, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(read_size, buf_size * NUM_SEGS);

	rc = dfs_release(obj);
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0)
		dfs_test_file_del(name);

	D_FREE(wbuf);
	for (i = 0; i < NUM_SEGS; i++)
		D_FREE(rbuf[i]);
	D_FREE(rsgl.sg_iovs);
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
	assert_int_equal(size, strlen(val)+1);

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

	dfs_obj_share(dfs_mt, O_RDONLY, arg->myrank, &obj);

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
			rc = dfs_write(dfs_mt, obj, &wsgl, i*2*buf_size, NULL);
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
		if (i%2 == 0) {
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
	rc = dfs_read(dfs_mt, obj, &rsgl, buf_size * (NUM_SEGS*2-3),
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

		for (i = 0; i < buf_size/64; i++) {
			d_iov_set(&iov, ptr, 64);
			wsgl.sg_nr = 1;
			wsgl.sg_iovs = &iov;

			rc = dfs_write(dfs_mt, obj, &wsgl, off, NULL);
			assert_int_equal(rc, 0);
			ptr += 64;
			off += 64*2;
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

	for (i = 0; i < (buf_size*2)/64; i++) {
		if (i == buf_size/64)
			rptr = rbuf[1];

		if (i%2 == 0) {
			rc = memcmp(rptr, wptr, 64);
			assert_int_equal(rc, 0);
			wptr += 64;
		} else {
			/** last block is beyond EOF */
			if (i == (buf_size*2)/64 - 1) {
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
	if (arg->myrank == 0)
		dfs_test_file_del(name);

	D_FREE(wbuf);
	D_FREE(zbuf);
	D_FREE(obuf);
	for (i = 0; i < NUM_SEGS; i++)
		D_FREE(rbuf[i]);
	D_FREE(rsgl.sg_iovs);
}

static const struct CMUnitTest dfs_tests[] = {
	{ "DFS_TEST1: DFS mount / umount",
	  dfs_test_mount, async_disable, test_case_teardown},
	{ "DFS_TEST2: DFS short reads",
	  dfs_test_short_read, async_disable, test_case_teardown},
	{ "DFS_TEST3: multi-threads read shared file",
	  dfs_test_read_shared_file, async_disable, test_case_teardown},
	{ "DFS_TEST4: Conditional OPs",
	  dfs_test_cond, async_disable, test_case_teardown},
	{ "DFS_TEST5: Simple Symlinks",
	  dfs_test_syml, async_disable, test_case_teardown},
	{ "DFS_TEST6: DFS hole management",
	  dfs_test_hole_mgmt, async_disable, test_case_teardown},
};

static int
dfs_setup(void **state)
{
	test_arg_t		*arg;
	int			rc = 0;

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
run_daos_fs_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	rc = run_daos_sub_tests("DAOS FileSystem (DFS) tests", dfs_tests,
				ARRAY_SIZE(dfs_tests), sub_tests,
				sub_tests_size, dfs_setup, dfs_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
