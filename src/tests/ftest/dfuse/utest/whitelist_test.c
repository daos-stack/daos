/*
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This tests the whitelist mode of the DAOS libpil4dfs library
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <setjmp.h>
#include <cmocka.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <gurt/common.h>

static char *fuse_mnt;
static char *preload_path;
static char  exe_path[PATH_MAX];

static int
task_child(void)
{
	int   rc, fd, status;
	pid_t pid;
	char *str_preload;
	char *argv[3] = {"/usr/bin/ls", "", NULL};
	/* The first string in envp[5] is a placeholder. It will be overridden by str_preload. */
	char *envp[5] = {"LD_PRELOAD=/dummy_path/libpil4dfs.so", "D_LOG_MASK=DEBUG", "DD_SUBSYS=il",
			 "DD_MASK=DEBUG", NULL};

	rc = asprintf(&str_preload, "LD_PRELOAD=%s", preload_path);
	assert_true(rc > 0);
	envp[0] = str_preload;

	/* access dfuse mount point to trigger daos_init() */
	fd = open(fuse_mnt, O_RDONLY);
	assert_return_code(fd, errno);
	rc = close(fd);
	assert_return_code(rc, errno);

	/* ls to list dfuse mount point */
	argv[1] = fuse_mnt;
	pid     = fork();
	if (pid == 0)
		/* run "ls" as child process */
		execve("/usr/bin/ls", argv, envp);
	waitpid(pid, &status, 0);
	if (WIFEXITED(status))
		assert_int_equal(WEXITSTATUS(status), 0);

	return 0;
}

static void
test_whitelist_mode(void **state)
{
	pid_t pid;
	int   i, rc, status;
	char *argv[3] = {"whitelist_test", "child", NULL};

	rc = readlink("/proc/self/exe", exe_path, PATH_MAX - 1);
	assert_true(rc > 0);

	for (i = 0; i < 5; i++) {
		pid = fork();
		if (pid == 0)
			execv(exe_path, argv);
		waitpid(pid, &status, 0);
		if (WIFEXITED(status))
			assert_int_equal(WEXITSTATUS(status), 0);
	}
}

int
main(int argc, char *argv[])
{
	struct CMUnitTest tests[] = {cmocka_unit_test(test_whitelist_mode)};

	d_register_alt_assert(mock_assert);
	fuse_mnt = getenv("D_DFUSE_MNT");
	assert_true(fuse_mnt != NULL);
	preload_path = getenv("LD_PRELOAD");
	assert_true(preload_path != NULL);

	if (argc == 2)
		if (strcmp(argv[1], "child") == 0)
			return task_child();

	return cmocka_run_group_tests_name("utest_whitelist_jobs", tests, NULL, NULL);
}
