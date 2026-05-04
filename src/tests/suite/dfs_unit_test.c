/**
 * (C) Copyright 2019-2024 Intel Corporation.
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

#include "dfs_test.h"
#include <daos/dfs_lib_int.h>
#include <daos/array.h>
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
	if (l.tv_sec == r.tv_sec) {
		if (l.tv_nsec >= r.tv_nsec)
			print_error("timestamp difference of %09ld nsec\n", l.tv_nsec - r.tv_nsec);
		return l.tv_nsec < r.tv_nsec;
	} else {
		if (l.tv_sec >= r.tv_sec)
			print_error("timestamp difference of %jd sec\n", l.tv_sec - r.tv_sec);
		return l.tv_sec < r.tv_sec;
	}
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
	daos_range_t     iod_rg;
	daos_range_t    *iod_rgs;
	dfs_iod_t	iod;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	char            *buf;
	daos_size_t	read_size;
	int              i;
	int		rc;

	if (arg->myrank != 0)
		return;

	D_ALLOC_ARRAY(iod_rgs, DAOS_ARRAY_LIST_IO_LIMIT + 1);
	D_ALLOC_ARRAY(buf, DAOS_ARRAY_LIST_IO_LIMIT + 1);

	rc = dfs_open(dfs_mt, NULL, "io_error", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, 0, 0, NULL, &file);
	assert_int_equal(rc, 0);

	/** set an IOD with a large nr count that is not supported */
	iod.iod_nr = DAOS_ARRAY_LIST_IO_LIMIT + 1;
	for (i = 0; i < DAOS_ARRAY_LIST_IO_LIMIT + 1; i++) {
		iod_rgs[i].rg_idx = i + 2;
		iod_rgs[i].rg_len = 1;
	}
	iod.iod_rgs = iod_rgs;
	d_iov_set(&iov, buf, DAOS_ARRAY_LIST_IO_LIMIT + 1);
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs   = &iov;
	rc            = dfs_writex(dfs_mt, file, &iod, &sgl, NULL);
	assert_int_equal(rc, ENOTSUP);
	rc = dfs_readx(dfs_mt, file, &iod, &sgl, &read_size, NULL);
	assert_int_equal(rc, ENOTSUP);

	/*
	 * set an IOD that has writes more data than sgl to trigger error in
	 * array layer.
	 */
	iod.iod_nr = 1;
	iod_rg.rg_idx = 0;
	iod_rg.rg_len = 10;
	iod.iod_rgs   = &iod_rg;
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
	D_FREE(buf);
	D_FREE(iod_rgs);
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
	assert_int_equal(stbuf.st_blksize, DFS_DEFAULT_CHUNK_SIZE);

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
	struct timespec  ctime_orig, mtime_orig;
	mode_t           orig_mode;
	int		rc;

	if (arg->myrank != 0)
		return;

	rc = dfs_lookup(dfs_mt, "/", O_RDWR, &dir, &orig_mode, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_uid, geteuid());
	assert_int_equal(stbuf.st_gid, getegid());
	mtime_orig.tv_sec  = stbuf.st_mtim.tv_sec;
	mtime_orig.tv_nsec = stbuf.st_mtim.tv_nsec;
	ctime_orig.tv_sec  = stbuf.st_ctim.tv_sec;
	ctime_orig.tv_nsec = stbuf.st_ctim.tv_nsec;

	/** chown of root and see if visible */
	print_message("Running chown tests on root object...\n");
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_uid          = 3;
	stbuf.st_gid          = 4;
	stbuf.st_mtim.tv_sec  = mtime_orig.tv_sec + 10;
	stbuf.st_mtim.tv_nsec = mtime_orig.tv_nsec;
	stbuf.st_mode         = orig_mode | S_IROTH | S_IWOTH | S_IXOTH;
	rc                    = dfs_osetattr(dfs_mt, dir, &stbuf,
					     DFS_SET_ATTR_UID | DFS_SET_ATTR_GID | DFS_SET_ATTR_MTIME |
						 DFS_SET_ATTR_MODE);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir);
	assert_int_equal(rc, 0);

	memset(&stbuf, 0, sizeof(stbuf));
	rc = dfs_lookup(dfs_mt, "/", O_RDWR, &dir, NULL, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_mode, orig_mode | S_IROTH | S_IWOTH | S_IXOTH);
	assert_int_equal(stbuf.st_uid, 3);
	assert_int_equal(stbuf.st_gid, 4);
	assert_true(check_ts(ctime_orig, stbuf.st_ctim));
	assert_int_equal(mtime_orig.tv_sec + 10, stbuf.st_mtim.tv_sec);
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
	assert_int_equal(stbuf.st_gid, getegid());

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
printtimespec(struct timespec ts)
{
	char buf[128];

	strftime(buf, sizeof(buf), "%D %T", gmtime(&ts.tv_sec));
	print_message("\tTime: %s.%09ld UTC\n", buf, ts.tv_nsec);
	print_message("\tRaw timespec.tv_sec: %jd\n", (intmax_t)ts.tv_sec);
	print_message("\tRaw timespec.tv_nsec: %09ld\n", ts.tv_nsec);
}

static void
run_time_tests(dfs_obj_t *obj, char *name, int mode)
{
	d_sg_list_t		sgl;
	d_iov_t			iov;
	char			buf[64];
	struct stat		stbuf;
	struct timespec          prev_ts, first_ts, now;
	daos_size_t		size;
	dfs_obj_t		*tmp_obj;
	struct tm                tm = {0};
	time_t                   ts;
	char                    *p;
	struct tm               *timeptr;
	char                     time_str[64];
	int			rc;

	rc = dfs_stat(dfs_mt, NULL, name, &stbuf);
	assert_int_equal(rc, 0);
	prev_ts.tv_sec = stbuf.st_mtim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_mtim.tv_nsec;

	/** store the first modification timestamp (at creation time) */
	first_ts.tv_sec = prev_ts.tv_sec;
	first_ts.tv_nsec = prev_ts.tv_nsec;

	printf("Start Time:\n");
	printtimespec(first_ts);
	usleep(10000);
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
	usleep(10000);
	/** reset the mtime on the file/dir to the first timestamp */
	print_message("Reset mtime to Start Time at:\n");
	clock_gettime(CLOCK_REALTIME, &now);
	printtimespec(now);
	print_message("prev is:\n");
	printtimespec(prev_ts);
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_mtim.tv_sec = first_ts.tv_sec;
	stbuf.st_mtim.tv_nsec = first_ts.tv_nsec;
	rc = dfs_osetattr(dfs_mt, obj, &stbuf, DFS_SET_ATTR_MTIME);
	assert_int_equal(rc, 0);
	print_message("ctime is:\n");
	printtimespec(stbuf.st_ctim);
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

	usleep(10000);
	memset(&stbuf, 0, sizeof(stbuf));
	rc = dfs_lookupx(dfs_mt, NULL, name, O_RDWR, &tmp_obj, NULL, &stbuf, 0, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	assert_true(check_ts(prev_ts, stbuf.st_mtim));
	assert_true(check_ts(prev_ts, stbuf.st_ctim));
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	prev_ts.tv_sec = stbuf.st_mtim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_mtim.tv_nsec;

	if (S_ISREG(mode)) {
		/** set mtime and size at the same time; mtime should be what we set */
		memset(&stbuf, 0, sizeof(stbuf));
		stbuf.st_size = 1000;
		p             = strptime("2023-12-31", "%Y-%m-%d", &tm);
		assert_non_null(p);
		ts                    = mktime(&tm);
		stbuf.st_mtim.tv_sec  = ts;
		stbuf.st_mtim.tv_nsec = 0;
		rc = dfs_osetattr(dfs_mt, obj, &stbuf, DFS_SET_ATTR_SIZE | DFS_SET_ATTR_MTIME);
		assert_int_equal(rc, 0);
		assert_int_equal(stbuf.st_size, 1000);
		/** check the mtime was updated with the setattr */
		assert_int_equal(ts, stbuf.st_mtim.tv_sec);
		timeptr = localtime(&stbuf.st_mtim.tv_sec);
		strftime(time_str, sizeof(time_str), "%Y-%m-%d", timeptr);
		print_message("mtime = %s\n", time_str);
		assert_true(strncmp("2023", time_str, 4) == 0);

		memset(&stbuf, 0, sizeof(stbuf));
		rc = dfs_ostat(dfs_mt, obj, &stbuf);
		assert_int_equal(rc, 0);
		assert_int_equal(stbuf.st_size, 1000);
		timeptr = localtime(&stbuf.st_mtim.tv_sec);
		strftime(time_str, sizeof(time_str), "%Y-%m-%d", timeptr);
		assert_int_equal(ts, stbuf.st_mtim.tv_sec);
		assert_true(strncmp("2023", time_str, 4) == 0);

		memset(&stbuf, 0, sizeof(stbuf));
		stbuf.st_size = 1024;
		rc = dfs_osetattr(dfs_mt, obj, &stbuf, DFS_SET_ATTR_SIZE);
		assert_int_equal(rc, 0);
		assert_int_equal(stbuf.st_size, 1024);
		/** check the mtime was updated with the setattr */
		assert_true(check_ts(prev_ts, stbuf.st_mtim));
	}

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

	/** readdir with split anchor */
	uint32_t num_splits = 0, j;

	rc = dfs_obj_anchor_split(dir, &num_splits, NULL);
	assert_int_equal(rc, 0);
	print_message("Anchor split in %u parts\n", num_splits);

	daos_anchor_t *anchors;

	anchors       = malloc(sizeof(daos_anchor_t) * num_splits);
	num_files     = 0;
	num_dirs      = 0;
	total_entries = 0;

	for (j = 0; j < num_splits; j++) {
		daos_anchor_t *split_anchor = &anchors[j];

		memset(split_anchor, 0, sizeof(daos_anchor_t));

		rc = dfs_obj_anchor_set(dir, j, split_anchor);
		assert_int_equal(rc, 0);

		while (!daos_anchor_is_eof(split_anchor)) {
			num_ents = 10;
			rc = dfs_readdirplus(dfs_mt, dir, split_anchor, &num_ents, ents, stbufs);
			assert_int_equal(rc, 0);

			for (i = 0; i < num_ents; i++) {
				/** save the 50th entry to restart iteration from there */
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
		}
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
	test_arg_t        *arg  = *state;
	struct pl_map_attr attr = {0};
	int                rc;

	print_message("Running readdir test with OC_SX dir..\n");
	dfs_test_readdir_internal(state, OC_SX);
	if (test_runable(arg, 3)) {
		print_message("Running readdir test with OC_RP_3GX dir..\n");
		dfs_test_readdir_internal(state, OC_RP_3GX);
	}

	rc = pl_map_query(arg->pool.pool_uuid, &attr);
	assert_rc_equal(rc, 0);

	/** set the expect EC object class ID based on domain nr */
	if (attr.pa_domain_nr >= 18) {
		print_message("Running readdir test with OC_EC_16P2GX dir..\n");
		dfs_test_readdir_internal(state, OC_EC_16P2GX);
	} else if (attr.pa_domain_nr >= 10) {
		print_message("Running readdir test with OC_EC_8P2GX dir..\n");
		dfs_test_readdir_internal(state, OC_EC_8P2GX);
	} else if (attr.pa_domain_nr >= 6) {
		print_message("Running readdir test with OC_EC_4P2GX dir..\n");
		dfs_test_readdir_internal(state, OC_EC_4P2GX);
	} else {
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
	dfs_obj_t               *obj, *dir;
	daos_obj_id_t		oid;
	daos_oclass_id_t	ecidx;
	daos_prop_t             *prop = NULL;
	dfs_attr_t		dattr = {0};
	dfs_obj_info_t           oinfo = {0};
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

	/** create a directory and set EC to be used on the directory */
	rc = dfs_open(dfs_l, NULL, "d1", S_IFDIR | S_IWUSR | S_IRUSR, O_RDWR | O_CREAT, 0, 0, NULL,
		      &dir);
	assert_int_equal(rc, 0);
	rc = dfs_obj_set_oclass(dfs_l, dir, 0, ecidx);
	assert_int_equal(rc, 0);
	/** get the dir info to query what oclass will be used */
	rc = dfs_obj_get_info(dfs_l, dir, &oinfo);
	assert_int_equal(rc, 0);
	rc = compare_oclass(coh, oinfo.doi_dir_oclass_id, OC_RP_2G1);
	assert_int_equal(rc, 0);
	rc = compare_oclass(coh, oinfo.doi_file_oclass_id, ecidx);
	assert_int_equal(rc, 0);
	dfs_release(dir);

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

	/** create a directory and set EC to be used on the directory */
	rc = dfs_open(dfs_l, NULL, "d1", S_IFDIR | S_IWUSR | S_IRUSR, O_RDWR | O_CREAT, 0, 0, NULL,
		      &dir);
	assert_int_equal(rc, 0);
	rc = dfs_obj_set_oclass(dfs_l, dir, 0, ecidx);
	assert_int_equal(rc, 0);
	/** get the dir info to query what oclass will be used */
	rc = dfs_obj_get_info(dfs_l, dir, &oinfo);
	assert_int_equal(rc, 0);
	rc = compare_oclass(coh, oinfo.doi_dir_oclass_id, OC_RP_3G1);
	assert_int_equal(rc, 0);
	rc = compare_oclass(coh, oinfo.doi_file_oclass_id, ecidx);
	assert_int_equal(rc, 0);
	dfs_release(dir);

	rc = dfs_umount(dfs_l);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_success(rc);
	rc = daos_cont_destroy(arg->pool.poh, "oc_cont2", 0, NULL);
	assert_success(rc);

	/** create container with RF = 3 */
	print_message("DFS object class hints with container RF3:\n");
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	prop->dpp_entries[0].dpe_val  = DAOS_PROP_CO_REDUN_RF3;
	rc = dfs_cont_create_with_label(arg->pool.poh, "oc_cont3", &dattr, NULL, &coh, &dfs_l);
	assert_int_equal(rc, 0);

	/** set the expect EC object class ID based on domain nr */
	if (attr.pa_domain_nr >= 22)
		ecidx = OC_EC_16P3GX;
	else if (attr.pa_domain_nr >= 14)
		ecidx = OC_EC_8P3GX;
	else if (attr.pa_domain_nr >= 10)
		ecidx = OC_EC_4P3GX;
	else
		ecidx = OC_RP_4GX;

	rc = dfs_suggest_oclass(dfs_l, "file:single", &cid);
	assert_int_equal(rc, 0);
	daos_oclass_id2name(cid, oclass_name);
	print_message("oclass suggested for \"file:single\" = %s\n", oclass_name);
	rc = compare_oclass(coh, cid, OC_RP_4G1);
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
	rc = compare_oclass(coh, cid, OC_RP_4G1);
	assert_rc_equal(rc, 0);

	rc = dfs_suggest_oclass(dfs_l, "Directory:max", &cid);
	assert_int_equal(rc, 0);
	daos_oclass_id2name(cid, oclass_name);
	print_message("oclass suggested for \"Directory:max\" = %s\n", oclass_name);
	rc = compare_oclass(coh, cid, OC_RP_4GX);
	assert_rc_equal(rc, 0);

	/** create a directory and set EC to be used on the directory */
	rc = dfs_open(dfs_l, NULL, "d1", S_IFDIR | S_IWUSR | S_IRUSR, O_RDWR | O_CREAT, 0, 0, NULL,
		      &dir);
	assert_int_equal(rc, 0);
	rc = dfs_obj_set_oclass(dfs_l, dir, 0, ecidx);
	assert_int_equal(rc, 0);
	/** get the dir info to query what oclass will be used */
	rc = dfs_obj_get_info(dfs_l, dir, &oinfo);
	assert_int_equal(rc, 0);
	rc = compare_oclass(coh, oinfo.doi_dir_oclass_id, OC_RP_4G1);
	assert_int_equal(rc, 0);
	rc = compare_oclass(coh, oinfo.doi_file_oclass_id, ecidx);
	assert_int_equal(rc, 0);
	dfs_release(dir);

	assert_int_equal(rc, 0);
	rc = dfs_umount(dfs_l);
	assert_int_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_success(rc);
	rc = daos_cont_destroy(arg->pool.poh, "oc_cont3", 0, NULL);
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

	size = 0;
	rc = dfs_getxattr(dfs_mt, obj, xname1, NULL, &size);
	assert_int_equal(rc, ENODATA);

	rc = dfs_setxattr(dfs_mt, obj, xname1, NULL, size, 0);
	assert_int_equal(rc, 0);

	size = sizeof(buf);
	rc = dfs_getxattr(dfs_mt, obj, xname1, buf, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, 0);

	size = 0;
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
	dfs_obj_t               *root, *lf, *sym;
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

	/** create a symlink with a non-existent target in the container */
	rc = dfs_open(dfs, NULL, "SL1", S_IFLNK | S_IWUSR | S_IRUSR, O_RDWR | O_CREAT | O_EXCL, 0,
		      0, "/usr/local", &sym);
	assert_int_equal(rc, 0);
	rc = dfs_release(sym);

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

static void
dfs_test_oflags(void **state)
{
	test_arg_t		*arg = *state;
	dfs_obj_t		*obj;
	char			*filename_file1 = "file1";
	char			*path_file1 = "/file1";
	mode_t			create_mode = S_IWUSR | S_IRUSR;
	mode_t			mode;
	int			create_flags = O_RDWR | O_CREAT | O_EXCL;
	int			rc;
	struct stat		stbuf;

	if (arg->myrank != 0)
		return;

	/** Testing O_APPEND & O_TRUNC in dfs_open/dfs_lookup_rel */

	/** remove /file1 if existing */
	dfs_remove(dfs_mt, NULL, filename_file1, 0, NULL);

	/** Create /file1 with O_APPEND, should fail */
	rc = dfs_open(dfs_mt, NULL, filename_file1, create_mode | S_IFREG,
		      create_flags | O_APPEND, 0, 0, NULL, &obj);
	assert_int_equal(rc, ENOTSUP);

	/** Create /file1 with O_APPEND using dfs_lookup, should fail */
	rc = dfs_lookup(dfs_mt, path_file1, create_flags | O_APPEND, &obj, &mode, NULL);
	assert_int_equal(rc, ENOTSUP);

	/** Create /file1 and write 5 bytes */
	rc = dfs_test_file_gen(filename_file1, 0, OC_S1, 5);
	assert_int_equal(rc, 0);

	/** Create /file1 with O_TRUNC, size should be zero */
	rc = dfs_open(dfs_mt, NULL, filename_file1, create_mode | S_IFREG,
		      O_RDWR | O_TRUNC, 0, 0, NULL, &obj);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	/** verify file size after truncating */
	rc = dfs_lookup(dfs_mt, path_file1, O_RDONLY, &obj, &mode, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_size, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	rc = dfs_remove(dfs_mt, NULL, filename_file1, 0, NULL);
	assert_int_equal(rc, 0);

	/** Create /file1 and write 5 bytes */
	rc = dfs_test_file_gen(filename_file1, 0, OC_S1, 5);
	assert_int_equal(rc, 0);

	/** Create /file1 with O_TRUNC, size should be zero */
	rc = dfs_lookup(dfs_mt, path_file1, O_RDWR | O_TRUNC, &obj, &mode, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	/** verify file size after truncating */
	rc = dfs_lookup(dfs_mt, path_file1, O_RDONLY, &obj, &mode, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_size, 0);
	rc = dfs_release(obj);
	assert_int_equal(rc, 0);

	rc = dfs_remove(dfs_mt, NULL, filename_file1, 0, NULL);
	assert_int_equal(rc, 0);
}

#define NUM_ENTRIES	1024
#define NR_ENUM		64

static void
test_pipeline_find(void **state, daos_oclass_id_t dir_oclass)
{
#ifndef BUILD_PIPELINE
	skip();
#endif
	dfs_obj_t	*dir1, *f1;
	int		i;
	time_t		ts = 0;
	mode_t		create_mode = S_IWUSR | S_IRUSR;
	int		create_flags = O_RDWR | O_CREAT | O_EXCL;
	char		*dirname = "pipeline_dir";
	int		rc;

	rc = dfs_open(dfs_mt, NULL, dirname, create_mode | S_IFDIR, create_flags, dir_oclass, 0,
		      NULL, &dir1);
	assert_int_equal(rc, 0);

	for (i = 0; i < NUM_ENTRIES; i++) {
		char name[24];

		/* create 1 dir for every 100 files */
		if (i % 100 == 0) {
			sprintf(name, "dir.%d", i);
			rc = dfs_mkdir(dfs_mt, dir1, name, create_mode | S_IFDIR, 0);
			assert_int_equal(rc, 0);
		} else {
			daos_obj_id_t oid;

			sprintf(name, "file.%d", i);
			rc = dfs_open(dfs_mt, dir1, name, create_mode | S_IFREG, create_flags, 0, 0,
				      NULL, &f1);
			assert_int_equal(rc, 0);

			dfs_obj2id(f1, &oid);
			/* printf("File %s \t OID: %"PRIu64".%"PRIu64"\n", name, oid.hi, oid.lo); */

			rc = dfs_release(f1);
			assert_int_equal(rc, 0);
		}

		if (i == NUM_ENTRIES / 2) {
			sleep(1);
			ts = time(NULL);
			sleep(1);
		}
	}

	/** sleep to avoid DER_INPROGRESS errors since pipeline currently does not retry */
	sleep(10);

	dfs_predicate_t pred = {0};
	dfs_pipeline_t *dpipe = NULL;

	strcpy(pred.dp_name, "%.6%");
	pred.dp_newer = ts;
	rc = dfs_pipeline_create(dfs_mt, pred, DFS_FILTER_NAME | DFS_FILTER_NEWER, &dpipe);
	assert_int_equal(rc, 0);

	uint32_t num_split = 0, j;

	rc = dfs_obj_anchor_split(dir1, &num_split, NULL);
	assert_int_equal(rc, 0);
	print_message("Anchor split in %u parts\n", num_split);

	daos_anchor_t *anchors;
	struct dirent *dents = NULL;
	daos_obj_id_t *oids = NULL;
	daos_size_t *csizes = NULL;

	anchors = malloc(sizeof(daos_anchor_t) * num_split);
	dents = malloc (sizeof(struct dirent) * NR_ENUM);
	oids = calloc(NR_ENUM, sizeof(daos_obj_id_t));
	csizes = calloc(NR_ENUM, sizeof(daos_size_t));

	uint64_t nr_total = 0, nr_matched = 0, nr_scanned;

	for (j = 0; j < num_split; j++) {
		daos_anchor_t *anchor = &anchors[j];
		uint32_t nr;

		memset(anchor, 0, sizeof(daos_anchor_t));

		rc = dfs_obj_anchor_set(dir1, j, anchor);
		assert_int_equal(rc, 0);

		while (!daos_anchor_is_eof(anchor)) {
			nr = NR_ENUM;
			rc = dfs_readdir_with_filter(dfs_mt, dir1, dpipe, anchor, &nr, dents, oids,
						     csizes, &nr_scanned);
			/*
			 * It is still possible to get INPROGRESS even with the sleep, so let's just
			 * skip the test in this case.
			 */
			if (rc == -DER_INPROGRESS) {
				print_message("dfs_readdir_with_filter() returned -DER_INPROGRESS; "
					      "skipping test!\n");
				free(dents);
				free(anchors);
				free(oids);
				free(csizes);
				dfs_pipeline_destroy(dpipe);
				rc = dfs_release(dir1);
				assert_int_equal(rc, 0);
				rc = dfs_remove(dfs_mt, NULL, dirname, true, NULL);
				assert_int_equal(rc, 0);
				skip();
			}
			assert_int_equal(rc, 0);

			nr_total += nr_scanned;
			nr_matched += nr;

			for (i = 0; i < nr; i++) {
				print_message("Name: %s\t", dents[i].d_name);
				print_message("OID: %"PRIu64".%"PRIu64"\t", oids[i].hi, oids[i].lo);
				print_message("CSIZE = %zu\n", csizes[i]);
				if (dents[i].d_type == DT_DIR)
					print_message("Type: DIR\n");
				else if (dents[i].d_type == DT_REG)
					print_message("Type: FILE\n");
				else
					assert(0);
			}
		}
	}

	print_message("total entries scanned = %"PRIu64"\n", nr_total);
	print_message("total entries matched = %"PRIu64"\n", nr_matched);

	free(dents);
	free(anchors);
	free(oids);
	free(csizes);
	rc = dfs_pipeline_destroy(dpipe);
	assert_int_equal(rc, 0);
	/** close / finalize */
	rc = dfs_release(dir1);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, dirname, true, NULL);
	assert_int_equal(rc, 0);
}

static void
dfs_test_pipeline_find(void **state)
{
	print_message("Running Pipeline Find test with Dir oclass OC_SX\n");
	test_pipeline_find(state, OC_SX);
	print_message("Running Pipeline Find test with Dir oclass OC_RP_2GX\n");
	test_pipeline_find(state, OC_RP_2GX);
	print_message("Running Pipeline Find test with Dir oclass OC_RP_3GX\n");
	test_pipeline_find(state, OC_RP_3GX);
}

/**
 * Test hardlink functionality:
 * 1. Create 2 directories
 * 2. Create a file in the first directory
 * 3. Stat the file and verify nlink == 1
 * 4. Create a hardlink to the file in the same directory
 * 5. Stat both files - verify nlink == 2 and other properties match
 * 6. Create a hardlink in the second directory
 * 7. Stat all 3 files - verify nlink == 3 and properties match
 * 8. Delete first file - verify nlink == 2
 * 9. Delete third file - verify nlink == 1
 * 10. Delete remaining file - verify object is removed (stat returns ENOENT)
 */
static void
dfs_test_hardlink(void **state)
{
	test_arg_t   *arg = *state;
	dfs_obj_t    *dir1, *dir2;
	dfs_obj_t    *file1, *file2, *file3;
	struct stat   stbuf1, stbuf2, stbuf3;
	struct stat   stbuf_orig, stbuf_prev;
	daos_obj_id_t oid1, oid2, oid3, removed_oid;
	int           rc;

	if (arg->myrank != 0)
		return;

	print_message("Creating 2 directories...\n");

	/* Step 1: Create 2 directories */
	rc = dfs_open(dfs_mt, NULL, "hldir1", S_IFDIR | S_IWUSR | S_IRUSR | S_IXUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &dir1);
	assert_int_equal(rc, 0);

	rc = dfs_open(dfs_mt, NULL, "hldir2", S_IFDIR | S_IWUSR | S_IRUSR | S_IXUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &dir2);
	assert_int_equal(rc, 0);

	/* Step 2: Create a file in the first directory */
	print_message("Creating file in first directory...\n");
	rc = dfs_open(dfs_mt, dir1, "testfile", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file1);
	assert_int_equal(rc, 0);

	/* Get the object ID for later comparison */
	rc = dfs_obj2id(file1, &oid1);
	assert_int_equal(rc, 0);

	/* Step 3: Stat the file and save info - nlink should be 1 */
	print_message("Stat original file - expecting nlink=1...\n");
	rc = dfs_ostat(dfs_mt, file1, &stbuf_orig);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf_orig.st_nlink, 1);
	print_message("  st_nlink = %lu (expected 1)\n", (unsigned long)stbuf_orig.st_nlink);
	print_message("  st_ino   = %lu\n", (unsigned long)stbuf_orig.st_ino);
	print_message("  st_mode  = 0%o\n", stbuf_orig.st_mode);
	print_message("  st_uid   = %u\n", stbuf_orig.st_uid);
	print_message("  st_gid   = %u\n", stbuf_orig.st_gid);
	print_message("  st_mtim  = %ld.%09ld\n", stbuf_orig.st_mtim.tv_sec,
		      stbuf_orig.st_mtim.tv_nsec);
	print_message("  st_ctim  = %ld.%09ld\n", stbuf_orig.st_ctim.tv_sec,
		      stbuf_orig.st_ctim.tv_nsec);

	/* Save for later comparison */
	stbuf_prev = stbuf_orig;

	/* Step 4: Create a hardlink in the same directory */
	print_message("Creating hardlink in same directory...\n");
	rc = dfs_link(dfs_mt, file1, dir1, "testfile_link1", &file2, &stbuf2);
	assert_int_equal(rc, 0);

	/* Get the object ID - should match original */
	rc = dfs_obj2id(file2, &oid2);
	assert_int_equal(rc, 0);
	assert_true(oid1.lo == oid2.lo && oid1.hi == oid2.hi);

	/* Step 5: Stat first and second files - nlink should be 2 */
	print_message("Stat both files - expecting nlink=2...\n");
	rc = dfs_ostat(dfs_mt, file1, &stbuf1);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf1.st_nlink, 2);
	print_message("  file1: st_nlink = %lu (expected 2)\n", (unsigned long)stbuf1.st_nlink);

	/* stbuf2 was already filled by dfs_link */
	assert_int_equal(stbuf2.st_nlink, 2);
	print_message("  file2: st_nlink = %lu (expected 2)\n", (unsigned long)stbuf2.st_nlink);

	/* Verify other properties match (inode, mode, uid, gid should be same) */
	assert_int_equal(stbuf1.st_ino, stbuf2.st_ino);
	assert_int_equal(stbuf1.st_ino, stbuf_orig.st_ino);
	assert_int_equal(stbuf1.st_mode & ~S_IFMT, stbuf_orig.st_mode & ~S_IFMT);
	assert_int_equal(stbuf1.st_uid, stbuf_orig.st_uid);
	assert_int_equal(stbuf1.st_gid, stbuf_orig.st_gid);

	/*
	 * POSIX: Creating a hardlink updates ctime (metadata changed).
	 * mtime should NOT change (file content not modified).
	 */
	print_message("  Checking ctime/mtime after first hardlink creation...\n");
	assert_true(check_ts(stbuf_prev.st_ctim, stbuf1.st_ctim));
	print_message("  Verified: ctime updated (prev < current)\n");
	/* mtime should remain unchanged */
	assert_int_equal(stbuf1.st_mtim.tv_sec, stbuf_orig.st_mtim.tv_sec);
	assert_int_equal(stbuf1.st_mtim.tv_nsec, stbuf_orig.st_mtim.tv_nsec);
	print_message("  Verified: mtime unchanged\n");
	print_message("  Verified: ino, mode, uid, gid match original\n");

	/* Save current state for next comparison */
	stbuf_prev = stbuf1;

	/* Step 6: Create a hardlink in the second directory */
	print_message("Creating hardlink in second directory...\n");
	rc = dfs_link(dfs_mt, file1, dir2, "testfile_link2", &file3, &stbuf3);
	assert_int_equal(rc, 0);

	/* Get the object ID - should match original */
	rc = dfs_obj2id(file3, &oid3);
	assert_int_equal(rc, 0);
	assert_true(oid1.lo == oid3.lo && oid1.hi == oid3.hi);

	/* Step 7: Stat all 3 files - nlink should be 3 */
	print_message("Stat all 3 files - expecting nlink=3...\n");
	rc = dfs_ostat(dfs_mt, file1, &stbuf1);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf1.st_nlink, 3);
	print_message("  file1: st_nlink = %lu (expected 3)\n", (unsigned long)stbuf1.st_nlink);

	rc = dfs_ostat(dfs_mt, file2, &stbuf2);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf2.st_nlink, 3);
	print_message("  file2: st_nlink = %lu (expected 3)\n", (unsigned long)stbuf2.st_nlink);

	/* stbuf3 was already filled by dfs_link, but let's re-stat to be sure */
	rc = dfs_ostat(dfs_mt, file3, &stbuf3);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf3.st_nlink, 3);
	print_message("  file3: st_nlink = %lu (expected 3)\n", (unsigned long)stbuf3.st_nlink);

	/* All should have same inode */
	assert_int_equal(stbuf1.st_ino, stbuf2.st_ino);
	assert_int_equal(stbuf2.st_ino, stbuf3.st_ino);
	assert_int_equal(stbuf1.st_ino, stbuf_orig.st_ino);
	print_message("  Verified: all files have same inode\n");

	/*
	 * POSIX: Creating a hardlink updates ctime (metadata changed).
	 * mtime should NOT change (file content not modified).
	 */
	print_message("  Checking ctime/mtime after second hardlink creation...\n");
	assert_true(check_ts(stbuf_prev.st_ctim, stbuf1.st_ctim));
	print_message("  Verified: ctime updated (prev < current)\n");
	/* mtime should remain unchanged from original */
	assert_int_equal(stbuf1.st_mtim.tv_sec, stbuf_orig.st_mtim.tv_sec);
	assert_int_equal(stbuf1.st_mtim.tv_nsec, stbuf_orig.st_mtim.tv_nsec);
	print_message("  Verified: mtime unchanged from original\n");

	/* Save current state for next comparison */
	stbuf_prev = stbuf1;

	/* Step 8: Delete the first file and check stat */
	print_message("Removing first file...\n");
	rc = dfs_remove(dfs_mt, dir1, "testfile", false, &removed_oid);
	assert_int_equal(rc, 0);

	/* file2 and file3 should now have nlink=2 */
	rc = dfs_ostat(dfs_mt, file2, &stbuf2);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf2.st_nlink, 2);
	print_message("  file2: st_nlink = %lu (expected 2)\n", (unsigned long)stbuf2.st_nlink);

	rc = dfs_ostat(dfs_mt, file3, &stbuf3);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf3.st_nlink, 2);
	print_message("  file3: st_nlink = %lu (expected 2)\n", (unsigned long)stbuf3.st_nlink);

	/* file1 handle should still be valid but stat via name should fail */
	rc = dfs_stat(dfs_mt, dir1, "testfile", &stbuf1);
	assert_int_equal(rc, ENOENT);
	print_message("  Verified: original name no longer exists (ENOENT)\n");

	/*
	 * POSIX: Removing a hardlink updates ctime (metadata changed - link count decreased).
	 * mtime should NOT change (file content not modified).
	 */
	print_message("  Checking ctime/mtime after first unlink...\n");
	assert_true(check_ts(stbuf_prev.st_ctim, stbuf2.st_ctim));
	print_message("  Verified: ctime updated (prev < current)\n");
	/* mtime should remain unchanged from original */
	assert_int_equal(stbuf2.st_mtim.tv_sec, stbuf_orig.st_mtim.tv_sec);
	assert_int_equal(stbuf2.st_mtim.tv_nsec, stbuf_orig.st_mtim.tv_nsec);
	print_message("  Verified: mtime unchanged from original\n");

	/* Save current state for next comparison */
	stbuf_prev = stbuf2;

	/* Step 9: Delete the third file (in dir2) and check stat */
	print_message("Removing third file (from dir2)...\n");
	rc = dfs_remove(dfs_mt, dir2, "testfile_link2", false, NULL);
	assert_int_equal(rc, 0);

	/* file2 should now have nlink=1 */
	rc = dfs_ostat(dfs_mt, file2, &stbuf2);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf2.st_nlink, 1);
	print_message("  file2: st_nlink = %lu (expected 1)\n", (unsigned long)stbuf2.st_nlink);

	/*
	 * POSIX: Removing a hardlink updates ctime (metadata changed - link count decreased).
	 * mtime should NOT change (file content not modified).
	 */
	print_message("  Checking ctime/mtime after second unlink...\n");
	assert_true(check_ts(stbuf_prev.st_ctim, stbuf2.st_ctim));
	print_message("  Verified: ctime updated (prev < current)\n");
	/* mtime should remain unchanged from original */
	assert_int_equal(stbuf2.st_mtim.tv_sec, stbuf_orig.st_mtim.tv_sec);
	assert_int_equal(stbuf2.st_mtim.tv_nsec, stbuf_orig.st_mtim.tv_nsec);
	print_message("  Verified: mtime unchanged from original\n");

	/* Step 10: Delete the last remaining file */
	print_message("Removing last file...\n");
	rc = dfs_remove(dfs_mt, dir1, "testfile_link1", false, &removed_oid);
	assert_int_equal(rc, 0);

	/*
	 * Verify the object is really deleted by trying to stat via
	 * the open handle - should return ENOENT after the object
	 * is punched/deleted from the backend.
	 */
	rc = dfs_ostat(dfs_mt, file2, &stbuf2);
	assert_int_equal(rc, ENOENT);
	print_message("  Verified: object is deleted (stat on handle returns ENOENT)\n");

	/* Also verify stat by name fails */
	rc = dfs_stat(dfs_mt, dir1, "testfile_link1", &stbuf2);
	assert_int_equal(rc, ENOENT);
	print_message("  Verified: name no longer exists (ENOENT)\n");

	/* Cleanup: release all handles */
	rc = dfs_release(file1);
	assert_int_equal(rc, 0);
	rc = dfs_release(file2);
	assert_int_equal(rc, 0);
	rc = dfs_release(file3);
	assert_int_equal(rc, 0);

	/* Remove directories */
	rc = dfs_release(dir1);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir2);
	assert_int_equal(rc, 0);

	rc = dfs_remove(dfs_mt, NULL, "hldir1", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "hldir2", false, NULL);
	assert_int_equal(rc, 0);

	print_message("Hardlink test completed successfully!\n");
}

static void
dfs_test_hardlink_chmod_chown(void **state)
{
	test_arg_t   *arg = *state;
	dfs_obj_t    *dir1, *dir2;
	dfs_obj_t    *file1, *file2, *file3;
	struct stat   stbuf1, stbuf2, stbuf3;
	struct stat   stbuf_orig, stbuf_prev;
	daos_obj_id_t oid1, oid2, oid3;
	mode_t        orig_mode, new_mode;
	uid_t         orig_uid, new_uid;
	gid_t         orig_gid, new_gid;
	int           rc;

	if (arg->myrank != 0)
		return;

	print_message("=== Hardlink chmod/chown test ===\n");

	/* Step 1: Create 2 directories */
	print_message("Creating 2 directories...\n");
	rc = dfs_open(dfs_mt, NULL, "hl_chmod_dir1", S_IFDIR | S_IWUSR | S_IRUSR | S_IXUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &dir1);
	assert_int_equal(rc, 0);

	rc = dfs_open(dfs_mt, NULL, "hl_chmod_dir2", S_IFDIR | S_IWUSR | S_IRUSR | S_IXUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &dir2);
	assert_int_equal(rc, 0);

	/* Step 2: Create original file in dir1 */
	print_message("Creating original file in dir1...\n");
	rc = dfs_open(dfs_mt, dir1, "original", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file1);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(file1, &oid1);
	assert_int_equal(rc, 0);

	/* Get original stats */
	rc = dfs_ostat(dfs_mt, file1, &stbuf_orig);
	assert_int_equal(rc, 0);
	orig_mode = stbuf_orig.st_mode;
	orig_uid  = stbuf_orig.st_uid;
	orig_gid  = stbuf_orig.st_gid;
	print_message("  Original: mode=0%o, uid=%u, gid=%u, nlink=%lu\n", orig_mode, orig_uid,
		      orig_gid, (unsigned long)stbuf_orig.st_nlink);
	assert_int_equal(stbuf_orig.st_nlink, 1);

	/* Step 3: Create hardlink in same directory (dir1) */
	print_message("Creating hardlink 'link1' in same directory (dir1)...\n");
	rc = dfs_link(dfs_mt, file1, dir1, "link1", &file2, &stbuf2);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(file2, &oid2);
	assert_int_equal(rc, 0);
	assert_true(oid1.lo == oid2.lo && oid1.hi == oid2.hi);
	print_message("  Verified: OIDs match\n");

	/* Step 4: Create hardlink in different directory (dir2) */
	print_message("Creating hardlink 'link2' in different directory (dir2)...\n");
	rc = dfs_link(dfs_mt, file1, dir2, "link2", &file3, &stbuf3);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(file3, &oid3);
	assert_int_equal(rc, 0);
	assert_true(oid1.lo == oid3.lo && oid1.hi == oid3.hi);
	print_message("  Verified: OIDs match\n");

	/* Verify all have nlink=3 */
	rc = dfs_ostat(dfs_mt, file1, &stbuf1);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf1.st_nlink, 3);
	rc = dfs_ostat(dfs_mt, file2, &stbuf2);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf2.st_nlink, 3);
	rc = dfs_ostat(dfs_mt, file3, &stbuf3);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf3.st_nlink, 3);
	print_message("  All 3 files have nlink=3\n");

	/* Save state for ctime comparison */
	stbuf_prev = stbuf1;

	/* ========== CHMOD TESTS ========== */
	print_message("\n--- Testing chmod on hardlink ---\n");

	/* Step 5: chmod on the second hardlink (link1 in dir1) */
	new_mode = S_IFREG | S_IRWXU | S_IRGRP | S_IXGRP; /* rwx for user, rx for group */
	print_message("Calling chmod on 'link1' to mode 0%o...\n", new_mode & ~S_IFMT);
	rc = dfs_chmod(dfs_mt, dir1, "link1", new_mode);
	assert_int_equal(rc, 0);

	/* Verify mode changed on ALL hardlinks */
	print_message("Verifying mode change visible on all hardlinks...\n");
	rc = dfs_ostat(dfs_mt, file1, &stbuf1);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf1.st_mode, new_mode);
	print_message("  file1 (original): mode=0%o - PASS\n", stbuf1.st_mode);

	rc = dfs_ostat(dfs_mt, file2, &stbuf2);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf2.st_mode, new_mode);
	print_message("  file2 (link1): mode=0%o - PASS\n", stbuf2.st_mode);

	rc = dfs_ostat(dfs_mt, file3, &stbuf3);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf3.st_mode, new_mode);
	print_message("  file3 (link2 in dir2): mode=0%o - PASS\n", stbuf3.st_mode);

	/* Verify ctime updated (chmod changes metadata) */
	assert_true(check_ts(stbuf_prev.st_ctim, stbuf1.st_ctim));
	print_message("  Verified: ctime updated after chmod\n");

	/* Also verify via path lookup */
	rc = dfs_stat(dfs_mt, dir1, "original", &stbuf1);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf1.st_mode, new_mode);
	rc = dfs_stat(dfs_mt, dir2, "link2", &stbuf3);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf3.st_mode, new_mode);
	print_message("  Verified via path lookup as well\n");

	stbuf_prev = stbuf1;

	/* ========== CHOWN TESTS ========== */
	print_message("\n--- Testing chown on hardlink ---\n");

	/* Step 6: chown on the third hardlink (link2 in dir2) */
	new_uid = 1000;
	new_gid = 2000;
	print_message("Calling chown on 'link2' (in dir2) to uid=%u, gid=%u...\n", new_uid,
		      new_gid);
	rc = dfs_chown(dfs_mt, dir2, "link2", new_uid, new_gid, 0);
	assert_int_equal(rc, 0);

	/* Verify uid/gid changed on ALL hardlinks */
	print_message("Verifying uid/gid change visible on all hardlinks...\n");
	rc = dfs_ostat(dfs_mt, file1, &stbuf1);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf1.st_uid, new_uid);
	assert_int_equal(stbuf1.st_gid, new_gid);
	print_message("  file1 (original): uid=%u, gid=%u - PASS\n", stbuf1.st_uid, stbuf1.st_gid);

	rc = dfs_ostat(dfs_mt, file2, &stbuf2);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf2.st_uid, new_uid);
	assert_int_equal(stbuf2.st_gid, new_gid);
	print_message("  file2 (link1): uid=%u, gid=%u - PASS\n", stbuf2.st_uid, stbuf2.st_gid);

	rc = dfs_ostat(dfs_mt, file3, &stbuf3);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf3.st_uid, new_uid);
	assert_int_equal(stbuf3.st_gid, new_gid);
	print_message("  file3 (link2 in dir2): uid=%u, gid=%u - PASS\n", stbuf3.st_uid,
		      stbuf3.st_gid);

	/* Verify ctime updated (chown changes metadata) */
	assert_true(check_ts(stbuf_prev.st_ctim, stbuf1.st_ctim));
	print_message("  Verified: ctime updated after chown\n");

	stbuf_prev = stbuf1;

	/* ========== REMOVE FILE USED FOR CHMOD AND VERIFY PERSISTENCE ========== */
	print_message("\n--- Removing link1 (chmod target) and verifying persistence ---\n");

	rc = dfs_release(file2);
	assert_int_equal(rc, 0);

	rc = dfs_remove(dfs_mt, dir1, "link1", false, NULL);
	assert_int_equal(rc, 0);
	print_message("  Removed 'link1' from dir1\n");

	/* Re-open via another path for verification */
	rc = dfs_lookup_rel(dfs_mt, dir1, "original", O_RDWR, &file2, NULL, &stbuf2);
	assert_int_equal(rc, 0);

	/* Verify changes still visible on remaining hardlinks */
	rc = dfs_ostat(dfs_mt, file1, &stbuf1);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf1.st_nlink, 2);
	assert_int_equal(stbuf1.st_mode, new_mode);
	assert_int_equal(stbuf1.st_uid, new_uid);
	assert_int_equal(stbuf1.st_gid, new_gid);
	print_message("  file1: nlink=%lu, mode=0%o, uid=%u, gid=%u - PASS\n",
		      (unsigned long)stbuf1.st_nlink, stbuf1.st_mode, stbuf1.st_uid, stbuf1.st_gid);

	rc = dfs_ostat(dfs_mt, file3, &stbuf3);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf3.st_nlink, 2);
	assert_int_equal(stbuf3.st_mode, new_mode);
	assert_int_equal(stbuf3.st_uid, new_uid);
	assert_int_equal(stbuf3.st_gid, new_gid);
	print_message("  file3: nlink=%lu, mode=0%o, uid=%u, gid=%u - PASS\n",
		      (unsigned long)stbuf3.st_nlink, stbuf3.st_mode, stbuf3.st_uid, stbuf3.st_gid);

	/* Verify ctime updated (unlink changes metadata - link count) */
	assert_true(check_ts(stbuf_prev.st_ctim, stbuf1.st_ctim));
	print_message("  Verified: ctime updated after unlink\n");

	stbuf_prev = stbuf1;

	/* ========== REMOVE ORIGINAL FILE AND VERIFY PERSISTENCE ========== */
	print_message("\n--- Removing original file and verifying persistence ---\n");

	rc = dfs_release(file1);
	assert_int_equal(rc, 0);

	rc = dfs_remove(dfs_mt, dir1, "original", false, NULL);
	assert_int_equal(rc, 0);
	print_message("  Removed 'original' from dir1\n");

	/* Verify changes still visible on last remaining hardlink */
	rc = dfs_ostat(dfs_mt, file3, &stbuf3);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf3.st_nlink, 1);
	assert_int_equal(stbuf3.st_mode, new_mode);
	assert_int_equal(stbuf3.st_uid, new_uid);
	assert_int_equal(stbuf3.st_gid, new_gid);
	print_message("  file3 (last link): nlink=%lu, mode=0%o, uid=%u, gid=%u - PASS\n",
		      (unsigned long)stbuf3.st_nlink, stbuf3.st_mode, stbuf3.st_uid, stbuf3.st_gid);

	/* Verify via path lookup as well */
	rc = dfs_stat(dfs_mt, dir2, "link2", &stbuf3);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf3.st_nlink, 1);
	assert_int_equal(stbuf3.st_mode, new_mode);
	assert_int_equal(stbuf3.st_uid, new_uid);
	assert_int_equal(stbuf3.st_gid, new_gid);
	print_message("  Verified via path lookup: link2 in dir2 has correct attributes\n");

	/* Verify ctime updated (unlink changes metadata - link count) */
	assert_true(check_ts(stbuf_prev.st_ctim, stbuf3.st_ctim));
	print_message("  Verified: ctime updated after unlink\n");

	/* ========== CLEANUP ========== */
	print_message("\n--- Cleanup ---\n");

	rc = dfs_release(file2);
	assert_int_equal(rc, 0);
	rc = dfs_release(file3);
	assert_int_equal(rc, 0);

	/* Remove last file */
	rc = dfs_remove(dfs_mt, dir2, "link2", false, NULL);
	assert_int_equal(rc, 0);

	/* Remove directories */
	rc = dfs_release(dir1);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir2);
	assert_int_equal(rc, 0);

	rc = dfs_remove(dfs_mt, NULL, "hl_chmod_dir1", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "hl_chmod_dir2", false, NULL);
	assert_int_equal(rc, 0);

	print_message("\nHardlink chmod/chown test completed successfully!\n");
}

static void
dfs_test_hardlink_rename(void **state)
{
	test_arg_t   *arg = *state;
	dfs_obj_t    *src, *src_link, *dst, *dst_link;
	dfs_obj_t    *file_a, *file_a_link, *file_b, *file_b_link;
	dfs_obj_t    *tmp_obj;
	dfs_obj_t    *dir_src, *dir_src_link, *dir_dst, *dir_dst_link;
	struct stat   stbuf;
	d_sg_list_t   sgl;
	d_iov_t       iov;
	char          src_data[64], dst_data[64], read_buf[64];
	daos_size_t   read_size;
	daos_obj_id_t oid_src, oid_dst, oid_a, oid_b, oid_tmp;
	const char   *xattr_name = "user.rename_test";
	const char   *xattr_val  = "xattr_preserved_after_rename";
	char          xattr_buf[64];
	daos_size_t   xattr_size;
	int           rc;

	if (arg->myrank != 0)
		return;

	print_message("=== Hardlink rename test ===\n");
	print_message("All files and hardlinks in different directories, with xattr validation\n");

	/* Prepare unique data patterns for source and destination */
	memset(src_data, 'S', sizeof(src_data)); /* Source pattern */
	memset(dst_data, 'D', sizeof(dst_data)); /* Destination pattern */

	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs   = &iov;

	/*
	 * ============================================================
	 * Scenario 1: Source file has hardlinks, destination is regular
	 * ============================================================
	 * /dir_src1/src1 has hardlink /dir_src1_link/src1_link
	 * /dir_dst1/dst1 is a regular file (no hardlinks)
	 * rename(src1 -> dst1) with xattr on src
	 */
	print_message("\n--- Scenario 1: Source has hardlinks, dest is regular file ---\n");

	/* Create directories */
	rc = dfs_mkdir(dfs_mt, NULL, "dir_src1", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "dir_src1", O_RDWR, &dir_src, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_mkdir(dfs_mt, NULL, "dir_src1_link", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "dir_src1_link", O_RDWR, &dir_src_link, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_mkdir(dfs_mt, NULL, "dir_dst1", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "dir_dst1", O_RDWR, &dir_dst, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("  Created directories: dir_src1/, dir_src1_link/, dir_dst1/\n");

	/* Create source file in dir_src1 */
	rc = dfs_open(dfs_mt, dir_src, "src1", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &src);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(src, &oid_src);
	assert_int_equal(rc, 0);
	print_message("  Created /dir_src1/src1, oid=" DF_OID "\n", DP_OID(oid_src));

	/* Write source data */
	d_iov_set(&iov, src_data, sizeof(src_data));
	rc = dfs_write(dfs_mt, src, &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	/* Set xattr on source file */
	rc = dfs_setxattr(dfs_mt, src, xattr_name, xattr_val, strlen(xattr_val) + 1, 0);
	assert_int_equal(rc, 0);
	print_message("  Set xattr '%s' = '%s' on src1\n", xattr_name, xattr_val);

	rc = dfs_release(src);
	assert_int_equal(rc, 0);

	/* Re-open and create hardlink in different directory */
	rc = dfs_lookup_rel(dfs_mt, dir_src, "src1", O_RDWR, &src, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_link(dfs_mt, src, dir_src_link, "src1_link", &src_link, &stbuf);
	assert_int_equal(rc, 0);
	print_message("  Created hardlink /dir_src1_link/src1_link, nlink=%lu\n",
		      (unsigned long)stbuf.st_nlink);

	rc = dfs_release(src);
	assert_int_equal(rc, 0);
	rc = dfs_release(src_link);
	assert_int_equal(rc, 0);

	/* Create destination file in dir_dst1 */
	rc = dfs_open(dfs_mt, dir_dst, "dst1", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &dst);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(dst, &oid_dst);
	assert_int_equal(rc, 0);
	print_message("  Created /dir_dst1/dst1, oid=" DF_OID "\n", DP_OID(oid_dst));

	d_iov_set(&iov, dst_data, sizeof(dst_data));
	rc = dfs_write(dfs_mt, dst, &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_release(dst);
	assert_int_equal(rc, 0);

	/* Rename src1 -> dst1 (cross-directory rename) */
	print_message("  Renaming /dir_src1/src1 -> /dir_dst1/dst1...\n");
	rc = dfs_move(dfs_mt, dir_src, "src1", dir_dst, "dst1", NULL);
	assert_int_equal(rc, 0);

	/* src1 name should not exist */
	rc = dfs_stat(dfs_mt, dir_src, "src1", &stbuf);
	assert_int_equal(rc, ENOENT);
	print_message("  /dir_src1/src1 no longer exists - PASS\n");

	/* Verify src1_link still works */
	rc = dfs_lookup_rel(dfs_mt, dir_src_link, "src1_link", O_RDWR, &src_link, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(src_link, &oid_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, src_link, &stbuf);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_src.lo && oid_tmp.hi == oid_src.hi);
	assert_int_equal(stbuf.st_nlink, 2);
	print_message("  /dir_src1_link/src1_link: oid=" DF_OID ", nlink=%lu - PASS\n",
		      DP_OID(oid_tmp), (unsigned long)stbuf.st_nlink);

	/* Verify xattr accessible via src1_link */
	xattr_size = sizeof(xattr_buf);
	memset(xattr_buf, 0, sizeof(xattr_buf));
	rc = dfs_getxattr(dfs_mt, src_link, xattr_name, xattr_buf, &xattr_size);
	assert_int_equal(rc, 0);
	assert_string_equal(xattr_buf, xattr_val);
	print_message("  xattr via src1_link: '%s' = '%s' - PASS\n", xattr_name, xattr_buf);

	rc = dfs_release(src_link);
	assert_int_equal(rc, 0);

	/* Verify dst1 now has src's oid */
	rc = dfs_lookup_rel(dfs_mt, dir_dst, "dst1", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(tmp_obj, &oid_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, tmp_obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_src.lo && oid_tmp.hi == oid_src.hi);
	assert_int_equal(stbuf.st_nlink, 2);
	print_message("  /dir_dst1/dst1: oid=" DF_OID ", nlink=%lu - PASS\n", DP_OID(oid_tmp),
		      (unsigned long)stbuf.st_nlink);

	/* Verify xattr accessible via dst1 */
	xattr_size = sizeof(xattr_buf);
	memset(xattr_buf, 0, sizeof(xattr_buf));
	rc = dfs_getxattr(dfs_mt, tmp_obj, xattr_name, xattr_buf, &xattr_size);
	assert_int_equal(rc, 0);
	assert_string_equal(xattr_buf, xattr_val);
	print_message("  xattr via dst1: '%s' = '%s' - PASS\n", xattr_name, xattr_buf);

	/* Verify content */
	memset(read_buf, 0, sizeof(read_buf));
	d_iov_set(&iov, read_buf, sizeof(read_buf));
	rc = dfs_read(dfs_mt, tmp_obj, &sgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_memory_equal(read_buf, src_data, sizeof(src_data));
	print_message("  dst1 content matches original src1 data - PASS\n");
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* Cleanup scenario 1 */
	rc = dfs_remove(dfs_mt, dir_dst, "dst1", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir_src_link, "src1_link", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_src);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_src_link);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_dst);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "dir_src1", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "dir_src1_link", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "dir_dst1", false, NULL);
	assert_int_equal(rc, 0);

	/*
	 * ============================================================
	 * Scenario 2: Source is regular file, destination has hardlinks
	 * ============================================================
	 * /dir_src2/src2 is a regular file (no hardlinks) with xattr
	 * /dir_dst2/dst2 has hardlink /dir_dst2_link/dst2_link
	 */
	print_message("\n--- Scenario 2: Source is regular, dest has hardlinks ---\n");

	/* Create directories */
	rc = dfs_mkdir(dfs_mt, NULL, "dir_src2", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "dir_src2", O_RDWR, &dir_src, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_mkdir(dfs_mt, NULL, "dir_dst2", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "dir_dst2", O_RDWR, &dir_dst, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_mkdir(dfs_mt, NULL, "dir_dst2_link", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "dir_dst2_link", O_RDWR, &dir_dst_link, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("  Created directories: dir_src2/, dir_dst2/, dir_dst2_link/\n");

	/* Create source file with xattr */
	rc = dfs_open(dfs_mt, dir_src, "src2", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &src);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(src, &oid_src);
	assert_int_equal(rc, 0);
	print_message("  Created /dir_src2/src2, oid=" DF_OID "\n", DP_OID(oid_src));

	d_iov_set(&iov, src_data, sizeof(src_data));
	rc = dfs_write(dfs_mt, src, &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_setxattr(dfs_mt, src, xattr_name, xattr_val, strlen(xattr_val) + 1, 0);
	assert_int_equal(rc, 0);
	print_message("  Set xattr '%s' on src2\n", xattr_name);

	rc = dfs_release(src);
	assert_int_equal(rc, 0);

	/* Create destination file with hardlink */
	rc = dfs_open(dfs_mt, dir_dst, "dst2", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &dst);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(dst, &oid_dst);
	assert_int_equal(rc, 0);
	print_message("  Created /dir_dst2/dst2, oid=" DF_OID "\n", DP_OID(oid_dst));

	d_iov_set(&iov, dst_data, sizeof(dst_data));
	rc = dfs_write(dfs_mt, dst, &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_release(dst);
	assert_int_equal(rc, 0);

	/* Create hardlink to dst2 in different directory */
	rc = dfs_lookup_rel(dfs_mt, dir_dst, "dst2", O_RDWR, &dst, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_link(dfs_mt, dst, dir_dst_link, "dst2_link", &dst_link, &stbuf);
	assert_int_equal(rc, 0);
	print_message("  Created hardlink /dir_dst2_link/dst2_link, nlink=%lu\n",
		      (unsigned long)stbuf.st_nlink);

	rc = dfs_release(dst);
	assert_int_equal(rc, 0);
	rc = dfs_release(dst_link);
	assert_int_equal(rc, 0);

	/* Rename src2 -> dst2 */
	print_message("  Renaming /dir_src2/src2 -> /dir_dst2/dst2...\n");
	rc = dfs_move(dfs_mt, dir_src, "src2", dir_dst, "dst2", NULL);
	assert_int_equal(rc, 0);

	/* src2 name should not exist */
	rc = dfs_stat(dfs_mt, dir_src, "src2", &stbuf);
	assert_int_equal(rc, ENOENT);
	print_message("  /dir_src2/src2 no longer exists - PASS\n");

	/* dst2 should now have src's oid with nlink=1 */
	rc = dfs_lookup_rel(dfs_mt, dir_dst, "dst2", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(tmp_obj, &oid_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, tmp_obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_src.lo && oid_tmp.hi == oid_src.hi);
	assert_int_equal(stbuf.st_nlink, 1);
	print_message("  /dir_dst2/dst2: oid=" DF_OID " (src's), nlink=%lu - PASS\n",
		      DP_OID(oid_tmp), (unsigned long)stbuf.st_nlink);
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* dst2_link should have old dst's oid with nlink=1 */
	rc = dfs_lookup_rel(dfs_mt, dir_dst_link, "dst2_link", O_RDONLY, &dst_link, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(dst_link, &oid_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, dst_link, &stbuf);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_dst.lo && oid_tmp.hi == oid_dst.hi);
	assert_int_equal(stbuf.st_nlink, 1);
	print_message("  /dir_dst2_link/dst2_link: oid=" DF_OID " (old dst), nlink=%lu - PASS\n",
		      DP_OID(oid_tmp), (unsigned long)stbuf.st_nlink);

	/* Verify xattr on dst2 (moved from src2) */
	rc = dfs_lookup_rel(dfs_mt, dir_dst, "dst2", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	xattr_size = sizeof(xattr_buf);
	memset(xattr_buf, 0, sizeof(xattr_buf));
	rc = dfs_getxattr(dfs_mt, tmp_obj, xattr_name, xattr_buf, &xattr_size);
	assert_int_equal(rc, 0);
	assert_string_equal(xattr_buf, xattr_val);
	print_message("  xattr via dst2: '%s' = '%s' - PASS\n", xattr_name, xattr_buf);

	/* Verify content of dst2 */
	memset(read_buf, 0, sizeof(read_buf));
	d_iov_set(&iov, read_buf, sizeof(read_buf));
	rc = dfs_read(dfs_mt, tmp_obj, &sgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_memory_equal(read_buf, src_data, sizeof(src_data));
	print_message("  dst2 content matches original src2 data - PASS\n");
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* Verify dst2_link content - should be old dst_data */
	memset(read_buf, 0, sizeof(read_buf));
	d_iov_set(&iov, read_buf, sizeof(read_buf));
	rc = dfs_read(dfs_mt, dst_link, &sgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_memory_equal(read_buf, dst_data, sizeof(dst_data));
	print_message("  dst2_link content matches original dst2 data - PASS\n");

	/* Cleanup scenario 2 */
	rc = dfs_release(dst_link);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir_dst, "dst2", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir_dst_link, "dst2_link", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_src);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_dst);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_dst_link);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "dir_src2", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "dir_dst2", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "dir_dst2_link", false, NULL);
	assert_int_equal(rc, 0);

	/*
	 * ============================================================
	 * Scenario 3: Both source and destination have hardlinks
	 * ============================================================
	 * /dir_a/file_a has hardlink /dir_a_link/file_a_link, with xattr
	 * /dir_b/file_b has hardlink /dir_b_link/file_b_link
	 */
	print_message("\n--- Scenario 3: Both source and dest have hardlinks ---\n");

	/* Create directories */
	rc = dfs_mkdir(dfs_mt, NULL, "dir_a", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "dir_a", O_RDWR, &dir_src, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_mkdir(dfs_mt, NULL, "dir_a_link", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "dir_a_link", O_RDWR, &dir_src_link, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_mkdir(dfs_mt, NULL, "dir_b", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "dir_b", O_RDWR, &dir_dst, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_mkdir(dfs_mt, NULL, "dir_b_link", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "dir_b_link", O_RDWR, &dir_dst_link, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("  Created directories: dir_a/, dir_a_link/, dir_b/, dir_b_link/\n");

	/* Create file_a with xattr */
	rc = dfs_open(dfs_mt, dir_src, "file_a", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file_a);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(file_a, &oid_a);
	assert_int_equal(rc, 0);
	print_message("  Created /dir_a/file_a, oid=" DF_OID "\n", DP_OID(oid_a));

	d_iov_set(&iov, src_data, sizeof(src_data));
	rc = dfs_write(dfs_mt, file_a, &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_setxattr(dfs_mt, file_a, xattr_name, xattr_val, strlen(xattr_val) + 1, 0);
	assert_int_equal(rc, 0);
	print_message("  Set xattr '%s' on file_a\n", xattr_name);

	rc = dfs_release(file_a);
	assert_int_equal(rc, 0);

	/* Create hardlink to file_a */
	rc = dfs_lookup_rel(dfs_mt, dir_src, "file_a", O_RDWR, &file_a, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_link(dfs_mt, file_a, dir_src_link, "file_a_link", &file_a_link, &stbuf);
	assert_int_equal(rc, 0);
	print_message("  Created hardlink /dir_a_link/file_a_link, nlink=%lu\n",
		      (unsigned long)stbuf.st_nlink);

	rc = dfs_release(file_a);
	assert_int_equal(rc, 0);
	rc = dfs_release(file_a_link);
	assert_int_equal(rc, 0);

	/* Create file_b with hardlink */
	rc = dfs_open(dfs_mt, dir_dst, "file_b", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file_b);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(file_b, &oid_b);
	assert_int_equal(rc, 0);
	print_message("  Created /dir_b/file_b, oid=" DF_OID "\n", DP_OID(oid_b));

	d_iov_set(&iov, dst_data, sizeof(dst_data));
	rc = dfs_write(dfs_mt, file_b, &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_release(file_b);
	assert_int_equal(rc, 0);

	rc = dfs_lookup_rel(dfs_mt, dir_dst, "file_b", O_RDWR, &file_b, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_link(dfs_mt, file_b, dir_dst_link, "file_b_link", &file_b_link, &stbuf);
	assert_int_equal(rc, 0);
	print_message("  Created hardlink /dir_b_link/file_b_link, nlink=%lu\n",
		      (unsigned long)stbuf.st_nlink);

	rc = dfs_release(file_b);
	assert_int_equal(rc, 0);
	rc = dfs_release(file_b_link);
	assert_int_equal(rc, 0);

	/* Rename file_a -> file_b */
	print_message("  Renaming /dir_a/file_a -> /dir_b/file_b...\n");
	rc = dfs_move(dfs_mt, dir_src, "file_a", dir_dst, "file_b", NULL);
	assert_int_equal(rc, 0);

	/* file_a name should not exist */
	rc = dfs_stat(dfs_mt, dir_src, "file_a", &stbuf);
	assert_int_equal(rc, ENOENT);
	print_message("  /dir_a/file_a no longer exists - PASS\n");

	/* file_a_link should have original oid, nlink=2 */
	rc = dfs_lookup_rel(dfs_mt, dir_src_link, "file_a_link", O_RDWR, &file_a_link, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(file_a_link, &oid_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, file_a_link, &stbuf);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_a.lo && oid_tmp.hi == oid_a.hi);
	assert_int_equal(stbuf.st_nlink, 2);
	print_message("  /dir_a_link/file_a_link: oid=" DF_OID ", nlink=%lu - PASS\n",
		      DP_OID(oid_tmp), (unsigned long)stbuf.st_nlink);

	/* Verify xattr via file_a_link */
	xattr_size = sizeof(xattr_buf);
	memset(xattr_buf, 0, sizeof(xattr_buf));
	rc = dfs_getxattr(dfs_mt, file_a_link, xattr_name, xattr_buf, &xattr_size);
	assert_int_equal(rc, 0);
	assert_string_equal(xattr_buf, xattr_val);
	print_message("  xattr via file_a_link: '%s' = '%s' - PASS\n", xattr_name, xattr_buf);

	rc = dfs_release(file_a_link);
	assert_int_equal(rc, 0);

	/* file_b should now have file_a's oid */
	rc = dfs_lookup_rel(dfs_mt, dir_dst, "file_b", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(tmp_obj, &oid_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, tmp_obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_a.lo && oid_tmp.hi == oid_a.hi);
	assert_int_equal(stbuf.st_nlink, 2);
	print_message("  /dir_b/file_b: oid=" DF_OID " (file_a's), nlink=%lu - PASS\n",
		      DP_OID(oid_tmp), (unsigned long)stbuf.st_nlink);

	/* Verify xattr via file_b */
	xattr_size = sizeof(xattr_buf);
	memset(xattr_buf, 0, sizeof(xattr_buf));
	rc = dfs_getxattr(dfs_mt, tmp_obj, xattr_name, xattr_buf, &xattr_size);
	assert_int_equal(rc, 0);
	assert_string_equal(xattr_buf, xattr_val);
	print_message("  xattr via file_b: '%s' = '%s' - PASS\n", xattr_name, xattr_buf);

	/* Verify content */
	memset(read_buf, 0, sizeof(read_buf));
	d_iov_set(&iov, read_buf, sizeof(read_buf));
	rc = dfs_read(dfs_mt, tmp_obj, &sgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_memory_equal(read_buf, src_data, sizeof(src_data));
	print_message("  file_b content matches original file_a data - PASS\n");
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* file_b_link should have old file_b oid, nlink=1 */
	rc = dfs_lookup_rel(dfs_mt, dir_dst_link, "file_b_link", O_RDWR, &file_b_link, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(file_b_link, &oid_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, file_b_link, &stbuf);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_b.lo && oid_tmp.hi == oid_b.hi);
	assert_int_equal(stbuf.st_nlink, 1);
	print_message("  /dir_b_link/file_b_link: oid=" DF_OID " (old file_b), nlink=%lu - PASS\n",
		      DP_OID(oid_tmp), (unsigned long)stbuf.st_nlink);

	/* Verify file_b_link content - should be old dst_data */
	memset(read_buf, 0, sizeof(read_buf));
	d_iov_set(&iov, read_buf, sizeof(read_buf));
	rc = dfs_read(dfs_mt, file_b_link, &sgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_memory_equal(read_buf, dst_data, sizeof(dst_data));
	print_message("  file_b_link content matches original file_b data - PASS\n");

	/* Cleanup scenario 3 */
	rc = dfs_release(file_b_link);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir_dst, "file_b", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir_src_link, "file_a_link", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir_dst_link, "file_b_link", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_src);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_src_link);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_dst);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_dst_link);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "dir_a", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "dir_a_link", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "dir_b", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "dir_b_link", false, NULL);
	assert_int_equal(rc, 0);

	/*
	 * ============================================================
	 * Scenario 4: Source has hardlinks, destination doesn't exist
	 * ============================================================
	 * /dir_src4/src4 has hardlink /dir_src4_link/src4_link, with xattr
	 * /dir_dst4/dst4 does not exist
	 */
	print_message("\n--- Scenario 4: Source has hardlinks, dest doesn't exist ---\n");

	/* Create directories */
	rc = dfs_mkdir(dfs_mt, NULL, "dir_src4", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "dir_src4", O_RDWR, &dir_src, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_mkdir(dfs_mt, NULL, "dir_src4_link", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "dir_src4_link", O_RDWR, &dir_src_link, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_mkdir(dfs_mt, NULL, "dir_dst4", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "dir_dst4", O_RDWR, &dir_dst, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("  Created directories: dir_src4/, dir_src4_link/, dir_dst4/\n");

	/* Create source file with xattr */
	rc = dfs_open(dfs_mt, dir_src, "src4", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &src);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(src, &oid_src);
	assert_int_equal(rc, 0);
	print_message("  Created /dir_src4/src4, oid=" DF_OID "\n", DP_OID(oid_src));

	d_iov_set(&iov, src_data, sizeof(src_data));
	rc = dfs_write(dfs_mt, src, &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_setxattr(dfs_mt, src, xattr_name, xattr_val, strlen(xattr_val) + 1, 0);
	assert_int_equal(rc, 0);
	print_message("  Set xattr '%s' on src4\n", xattr_name);

	rc = dfs_release(src);
	assert_int_equal(rc, 0);

	/* Create hardlink in different directory */
	rc = dfs_lookup_rel(dfs_mt, dir_src, "src4", O_RDWR, &src, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_link(dfs_mt, src, dir_src_link, "src4_link", &src_link, &stbuf);
	assert_int_equal(rc, 0);
	print_message("  Created hardlink /dir_src4_link/src4_link, nlink=%lu\n",
		      (unsigned long)stbuf.st_nlink);

	rc = dfs_release(src);
	assert_int_equal(rc, 0);
	rc = dfs_release(src_link);
	assert_int_equal(rc, 0);

	/* Verify dst4 does not exist */
	rc = dfs_stat(dfs_mt, dir_dst, "dst4", &stbuf);
	assert_int_equal(rc, ENOENT);
	print_message("  /dir_dst4/dst4 does not exist - confirmed\n");

	/* Rename src4 -> dst4 */
	print_message("  Renaming /dir_src4/src4 -> /dir_dst4/dst4...\n");
	rc = dfs_move(dfs_mt, dir_src, "src4", dir_dst, "dst4", NULL);
	assert_int_equal(rc, 0);

	/* src4 name should not exist */
	rc = dfs_stat(dfs_mt, dir_src, "src4", &stbuf);
	assert_int_equal(rc, ENOENT);
	print_message("  /dir_src4/src4 no longer exists - PASS\n");

	/* dst4 should have src's oid, nlink=2 */
	rc = dfs_lookup_rel(dfs_mt, dir_dst, "dst4", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(tmp_obj, &oid_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, tmp_obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_src.lo && oid_tmp.hi == oid_src.hi);
	assert_int_equal(stbuf.st_nlink, 2);
	print_message("  /dir_dst4/dst4: oid=" DF_OID ", nlink=%lu - PASS\n", DP_OID(oid_tmp),
		      (unsigned long)stbuf.st_nlink);
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* src4_link should still have original oid, nlink=2 */
	rc = dfs_lookup_rel(dfs_mt, dir_src_link, "src4_link", O_RDWR, &src_link, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(src_link, &oid_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, src_link, &stbuf);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_src.lo && oid_tmp.hi == oid_src.hi);
	assert_int_equal(stbuf.st_nlink, 2);
	print_message("  /dir_src4_link/src4_link: oid=" DF_OID ", nlink=%lu - PASS\n",
		      DP_OID(oid_tmp), (unsigned long)stbuf.st_nlink);

	/* Verify xattr via src4_link */
	xattr_size = sizeof(xattr_buf);
	memset(xattr_buf, 0, sizeof(xattr_buf));
	rc = dfs_getxattr(dfs_mt, src_link, xattr_name, xattr_buf, &xattr_size);
	assert_int_equal(rc, 0);
	assert_string_equal(xattr_buf, xattr_val);
	print_message("  xattr via src4_link: '%s' = '%s' - PASS\n", xattr_name, xattr_buf);

	rc = dfs_release(src_link);
	assert_int_equal(rc, 0);

	/* Verify xattr via dst4 */
	rc = dfs_lookup_rel(dfs_mt, dir_dst, "dst4", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	xattr_size = sizeof(xattr_buf);
	memset(xattr_buf, 0, sizeof(xattr_buf));
	rc = dfs_getxattr(dfs_mt, tmp_obj, xattr_name, xattr_buf, &xattr_size);
	assert_int_equal(rc, 0);
	assert_string_equal(xattr_buf, xattr_val);
	print_message("  xattr via dst4: '%s' = '%s' - PASS\n", xattr_name, xattr_buf);

	/* Verify content */
	memset(read_buf, 0, sizeof(read_buf));
	d_iov_set(&iov, read_buf, sizeof(read_buf));
	rc = dfs_read(dfs_mt, tmp_obj, &sgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_memory_equal(read_buf, src_data, sizeof(src_data));
	print_message("  dst4 content matches original src4 data - PASS\n");
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* Cleanup scenario 4 */
	rc = dfs_remove(dfs_mt, dir_dst, "dst4", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir_src_link, "src4_link", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_src);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_src_link);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_dst);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "dir_src4", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "dir_src4_link", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "dir_dst4", false, NULL);
	assert_int_equal(rc, 0);

	print_message("\nHardlink rename test completed successfully!\n");
}

static void
dfs_test_hardlink_xattr(void **state)
{
	test_arg_t   *arg = *state;
	dfs_obj_t    *file1, *file2;
	struct stat   stbuf;
	const char   *xname1 = "user.attr1";
	const char   *xname2 = "user.attr2";
	const char   *xval1  = "value1";
	const char   *xval2  = "value2";
	daos_size_t   size;
	char          buf[64];
	daos_obj_id_t oid_orig, oid_tmp;
	int           rc;

	if (arg->myrank != 0)
		return;

	print_message("=== Hardlink xattr test ===\n");

	/*
	 * ============================================================
	 * Part 1: Test xattr sharing with hardlinks
	 * ============================================================
	 * - Create file, set xname1
	 * - Create hardlink, set xname2 on hardlink
	 * - Both xnames should be visible on both files
	 * - Delete first file
	 * - Both xattrs still visible on second file
	 */
	print_message("\n--- Part 1: xattr sharing across hardlinks ---\n");

	/* Create first file */
	rc = dfs_open(dfs_mt, NULL, "xattr_file1", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file1);
	assert_int_equal(rc, 0);

	/* Record oid immediately after creation */
	rc = dfs_obj2id(file1, &oid_orig);
	assert_int_equal(rc, 0);
	print_message("  Created xattr_file1, oid=" DF_OID "\n", DP_OID(oid_orig));

	/* Set xname1 on first file BEFORE hardlink is created */
	rc = dfs_setxattr(dfs_mt, file1, xname1, xval1, strlen(xval1) + 1, 0);
	assert_int_equal(rc, 0);
	print_message("  Set xattr '%s' = '%s' on file1\n", xname1, xval1);

	/* Verify xname1 is set */
	size = sizeof(buf);
	memset(buf, 0, sizeof(buf));
	rc = dfs_getxattr(dfs_mt, file1, xname1, buf, &size);
	assert_int_equal(rc, 0);
	assert_string_equal(buf, xval1);
	print_message("  Verified xattr on file1 - PASS\n");

	/* Release handle */
	rc = dfs_release(file1);
	assert_int_equal(rc, 0);

	/* Re-open and create hardlink */
	rc = dfs_lookup_rel(dfs_mt, NULL, "xattr_file1", O_RDWR, &file1, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_link(dfs_mt, file1, NULL, "xattr_file2", &file2, &stbuf);
	assert_int_equal(rc, 0);
	print_message("  Created hardlink xattr_file2\n");

	/* Verify oid matches */
	rc = dfs_obj2id(file2, &oid_tmp);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_orig.lo && oid_tmp.hi == oid_orig.hi);
	assert_int_equal(stbuf.st_nlink, 2);
	print_message("  Hardlink oid=" DF_OID " (matches), nlink=%lu\n", DP_OID(oid_tmp),
		      (unsigned long)stbuf.st_nlink);

	/* Set xname2 on the hardlink (file2) */
	rc = dfs_setxattr(dfs_mt, file2, xname2, xval2, strlen(xval2) + 1, 0);
	assert_int_equal(rc, 0);
	print_message("  Set xattr '%s' = '%s' on file2 (hardlink)\n", xname2, xval2);

	/* Verify both xattrs are visible on file1 */
	print_message("  Verifying both xattrs visible on file1...\n");
	size = sizeof(buf);
	memset(buf, 0, sizeof(buf));
	rc = dfs_getxattr(dfs_mt, file1, xname1, buf, &size);
	assert_int_equal(rc, 0);
	assert_string_equal(buf, xval1);
	print_message("    file1 has '%s' = '%s' - PASS\n", xname1, xval1);

	size = sizeof(buf);
	memset(buf, 0, sizeof(buf));
	rc = dfs_getxattr(dfs_mt, file1, xname2, buf, &size);
	assert_int_equal(rc, 0);
	assert_string_equal(buf, xval2);
	print_message("    file1 has '%s' = '%s' - PASS\n", xname2, xval2);

	/* Verify both xattrs are visible on file2 */
	print_message("  Verifying both xattrs visible on file2...\n");
	size = sizeof(buf);
	memset(buf, 0, sizeof(buf));
	rc = dfs_getxattr(dfs_mt, file2, xname1, buf, &size);
	assert_int_equal(rc, 0);
	assert_string_equal(buf, xval1);
	print_message("    file2 has '%s' = '%s' - PASS\n", xname1, xval1);

	size = sizeof(buf);
	memset(buf, 0, sizeof(buf));
	rc = dfs_getxattr(dfs_mt, file2, xname2, buf, &size);
	assert_int_equal(rc, 0);
	assert_string_equal(buf, xval2);
	print_message("    file2 has '%s' = '%s' - PASS\n", xname2, xval2);

	/* Verify listxattr shows both on file1 */
	size = sizeof(buf);
	memset(buf, 0, sizeof(buf));
	rc = dfs_listxattr(dfs_mt, file1, buf, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, strlen(xname1) + 1 + strlen(xname2) + 1);
	print_message("  listxattr on file1 shows both xattrs - PASS\n");

	/* Release handles */
	rc = dfs_release(file1);
	assert_int_equal(rc, 0);
	rc = dfs_release(file2);
	assert_int_equal(rc, 0);

	/* Delete first file */
	print_message("  Removing xattr_file1...\n");
	rc = dfs_remove(dfs_mt, NULL, "xattr_file1", false, NULL);
	assert_int_equal(rc, 0);

	/* Re-open file2 and verify xattrs still exist */
	rc = dfs_lookup_rel(dfs_mt, NULL, "xattr_file2", O_RDWR, &file2, NULL, NULL);
	assert_int_equal(rc, 0);

	/* Verify oid and nlink */
	rc = dfs_obj2id(file2, &oid_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, file2, &stbuf);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_orig.lo && oid_tmp.hi == oid_orig.hi);
	assert_int_equal(stbuf.st_nlink, 1);
	print_message("  file2 after deletion: oid=" DF_OID ", nlink=%lu\n", DP_OID(oid_tmp),
		      (unsigned long)stbuf.st_nlink);

	/* Verify both xattrs still visible on file2 */
	print_message("  Verifying xattrs still visible after file1 deletion...\n");
	size = sizeof(buf);
	memset(buf, 0, sizeof(buf));
	rc = dfs_getxattr(dfs_mt, file2, xname1, buf, &size);
	assert_int_equal(rc, 0);
	assert_string_equal(buf, xval1);
	print_message("    file2 still has '%s' = '%s' - PASS\n", xname1, xval1);

	size = sizeof(buf);
	memset(buf, 0, sizeof(buf));
	rc = dfs_getxattr(dfs_mt, file2, xname2, buf, &size);
	assert_int_equal(rc, 0);
	assert_string_equal(buf, xval2);
	print_message("    file2 still has '%s' = '%s' - PASS\n", xname2, xval2);

	/* Cleanup part 1 */
	rc = dfs_release(file2);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "xattr_file2", false, NULL);
	assert_int_equal(rc, 0);

	/*
	 * ============================================================
	 * Part 2: Test xattr removal with hardlinks
	 * ============================================================
	 * - Create file, set xname1
	 * - Create hardlink, set xname2 on hardlink
	 * - Remove xattr from first file
	 * - Verify removal is visible in both (xattr gone from both)
	 */
	print_message("\n--- Part 2: xattr removal across hardlinks ---\n");

	/* Create first file */
	rc = dfs_open(dfs_mt, NULL, "xattr_rm_file1", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file1);
	assert_int_equal(rc, 0);

	/* Record oid immediately after creation */
	rc = dfs_obj2id(file1, &oid_orig);
	assert_int_equal(rc, 0);
	print_message("  Created xattr_rm_file1, oid=" DF_OID "\n", DP_OID(oid_orig));

	/* Set xname1 on first file */
	rc = dfs_setxattr(dfs_mt, file1, xname1, xval1, strlen(xval1) + 1, 0);
	assert_int_equal(rc, 0);
	print_message("  Set xattr '%s' = '%s' on file1\n", xname1, xval1);

	/* Release handle */
	rc = dfs_release(file1);
	assert_int_equal(rc, 0);

	/* Re-open and create hardlink */
	rc = dfs_lookup_rel(dfs_mt, NULL, "xattr_rm_file1", O_RDWR, &file1, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_link(dfs_mt, file1, NULL, "xattr_rm_file2", &file2, &stbuf);
	assert_int_equal(rc, 0);
	print_message("  Created hardlink xattr_rm_file2\n");

	/* Set xname2 on the hardlink (file2) */
	rc = dfs_setxattr(dfs_mt, file2, xname2, xval2, strlen(xval2) + 1, 0);
	assert_int_equal(rc, 0);
	print_message("  Set xattr '%s' = '%s' on file2 (hardlink)\n", xname2, xval2);

	/* Verify both xattrs visible on both files */
	print_message("  Verifying both xattrs on both files before removal...\n");
	size = sizeof(buf);
	rc   = dfs_getxattr(dfs_mt, file1, xname1, buf, &size);
	assert_int_equal(rc, 0);
	size = sizeof(buf);
	rc   = dfs_getxattr(dfs_mt, file1, xname2, buf, &size);
	assert_int_equal(rc, 0);
	size = sizeof(buf);
	rc   = dfs_getxattr(dfs_mt, file2, xname1, buf, &size);
	assert_int_equal(rc, 0);
	size = sizeof(buf);
	rc   = dfs_getxattr(dfs_mt, file2, xname2, buf, &size);
	assert_int_equal(rc, 0);
	print_message("    Both files have both xattrs - PASS\n");

	/* Remove xname1 from file1 */
	print_message("  Removing xattr '%s' from file1...\n", xname1);
	rc = dfs_removexattr(dfs_mt, file1, xname1);
	assert_int_equal(rc, 0);

	/* Verify xname1 is gone from file1 */
	size = sizeof(buf);
	rc   = dfs_getxattr(dfs_mt, file1, xname1, buf, &size);
	assert_int_equal(rc, ENODATA);
	print_message("    file1: '%s' removed (ENODATA) - PASS\n", xname1);

	/* Verify xname1 is also gone from file2 (shared xattr) */
	size = sizeof(buf);
	rc   = dfs_getxattr(dfs_mt, file2, xname1, buf, &size);
	assert_int_equal(rc, ENODATA);
	print_message("    file2: '%s' also removed (ENODATA) - PASS\n", xname1);

	/* Verify xname2 still exists on both */
	size = sizeof(buf);
	memset(buf, 0, sizeof(buf));
	rc = dfs_getxattr(dfs_mt, file1, xname2, buf, &size);
	assert_int_equal(rc, 0);
	assert_string_equal(buf, xval2);
	print_message("    file1: '%s' still exists - PASS\n", xname2);

	size = sizeof(buf);
	memset(buf, 0, sizeof(buf));
	rc = dfs_getxattr(dfs_mt, file2, xname2, buf, &size);
	assert_int_equal(rc, 0);
	assert_string_equal(buf, xval2);
	print_message("    file2: '%s' still exists - PASS\n", xname2);

	/* Verify listxattr shows only xname2 on both files */
	size = sizeof(buf);
	memset(buf, 0, sizeof(buf));
	rc = dfs_listxattr(dfs_mt, file1, buf, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, strlen(xname2) + 1);
	assert_string_equal(buf, xname2);
	print_message("    file1 listxattr shows only '%s' - PASS\n", xname2);

	size = sizeof(buf);
	memset(buf, 0, sizeof(buf));
	rc = dfs_listxattr(dfs_mt, file2, buf, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, strlen(xname2) + 1);
	assert_string_equal(buf, xname2);
	print_message("    file2 listxattr shows only '%s' - PASS\n", xname2);

	/* Cleanup part 2 */
	rc = dfs_release(file1);
	assert_int_equal(rc, 0);
	rc = dfs_release(file2);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "xattr_rm_file1", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "xattr_rm_file2", false, NULL);
	assert_int_equal(rc, 0);

	print_message("\nHardlink xattr test completed successfully!\n");
}

static void
dfs_test_exchange(void **state)
{
	test_arg_t   *arg = *state;
	dfs_obj_t    *file_a, *file_a_link, *file_b, *file_b_link;
	dfs_obj_t    *tmp_obj;
	dfs_obj_t    *dir_a, *dir_a_link, *dir_b, *dir_b_link;
	struct stat   stbuf;
	d_sg_list_t   sgl;
	d_iov_t       iov;
	char          data_a[64], data_b[64], read_buf[64];
	daos_size_t   read_size;
	daos_obj_id_t oid_a, oid_b, oid_tmp;
	int           rc;

	if (arg->myrank != 0)
		return;

	print_message("=== dfs_exchange test ===\n");

	/* Prepare unique data patterns */
	memset(data_a, 'A', sizeof(data_a));
	memset(data_b, 'B', sizeof(data_b));

	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs   = &iov;

	/*
	 * ============================================================
	 * Scenario 1: Basic exchange (no hardlinks)
	 * ============================================================
	 * /exch_dir_a1/file_a and /exch_dir_b1/file_b are regular files
	 * exchange(file_a, file_b) should swap their directory entries
	 */
	print_message("\n--- Scenario 1: Basic exchange (no hardlinks) ---\n");

	/* Create directories for file_a and file_b */
	rc = dfs_mkdir(dfs_mt, NULL, "exch_dir_a1", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "exch_dir_a1", O_RDWR, &dir_a, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_mkdir(dfs_mt, NULL, "exch_dir_b1", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "exch_dir_b1", O_RDWR, &dir_b, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("  Created directories: exch_dir_a1/, exch_dir_b1/\n");

	/* Create file_a and write data */
	rc = dfs_open(dfs_mt, dir_a, "exch_a1", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file_a);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(file_a, &oid_a);
	assert_int_equal(rc, 0);
	print_message("  Created /exch_dir_a1/exch_a1, oid=" DF_OID "\n", DP_OID(oid_a));

	d_iov_set(&iov, data_a, sizeof(data_a));
	rc = dfs_write(dfs_mt, file_a, &sgl, 0, NULL);
	assert_int_equal(rc, 0);
	print_message("  Wrote data 'A' to exch_a1\n");

	rc = dfs_release(file_a);
	assert_int_equal(rc, 0);

	/* Create file_b and write data */
	rc = dfs_open(dfs_mt, dir_b, "exch_b1", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file_b);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(file_b, &oid_b);
	assert_int_equal(rc, 0);
	print_message("  Created /exch_dir_b1/exch_b1, oid=" DF_OID "\n", DP_OID(oid_b));

	d_iov_set(&iov, data_b, sizeof(data_b));
	rc = dfs_write(dfs_mt, file_b, &sgl, 0, NULL);
	assert_int_equal(rc, 0);
	print_message("  Wrote data 'B' to exch_b1\n");

	rc = dfs_release(file_b);
	assert_int_equal(rc, 0);

	/* Exchange file_a and file_b - names swap directories */
	print_message("  Exchanging /exch_dir_a1/exch_a1 <-> /exch_dir_b1/exch_b1...\n");
	rc = dfs_exchange(dfs_mt, dir_a, "exch_a1", dir_b, "exch_b1");
	assert_int_equal(rc, 0);

	/* After exchange: exch_a1 is now in dir_b, exch_b1 is now in dir_a */
	/* Verify exch_a1 (now in dir_b) still has oid_a and data_a */
	rc = dfs_lookup_rel(dfs_mt, dir_b, "exch_a1", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(tmp_obj, &oid_tmp);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_a.lo && oid_tmp.hi == oid_a.hi);
	print_message("  /exch_dir_b1/exch_a1: oid=" DF_OID " (still oid_a) - PASS\n",
		      DP_OID(oid_tmp));

	memset(read_buf, 0, sizeof(read_buf));
	d_iov_set(&iov, read_buf, sizeof(read_buf));
	rc = dfs_read(dfs_mt, tmp_obj, &sgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_memory_equal(read_buf, data_a, sizeof(data_a));
	print_message("  /exch_dir_b1/exch_a1: content is 'A' - PASS\n");
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* Verify exch_b1 (now in dir_a) still has oid_b and data_b */
	rc = dfs_lookup_rel(dfs_mt, dir_a, "exch_b1", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(tmp_obj, &oid_tmp);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_b.lo && oid_tmp.hi == oid_b.hi);
	print_message("  /exch_dir_a1/exch_b1: oid=" DF_OID " (still oid_b) - PASS\n",
		      DP_OID(oid_tmp));

	memset(read_buf, 0, sizeof(read_buf));
	d_iov_set(&iov, read_buf, sizeof(read_buf));
	rc = dfs_read(dfs_mt, tmp_obj, &sgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_memory_equal(read_buf, data_b, sizeof(data_b));
	print_message("  /exch_dir_a1/exch_b1: content is 'B' - PASS\n");
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* Cleanup scenario 1 - files are now in swapped directories */
	rc = dfs_remove(dfs_mt, dir_b, "exch_a1", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir_a, "exch_b1", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_a);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_b);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "exch_dir_a1", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "exch_dir_b1", false, NULL);
	assert_int_equal(rc, 0);

	/*
	 * ============================================================
	 * Scenario 2: Exchange where one file has hardlinks
	 * ============================================================
	 * /exch_dir_a2/file_a has a hardlink /exch_linkdir_a2/file_a_link
	 * /exch_dir_b2/file_b is a regular file (no hardlinks)
	 * exchange(file_a, file_b) should:
	 *   - file_a name now points to file_b's object
	 *   - file_b name now points to file_a's object
	 *   - file_a_link still points to file_a's original object
	 */
	print_message("\n--- Scenario 2: Exchange where one file has hardlinks ---\n");

	/* Create directory for file_a */
	rc = dfs_mkdir(dfs_mt, NULL, "exch_dir_a2", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "exch_dir_a2", O_RDWR, &dir_a, NULL, NULL);
	assert_int_equal(rc, 0);

	/* Create directory for file_a's hardlink */
	rc = dfs_mkdir(dfs_mt, NULL, "exch_linkdir_a2", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "exch_linkdir_a2", O_RDWR, &dir_a_link, NULL, NULL);
	assert_int_equal(rc, 0);

	/* Create directory for file_b */
	rc = dfs_mkdir(dfs_mt, NULL, "exch_dir_b2", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "exch_dir_b2", O_RDWR, &dir_b, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("  Created directories: exch_dir_a2/, exch_linkdir_a2/, exch_dir_b2/\n");

	/* Create file_a in its directory */
	rc = dfs_open(dfs_mt, dir_a, "exch_a2", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file_a);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(file_a, &oid_a);
	assert_int_equal(rc, 0);
	print_message("  Created /exch_dir_a2/exch_a2, oid=" DF_OID "\n", DP_OID(oid_a));

	d_iov_set(&iov, data_a, sizeof(data_a));
	rc = dfs_write(dfs_mt, file_a, &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	/* Create hardlink in separate directory */
	rc = dfs_link(dfs_mt, file_a, dir_a_link, "exch_a2_link", &file_a_link, &stbuf);
	assert_int_equal(rc, 0);
	print_message("  Created hardlink /exch_linkdir_a2/exch_a2_link, nlink=%lu\n",
		      (unsigned long)stbuf.st_nlink);

	rc = dfs_release(file_a);
	assert_int_equal(rc, 0);
	rc = dfs_release(file_a_link);
	assert_int_equal(rc, 0);

	/* Create file_b in its directory (no hardlink) */
	rc = dfs_open(dfs_mt, dir_b, "exch_b2", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file_b);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(file_b, &oid_b);
	assert_int_equal(rc, 0);
	print_message("  Created /exch_dir_b2/exch_b2, oid=" DF_OID "\n", DP_OID(oid_b));

	d_iov_set(&iov, data_b, sizeof(data_b));
	rc = dfs_write(dfs_mt, file_b, &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_release(file_b);
	assert_int_equal(rc, 0);

	/* Exchange file_a and file_b - names swap directories */
	print_message("  Exchanging /exch_dir_a2/exch_a2 <-> /exch_dir_b2/exch_b2...\n");
	rc = dfs_exchange(dfs_mt, dir_a, "exch_a2", dir_b, "exch_b2");
	assert_int_equal(rc, 0);

	/* After exchange: exch_a2 is now in dir_b, exch_b2 is now in dir_a */
	/* Verify exch_a2 (now in dir_b) still has oid_a, nlink=2 (has hardlink) */
	rc = dfs_lookup_rel(dfs_mt, dir_b, "exch_a2", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(tmp_obj, &oid_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, tmp_obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_a.lo && oid_tmp.hi == oid_a.hi);
	assert_int_equal(stbuf.st_nlink, 2);
	print_message("  /exch_dir_b2/exch_a2: oid=" DF_OID ", nlink=%lu (still oid_a) - PASS\n",
		      DP_OID(oid_tmp), (unsigned long)stbuf.st_nlink);
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* Verify exch_b2 (now in dir_a) still has oid_b, nlink=1 */
	rc = dfs_lookup_rel(dfs_mt, dir_a, "exch_b2", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(tmp_obj, &oid_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, tmp_obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_b.lo && oid_tmp.hi == oid_b.hi);
	assert_int_equal(stbuf.st_nlink, 1);
	print_message("  /exch_dir_a2/exch_b2: oid=" DF_OID ", nlink=%lu (still oid_b) - PASS\n",
		      DP_OID(oid_tmp), (unsigned long)stbuf.st_nlink);
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* Verify exch_linkdir_a2/exch_a2_link still has oid_a, nlink=2 */
	rc = dfs_lookup_rel(dfs_mt, dir_a_link, "exch_a2_link", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(tmp_obj, &oid_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, tmp_obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_a.lo && oid_tmp.hi == oid_a.hi);
	assert_int_equal(stbuf.st_nlink, 2);
	print_message("  /exch_linkdir_a2/exch_a2_link: oid=" DF_OID
		      ", nlink=%lu (same as exch_a2) - PASS\n",
		      DP_OID(oid_tmp), (unsigned long)stbuf.st_nlink);
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* Verify content of exch_a2 (now in dir_b, should be data_a) */
	rc = dfs_lookup_rel(dfs_mt, dir_b, "exch_a2", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	memset(read_buf, 0, sizeof(read_buf));
	d_iov_set(&iov, read_buf, sizeof(read_buf));
	rc = dfs_read(dfs_mt, tmp_obj, &sgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_memory_equal(read_buf, data_a, sizeof(data_a));
	print_message("  /exch_dir_b2/exch_a2: content is 'A' - PASS\n");
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* Verify content of exch_b2 (now in dir_a, should be data_b) */
	rc = dfs_lookup_rel(dfs_mt, dir_a, "exch_b2", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	memset(read_buf, 0, sizeof(read_buf));
	d_iov_set(&iov, read_buf, sizeof(read_buf));
	rc = dfs_read(dfs_mt, tmp_obj, &sgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_memory_equal(read_buf, data_b, sizeof(data_b));
	print_message("  /exch_dir_a2/exch_b2: content is 'B' - PASS\n");
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* Verify content of exch_a2_link (should be data_a, same as exch_a2) */
	rc = dfs_lookup_rel(dfs_mt, dir_a_link, "exch_a2_link", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	memset(read_buf, 0, sizeof(read_buf));
	d_iov_set(&iov, read_buf, sizeof(read_buf));
	rc = dfs_read(dfs_mt, tmp_obj, &sgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_memory_equal(read_buf, data_a, sizeof(data_a));
	print_message("  /exch_linkdir_a2/exch_a2_link: content is 'A' (same as exch_a2) - PASS\n");
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* Cleanup scenario 2 - files are now in swapped directories */
	rc = dfs_remove(dfs_mt, dir_b, "exch_a2", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir_a, "exch_b2", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir_a_link, "exch_a2_link", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_a);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_a_link);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_b);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "exch_dir_a2", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "exch_linkdir_a2", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "exch_dir_b2", false, NULL);
	assert_int_equal(rc, 0);

	/*
	 * ============================================================
	 * Scenario 3: Exchange where both files have hardlinks
	 * ============================================================
	 * /exch_dir_a3/file_a has hardlink /exch_linkdir_a3/file_a_link
	 * /exch_dir_b3/file_b has hardlink /exch_linkdir_b3/file_b_link
	 * exchange(file_a, file_b) should:
	 *   - file_a name now points to file_b's object
	 *   - file_b name now points to file_a's object
	 *   - file_a_link still points to original file_a object (now same as file_b)
	 *   - file_b_link still points to original file_b object (now same as file_a)
	 */
	print_message("\n--- Scenario 3: Exchange where both files have hardlinks ---\n");

	/* Create directory for file_a */
	rc = dfs_mkdir(dfs_mt, NULL, "exch_dir_a3", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "exch_dir_a3", O_RDWR, &dir_a, NULL, NULL);
	assert_int_equal(rc, 0);

	/* Create directory for file_a's hardlink */
	rc = dfs_mkdir(dfs_mt, NULL, "exch_linkdir_a3", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "exch_linkdir_a3", O_RDWR, &dir_a_link, NULL, NULL);
	assert_int_equal(rc, 0);

	/* Create directory for file_b */
	rc = dfs_mkdir(dfs_mt, NULL, "exch_dir_b3", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "exch_dir_b3", O_RDWR, &dir_b, NULL, NULL);
	assert_int_equal(rc, 0);

	/* Create directory for file_b's hardlink */
	rc = dfs_mkdir(dfs_mt, NULL, "exch_linkdir_b3", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "exch_linkdir_b3", O_RDWR, &dir_b_link, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("  Created directories: exch_dir_a3/, exch_linkdir_a3/, exch_dir_b3/, "
		      "exch_linkdir_b3/\n");

	/* Create file_a in its directory */
	rc = dfs_open(dfs_mt, dir_a, "exch_a3", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file_a);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(file_a, &oid_a);
	assert_int_equal(rc, 0);
	print_message("  Created /exch_dir_a3/exch_a3, oid=" DF_OID "\n", DP_OID(oid_a));

	d_iov_set(&iov, data_a, sizeof(data_a));
	rc = dfs_write(dfs_mt, file_a, &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	/* Create hardlink in separate directory */
	rc = dfs_link(dfs_mt, file_a, dir_a_link, "exch_a3_link", &file_a_link, &stbuf);
	assert_int_equal(rc, 0);
	print_message("  Created hardlink /exch_linkdir_a3/exch_a3_link\n");
	rc = dfs_release(file_a);
	assert_int_equal(rc, 0);
	rc = dfs_release(file_a_link);
	assert_int_equal(rc, 0);

	/* Create file_b in its directory */
	rc = dfs_open(dfs_mt, dir_b, "exch_b3", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file_b);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(file_b, &oid_b);
	assert_int_equal(rc, 0);
	print_message("  Created /exch_dir_b3/exch_b3, oid=" DF_OID "\n", DP_OID(oid_b));

	d_iov_set(&iov, data_b, sizeof(data_b));
	rc = dfs_write(dfs_mt, file_b, &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	/* Create hardlink in separate directory */
	rc = dfs_link(dfs_mt, file_b, dir_b_link, "exch_b3_link", &file_b_link, &stbuf);
	assert_int_equal(rc, 0);
	print_message("  Created hardlink /exch_linkdir_b3/exch_b3_link\n");
	rc = dfs_release(file_b);
	assert_int_equal(rc, 0);
	rc = dfs_release(file_b_link);
	assert_int_equal(rc, 0);

	/* Exchange file_a and file_b - names swap directories */
	print_message("  Exchanging /exch_dir_a3/exch_a3 <-> /exch_dir_b3/exch_b3...\n");
	rc = dfs_exchange(dfs_mt, dir_a, "exch_a3", dir_b, "exch_b3");
	assert_int_equal(rc, 0);

	/* After exchange: exch_a3 is now in dir_b, exch_b3 is now in dir_a */
	/* Verify exch_a3 (now in dir_b) still has oid_a, nlink=2 */
	rc = dfs_lookup_rel(dfs_mt, dir_b, "exch_a3", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(tmp_obj, &oid_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, tmp_obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_a.lo && oid_tmp.hi == oid_a.hi);
	assert_int_equal(stbuf.st_nlink, 2);
	print_message("  /exch_dir_b3/exch_a3: oid=" DF_OID " (still oid_a), nlink=%lu - PASS\n",
		      DP_OID(oid_tmp), (unsigned long)stbuf.st_nlink);
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* Verify exch_b3 (now in dir_a) still has oid_b, nlink=2 */
	rc = dfs_lookup_rel(dfs_mt, dir_a, "exch_b3", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(tmp_obj, &oid_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, tmp_obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_b.lo && oid_tmp.hi == oid_b.hi);
	assert_int_equal(stbuf.st_nlink, 2);
	print_message("  /exch_dir_a3/exch_b3: oid=" DF_OID " (still oid_b), nlink=%lu - PASS\n",
		      DP_OID(oid_tmp), (unsigned long)stbuf.st_nlink);
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* Verify exch_linkdir_a3/exch_a3_link still has oid_a (same as exch_a3) */
	rc = dfs_lookup_rel(dfs_mt, dir_a_link, "exch_a3_link", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(tmp_obj, &oid_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, tmp_obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_a.lo && oid_tmp.hi == oid_a.hi);
	assert_int_equal(stbuf.st_nlink, 2);
	print_message("  /exch_linkdir_a3/exch_a3_link: oid=" DF_OID
		      " (oid_a, same as exch_a3) - PASS\n",
		      DP_OID(oid_tmp));
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* Verify exch_linkdir_b3/exch_b3_link still has oid_b (same as exch_b3) */
	rc = dfs_lookup_rel(dfs_mt, dir_b_link, "exch_b3_link", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(tmp_obj, &oid_tmp);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, tmp_obj, &stbuf);
	assert_int_equal(rc, 0);
	assert_true(oid_tmp.lo == oid_b.lo && oid_tmp.hi == oid_b.hi);
	assert_int_equal(stbuf.st_nlink, 2);
	print_message("  /exch_linkdir_b3/exch_b3_link: oid=" DF_OID
		      " (oid_b, same as exch_b3) - PASS\n",
		      DP_OID(oid_tmp));
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* Verify content: exch_a3 (now in dir_b) and exch_a3_link should have data_a */
	rc = dfs_lookup_rel(dfs_mt, dir_b, "exch_a3", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	memset(read_buf, 0, sizeof(read_buf));
	d_iov_set(&iov, read_buf, sizeof(read_buf));
	rc = dfs_read(dfs_mt, tmp_obj, &sgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_memory_equal(read_buf, data_a, sizeof(data_a));
	print_message("  /exch_dir_b3/exch_a3: content is 'A' - PASS\n");
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	rc = dfs_lookup_rel(dfs_mt, dir_a_link, "exch_a3_link", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	memset(read_buf, 0, sizeof(read_buf));
	d_iov_set(&iov, read_buf, sizeof(read_buf));
	rc = dfs_read(dfs_mt, tmp_obj, &sgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_memory_equal(read_buf, data_a, sizeof(data_a));
	print_message("  /exch_linkdir_a3/exch_a3_link: content is 'A' (same as exch_a3) - PASS\n");
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* Verify content: exch_b3 (now in dir_a) and exch_b3_link should have data_b */
	rc = dfs_lookup_rel(dfs_mt, dir_a, "exch_b3", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	memset(read_buf, 0, sizeof(read_buf));
	d_iov_set(&iov, read_buf, sizeof(read_buf));
	rc = dfs_read(dfs_mt, tmp_obj, &sgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_memory_equal(read_buf, data_b, sizeof(data_b));
	print_message("  /exch_dir_a3/exch_b3: content is 'B' - PASS\n");
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	rc = dfs_lookup_rel(dfs_mt, dir_b_link, "exch_b3_link", O_RDONLY, &tmp_obj, NULL, NULL);
	assert_int_equal(rc, 0);
	memset(read_buf, 0, sizeof(read_buf));
	d_iov_set(&iov, read_buf, sizeof(read_buf));
	rc = dfs_read(dfs_mt, tmp_obj, &sgl, 0, &read_size, NULL);
	assert_int_equal(rc, 0);
	assert_memory_equal(read_buf, data_b, sizeof(data_b));
	print_message("  /exch_linkdir_b3/exch_b3_link: content is 'B' (same as exch_b3) - PASS\n");
	rc = dfs_release(tmp_obj);
	assert_int_equal(rc, 0);

	/* Cleanup scenario 3 - files are now in swapped directories */
	rc = dfs_remove(dfs_mt, dir_b, "exch_a3", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir_a, "exch_b3", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir_a_link, "exch_a3_link", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir_b_link, "exch_b3_link", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_a);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_a_link);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_b);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_b_link);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "exch_dir_a3", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "exch_linkdir_a3", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "exch_dir_b3", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "exch_linkdir_b3", false, NULL);
	assert_int_equal(rc, 0);

	print_message("\ndfs_exchange test completed successfully!\n");
}

static void
dfs_test_hardlink_access(void **state)
{
	test_arg_t   *arg = *state;
	dfs_obj_t    *file, *link1, *link2, *symlink_obj;
	dfs_obj_t    *dir_file, *dir_link1, *dir_link2, *dir_symlink;
	struct stat   stbuf;
	daos_obj_id_t oid_file;
	int           rc;

	if (arg->myrank != 0)
		return;

	print_message("=== Hardlink dfs_access test ===\n");
	print_message("Testing that dfs_access returns consistent results across hardlinks\n");

	/*
	 * Create structure:
	 *   /access_dir_file/access_file     - original file
	 *   /access_dir_link1/access_link1   - hardlink 1
	 *   /access_dir_link2/access_link2   - hardlink 2
	 *   /access_dir_symlink/access_symlink -> ../access_dir_link2/access_link2
	 */

	/* Create directories */
	rc = dfs_mkdir(dfs_mt, NULL, "access_dir_file", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "access_dir_file", O_RDWR, &dir_file, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("  Created directory /access_dir_file/\n");

	rc = dfs_mkdir(dfs_mt, NULL, "access_dir_link1", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "access_dir_link1", O_RDWR, &dir_link1, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("  Created directory /access_dir_link1/\n");

	rc = dfs_mkdir(dfs_mt, NULL, "access_dir_link2", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "access_dir_link2", O_RDWR, &dir_link2, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("  Created directory /access_dir_link2/\n");

	rc = dfs_mkdir(dfs_mt, NULL, "access_dir_symlink", S_IFDIR | S_IRWXU, 0);
	assert_int_equal(rc, 0);
	rc = dfs_lookup_rel(dfs_mt, NULL, "access_dir_symlink", O_RDWR, &dir_symlink, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("  Created directory /access_dir_symlink/\n");

	/* Create file with read-write permissions */
	rc = dfs_open(dfs_mt, dir_file, "access_file", S_IFREG | S_IRUSR | S_IWUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file);
	assert_int_equal(rc, 0);

	rc = dfs_obj2id(file, &oid_file);
	assert_int_equal(rc, 0);
	rc = dfs_ostat(dfs_mt, file, &stbuf);
	assert_int_equal(rc, 0);
	print_message("  Created /access_dir_file/access_file, oid=" DF_OID ", mode=0%o\n",
		      DP_OID(oid_file), stbuf.st_mode & 0777);

	/* Create hardlinks in different directories */
	rc = dfs_link(dfs_mt, file, dir_link1, "access_link1", &link1, &stbuf);
	assert_int_equal(rc, 0);
	print_message("  Created hardlink /access_dir_link1/access_link1, nlink=%lu\n",
		      (unsigned long)stbuf.st_nlink);

	rc = dfs_link(dfs_mt, file, dir_link2, "access_link2", &link2, &stbuf);
	assert_int_equal(rc, 0);
	print_message("  Created hardlink /access_dir_link2/access_link2, nlink=%lu\n",
		      (unsigned long)stbuf.st_nlink);

	/* Create symlink pointing to link2 */
	rc = dfs_open(dfs_mt, dir_symlink, "access_symlink", S_IFLNK, O_RDWR | O_CREAT | O_EXCL, 0,
		      0, "../access_dir_link2/access_link2", &symlink_obj);
	assert_int_equal(rc, 0);
	print_message("  Created symlink /access_dir_symlink/access_symlink -> "
		      "../access_dir_link2/access_link2\n");

	rc = dfs_release(file);
	assert_int_equal(rc, 0);
	rc = dfs_release(link1);
	assert_int_equal(rc, 0);
	rc = dfs_release(link2);
	assert_int_equal(rc, 0);
	rc = dfs_release(symlink_obj);
	assert_int_equal(rc, 0);

	/*
	 * Test 1: F_OK (file existence) on all paths
	 */
	print_message("\n--- Test 1: F_OK (existence check) ---\n");

	rc = dfs_access(dfs_mt, dir_file, "access_file", F_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_file/access_file, F_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_link1, "access_link1", F_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_link1/access_link1, F_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_link2, "access_link2", F_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_link2/access_link2, F_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_symlink, "access_symlink", F_OK);
	assert_int_equal(rc, 0);
	print_message(
	    "  dfs_access(/access_dir_symlink/access_symlink, F_OK) = %d - PASS (via symlink)\n",
	    rc);

	/*
	 * Test 2: R_OK (read permission) on all paths
	 */
	print_message("\n--- Test 2: R_OK (read permission) ---\n");

	rc = dfs_access(dfs_mt, dir_file, "access_file", R_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_file/access_file, R_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_link1, "access_link1", R_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_link1/access_link1, R_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_link2, "access_link2", R_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_link2/access_link2, R_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_symlink, "access_symlink", R_OK);
	assert_int_equal(rc, 0);
	print_message(
	    "  dfs_access(/access_dir_symlink/access_symlink, R_OK) = %d - PASS (via symlink)\n",
	    rc);

	/*
	 * Test 3: W_OK (write permission) on all paths
	 */
	print_message("\n--- Test 3: W_OK (write permission) ---\n");

	rc = dfs_access(dfs_mt, dir_file, "access_file", W_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_file/access_file, W_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_link1, "access_link1", W_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_link1/access_link1, W_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_link2, "access_link2", W_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_link2/access_link2, W_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_symlink, "access_symlink", W_OK);
	assert_int_equal(rc, 0);
	print_message(
	    "  dfs_access(/access_dir_symlink/access_symlink, W_OK) = %d - PASS (via symlink)\n",
	    rc);

	/*
	 * Test 4: R_OK | W_OK combined on all paths
	 */
	print_message("\n--- Test 4: R_OK | W_OK (read+write permission) ---\n");

	rc = dfs_access(dfs_mt, dir_file, "access_file", R_OK | W_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_file/access_file, R_OK|W_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_link1, "access_link1", R_OK | W_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_link1/access_link1, R_OK|W_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_link2, "access_link2", R_OK | W_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_link2/access_link2, R_OK|W_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_symlink, "access_symlink", R_OK | W_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_symlink/access_symlink, R_OK|W_OK) = %d - PASS "
		      "(via symlink)\n",
		      rc);

	/*
	 * Test 5: Change permissions via one link, verify via all links
	 */
	print_message("\n--- Test 5: chmod via one link, verify access via all links ---\n");

	/* Change to read-only via link1 */
	rc = dfs_chmod(dfs_mt, dir_link1, "access_link1", S_IRUSR);
	assert_int_equal(rc, 0);
	print_message("  Changed mode to read-only (0400) via /access_dir_link1/access_link1\n");

	/* Verify R_OK succeeds on all */
	rc = dfs_access(dfs_mt, dir_file, "access_file", R_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_file/access_file, R_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_link1, "access_link1", R_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_link1/access_link1, R_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_link2, "access_link2", R_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_link2/access_link2, R_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_symlink, "access_symlink", R_OK);
	assert_int_equal(rc, 0);
	print_message(
	    "  dfs_access(/access_dir_symlink/access_symlink, R_OK) = %d - PASS (via symlink)\n",
	    rc);

	/* Verify W_OK fails on all (read-only now) */
	rc = dfs_access(dfs_mt, dir_file, "access_file", W_OK);
	assert_int_equal(rc, EACCES);
	print_message("  dfs_access(/access_dir_file/access_file, W_OK) = EACCES - PASS\n");

	rc = dfs_access(dfs_mt, dir_link1, "access_link1", W_OK);
	assert_int_equal(rc, EACCES);
	print_message("  dfs_access(/access_dir_link1/access_link1, W_OK) = EACCES - PASS\n");

	rc = dfs_access(dfs_mt, dir_link2, "access_link2", W_OK);
	assert_int_equal(rc, EACCES);
	print_message("  dfs_access(/access_dir_link2/access_link2, W_OK) = EACCES - PASS\n");

	rc = dfs_access(dfs_mt, dir_symlink, "access_symlink", W_OK);
	assert_int_equal(rc, EACCES);
	print_message("  dfs_access(/access_dir_symlink/access_symlink, W_OK) = EACCES - PASS (via "
		      "symlink)\n");

	/*
	 * Test 6: Restore permissions via original file, verify via all links
	 */
	print_message("\n--- Test 6: Restore permissions, verify access ---\n");

	/* Restore read-write via original file */
	rc = dfs_chmod(dfs_mt, dir_file, "access_file", S_IRUSR | S_IWUSR);
	assert_int_equal(rc, 0);
	print_message("  Restored mode to read-write (0600) via /access_dir_file/access_file\n");

	/* Verify W_OK now succeeds on all */
	rc = dfs_access(dfs_mt, dir_file, "access_file", W_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_file/access_file, W_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_link1, "access_link1", W_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_link1/access_link1, W_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_link2, "access_link2", W_OK);
	assert_int_equal(rc, 0);
	print_message("  dfs_access(/access_dir_link2/access_link2, W_OK) = %d - PASS\n", rc);

	rc = dfs_access(dfs_mt, dir_symlink, "access_symlink", W_OK);
	assert_int_equal(rc, 0);
	print_message(
	    "  dfs_access(/access_dir_symlink/access_symlink, W_OK) = %d - PASS (via symlink)\n",
	    rc);

	/* Cleanup */
	rc = dfs_remove(dfs_mt, dir_file, "access_file", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir_link1, "access_link1", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir_link2, "access_link2", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir_symlink, "access_symlink", false, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_release(dir_file);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_link1);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_link2);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir_symlink);
	assert_int_equal(rc, 0);

	rc = dfs_remove(dfs_mt, NULL, "access_dir_file", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "access_dir_link1", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "access_dir_link2", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "access_dir_symlink", false, NULL);
	assert_int_equal(rc, 0);

	print_message("\nHardlink dfs_access test completed successfully!\n");
}

/**
 * Test dfs_ostatx with hardlinks.
 *
 * This test verifies that dfs_ostatx works correctly when:
 * 1. A file is converted to a hardlink (another DFS instance scenario)
 * 2. A stale handle (opened before hardlink creation) is used
 *
 * Test steps:
 * 1. Create two subdirectories (dir1 and dir2)
 * 2. Create a file in dir1 and open two handles to it
 * 3. Using handle1, create a hardlink in dir2 (converts file to hardlink)
 * 4. Using handle1, stat both files - they should have identical metadata
 * 5. Write some data using handle1, stat both files - size should match
 * 6. Use handle2 (opened before hardlink) to call dfs_ostatx - should work
 */
static void
dfs_test_hardlink_ostatx(void **state)
{
	test_arg_t   *arg  = *state;
	dfs_obj_t    *dir1 = NULL, *dir2 = NULL;
	dfs_obj_t    *file_handle1 = NULL, *file_handle2 = NULL;
	dfs_obj_t    *link_handle = NULL;
	struct stat   stbuf1, stbuf_link, stbuf_handle2;
	daos_obj_id_t oid1, oid2, oid_link;
	char         *write_buf = "Hello, hardlink ostatx test!";
	daos_size_t   write_size;
	d_sg_list_t   sgl;
	d_iov_t       iov;
	daos_event_t  ev, *evp;
	int           rc;

	if (arg->myrank != 0)
		return;

	print_message("\n=== Test: dfs_ostatx with hardlinks ===\n");

	/*
	 * Step 1: Create two subdirectories
	 */
	print_message("\nStep 1: Creating two subdirectories...\n");
	rc = dfs_open(dfs_mt, NULL, "ostatx_dir1", S_IFDIR | S_IRWXU, O_RDWR | O_CREAT | O_EXCL, 0,
		      0, NULL, &dir1);
	assert_int_equal(rc, 0);
	print_message("  Created /ostatx_dir1\n");

	rc = dfs_open(dfs_mt, NULL, "ostatx_dir2", S_IFDIR | S_IRWXU, O_RDWR | O_CREAT | O_EXCL, 0,
		      0, NULL, &dir2);
	assert_int_equal(rc, 0);
	print_message("  Created /ostatx_dir2\n");

	/*
	 * Step 2: Create a file in dir1 and open TWO handles to it
	 */
	print_message("\nStep 2: Creating file and opening two handles...\n");

	/* First handle - will be used to create hardlink */
	rc = dfs_open(dfs_mt, dir1, "testfile", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file_handle1);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(file_handle1, &oid1);
	assert_int_equal(rc, 0);
	print_message("  Created /ostatx_dir1/testfile (handle1)\n");

	/* Second handle - opened BEFORE hardlink creation (simulates stale handle) */
	rc = dfs_lookup(dfs_mt, "/ostatx_dir1/testfile", O_RDWR, &file_handle2, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(file_handle2, &oid2);
	assert_int_equal(rc, 0);
	print_message("  Opened second handle to same file (handle2)\n");

	/* Verify both handles point to the same OID */
	assert_true(oid1.lo == oid2.lo && oid1.hi == oid2.hi);
	print_message("  Verified: both handles have same OID\n");

	/* Stat using handle1 before hardlink - nlink should be 1 */
	rc = dfs_ostat(dfs_mt, file_handle1, &stbuf1);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf1.st_nlink, 1);
	print_message("  Initial stat via handle1: nlink=%lu, size=%lu\n",
		      (unsigned long)stbuf1.st_nlink, (unsigned long)stbuf1.st_size);

	/*
	 * Step 3: Using handle1, create a hardlink in dir2
	 * This converts the file to a hardlink (metadata moves to HLM)
	 */
	print_message("\nStep 3: Creating hardlink using handle1...\n");
	rc = dfs_link(dfs_mt, file_handle1, dir2, "testfile_link", &link_handle, &stbuf_link);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(link_handle, &oid_link);
	assert_int_equal(rc, 0);
	assert_true(oid1.lo == oid_link.lo && oid1.hi == oid_link.hi);
	print_message("  Created /ostatx_dir2/testfile_link\n");
	print_message("  Verified: link has same OID as original\n");
	assert_int_equal(stbuf_link.st_nlink, 2);
	print_message("  Link stat: nlink=%lu (expected 2)\n", (unsigned long)stbuf_link.st_nlink);

	/*
	 * Step 4: Using handle1, stat both files - they should have identical data
	 */
	print_message("\nStep 4: Stat both files via handle1 - verifying identical metadata...\n");

	rc = dfs_ostat(dfs_mt, file_handle1, &stbuf1);
	assert_int_equal(rc, 0);
	print_message("  Original file: nlink=%lu, size=%lu, mode=0%o\n",
		      (unsigned long)stbuf1.st_nlink, (unsigned long)stbuf1.st_size,
		      stbuf1.st_mode);

	rc = dfs_ostat(dfs_mt, link_handle, &stbuf_link);
	assert_int_equal(rc, 0);
	print_message("  Link file:     nlink=%lu, size=%lu, mode=0%o\n",
		      (unsigned long)stbuf_link.st_nlink, (unsigned long)stbuf_link.st_size,
		      stbuf_link.st_mode);

	/* Verify identical metadata */
	assert_int_equal(stbuf1.st_nlink, 2);
	assert_int_equal(stbuf_link.st_nlink, 2);
	assert_int_equal(stbuf1.st_mode, stbuf_link.st_mode);
	assert_int_equal(stbuf1.st_uid, stbuf_link.st_uid);
	assert_int_equal(stbuf1.st_gid, stbuf_link.st_gid);
	assert_int_equal(stbuf1.st_size, stbuf_link.st_size);
	print_message("  Verified: both files have identical metadata\n");

	/*
	 * Step 5: Write some data and repeat stat - size should match
	 * Use dfs_ostatx with event handle to test async path
	 */
	print_message("\nStep 5: Writing data and verifying size (using dfs_ostatx async)...\n");

	write_size = strlen(write_buf);
	d_iov_set(&iov, write_buf, write_size);
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &iov;

	rc = dfs_write(dfs_mt, file_handle1, &sgl, 0, NULL);
	assert_int_equal(rc, 0);
	print_message("  Wrote %lu bytes via handle1\n", (unsigned long)write_size);

	/* Stat original file using dfs_ostatx with event */
	rc = daos_event_init(&ev, arg->eq, NULL);
	assert_rc_equal(rc, 0);

	rc = dfs_ostatx(dfs_mt, file_handle1, &stbuf1, &ev);
	assert_int_equal(rc, 0);

	rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
	assert_rc_equal(rc, 1);
	assert_ptr_equal(evp, &ev);
	assert_int_equal(evp->ev_error, 0);

	rc = daos_event_fini(&ev);
	assert_rc_equal(rc, 0);

	print_message("  Original file after write: nlink=%lu, size=%lu\n",
		      (unsigned long)stbuf1.st_nlink, (unsigned long)stbuf1.st_size);

	/* Stat link file using dfs_ostatx with event */
	rc = daos_event_init(&ev, arg->eq, NULL);
	assert_rc_equal(rc, 0);

	rc = dfs_ostatx(dfs_mt, link_handle, &stbuf_link, &ev);
	assert_int_equal(rc, 0);

	rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
	assert_rc_equal(rc, 1);
	assert_ptr_equal(evp, &ev);
	assert_int_equal(evp->ev_error, 0);

	rc = daos_event_fini(&ev);
	assert_rc_equal(rc, 0);

	print_message("  Link file after write:     nlink=%lu, size=%lu\n",
		      (unsigned long)stbuf_link.st_nlink, (unsigned long)stbuf_link.st_size);

	/* Verify size is correct on both */
	assert_int_equal(stbuf1.st_size, write_size);
	assert_int_equal(stbuf_link.st_size, write_size);
	assert_int_equal(stbuf1.st_nlink, 2);
	assert_int_equal(stbuf_link.st_nlink, 2);
	print_message("  Verified: both files show correct size (%lu bytes)\n",
		      (unsigned long)write_size);

	/*
	 * Step 6: Use handle2 (opened BEFORE hardlink creation) to call dfs_ostatx
	 * This simulates the case where a DFS handle was opened before another
	 * DFS instance converted the file to a hardlink.
	 * dfs_ostatx should detect the hardlink bit and fetch from HLM.
	 * We use an event handle to test the async code path.
	 */
	print_message("\nStep 6: Using stale handle2 with dfs_ostatx (async)...\n");
	print_message("  (handle2 was opened before hardlink creation)\n");

	/* Initialize event for async operation */
	rc = daos_event_init(&ev, arg->eq, NULL);
	assert_rc_equal(rc, 0);

	rc = dfs_ostatx(dfs_mt, file_handle2, &stbuf_handle2, &ev);
	assert_int_equal(rc, 0);

	/* Wait for async completion */
	rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
	assert_rc_equal(rc, 1);
	assert_ptr_equal(evp, &ev);
	assert_int_equal(evp->ev_error, 0);

	rc = daos_event_fini(&ev);
	assert_rc_equal(rc, 0);

	print_message("  dfs_ostatx via handle2: nlink=%lu, size=%lu, mode=0%o\n",
		      (unsigned long)stbuf_handle2.st_nlink, (unsigned long)stbuf_handle2.st_size,
		      stbuf_handle2.st_mode);

	/* Verify handle2 sees the same data as handle1 */
	assert_int_equal(stbuf_handle2.st_nlink, 2);
	assert_int_equal(stbuf_handle2.st_size, write_size);
	assert_int_equal(stbuf_handle2.st_mode, stbuf1.st_mode);
	assert_int_equal(stbuf_handle2.st_uid, stbuf1.st_uid);
	assert_int_equal(stbuf_handle2.st_gid, stbuf1.st_gid);
	print_message("  Verified: handle2 sees identical metadata as handle1\n");
	print_message("  SUCCESS: dfs_ostatx correctly detected hardlink and fetched from HLM\n");

	/*
	 * Cleanup
	 */
	print_message("\nCleaning up...\n");

	rc = dfs_release(file_handle1);
	assert_int_equal(rc, 0);
	rc = dfs_release(file_handle2);
	assert_int_equal(rc, 0);
	rc = dfs_release(link_handle);
	assert_int_equal(rc, 0);

	rc = dfs_remove(dfs_mt, dir1, "testfile", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir2, "testfile_link", false, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_release(dir1);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir2);
	assert_int_equal(rc, 0);

	rc = dfs_remove(dfs_mt, NULL, "ostatx_dir1", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "ostatx_dir2", false, NULL);
	assert_int_equal(rc, 0);

	print_message("\nHardlink dfs_ostatx test completed successfully!\n");
}

static void
dfs_test_hardlink_osetattr(void **state)
{
	test_arg_t   *arg  = *state;
	dfs_obj_t    *dir1 = NULL, *dir2 = NULL;
	dfs_obj_t    *file_handle1 = NULL, *file_handle2 = NULL;
	dfs_obj_t    *link_handle = NULL;
	struct stat   stbuf1, stbuf2, stbuf_link;
	daos_obj_id_t oid1, oid2, oid_link;
	mode_t        new_mode;
	uid_t         new_uid;
	gid_t         new_gid;
	daos_size_t   new_size;
	daos_event_t  ev, *evp;
	int           rc;

	if (arg->myrank != 0)
		return;

	print_message("\n=== Test: dfs_osetattr with hardlinks ===\n");

	/*
	 * Step 1: Create two subdirectories
	 */
	print_message("\nStep 1: Creating two subdirectories...\n");
	rc = dfs_open(dfs_mt, NULL, "osetattr_dir1", S_IFDIR | S_IRWXU, O_RDWR | O_CREAT | O_EXCL,
		      0, 0, NULL, &dir1);
	assert_int_equal(rc, 0);
	print_message("  Created /osetattr_dir1\n");

	rc = dfs_open(dfs_mt, NULL, "osetattr_dir2", S_IFDIR | S_IRWXU, O_RDWR | O_CREAT | O_EXCL,
		      0, 0, NULL, &dir2);
	assert_int_equal(rc, 0);
	print_message("  Created /osetattr_dir2\n");

	/*
	 * Step 2: Create a file in dir1 (not root) and open TWO handles to it
	 */
	print_message("\nStep 2: Creating file in subdirectory and opening two handles...\n");

	/* First handle */
	rc = dfs_open(dfs_mt, dir1, "testfile", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file_handle1);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(file_handle1, &oid1);
	assert_int_equal(rc, 0);
	print_message("  Created /osetattr_dir1/testfile (handle1)\n");

	/* Second handle - opened before hardlink creation */
	rc = dfs_lookup(dfs_mt, "/osetattr_dir1/testfile", O_RDWR, &file_handle2, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(file_handle2, &oid2);
	assert_int_equal(rc, 0);
	print_message("  Opened second handle to same file (handle2)\n");

	/* Verify both handles point to the same OID */
	assert_true(oid1.lo == oid2.lo && oid1.hi == oid2.hi);
	print_message("  Verified: both handles have same OID\n");

	/*
	 * Step 3: Using handle1, convert to hardlink by creating link in dir2
	 */
	print_message("\nStep 3: Converting to hardlink using handle1...\n");
	rc = dfs_link(dfs_mt, file_handle1, dir2, "testfile_link", &link_handle, &stbuf_link);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(link_handle, &oid_link);
	assert_int_equal(rc, 0);
	assert_true(oid1.lo == oid_link.lo && oid1.hi == oid_link.hi);
	print_message("  Created /osetattr_dir2/testfile_link\n");
	print_message("  Verified: link has same OID as original\n");
	assert_int_equal(stbuf_link.st_nlink, 2);
	print_message("  Link stat: nlink=%lu (expected 2)\n", (unsigned long)stbuf_link.st_nlink);

	/*
	 * Step 4: Using handle1, set mode, ownership and size using dfs_osetattr()
	 */
	print_message(
	    "\nStep 4: Setting mode, ownership and size via dfs_osetattr() using handle1...\n");

	new_mode = S_IFREG | S_IRWXU | S_IRGRP | S_IXGRP; /* rwxr-x--- = 0750 */
	new_uid  = 1001;
	new_gid  = 2002;
	new_size = 4096; /* Set size to 4K */

	memset(&stbuf1, 0, sizeof(stbuf1));
	stbuf1.st_mode = new_mode;
	stbuf1.st_uid  = new_uid;
	stbuf1.st_gid  = new_gid;
	stbuf1.st_size = new_size;

	print_message("  Setting mode=0%o, uid=%u, gid=%u, size=%lu\n", new_mode & ~S_IFMT, new_uid,
		      new_gid, (unsigned long)new_size);

	rc = dfs_osetattr(dfs_mt, file_handle1, &stbuf1,
			  DFS_SET_ATTR_MODE | DFS_SET_ATTR_UID | DFS_SET_ATTR_GID |
			      DFS_SET_ATTR_SIZE);
	assert_int_equal(rc, 0);
	print_message("  dfs_osetattr() completed successfully\n");

	/*
	 * Step 5: Call dfs_ostat() using handle1 and verify stat values are correct
	 */
	print_message("\nStep 5: Verifying attributes via dfs_ostat() using handle1...\n");

	memset(&stbuf1, 0, sizeof(stbuf1));
	rc = dfs_ostat(dfs_mt, file_handle1, &stbuf1);
	assert_int_equal(rc, 0);

	print_message("  handle1 stat: mode=0%o, uid=%u, gid=%u, size=%lu, nlink=%lu\n",
		      stbuf1.st_mode & ~S_IFMT, stbuf1.st_uid, stbuf1.st_gid,
		      (unsigned long)stbuf1.st_size, (unsigned long)stbuf1.st_nlink);

	assert_int_equal(stbuf1.st_mode, new_mode);
	assert_int_equal(stbuf1.st_uid, new_uid);
	assert_int_equal(stbuf1.st_gid, new_gid);
	assert_int_equal(stbuf1.st_size, new_size);
	assert_int_equal(stbuf1.st_nlink, 2);
	print_message("  Verified: all attributes are correctly set via handle1\n");

	/*
	 * Step 6: Call dfs_ostatx() using handle2 (opened before hardlink creation)
	 * The output should be the same as handle1
	 */
	print_message("\nStep 6: Verifying attributes via dfs_ostatx() using handle2...\n");
	print_message("  (handle2 was opened before hardlink creation)\n");

	/* Initialize event for async operation */
	rc = daos_event_init(&ev, arg->eq, NULL);
	assert_rc_equal(rc, 0);

	memset(&stbuf2, 0, sizeof(stbuf2));
	rc = dfs_ostatx(dfs_mt, file_handle2, &stbuf2, &ev);
	assert_int_equal(rc, 0);

	/* Wait for async completion */
	rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
	assert_rc_equal(rc, 1);
	assert_ptr_equal(evp, &ev);
	assert_int_equal(evp->ev_error, 0);

	rc = daos_event_fini(&ev);
	assert_rc_equal(rc, 0);

	print_message("  handle2 stat: mode=0%o, uid=%u, gid=%u, size=%lu, nlink=%lu\n",
		      stbuf2.st_mode & ~S_IFMT, stbuf2.st_uid, stbuf2.st_gid,
		      (unsigned long)stbuf2.st_size, (unsigned long)stbuf2.st_nlink);

	/* Verify handle2 sees the same data as handle1 */
	assert_int_equal(stbuf2.st_mode, new_mode);
	assert_int_equal(stbuf2.st_uid, new_uid);
	assert_int_equal(stbuf2.st_gid, new_gid);
	assert_int_equal(stbuf2.st_size, new_size);
	assert_int_equal(stbuf2.st_nlink, 2);
	assert_int_equal(stbuf2.st_ino, stbuf1.st_ino);
	print_message("  Verified: handle2 (via dfs_ostatx) sees identical metadata as handle1\n");
	print_message(
	    "  SUCCESS: dfs_osetattr changes visible through stale handle via dfs_ostatx\n");

	/*
	 * Cleanup
	 */
	print_message("\nCleaning up...\n");

	rc = dfs_release(file_handle1);
	assert_int_equal(rc, 0);
	rc = dfs_release(file_handle2);
	assert_int_equal(rc, 0);
	rc = dfs_release(link_handle);
	assert_int_equal(rc, 0);

	rc = dfs_remove(dfs_mt, dir1, "testfile", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir2, "testfile_link", false, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_release(dir1);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir2);
	assert_int_equal(rc, 0);

	rc = dfs_remove(dfs_mt, NULL, "osetattr_dir1", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "osetattr_dir2", false, NULL);
	assert_int_equal(rc, 0);

	print_message("\nHardlink dfs_osetattr test completed successfully!\n");
}

/**
 * Test dfs_cont_check with HLM (Hardlink Metadata) inconsistencies.
 *
 * This test covers five scenarios:
 *
 * SCENARIO 1: Orphan HLM entries
 * - Creates a file with a hardlink (which creates an HLM entry)
 * - Punches ALL directory entries using low-level DAOS APIs
 * - HLM entry becomes orphaned (no directory entries point to the OID)
 * - Verifies PRINT mode reports the orphan
 * - Verifies REMOVE mode removes HLM entry and punches the object
 * - Verifies RELINK mode restores file in lost+found with correct nlink=1
 * - Additional test: Empty file orphan - RELINK deletes HLM entry instead of relinking
 *
 * SCENARIO 2: HLM link count mismatch
 * - Creates a file with a hardlink (nlink=2 stored in HLM)
 * - Punches only ONE directory entry using low-level DAOS APIs
 * - HLM has link count=2 but only 1 dentry exists
 * - Verifies PRINT mode reports the mismatch (stored=2, cur=1)
 * - Verifies RELINK mode fixes link count from 2 to 1
 *
 * SCENARIO 3: Missing hardlink bit in directory entry
 * - Creates a file with mode 555, then creates a hardlink
 * - Changes mode to 500 via the hardlink
 * - Clears the hardlink bit from one dentry using low-level DAOS APIs
 * - Verifies PRINT mode reports the missing hardlink bit
 * - Verifies RELINK mode restores the hardlink bit
 * - Verifies both files show mode 500 after repair
 *
 * SCENARIO 4: Spurious hardlink bit on regular file
 * - Creates a regular file (not a hardlink)
 * - Sets the hardlink bit on the dentry using low-level DAOS APIs
 * - Verifies dfs_osetattr() fails on the corrupted file
 * - Verifies PRINT mode reports the spurious hardlink bit
 * - Verifies RELINK mode clears the spurious hardlink bit
 * - Verifies dfs_osetattr() succeeds after repair
 *
 * SCENARIO 5: Hardlink bit on directory or symlink
 * - Creates a directory and a symlink
 * - Sets the hardlink bit on both using low-level DAOS APIs
 * - Verifies PRINT mode reports the spurious hardlink bit
 * - Verifies RELINK mode clears the hardlink bit from both
 * - Fetches dentries directly and verifies hardlink bit is cleared
 */
static void
dfs_test_checker_hlm(void **state)
{
	test_arg_t   *arg = *state;
	dfs_t        *dfs;
	dfs_obj_t    *dir, *file1, *file2;
	daos_obj_id_t file_oid, dir_oid;
	daos_handle_t coh;
	struct stat   stbuf;
	uint64_t      nr_oids = 0;
	char         *cname   = "cont_chkr_hlm";
	int           rc;

	if (arg->myrank != 0)
		return;

	print_message("Testing dfs_cont_check with HLM inconsistencies...\n");

	/*
	 * ==========================================================================
	 * SCENARIO 1: Orphan HLM entries (both directory entries punched)
	 * ==========================================================================
	 */
	print_message("\n=== SCENARIO 1: Orphan HLM entries ===\n");

	/*
	 * Part 1.1: Setup - Create container, file with hardlink
	 */
	print_message("Creating container and file with hardlink...\n");
	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_CREAT | O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);

	/* Create a directory */
	rc = dfs_open(dfs, NULL, "hlm_test_dir", S_IFDIR | S_IWUSR | S_IRUSR | S_IXUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &dir);
	assert_int_equal(rc, 0);

	/* Create a file */
	rc = dfs_open(dfs, dir, "testfile", S_IFREG | S_IWUSR | S_IRUSR, O_RDWR | O_CREAT | O_EXCL,
		      0, 0, NULL, &file1);
	assert_int_equal(rc, 0);

	/* Write some data to the file */
	{
		d_sg_list_t sgl;
		d_iov_t     iov;
		char       *buf;

		D_ALLOC(buf, 1024);
		assert_non_null(buf);
		memset(buf, 'A', 1024);
		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 1;
		sgl.sg_iovs   = &iov;
		d_iov_set(&iov, buf, 1024);
		rc = dfs_write(dfs, file1, &sgl, 0, NULL);
		assert_int_equal(rc, 0);
		D_FREE(buf);
		print_message("  Wrote 1024 bytes of data to file\n");
	}

	/* Get the file OID */
	rc = dfs_obj2id(file1, &file_oid);
	assert_int_equal(rc, 0);

	/* Create a hardlink - this creates the HLM entry */
	rc = dfs_link(dfs, file1, dir, "testfile_link", &file2, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_nlink, 2);
	print_message("  Created file " DF_OID " with nlink=2\n", DP_OID(file_oid));

	/* Get the directory OID for later use */
	rc = dfs_obj2id(dir, &dir_oid);
	assert_int_equal(rc, 0);

	rc = dfs_release(file1);
	assert_int_equal(rc, 0);
	rc = dfs_release(file2);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir);
	assert_int_equal(rc, 0);

	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	/** have to call fini to release the cached container handle for the checker to work */
	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/*
	 * Part 1.2: Corrupt - Punch all directory entries using low-level API
	 * This leaves the HLM entry orphaned
	 */
	print_message("Punching all directory entries (leaving HLM orphaned)...\n");
	rc = daos_cont_open(arg->pool.poh, cname, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	{
		daos_handle_t dir_oh;
		d_iov_t       dkey;

		/* Punch the file entries from the directory object */
		rc = daos_obj_open(coh, dir_oid, DAOS_OO_RW, &dir_oh, NULL);
		assert_rc_equal(rc, 0);

		/* Punch "testfile" entry */
		d_iov_set(&dkey, "testfile", strlen("testfile"));
		rc = daos_obj_punch_dkeys(dir_oh, DAOS_TX_NONE, DAOS_COND_PUNCH, 1, &dkey, NULL);
		assert_rc_equal(rc, 0);

		/* Punch "testfile_link" entry */
		d_iov_set(&dkey, "testfile_link", strlen("testfile_link"));
		rc = daos_obj_punch_dkeys(dir_oh, DAOS_TX_NONE, DAOS_COND_PUNCH, 1, &dkey, NULL);
		assert_rc_equal(rc, 0);

		rc = daos_obj_close(dir_oh, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	/*
	 * Part 1.3: Test PRINT mode - should report orphan HLM entry
	 */
	print_message("Testing PRINT mode - should report orphan...\n");
	rc = dfs_cont_check(arg->pool.poh, cname, DFS_CHECK_PRINT, NULL);
	assert_int_equal(rc, 0);

	/* Objects should still exist (PRINT doesn't modify anything) */
	get_nr_oids(arg->pool.poh, cname, &nr_oids);
	print_message("  Number of OIDs after PRINT: %lu\n", (unsigned long)nr_oids);
	/* Should be: SB + root + dir + file + HLM = 5 (HLM is a reserved object) */
	assert_int_equal(nr_oids, 5);

	/*
	 * Part 1.4: Test REMOVE mode - should remove HLM entry and punch object
	 */
	print_message("Testing REMOVE mode - should remove orphan HLM and object...\n");
	rc = dfs_cont_check(arg->pool.poh, cname, DFS_CHECK_PRINT | DFS_CHECK_REMOVE, NULL);
	assert_int_equal(rc, 0);

	/* File object should be removed (unmarked OID gets punched) */
	get_nr_oids(arg->pool.poh, cname, &nr_oids);
	print_message("  Number of OIDs after REMOVE: %lu\n", (unsigned long)nr_oids);
	/* Should be: SB + root + dir + HLM = 4 (file is gone) */
	assert_int_equal(nr_oids, 4);

	/*
	 * Part 1.5: Recreate corruption and test RELINK mode
	 */
	print_message("Recreating file with hardlink for RELINK test...\n");
	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);

	/* Create a new file with hardlink */
	rc = dfs_lookup(dfs, "/hlm_test_dir", O_RDWR, &dir, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_open(dfs, dir, "testfile2", S_IFREG | S_IWUSR | S_IRUSR, O_RDWR | O_CREAT | O_EXCL,
		      0, 0, NULL, &file1);
	assert_int_equal(rc, 0);

	/* Write some data to the file */
	{
		d_sg_list_t sgl;
		d_iov_t     iov;
		char       *buf;

		D_ALLOC(buf, 1024);
		assert_non_null(buf);
		memset(buf, 'A', 1024);
		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 1;
		sgl.sg_iovs   = &iov;
		d_iov_set(&iov, buf, 1024);
		rc = dfs_write(dfs, file1, &sgl, 0, NULL);
		assert_int_equal(rc, 0);
		D_FREE(buf);
		print_message("  Wrote 1024 bytes of data to file\n");
	}

	rc = dfs_obj2id(file1, &file_oid);
	assert_int_equal(rc, 0);

	rc = dfs_link(dfs, file1, dir, "testfile2_link", &file2, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_nlink, 2);
	print_message("  Created file " DF_OID " with nlink=2\n", DP_OID(file_oid));

	/* Get dir_oid before disconnecting */
	rc = dfs_obj2id(dir, &dir_oid);
	assert_int_equal(rc, 0);

	rc = dfs_release(file1);
	assert_int_equal(rc, 0);
	rc = dfs_release(file2);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir);
	assert_int_equal(rc, 0);
	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/* Punch directory entries again */
	rc = daos_cont_open(arg->pool.poh, cname, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	{
		daos_handle_t dir_oh;
		d_iov_t       dkey;

		rc = daos_obj_open(coh, dir_oid, DAOS_OO_RW, &dir_oh, NULL);
		assert_rc_equal(rc, 0);

		d_iov_set(&dkey, "testfile2", strlen("testfile2"));
		rc = daos_obj_punch_dkeys(dir_oh, DAOS_TX_NONE, DAOS_COND_PUNCH, 1, &dkey, NULL);
		assert_rc_equal(rc, 0);

		d_iov_set(&dkey, "testfile2_link", strlen("testfile2_link"));
		rc = daos_obj_punch_dkeys(dir_oh, DAOS_TX_NONE, DAOS_COND_PUNCH, 1, &dkey, NULL);
		assert_rc_equal(rc, 0);

		rc = daos_obj_close(dir_oh, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	print_message("Testing RELINK mode - should restore file in lost+found...\n");
	rc = dfs_cont_check(arg->pool.poh, cname, DFS_CHECK_PRINT | DFS_CHECK_RELINK, "lf_orphan");
	assert_int_equal(rc, 0);

	/* Verify file is in lost+found */
	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);

	{
		char fpath[128];

		/* Construct expected path in lost+found */
		sprintf(fpath, "/lost+found/lf_orphan/%" PRIu64 ".%" PRIu64 "", file_oid.hi,
			file_oid.lo);

		print_message("  Looking for restored file at %s\n", fpath);
		rc = dfs_lookup(dfs, fpath, O_RDONLY, &file1, NULL, &stbuf);
		assert_int_equal(rc, 0);

		/* Verify it's a regular file with the hardlink bit set and nlink=1 */
		assert_true(S_ISREG(stbuf.st_mode));
		assert_int_equal(stbuf.st_nlink, 1);
		print_message("  Found file with nlink=%lu\n", (unsigned long)stbuf.st_nlink);

		rc = dfs_release(file1);
		assert_int_equal(rc, 0);
	}

	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/* Cleanup scenario 1 - destroy container */
	rc = daos_cont_destroy(arg->pool.poh, cname, 1, NULL);
	assert_rc_equal(rc, 0);

	/*
	 * Part 1.5: RELINK with empty file - should delete HLM entry instead of relinking
	 */
	print_message(
	    "\n--- Scenario 1b: Empty orphan file - RELINK should delete HLM entry ---\n");

	/* Create container with empty file (no data) that has a hardlink */
	print_message("Creating container with empty hardlinked file...\n");
	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_CREAT | O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);

	/* Create directory */
	rc = dfs_open(dfs, NULL, "hlm_test_dir", S_IFDIR | S_IWUSR | S_IRUSR | S_IXUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &dir);
	assert_int_equal(rc, 0);

	/* Create empty file (no data written) */
	rc = dfs_open(dfs, dir, "empty_file", S_IFREG | 0644, O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL,
		      &file1);
	assert_int_equal(rc, 0);

	/* Create a hardlink to make it an HLM entry (link count > 1) */
	rc = dfs_link(dfs, file1, dir, "empty_file_link", &file2, NULL);
	assert_int_equal(rc, 0);
	print_message("  Created empty file with hardlink (nlink=2)\n");

	/* Get the file OID */
	rc = dfs_obj2id(file1, &file_oid);
	assert_int_equal(rc, 0);
	rc = dfs_obj2id(dir, &dir_oid);
	assert_int_equal(rc, 0);

	rc = dfs_release(file1);
	assert_int_equal(rc, 0);
	rc = dfs_release(file2);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir);
	assert_int_equal(rc, 0);

	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/* Punch ALL directory entries to make HLM entry orphaned */
	print_message("Punching all directory entries to orphan the HLM entry...\n");
	rc = daos_cont_open(arg->pool.poh, cname, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	{
		daos_handle_t dir_oh;
		d_iov_t       dkey;

		rc = daos_obj_open(coh, dir_oid, DAOS_OO_RW, &dir_oh, NULL);
		assert_rc_equal(rc, 0);

		d_iov_set(&dkey, "empty_file", strlen("empty_file"));
		rc = daos_obj_punch_dkeys(dir_oh, DAOS_TX_NONE, DAOS_COND_PUNCH, 1, &dkey, NULL);
		assert_rc_equal(rc, 0);

		d_iov_set(&dkey, "empty_file_link", strlen("empty_file_link"));
		rc = daos_obj_punch_dkeys(dir_oh, DAOS_TX_NONE, DAOS_COND_PUNCH, 1, &dkey, NULL);
		assert_rc_equal(rc, 0);

		rc = daos_obj_close(dir_oh, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	/* Run RELINK mode - should delete the HLM entry (file is empty, not worth relinking) */
	print_message("Testing RELINK mode - should delete HLM entry for empty file...\n");
	rc = dfs_cont_check(arg->pool.poh, cname, DFS_CHECK_PRINT | DFS_CHECK_RELINK, "lf_empty");
	assert_int_equal(rc, 0);

	/* Verify HLM object has no dkeys by querying container roots and listing dkeys */
	print_message("Verifying HLM object has no entries...\n");
	rc = daos_cont_open(arg->pool.poh, cname, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	{
		daos_prop_t               *prop = NULL;
		struct daos_prop_entry    *entry;
		struct daos_prop_co_roots *roots;
		daos_obj_id_t              hlm_oid;
		daos_handle_t              hlm_oh;
		daos_anchor_t              anchor = {0};
		uint32_t                   nr_dkeys;
		daos_key_desc_t            kds[1];
		d_sg_list_t                sgl;
		d_iov_t                    iov;
		char                       dkey_buf[64];

		/* Get HLM OID from container roots */
		prop = daos_prop_alloc(1);
		assert_non_null(prop);
		prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_ROOTS;
		rc                            = daos_cont_query(coh, NULL, prop, NULL);
		assert_rc_equal(rc, 0);
		entry = daos_prop_entry_get(prop, DAOS_PROP_CO_ROOTS);
		assert_non_null(entry);
		roots   = (struct daos_prop_co_roots *)entry->dpe_val_ptr;
		hlm_oid = roots->cr_oids[2];
		print_message("  HLM OID: " DF_OID "\n", DP_OID(hlm_oid));

		/* Open HLM object and list dkeys */
		rc = daos_obj_open(coh, hlm_oid, DAOS_OO_RO, &hlm_oh, NULL);
		assert_rc_equal(rc, 0);

		d_iov_set(&iov, dkey_buf, sizeof(dkey_buf));
		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs   = &iov;
		nr_dkeys      = 1;
		rc = daos_obj_list_dkey(hlm_oh, DAOS_TX_NONE, &nr_dkeys, kds, &sgl, &anchor, NULL);
		/* rc might be 0 or -DER_NONEXIST if no dkeys */
		if (rc == 0 && nr_dkeys == 0) {
			print_message("  HLM object has no entries (as expected)\n");
		} else if (rc == -DER_NONEXIST) {
			print_message("  HLM object has no entries (DER_NONEXIST)\n");
			rc = 0;
		} else {
			print_message("  ERROR: HLM object has %u entries (expected 0)\n",
				      nr_dkeys);
			assert_int_equal(nr_dkeys, 0);
		}

		rc = daos_obj_close(hlm_oh, NULL);
		assert_rc_equal(rc, 0);

		daos_prop_free(prop);
	}

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	/* Cleanup scenario 1b - destroy container */
	rc = daos_cont_destroy(arg->pool.poh, cname, 1, NULL);
	assert_rc_equal(rc, 0);

	print_message("Scenario 1 (orphan HLM) completed successfully!\n");

	/*
	 * ==========================================================================
	 * SCENARIO 2: HLM link count mismatch (one directory entry punched)
	 * ==========================================================================
	 */
	print_message("\n=== SCENARIO 2: HLM link count mismatch ===\n");

	/*
	 * Part 2.1: Setup - Create container, file with 2 hardlinks
	 */
	print_message("Creating container and file with 2 hardlinks...\n");
	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_CREAT | O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);

	/* Create a directory */
	rc = dfs_open(dfs, NULL, "hlm_test_dir", S_IFDIR | S_IWUSR | S_IRUSR | S_IXUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &dir);
	assert_int_equal(rc, 0);

	/* Create a file */
	rc = dfs_open(dfs, dir, "testfile", S_IFREG | S_IWUSR | S_IRUSR, O_RDWR | O_CREAT | O_EXCL,
		      0, 0, NULL, &file1);
	assert_int_equal(rc, 0);

	/* Write some data to the file */
	{
		d_sg_list_t sgl;
		d_iov_t     iov;
		char       *buf;

		D_ALLOC(buf, 1024);
		assert_non_null(buf);
		memset(buf, 'B', 1024);
		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 1;
		sgl.sg_iovs   = &iov;
		d_iov_set(&iov, buf, 1024);
		rc = dfs_write(dfs, file1, &sgl, 0, NULL);
		assert_int_equal(rc, 0);
		D_FREE(buf);
		print_message("  Wrote 1024 bytes of data to file\n");
	}

	/* Get the file OID */
	rc = dfs_obj2id(file1, &file_oid);
	assert_int_equal(rc, 0);

	/* Create a hardlink - this creates the HLM entry with link count = 2 */
	rc = dfs_link(dfs, file1, dir, "testfile_link", &file2, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_nlink, 2);
	print_message("  Created file " DF_OID " with nlink=2\n", DP_OID(file_oid));

	/* Get the directory OID for later use */
	rc = dfs_obj2id(dir, &dir_oid);
	assert_int_equal(rc, 0);

	rc = dfs_release(file1);
	assert_int_equal(rc, 0);
	rc = dfs_release(file2);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir);
	assert_int_equal(rc, 0);

	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/*
	 * Part 2.2: Corrupt - Punch only ONE directory entry using low-level API
	 * This leaves the file with 1 dentry but HLM has link count = 2
	 */
	print_message("Punching one directory entry (creating link count mismatch)...\n");
	rc = daos_cont_open(arg->pool.poh, cname, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	{
		daos_handle_t dir_oh;
		d_iov_t       dkey;

		/* Punch the file entries from the directory object */
		rc = daos_obj_open(coh, dir_oid, DAOS_OO_RW, &dir_oh, NULL);
		assert_rc_equal(rc, 0);

		/* Punch only "testfile_link" entry - keep "testfile" */
		d_iov_set(&dkey, "testfile_link", strlen("testfile_link"));
		rc = daos_obj_punch_dkeys(dir_oh, DAOS_TX_NONE, DAOS_COND_PUNCH, 1, &dkey, NULL);
		assert_rc_equal(rc, 0);

		rc = daos_obj_close(dir_oh, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	/*
	 * Part 2.3: Test PRINT mode - should report link count mismatch (stored=2, cur=1)
	 */
	print_message("Testing PRINT mode - should report link count mismatch...\n");
	rc = dfs_cont_check(arg->pool.poh, cname, DFS_CHECK_PRINT, NULL);
	assert_int_equal(rc, 0);

	/* Verify the file is still accessible and has wrong nlink */
	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);

	rc = dfs_lookup(dfs, "/hlm_test_dir/testfile", O_RDONLY, &file1, NULL, &stbuf);
	assert_int_equal(rc, 0);
	/* nlink should still be 2 because PRINT doesn't fix anything */
	print_message("  File nlink after PRINT mode: %lu (expected 2, unfixed)\n",
		      (unsigned long)stbuf.st_nlink);
	assert_int_equal(stbuf.st_nlink, 2);
	rc = dfs_release(file1);
	assert_int_equal(rc, 0);

	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/*
	 * Part 2.4: Test RELINK mode - should fix link count from 2 to 1
	 */
	print_message("Testing RELINK mode - should fix link count to 1...\n");
	rc =
	    dfs_cont_check(arg->pool.poh, cname, DFS_CHECK_PRINT | DFS_CHECK_RELINK, "lf_mismatch");
	assert_int_equal(rc, 0);

	/* Verify the file now has correct nlink = 1 */
	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);

	rc = dfs_lookup(dfs, "/hlm_test_dir/testfile", O_RDONLY, &file1, NULL, &stbuf);
	assert_int_equal(rc, 0);
	/* nlink should now be 1 because RELINK fixed the link count */
	print_message("  File nlink after RELINK mode: %lu (expected 1, fixed)\n",
		      (unsigned long)stbuf.st_nlink);
	assert_int_equal(stbuf.st_nlink, 1);
	rc = dfs_release(file1);
	assert_int_equal(rc, 0);

	/*
	 * Verify that lost+found/lf_mismatch directory is empty since
	 * no files were orphaned - only link count was fixed.
	 * Note: dfs_cont_check creates the directory regardless, so we check it's empty.
	 */
	rc = dfs_lookup(dfs, "/lost+found/lf_mismatch", O_RDONLY, &dir, NULL, NULL);
	assert_int_equal(rc, 0);
	{
		daos_anchor_t anchor = {0};
		uint32_t      nr     = 1;
		struct dirent ents[1];

		rc = dfs_readdir(dfs, dir, &anchor, &nr, ents);
		assert_int_equal(rc, 0);
		if (nr == 0) {
			print_message("  /lost+found/lf_mismatch is empty (as expected)\n");
		} else {
			print_message(
			    "  ERROR: /lost+found/lf_mismatch has entries (expected 0)\n");
			assert_int_equal(nr, 0);
		}
	}
	rc = dfs_release(dir);
	assert_int_equal(rc, 0);

	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/* Cleanup scenario 2 - destroy container */
	rc = daos_cont_destroy(arg->pool.poh, cname, 1, NULL);
	assert_rc_equal(rc, 0);

	print_message("Scenario 2 (link count mismatch) completed successfully!\n");

	/*
	 * ==========================================================================
	 * SCENARIO 3: Missing hardlink bit in directory entry
	 * ==========================================================================
	 */
	print_message("\n=== SCENARIO 3: Missing hardlink bit in dentry ===\n");

	/*
	 * Part 3.1: Setup - Create container, file with mode 555, then create hardlink
	 */
	print_message("Creating container and file with mode 555...\n");
	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_CREAT | O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);

	/* Create a directory */
	rc = dfs_open(dfs, NULL, "hlm_test_dir", S_IFDIR | S_IWUSR | S_IRUSR | S_IXUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &dir);
	assert_int_equal(rc, 0);

	/* Create a file with mode 555 */
	rc = dfs_open(dfs, dir, "testfile", S_IFREG | 0555, O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL,
		      &file1);
	assert_int_equal(rc, 0);

	/* Write some data to the file */
	{
		d_sg_list_t sgl;
		d_iov_t     iov;
		char       *buf;

		D_ALLOC(buf, 1024);
		assert_non_null(buf);
		memset(buf, 'C', 1024);
		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 1;
		sgl.sg_iovs   = &iov;
		d_iov_set(&iov, buf, 1024);
		rc = dfs_write(dfs, file1, &sgl, 0, NULL);
		assert_int_equal(rc, 0);
		D_FREE(buf);
		print_message("  Wrote 1024 bytes of data to file\n");
	}

	/* Verify initial mode is 555 */
	rc = dfs_stat(dfs, dir, "testfile", &stbuf);
	assert_int_equal(rc, 0);
	print_message("  Initial file mode: 0%o\n", stbuf.st_mode & 0777);
	assert_int_equal(stbuf.st_mode & 0777, 0555);

	/* Get the file OID */
	rc = dfs_obj2id(file1, &file_oid);
	assert_int_equal(rc, 0);

	/* Create a hardlink */
	rc = dfs_link(dfs, file1, dir, "testfile_link", &file2, &stbuf);
	assert_int_equal(rc, 0);
	assert_int_equal(stbuf.st_nlink, 2);
	print_message("  Created hardlink with nlink=2\n");

	/* Change mode to 500 via the second handle */
	stbuf.st_mode = S_IFREG | 0500;
	rc            = dfs_osetattr(dfs, file2, &stbuf, DFS_SET_ATTR_MODE);
	assert_int_equal(rc, 0);
	print_message("  Changed mode to 500 via hardlink\n");

	/* Verify both files now show mode 500 */
	rc = dfs_stat(dfs, dir, "testfile", &stbuf);
	assert_int_equal(rc, 0);
	print_message("  file1 mode after chmod: 0%o\n", stbuf.st_mode & 0777);
	assert_int_equal(stbuf.st_mode & 0777, 0500);

	rc = dfs_stat(dfs, dir, "testfile_link", &stbuf);
	assert_int_equal(rc, 0);
	print_message("  file2 mode after chmod: 0%o\n", stbuf.st_mode & 0777);
	assert_int_equal(stbuf.st_mode & 0777, 0500);

	/* Get the directory OID for later use */
	rc = dfs_obj2id(dir, &dir_oid);
	assert_int_equal(rc, 0);

	rc = dfs_release(file1);
	assert_int_equal(rc, 0);
	rc = dfs_release(file2);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir);
	assert_int_equal(rc, 0);

	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/*
	 * Part 3.2: Corrupt - Clear the hardlink bit from testfile's dentry
	 */
	print_message("Clearing hardlink bit from testfile's dentry...\n");
	rc = daos_cont_open(arg->pool.poh, cname, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	{
		daos_handle_t dir_oh;
		d_iov_t       dkey;
		d_iov_t       akey;
		d_sg_list_t   sgl;
		d_iov_t       iov;
		daos_iod_t    iod;
		daos_recx_t   recx;
		mode_t        mode;

		/* Open the directory object */
		rc = daos_obj_open(coh, dir_oid, DAOS_OO_RW, &dir_oh, NULL);
		assert_rc_equal(rc, 0);

		/* Fetch the current mode */
		d_iov_set(&dkey, "testfile", strlen("testfile"));
		d_iov_set(&akey, "DFS_INODE", strlen("DFS_INODE"));
		d_iov_set(&iov, &mode, sizeof(mode_t));
		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs   = &iov;
		recx.rx_idx   = 0; /* MODE_IDX = 0 */
		recx.rx_nr    = sizeof(mode_t);
		iod.iod_name  = akey;
		iod.iod_nr    = 1;
		iod.iod_recxs = &recx;
		iod.iod_type  = DAOS_IOD_ARRAY;
		iod.iod_size  = 1;

		rc = daos_obj_fetch(dir_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL, NULL);
		assert_rc_equal(rc, 0);
		print_message("  Fetched mode from dentry: 0x%x\n", mode);

		/* Clear the hardlink bit (MODE_HARDLINK_BIT = 1U << 31) */
		mode &= ~(1U << 31);
		print_message("  Mode after clearing hardlink bit: 0x%x\n", mode);

		/* Update the mode */
		rc = daos_obj_update(dir_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
		assert_rc_equal(rc, 0);
		print_message("  Updated dentry with cleared hardlink bit\n");

		rc = daos_obj_close(dir_oh, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	/*
	 * Part 3.3: Test PRINT mode - should report missing hardlink bit
	 */
	print_message("Testing PRINT mode - should report missing hardlink bit...\n");
	rc = dfs_cont_check(arg->pool.poh, cname, DFS_CHECK_PRINT, NULL);
	assert_int_equal(rc, 0);

	/* Verify the file still works but has inconsistent state */
	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);

	rc = dfs_lookup(dfs, "/hlm_test_dir", O_RDWR, &dir, NULL, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir);
	assert_int_equal(rc, 0);

	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/*
	 * Part 3.4: Test RELINK mode - should fix the hardlink bit
	 */
	print_message("Testing RELINK mode - should fix hardlink bit...\n");
	rc = dfs_cont_check(arg->pool.poh, cname, DFS_CHECK_PRINT | DFS_CHECK_RELINK, "lf_hlbit");
	assert_int_equal(rc, 0);

	/* Verify the hardlink bit is restored by fetching the dentry mode directly */
	rc = daos_cont_open(arg->pool.poh, cname, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	{
		daos_handle_t dir_oh;
		d_iov_t       dkey;
		d_iov_t       akey;
		d_sg_list_t   sgl;
		d_iov_t       iov;
		daos_iod_t    iod;
		daos_recx_t   recx;
		mode_t        mode;

		/* Open the directory object */
		rc = daos_obj_open(coh, dir_oid, DAOS_OO_RO, &dir_oh, NULL);
		assert_rc_equal(rc, 0);

		/* Fetch the mode */
		d_iov_set(&dkey, "testfile", strlen("testfile"));
		d_iov_set(&akey, "DFS_INODE", strlen("DFS_INODE"));
		d_iov_set(&iov, &mode, sizeof(mode_t));
		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs   = &iov;
		recx.rx_idx   = 0; /* MODE_IDX = 0 */
		recx.rx_nr    = sizeof(mode_t);
		iod.iod_name  = akey;
		iod.iod_nr    = 1;
		iod.iod_recxs = &recx;
		iod.iod_type  = DAOS_IOD_ARRAY;
		iod.iod_size  = 1;

		rc = daos_obj_fetch(dir_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL, NULL);
		assert_rc_equal(rc, 0);
		print_message("  Dentry mode after RELINK: 0x%x\n", mode);
		/* Verify hardlink bit is set (MODE_HARDLINK_BIT = 1U << 31) */
		assert_true((mode & (1U << 31)) != 0);
		print_message("  Hardlink bit is now set!\n");

		rc = daos_obj_close(dir_oh, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	/*
	 * Part 3.5: Verify both files show mode 500 via DFS APIs
	 */
	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);

	rc = dfs_lookup(dfs, "/hlm_test_dir", O_RDWR, &dir, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_stat(dfs, dir, "testfile", &stbuf);
	assert_int_equal(rc, 0);
	print_message("  file1 mode after repair: 0%o (expected 0500)\n", stbuf.st_mode & 0777);
	assert_int_equal(stbuf.st_mode & 0777, 0500);

	rc = dfs_stat(dfs, dir, "testfile_link", &stbuf);
	assert_int_equal(rc, 0);
	print_message("  file2 mode after repair: 0%o (expected 0500)\n", stbuf.st_mode & 0777);
	assert_int_equal(stbuf.st_mode & 0777, 0500);

	rc = dfs_release(dir);
	assert_int_equal(rc, 0);

	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/* Cleanup scenario 3 - destroy container */
	rc = daos_cont_destroy(arg->pool.poh, cname, 1, NULL);
	assert_rc_equal(rc, 0);

	print_message("Scenario 3 (missing hardlink bit) completed successfully!\n");

#if 0 /* REVISIT */
	/*
	 * ==========================================================================
	 * SCENARIO 4: Spurious hardlink bit on non-hardlink file
	 * ==========================================================================
	 */
	print_message("\n=== SCENARIO 4: Spurious hardlink bit on regular file ===\n");

	/*
	 * Part 4.1: Setup - Create container and a regular file (no hardlink)
	 */
	print_message("Creating container and regular file (no hardlink)...\n");
	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_CREAT | O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);

	/* Create a directory */
	rc = dfs_open(dfs, NULL, "hlm_test_dir", S_IFDIR | S_IWUSR | S_IRUSR | S_IXUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &dir);
	assert_int_equal(rc, 0);

	/* Create a regular file (NOT a hardlink) */
	rc = dfs_open(dfs, dir, "testfile", S_IFREG | 0644,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file1);
	assert_int_equal(rc, 0);

	/* Write some data to the file */
	{
		d_sg_list_t	sgl;
		d_iov_t		iov;
		char		*buf;

		D_ALLOC(buf, 1024);
		assert_non_null(buf);
		memset(buf, 'D', 1024);
		sgl.sg_nr = 1;
		sgl.sg_nr_out = 1;
		sgl.sg_iovs = &iov;
		d_iov_set(&iov, buf, 1024);
		rc = dfs_write(dfs, file1, &sgl, 0, NULL);
		assert_int_equal(rc, 0);
		D_FREE(buf);
		print_message("  Wrote 1024 bytes of data to file\n");
	}

	/* Get the directory OID for later use */
	rc = dfs_obj2id(dir, &dir_oid);
	assert_int_equal(rc, 0);

	rc = dfs_release(file1);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir);
	assert_int_equal(rc, 0);

	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/*
	 * Part 4.2: Corrupt - Set the hardlink bit on the dentry
	 */
	print_message("Setting hardlink bit on regular file's dentry...\n");
	rc = daos_cont_open(arg->pool.poh, cname, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	{
		daos_handle_t	dir_oh;
		d_iov_t		dkey;
		d_iov_t		akey;
		d_sg_list_t	sgl;
		d_iov_t		iov;
		daos_iod_t	iod;
		daos_recx_t	recx;
		mode_t		mode;

		/* Open the directory object */
		rc = daos_obj_open(coh, dir_oid, DAOS_OO_RW, &dir_oh, NULL);
		assert_rc_equal(rc, 0);

		/* Fetch the current mode */
		d_iov_set(&dkey, "testfile", strlen("testfile"));
		d_iov_set(&akey, "DFS_INODE", strlen("DFS_INODE"));
		d_iov_set(&iov, &mode, sizeof(mode_t));
		sgl.sg_nr = 1;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs = &iov;
		recx.rx_idx = 0;  /* MODE_IDX = 0 */
		recx.rx_nr = sizeof(mode_t);
		iod.iod_name = akey;
		iod.iod_nr = 1;
		iod.iod_recxs = &recx;
		iod.iod_type = DAOS_IOD_ARRAY;
		iod.iod_size = 1;

		rc = daos_obj_fetch(dir_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL, NULL);
		assert_rc_equal(rc, 0);
		print_message("  Fetched mode from dentry: 0x%x\n", mode);

		/* Set the hardlink bit (MODE_HARDLINK_BIT = 1U << 31) */
		mode |= (1U << 31);
		print_message("  Mode after setting hardlink bit: 0x%x\n", mode);

		/* Update the mode */
		rc = daos_obj_update(dir_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
		assert_rc_equal(rc, 0);
		print_message("  Updated dentry with spurious hardlink bit\n");

		rc = daos_obj_close(dir_oh, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	/*
	 * Part 4.3: Verify dfs_osetattr fails on corrupted file
	 */
	print_message("Verifying dfs_osetattr fails on corrupted file...\n");
	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);

	rc = dfs_lookup(dfs, "/hlm_test_dir", O_RDWR, &dir, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_lookup_rel(dfs, dir, "testfile", O_RDWR, &file1, NULL, NULL);
	assert_int_equal(rc, 0);

	/* Try to set mode - should fail because file claims to be hardlink but no HLM entry */
	stbuf.st_mode = S_IFREG | 0600;
	rc = dfs_osetattr(dfs, file1, &stbuf, DFS_SET_ATTR_MODE);
	print_message("  dfs_osetattr returned: %d (expected non-zero failure)\n", rc);
	assert_int_not_equal(rc, 0);

	rc = dfs_release(file1);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir);
	assert_int_equal(rc, 0);

	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/*
	 * Part 4.4: Test PRINT mode - should report spurious hardlink bit
	 */
	print_message("Testing PRINT mode - should report spurious hardlink bit...\n");
	rc = dfs_cont_check(arg->pool.poh, cname, DFS_CHECK_PRINT, NULL);
	assert_int_equal(rc, 0);

	/*
	 * Part 4.5: Test RELINK mode - should clear the hardlink bit
	 */
	print_message("Testing RELINK mode - should clear spurious hardlink bit...\n");
	rc = dfs_cont_check(arg->pool.poh, cname, DFS_CHECK_PRINT | DFS_CHECK_RELINK, "lf_spurious");
	assert_int_equal(rc, 0);

	/*
	 * Part 4.6: Verify dfs_osetattr now succeeds
	 */
	print_message("Verifying dfs_osetattr succeeds after repair...\n");
	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);

	rc = dfs_lookup(dfs, "/hlm_test_dir", O_RDWR, &dir, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = dfs_lookup_rel(dfs, dir, "testfile", O_RDWR, &file1, NULL, NULL);
	assert_int_equal(rc, 0);

	/* Now dfs_osetattr should succeed because hardlink bit is cleared */
	stbuf.st_mode = S_IFREG | 0600;
	rc = dfs_osetattr(dfs, file1, &stbuf, DFS_SET_ATTR_MODE);
	print_message("  dfs_osetattr returned: %d (expected 0)\n", rc);
	assert_int_equal(rc, 0);

	/* Verify mode was changed */
	rc = dfs_ostat(dfs, file1, &stbuf);
	assert_int_equal(rc, 0);
	print_message("  File mode after dfs_osetattr: 0%o (expected 0600)\n", stbuf.st_mode & 0777);
	assert_int_equal(stbuf.st_mode & 0777, 0600);

	rc = dfs_release(file1);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir);
	assert_int_equal(rc, 0);

	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/* Cleanup scenario 4 - destroy container */
	rc = daos_cont_destroy(arg->pool.poh, cname, 1, NULL);
	assert_rc_equal(rc, 0);

	print_message("Scenario 4 (spurious hardlink bit) completed successfully!\n");
#endif

	/*
	 * ==========================================================================
	 * SCENARIO 5: Hardlink bit on directory or symlink
	 * ==========================================================================
	 */
	print_message("\n=== SCENARIO 5: Hardlink bit on directory or symlink ===\n");

	/*
	 * Part 5.1: Setup - Create container with a directory and a symlink
	 */
	print_message("Creating container with directory and symlink...\n");
	rc = dfs_init();
	assert_int_equal(rc, 0);
	rc = dfs_connect(arg->pool.pool_str, arg->group, cname, O_CREAT | O_RDWR, NULL, &dfs);
	assert_int_equal(rc, 0);

	/* Create a parent directory */
	rc = dfs_open(dfs, NULL, "parent_dir", S_IFDIR | S_IWUSR | S_IRUSR | S_IXUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &dir);
	assert_int_equal(rc, 0);

	/* Create a test directory */
	rc = dfs_open(dfs, dir, "test_dir", S_IFDIR | 0755, O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL,
		      &file1);
	assert_int_equal(rc, 0);
	rc = dfs_release(file1);
	assert_int_equal(rc, 0);

	/* Create a symlink */
	rc = dfs_open(dfs, dir, "test_symlink", S_IFLNK | 0777, O_RDWR | O_CREAT | O_EXCL, 0, 0,
		      "target", &file2);
	assert_int_equal(rc, 0);
	rc = dfs_release(file2);
	assert_int_equal(rc, 0);

	/* Get the parent directory OID for later use */
	rc = dfs_obj2id(dir, &dir_oid);
	assert_int_equal(rc, 0);

	rc = dfs_release(dir);
	assert_int_equal(rc, 0);

	rc = dfs_disconnect(dfs);
	assert_int_equal(rc, 0);
	rc = dfs_fini();
	assert_int_equal(rc, 0);

	/*
	 * Part 5.2: Corrupt - Set the hardlink bit on directory and symlink
	 */
	print_message("Setting hardlink bit on directory and symlink dentries...\n");
	rc = daos_cont_open(arg->pool.poh, cname, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	{
		daos_handle_t parent_oh;
		d_iov_t       dkey;
		d_iov_t       akey;
		d_sg_list_t   sgl;
		d_iov_t       iov;
		daos_iod_t    iod;
		daos_recx_t   recx;
		mode_t        mode;

		/* Open the parent directory object */
		rc = daos_obj_open(coh, dir_oid, DAOS_OO_RW, &parent_oh, NULL);
		assert_rc_equal(rc, 0);

		/* Set hardlink bit on directory "test_dir" */
		d_iov_set(&dkey, "test_dir", strlen("test_dir"));
		d_iov_set(&akey, "DFS_INODE", strlen("DFS_INODE"));
		d_iov_set(&iov, &mode, sizeof(mode_t));
		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs   = &iov;
		recx.rx_idx   = 0; /* MODE_IDX = 0 */
		recx.rx_nr    = sizeof(mode_t);
		iod.iod_name  = akey;
		iod.iod_nr    = 1;
		iod.iod_recxs = &recx;
		iod.iod_type  = DAOS_IOD_ARRAY;
		iod.iod_size  = 1;

		rc = daos_obj_fetch(parent_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL, NULL);
		assert_rc_equal(rc, 0);
		print_message("  Directory mode before: 0x%x\n", mode);

		mode |= (1U << 31); /* MODE_HARDLINK_BIT */
		print_message("  Directory mode after setting hardlink bit: 0x%x\n", mode);

		rc = daos_obj_update(parent_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
		assert_rc_equal(rc, 0);

		/* Set hardlink bit on symlink "test_symlink" */
		d_iov_set(&dkey, "test_symlink", strlen("test_symlink"));

		rc = daos_obj_fetch(parent_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL, NULL);
		assert_rc_equal(rc, 0);
		print_message("  Symlink mode before: 0x%x\n", mode);

		mode |= (1U << 31); /* MODE_HARDLINK_BIT */
		print_message("  Symlink mode after setting hardlink bit: 0x%x\n", mode);

		rc = daos_obj_update(parent_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
		assert_rc_equal(rc, 0);

		print_message("  Updated both dentries with spurious hardlink bit\n");

		rc = daos_obj_close(parent_oh, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	/*
	 * Part 5.3: Test PRINT mode - should report spurious hardlink bits
	 */
	print_message("Testing PRINT mode - should report spurious hardlink bits...\n");
	rc = dfs_cont_check(arg->pool.poh, cname, DFS_CHECK_PRINT, NULL);
	assert_int_equal(rc, 0);

	/*
	 * Part 5.4: Test RELINK mode - should clear the hardlink bits
	 */
	print_message("Testing RELINK mode - should clear spurious hardlink bits...\n");
	rc = dfs_cont_check(arg->pool.poh, cname, DFS_CHECK_PRINT | DFS_CHECK_RELINK, NULL);
	assert_int_equal(rc, 0);

	/*
	 * Part 5.5: Verify hardlink bit is cleared by fetching dentries directly
	 */
	print_message("Verifying hardlink bit cleared via direct dentry fetch...\n");
	rc = daos_cont_open(arg->pool.poh, cname, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	{
		daos_handle_t parent_oh;
		d_iov_t       dkey;
		d_iov_t       akey;
		d_sg_list_t   sgl;
		d_iov_t       iov;
		daos_iod_t    iod;
		daos_recx_t   recx;
		mode_t        mode;

		/* Open the parent directory object */
		rc = daos_obj_open(coh, dir_oid, DAOS_OO_RO, &parent_oh, NULL);
		assert_rc_equal(rc, 0);

		/* Fetch directory mode and verify hardlink bit is cleared */
		d_iov_set(&dkey, "test_dir", strlen("test_dir"));
		d_iov_set(&akey, "DFS_INODE", strlen("DFS_INODE"));
		d_iov_set(&iov, &mode, sizeof(mode_t));
		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs   = &iov;
		recx.rx_idx   = 0; /* MODE_IDX = 0 */
		recx.rx_nr    = sizeof(mode_t);
		iod.iod_name  = akey;
		iod.iod_nr    = 1;
		iod.iod_recxs = &recx;
		iod.iod_type  = DAOS_IOD_ARRAY;
		iod.iod_size  = 1;

		rc = daos_obj_fetch(parent_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL, NULL);
		assert_rc_equal(rc, 0);
		print_message("  Directory mode from dentry: 0x%x\n", mode);
		print_message("  Expected: S_IFDIR | 0755 = 0x%x (no hardlink bit)\n",
			      S_IFDIR | 0755);
		assert_true(S_ISDIR(mode));
		assert_int_equal(mode & (1U << 31), 0); /* Hardlink bit should be cleared */
		assert_int_equal(mode & 0777, 0755);
		print_message("  Directory hardlink bit verified cleared\n");

		/* Fetch symlink mode and verify hardlink bit is cleared */
		d_iov_set(&dkey, "test_symlink", strlen("test_symlink"));

		rc = daos_obj_fetch(parent_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL, NULL);
		assert_rc_equal(rc, 0);
		print_message("  Symlink mode from dentry: 0x%x\n", mode);
		assert_true(S_ISLNK(mode));
		assert_int_equal(mode & (1U << 31), 0); /* Hardlink bit should be cleared */
		print_message("  Symlink hardlink bit verified cleared\n");

		rc = daos_obj_close(parent_oh, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	/* Cleanup scenario 5 - destroy container */
	rc = daos_cont_destroy(arg->pool.poh, cname, 1, NULL);
	assert_rc_equal(rc, 0);

	print_message("Scenario 5 (hardlink bit on directory/symlink) completed successfully!\n");
	print_message("\ndfs_test_checker_hlm completed successfully!\n");
}

static void
dfs_test_hardlink_stbuf_size(void **state)
{
	test_arg_t *arg = *state;
	dfs_obj_t  *dir;
	dfs_obj_t  *file, *link_obj;
	struct stat stbuf_before, stbuf_link, stbuf_after;
	d_sg_list_t sgl;
	d_iov_t     iov;
	char        buf[512];
	int         rc;

	if (arg->myrank != 0)
		return;

	print_message("dfs_test_hardlink_stbuf_size: verify st_size in dfs_link() stbuf\n");

	/* Create a directory for this test */
	rc = dfs_open(dfs_mt, NULL, "hlsz_dir", S_IFDIR | S_IWUSR | S_IRUSR | S_IXUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &dir);
	assert_int_equal(rc, 0);

	/* Create a regular file */
	rc = dfs_open(dfs_mt, dir, "hlsz_file", S_IFREG | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &file);
	assert_int_equal(rc, 0);

	/* Write a known amount of data so st_size is non-zero */
	memset(buf, 'A', sizeof(buf));
	d_iov_set(&iov, buf, sizeof(buf));
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs   = &iov;
	rc            = dfs_write(dfs_mt, file, &sgl, 0, NULL);
	assert_int_equal(rc, 0);

	/* Stat the source file to capture its size before linking */
	rc = dfs_ostat(dfs_mt, file, &stbuf_before);
	assert_int_equal(rc, 0);
	print_message("  Source file st_size = %lld (expected %zu)\n",
		      (long long)stbuf_before.st_size, sizeof(buf));
	assert_int_equal(stbuf_before.st_size, sizeof(buf));

	/* Create a hardlink and capture the stbuf returned by dfs_link() */
	rc = dfs_link(dfs_mt, file, dir, "hlsz_link", &link_obj, &stbuf_link);
	assert_int_equal(rc, 0);

	/* Bug check: st_size must not be 0 after dfs_link() */
	print_message("  dfs_link() stbuf st_size  = %lld (expected %zu)\n",
		      (long long)stbuf_link.st_size, sizeof(buf));
	assert_int_equal(stbuf_link.st_size, sizeof(buf));

	/* st_blocks must also be consistent (non-zero for a non-empty file) */
	print_message("  dfs_link() stbuf st_blocks = %lld\n", (long long)stbuf_link.st_blocks);
	assert_true(stbuf_link.st_blocks > 0);

	/* Cross-check: stat through the link entry and confirm size matches */
	rc = dfs_ostat(dfs_mt, link_obj, &stbuf_after);
	assert_int_equal(rc, 0);
	print_message("  dfs_ostat(link) st_size    = %lld\n", (long long)stbuf_after.st_size);
	assert_int_equal(stbuf_after.st_size, sizeof(buf));

	/* Cleanup */
	rc = dfs_release(link_obj);
	assert_int_equal(rc, 0);
	rc = dfs_release(file);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir, "hlsz_link", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, dir, "hlsz_file", false, NULL);
	assert_int_equal(rc, 0);
	rc = dfs_release(dir);
	assert_int_equal(rc, 0);
	rc = dfs_remove(dfs_mt, NULL, "hlsz_dir", false, NULL);
	assert_int_equal(rc, 0);

	print_message("dfs_test_hardlink_stbuf_size: PASSED\n");
}

static const struct CMUnitTest dfs_unit_tests[] = {
    {"DFS_UNIT_TEST1: DFS mount / umount", dfs_test_mount, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST2: DFS container modes", dfs_test_modes, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST3: DFS lookup / lookup_rel", dfs_test_lookup, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST4: Simple Symlinks", dfs_test_syml, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST5: Symlinks with / without O_NOFOLLOW", dfs_test_syml_follow, async_disable,
     test_case_teardown},
    {"DFS_UNIT_TEST6: multi-threads read shared file", dfs_test_read_shared_file, async_disable,
     test_case_teardown},
    {"DFS_UNIT_TEST7: DFS lookupx", dfs_test_lookupx, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST8: DFS IO sync error code", dfs_test_io_error_code, async_disable,
     test_case_teardown},
    {"DFS_UNIT_TEST9: DFS IO async error code", dfs_test_io_error_code, async_enable,
     test_case_teardown},
    {"DFS_UNIT_TEST10: multi-threads mkdir same dir", dfs_test_mt_mkdir, async_disable,
     test_case_teardown},
    {"DFS_UNIT_TEST11: Simple rename", dfs_test_rename, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST12: DFS API compat", dfs_test_compat, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST13: DFS l2g/g2l_all", dfs_test_handles, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST14: multi-threads connect to same container", dfs_test_mt_connect, async_disable,
     test_case_teardown},
    {"DFS_UNIT_TEST15: DFS chown", dfs_test_chown, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST16: DFS stat mtime", dfs_test_mtime, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST17: multi-threads async IO", dfs_test_async_io_th, async_disable,
     test_case_teardown},
    {"DFS_UNIT_TEST18: async IO", dfs_test_async_io, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST19: DFS readdir", dfs_test_readdir, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST20: dfs oclass hints", dfs_test_oclass_hints, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST21: dfs multiple pools", dfs_test_multiple_pools, async_disable,
     test_case_teardown},
    {"DFS_UNIT_TEST22: dfs extended attributes", dfs_test_xattrs, test_case_teardown},
    {"DFS_UNIT_TEST23: dfs MWC container checker", dfs_test_checker, async_disable,
     test_case_teardown},
    {"DFS_UNIT_TEST24: dfs MWC SB fix", dfs_test_fix_sb, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST25: dfs MWC root fix", dfs_test_relink_root, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST26: dfs MWC chunk size fix", dfs_test_fix_chunk_size, async_disable,
     test_case_teardown},
    {"DFS_UNIT_TEST27: dfs pipeline find", dfs_test_pipeline_find, async_disable,
     test_case_teardown},
    {"DFS_UNIT_TEST28: dfs open/lookup flags", dfs_test_oflags, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST29: dfs hardlink", dfs_test_hardlink, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST30: dfs hardlink chmod/chown", dfs_test_hardlink_chmod_chown, async_disable,
     test_case_teardown},
    {"DFS_UNIT_TEST31: dfs hardlink rename", dfs_test_hardlink_rename, async_disable,
     test_case_teardown},
    {"DFS_UNIT_TEST32: dfs hardlink xattr", dfs_test_hardlink_xattr, async_disable,
     test_case_teardown},
    {"DFS_UNIT_TEST33: dfs exchange", dfs_test_exchange, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST34: dfs hardlink access", dfs_test_hardlink_access, async_disable,
     test_case_teardown},
    {"DFS_UNIT_TEST35: dfs hardlink ostatx", dfs_test_hardlink_ostatx, async_disable,
     test_case_teardown},
    {"DFS_UNIT_TEST36: dfs hardlink osetattr", dfs_test_hardlink_osetattr, async_disable,
     test_case_teardown},
    {"DFS_UNIT_TEST37: dfs checker HLM", dfs_test_checker_hlm, async_disable, test_case_teardown},
    {"DFS_UNIT_TEST38: dfs hardlink stbuf st_size", dfs_test_hardlink_stbuf_size, async_disable,
     test_case_teardown},
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
	d_setenv("DFS_USE_DTX", "1", 1);

	par_barrier(PAR_COMM_WORLD);
	rc += cmocka_run_group_tests_name("DAOS_FileSystem_DFS_Unit_DTX", dfs_unit_tests,
					  dfs_setup, dfs_teardown);
	par_barrier(PAR_COMM_WORLD);
	return rc;
}
