/**
 * (C) Copyright 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Unit testing for dfuse and Interception library.  This code does not interact with dfuse
 * directly, however makes filesystem calls into libc and checks the results are as expected.
 *
 * It is also called with the Interception Library to verify that I/O calls have the expected
 * behavior in this case as well.
 *
 * It uses cmocka, but not to mock any functions, only for the reporting and assert macros.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdio.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <sys/ioctl.h>

#include "dfuse_ioctl.h"

static void
print_usage()
{
	printf("DFuse tests\n");
	printf("dfuse_test --test-dir <path to test>\n");
	printf("dfuse_test -m|--metadata\n");
}

char *test_dir;

#ifndef O_PATH
#define O_PATH 0
#endif

void
do_openat(void **state)
{
	struct stat stbuf0;
	struct stat stbuf;
	int         fd;
	int         rc;
	char        output_buf[10];
	char        input_buf[] = "hello";
	off_t       offset;
	int         root = open(test_dir, O_PATH | O_DIRECTORY);

	assert_return_code(root, errno);

	fd = openat(root, "my_file", O_RDWR | O_CREAT | O_EXCL, S_IWUSR | S_IRUSR);
	assert_return_code(fd, errno);

	/* This will write six bytes, including a \0 terminator */
	rc = write(fd, input_buf, sizeof(input_buf));
	assert_return_code(rc, errno);

	/* First fstat.  IL will forward this to the kernel so it can save ino for future calls */
	rc = fstat(fd, &stbuf0);
	assert_return_code(rc, errno);
	assert_int_equal(stbuf0.st_size, sizeof(input_buf));

	/* Second fstat.  IL will bypass the kernel for this one */
	rc = fstat(fd, &stbuf);
	assert_return_code(rc, errno);
	assert_int_equal(stbuf.st_size, sizeof(input_buf));
	assert_int_equal(stbuf0.st_dev, stbuf.st_dev);
	assert_int_equal(stbuf0.st_ino, stbuf.st_ino);

	/* This will write six bytes, including a \0 terminator */
	rc = write(fd, input_buf, sizeof(input_buf));
	assert_return_code(rc, errno);

	/* fstat to check the file size is updated */
	rc = fstat(fd, &stbuf0);
	assert_return_code(rc, errno);
	assert_int_equal(stbuf0.st_size, sizeof(input_buf) * 2);

	/* stat through kernel to ensure it has observed write */
	rc = fstatat(root, "my_file", &stbuf, AT_SYMLINK_NOFOLLOW);
	assert_return_code(rc, errno);
	assert_int_equal(stbuf.st_size, stbuf0.st_size);

	offset = lseek(fd, -8, SEEK_CUR);
	assert_return_code(offset, errno);
	assert_int_equal(offset, sizeof(input_buf) - 2);

	rc = read(fd, &output_buf, 2);
	assert_return_code(rc, errno);
	assert_int_equal(rc, 2);
	assert_memory_equal(&input_buf[offset], &output_buf, rc);

	rc = fstat(fd, &stbuf);
	assert_return_code(rc, errno);
	assert_int_equal(stbuf.st_size, 12);

	rc = ftruncate(fd, offset);
	assert_return_code(rc, errno);

	rc = fstatat(root, "my_file", &stbuf, AT_SYMLINK_NOFOLLOW);
	assert_return_code(rc, errno);
	assert_int_equal(stbuf.st_size, offset);

	rc = fstat(fd, &stbuf);
	assert_return_code(rc, errno);
	assert_int_equal(stbuf.st_size, offset);

	/* stat/fstatat */
	rc = read(fd, &output_buf, 2);
	assert_return_code(rc, errno);
	assert_int_equal(rc, 0);

	offset = lseek(fd, -4, SEEK_CUR);
	assert_return_code(offset, errno);
	assert_int_equal(offset, 2);

	rc = read(fd, &output_buf, 10);
	assert_return_code(rc, errno);
	assert_int_equal(rc, 2);
	assert_memory_equal(&input_buf[offset], &output_buf, rc);

	rc = fstat(fd, &stbuf);
	assert_return_code(rc, errno);
	assert_int_equal(stbuf.st_size, 4);

	rc = fstatat(root, "my_file", &stbuf0, AT_SYMLINK_NOFOLLOW);
	assert_return_code(rc, errno);
	assert_int_equal(stbuf.st_size, stbuf0.st_size);

	rc = close(fd);
	assert_return_code(rc, errno);

	rc = unlinkat(root, "my_file", 0);
	assert_return_code(rc, errno);

	rc = close(root);
	assert_return_code(rc, errno);
}

void
do_ioctl(void **state)
{
	int                     fd;
	int                     rc;
	struct dfuse_user_reply dur  = {};
	int                     root = open(test_dir, O_DIRECTORY);

	assert_return_code(root, errno);

	/* Open a file in dfuse and call the ioctl on it and verify the uid/gids match */
	fd = openat(root, "ioctl_file", O_RDWR | O_CREAT | O_EXCL, S_IWUSR | S_IRUSR);
	assert_return_code(fd, errno);

	rc = ioctl(fd, DFUSE_IOCTL_DFUSE_USER, &dur);
	assert_return_code(rc, errno);

	assert_int_equal(dur.uid, geteuid());
	assert_int_equal(dur.gid, getegid());

	rc = close(fd);
	assert_return_code(rc, errno);

	/* Now do the same test but on the directory itself */
	rc = ioctl(root, DFUSE_IOCTL_DFUSE_USER, &dur);
	assert_return_code(rc, errno);

	assert_int_equal(dur.uid, geteuid());
	assert_int_equal(dur.gid, getegid());

	rc = unlinkat(root, "ioctl_file", 0);
	assert_return_code(rc, errno);

	rc = close(root);
	assert_return_code(rc, errno);
}

static bool
timespec_gt(struct timespec t1, struct timespec t2)
{
	if (t2.tv_sec == t1.tv_sec)
		return t2.tv_nsec < t1.tv_nsec;
	else
		return t2.tv_sec < t1.tv_sec;
}

void
do_mtime(void **state)
{
	struct stat	stbuf;
	struct timespec	prev_ts;
	struct timespec	now;
	struct timespec	times[2];
	int		fd;
	int		rc;
	char		input_buf[] = "hello";
	int		root = open(test_dir, O_PATH | O_DIRECTORY);

	assert_return_code(root, errno);

	/* Open a file and sanity check the mtime */
	fd = openat(root, "my_file", O_RDWR | O_CREAT | O_EXCL, S_IWUSR | S_IRUSR);
	assert_return_code(fd, errno);
	rc = clock_gettime(CLOCK_REALTIME, &now);
	assert_return_code(fd, errno);
	rc = fstat(fd, &stbuf);
	assert_return_code(rc, errno);
	prev_ts.tv_sec = stbuf.st_mtim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_mtim.tv_nsec;
	assert_true(now.tv_sec - prev_ts.tv_sec < 3);

	/* Write to the file and verify mtime is newer */
	rc = write(fd, input_buf, sizeof(input_buf));
	assert_return_code(rc, errno);
	rc = fstat(fd, &stbuf);
	assert_return_code(rc, errno);
	printf("prev_ts.tv_sec        = %ld\n", prev_ts.tv_sec);
	printf("prev_ts.tv_nsec       = %ld\n", prev_ts.tv_nsec);
	printf("stbuf.st_mtim.tv_sec  = %ld\n", stbuf.st_mtim.tv_sec);
	printf("stbuf.st_mtim.tv_nsec = %ld\n", stbuf.st_mtim.tv_nsec);
	assert_true(timespec_gt(stbuf.st_mtim, prev_ts));
	prev_ts.tv_sec = stbuf.st_mtim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_mtim.tv_nsec;

	/* Truncate the file and verify mtime is newer */
	rc = ftruncate(fd, 0);
	assert_return_code(rc, errno);
	rc = fstat(fd, &stbuf);
	assert_return_code(rc, errno);
	printf("prev_ts.tv_sec        = %ld\n", prev_ts.tv_sec);
	printf("prev_ts.tv_nsec       = %ld\n", prev_ts.tv_nsec);
	printf("stbuf.st_mtim.tv_sec  = %ld\n", stbuf.st_mtim.tv_sec);
	printf("stbuf.st_mtim.tv_nsec = %ld\n", stbuf.st_mtim.tv_nsec);
	assert_true(timespec_gt(stbuf.st_mtim, prev_ts));
	prev_ts.tv_sec = stbuf.st_mtim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_mtim.tv_nsec;

	/* Set and verify mtime set in the past */
	times[0] = now;
	times[1].tv_sec = now.tv_sec - 10;
	times[1].tv_nsec = 20;
	rc = futimens(fd, times);
	assert_return_code(fd, errno);
	rc = fstat(fd, &stbuf);
	assert_return_code(rc, errno);
	assert_int_equal(stbuf.st_mtim.tv_sec, times[1].tv_sec);
	assert_int_equal(stbuf.st_mtim.tv_nsec, times[1].tv_nsec);
	prev_ts.tv_sec = stbuf.st_mtim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_mtim.tv_nsec;

	rc = close(fd);
	assert_return_code(rc, errno);

	rc = unlinkat(root, "my_file", 0);
	assert_return_code(rc, errno);

	rc = close(root);
	assert_return_code(rc, errno);
}

int
main(int argc, char **argv)
{
	int                     index = 0;
	int                     opt;
	bool			do_run_metadata = false;
	int			nr_failed;

	struct option           long_options[] = {
		{"test-dir",	required_argument,	NULL,	'M'},
		{"metadata",	no_argument,		NULL,	'm'},
		{NULL,		0,			NULL,	0}
	};

	const struct CMUnitTest basic_tests[] = {
	    cmocka_unit_test(do_openat),
	    cmocka_unit_test(do_ioctl),
	};

	const struct CMUnitTest metadata_tests[] = {
	    cmocka_unit_test(do_mtime),
	};

	while ((opt = getopt_long(argc, argv, "M:m", long_options, &index)) != -1) {
		switch (opt) {
		case 'M':
			test_dir = optarg;
			break;
		case 'm':
			do_run_metadata = true;
			break;
		default:
			printf("Unknown Option\n");
			print_usage();
			return 1;
		}
	}

	if (test_dir == NULL) {
		printf("--test-dir option required\n");
		return 1;
	}

	nr_failed = cmocka_run_group_tests(basic_tests, NULL, NULL);
	if (do_run_metadata)
		nr_failed += cmocka_run_group_tests(metadata_tests, NULL, NULL);

	return nr_failed;
}
