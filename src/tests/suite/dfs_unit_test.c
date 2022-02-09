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
static dfs_t		*dfs_mt;

static void
dfs_test_mount(void **state)
{
	test_arg_t		*arg = *state;
	char			str[37];
	uuid_t			cuuid;
	daos_cont_info_t	co_info;
	daos_handle_t		coh;
	dfs_t			*dfs;
	int			rc;

	if (arg->myrank != 0)
		return;

	/** connect to DFS without calling dfs_init(), should fail. */
	rc = dfs_connect(arg->pool.pool_str, arg->group, "cont0", O_CREAT | O_RDWR, NULL, &dfs);
	assert_int_equal(rc, EACCES);

	rc = dfs_init();
	assert_int_equal(rc, 0);

	/** create & open a non-posix container */
	rc = daos_cont_create_with_label(arg->pool.poh, "non-posix-cont", NULL, &cuuid, NULL);
	assert_rc_equal(rc, 0);
	print_message("Created non-POSIX Container\n");
	uuid_unparse(cuuid, str);
	rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW, &coh, &co_info, NULL);
	assert_rc_equal(rc, 0);
	/** try to mount DFS on it, should fail. */
	rc = dfs_mount(arg->pool.poh, coh, O_RDWR, &dfs);
	assert_int_equal(rc, EINVAL);
	/** try to connect DFS, should fail. */
	rc = dfs_connect(arg->pool.pool_str, arg->group, "non-posix-cont", O_RDWR, NULL, &dfs);
	assert_int_equal(rc, EINVAL);
	/** close and destroy non posix container */
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, str, 0, NULL);
	assert_rc_equal(rc, 0);
	print_message("Destroyed non-POSIX Container "DF_UUIDF"\n", DP_UUID(cuuid));

	/** Connect to non existing container - should succeed as container will be created  */
	rc = dfs_connect(arg->pool.pool_str, arg->group, "cont0", O_CREAT | O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);
	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	/** try to open the container and mount it, should succeed */
	rc = daos_cont_open(arg->pool.poh, "cont0", DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);
	rc = dfs_mount(arg->pool.poh, coh, O_RDWR, &dfs);
	assert_int_equal(rc, 0);
	rc = dfs_umount(dfs);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	/** create a DFS container with an invalid label */
	rc = dfs_cont_create_with_label(arg->pool.poh, "invalid:-/label", NULL, &cuuid, NULL, NULL);
	assert_int_equal(rc, EINVAL);

	/** create a DFS container with a valid label and set uuid */
	rc = dfs_cont_create_with_label(arg->pool.poh, "cont1", NULL, &cuuid, NULL, NULL);
	assert_int_equal(rc, 0);
	/** open with label */
	rc = daos_cont_open(arg->pool.poh, "cont1", DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);
	/** mount */
	rc = dfs_mount(arg->pool.poh, coh, O_RDWR, &dfs);
	assert_int_equal(rc, 0);
	/** try to disconnect instead of mount - should fail */
	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, EINVAL);
	rc = dfs_umount(dfs);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	/** Connect and disconnect to DFS container */
	rc = dfs_connect(arg->pool.pool_str, arg->group, "cont1", O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);
	/** try to umount instead of disconnect - should fail */
	rc = dfs_umount(dfs);
	assert_int_equal(rc, EINVAL);
	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);

	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/** destroy the containers */
	rc = daos_cont_destroy(arg->pool.poh, cuuid, 0, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, "cont0", 0, NULL);
	assert_rc_equal(rc, 0);

	/** create a DFS container with a valid label, no uuid out */
	rc = dfs_cont_create_with_label(arg->pool.poh, "label1", NULL, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	/** destroy with label */
	rc = daos_cont_destroy(arg->pool.poh, "label1", 0, NULL);
	assert_rc_equal(rc, 0);

	/** create a DFS container with POSIX layout */
	rc = dfs_cont_create(arg->pool.poh, &cuuid, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("Created POSIX Container "DF_UUIDF"\n", DP_UUID(cuuid));
	uuid_unparse(cuuid, str);
	rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW, &coh, &co_info, NULL);
	assert_rc_equal(rc, 0);
	rc = dfs_mount(arg->pool.poh, coh, O_RDWR, &dfs);
	assert_int_equal(rc, 0);
	rc = dfs_umount(dfs);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, str, 0, NULL);
	assert_rc_equal(rc, 0);
	print_message("Destroyed POSIX Container "DF_UUIDF"\n", DP_UUID(cuuid));
}

static void
dfs_test_modes(void **state)
{
	test_arg_t		*arg = *state;
	char			str[37];
	uuid_t			cuuid;
	daos_cont_info_t	co_info;
	daos_handle_t		coh;
	dfs_attr_t		attr = {0};
	dfs_t			*dfs;
	int			rc;

	if (arg->myrank != 0)
		return;

	/** create a DFS container in Relaxed mode */
	attr.da_mode = DFS_RELAXED;
	rc = dfs_cont_create(arg->pool.poh, &cuuid, &attr, NULL, NULL);
	assert_int_equal(rc, 0);
	uuid_unparse(cuuid, str);
	rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW,
			    &coh, &co_info, NULL);
	assert_int_equal(rc, 0);
	/** mount in Relaxed mode should succeed */
	rc = dfs_mount(arg->pool.poh, coh, O_RDWR | DFS_RELAXED, &dfs);
	assert_int_equal(rc, 0);
	rc = dfs_umount(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_mount(arg->pool.poh, coh, O_RDONLY | DFS_RELAXED, &dfs);
	assert_int_equal(rc, 0);
	rc = dfs_umount(dfs);
	assert_int_equal(rc, 0);
	/** mount in Balanced mode should succeed */
	rc = dfs_mount(arg->pool.poh, coh, O_RDWR | DFS_BALANCED, &dfs);
	assert_int_equal(rc, 0);
	rc = dfs_umount(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_mount(arg->pool.poh, coh, O_RDONLY | DFS_BALANCED, &dfs);
	assert_int_equal(rc, 0);
	rc = dfs_umount(dfs);
	assert_int_equal(rc, 0);
	/** destroy */
	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, str, 1, NULL);
	assert_int_equal(rc, 0);

	/** create a DFS container in Balanced mode */
	attr.da_mode = DFS_BALANCED;
	rc = dfs_cont_create(arg->pool.poh, &cuuid, &attr, NULL, NULL);
	assert_int_equal(rc, 0);
	uuid_unparse(cuuid, str);
	rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW,
			    &coh, &co_info, NULL);
	assert_int_equal(rc, 0);
	/** mount in Relaxed mode should fail with EPERM */
	rc = dfs_mount(arg->pool.poh, coh, O_RDWR | DFS_RELAXED, &dfs);
	assert_int_equal(rc, EPERM);
	rc = dfs_mount(arg->pool.poh, coh, O_RDONLY | DFS_RELAXED, &dfs);
	assert_int_equal(rc, EPERM);
	/** mount in default mode should fail with EPERM */
	rc = dfs_mount(arg->pool.poh, coh, O_RDWR, &dfs);
	assert_int_equal(rc, EPERM);
	/** mount in Balanced mode should succeed */
	rc = dfs_mount(arg->pool.poh, coh, O_RDONLY | DFS_BALANCED, &dfs);
	assert_int_equal(rc, 0);
	rc = dfs_umount(dfs);
	assert_int_equal(rc, 0);
	/** destroy */
	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, str, 1, NULL);
	assert_int_equal(rc, 0);

	/** create a DFS container with no mode specified */
	rc = dfs_cont_create(arg->pool.poh, &cuuid, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	uuid_unparse(cuuid, str);
	rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW,
			    &coh, &co_info, NULL);
	assert_int_equal(rc, 0);
	/** mount in Relaxed mode should succeed */
	rc = dfs_mount(arg->pool.poh, coh, O_RDWR | DFS_RELAXED, &dfs);
	assert_int_equal(rc, 0);
	rc = dfs_umount(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_mount(arg->pool.poh, coh, O_RDONLY | DFS_RELAXED, &dfs);
	assert_int_equal(rc, 0);
	rc = dfs_umount(dfs);
	assert_int_equal(rc, 0);
	/** destroy */
	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, str, 1, NULL);
	assert_int_equal(rc, 0);
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
	struct stat		stbuf;
	int			create_flags = O_RDWR | O_CREAT | O_EXCL;
	int			rc;

	if (arg->myrank != 0)
		return;

	/** Create /file1 */
	rc = dfs_open(dfs_mt, NULL, filename_file1, create_mode | S_IFREG,
		      create_flags, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);

	/** try chmod to a dir, should fail */
	rc = dfs_chmod(dfs_mt, NULL, filename_file1, S_IFDIR);
	assert_int_equal(rc, EINVAL);

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

	/** try chmod to a symlink, should fail (since chmod resolves link) */
	rc = dfs_chmod(dfs_mt, NULL, filename_sym1, S_IFLNK);
	assert_int_equal(rc, EINVAL);

	/** chmod + IXUSR to dir1 */
	rc = dfs_chmod(dfs_mt, NULL, filename_sym1, create_mode | S_IXUSR);
	assert_int_equal(rc, 0);

	/** verify mode */
	rc = dfs_stat(dfs_mt, NULL, filename_dir1, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_mode, S_IFDIR | create_mode | S_IXUSR);

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
	struct stat	stbuf;
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

	rc = dfs_punch(dfs_mt, obj, 10, DFS_MAX_FSIZE);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_size, 10);

	/** test for overflow */
	rc = dfs_punch(dfs_mt, obj, 9, DFS_MAX_FSIZE-1);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_size, 9);

	rc = dfs_punch(dfs_mt, obj, 0, DFS_MAX_FSIZE);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_size, 0);

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
dfs_test_rm(const char *name)
{
	int	rc;

	rc = dfs_remove(dfs_mt, NULL, name, 0, NULL);
	assert_int_equal(rc, 0);
}

int dfs_test_thread_nr		= 8;
#define DFS_TEST_MAX_THREAD_NR	(16)
pthread_t dfs_test_tid[DFS_TEST_MAX_THREAD_NR];

struct dfs_test_thread_arg {
	int			thread_idx;
	pthread_barrier_t	*barrier;
	char			*name;
	char			*pool;
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
	rc = dfs_open(dfs_mt, NULL, targ->name, S_IFREG, O_RDONLY, 0, 0, NULL, &obj);
	print_message("dfs_test_read_thread %d, dfs_open rc %d.\n", targ->thread_idx, rc);
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

	sprintf(name, "MTA_file_%d", arg->myrank);
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

	dfs_test_rm(name);
	MPI_Barrier(MPI_COMM_WORLD);
}

static void
dfs_test_lookupx(void **state)
{
	test_arg_t		*arg = *state;
	dfs_obj_t		*obj;
	char			*dir1 = "xdir1", *dir2 = "xdir2";
	mode_t			create_mode = S_IWUSR | S_IRUSR;
	int			create_flags = O_RDWR | O_CREAT;
	char			*xnames[] = {"x1", "x2", "x3"};
	int			vals_in[] = {1, 2, 3};
	void			*vals_out[3];
	daos_size_t		*val_sizes;
	mode_t			mode;
	struct stat             stbuf;
	int			i;
	int			rc;

	if (arg->myrank != 0)
		return;

	/** Create dir1 */
	rc = dfs_open(dfs_mt, NULL, dir1, create_mode | S_IFDIR, create_flags,
		      0, 0, NULL, &obj);
	assert_int_equal(rc, 0);

	rc = dfs_setxattr(dfs_mt, obj, xnames[0], &vals_in[0], sizeof(int), 0);
	assert_int_equal(rc, 0);
	rc = dfs_setxattr(dfs_mt, obj, xnames[1], &vals_in[1], sizeof(int), 0);
	assert_int_equal(rc, 0);

	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	/** Create dir2 */
	rc = dfs_open(dfs_mt, NULL, dir2, create_mode | S_IFDIR, create_flags,
		      0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	D_ALLOC_ARRAY(val_sizes, 3);
	D_ASSERT(val_sizes != NULL);

	/** MSC - this is currently not allowed by the DAOS obj API */
#if 0
	/** lookup with xattr first without sink buffer for vals */
	rc = dfs_lookupx(dfs_mt, NULL, dir1, O_RDWR, &obj, &mode, &stbuf, 3,
			 xnames, NULL, val_sizes);
	assert_int_equal(rc, 0);
	assert_int_equal(val_sizes[0], sizeof(int));
	assert_int_equal(val_sizes[1], sizeof(int));
	assert_int_equal(val_sizes[2], 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);
#endif

	for (i = 0; i < 3; i++) {
		D_ALLOC(vals_out[i], sizeof(int));
		D_ASSERT(vals_out[i] != NULL);
		*((int *)vals_out[i]) = 5;
		val_sizes[i] = sizeof(int);
	}

	rc = dfs_lookupx(dfs_mt, NULL, dir1, O_RDWR, &obj, &mode, &stbuf, 3,
			 xnames, vals_out, val_sizes);
	assert_int_equal(rc, 0);
	assert_int_equal(val_sizes[0], sizeof(int));
	assert_int_equal(val_sizes[1], sizeof(int));
	assert_int_equal(val_sizes[2], 0);
	assert_int_equal(*((int *)vals_out[0]), vals_in[0]);
	assert_int_equal(*((int *)vals_out[1]), vals_in[1]);
	assert_int_equal(*((int *)vals_out[2]), 5);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	for (i = 0; i < 3; i++)
		*((int *)vals_out[i]) = 5;

	val_sizes[2] = sizeof(int);
	rc = dfs_lookupx(dfs_mt, NULL, dir2, O_RDWR, &obj, &mode, &stbuf, 3,
			 xnames, vals_out, val_sizes);
	assert_int_equal(rc, 0);
	assert_int_equal(val_sizes[0], 0);
	assert_int_equal(val_sizes[1], 0);
	assert_int_equal(val_sizes[2], 0);
	assert_int_equal(*((int *)vals_out[0]), 5);
	assert_int_equal(*((int *)vals_out[1]), 5);
	assert_int_equal(*((int *)vals_out[2]), 5);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	for (i = 0; i < 3; i++)
		D_FREE(vals_out[i]);
	D_FREE(val_sizes);
}

static void
dfs_test_io_error_code(void **state)
{
	test_arg_t	*arg = *state;
	dfs_obj_t	*file;
	daos_event_t	ev, *evp;
	daos_range_t	iod_rgs;
	dfs_iod_t	iod;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	char		buf[10];
	daos_size_t	read_size;
	int		rc;

	if (arg->myrank != 0)
		return;

	rc = dfs_open(dfs_mt, NULL, "io_error", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, 0, 0, NULL, &file);
	assert_int_equal(rc, 0);

	/*
	 * set an IOD that has writes more data than sgl to trigger error in
	 * array layer.
	 */
	iod.iod_nr = 1;
	iod_rgs.rg_idx = 0;
	iod_rgs.rg_len = 10;
	iod.iod_rgs = &iod_rgs;
	d_iov_set(&iov, buf, 5);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = &iov;

	/** Write */
	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}
	rc = dfs_writex(dfs_mt, file, &iod, &sgl, arg->async ? &ev : NULL);
	if (arg->async) {
		/** Wait for completion */
		rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
		assert_rc_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(evp->ev_error, EINVAL);

		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
	} else {
		assert_int_equal(rc, EINVAL);
	}

	/** Read */
	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}
	rc = dfs_readx(dfs_mt, file, &iod, &sgl, &read_size,
		       arg->async ? &ev : NULL);
	if (arg->async) {
		/** Wait for completion */
		rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
		assert_rc_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(evp->ev_error, EINVAL);

		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
	} else {
		assert_int_equal(rc, EINVAL);
	}

	rc = dfs_release(file);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "io_error", 0, NULL);
	assert_int_equal(rc, 0);
}

int dfs_test_rc[DFS_TEST_MAX_THREAD_NR];

static void *
dfs_test_mkdir_thread(void *arg)
{
	struct dfs_test_thread_arg	*targ = arg;
	int				rc;

	pthread_barrier_wait(targ->barrier);
	rc = dfs_mkdir(dfs_mt, NULL, targ->name, S_IFDIR, OC_S1);
	print_message("dfs_test_mkdir_thread %d, dfs_mkdir rc %d.\n", targ->thread_idx, rc);
	dfs_test_rc[targ->thread_idx] = rc;
	pthread_exit(NULL);
}

static void
dfs_test_mt_mkdir(void **state)
{
	test_arg_t		*arg = *state;
	char			name[16];
	pthread_barrier_t	barrier;
	int			i, one_success;
	int			rc;

	MPI_Barrier(MPI_COMM_WORLD);

	sprintf(name, "MTA_dir_%d", arg->myrank);

	/* usr barrier to all threads start at the same time and start
	 * concurrent test.
	 */
	pthread_barrier_init(&barrier, NULL, dfs_test_thread_nr + 1);
	for (i = 0; i < dfs_test_thread_nr; i++) {
		dfs_test_targ[i].thread_idx = i;
		dfs_test_targ[i].name = name;
		dfs_test_targ[i].barrier = &barrier;
		rc = pthread_create(&dfs_test_tid[i], NULL,
				    dfs_test_mkdir_thread, &dfs_test_targ[i]);
		assert_int_equal(rc, 0);
	}

	pthread_barrier_wait(&barrier);
	for (i = 0; i < dfs_test_thread_nr; i++) {
		rc = pthread_join(dfs_test_tid[i], NULL);
		assert_int_equal(rc, 0);
	}

	one_success = 0;
	for (i = 0; i < dfs_test_thread_nr; i++) {
		if (dfs_test_rc[i] == 0) {
			if (one_success == 0) {
				one_success++;
				continue;
			}
			print_error("mkdir succeeded on more than thread\n");
			assert_int_not_equal(dfs_test_rc[i], 0);
		}
		if (dfs_test_rc[i] != EEXIST)
			print_error("mkdir returned unexpected error: %d\n",
				    dfs_test_rc[i]);
		assert_int_equal(dfs_test_rc[i], EEXIST);
	}

	if (one_success != 1)
		print_error("all mkdirs failed, expected 1 to succeed\n");
	assert_int_equal(one_success, 1);

	dfs_test_rm(name);
	MPI_Barrier(MPI_COMM_WORLD);
}

static void
dfs_test_rename(void **state)
{
	test_arg_t		*arg = *state;
	dfs_obj_t		*obj1, *obj2;
	char			*f1 = "f1";
	char			*f2 = "f2";
	d_sg_list_t		sgl;
	d_iov_t			iov;
	char			buf[64];
	struct stat		stbuf;
	int			rc;

	if (arg->myrank != 0)
		return;

	rc = dfs_open(dfs_mt, NULL, f1, S_IFREG | S_IWUSR | S_IRUSR | S_IXUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &obj1);
	assert_int_equal(rc, 0);
	rc = dfs_open(dfs_mt, NULL, f2, S_IFREG | S_IWUSR | S_IRUSR | S_IXUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &obj2);
	assert_int_equal(rc, 0);

	d_iov_set(&iov, buf, 64);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = &iov;
	dts_buf_render(buf, 64);
	rc = dfs_write(dfs_mt, obj2, &sgl, 64, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_release(obj2);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj1);
	assert_int_equal(rc, 0);

	rc = dfs_stat(dfs_mt, NULL, f1, &stbuf);
	assert_int_equal(rc, 0);
	rc = dfs_stat(dfs_mt, NULL, f2, &stbuf);
	assert_int_equal(rc, 0);

	rc = dfs_chmod(dfs_mt, NULL, f1, S_IFREG | S_IRUSR | S_IWUSR);
	assert_int_equal(rc, 0);
	rc = dfs_chmod(dfs_mt, NULL, f2, S_IFREG | S_IRUSR | S_IWUSR | S_IXUSR);
	assert_int_equal(rc, 0);

	rc = dfs_stat(dfs_mt, NULL, f1, &stbuf);
	assert_int_equal(rc, 0);
	rc = dfs_stat(dfs_mt, NULL, f2, &stbuf);
	assert_int_equal(rc, 0);

	rc = dfs_move(dfs_mt, NULL, f2, NULL, f1, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_remove(dfs_mt, NULL, f1, 0, NULL);
	assert_int_equal(rc, 0);
}

static void
dfs_test_compat(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		uuid1;
	uuid_t		uuid2;
	daos_handle_t	coh;
	dfs_t		*dfs;
	int		rc;

	uuid_generate(uuid1);
	uuid_clear(uuid2);

	if (arg->myrank != 0)
		return;

	print_message("creating DFS container with set uuid "DF_UUIDF" ...\n", DP_UUID(uuid1));
	rc = dfs_cont_create(arg->pool.poh, uuid1, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("Created POSIX Container "DF_UUIDF"\n", DP_UUID(uuid1));
	rc = daos_cont_open(arg->pool.poh, uuid1, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);
	rc = dfs_mount(arg->pool.poh, coh, O_RDWR, &dfs);
	assert_int_equal(rc, 0);
	rc = dfs_umount(dfs);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, uuid1, 1, NULL);
	assert_rc_equal(rc, 0);
	print_message("Destroyed POSIX Container "DF_UUIDF"\n", DP_UUID(uuid1));

	print_message("creating DFS container with a uuid pointer (not set by caller) ...\n");
	rc = dfs_cont_create(arg->pool.poh, &uuid2, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("Created POSIX Container "DF_UUIDF"\n", DP_UUID(uuid2));
	rc = daos_cont_open(arg->pool.poh, uuid2, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);
	rc = dfs_mount(arg->pool.poh, coh, O_RDWR, &dfs);
	assert_int_equal(rc, 0);
	rc = dfs_umount(dfs);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, uuid2, 1, NULL);
	assert_rc_equal(rc, 0);
	print_message("Destroyed POSIX Container "DF_UUIDF"\n", DP_UUID(uuid2));

	print_message("creating DFS container with a NULL pointer, should fail ...\n");
	rc = dfs_cont_create(arg->pool.poh, NULL, NULL, &coh, &dfs);
	assert_int_equal(rc, EINVAL);
}

static void
dfs_test_handles(void **state)
{
	test_arg_t		*arg = *state;
	dfs_t			*dfs_l, *dfs_g;
	d_iov_t			ghdl = { NULL, 0, 0 };
	dfs_obj_t		*file;
	int			rc;

	if (arg->myrank != 0)
		return;

	rc = dfs_init();
	assert_int_equal(rc, 0);

	/** create and connect to DFS container */
	rc = dfs_connect(arg->pool.pool_str, arg->group, "cont0", O_CREAT | O_RDWR, NULL, &dfs_l);
	assert_int_equal(rc, 0);

	/** create a file with "local" handle */
	rc = dfs_open(dfs_l, NULL, "testfile", S_IFREG | S_IWUSR | S_IRUSR, O_RDWR | O_CREAT, 0, 0,
		      NULL, &file);
	assert_int_equal(rc, 0);
	dfs_release(file);

	rc = dfs_local2global_all(dfs_l, &ghdl);
	assert_int_equal(rc, 0);

	D_ALLOC(ghdl.iov_buf, ghdl.iov_buf_len);
	ghdl.iov_len = ghdl.iov_buf_len;

	rc = dfs_local2global_all(dfs_l, &ghdl);
	assert_int_equal(rc, 0);

	rc = dfs_global2local_all(O_RDWR, ghdl, &dfs_g);
	assert_int_equal(rc, 0);

	/** open the file with "global" handle */
	rc = dfs_open(dfs_g, NULL, "testfile", S_IFREG | S_IWUSR | S_IRUSR, O_RDWR, 0, 0, NULL,
		      &file);
	assert_int_equal(rc, 0);
	dfs_release(file);

	rc = dfs_disconnect(dfs_l);
	assert_int_equal(rc, 0);
	rc = dfs_disconnect(dfs_g);
	assert_int_equal(rc, 0);
	rc = dfs_fini();
	assert_int_equal(rc, 0);
	D_FREE(ghdl.iov_buf);
}

static void *
dfs_test_connect_thread(void *arg)
{
	struct dfs_test_thread_arg	*targ = arg;
	dfs_t				*dfs;
	int				rc;

	pthread_barrier_wait(targ->barrier);
	printf("Thread %d connecting to pool %s cont %s\n", targ->thread_idx, targ->pool,
	       targ->name);
	dfs_init();
	rc = dfs_connect(targ->pool, NULL, targ->name, O_CREAT | O_RDWR, NULL, &dfs);
	dfs_test_rc[targ->thread_idx] = rc;
	if (rc) {
		printf("Thread %d failed to connect %d\n", targ->thread_idx, rc);
		pthread_exit(NULL);
	}

	rc = dfs_disconnect(dfs);
	dfs_test_rc[targ->thread_idx] = rc;
	if (rc) {
		printf("Thread %d failed to disconnect %d\n", targ->thread_idx, rc);
		pthread_exit(NULL);
	}
	dfs_fini();
	pthread_exit(NULL);
}

static void
dfs_test_mt_connect(void **state)
{
	test_arg_t		*arg = *state;
	char			name[16];
	pthread_barrier_t	barrier;
	int			i;
	int			rc;

	MPI_Barrier(MPI_COMM_WORLD);

	sprintf(name, "MTA_cont_%d", arg->myrank);

	pthread_barrier_init(&barrier, NULL, dfs_test_thread_nr + 1);
	for (i = 0; i < dfs_test_thread_nr; i++) {
		dfs_test_targ[i].thread_idx = i;
		dfs_test_targ[i].name = name;
		dfs_test_targ[i].barrier = &barrier;
		dfs_test_targ[i].pool = arg->pool.pool_str;
		rc = pthread_create(&dfs_test_tid[i], NULL, dfs_test_connect_thread,
				    &dfs_test_targ[i]);
		assert_int_equal(rc, 0);
	}
	pthread_barrier_wait(&barrier);
	for (i = 0; i < dfs_test_thread_nr; i++)
		rc = pthread_join(dfs_test_tid[i], NULL);

	for (i = 0; i < dfs_test_thread_nr; i++)
		assert_int_equal(dfs_test_rc[i], 0);

	rc = daos_cont_destroy(arg->pool.poh, name, 0, NULL);
	assert_rc_equal(rc, 0);
	MPI_Barrier(MPI_COMM_WORLD);
}

static const struct CMUnitTest dfs_unit_tests[] = {
	{ "DFS_UNIT_TEST1: DFS mount / umount",
	  dfs_test_mount, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST2: DFS container modes",
	  dfs_test_modes, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST3: DFS lookup / lookup_rel",
	  dfs_test_lookup, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST4: Simple Symlinks",
	  dfs_test_syml, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST5: Symlinks with / without O_NOFOLLOW",
	  dfs_test_syml_follow, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST6: multi-threads read shared file",
	  dfs_test_read_shared_file, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST7: DFS lookupx",
	  dfs_test_lookupx, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST8: DFS IO sync error code",
	  dfs_test_io_error_code, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST9: DFS IO async error code",
	  dfs_test_io_error_code, async_enable, test_case_teardown},
	{ "DFS_UNIT_TEST10: multi-threads mkdir same dir",
	  dfs_test_mt_mkdir, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST11: Simple rename",
	  dfs_test_rename, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST12: DFS API compat",
	  dfs_test_compat, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST13: DFS l2g/g2l_all",
	  dfs_test_handles, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST14: multi-threads connect to same container",
	  dfs_test_mt_connect, async_disable, test_case_teardown},
};

static int
dfs_setup(void **state)
{
	test_arg_t		*arg;
	int			rc = 0;

	rc = test_setup(state, SETUP_POOL_CONNECT, true, DEFAULT_POOL_SIZE, 0, NULL);
	assert_int_equal(rc, 0);

	arg = *state;

	if (arg->myrank == 0) {
		rc = dfs_cont_create(arg->pool.poh, &co_uuid, NULL, &co_hdl, &dfs_mt);
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
	assert_rc_equal(rc, 0);

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		char str[37];

		uuid_unparse(co_uuid, str);
		rc = daos_cont_destroy(arg->pool.poh, str, 1, NULL);
		assert_rc_equal(rc, 0);
		print_message("Destroyed DFS Container "DF_UUIDF"\n", DP_UUID(co_uuid));
	}
	MPI_Barrier(MPI_COMM_WORLD);

	return test_teardown(state);
}

int
run_dfs_unit_test(int rank, int size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	rc = cmocka_run_group_tests_name("DAOS_FileSystem_DFS_Unit", dfs_unit_tests, dfs_setup,
					 dfs_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
