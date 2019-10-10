/**
 * (C) Copyright 2019 Intel Corporation.
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

int dfs_test_thread_nr		= 8;
#define DFS_TEST_MAX_THREAD_NR	(32)
pthread_t dfs_test_tid[DFS_TEST_MAX_THREAD_NR];

struct dfs_test_thread_arg {
	int			 thread_idx;
	pthread_barrier_t	*barrier;
	struct dfs_test_args	*dfs_arg;
};

struct dfs_test_thread_arg dfs_test_targ[DFS_TEST_MAX_THREAD_NR];

static int
dfs_test_file_gen(dfs_t *dfs, const char *name, daos_size_t chunk_size,
		  daos_size_t file_size)
{
	dfs_obj_t	*obj;
	char		*buf;
	d_sg_list_t	 sgl;
	d_iov_t		 iov;
	daos_size_t	 buf_size = 128 * 1024;
	daos_size_t	 io_size;
	daos_size_t	 size = 0;
	int		 rc = 0;

	D_ALLOC(buf, buf_size);
	if (buf == NULL)
		return -DER_NOMEM;
	d_iov_set(&iov, buf, buf_size);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = &iov;

	rc = dfs_open(dfs, NULL, name, S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, 0, chunk_size, NULL, &obj);
	assert_int_equal(rc, 0);

	while (size < file_size) {
		io_size = file_size - size;
		io_size = min(io_size, buf_size);

		sgl.sg_iovs[0].iov_len = io_size;
		dts_buf_render(buf, io_size);
		rc = dfs_write(dfs, obj, sgl, size);
		assert_int_equal(rc, 0);
		size += io_size;
	}

	rc = dfs_release(obj);
	D_FREE(buf);
	return rc;
}

static void
dfs_test_file_del(dfs_t *dfs, const char *name)
{
	int	rc;

	rc = dfs_remove(dfs, NULL, name, 0, NULL);
	assert_int_equal(rc, 0);
}

static void *
dfs_test_read_thread(void *arg)
{
	struct dfs_test_thread_arg	*targ = arg;
	struct dfs_test_args		*dfs_arg = targ->dfs_arg;
	dfs_obj_t			*obj;
	char				*buf;
	d_sg_list_t			 sgl;
	d_iov_t				 iov;
	daos_size_t			 buf_size;
	daos_size_t			 read_size, got_size;
	daos_size_t			 off = 0;
	int				 count = 0;
	int				 rc;

	print_message("dfs_test_read_thread %d\n", targ->thread_idx);

	buf_size = dfs_arg->stride;
	D_ALLOC(buf, buf_size);
	D_ASSERT(buf != NULL);
	d_iov_set(&iov, buf, buf_size);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = &iov;

	pthread_barrier_wait(targ->barrier);
	rc = dfs_open(dfs_arg->dfs, NULL, dfs_arg->name, S_IFREG, O_RDONLY,
		      0, 0, NULL, &obj);
	print_message("dfs_test_read_thread %d, dfs_open rc %d.\n",
		      targ->thread_idx, rc);
	assert_int_equal(rc, 0);

	off = targ->thread_idx * dfs_arg->stride;
	while (off < dfs_arg->total_size) {
		read_size = min(dfs_arg->total_size - off, dfs_arg->stride);
		sgl.sg_iovs[0].iov_len = read_size;

		if (count % 10 == 0)
		print_message("thread %d try to read off %d, size %d......\n",
			      targ->thread_idx, (int)off, (int)read_size);
		rc = dfs_read(dfs_arg->dfs, obj, sgl, off, &got_size);
		if (count++ % 10 == 0)
		print_message("thread %d read done rc %d, got_size %d.\n",
			      targ->thread_idx, rc, (int)got_size);
		assert_int_equal(rc, 0);
		assert_int_equal(read_size, got_size);
		off += dfs_arg->stride * dfs_test_thread_nr;
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
	struct dfs_test_args	*dfs_arg = &arg->dfs_args;
	daos_size_t		 chunk_size = 64;
	daos_size_t		 file_size = 25600;
	pthread_barrier_t	 barrier;
	char			*name = "tmp_file";
	int			 i;
	int			 rc;

	rc = dfs_test_file_gen(dfs_arg->dfs, name, chunk_size, file_size);
	assert_int_equal(rc, 0);

	/* usr barrier to all threads start at the same time and start
	 * concurrent test.
	 */
	pthread_barrier_init(&barrier, NULL, dfs_test_thread_nr + 1);
	dfs_arg->total_size = file_size;
	dfs_arg->stride = 77;
	dfs_arg->name = name;
	for (i = 0; i < dfs_test_thread_nr; i++) {
		dfs_test_targ[i].thread_idx = i;
		dfs_test_targ[i].dfs_arg = dfs_arg;
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

	dfs_test_file_del(dfs_arg->dfs, name);
}

static const struct CMUnitTest dfs_tests[] = {
	{ "DFS_TEST1: multi-threads read shared file",
	  dfs_test_read_shared_file, async_disable, test_case_teardown},
};

static int
dfs_setup(void **state)
{
	test_arg_t		*arg;
	struct dfs_test_args	*dfs_arg;
	dfs_t			*dfs = NULL;
	int			 rc = 0;

	rc = test_setup(state, SETUP_POOL_CONNECT, true, DEFAULT_POOL_SIZE,
			NULL);
	assert_int_equal(rc, 0);

	arg = *state;
	dfs_arg = &arg->dfs_args;
	arg->cont_for_dfs = true;

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_mount(arg->pool.poh, arg->coh, O_RDWR, &dfs);
	assert_int_equal(rc, 0);
	dfs_arg->dfs = dfs;




	return rc;
}

static int
dfs_teardown(void **state)
{
	test_arg_t		*arg = *state;
	struct dfs_test_args	*dfs_arg = &arg->dfs_args;
	int			 rc;

	if (dfs_arg->dfs != NULL) {
		rc = dfs_umount(dfs_arg->dfs);
		if (rc)
			D_ERROR("dfs_unmount failed %d.\n", rc);
	}


	return test_teardown(state);
}

int
run_dfs_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int	rc;

	MPI_Barrier(MPI_COMM_WORLD);
	rc = cmocka_run_group_tests_name("DAOS FileSystem (DFS) tests",
			dfs_tests, dfs_setup,
			dfs_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
