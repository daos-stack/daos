/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

#include "dfs_test.h"
#include <pthread.h>

/** global DFS mount used for all tests */
static uuid_t		co_uuid;
static daos_handle_t	co_hdl;
static dfs_t		*dfs_mt;

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

static void
dfs_test_lookup_hlpr(const char *name, mode_t mode)
{
	dfs_obj_t		*obj;
	mode_t			actual_mode;
	int			rc;

	rc = dfs_lookup(dfs_mt, name, O_RDWR | O_NOFOLLOW, &obj,
			&actual_mode, NULL);
	print_message("dfs_test_lookup_hlpr(\"%s\") = %d\n", name, rc);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);
	assert_int_equal(actual_mode & S_IFMT, mode & S_IFMT);
}

static void
dfs_test_lookup_rel_hlpr(dfs_obj_t *parent, const char *name, mode_t mode)
{
	dfs_obj_t		*obj;
	mode_t			actual_mode;
	int			rc;

	rc = dfs_lookup_rel(dfs_mt, parent, name, O_RDWR | O_NOFOLLOW, &obj,
			    &actual_mode, NULL);
	print_message("dfs_test_lookup_rel_hlpr(\"%s\") = %d\n", name, rc);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);
	assert_int_equal(actual_mode & S_IFMT, mode & S_IFMT);
}

static void
dfs_test_lookup(void **state)
{
	test_arg_t		*arg = *state;
	dfs_obj_t		*dir;
	dfs_obj_t		*obj;
	char			*filename_file1 = "file1";
	char			*path_file1 = "/file1";
	char			*filename_file2 = "file2";
	char			*path_file2 = "/dir1/file2";
	char			*path_file2_sym_up = "/sym1/dir2/../file2";
	char			*filename_dir1 = "dir1";
	char			*path_dir1 = "/dir1";
	char			*filename_dir2 = "dir2";
	char			*path_dir2 = "/dir1/dir2";
	char			*filename_sym1 = "sym1";
	char			*path_sym1 = "/sym1";
	char			*filename_sym2 = "sym2";
	char			*path_sym2 = "/dir1/sym2";
	mode_t			create_mode = S_IWUSR | S_IRUSR;
	int			create_flags = O_RDWR | O_CREAT | O_EXCL;
	int			rc;

	if (arg->myrank != 0)
		return;

	/** Create /file1 */
	rc = dfs_open(dfs_mt, NULL, filename_file1, create_mode | S_IFREG,
		      create_flags, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	dfs_test_lookup_hlpr(path_file1, S_IFREG);
	dfs_test_lookup_rel_hlpr(NULL, filename_file1, S_IFREG);

	/** Create /sym1 -> dir1 */
	rc = dfs_open(dfs_mt, NULL, filename_sym1, create_mode | S_IFLNK,
		      create_flags, 0, 0, filename_dir1, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	dfs_test_lookup_hlpr(path_sym1, S_IFLNK);
	dfs_test_lookup_rel_hlpr(NULL, filename_sym1, S_IFLNK);

	/** Create /dir1 */
	rc = dfs_open(dfs_mt, NULL, filename_dir1, create_mode | S_IFDIR,
		      create_flags, 0, 0, NULL, &dir);
	assert_int_equal(rc, 0);

	dfs_test_lookup_hlpr(path_dir1, S_IFDIR);
	dfs_test_lookup_rel_hlpr(NULL, filename_dir1, S_IFDIR);

	/** Create /dir1/dir2 */
	rc = dfs_open(dfs_mt, dir, filename_dir2, create_mode | S_IFDIR,
		      create_flags, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	dfs_test_lookup_hlpr(path_dir2, S_IFDIR);
	dfs_test_lookup_rel_hlpr(dir, filename_dir2, S_IFDIR);

	/** Create /dir1/file2 */
	rc = dfs_open(dfs_mt, dir, filename_file2, create_mode | S_IFREG,
		      create_flags, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	dfs_test_lookup_hlpr(path_file2, S_IFREG);
	dfs_test_lookup_rel_hlpr(dir, filename_file2, S_IFREG);
	dfs_test_lookup_hlpr(path_file2_sym_up, S_IFREG);

	/** Create /dir1/sym2 -> file2 */
	rc = dfs_open(dfs_mt, dir, filename_sym2, create_mode | S_IFLNK,
		      create_flags, 0, 0, filename_file2, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	dfs_test_lookup_hlpr(path_sym2, S_IFLNK);
	dfs_test_lookup_rel_hlpr(dir, filename_sym2, S_IFLNK);

	/** Close dir1 */
	rc = dfs_release(dir);
	assert_int_equal(rc, 0);
}

static void
dfs_test_syml(void **state)
{
	test_arg_t		*arg = *state;
	dfs_obj_t		*sym;
	char			*filename = "syml_file";
	char			*val = "SYMLINK VAL 1";
	char			tmp_buf[64];
	struct stat		stbuf;
	daos_size_t		size = 0;
	int			rc;

	if (arg->myrank != 0)
		goto syml_stat;

	rc = dfs_open(dfs_mt, NULL, filename, S_IFLNK | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, val, &sym);
	assert_int_equal(rc, 0);

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
	MPI_Barrier(MPI_COMM_WORLD);
	rc = dfs_stat(dfs_mt, NULL, filename, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_size, strlen(val));
	MPI_Barrier(MPI_COMM_WORLD);
}

static void
dfs_test_syml_follow_hlpr(const char *name, mode_t mode)
{
	dfs_obj_t	*obj;
	mode_t		actual_mode;
	char		path[64];
	int		rc;

	strncpy(path, "/", 2);
	strncat(path, name, 62);

	/** O_NOFOLLOW should open the link itself */
	rc = dfs_lookup(dfs_mt, path, O_RDWR | O_NOFOLLOW, &obj,
			&actual_mode, NULL);
	print_message("dfs_test_syml_follow(\"%s\", O_NOFOLLOW) = %d\n",
		      path, rc);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);
	assert_true(S_ISLNK(actual_mode));

	/** O_NOFOLLOW should open the link itself */
	rc = dfs_lookup_rel(dfs_mt, NULL, name, O_RDWR | O_NOFOLLOW, &obj,
			    &actual_mode, NULL);
	print_message("dfs_test_syml_follow_rel(\"%s\", O_NOFOLLOW) = %d\n",
		      name, rc);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);
	assert_true(S_ISLNK(actual_mode));

	/** Default should follow the link */
	rc = dfs_lookup(dfs_mt, path, O_RDWR, &obj,
			&actual_mode, NULL);
	print_message("dfs_test_syml_follow(\"%s\") = %d\n", path, rc);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);
	assert_int_equal(actual_mode & S_IFMT, mode & S_IFMT);

	/** Default should follow the link */
	rc = dfs_lookup_rel(dfs_mt, NULL, name, O_RDWR, &obj,
			    &actual_mode, NULL);
	print_message("dfs_test_syml_follow_rel(\"%s\") = %d\n", name, rc);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);
	assert_int_equal(actual_mode & S_IFMT, mode & S_IFMT);

	/** Default access should follow the link */
	rc = dfs_access(dfs_mt, NULL, name, R_OK | W_OK);
	print_message("dfs_test_syml_follow_access(\"%s\") = %d\n", name, rc);
	assert_int_equal(rc, 0);
}

static void
dfs_test_syml_follow(void **state)
{
	test_arg_t		*arg = *state;
	dfs_obj_t		*obj;
	char			*name_file = "reg_file";
	char			*name_dir = "test_dir";
	char			*name_sym_to_file = "sym_to_file";
	char			*name_sym_to_dir = "sym_to_dir";
	char			*name_sym_to_sym = "sym_to_sym";
	char			*name_sym_to_self = "sym_to_self";
	char			*path_sym_to_self = "/sym_to_self";
	int			rc;

	if (arg->myrank != 0)
		return;

	/** Create /file */
	rc = dfs_open(dfs_mt, NULL, name_file, S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	/** Create /sym_to_file -> file */
	rc = dfs_open(dfs_mt, NULL, name_sym_to_file, S_IFLNK,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, name_file, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	dfs_test_syml_follow_hlpr(name_sym_to_file, S_IFREG);

	/** Create /dir */
	rc = dfs_open(dfs_mt, NULL, name_dir, S_IFDIR | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	/** Create /sym_to_dir -> dir */
	rc = dfs_open(dfs_mt, NULL, name_sym_to_dir, S_IFLNK,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, name_dir, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	dfs_test_syml_follow_hlpr(name_sym_to_dir, S_IFDIR);

	/** Create /sym_to_sym -> sym_to_file */
	rc = dfs_open(dfs_mt, NULL, name_sym_to_sym, S_IFLNK,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, name_sym_to_file, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	dfs_test_syml_follow_hlpr(name_sym_to_sym, S_IFREG);

	/** Create /sym_to_self -> sym_to_self */
	rc = dfs_open(dfs_mt, NULL, name_sym_to_self, S_IFLNK,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, name_sym_to_self, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	/* Lookup on link with a loop should return ELOOP */
	rc = dfs_lookup(dfs_mt, path_sym_to_self, O_RDWR, &obj, NULL, NULL);
	print_message("dfs_test_syml_follow_eloop(\"%s\") = %d\n",
		      path_sym_to_self, rc);
	assert_int_equal(rc, ELOOP);

	rc = dfs_lookup_rel(dfs_mt, NULL, name_sym_to_self, O_RDWR, &obj,
			    NULL, NULL);
	print_message("dfs_test_syml_follow_eloop_rel(\"%s\") = %d\n",
		      name_sym_to_self, rc);
	assert_int_equal(rc, ELOOP);
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

static const struct CMUnitTest dfs_unit_tests[] = {
	{ "DFS_UNIT_TEST1: DFS mount / umount",
	  dfs_test_mount, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST2: DFS lookup / lookup_rel",
	  dfs_test_lookup, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST3: Simple Symlinks",
	  dfs_test_syml, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST4: Symlinks with / without O_NOFOLLOW",
	  dfs_test_syml_follow, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST5: multi-threads read shared file",
	  dfs_test_read_shared_file, async_disable, test_case_teardown},
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
		print_message("Destroyed DFS Container "DF_UUIDF"\n",
			      DP_UUID(co_uuid));
	}
	MPI_Barrier(MPI_COMM_WORLD);

	return test_teardown(state);
}

int
run_dfs_unit_test(int rank, int size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	rc = cmocka_run_group_tests_name("DAOS FileSystem (DFS) unit tests",
					 dfs_unit_tests, dfs_setup,
					 dfs_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
