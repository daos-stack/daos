/**
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

#include "dfs_test.h"
#include "dfs_internal.h"
#include <daos_types.h>
#include <daos/placement.h>
#include <pthread.h>

/** global DFS mount used for all tests */
static uuid_t		co_uuid;
static daos_handle_t	co_hdl;
static dfs_t		*dfs_mt;

static bool
check_ts(struct timespec l, struct timespec r)
{
	if (l.tv_sec == r.tv_sec)
		return l.tv_nsec < r.tv_nsec;
	else
		return l.tv_sec < r.tv_sec;
}

static void
dfs_test_mount(void **state)
{
	test_arg_t		*arg = *state;
	char			str[37];
	uuid_t			cuuid;
	daos_cont_info_t	co_info;
	daos_handle_t		coh;
	daos_handle_t		poh_tmp, coh_tmp;
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
	/** destroy the container while it's still cached (using dfs_connect) - should fail */
	rc = daos_cont_destroy(arg->pool.poh, "cont0", 0, NULL);
	assert_rc_equal(rc, -DER_BUSY);
	rc = dfs_destroy(arg->pool.pool_str, arg->group, "cont0", 0, NULL);
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
	assert_success(rc);

	/** Connect and disconnect to DFS container */
	rc = dfs_connect(arg->pool.pool_str, arg->group, "cont1", O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);
	/** try to umount instead of disconnect - should fail */
	rc = dfs_umount(dfs);
	assert_int_equal(rc, EINVAL);
	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	/** try to destroy container without force, using the daos API, should fail */
	rc = daos_cont_destroy(arg->pool.poh, "cont1", 0, NULL);
	assert_rc_equal(rc, -DER_BUSY);
	/** dfs_destroy will take the refcount and destroy */
	rc = dfs_destroy(arg->pool.pool_str, arg->group, "cont1", 0, NULL);
	assert_rc_equal(rc, 0);

	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/** create a DFS container with a valid label, no uuid out */
	rc = dfs_cont_create_with_label(arg->pool.poh, "label1", NULL, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	/** destroy with label */
	rc = daos_cont_destroy(arg->pool.poh, "label1", 0, NULL);
	assert_success(rc);

	/** create a DFS container with POSIX layout */
	rc = dfs_cont_create(arg->pool.poh, &cuuid, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("Created POSIX Container "DF_UUIDF"\n", DP_UUID(cuuid));
	uuid_unparse(cuuid, str);
	rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW, &coh, &co_info, NULL);
	assert_rc_equal(rc, 0);
	rc = dfs_mount(arg->pool.poh, coh, O_RDWR, &dfs);
	assert_int_equal(rc, 0);

	/** get/put poh and coh */
	print_message("Testing dfs_pool/cont_get/put\n");
	rc = dfs_pool_get(dfs, &poh_tmp);
	assert_int_equal(rc, 0);
	assert_int_equal(poh_tmp.cookie, arg->pool.poh.cookie);
	/** try to umount now, should fail */
	rc = dfs_umount(dfs);
	assert_int_equal(rc, EBUSY);
	rc = dfs_pool_put(dfs, poh_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_cont_get(dfs, &coh_tmp);
	assert_int_equal(rc, 0);
	assert_int_equal(coh_tmp.cookie, coh.cookie);
	/** try to umount now, should fail */
	rc = dfs_umount(dfs);
	assert_int_equal(rc, EBUSY);
	rc = dfs_cont_put(dfs, coh_tmp);
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
	rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW, &coh, &co_info, NULL);
	assert_success(rc);
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
	assert_success(rc);
	rc = daos_cont_destroy(arg->pool.poh, str, 0, NULL);
	assert_success(rc);

	/** create a DFS container in Balanced mode */
	attr.da_mode = DFS_BALANCED;
	rc = dfs_cont_create(arg->pool.poh, &cuuid, &attr, NULL, NULL);
	assert_int_equal(rc, 0);
	uuid_unparse(cuuid, str);
	rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW, &coh, &co_info, NULL);
	assert_success(rc);
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
	assert_success(rc);
	rc = daos_cont_destroy(arg->pool.poh, str, 0, NULL);
	assert_success(rc);

	/** create a DFS container with no mode specified */
	rc = dfs_cont_create(arg->pool.poh, &cuuid, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	uuid_unparse(cuuid, str);
	rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW, &coh, &co_info, NULL);
	assert_success(rc);
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
	assert_success(rc);
	rc = daos_cont_destroy(arg->pool.poh, str, 0, NULL);
	assert_success(rc);
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
	rc = dfs_ostatx(dfs_mt, obj, &stbuf, NULL);
	assert_int_equal(rc, 0);
	assert_true(S_ISLNK(stbuf.st_mode));
	assert_int_equal(stbuf.st_size, strlen(filename_dir1));
	memset(&stbuf, 0, sizeof(stbuf));
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	dfs_test_lookup_hlpr(path_sym1, S_IFLNK);
	dfs_test_lookup_rel_hlpr(NULL, filename_sym1, S_IFLNK);

	/** Create /dir1 */
	rc = dfs_open(dfs_mt, NULL, filename_dir1, create_mode | S_IFDIR,
		      create_flags, 0, 0, NULL, &dir);
	assert_int_equal(rc, 0);

	rc = dfs_ostatx(dfs_mt, dir, &stbuf, NULL);
	assert_int_equal(rc, 0);
	assert_true(S_ISDIR(stbuf.st_mode));

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
	rc = dfs_ostatx(dfs_mt, obj, &stbuf, NULL);
	assert_int_equal(rc, 0);
	assert_true(S_ISLNK(stbuf.st_mode));
	assert_int_equal(stbuf.st_size, strlen(filename_file2));
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

	rc = dfs_open_stat(dfs_mt, NULL, filename, S_IFLNK | S_IWUSR | S_IRUSR,
			   O_RDWR | O_CREAT | O_EXCL, 0, 0, val, &sym, &stbuf);
	assert_int_equal(rc, 0);

	/* symlink_value uses size plus space for the terminator, so it does not match stbuf */
	rc = dfs_get_symlink_value(sym, NULL, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, strlen(val) + 1);
	assert_int_equal(size, stbuf.st_size + 1);

	rc = dfs_get_symlink_value(sym, tmp_buf, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, strlen(val) + 1);
	assert_string_equal(val, tmp_buf);
	rc = dfs_release(sym);
	assert_int_equal(rc, 0);

syml_stat:
	par_barrier(PAR_COMM_WORLD);
	rc = dfs_stat(dfs_mt, NULL, filename, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_size, strlen(val));
	par_barrier(PAR_COMM_WORLD);
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
dfs_test_file_gen(const char *name, daos_size_t chunk_size, daos_oclass_id_t cid,
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
		      O_RDWR | O_CREAT, cid, chunk_size, NULL, &obj);
	assert_int_equal(rc, 0);

	rc = dfs_punch(dfs_mt, obj, 10, DFS_MAX_FSIZE);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_size, 10);

	/** test for overflow */
	rc = dfs_punch(dfs_mt, obj, 9, DFS_MAX_FSIZE - 1);
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

	par_barrier(PAR_COMM_WORLD);

	sprintf(name, "MTA_file_%d", arg->myrank);
	rc = dfs_test_file_gen(name, chunk_size, OC_S1, file_size);
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
	par_barrier(PAR_COMM_WORLD);
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

	par_barrier(PAR_COMM_WORLD);

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
	par_barrier(PAR_COMM_WORLD);
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
	struct timespec		prev_ts;
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

	rc = dfs_ostatx(dfs_mt, obj1, &stbuf, NULL);
	assert_int_equal(rc, 0);
	assert_true(stbuf.st_size == 0);
	prev_ts.tv_sec = stbuf.st_ctim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_ctim.tv_nsec;
	memset(&stbuf, 0, sizeof(stbuf));
	rc = dfs_ostatx(dfs_mt, obj2, &stbuf, NULL);
	assert_int_equal(rc, 0);
	assert_true(stbuf.st_size == 128);

	rc = dfs_chmod(dfs_mt, NULL, f1, S_IFREG | S_IRUSR | S_IWUSR);
	assert_int_equal(rc, 0);
	rc = dfs_chmod(dfs_mt, NULL, f2, S_IFREG | S_IRUSR | S_IWUSR | S_IXUSR);
	assert_int_equal(rc, 0);

	daos_event_t ev, *evp;

	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_size = 1234;
	rc = daos_event_init(&ev, arg->eq, NULL);
	rc = dfs_ostatx(dfs_mt, obj1, &stbuf, &ev);
	assert_int_equal(rc, 0);
	rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
	assert_rc_equal(rc, 1);
	assert_ptr_equal(evp, &ev);
	assert_int_equal(evp->ev_error, 0);
	rc = daos_event_fini(&ev);
	assert_rc_equal(rc, 0);
	/** check ctime updated */
	assert_true(check_ts(prev_ts, stbuf.st_ctim));
	assert_true(stbuf.st_size == 0);

	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_size = 1234;
	rc = daos_event_init(&ev, arg->eq, NULL);
	rc = dfs_ostatx(dfs_mt, obj2, &stbuf, &ev);
	assert_int_equal(rc, 0);
	rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
	assert_rc_equal(rc, 1);
	assert_ptr_equal(evp, &ev);
	assert_int_equal(evp->ev_error, 0);
	rc = daos_event_fini(&ev);
	assert_rc_equal(rc, 0);
	/** check ctime updated */
	assert_true(check_ts(prev_ts, stbuf.st_ctim));
	assert_true(stbuf.st_size == 128);

	memset(&stbuf, 0, sizeof(stbuf));
	rc = dfs_stat(dfs_mt, NULL, f2, &stbuf);
	assert_int_equal(rc, 0);

	rc = dfs_move(dfs_mt, NULL, f2, NULL, f1, NULL);
	assert_int_equal(rc, 0);

	/** try to stat obj1 corresponding to f1 which was removed, should fail. */
	rc = dfs_ostatx(dfs_mt, obj1, &stbuf, NULL);
	assert_int_equal(rc, ENOENT);
	rc = daos_event_init(&ev, arg->eq, NULL);
	rc = dfs_ostatx(dfs_mt, obj1, &stbuf, &ev);
	assert_int_equal(rc, 0);
	rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
	assert_rc_equal(rc, 1);
	assert_ptr_equal(evp, &ev);
	assert_int_equal(evp->ev_error, ENOENT);
	rc = daos_event_fini(&ev);
	assert_rc_equal(rc, 0);

	rc = dfs_remove(dfs_mt, NULL, f1, 0, NULL);
	assert_int_equal(rc, 0);

	/** try to stat obj2 corresponding to f2 which was renamed, should fail. */
	rc = dfs_ostatx(dfs_mt, obj2, &stbuf, NULL);
	assert_int_equal(rc, ENOENT);
	rc = daos_event_init(&ev, arg->eq, NULL);
	rc = dfs_ostatx(dfs_mt, obj2, &stbuf, &ev);
	assert_int_equal(rc, 0);
	rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
	assert_rc_equal(rc, 1);
	assert_ptr_equal(evp, &ev);
	assert_int_equal(evp->ev_error, ENOENT);
	rc = daos_event_fini(&ev);
	assert_rc_equal(rc, 0);

	rc = dfs_release(obj2);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj1);
	assert_int_equal(rc, 0);
}

static void
dfs_test_compat(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		uuid;
	daos_handle_t	coh;
	dfs_t		*dfs;
	int		rc;
	char		uuid_str[37];

	uuid_clear(uuid);

	if (arg->myrank != 0)
		return;

	print_message("creating DFS container with a uuid pointer (not set by caller) ...\n");
	rc = dfs_cont_create(arg->pool.poh, &uuid, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("Created POSIX Container "DF_UUIDF"\n", DP_UUID(uuid));
	uuid_unparse(uuid, uuid_str);
	rc = daos_cont_open(arg->pool.poh, uuid_str, DAOS_COO_RW, &coh, NULL, NULL);
	assert_success(rc);
	rc = dfs_mount(arg->pool.poh, coh, O_RDWR, &dfs);
	assert_int_equal(rc, 0);
	rc = dfs_umount(dfs);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_success(rc);
	rc = daos_cont_destroy(arg->pool.poh, uuid_str, 1, NULL);
	assert_success(rc);
	print_message("Destroyed POSIX Container "DF_UUIDF"\n", DP_UUID(uuid));

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

	par_barrier(PAR_COMM_WORLD);

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
	for (i = 0; i < dfs_test_thread_nr; i++) {
		rc = pthread_join(dfs_test_tid[i], NULL);
		assert_int_equal(rc, 0);
	}

	for (i = 0; i < dfs_test_thread_nr; i++)
		assert_int_equal(dfs_test_rc[i], 0);

	rc = daos_cont_destroy(arg->pool.poh, name, 0, NULL);
	assert_rc_equal(rc, 0);
	par_barrier(PAR_COMM_WORLD);
}

static void
run_chown_tests(dfs_obj_t *obj, char *name, int mode)
{
	dfs_obj_t	*sym;
	char		*s = "sym_chown_test";
	struct stat	stbuf;
	uid_t		orig_uid;
	gid_t		orig_gid;
	struct timespec	prev_ts;
	int		rc;

	rc = dfs_stat(dfs_mt, NULL, name, &stbuf);
	assert_int_equal(rc, 0);
	prev_ts.tv_sec = stbuf.st_ctim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_ctim.tv_nsec;

	orig_uid = stbuf.st_uid;
	orig_gid = stbuf.st_gid;

	/** should succeed but not change anything */
	rc = dfs_chown(dfs_mt, NULL, name, -1, -1, 0);
	assert_int_equal(rc, 0);
	rc = dfs_stat(dfs_mt, NULL, name, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_uid, orig_uid);
	assert_int_equal(stbuf.st_gid, orig_gid);
	/** check ctime unchanged */
	assert_int_equal(prev_ts.tv_sec, stbuf.st_ctim.tv_sec);
	assert_int_equal(prev_ts.tv_nsec, stbuf.st_ctim.tv_nsec);

	/** set uid to 0 */
	rc = dfs_chown(dfs_mt, NULL, name, 0, -1, 0);
	assert_int_equal(rc, 0);
	rc = dfs_stat(dfs_mt, NULL, name, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_uid, 0);
	assert_int_equal(stbuf.st_gid, orig_gid);
	assert_true(check_ts(prev_ts, stbuf.st_ctim));
	prev_ts.tv_sec = stbuf.st_ctim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_ctim.tv_nsec;

	/** set gid to 0 */
	rc = dfs_chown(dfs_mt, NULL, name, -1, 0, 0);
	assert_int_equal(rc, 0);
	rc = dfs_stat(dfs_mt, NULL, name, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_uid, 0);
	assert_int_equal(stbuf.st_gid, 0);
	assert_true(check_ts(prev_ts, stbuf.st_ctim));
	prev_ts.tv_sec = stbuf.st_ctim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_ctim.tv_nsec;

	/** set uid to 3, gid to 4 - using dfs_osetattr */
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_uid = 3;
	stbuf.st_gid = 4;
	rc = dfs_osetattr(dfs_mt, obj, &stbuf, DFS_SET_ATTR_UID | DFS_SET_ATTR_GID);
	assert_int_equal(rc, 0);
	assert_true(check_ts(prev_ts, stbuf.st_ctim));
	assert_int_equal(stbuf.st_uid, 3);
	assert_int_equal(stbuf.st_gid, 4);
	memset(&stbuf, 0, sizeof(stbuf));
	rc = dfs_stat(dfs_mt, NULL, name, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_uid, 3);
	assert_int_equal(stbuf.st_gid, 4);
	assert_true(check_ts(prev_ts, stbuf.st_ctim));
	prev_ts.tv_sec = stbuf.st_ctim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_ctim.tv_nsec;

	/** set uid to 1, gid to 2 */
	rc = dfs_chown(dfs_mt, NULL, name, 1, 2, 0);
	assert_int_equal(rc, 0);
	rc = dfs_stat(dfs_mt, NULL, name, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_uid, 1);
	assert_int_equal(stbuf.st_gid, 2);
	assert_true(check_ts(prev_ts, stbuf.st_ctim));
	prev_ts.tv_sec = stbuf.st_ctim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_ctim.tv_nsec;

	/** create a symlink to that file/dir */
	rc = dfs_open(dfs_mt, NULL, s, S_IFLNK | S_IWUSR | S_IRUSR, O_RDWR | O_CREAT | O_EXCL, 0, 0,
		      name, &sym);
	assert_int_equal(rc, 0);

	/** chown of file/dir through symlink */
	rc = dfs_chown(dfs_mt, NULL, s, 3, 4, 0);
	assert_int_equal(rc, 0);
	rc = dfs_stat(dfs_mt, NULL, name, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_uid, 3);
	assert_int_equal(stbuf.st_gid, 4);

	/** chown of symlink itself */
	rc = dfs_chown(dfs_mt, NULL, s, 5, 6, O_NOFOLLOW);
	assert_int_equal(rc, 0);
	rc = dfs_stat(dfs_mt, NULL, name, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_uid, 3);
	assert_int_equal(stbuf.st_gid, 4);
	rc = dfs_stat(dfs_mt, NULL, s, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_uid, 5);
	assert_int_equal(stbuf.st_gid, 6);

	rc = dfs_release(sym);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, s, 0, NULL);
	assert_int_equal(rc, 0);
}

static void
dfs_test_chown(void **state)
{
	test_arg_t	*arg = *state;
	dfs_obj_t	*file, *dir;
	char		*f = "chown_test_f";
	char		*d = "chown_test_d";
	struct stat	stbuf;
	struct stat	stbuf2;
	char		*filename_file1 = "open_stat1";
	char		*filename_file2 = "open_stat2";
	mode_t		create_mode = S_IWUSR | S_IRUSR;
	int		create_flags = O_RDWR | O_CREAT | O_EXCL;
	int		rc;

	if (arg->myrank != 0)
		return;

	rc = dfs_lookup(dfs_mt, "/", O_RDWR, &dir, NULL, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_uid, geteuid());
	assert_int_equal(stbuf.st_gid, getegid());
	rc = dfs_release(dir);
	assert_int_equal(rc, 0);

	rc = dfs_open(dfs_mt, NULL, f, S_IFREG | S_IWUSR | S_IRUSR | S_IXUSR, create_flags,
		      0, 0, NULL, &file);
	assert_int_equal(rc, 0);
	rc = dfs_open(dfs_mt, NULL, d, S_IFDIR | S_IWUSR | S_IRUSR | S_IXUSR, create_flags,
		      0, 0, NULL, &dir);
	assert_int_equal(rc, 0);

	print_message("Running chown tests on file object...\n");
	run_chown_tests(file, f, S_IFREG);
	print_message("done\n");
	print_message("Running chown tests on dir object...\n");
	run_chown_tests(dir, d, S_IFDIR);
	print_message("done\n");

	rc = dfs_release(file);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, f, 0, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, d, 0, NULL);
	assert_int_equal(rc, 0);

	/* Test the open_stat call with passing in uid/gid */
	/** Create /file1 */
	rc = dfs_open_stat(dfs_mt, NULL, filename_file1, create_mode | S_IFREG,
			   create_flags, 0, 0, NULL, &file, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_release(file);
	assert_int_equal(rc, 0);

	/** verify ownership */
	rc = dfs_stat(dfs_mt, NULL, filename_file1, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_uid, geteuid());
	assert_int_equal(stbuf.st_uid, getegid());

	/* Now do a create with uid/gid set */
	stbuf2.st_uid = 14;
	stbuf2.st_gid = 15;
	rc = dfs_open_stat(dfs_mt, NULL, filename_file2, create_mode | S_IFREG,
			   create_flags, 0, 0, NULL, &file, &stbuf2);
	assert_int_equal(rc, 0);

	assert_int_equal(stbuf2.st_uid, 14);
	assert_int_equal(stbuf2.st_gid, 15);

	rc = dfs_release(file);
	assert_int_equal(rc, 0);

	/** verify ownership */
	rc = dfs_stat(dfs_mt, NULL, filename_file2, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_uid, stbuf2.st_uid);
	assert_int_equal(stbuf.st_gid, stbuf2.st_gid);
}

static void
run_time_tests(dfs_obj_t *obj, char *name, int mode)
{
	d_sg_list_t		sgl;
	d_iov_t			iov;
	char			buf[64];
	struct stat		stbuf;
	struct timespec		prev_ts, first_ts;
	daos_size_t		size;
	dfs_obj_t		*tmp_obj;
	int			rc;

	rc = dfs_stat(dfs_mt, NULL, name, &stbuf);
	assert_int_equal(rc, 0);
	prev_ts.tv_sec = stbuf.st_mtim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_mtim.tv_nsec;

	/** store the first modification timestamp (at creation time) */
	first_ts.tv_sec = prev_ts.tv_sec;
	first_ts.tv_nsec = prev_ts.tv_nsec;

	if (S_ISREG(mode)) {
		d_iov_set(&iov, buf, 64);
		sgl.sg_nr = 1;
		sgl.sg_nr_out = 1;
		sgl.sg_iovs = &iov;
		dts_buf_render(buf, 64);
		rc = dfs_write(dfs_mt, obj, &sgl, 0, NULL);
		assert_int_equal(rc, 0);
	} else {
		rc = dfs_mkdir(dfs_mt, obj, "d1", S_IFDIR, OC_S1);
		assert_int_equal(rc, 0);
	}

	memset(&stbuf, 0, sizeof(stbuf));
	rc = dfs_stat(dfs_mt, NULL, name, &stbuf);
	assert_int_equal(rc, 0);
	if (S_ISREG(mode))
		assert_int_equal(stbuf.st_size, 64);
	assert_true(check_ts(prev_ts, stbuf.st_mtim));
	assert_true(check_ts(prev_ts, stbuf.st_ctim));
	prev_ts.tv_sec = stbuf.st_mtim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_mtim.tv_nsec;

	if (S_ISREG(mode)) {
		rc = dfs_read(dfs_mt, obj, &sgl, 0, &size, NULL);
		assert_int_equal(rc, 0);

		memset(&stbuf, 0, sizeof(stbuf));
		rc = dfs_stat(dfs_mt, NULL, name, &stbuf);
		assert_int_equal(rc, 0);
		assert_int_equal(stbuf.st_size, 64);
		assert_int_equal(prev_ts.tv_sec, stbuf.st_mtim.tv_sec);
		assert_int_equal(prev_ts.tv_nsec, stbuf.st_mtim.tv_nsec);
	}

	/** reset the mtime on the file/dir to the first timestamp */
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_mtim.tv_sec = first_ts.tv_sec;
	stbuf.st_mtim.tv_nsec = first_ts.tv_nsec;
	rc = dfs_osetattr(dfs_mt, obj, &stbuf, DFS_SET_ATTR_MTIME);
	assert_int_equal(rc, 0);
	assert_true(check_ts(prev_ts, stbuf.st_ctim));

	/** verify mtime is now the same as the one we just set */
	memset(&stbuf, 0, sizeof(stbuf));
	rc = dfs_ostatx(dfs_mt, obj, &stbuf, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(first_ts.tv_sec, stbuf.st_mtim.tv_sec);
	assert_int_equal(first_ts.tv_nsec, stbuf.st_mtim.tv_nsec);
	assert_true(check_ts(prev_ts, stbuf.st_ctim));

	/** truncate the file or remove an entry from the dir */
	if (S_ISREG(mode)) {
		rc = dfs_punch(dfs_mt, obj, 0, DFS_MAX_FSIZE);
		assert_int_equal(rc, 0);
	} else {
		rc = dfs_remove(dfs_mt, obj, "d1", true, NULL);
		assert_int_equal(rc, 0);
	}
	memset(&stbuf, 0, sizeof(stbuf));
	rc = dfs_ostatx(dfs_mt, obj, &stbuf, NULL);
	assert_int_equal(rc, 0);
	assert_true(check_ts(prev_ts, stbuf.st_mtim));
	assert_true(check_ts(prev_ts, stbuf.st_ctim));

	memset(&stbuf, 0, sizeof(stbuf));
	rc = dfs_lookupx(dfs_mt, NULL, name, O_RDWR, &tmp_obj, NULL, &stbuf, 0, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	assert_true(check_ts(prev_ts, stbuf.st_mtim));
	assert_true(check_ts(prev_ts, stbuf.st_ctim));
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	prev_ts.tv_sec = stbuf.st_mtim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_mtim.tv_nsec;

	/** set size on file with dfs_osetattr and stat at same time */
	if (S_ISREG(mode)) {
		memset(&stbuf, 0, sizeof(stbuf));
		stbuf.st_size = 1024;
		rc = dfs_osetattr(dfs_mt, obj, &stbuf, DFS_SET_ATTR_SIZE);
		assert_int_equal(rc, 0);
		assert_int_equal(stbuf.st_size, 1024);
		/** check the mtime was updated with the setattr */
		assert_true(check_ts(prev_ts, stbuf.st_mtim));
	}

	struct tm	tm = {0};
	time_t		ts;
	char		*p;
	struct tm       *timeptr;
	char		time_str[64];

	/** set the mtime to 2020 */
	p = strptime("2020-12-31", "%Y-%m-%d", &tm);
	assert_non_null(p);
	ts = mktime(&tm);
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_mtim.tv_sec = ts;
	stbuf.st_mtim.tv_nsec = 0;
	rc = dfs_osetattr(dfs_mt, obj, &stbuf, DFS_SET_ATTR_MTIME);
	assert_int_equal(rc, 0);
	/** verify */
	memset(&stbuf, 0, sizeof(stbuf));
	rc = dfs_ostat(dfs_mt, obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(ts, stbuf.st_mtim.tv_sec);
	timeptr = localtime(&stbuf.st_mtim.tv_sec);
	strftime(time_str, sizeof(time_str), "%Y-%m-%d", timeptr);
	print_message("mtime = %s\n", time_str);
	assert_true(strncmp("2020", time_str, 4) == 0);

	/** set the mtime to 1900 */
	memset(&tm, 0, sizeof(struct tm));
	p = strptime("1900-12-31", "%Y-%m-%d", &tm);
	assert_non_null(p);
	ts = mktime(&tm);
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_mtim.tv_sec = ts;
	stbuf.st_mtim.tv_nsec = 0;
	rc = dfs_osetattr(dfs_mt, obj, &stbuf, DFS_SET_ATTR_MTIME);
	assert_int_equal(rc, 0);
	/* verify */
	memset(&stbuf, 0, sizeof(stbuf));
	rc = dfs_ostat(dfs_mt, obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(ts, stbuf.st_mtim.tv_sec);
	timeptr = localtime(&stbuf.st_mtim.tv_sec);
	strftime(time_str, sizeof(time_str), "%Y-%m-%d", timeptr);
	print_message("mtime = %s\n", time_str);
	assert_true(strncmp("1900", time_str, 4) == 0);

	/** set the mtime to 2999 */
	memset(&tm, 0, sizeof(struct tm));
	p = strptime("2999-12-31", "%Y-%m-%d", &tm);
	assert_non_null(p);
	ts = mktime(&tm);
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_mtim.tv_sec = ts;
	stbuf.st_mtim.tv_nsec = 0;
	rc = dfs_osetattr(dfs_mt, obj, &stbuf, DFS_SET_ATTR_MTIME);
	assert_int_equal(rc, 0);
	/* verify */
	memset(&stbuf, 0, sizeof(stbuf));
	rc = dfs_ostatx(dfs_mt, obj, &stbuf, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(ts, stbuf.st_mtim.tv_sec);
	timeptr = localtime(&stbuf.st_mtim.tv_sec);
	strftime(time_str, sizeof(time_str), "%Y-%m-%d", timeptr);
	print_message("mtime = %s\n", time_str);
	assert_true(strncmp("2999", time_str, 4) == 0);
}

static void
dfs_test_mtime(void **state)
{
	test_arg_t		*arg = *state;
	dfs_obj_t		*file, *dir;
	char			*f = "test_mtime_f";
	char			*d = "test_mtime_d";
	int			rc;

	if (arg->myrank != 0)
		return;

	rc = dfs_open(dfs_mt, NULL, f, S_IFREG | S_IWUSR | S_IRUSR | S_IXUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file);
	assert_int_equal(rc, 0);

	rc = dfs_open(dfs_mt, NULL, d, S_IFDIR | S_IWUSR | S_IRUSR | S_IXUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &dir);
	assert_int_equal(rc, 0);

	print_message("Running mtime/ctime tests on file object...\n");
	run_time_tests(file, f, S_IFREG);
	print_message("done\n");
	print_message("Running mtime/ctime tests on dir object...\n");
	run_time_tests(dir, d, S_IFDIR);
	print_message("done\n");

	rc = dfs_release(file);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, f, 0, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, d, 0, NULL);
	assert_int_equal(rc, 0);
}

#define NUM_IOS 256
#define IO_SIZE 8192

struct dfs_test_async_arg {
	int			thread_idx;
	pthread_barrier_t	*barrier;
	dfs_obj_t		*file;
	d_sg_list_t		sgls[NUM_IOS];
	d_iov_t			iovs[NUM_IOS];
	daos_size_t		read_sizes[NUM_IOS];
	struct daos_event	*events[NUM_IOS];
	char			*bufs[NUM_IOS];
	test_arg_t		*arg;
};

struct dfs_test_async_arg th_arg[DFS_TEST_MAX_THREAD_NR];

static bool	stop_progress;
static int	polled_events;
pthread_mutex_t	eqh_mutex;

static void *
dfs_test_read_async(void *arg)
{
	struct dfs_test_async_arg	*targ = arg;
	struct daos_event		*eps[NUM_IOS] = { 0 };
	int				i, rc;

	print_message("dfs_test_read_thread %d\n", targ->thread_idx);

	for (i = 0; i < NUM_IOS; i++) {
		daos_event_t *ev;
		char *buf;

		D_ALLOC_PTR_NZ(ev);
		D_ASSERT(ev != NULL);

		rc = daos_event_init(ev, targ->arg->eq, NULL);
		assert_rc_equal(rc, 0);

		D_ALLOC(buf, IO_SIZE);
		D_ASSERT(buf != NULL);

		targ->events[i] = ev;
		targ->bufs[i] = buf;

		d_iov_set(&targ->iovs[i], buf, IO_SIZE);
		targ->sgls[i].sg_nr = 1;
		targ->sgls[i].sg_nr_out = 1;
		targ->sgls[i].sg_iovs = &targ->iovs[i];

		rc = dfs_read(dfs_mt, targ->file, &targ->sgls[i], IO_SIZE * i,
			      &targ->read_sizes[i], ev);
		D_ASSERT(rc == 0);
	}

	pthread_barrier_wait(targ->barrier);

	while (1) {
		if (stop_progress)
			pthread_exit(NULL);

		rc = daos_eq_poll(targ->arg->eq, 0, DAOS_EQ_NOWAIT, NUM_IOS, eps);
		if (rc < 0) {
			print_error("EQ poll failed: %d\n", rc);
			rc = -1;
			pthread_exit(NULL);
		}

		if (rc) {
			D_MUTEX_LOCK(&eqh_mutex);
			polled_events += rc;
			D_MUTEX_UNLOCK(&eqh_mutex);
		}
	}

	print_message("dfs_test_read_thread %d succeed.\n", targ->thread_idx);
	pthread_exit(NULL);
}

static void
dfs_test_async_io_th(void **state)
{
	test_arg_t		*arg = *state;
	pthread_barrier_t	barrier;
	char			name[16];
	dfs_obj_t		*obj;
	int			i;
	int			rc;

	par_barrier(PAR_COMM_WORLD);

	rc = D_MUTEX_INIT(&eqh_mutex, NULL);
	assert_int_equal(rc, 0);

	sprintf(name, "file_async_mt_%d", arg->myrank);
	rc = dfs_test_file_gen(name, 0, OC_S1, IO_SIZE * NUM_IOS);
	assert_int_equal(rc, 0);

	rc = dfs_open(dfs_mt, NULL, name, S_IFREG, O_RDONLY, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);

	stop_progress = false;
	pthread_barrier_init(&barrier, NULL, dfs_test_thread_nr + 1);

	for (i = 0; i < dfs_test_thread_nr; i++) {
		th_arg[i].thread_idx	= i;
		th_arg[i].file		= obj;
		th_arg[i].arg		= arg;
		th_arg[i].barrier	= &barrier;
		rc = pthread_create(&dfs_test_tid[i], NULL, dfs_test_read_async, &th_arg[i]);
		assert_int_equal(rc, 0);
	}

	pthread_barrier_wait(&barrier);

	while (1) {
		rc = daos_eq_query(arg->eq, DAOS_EQR_ALL, 0, NULL);
		if (rc == 0) {
			stop_progress = true;
			break;
		}
	}

	for (i = 0; i < dfs_test_thread_nr; i++) {
		int j;

		rc = pthread_join(dfs_test_tid[i], NULL);
		assert_int_equal(rc, 0);

		for (j = 0; j < NUM_IOS; j++) {
			daos_event_fini(th_arg[i].events[j]);
			D_FREE(th_arg[i].events[j]);
			D_FREE(th_arg[i].bufs[j]);
			D_ASSERT(th_arg[i].read_sizes[j] == IO_SIZE);
		}
	}

	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	dfs_test_rm(name);
	D_MUTEX_DESTROY(&eqh_mutex);
	par_barrier(PAR_COMM_WORLD);
}


#define NUM_ABORTS 64
#define IO_SIZE_2 1048576

static void
dfs_test_async_io(void **state)
{
	test_arg_t		*arg = *state;
	char			name[16];
	dfs_obj_t		*obj;
	int			i, j;
	int			rc;

	par_barrier(PAR_COMM_WORLD);

	sprintf(name, "file_async_%d", arg->myrank);
	rc = dfs_test_file_gen(name, 0, OC_SX, IO_SIZE_2 * NUM_IOS);
	assert_int_equal(rc, 0);

	rc = dfs_open(dfs_mt, NULL, name, S_IFREG, O_RDONLY, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);

	struct daos_event	evs[NUM_IOS];
	d_sg_list_t		sgls[NUM_IOS];
	d_iov_t			iovs[NUM_IOS];
	daos_size_t		read_sizes[NUM_IOS];
	char			*bufs[NUM_IOS];

	for (i = 0; i < NUM_IOS; i++) {
		rc = daos_event_init(&evs[i], arg->eq, NULL);
		assert_rc_equal(rc, 0);

		D_ALLOC(bufs[i], IO_SIZE_2);
		D_ASSERT(bufs[i] != NULL);

		d_iov_set(&iovs[i], bufs[i], IO_SIZE_2);
		sgls[i].sg_nr = 1;
		sgls[i].sg_nr_out = 1;
		sgls[i].sg_iovs = &iovs[i];
	}

	for (j = 0; j < NUM_ABORTS; j++) {
		for (i = 0; i < NUM_IOS; i++) {
			bool flag;
			daos_event_t *ev = &evs[i];

			rc = daos_event_test(ev, DAOS_EQ_NOWAIT, &flag);
			assert_success(rc);

			if (!flag) {
				rc = daos_event_abort(ev);
				assert_int_equal(rc, 0);

				rc = daos_event_test(ev, DAOS_EQ_WAIT, &flag);
				assert_success(rc);
			}
			D_ASSERT(flag == true);

			rc = daos_event_fini(ev);
			assert_success(rc);
			rc = daos_event_init(ev, arg->eq, NULL);
			assert_success(rc);

			rc = dfs_read(dfs_mt, obj, &sgls[i], 0, &read_sizes[i], ev);
			assert_int_equal(rc, 0);
		}
	}

	for (i = 0; i < NUM_IOS; i++) {
		bool flag;

		rc = daos_event_test(&evs[i], DAOS_EQ_WAIT, &flag);
		assert_success(rc);
		D_ASSERT(flag == true);
		daos_event_fini(&evs[i]);
		evs[i].ev_error = INT_MAX;
		evs[i].ev_private.space[0] = ULONG_MAX;
		D_FREE(bufs[i]);
		D_ASSERT(read_sizes[i] == IO_SIZE_2);
	}

	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	dfs_test_rm(name);
	par_barrier(PAR_COMM_WORLD);
}

static void
dfs_test_readdir_internal(void **state, daos_oclass_id_t obj_class)
{
	dfs_obj_t		*dir;
	dfs_obj_t		*obj;
	int			nr = 100;
	char                    name[24];
	char                    anchor_name[24];
	daos_anchor_t		anchor = {0};
	uint32_t		num_ents = 10;
	struct dirent		ents[10];
	struct stat		stbufs[10];
	int			num_files = 0;
	int			num_dirs = 0;
	int			total_entries = 0;
	bool			check_first = true;
	char			dir_name[24];
	int			i;
	int			rc;

	sprintf(dir_name, "dir_%d", obj_class);
	rc = dfs_open(dfs_mt, NULL, dir_name, S_IFDIR | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, obj_class, 0, NULL, &dir);
	assert_int_equal(rc, 0);

	/** create 100 files and dirs */
	for (i = 0; i < nr; i++) {
		sprintf(name, "RD_file_%d", i);
		rc = dfs_open(dfs_mt, dir, name, S_IFREG | S_IWUSR | S_IRUSR,
			      O_RDWR | O_CREAT, OC_S1, 0, NULL, &obj);
		assert_int_equal(rc, 0);
		rc = dfs_release(obj);
		assert_int_equal(rc, 0);

		sprintf(name, "RD_dir_%d", i);
		rc = dfs_mkdir(dfs_mt, dir, name, S_IFDIR, OC_S1);
		assert_int_equal(rc, 0);
	}

	/** readdir and stat */
	print_message("start readdirplus and verify statbuf\n");
	while (!daos_anchor_is_eof(&anchor)) {
		rc = dfs_readdirplus(dfs_mt, dir, &anchor, &num_ents, ents, stbufs);
		assert_int_equal(rc, 0);

		for (i = 0; i < num_ents; i++) {
			/** save the 50th entry to restart iteration from there */
			if (num_files+num_dirs == 50)
				strcpy(anchor_name, ents[i].d_name);
			if (strncmp(ents[i].d_name, "RD_file", 7) == 0) {
				assert_true(S_ISREG(stbufs[i].st_mode));
				num_files++;
			} else if (strncmp(ents[i].d_name, "RD_dir", 6) == 0) {
				assert_true(S_ISDIR(stbufs[i].st_mode));
				num_dirs++;
			} else {
				print_error("Found invalid entry: %s\n", ents[i].d_name);
			}
			total_entries++;
		}
		num_ents = 10;
	}

	assert_true(num_files == 100);
	assert_true(num_dirs == 100);
	assert_true(total_entries == 200);

	/** set anchor at the saved entry and restart iteration */
	rc = dfs_dir_anchor_set(dir, anchor_name, &anchor);
	assert_int_equal(rc, 0);
	total_entries = 0;
	print_message("restart readdir with anchor set at: %s\n", anchor_name);
	while (!daos_anchor_is_eof(&anchor)) {
		rc = dfs_readdirplus(dfs_mt, dir, &anchor, &num_ents, ents, stbufs);
		assert_int_equal(rc, 0);
		for (i = 0; i < num_ents; i++) {
			total_entries++;
			if (check_first) {
				assert_true(strcmp(ents[i].d_name, anchor_name) == 0);
				check_first = false;
			}
		}
		num_ents = 10;
	}
	assert_true(total_entries == 150);

	/** set anchor at the saved entry */
	rc = dfs_dir_anchor_set(dir, anchor_name, &anchor);
	assert_int_equal(rc, 0);
	total_entries = 0;

	/** remove the entry of the anchor */
	rc = dfs_remove(dfs_mt, dir, anchor_name, 0, NULL);
	assert_int_equal(rc, 0);

	print_message("restart readdir with anchor set at removed entry: %s\n", anchor_name);
	while (!daos_anchor_is_eof(&anchor)) {
		rc = dfs_readdirplus(dfs_mt, dir, &anchor, &num_ents, ents, stbufs);
		assert_int_equal(rc, 0);
		for (i = 0; i < num_ents; i++)
			total_entries++;
		num_ents = 10;
	}
	assert_true(total_entries == 149);

	rc = dfs_release(dir);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, dir_name, 1, NULL);
	assert_int_equal(rc, 0);
}

static void
dfs_test_readdir(void **state)
{
	test_arg_t	*arg = *state;

	print_message("Running readdir test with OC_SX dir..\n");
	dfs_test_readdir_internal(state, OC_SX);
	if (test_runable(arg, 2)) {
		print_message("Running readdir test with OC_RP_2GX dir..\n");
		dfs_test_readdir_internal(state, OC_RP_2GX);
	}
	if (test_runable(arg, 4)) {
		print_message("Running readdir test with OC_EC_2P2GX dir..\n");
		dfs_test_readdir_internal(state, OC_EC_2P2GX);
	}
}

static int
compare_oclass(daos_handle_t coh, daos_oclass_id_t acid, daos_oclass_id_t ecid)
{
	int		rc;
	daos_obj_id_t	oid = {};

	/*
	 * get the expected oclass - this is needed to convert things with GX to fit them in current
	 * system.
	 */
	rc = daos_obj_generate_oid(coh, &oid, 0, ecid, 0, 0);
	assert_rc_equal(rc, 0);
	ecid = daos_obj_id2class(oid);

	if (acid == ecid)
		return 0;
	else
		return 1;
}

static void
dfs_test_oclass_hints(void **state)
{
	test_arg_t		*arg = *state;
	char			oclass_name[24];
	daos_oclass_id_t	cid;
	daos_handle_t		coh;
	dfs_t			*dfs_l;
	dfs_obj_t		*obj;
	daos_obj_id_t		oid;
	daos_oclass_id_t	ecidx;
	daos_prop_t             *prop = NULL;
	dfs_attr_t		dattr = {0};
	struct pl_map_attr      attr = {0};
	int			rc;

	/** check invalid hints */
	rc = dfs_suggest_oclass(dfs_mt, "file:single,dir:large", &cid);
	assert_int_equal(rc, EINVAL);
	rc = dfs_suggest_oclass(dfs_mt, "file:singlee", &cid);
	assert_int_equal(rc, EINVAL);

	rc = pl_map_query(arg->pool.pool_uuid, &attr);
	assert_rc_equal(rc, 0);

	strcpy(dattr.da_hints, "file:max,dir:invalid");
	rc = dfs_cont_create_with_label(arg->pool.poh, "h_cont", &dattr, NULL, &coh, &dfs_l);
	assert_int_equal(rc, EINVAL);
	strcpy(dattr.da_hints, "file:max;dir:max");
	rc = dfs_cont_create_with_label(arg->pool.poh, "h_cont", &dattr, NULL, &coh, &dfs_l);
	assert_int_equal(rc, EINVAL);
	strcpy(dattr.da_hints, "file:max:dir:max");
	rc = dfs_cont_create_with_label(arg->pool.poh, "h_cont", &dattr, NULL, &coh, &dfs_l);
	assert_int_equal(rc, EINVAL);
	strcpy(dattr.da_hints, "invalid");
	rc = dfs_cont_create_with_label(arg->pool.poh, "h_cont", &dattr, NULL, &coh, &dfs_l);
	assert_int_equal(rc, EINVAL);
	strcpy(dattr.da_hints, "file:single,dir:max,hello:world");
	rc = dfs_cont_create_with_label(arg->pool.poh, "h_cont", &dattr, NULL, &coh, &dfs_l);
	assert_int_equal(rc, EINVAL);

	strcpy(dattr.da_hints, "File:single,Dir:max");
	rc = dfs_cont_create_with_label(arg->pool.poh, "h_cont", &dattr, NULL, &coh, &dfs_l);
	assert_int_equal(rc, 0);

	/** Create /file1 */
	rc = dfs_open(dfs_l, NULL, "file1", S_IWUSR | S_IRUSR | S_IFREG, O_RDWR | O_CREAT | O_EXCL,
		      0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(obj, &oid);
	assert_int_equal(rc, 0);
	cid = daos_obj_id2class(oid);
	rc = compare_oclass(coh, cid, OC_S1);
	assert_rc_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	/** Create /dir1 */
	rc = dfs_open(dfs_l, NULL, "dir1", S_IWUSR | S_IRUSR | S_IFDIR, O_RDWR | O_CREAT | O_EXCL,
		      0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(obj, &oid);
	assert_int_equal(rc, 0);
	cid = daos_obj_id2class(oid);
	rc = compare_oclass(coh, cid, OC_SX);
	assert_rc_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	memset(dattr.da_hints, 0, DAOS_CONT_HINT_MAX_LEN);
	rc = dfs_umount(dfs_l);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_success(rc);
	rc = daos_cont_destroy(arg->pool.poh, "h_cont", 0, NULL);
	assert_success(rc);

	prop = daos_prop_alloc(1);
	assert_non_null(prop);
	dattr.da_props = prop;

	/** create container with RF = 0 */
	print_message("DFS object class hints with container RF0:\n");
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	prop->dpp_entries[0].dpe_val = DAOS_PROP_CO_REDUN_RF0;
	rc = dfs_cont_create_with_label(arg->pool.poh, "oc_cont0", &dattr, NULL, &coh, &dfs_l);
	assert_int_equal(rc, 0);

	rc = dfs_suggest_oclass(dfs_l, "file:single", &cid);
	assert_int_equal(rc, 0);
	daos_oclass_id2name(cid, oclass_name);
	print_message("oclass suggested for \"file:single\" = %s\n", oclass_name);
	rc = compare_oclass(coh, cid, OC_S1);
	assert_rc_equal(rc, 0);

	rc = dfs_suggest_oclass(dfs_l, "File:max", &cid);
	assert_int_equal(rc, 0);
	daos_oclass_id2name(cid, oclass_name);
	print_message("oclass suggested for \"File:max\" = %s\n", oclass_name);
	rc = compare_oclass(coh, cid, OC_SX);
	assert_rc_equal(rc, 0);

	rc = dfs_suggest_oclass(dfs_l, "dir:single", &cid);
	assert_int_equal(rc, 0);
	daos_oclass_id2name(cid, oclass_name);
	print_message("oclass suggested for \"dir:single\" = %s\n", oclass_name);
	rc = compare_oclass(coh, cid, OC_S1);
	assert_rc_equal(rc, 0);

	rc = dfs_suggest_oclass(dfs_l, "Directory:max", &cid);
	assert_int_equal(rc, 0);
	daos_oclass_id2name(cid, oclass_name);
	print_message("oclass suggested for \"Directory:max\" = %s\n", oclass_name);
	rc = compare_oclass(coh, cid, OC_SX);
	assert_rc_equal(rc, 0);

	rc = dfs_umount(dfs_l);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_success(rc);
	rc = daos_cont_destroy(arg->pool.poh, "oc_cont0", 0, NULL);
	assert_success(rc);

	/** create container with RF = 1 */
	print_message("DFS object class hints with container RF1:\n");
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	prop->dpp_entries[0].dpe_val = DAOS_PROP_CO_REDUN_RF1;
	rc = dfs_cont_create_with_label(arg->pool.poh, "oc_cont1", &dattr, NULL, &coh, &dfs_l);
	assert_int_equal(rc, 0);

	/** set the expect EC object class ID based on domain nr */
	if (attr.pa_domain_nr >= 18)
		ecidx = OC_EC_16P1GX;
	else if (attr.pa_domain_nr >= 10)
		ecidx = OC_EC_8P1GX;
	else if (attr.pa_domain_nr >= 6)
		ecidx = OC_EC_4P1GX;
	else
		ecidx = OC_EC_2P1GX;

	rc = dfs_suggest_oclass(dfs_l, "file:single", &cid);
	assert_int_equal(rc, 0);
	daos_oclass_id2name(cid, oclass_name);
	print_message("oclass suggested for \"file:single\" = %s\n", oclass_name);
	rc = compare_oclass(coh, cid, OC_RP_2G1);
	assert_rc_equal(rc, 0);

	rc = dfs_suggest_oclass(dfs_l, "File:max", &cid);
	assert_int_equal(rc, 0);
	daos_oclass_id2name(cid, oclass_name);
	print_message("oclass suggested for \"File:max\" = %s\n", oclass_name);
	rc = compare_oclass(coh, cid, ecidx);
	assert_rc_equal(rc, 0);

	rc = dfs_suggest_oclass(dfs_l, "dir:single", &cid);
	assert_int_equal(rc, 0);
	daos_oclass_id2name(cid, oclass_name);
	print_message("oclass suggested for \"dir:single\" = %s\n", oclass_name);
	rc = compare_oclass(coh, cid, OC_RP_2G1);
	assert_rc_equal(rc, 0);

	rc = dfs_suggest_oclass(dfs_l, "Directory:max", &cid);
	assert_int_equal(rc, 0);
	daos_oclass_id2name(cid, oclass_name);
	print_message("oclass suggested for \"Directory:max\" = %s\n", oclass_name);
	rc = compare_oclass(coh, cid, OC_RP_2GX);
	assert_rc_equal(rc, 0);

	rc = dfs_umount(dfs_l);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_success(rc);
	rc = daos_cont_destroy(arg->pool.poh, "oc_cont1", 0, NULL);
	assert_success(rc);

	/** create container with RF = 2 */
	print_message("DFS object class hints with container RF2:\n");
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	prop->dpp_entries[0].dpe_val = DAOS_PROP_CO_REDUN_RF2;
	rc = dfs_cont_create_with_label(arg->pool.poh, "oc_cont2", &dattr, NULL, &coh, &dfs_l);
	assert_int_equal(rc, 0);

	/** set the expect EC object class ID based on domain nr */
	if (attr.pa_domain_nr >= 18)
		ecidx = OC_EC_16P2GX;
	else if (attr.pa_domain_nr >= 10)
		ecidx = OC_EC_8P2GX;
	else if (attr.pa_domain_nr >= 6)
		ecidx = OC_EC_4P2GX;
	else
		ecidx = OC_EC_2P2GX;

	rc = dfs_suggest_oclass(dfs_l, "file:single", &cid);
	assert_int_equal(rc, 0);
	daos_oclass_id2name(cid, oclass_name);
	print_message("oclass suggested for \"file:single\" = %s\n", oclass_name);
	rc = compare_oclass(coh, cid, OC_RP_3G1);
	assert_rc_equal(rc, 0);

	rc = dfs_suggest_oclass(dfs_l, "File:max", &cid);
	assert_int_equal(rc, 0);
	daos_oclass_id2name(cid, oclass_name);
	print_message("oclass suggested for \"File:max\" = %s\n", oclass_name);
	rc = compare_oclass(coh, cid, ecidx);
	assert_rc_equal(rc, 0);

	rc = dfs_suggest_oclass(dfs_l, "dir:single", &cid);
	assert_int_equal(rc, 0);
	daos_oclass_id2name(cid, oclass_name);
	print_message("oclass suggested for \"dir:single\" = %s\n", oclass_name);
	rc = compare_oclass(coh, cid, OC_RP_3G1);
	assert_rc_equal(rc, 0);

	rc = dfs_suggest_oclass(dfs_l, "Directory:max", &cid);
	assert_int_equal(rc, 0);
	daos_oclass_id2name(cid, oclass_name);
	print_message("oclass suggested for \"Directory:max\" = %s\n", oclass_name);
	rc = compare_oclass(coh, cid, OC_RP_3GX);
	assert_rc_equal(rc, 0);

	rc = dfs_umount(dfs_l);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_success(rc);
	rc = daos_cont_destroy(arg->pool.poh, "oc_cont2", 0, NULL);
	assert_success(rc);

	daos_prop_free(prop);
}

static void
dfs_test_multiple_pools(void **state)
{
	test_arg_t		*arg = *state;
	dfs_t			*dfs1, *dfs2;
	uuid_t			uuid1, uuid2;
	daos_handle_t		poh1, poh2;
	daos_handle_t		coh1, coh2;
	char			str1[37], str2[37];
	int			rc;

	rc = dmg_pool_create(dmg_config_file, geteuid(), getegid(), arg->group, NULL,
			     128 * 1024 * 1024, 0, NULL, arg->pool.svc, uuid1);
	assert_rc_equal(rc, 0);
	uuid_unparse_lower(uuid1, str1);

	rc = dmg_pool_create(dmg_config_file, geteuid(), getegid(), arg->group, NULL,
			     128 * 1024 * 1024, 0, NULL, arg->pool.svc, uuid2);
	assert_rc_equal(rc, 0);
	uuid_unparse_lower(uuid2, str2);

	rc = dfs_init();
	assert_int_equal(rc, 0);

	/** try creating the same container label on different pools, should succeed */
	rc = dfs_connect(str1, arg->group, "cont0", O_CREAT | O_RDWR, NULL, &dfs1);
	assert_int_equal(rc, 0);
	rc = dfs_connect(str2, arg->group, "cont0", O_CREAT | O_RDWR, NULL, &dfs2);
	assert_int_equal(rc, 0);

	rc = dfs_disconnect(dfs1);
	assert_int_equal(rc, 0);
	rc = dfs_disconnect(dfs2);
	assert_int_equal(rc, 0);

	rc = daos_pool_connect(str1, arg->group, DAOS_PC_RW, &poh1, NULL, NULL);
	assert_success(rc);
	rc = daos_pool_connect(str2, arg->group, DAOS_PC_RW, &poh2, NULL, NULL);
	assert_success(rc);

	rc = daos_cont_open(poh1, "cont0", DAOS_COO_RW, &coh1, NULL, NULL);
	assert_success(rc);
	rc = daos_cont_open(poh2, "cont0", DAOS_COO_RW, &coh2, NULL, NULL);
	assert_success(rc);

	rc = daos_cont_close(coh1, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_close(coh2, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_pool_disconnect(poh1, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_pool_disconnect(poh2, NULL);
	assert_rc_equal(rc, 0);

	rc = dfs_fini();
	assert_int_equal(rc, 0);

	rc = dmg_pool_destroy(dmg_config_file, uuid1, arg->group, 1);
	assert_rc_equal(rc, 0);
	rc = dmg_pool_destroy(dmg_config_file, uuid2, arg->group, 1);
	assert_rc_equal(rc, 0);
}

static void
dfs_test_xattrs(void **state)
{
	test_arg_t		*arg = *state;
	dfs_obj_t		*obj;
	char			*dir = "xdir";
	mode_t			create_mode = S_IWUSR | S_IRUSR;
	int			create_flags = O_RDWR | O_CREAT;
	const char		*xname1 = "user.empty";
	const char		*xname2 = "user.with_value";
	const char		*xval2  = "some value";
	daos_size_t		size;
	char			buf[32];
	int			rc;

	if (arg->myrank != 0)
		return;

	rc = dfs_open(dfs_mt, NULL, dir, create_mode | S_IFDIR, create_flags,
		      0, 0, NULL, &obj);
	assert_int_equal(rc, 0);

	rc = dfs_getxattr(dfs_mt, obj, xname1, NULL, &size);
	assert_int_equal(rc, ENODATA);

	size = 0;
	rc = dfs_setxattr(dfs_mt, obj, xname1, NULL, size, 0);
	assert_int_equal(rc, 0);

	size = sizeof(buf);
	rc = dfs_getxattr(dfs_mt, obj, xname1, buf, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, 0);

	rc = dfs_getxattr(dfs_mt, obj, xname2, NULL, &size);
	assert_int_equal(rc, ENODATA);

	size = strlen(xval2) + 1;
	rc = dfs_setxattr(dfs_mt, obj, xname2, xval2, size, 0);
	assert_int_equal(rc, 0);

	size = sizeof(buf);
	memset(buf, 0, sizeof(buf));
	rc = dfs_getxattr(dfs_mt, obj, xname2, buf, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, strlen(xval2) + 1);
	assert_string_equal(xval2, buf);

	size = sizeof(buf);
	memset(buf, 0, sizeof(buf));
	rc = dfs_listxattr(dfs_mt, obj, buf, &size);

	assert_int_equal(rc, 0);
	assert_int_equal(size, strlen(xname1) + 1 + strlen(xname2) + 1);
	assert_string_equal(buf, xname1);
	assert_string_equal(buf + strlen(xname1) + 1, xname2);

	rc = dfs_removexattr(dfs_mt, obj, xname1);
	assert_int_equal(rc, 0);

	size = 0;
	rc = dfs_getxattr(dfs_mt, obj, xname1, NULL, &size);
	assert_int_equal(rc, ENODATA);

	size = sizeof(buf);
	memset(buf, 0, sizeof(buf));
	rc = dfs_listxattr(dfs_mt, obj, buf, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, strlen(xname2) + 1);
	assert_string_equal(buf, xname2);

	rc = dfs_removexattr(dfs_mt, obj, xname2);
	assert_int_equal(rc, 0);

	size = 0;
	rc = dfs_getxattr(dfs_mt, obj, xname2, NULL, &size);
	assert_int_equal(rc, ENODATA);

	size = sizeof(buf);
	rc = dfs_listxattr(dfs_mt, obj, buf, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, 0);

	rc = dfs_release(obj);
	assert_int_equal(rc, 0);
}

#define NR_LIST	128

static void
get_nr_oids(daos_handle_t poh, const char *cont, uint64_t *nr_oids)
{
	daos_epoch_t		snap;
	daos_handle_t		coh, oit;
	daos_obj_id_t		oids[NR_LIST];
	daos_anchor_t		anchor = {0};
	daos_epoch_range_t	epr;
	uint32_t		nr_entries;
	int			rc;

	rc = daos_cont_open(poh, cont, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_create_snap_opt(coh, &snap, NULL, DAOS_SNAP_OPT_CR | DAOS_SNAP_OPT_OIT,
				       NULL);
	assert_rc_equal(rc, 0);
	rc = daos_oit_open(coh, snap, &oit, NULL);
	assert_rc_equal(rc, 0);

	*nr_oids = 0;
	while (!daos_anchor_is_eof(&anchor)) {
		nr_entries = NR_LIST;
		rc = daos_oit_list(oit, oids, &nr_entries, &anchor, NULL);
		assert_rc_equal(rc, 0);
		*nr_oids += nr_entries;
	}

	rc = daos_oit_close(oit, NULL);
	assert_rc_equal(rc, 0);
	epr.epr_hi = epr.epr_lo = snap;
	rc = daos_cont_destroy_snap(coh, epr, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);
}

static void
dfs_test_checker(void **state)
{
	test_arg_t		*arg = *state;
	dfs_t			*dfs;
	int			nr = 100, i;
	dfs_obj_t		*root, *lf;
	daos_obj_id_t		root_oid;
	daos_handle_t		root_oh;
	daos_handle_t		coh;
	uint64_t		nr_oids = 0;
	char			*cname = "cont_chkr";
	int			rc;

	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_CREAT | O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);

	/* save the root object ID for later */
	rc = dfs_lookup(dfs, "/", O_RDWR, &root, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(root, &root_oid);
	assert_int_equal(rc, 0);
	rc = dfs_release(root);
	assert_int_equal(rc, 0);

	/** create 100 files and 100 dirs */
	for (i = 0; i < nr; i++) {
		dfs_obj_t	*dir, *file;
		d_sg_list_t	sgl;
		d_iov_t		iov;
		char		name[24];
		char		buf[1024];

		sprintf(name, "RD_dir_%d", i);
		rc = dfs_open(dfs, NULL, name, S_IFDIR | S_IWUSR | S_IRUSR,
			      O_RDWR | O_CREAT, OC_S1, 0, NULL, &dir);
		assert_int_equal(rc, 0);

		sprintf(name, "RD_file_%d", i);
		rc = dfs_open(dfs, NULL, name, S_IFREG | S_IWUSR | S_IRUSR,
			      O_RDWR | O_CREAT, OC_S1, 0, NULL, &file);
		assert_int_equal(rc, 0);

		d_iov_set(&iov, buf, 1024);
		sgl.sg_nr = 1;
		sgl.sg_nr_out = 1;
		sgl.sg_iovs = &iov;

		rc = dfs_write(dfs, file, &sgl, 0, NULL);
		assert_int_equal(rc, 0);

		if (i == 0 || i == 10) {
			int j;

			for (j = 1; j < 10 ; j++) {
				rc = dfs_write(dfs, file, &sgl, j * 1048576, NULL);
				assert_int_equal(rc, 0);
			}
		}
		rc = dfs_release(file);
		assert_int_equal(rc, 0);

		/** create an additional file under each dir */
		rc = dfs_open(dfs, dir, "newfile", S_IFREG | S_IWUSR | S_IRUSR,
			      O_RDWR | O_CREAT, OC_S1, 0, NULL, &file);
		assert_int_equal(rc, 0);
		rc = dfs_write(dfs, file, &sgl, 0, NULL);
		assert_int_equal(rc, 0);
		rc = dfs_release(file);
		assert_int_equal(rc, 0);

		rc = dfs_release(dir);
		assert_int_equal(rc, 0);
	}

	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	/** have to call fini to release the cached container handle for the checker to work */
	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/*
	 * Using lower level obj API, punch 10 files and 10 directory entries leaving orphaned
	 * directory object and the file that was created under it.
	 */
	rc = daos_cont_open(arg->pool.poh, cname, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_obj_open(coh, root_oid, DAOS_OO_RW, &root_oh, NULL);
	assert_rc_equal(rc, 0);
	for (i = 0; i < 10; i++) {
		char		name[24];
		d_iov_t		dkey;

		sprintf(name, "RD_dir_%d", i);
		d_iov_set(&dkey, name, strlen(name));
		rc = daos_obj_punch_dkeys(root_oh, DAOS_TX_NONE, DAOS_COND_PUNCH, 1, &dkey, NULL);
		assert_rc_equal(rc, 0);

		sprintf(name, "RD_file_%d", i);
		d_iov_set(&dkey, name, strlen(name));
		rc = daos_obj_punch_dkeys(root_oh, DAOS_TX_NONE, DAOS_COND_PUNCH, 1, &dkey, NULL);
		assert_rc_equal(rc, 0);
	}
	/** run the checker before the container is closed, without evict - should fail */
	rc = dfs_cont_check(arg->pool.poh, cname,
			    DFS_CHECK_PRINT | DFS_CHECK_REMOVE | DFS_CHECK_VERIFY, NULL);
	assert_int_equal(rc, EBUSY);
	/** close the container */
	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);

	/** check how many OIDs in container before invoking the checker */
	get_nr_oids(arg->pool.poh, cname, &nr_oids);
	/** should be 300 + SB + root object */
	assert_int_equal((int)nr_oids, 302);

	rc = dfs_cont_check(arg->pool.poh, cname,
			    DFS_CHECK_PRINT | DFS_CHECK_REMOVE | DFS_CHECK_VERIFY, NULL);
	assert_int_equal(rc, 0);

	/** check how many OIDs in container after invoking the checker */
	get_nr_oids(arg->pool.poh, cname, &nr_oids);
	/** should be 300 - 30 punched objects + SB + root object */
	assert_int_equal((int)nr_oids, 272);

	/*
	 * Using lower level obj API, punch 10 more file and directory entries leaving orphaned
	 * directory objects and the file that was created under it.
	 */
	rc = daos_cont_open(arg->pool.poh, cname, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_obj_open(coh, root_oid, DAOS_OO_RW, &root_oh, NULL);
	assert_rc_equal(rc, 0);
	for (i = 10; i < 20; i++) {
		char		name[24];
		d_iov_t		dkey;

		sprintf(name, "RD_dir_%d", i);
		d_iov_set(&dkey, name, strlen(name));
		rc = daos_obj_punch_dkeys(root_oh, DAOS_TX_NONE, DAOS_COND_PUNCH, 1, &dkey, NULL);
		assert_rc_equal(rc, 0);

		sprintf(name, "RD_file_%d", i);
		d_iov_set(&dkey, name, strlen(name));
		rc = daos_obj_punch_dkeys(root_oh, DAOS_TX_NONE, DAOS_COND_PUNCH, 1, &dkey, NULL);
		assert_rc_equal(rc, 0);
	}

	/** check how many OIDs in container before invoking the checker */
	get_nr_oids(arg->pool.poh, cname, &nr_oids);
	/** should be 270 + SB + root object */
	assert_int_equal((int)nr_oids, 272);

	/** run the checker before the container is closed, with evict - should succeed */
	rc = dfs_cont_check(arg->pool.poh, cname,
			    DFS_CHECK_PRINT | DFS_CHECK_RELINK | DFS_CHECK_EVICT_ALL, "tlf");
	assert_int_equal(rc, 0);

	/** check how many OIDs in container after invoking the checker */
	get_nr_oids(arg->pool.poh, cname, &nr_oids);
	/** should be 274 (270 + SB + root object + LF dir + timestamp dir) */
	assert_int_equal((int)nr_oids, 274);

	/** close the handle - required to avoid resource leak even though it has been evicted */
	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);

	/** readdir of /lost+found/tlf confirming there are 10 files and dirs */
	int			num_files = 0;
	int			num_dirs = 0;
	daos_anchor_t		anchor = {0};
	uint32_t		num_ents = 10;
	struct dirent		ents[10];
	struct stat		stbufs[10];

	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_CREAT | O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);
	rc = dfs_lookup(dfs, "/lost+found/tlf", O_RDWR, &lf, NULL, NULL);
	assert_rc_equal(rc, 0);
	while (!daos_anchor_is_eof(&anchor)) {
		rc = dfs_readdirplus(dfs, lf, &anchor, &num_ents, ents, stbufs);
		assert_int_equal(rc, 0);

		for (i = 0; i < num_ents; i++) {
			if (S_ISREG(stbufs[i].st_mode)) {
				print_message("FILE: %s\n", ents[i].d_name);
				num_files++;
			} else if (S_ISDIR(stbufs[i].st_mode)) {
				print_message("DIR: %s\n", ents[i].d_name);
				num_dirs++;
			} else {
				print_error("Found invalid entry: %s\n", ents[i].d_name);
				assert_true(S_ISREG(stbufs[i].st_mode) ||
					    S_ISDIR(stbufs[i].st_mode));
			}
		}
		num_ents = 10;
	}
	assert_true(num_files == 10);
	assert_true(num_dirs == 10);
	rc = dfs_release(lf);
	assert_int_equal(rc, 0);
	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_destroy(arg->pool.pool_str, arg->group, cname, 0, NULL);
	assert_rc_equal(rc, 0);
	rc = dfs_fini();
	assert_int_equal(rc, 0);
}

static void
mwc_sb_root_test(void **state, const char *cname, bool sb_test)
{
	test_arg_t			*arg = *state;
	dfs_t				*dfs;
	daos_handle_t			coh;
	struct daos_prop_entry		*entry;
	struct daos_prop_co_roots	*roots;
	daos_prop_t			*prop = NULL;
	dfs_attr_t			attr = {};
	daos_handle_t			oh;
	int				rc;

	/** create the DFS container */
	rc = dfs_cont_create_with_label(arg->pool.poh, cname, NULL, NULL, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = daos_cont_open(arg->pool.poh, cname, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	prop = daos_prop_alloc(1);
	assert_non_null(prop);
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_ROOTS;
	rc = daos_cont_query(coh, NULL, prop, NULL);
	assert_rc_equal(rc, 0);
	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_ROOTS);
	D_ASSERT(entry != NULL);
	roots = (struct daos_prop_co_roots *)entry->dpe_val_ptr;

	rc = daos_obj_open(coh, roots->cr_oids[0], DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);
	if (sb_test) {
		/** punch the SB */
		rc = daos_obj_punch(oh, DAOS_TX_NONE, 0, NULL);
		assert_rc_equal(rc, 0);
	} else {
		/** punch the root entry from the SB */
		char		*name = "/";
		d_iov_t		dkey;

		d_iov_set(&dkey, name, strlen(name));
		rc = daos_obj_punch_dkeys(oh, DAOS_TX_NONE, DAOS_COND_PUNCH, 1, &dkey, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	/** try to mount the container. should fail */
	rc = dfs_mount(arg->pool.poh, coh, O_RDWR, &dfs);
	assert_int_equal(rc, ENOENT);

	if (sb_test) {
		/** fix the container by recreating the SB */
		rc = dfs_recreate_sb(coh, &attr);
		assert_int_equal(rc, 0);
	} else {
		/** relink the root object */
		rc = dfs_relink_root(coh);
		assert_int_equal(rc, 0);
	}

	/** try to mount the container. should succeed */
	rc = dfs_mount(arg->pool.poh, coh, O_RDWR, &dfs);
	assert_int_equal(rc, 0);
	rc = dfs_umount(dfs);
	assert_int_equal(rc, 0);

	daos_prop_free(prop);
	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, cname, 0, NULL);
	assert_rc_equal(rc, 0);
}

static void
dfs_test_fix_sb(void **state)
{
	mwc_sb_root_test(state, "cont_fix_sb", true);
}

static void
dfs_test_relink_root(void **state)
{
	mwc_sb_root_test(state, "cont_relink_root", false);
}

#define NR_FILES 5

static void
dfs_test_fix_chunk_size(void **state)
{
	test_arg_t		*arg = *state;
	dfs_t			*dfs;
	dfs_obj_t		*root;
	daos_handle_t		coh;
	daos_obj_id_t		root_oid;
	daos_handle_t		root_oh;
	char			*cname = "cont_csize_fix";
	dfs_obj_t		*files[NR_FILES];
	daos_size_t		csizes[NR_FILES] = {0, 65536, 65536, 2097152, 2097152};
	daos_obj_id_t		foids[NR_FILES] = {0};
	uint64_t		nr_oids = 0;
	d_sg_list_t		sgl;
	d_iov_t			iov;
	char			name[24];
	char			*buf;
	int			i;
	int			rc;

	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_CREAT | O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);

	/* save the root object ID for later */
	rc = dfs_lookup(dfs, "/", O_RDWR, &root, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(root, &root_oid);
	assert_int_equal(rc, 0);
	rc = dfs_release(root);
	assert_int_equal(rc, 0);

	/** create files with different chunk sizes */
	for (i = 0; i < NR_FILES; i++) {
		sprintf(name, "RD_file_%d", i);
		rc = dfs_open(dfs, NULL, name, S_IFREG | S_IWUSR | S_IRUSR, O_RDWR | O_CREAT, OC_S1,
			      csizes[i], NULL, &files[i]);
		assert_int_equal(rc, 0);
		rc = dfs_obj2id(files[i], &foids[i]);
		assert_int_equal(rc, 0);
	}

	D_ALLOC(buf, 2 * 1024 * 1024);
	assert_non_null(buf);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = &iov;

	/** for file with default chunk size, write 1m + 1 */
	d_iov_set(&iov, buf, 1024 * 1024 + 1);
	rc = dfs_write(dfs, files[0], &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	/** for files with chunk size 64k, write 32k to one and 1m to the other */
	d_iov_set(&iov, buf, 32*1024);
	rc = dfs_write(dfs, files[1], &sgl, 0, NULL);
	assert_int_equal(rc, 0);
	d_iov_set(&iov, buf, 1024*1024);
	rc = dfs_write(dfs, files[2], &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	/** for files with chunk size 2m, write 1m - 1 to one and 2m - 3 to the other */
	d_iov_set(&iov, buf, 1024 * 1024 - 1);
	rc = dfs_write(dfs, files[3], &sgl, 0, NULL);
	assert_int_equal(rc, 0);
	d_iov_set(&iov, buf, 2 * 1024 * 1024 - 3);
	rc = dfs_write(dfs, files[4], &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	for (i = 0; i < NR_FILES; i++) {
		rc = dfs_release(files[i]);
		assert_int_equal(rc, 0);
	}

	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	/** have to call fini to release the cached container handle for the checker to work */
	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/** Using lower level obj API, punch all the files created. */
	rc = daos_cont_open(arg->pool.poh, cname, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_obj_open(coh, root_oid, DAOS_OO_RW, &root_oh, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < NR_FILES; i++) {
		d_iov_t		dkey;

		sprintf(name, "RD_file_%d", i);
		d_iov_set(&dkey, name, strlen(name));
		rc = daos_obj_punch_dkeys(root_oh, DAOS_TX_NONE, DAOS_COND_PUNCH, 1, &dkey, NULL);
		assert_rc_equal(rc, 0);
	}
	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);

	/** check how many OIDs in container before invoking the checker */
	get_nr_oids(arg->pool.poh, cname, &nr_oids);
	/** should be NR_FILES + SB + root object */
	assert_int_equal((int)nr_oids, 7);

	/** run the checker and relink leaked files */
	rc = dfs_cont_check(arg->pool.poh, cname, DFS_CHECK_PRINT | DFS_CHECK_RELINK, "tlf");
	assert_int_equal(rc, 0);

	/** check how many OIDs in container after invoking the checker */
	get_nr_oids(arg->pool.poh, cname, &nr_oids);
	/** should be NR_FILES + SB + root object + LF dir + timestamp dir */
	assert_int_equal((int)nr_oids, 9);

	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_CREAT | O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);

	/** open every file under l+f and check the chunk size */
	for (i = 0; i < NR_FILES; i++) {
		char		fpath[128];
		struct stat	stbuf;
		mode_t		mode;
		daos_size_t	chunk_size, updated;
		dfs_obj_t	*file;

		/** construct the file path */
		sprintf(fpath, "/lost+found/tlf/%"PRIu64".%"PRIu64"", foids[i].hi, foids[i].lo);

		rc = dfs_lookup(dfs, fpath, O_RDWR, &file, &mode, &stbuf);
		assert_int_equal(rc, 0);
		assert_true(S_ISREG(mode));

		rc = dfs_get_chunk_size(file, &chunk_size);
		assert_int_equal(rc, 0);

		switch (i) {
		case 0:
			/** file 0 with default chunk size (1m + 1) - should be correct */
			assert_true(stbuf.st_size == 1024 * 1024 + 1);
			assert_true(chunk_size == DFS_DEFAULT_CHUNK_SIZE);
			break;
		case 1:
			/*
			 * file 1 with 64k chunk size (32k data):
			 *
			 * Since the chunk size is smaller than the default, the chunk size reported
			 * will be the default, 1m. Since we have written originally just 32k (less
			 * than the chunk size) the file size reported will be accurate.
			 */
			assert_true(stbuf.st_size == 32*1024);
			assert_true(chunk_size == DFS_DEFAULT_CHUNK_SIZE);

			/** adjust the chunk size to actual */
			rc = dfs_file_update_chunk_size(dfs, file, csizes[1]);
			assert_int_equal(rc, 0);

			/** query */
			rc = dfs_get_chunk_size(file, &updated);
			assert_int_equal(rc, 0);
			assert_true(updated == csizes[1]);
			break;
		case 2:
			/*
			 * file 2 with 64k chunk size (1m data):
			 *
			 * Since the chunk size is smaller than the default, the chunk size reported
			 * will be the default, 1m. And since we have written originally 1m (64k to
			 * 16 dkeys); the file size reported will be 15 dkeys x 1m + 64k in the last
			 * dkey.
			 */
			assert_true(stbuf.st_size == 15 * 1024 * 1024 + 64 * 1024);
			assert_true(chunk_size == DFS_DEFAULT_CHUNK_SIZE);

			/** adjust the chunk size to actual */
			rc = dfs_file_update_chunk_size(dfs, file, csizes[2]);
			assert_int_equal(rc, 0);
			rc = dfs_get_chunk_size(file, &updated);
			assert_int_equal(rc, 0);
			assert_true(updated == csizes[2]);

			/** size should be accurate now */
			rc = dfs_get_size(dfs, file, &updated);
			assert_int_equal(rc, 0);
			assert_int_equal(updated, 1024 * 1024);
			break;
		case 3:
			/*
			 * file 3 with 2m chunk size (1m - 1 data):
			 *
			 * Since the chunk size is larger than the default, the chunk size reported
			 * will be the larger from the default chunk size and highest offset from
			 * all the dkeys. Since we wrote less than the actual chunk size and less
			 * than the default chunk size, the chunk size reported will be the default
			 * and the file size should still be accurate.
			 */
			assert_true(stbuf.st_size == 1024 * 1024 - 1);
			assert_true(chunk_size == DFS_DEFAULT_CHUNK_SIZE);

			/** adjust the chunk size to actual */
			rc = dfs_file_update_chunk_size(dfs, file, csizes[3]);
			assert_int_equal(rc, 0);
			rc = dfs_get_chunk_size(file, &updated);
			assert_int_equal(rc, 0);
			assert_true(updated == csizes[3]);

			/** size should still be accurate */
			rc = dfs_get_size(dfs, file, &updated);
			assert_int_equal(rc, 0);
			assert_int_equal(updated, 1024 * 1024 - 1);
			break;
		case 4:
			/*
			 * file 4 with 2m chunk size (2m - 3 data):
			 *
			 * Since the chunk size is larger than the default, the chunk size reported
			 * will be the larger from the default chunk size and highest offset from
			 * all the dkeys. Since we wrote less than the actual chunk size but more
			 * than the default chunk size, the chunk size reported will be the largest
			 * offset seen in the dkey (2m -3), which also what will be seen as the file
			 * size.
			 */
			assert_true(stbuf.st_size == 2 * 1024 * 1024 - 3);
			assert_true(chunk_size == 2 * 1024 * 1024 - 3);

			/** adjust the chunk size to actual */
			rc = dfs_file_update_chunk_size(dfs, file, csizes[4]);
			assert_int_equal(rc, 0);
			rc = dfs_get_chunk_size(file, &updated);
			assert_int_equal(rc, 0);
			assert_true(updated == csizes[4]);

			/** size should still be accurate */
			rc = dfs_get_size(dfs, file, &updated);
			assert_int_equal(rc, 0);
			assert_int_equal(updated, 2 * 1024 * 1024 - 3);
			break;
		default:
			D_ASSERT(0);
		}
		rc = dfs_release(file);
		assert_int_equal(rc, 0);
	}

	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_destroy(arg->pool.pool_str, arg->group, cname, 0, NULL);
	assert_rc_equal(rc, 0);
	rc = dfs_fini();
	assert_int_equal(rc, 0);
	D_FREE(buf);
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
	{ "DFS_UNIT_TEST15: DFS chown",
	  dfs_test_chown, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST16: DFS stat mtime",
	  dfs_test_mtime, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST17: multi-threads async IO",
	  dfs_test_async_io_th, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST18: async IO",
	  dfs_test_async_io, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST19: DFS readdir",
	  dfs_test_readdir, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST20: dfs oclass hints",
	  dfs_test_oclass_hints, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST21: dfs multiple pools",
	  dfs_test_multiple_pools, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST22: dfs extended attributes",
	  dfs_test_xattrs, test_case_teardown},
	{ "DFS_UNIT_TEST23: dfs MWC container checker",
	  dfs_test_checker, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST24: dfs MWC SB fix",
	  dfs_test_fix_sb, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST25: dfs MWC root fix",
	  dfs_test_relink_root, async_disable, test_case_teardown},
	{ "DFS_UNIT_TEST26: dfs MWC chunk size fix",
	  dfs_test_fix_chunk_size, async_disable, test_case_teardown},
};

static int
dfs_setup(void **state)
{
	test_arg_t	*arg;
	int		rc = 0;

	rc = test_setup(state, SETUP_POOL_CONNECT, true, DEFAULT_POOL_SIZE, 0, NULL);
	if (rc != 0)
		return rc;

	arg = *state;

	if (arg->myrank == 0) {
		bool	use_dtx = false;

		d_getenv_bool("DFS_USE_DTX", &use_dtx);
		if (use_dtx)
			print_message("Running DFS Serial tests with DTX enabled\n");
		else
			print_message("Running DFS Serial tests with DTX disabled\n");

		rc = dfs_cont_create(arg->pool.poh, &co_uuid, NULL, &co_hdl, &dfs_mt);
		assert_int_equal(rc, 0);
		print_message("Created DFS Container "DF_UUIDF"\n", DP_UUID(co_uuid));
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
	assert_success(rc);

	par_barrier(PAR_COMM_WORLD);
	if (arg->myrank == 0) {
		char str[37];

		uuid_unparse(co_uuid, str);
		rc = daos_cont_destroy(arg->pool.poh, str, 0, NULL);
		assert_rc_equal(rc, 0);
		print_message("Destroyed DFS Container "DF_UUIDF"\n", DP_UUID(co_uuid));
	}
	par_barrier(PAR_COMM_WORLD);

	return test_teardown(state);
}

int
run_dfs_unit_test(int rank, int size)
{
	int rc = 0;

	par_barrier(PAR_COMM_WORLD);
	rc = cmocka_run_group_tests_name("DAOS_FileSystem_DFS_Unit", dfs_unit_tests, dfs_setup,
					 dfs_teardown);
	par_barrier(PAR_COMM_WORLD);

	/** run tests again with DTX */
	setenv("DFS_USE_DTX", "1", 1);

	par_barrier(PAR_COMM_WORLD);
	rc += cmocka_run_group_tests_name("DAOS_FileSystem_DFS_Unit_DTX", dfs_unit_tests,
					  dfs_setup, dfs_teardown);
	par_barrier(PAR_COMM_WORLD);
	return rc;
}
