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

	rc = close(root);
	assert_return_code(rc, errno);
}

int
main(int argc, char **argv)
{
	int                     index = 0;
	int                     opt;
	struct option           long_options[] = {{"test-dir", required_argument, NULL, 'M'},
						  {NULL, 0, NULL, 0} };

	const struct CMUnitTest tests[] = {
	    cmocka_unit_test(do_openat),
	    cmocka_unit_test(do_ioctl),
	};

	while ((opt = getopt_long(argc, argv, "M:", long_options, &index)) != -1) {
		switch (opt) {
		case 'M':
			test_dir = optarg;
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

	return cmocka_run_group_tests(tests, NULL, NULL);
}
