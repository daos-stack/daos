/**
 * (C) Copyright 2021-2023 Intel Corporation.
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
#include <sys/statfs.h>
#include <sys/ioctl.h>
#include <dirent.h>

#include "dfuse_ioctl.h"

/* Tests can be run by specifying the appropriate argument for a test or all will be run if no test
 * is specified.
 */
static const char *all_tests = "ismd";

static void
print_usage()
{
	print_message("\n\nDFuse tests\n=============================\n");
	print_message("dfuse_test -M|--test-dir <path to test>\n");
	print_message("Tests: Use one of these arg(s) for specific test\n");
	print_message("dfuse_test -a|--all\n");
	print_message("dfuse_test -i|--io\n");
	print_message("dfuse_test -s|--stream\n");
	print_message("dfuse_test -m|--metadata\n");
	print_message("dfuse_test -d|--directory\n");
	print_message("Default <dfuse_test> runs all tests\n=============\n");
	print_message("\n=============================\n");
}

char *test_dir;

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

	fd = openat(root, "openat_file", O_RDWR | O_CREAT | O_EXCL, S_IWUSR | S_IRUSR);
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
	rc = fstatat(root, "openat_file", &stbuf, AT_SYMLINK_NOFOLLOW);
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

	rc = fstatat(root, "openat_file", &stbuf, AT_SYMLINK_NOFOLLOW);
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

	rc = fstatat(root, "openat_file", &stbuf0, AT_SYMLINK_NOFOLLOW);
	assert_return_code(rc, errno);
	assert_int_equal(stbuf.st_size, stbuf0.st_size);

	rc = close(fd);
	assert_return_code(rc, errno);

	rc = unlinkat(root, "openat_file", 0);
	assert_return_code(rc, errno);

	rc = close(root);
	assert_return_code(rc, errno);
}

void
do_stream(void **state)
{
	int    fd;
	int    rc;
	FILE  *stream;
	size_t count;
	off_t  offset;

	int    root = open(test_dir, O_PATH | O_DIRECTORY);

	assert_return_code(root, errno);

	/* Streaming I/O testing */
	fd = openat(root, "stream_file", O_RDWR | O_CREAT | O_EXCL, S_IWUSR | S_IRUSR);
	assert_return_code(fd, errno);
	stream = fdopen(fd, "w+");
	assert_non_null(stream);

	count = fwrite("abcdefghijkl", 1, 10, stream);
	assert_int_equal(count, 10);

	errno = 0;
	rewind(stream);
	assert_int_equal(errno, 0);

	offset = ftello(stream);
	assert_int_equal(offset, 0);

	rc = fgetc(stream);
	assert_int_equal(rc, 'a');

	rc = ungetc('z', stream);
	assert_int_equal(rc, 'z');

	rc = fgetc(stream);
	assert_int_equal(rc, 'z');

	rc = fgetc(stream);
	assert_int_equal(rc, 'b');

	rc = getc(stream);
	assert_int_equal(rc, 'c');

	offset = ftello(stream);
	assert_int_equal(offset, 3);

	errno = 0;
	rewind(stream);
	assert_int_equal(errno, 0);

	offset = ftello(stream);
	assert_int_equal(offset, 0);

	/* This will also close fd */
	rc = fclose(stream);
	assert_int_equal(rc, 0);

	/* Streaming I/O testing */
	fd = openat(root, "stream_file", O_RDWR | O_EXCL, S_IWUSR | S_IRUSR);
	assert_return_code(fd, errno);
	stream = fdopen(fd, "w+");
	assert_non_null(stream);

	rc = getc(stream);
	assert_int_equal(rc, 'a');

	rc = ungetc('z', stream);
	assert_int_equal(rc, 'z');

	rc = getc(stream);
	assert_int_equal(rc, 'z');

	offset = ftello(stream);
	assert_int_equal(offset, 1);

	/* This will also close fd */
	rc = fclose(stream);
	assert_int_equal(rc, 0);

	/* Streaming I/O testing */
	fd = openat(root, "stream_file", O_RDWR | O_EXCL, S_IWUSR | S_IRUSR);
	assert_return_code(fd, errno);
	stream = fdopen(fd, "w+");
	assert_non_null(stream);

	/* now see to two before the end of file, this needs the filesize so will back-off */
	offset = fseeko(stream, -2, SEEK_END);
	assert_int_equal(offset, 0);

	offset = ftello(stream);
	assert_int_equal(offset, 8);

	/* This will also close fd */
	rc = fclose(stream);
	assert_int_equal(rc, 0);

	rc = unlinkat(root, "stream_file", 0);
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
	if (rc == -1 && errno == ENOTTY) {
		goto out;
	}
	assert_return_code(rc, errno);

	assert_int_equal(dur.uid, geteuid());
	assert_int_equal(dur.gid, getegid());

	/* Now do the same test but on the directory itself */
	rc = ioctl(root, DFUSE_IOCTL_DFUSE_USER, &dur);
	assert_return_code(rc, errno);

	assert_int_equal(dur.uid, geteuid());
	assert_int_equal(dur.gid, getegid());

out:

	rc = close(fd);
	assert_return_code(rc, errno);

	rc = unlinkat(root, "ioctl_file", 0);
	assert_return_code(rc, errno);

	rc = close(root);
	assert_return_code(rc, errno);
}

static bool
timespec_gt(struct timespec t1, struct timespec t2)
{
	if (t1.tv_sec == t2.tv_sec)
		return t1.tv_nsec > t2.tv_nsec;
	else
		return t1.tv_sec > t2.tv_sec;
}

#define FUSE_SUPER_MAGIC  0x65735546

void
do_mtime(void **state)
{
	struct stat     stbuf;
	struct timespec prev_ts;
	struct timespec now;
	struct timespec times[2];
	int             fd;
	int             rc;
	char            input_buf[] = "hello";
	struct statfs   fs;
	int             root        = open(test_dir, O_PATH | O_DIRECTORY);

	assert_return_code(root, errno);

	/* Open a file and sanity check the mtime */
	fd = openat(root, "mtime_file", O_RDWR | O_CREAT | O_EXCL, S_IWUSR | S_IRUSR);
	assert_return_code(fd, errno);

	rc = fstatfs(root, &fs);
	assert_return_code(fd, errno);

	rc = clock_gettime(CLOCK_REALTIME, &now);
	assert_return_code(fd, errno);
	rc = fstat(fd, &stbuf);
	assert_return_code(rc, errno);
	prev_ts.tv_sec  = stbuf.st_mtim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_mtim.tv_nsec;
	if (fs.f_type == FUSE_SUPER_MAGIC) {
		assert_true(timespec_gt(now, stbuf.st_mtim));
	} else {
		printf("Not comparing mtime\n");
		printf("%ld %ld\n", now.tv_sec, now.tv_nsec);
		printf("%ld %ld\n", stbuf.st_mtim.tv_sec, stbuf.st_mtim.tv_nsec);
	}

	/* Write to the file and verify mtime is newer */
	rc = write(fd, input_buf, sizeof(input_buf));
	assert_return_code(rc, errno);
	rc = fstat(fd, &stbuf);
	assert_return_code(rc, errno);

	if (fs.f_type == FUSE_SUPER_MAGIC) {
		assert_true(timespec_gt(stbuf.st_mtim, prev_ts));
	} else {
		printf("Not comparing mtime\n");
		printf("%ld %ld\n", stbuf.st_mtim.tv_sec, stbuf.st_mtim.tv_nsec);
		printf("%ld %ld\n", prev_ts.tv_sec, prev_ts.tv_nsec);
	}
	prev_ts.tv_sec  = stbuf.st_mtim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_mtim.tv_nsec;

	/* Truncate the file and verify mtime is newer */
	rc = ftruncate(fd, 0);
	assert_return_code(rc, errno);
	rc = fstat(fd, &stbuf);
	assert_return_code(rc, errno);
	if (fs.f_type == FUSE_SUPER_MAGIC) {
		assert_true(timespec_gt(stbuf.st_mtim, prev_ts));
	} else {
		printf("Not comparing mtime\n");
		printf("%ld %ld\n", stbuf.st_mtim.tv_sec, stbuf.st_mtim.tv_nsec);
		printf("%ld %ld\n", prev_ts.tv_sec, prev_ts.tv_nsec);
	}
	prev_ts.tv_sec  = stbuf.st_mtim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_mtim.tv_nsec;

	/* Set and verify mtime set in the past */
	times[0]         = now;
	times[1].tv_sec  = now.tv_sec - 10;
	times[1].tv_nsec = 20;
	rc               = futimens(fd, times);
	assert_return_code(fd, errno);
	rc = fstat(fd, &stbuf);
	assert_return_code(rc, errno);
	assert_int_equal(stbuf.st_mtim.tv_sec, times[1].tv_sec);
	assert_int_equal(stbuf.st_mtim.tv_nsec, times[1].tv_nsec);
	prev_ts.tv_sec  = stbuf.st_mtim.tv_sec;
	prev_ts.tv_nsec = stbuf.st_mtim.tv_nsec;

	/* Repeat the write test again */
	rc = write(fd, input_buf, sizeof(input_buf));
	assert_return_code(rc, errno);
	rc = fstat(fd, &stbuf);
	assert_return_code(rc, errno);
	assert_true(timespec_gt(stbuf.st_mtim, prev_ts));

	rc = close(fd);
	assert_return_code(rc, errno);

	rc = unlinkat(root, "mtime_file", 0);
	assert_return_code(rc, errno);

	rc = close(root);
	assert_return_code(rc, errno);
}

/*
 * Check readdir for issues.
 *
 * Create a directory
 * Populate it
 * Check the file count
 * Rewind the directory handle
 * Re-check the file count.
 *
 * In order for this test to be idempotent and because it takes time to create the files then
 * ignore errors about file exists when creating.
 */
void
do_directory(void **state)
{
	int            root;
	int            dfd;
	int            rc;
	int            i;
	DIR           *dirp;
	struct dirent *ent;
	long           pos;

	printf("Creating dir and files\n");
	root = open(test_dir, O_PATH | O_DIRECTORY);
	assert_return_code(root, errno);

	rc = mkdirat(root, "wide_dir", S_IWUSR | S_IRUSR | S_IXUSR);
	if (rc != 0 && errno != EEXIST)
		assert_return_code(rc, errno);

	dfd = openat(root, "wide_dir", O_RDONLY | O_DIRECTORY);
	assert_return_code(dfd, errno);

	for (i = 0; i < 100; i++) {
		char fname[17];
		int  fd;

		rc = snprintf(fname, 17, "file %d", i);
		assert_in_range(rc, 0, 16);

		fd = openat(dfd, fname, O_RDWR | O_CREAT, S_IWUSR | S_IRUSR);
		assert_return_code(fd, errno);
		rc = close(fd);
		assert_return_code(rc, errno);
	}

	printf("Checking file count\n");
	dirp = fdopendir(dfd);
	if (dirp == NULL)
		assert_return_code(-1, errno);

	pos = telldir(dirp);

	i     = 0;
	errno = 0;
	while ((ent = readdir(dirp)) != NULL) {
		i++;
	}
	if (errno != 0)
		assert_return_code(-1, errno);
	printf("File count is %d\n", i);
	assert_int_equal(i, 100);

	printf("Rewinding and rechecking file count\n");
	seekdir(dirp, pos);

	i     = 0;
	errno = 0;
	while ((ent = readdir(dirp)) != NULL) {
		i++;
	}
	if (errno != 0)
		assert_return_code(-1, errno);
	printf("File count is %d\n", i);
	assert_int_equal(i, 100);

	rc = close(dfd);
	assert_return_code(rc, errno);

	rc = close(root);
	assert_return_code(rc, errno);
}

static int
run_specified_tests(const char *tests, int *sub_tests, int sub_tests_size)
{
	int nr_failed = 0;

	if (strlen(tests) == 0)
		tests = all_tests;

	while (*tests != '\0') {
		switch (*tests) {
		case 'i':
			printf("\n\n=================");
			printf("dfuse IO tests");
			printf("=====================\n");
			const struct CMUnitTest io_tests[] = {
			    cmocka_unit_test(do_openat),
			    cmocka_unit_test(do_ioctl),
			};
			nr_failed += cmocka_run_group_tests(io_tests, NULL, NULL);
			break;
		case 's':
			printf("\n\n=================");
			printf("dfuse streaming tests");
			printf("=====================\n");
			const struct CMUnitTest stream_tests[] = {
			    cmocka_unit_test(do_stream),
			};
			nr_failed += cmocka_run_group_tests(stream_tests, NULL, NULL);
			break;

		case 'm':
			printf("\n\n=================");
			printf("dfuse metadata tests");
			printf("=====================\n");
			const struct CMUnitTest metadata_tests[] = {
			    cmocka_unit_test(do_mtime),
			};
			nr_failed += cmocka_run_group_tests(metadata_tests, NULL, NULL);
			break;

		case 'd':
			printf("\n\n=================");
			printf("dfuse directory tests");
			printf("=====================\n");
			const struct CMUnitTest readdir_tests[] = {
			    cmocka_unit_test(do_directory),
			};
			nr_failed += cmocka_run_group_tests(readdir_tests, NULL, NULL);
			break;

		default:
			assert_true(0);
		}

		tests++;
	}

	return nr_failed;
}

int
main(int argc, char **argv)
{
	char                 tests[64] = {};
	int                  ntests    = 0;
	int                  nr_failed = 0;
	int                  opt = 0, index = 0;

	static struct option long_options[] = {{"test-dir", required_argument, NULL, 'M'},
					       {"all", no_argument, NULL, 'a'},
					       {"io", no_argument, NULL, 'i'},
					       {"stream", no_argument, NULL, 's'},
					       {"metadata", no_argument, NULL, 'm'},
					       {"directory", no_argument, NULL, 'd'},
					       {NULL, 0, NULL, 0}};

	while ((opt = getopt_long(argc, argv, "aM:imsd", long_options, &index)) != -1) {
		if (strchr(all_tests, opt) != NULL) {
			tests[ntests] = opt;
			ntests++;
			continue;
		}
		switch (opt) {
		case 'a':
			break;
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
		printf("-M|--test-dir option required\n");
		return 1;
	}

	nr_failed = run_specified_tests(tests, NULL, 0);

	print_message("\n============ Summary %s\n", __FILE__);
	if (nr_failed == 0)
		print_message("OK - NO TEST FAILURES\n");
	else
		print_message("ERROR, %i TEST(S) FAILED\n", nr_failed);

	return nr_failed;
}
