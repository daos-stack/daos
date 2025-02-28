/**
 * (C) Copyright 2021-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/file.h>

#include <dfuse_ioctl.h>

/* Tests can be run by specifying the appropriate argument for a test or all will be run if no test
 * is specified.
 */
static const char *all_tests = "ismdlkfec";

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
	print_message("dfuse_test -l|--lowfd\n");
	print_message("dfuse_test -k|--flock\n");
	print_message("dfuse_test -f|--mmap\n");
	print_message("dfuse_test -e|--exec\n");
	/* verifyenv is only run by exec test. Should not be executed directly */
	/* print_message("dfuse_test    --verifyenv\n");                       */
	print_message("dfuse_test -c|--cache\n");
	print_message("Default <dfuse_test> runs all tests\n=============\n");
	print_message("\n=============================\n");
}

char *test_dir;
void
do_openat(void **state)
{
	struct stat  stbuf0;
	struct stat  stbuf;
	struct statx stxbuf;
	int          fd;
	int          rc;
	char         output_buf[10];
	char         input_buf[] = "hello";
	off_t        offset;
	int          root = open(test_dir, O_PATH | O_DIRECTORY);

	assert_return_code(root, errno);

	/* Test corner case: empty path in stat() and its variants. */
	rc = stat("", &stbuf);
	assert_int_equal(rc, -1);
	assert_int_equal(errno, ENOENT);

	rc = lstat("", &stbuf);
	assert_int_equal(rc, -1);
	assert_int_equal(errno, ENOENT);

	rc = fstatat(root, "", &stbuf, 0);
	assert_int_equal(rc, -1);
	assert_int_equal(errno, ENOENT);

	rc = statx(root, "", 0, 0, &stxbuf);
	assert_int_equal(rc, -1);
	assert_int_equal(errno, ENOENT);

	fd = openat(root, "openat_file", O_RDWR | O_CREAT | O_EXCL, S_IWUSR | S_IRUSR);
	assert_return_code(fd, errno);

	/* This will write six bytes, including a \0 terminator */
	rc = write(fd, input_buf, sizeof(input_buf));
	assert_return_code(rc, errno);

	/* test fdatasync() */
	rc = fdatasync(fd);
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

	/* cornercase: fd for a regular file is passed into fstatat(). Path is empty. */
	rc = fstatat(fd, "", &stbuf0, AT_EMPTY_PATH);
	assert_return_code(rc, errno);
	assert_int_equal(stbuf.st_size, stbuf0.st_size);

	/* expected to fail */
	rc = fstatat(fd, "", &stbuf0, 0);
	assert_int_equal(rc, -1);
	assert_int_equal(errno, ENOENT);

	/* expected to fail */
	rc = fstatat(fd, "entry", &stbuf0, 0);
	assert_int_equal(rc, -1);
	assert_int_equal(errno, ENOTDIR);

	rc = close(fd);
	assert_return_code(rc, errno);

	rc = unlinkat(root, "openat_file", 0);
	assert_return_code(rc, errno);

	rc = close(root);
	assert_return_code(rc, errno);
}

extern int __open(const char *pathname, int flags, ...);
void
do_open(void **state)
{
	int  fd;
	int  rc;
	int  len;
	char path[512];

	len = snprintf(path, sizeof(path) - 1, "%s/open_file", test_dir);
	assert_true(len < (sizeof(path) - 1));

	/* Test O_CREAT with open but without mode. __open() is called to workaround
	 * "-D_FORTIFY_SOURCE=3". Normally mode is required when O_CREAT is in flag.
	 * libc seems supporting it although the permission could be undefined.
	 */
	fd = __open(path, O_RDWR | O_CREAT | O_EXCL);
	assert_return_code(fd, errno);

	rc = close(fd);
	assert_return_code(rc, errno);

	rc = unlink(path);
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

	fd = openat(root, "stream_file", O_RDWR | O_EXCL, S_IWUSR | S_IRUSR);
	assert_return_code(fd, errno);
	stream = fdopen(fd, "w+");
	assert_non_null(stream);

	rc = fputs("Hello World!\n", stream);
	assert_return_code(rc, errno);
	fclose(stream);

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

void
do_readv_writev(void **state)
{
	int          fd;
	int          rc;
	int          root = open(test_dir, O_DIRECTORY);
	char        *str0 = "hello ";
	char        *str1 = "world\n";
	struct iovec iov[2];
	ssize_t      bytes_written;
	ssize_t      bytes_read;
	char         buf_read[16];
	off_t        off;

	assert_return_code(root, errno);

	/* readv/writev testing */
	fd = openat(root, "readv_writev_file", O_RDWR | O_CREAT, S_IWUSR | S_IRUSR);
	assert_return_code(fd, errno);

	iov[0].iov_base = str0;
	iov[0].iov_len  = strlen(str0);
	iov[1].iov_base = str1;
	iov[1].iov_len  = strlen(str1);

	bytes_written = writev(fd, iov, 2);
	assert_int_equal(bytes_written, 12);

	off = lseek(fd, 0, SEEK_SET);
	assert_true(off == 0);

	iov[0].iov_base = buf_read;
	iov[1].iov_base = buf_read + strlen(str0);
	bytes_read      = readv(fd, iov, 2);
	assert_int_equal(bytes_read, 12);
	assert_true(strncmp(buf_read, "hello world\n", 12) == 0);

	rc = close(fd);
	assert_return_code(rc, errno);

	rc = unlinkat(root, "readv_writev_file", 0);
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

static int
get_dir_num_entry(DIR *dirp)
{
	int            num_entry = 0;
	struct dirent *ent;

	while ((ent = readdir(dirp)) != NULL) {
		num_entry++;
	}
	return num_entry;
}

/*
 * Check readdir for issues.
 *
 * Create a directory
 * Populate it
 * Test scandirat
 * Check the file count
 * Rewind the directory handle
 * Re-check the file count.
 * seekdir, then verify the number of entries left
 *
 * In order for this test to be idempotent and because it takes time to create the files then
 * ignore errors about file exists when creating.
 */
void
do_directory(void **state)
{
	int             root;
	int             dfd;
	int             rc;
	int             i;
	DIR            *dirp;
	struct dirent **namelist;
	long            pos;
	int             entry_count = 100;

	printf("Creating dir and files\n");
	root = open(test_dir, O_PATH | O_DIRECTORY);
	assert_return_code(root, errno);

	rc = mkdirat(root, "wide_dir", S_IWUSR | S_IRUSR | S_IXUSR);
	if (rc != 0 && errno != EEXIST)
		assert_return_code(rc, errno);

	dfd = openat(root, "wide_dir", O_RDONLY | O_DIRECTORY);
	assert_return_code(dfd, errno);

	for (i = 0; i < entry_count; i++) {
		char fname[17];
		int  fd;

		rc = snprintf(fname, 17, "file_%02d", i);
		assert_in_range(rc, 0, 16);

		fd = openat(dfd, fname, O_RDWR | O_CREAT, S_IWUSR | S_IRUSR);
		assert_return_code(fd, errno);
		rc = close(fd);
		assert_return_code(rc, errno);
	}

	rc = scandirat(dfd, ".", &namelist, NULL, alphasort);
	if (strcmp(namelist[0]->d_name, ".") == 0) {
		entry_count += 2;
	} else {
		assert_true(strcmp(namelist[0]->d_name, "file_00") == 0);
	}
	assert_int_equal(rc, entry_count);
	assert_true(strcmp(namelist[rc - 1]->d_name, "file_99") == 0);

	/* free namelist */
	while (rc--) {
		free(namelist[rc]);
	}
	free(namelist);

	printf("Checking file count\n");
	dirp = fdopendir(dfd);
	if (dirp == NULL)
		assert_return_code(-1, errno);

	pos = telldir(dirp);

	errno = 0;
	rc    = get_dir_num_entry(dirp);
	if (errno != 0)
		assert_return_code(-1, errno);
	printf("File count is %d\n", rc);
	assert_int_equal(rc, entry_count);

	printf("Rewinding and rechecking file count\n");
	seekdir(dirp, pos);

	errno = 0;
	rc    = get_dir_num_entry(dirp);
	if (errno != 0)
		assert_return_code(-1, errno);
	printf("File count is %d\n", rc);
	assert_int_equal(rc, entry_count);

	long           positions[entry_count];
	struct dirent *ent;
	i = 0;
	rewinddir(dirp);
	positions[i] = telldir(dirp);
	i++;

	while ((ent = readdir(dirp)) != NULL) {
		positions[i] = telldir(dirp);
		assert_true(i <= entry_count);
		i++;
	}

	for (i = 0; i < entry_count; i++) {
		rewinddir(dirp);
		seekdir(dirp, positions[i]);
		assert_int_equal(get_dir_num_entry(dirp), entry_count - i);
	}
	for (i = 0; i < entry_count; i++) {
		rewinddir(dirp);
		readdir(dirp);
		seekdir(dirp, positions[i]);
		assert_int_equal(get_dir_num_entry(dirp), entry_count - i);
	}

	rc = close(dfd);
	assert_return_code(rc, errno);

	rc = close(root);
	assert_return_code(rc, errno);
}

void
do_mmap(void **state)
{
	int   root;
	int   fd;
	int   rc;
	void *addr;

	root = open(test_dir, O_PATH | O_DIRECTORY);
	assert_return_code(root, errno);

	/* Always unlink the file but do not check for errors.  If running the test manually the
	 * file might pre-exist and affect the behavior.
	 */
	unlinkat(root, "file", 0);

	fd = openat(root, "file", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	assert_return_code(root, errno);

	rc = ftruncate(fd, 1024 * 1024);
	assert_return_code(rc, errno);

	addr = mmap(NULL, 1024 * 1024, PROT_WRITE, MAP_PRIVATE, fd, 0);
	assert_ptr_not_equal(addr, MAP_FAILED);

	printf("Mapped private to %p\n", addr);

	memset(addr, 0, 1024 * 1024);

	rc = munmap(addr, 1024 * 1024);
	assert_return_code(rc, errno);

	addr = mmap(NULL, 1024 * 1024, PROT_READ, MAP_SHARED, fd, 0);
	assert_ptr_not_equal(addr, MAP_FAILED);

	printf("Mapped shared to %p\n", addr);

	rc = munmap(addr, 1024 * 1024);
	assert_return_code(rc, errno);

	rc = close(fd);
	assert_return_code(rc, errno);

	rc = unlinkat(root, "file", 0);
	assert_return_code(rc, errno);

	rc = close(root);
	assert_return_code(rc, errno);
}

#define MIN_DAOS_FD 10
/*
 * Check whether daos network context uses low fds 0~9.
 */
void
do_lowfd(void **state)
{
	int   fd;
	int   rc;
	int   i;
	char *env_ldpreload;
	char  fd_path[64];
	char *path;

	env_ldpreload = getenv("LD_PRELOAD");
	if (env_ldpreload == NULL)
		return;

	if (strstr(env_ldpreload, "libpil4dfs.so") == NULL)
		/* libioil cannot pass this test since low fds are only temporarily blocked */
		return;

	/* first time access a dir on DFS mount to trigger daos_init() */
	fd = open(test_dir, O_PATH | O_DIRECTORY);
	assert_return_code(fd, errno);

	rc = close(fd);
	assert_return_code(rc, errno);

	/* open the root dir and print fd */
	fd = open("/", O_PATH | O_DIRECTORY);
	assert_return_code(fd, errno);
	printf("fd = %d\n", fd);
	rc = close(fd);
	assert_return_code(rc, errno);
	assert_true(fd >= MIN_DAOS_FD);

	/* now check whether daos uses low fds */
	path = malloc(PATH_MAX);
	assert_non_null(path);
	for (i = 0; i < MIN_DAOS_FD; i++) {
		snprintf(fd_path, sizeof(fd_path) - 1, "/proc/self/fd/%d", i);
		rc = readlink(fd_path, path, PATH_MAX - 1);
		assert_true(rc > 0);
		path[rc] = 0;
		assert_true(strstr(path, "socket:") == NULL);
		assert_true(strstr(path, "anon_inode:") == NULL);
	}
	free(path);
}

/*
 * Verify the behavior of flock() and fcntl().
 */
void
do_flock(void **state)
{
	int          len;
	int          fd;
	int          rc;
	char         path[512];
	struct flock fl;

	len = snprintf(path, sizeof(path) - 1, "%s/flock_file", test_dir);
	assert_true(len < (sizeof(path) - 1));

	fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0640);
	assert_return_code(fd, errno);

	rc = flock(fd, LOCK_EX);
	assert_true(rc == -1);
	assert_true(errno == ENOTSUP);

	fl.l_type   = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start  = 0;
	fl.l_len    = 0;
	fl.l_pid    = getpid();
	rc          = fcntl(fd, F_SETLKW, &fl);
	assert_true(rc == -1);
	assert_true(errno == ENOTSUP);

	rc = close(fd);
	assert_return_code(rc, errno);

	rc = unlink(path);
	assert_return_code(rc, errno);
}

#define ERR_ENV_UNSET (2)

void
verify_pil4dfs_env()
{
	char *p;

	p = getenv("LD_PRELOAD");
	if (!p) {
		printf("Error: LD_PRELOAD is unset.\n");
		goto err;
	}

	p = getenv("D_IL_REPORT");
	if (!p) {
		printf("Error: D_IL_REPORT is unset.\n");
		goto err;
	}

	p = getenv("D_IL_MOUNT_POINT");
	if (!p) {
		printf("Error: D_IL_MOUNT_POINT is unset.\n");
		goto err;
	}

	p = getenv("D_IL_POOL");
	if (!p) {
		printf("Error: D_IL_POOL is unset.\n");
		goto err;
	}

	p = getenv("D_IL_CONTAINER");
	if (!p) {
		printf("Error: D_IL_CONTAINER is unset.\n");
		goto err;
	}

	p = getenv("D_IL_MAX_EQ");
	if (!p) {
		printf("Error: D_IL_MAX_EQ is unset.\n");
		goto err;
	}

	exit(0);

err:
	exit(ERR_ENV_UNSET);
}

/*
 * fork() to create a child process and call exec() to run this test itself.
 * This test is only used for libpil4dfs.so.
 */
void
do_exec(void **state)
{
	pid_t pid;
	int   status, rc;
	char *envp[1] = {NULL};
	char *argv[3] = {"dfuse_test", "--verifyenv", NULL};
	char *exe_path;
	char *env_ldpreload;

	env_ldpreload = getenv("LD_PRELOAD");
	if (env_ldpreload == NULL)
		return;
	if (strstr(env_ldpreload, "libpil4dfs.so") == NULL)
		return;

	printf("Found libpil4dfs.so.\n");
	exe_path = malloc(PATH_MAX);
	assert_non_null(exe_path);
	rc = readlink("/proc/self/exe", exe_path, PATH_MAX - 1);
	assert_true(rc > 0);
	exe_path[rc] = 0;

	/* fork and call execve() */
	printf("Testing execve().\n");
	pid = fork();
	if (pid == 0)
		execve(exe_path, argv, envp);
	waitpid(pid, &status, 0);
	if (WIFEXITED(status))
		assert_int_equal(WEXITSTATUS(status), 0);

	/* fork and call execv() */
	printf("Testing execv().\n");
	pid = fork();
	if (pid == 0)
		execv(exe_path, argv);
	waitpid(pid, &status, 0);
	if (WIFEXITED(status))
		assert_int_equal(WEXITSTATUS(status), 0);

	/* fork and call execvp() */
	printf("Testing execvp().\n");
	pid = fork();
	if (pid == 0)
		execvp(exe_path, argv);
	waitpid(pid, &status, 0);
	if (WIFEXITED(status))
		assert_int_equal(WEXITSTATUS(status), 0);

	/* fork and call execvpe() */
	printf("Testing execvpe().\n");
	pid = fork();
	if (pid == 0)
		execvpe(exe_path, argv, envp);
	waitpid(pid, &status, 0);
	if (WIFEXITED(status))
		assert_int_equal(WEXITSTATUS(status), 0);
}

/*
 * Check the consistency of dir caching in interception library.
 *
 * Create a directory
 * Create a file under this directory
 * Remove the file
 * Remove the directory
 * Create this directory again
 * Create the same file again
 * Create a child process with fork and executable cat to show the content of the file
 *
 * Failure to pass means dir caching has inconsistency
 */
void
do_cachingcheck(void **state)
{
	int   fd;
	int   rc;
	int   pid;
	char  dir_name[256];
	char  file_name[256];
	char  exe_name[] = "/usr/bin/cat";
	char *argv[3];

	snprintf(dir_name, 256, "%s/%s", test_dir, "test_dir");
	snprintf(file_name, 256, "%s/%s/%s", test_dir, "test_dir", "test_file");

	rc = mkdir(dir_name, 0740);
	assert_return_code(rc, errno);

	fd = open(file_name, O_WRONLY | O_TRUNC | O_CREAT, 0640);
	assert_return_code(fd, errno);
	rc = close(fd);
	assert_return_code(rc, errno);

	rc = unlink(file_name);
	assert_return_code(rc, errno);

	rc = rmdir(dir_name);
	assert_return_code(rc, errno);

	rc = mkdir(dir_name, 0740);
	assert_return_code(rc, errno);

	fd = open(file_name, O_WRONLY | O_TRUNC | O_CREAT, 0640);
	assert_return_code(fd, errno);
	rc = close(fd);
	assert_return_code(rc, errno);

	/* fork() to create a child process and exec() to run "cat test_file" */
	pid = fork();
	if (pid == 0) {
		argv[0] = exe_name;
		argv[1] = file_name;
		argv[2] = NULL;
		/* Run command "cat test_file" in a new process */
		execv(exe_name, argv);
	}
	rc = unlink(file_name);
	assert_return_code(rc, errno);

	rc = rmdir(dir_name);
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
			    cmocka_unit_test(do_open),
			    cmocka_unit_test(do_ioctl),
			    cmocka_unit_test(do_readv_writev),
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

		case 'l':
			printf("\n\n=================");
			printf("dfuse low fd tests");
			printf("=====================\n");
			const struct CMUnitTest lowfd_tests[] = {
			    cmocka_unit_test(do_lowfd),
			};
			nr_failed += cmocka_run_group_tests(lowfd_tests, NULL, NULL);
			break;

		case 'k':
			printf("\n\n=================");
			printf("dfuse flock tests");
			printf("=====================\n");
			const struct CMUnitTest flock_tests[] = {
			    cmocka_unit_test(do_flock),
			};
			nr_failed += cmocka_run_group_tests(flock_tests, NULL, NULL);
			break;

		case 'f': {
			const struct CMUnitTest mmap_tests[] = {
			    cmocka_unit_test(do_mmap),
			};
			printf("\n\n=================");
			printf("dfuse mmap tests");
			printf("=====================\n");
			nr_failed += cmocka_run_group_tests(mmap_tests, NULL, NULL);
			break;
		}

		case 'e':
			printf("\n\n=================");
			printf("dfuse exec tests");
			printf("=====================\n");
			const struct CMUnitTest exec_tests[] = {
			    cmocka_unit_test(do_exec),
			};
			nr_failed += cmocka_run_group_tests(exec_tests, NULL, NULL);
			break;

		case 'c':
			printf("\n\n=================");
			printf("dfuse dir cache consistency check");
			printf("=====================\n");
			const struct CMUnitTest cache_tests[] = {
			    cmocka_unit_test(do_cachingcheck),
			};
			nr_failed += cmocka_run_group_tests(cache_tests, NULL, NULL);
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
					       {"mmap", no_argument, NULL, 'f'},
					       {"lowfd", no_argument, NULL, 'l'},
					       {"flock", no_argument, NULL, 'k'},
					       {"exec", no_argument, NULL, 'e'},
					       {"verifyenv", no_argument, NULL, 't'},
					       {"cache", no_argument, NULL, 'c'},
					       {NULL, 0, NULL, 0}};

	while ((opt = getopt_long(argc, argv, "aM:imsdlkfetc", long_options, &index)) != -1) {
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
		case 't':
			/* only run by child process */
			verify_pil4dfs_env();
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
