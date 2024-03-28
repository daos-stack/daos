/*
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This tests a threaded client sending RPCs on a single context
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include <gurt/common.h>

static char   mnt_path[PATH_MAX];
static size_t mnt_len;

static int
init_tests(void **state)
{
	int rc;

	rc = d_getenv_str(mnt_path, ARRAY_SIZE(mnt_path), "D_DFUSE_MNT");
	assert_int_equal(rc, -DER_SUCCESS);

	mnt_len = strnlen(mnt_path, PATH_MAX);
	assert_true(mnt_len < PATH_MAX);

	return d_log_init();
}

static int
fini_tests(void **state)
{
	d_log_fini();

	return 0;
}

static void
create_dir_tree(int fd)
{
	int rc;

	printf("\ncreating directory '/a'\n");
	rc = mkdirat(fd, "a", 0755);
	assert_return_code(rc, errno);

	printf("\ncreating directory '/a/bb'\n");
	rc = mkdirat(fd, "a/bb", 0755);
	assert_return_code(rc, errno);

	printf("\ncreating directory '/a/ccc'\n");
	rc = mkdirat(fd, "a/ccc", 0755);
	assert_return_code(rc, errno);

	printf("\ncreating directory '/a/bb/d'\n");
	rc = mkdirat(fd, "a/bb/d", 0755);
	assert_return_code(rc, errno);
}

static void
remove_tree_at(int fd)
{
	int   rc;
	char *path;

	printf("\nremoving directory '/a/bb/d'\n");
	rc = unlinkat(fd, "a/bb/d", AT_REMOVEDIR);
	assert_return_code(rc, errno);

	printf("\nremoving directory '/a/bb'\n");
	D_ALLOC(path, PATH_MAX);
	assert_non_null(path);
	memcpy(path, mnt_path, mnt_len);
	memcpy(path + mnt_len, "/a/bb", sizeof("/a/bb"));
	rc = unlinkat(fd, path, AT_REMOVEDIR);
	assert_return_code(rc, errno);
	D_FREE(path);

	printf("\nremoving directory '/a/ccc'\n");
	rc = unlinkat(fd, "a/ccc", AT_REMOVEDIR);
	assert_return_code(rc, errno);

	printf("\nremoving directory '/a'\n");
	rc = unlinkat(fd, "a", AT_REMOVEDIR);
	assert_return_code(rc, errno);
}

static void
test_mkdirat(void **state)
{
	int fd;
	int rc;

	(void)state; /* unused */

	printf("\n-- INIT of test_mkdirat --\n");

	printf("Opening path '%s'\n", mnt_path);
	fd = open(mnt_path, O_DIRECTORY, O_RDWR);
	assert_return_code(fd, errno);

	printf("\n-- START of test_mkdirat --");

	create_dir_tree(fd);

	printf("\n-- END of test_mkdirat --\n");

	remove_tree_at(fd);

	printf("Closing fd of path '%s'\n", mnt_path);
	rc = close(fd);
	assert_return_code(rc, errno);
}

static void
test_unlinkat(void **state)
{
	int fd;
	int rc;

	(void)state; /* unused */

	printf("\n-- INIT of test_unlinkat --\n");

	printf("Opening path '%s'\n", mnt_path);
	fd = open(mnt_path, O_DIRECTORY, O_RDWR);
	assert_return_code(fd, errno);

	create_dir_tree(fd);

	printf("\n-- START of test_unlinkat --");

	remove_tree_at(fd);

	printf("\n-- END of test_unlinkat --\n");

	printf("Closing fd of path '%s'\n", mnt_path);
	rc = close(fd);
	assert_return_code(rc, errno);
}

static void
test_rmdir(void **state)
{
	int   fd;
	char *path;
	char *tmp;
	int   rc;

	(void)state; /* unused */

	printf("\n-- INIT of test_rmdir --\n");

	printf("Opening path '%s'\n", mnt_path);
	fd = open(mnt_path, O_DIRECTORY, O_RDWR);
	assert_return_code(fd, errno);

	create_dir_tree(fd);

	printf("Closing fd of path '%s'\n", mnt_path);
	rc = close(fd);
	assert_return_code(rc, errno);

	D_ALLOC(path, PATH_MAX);
	assert_non_null(path);
	memcpy(path, mnt_path, mnt_len);
	tmp = path + mnt_len;

	printf("\n-- START of test_rmdir --\n");

	printf("\nremoving directory '/a/bb/d'\n");
	memcpy(tmp, "/a/bb/d", sizeof("/a/bb/d"));
	rc = rmdir(path);
	assert_return_code(rc, errno);

	printf("\nremoving directory '/a/bb'\n");
	memcpy(tmp, "/a/bb", sizeof("/a/bb"));
	rc = rmdir(path);
	assert_return_code(rc, errno);

	printf("\nremoving directory '/a/ccc'\n");
	memcpy(tmp, "/a/ccc", sizeof("/a/ccc"));
	rc = rmdir(path);
	assert_return_code(rc, errno);

	printf("\nremoving directory '/a'\n");
	memcpy(tmp, "/a", sizeof("/a"));
	rc = rmdir(path);
	assert_return_code(rc, errno);

	D_FREE(path);
	printf("\n-- END of test_rmdir --\n");
}

static void
test_rename(void **state)
{
	int   fd;
	char *path_old;
	char *path_new;
	int   rc;

	(void)state; /* unused */

	printf("\n-- INIT of test_rename --\n");

	printf("Opening path '%s'\n", mnt_path);
	fd = open(mnt_path, O_DIRECTORY, O_RDWR);
	assert_return_code(fd, errno);

	create_dir_tree(fd);

	printf("Closing fd of path '%s'\n", mnt_path);
	rc = close(fd);
	assert_return_code(rc, errno);

	D_ALLOC(path_old, PATH_MAX);
	assert_non_null(path_old);
	memcpy(path_old, mnt_path, mnt_len);
	D_ALLOC(path_new, PATH_MAX);
	assert_non_null(path_new);
	memcpy(path_new, mnt_path, mnt_len);

	printf("\n-- START of test_rename --\n");

	printf("\nrenaming directory '/a/bb' -> '/a/ccc/bb\n");
	memcpy(path_old + mnt_len, "/a/bb", sizeof("/a/bb"));
	memcpy(path_new + mnt_len, "/a/ccc/foo", sizeof("/a/ccc/foo"));
	rc = rename(path_old, path_new);
	assert_return_code(rc, errno);

	printf("\nrenaming directory '/a/ccc/foo/d' -> '/a/ccc/foo/bar\n");
	memcpy(path_old + mnt_len, "/a/ccc/foo/d", sizeof("/a/ccc/foo/d"));
	memcpy(path_new + mnt_len, "/a/ccc/foo/bar", sizeof("/a/ccc/foo/bar"));
	rc = rename(path_old, path_new);
	assert_return_code(rc, errno);

	printf("\nInvalid renaming of directory '/a/ccc/foo/bar\n");
	memcpy(path_old + mnt_len, "/a/ccc/foo/bar", sizeof("/a/ccc/foo/bar"));
	memcpy(path_new, "/tmp/bar", sizeof("/tmp/bar"));
	errno = 0;
	rc    = rename(path_old, path_new);
	assert_int_equal(rc, -1);
	assert_int_equal(errno, EXDEV);

	D_FREE(path_new);
	D_FREE(path_old);
	printf("\n-- END of test_rename --\n");
}

int
main(int argc, char *argv[])
{
	size_t            test_id;
	struct CMUnitTest tests[] = {
	    cmocka_unit_test(test_mkdirat),
	    cmocka_unit_test(test_unlinkat),
	    cmocka_unit_test(test_rmdir),
	    cmocka_unit_test(test_rename),
	};
	struct CMUnitTest test[1];

	d_register_alt_assert(mock_assert);

	assert_int_equal(argc, 2);
	test_id = atoi(argv[1]);
	assert_true(test_id < ARRAY_SIZE(tests));
	memcpy(&test[0], tests + test_id, sizeof(struct CMUnitTest));

	return cmocka_run_group_tests_name("utest_pil4dfs", test, init_tests, fini_tests);
}
