/**
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <CUnit/Basic.h>
#define D_LOGFAC DD_FAC(il)
#include "dfuse_log.h"
#include "dfuse_ioctl.h"
#include "ioil_api.h"

/* This test isn't presently enabled.  If mount_dir is NULL, it causes compiler
 * warning in sanity(...) with some newer compilers
 */
static char *mount_dir = "/tmp";
static uint64_t max_read_size = 4096;
static uint64_t max_iov_read_size = 4096;

#define BUF_SIZE 4096

char big_string[BUF_SIZE];

static void do_write_tests(int fd, char *buf, size_t len)
{
	struct iovec iov[2];
	ssize_t bytes;
	off_t offset;
	int rc;

	bytes = write(fd, buf, len);
	printf("Wrote %zd bytes, expected %zu\n", bytes, len);
	CU_ASSERT_EQUAL(bytes, len);

	offset = lseek(fd, 0, SEEK_CUR);
	printf("Seek offset is %zd, expected %zu\n", offset, len);
	CU_ASSERT_EQUAL(offset, len);

	bytes = pwrite(fd, buf, len, len);
	printf("Wrote %zd bytes, expected %zu\n", bytes, len);
	CU_ASSERT_EQUAL(bytes, len);

	offset = lseek(fd, 0, SEEK_CUR);
	printf("Seek offset is %zd, expected %zu\n", offset, len);
	CU_ASSERT_EQUAL(offset, len);

	offset = lseek(fd, len, SEEK_CUR);
	printf("Seek offset is %zd, expected %zu\n", offset, len * 2);
	CU_ASSERT_EQUAL(offset, len * 2);

	iov[0].iov_len = len;
	iov[0].iov_base = buf;
	iov[1].iov_len = len;
	iov[1].iov_base = buf;

	bytes = writev(fd, iov, 2);
	printf("Wrote %zd bytes, expected %zu\n", bytes, len * 2);
	CU_ASSERT_EQUAL(bytes, len * 2);

	offset = lseek(fd, 0, SEEK_END);
	printf("Seek offset is %zd, expected %zu\n", offset, len * 4);
	CU_ASSERT_EQUAL(offset, len * 4);

	memset(big_string, 'a', BUF_SIZE - 1);
	big_string[BUF_SIZE - 1] = 0;
	bytes = write(fd, big_string, BUF_SIZE);

	rc = close(fd);
	printf("Closed file, rc = %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);
}

static void do_read_tests(const char *fname, size_t len)
{
	char *buf;
	char buf2[len + 1];
	struct iovec iov[2];
	ssize_t bytes;
	off_t offset;
	int pos;
	int fd;
	int rc;

	buf = calloc(2, BUF_SIZE);
	CU_ASSERT_PTR_NOT_NULL(buf);

	fd = open(fname, O_RDONLY);
	printf("Opened %s, fd = %d\n", fname, fd);
	CU_ASSERT_NOT_EQUAL_FATAL(fd, -1);

	bytes = read(fd, buf, BUF_SIZE * 2);
	printf("Read %zd bytes, expected %zu\n", bytes, BUF_SIZE + (len * 4));
	CU_ASSERT_EQUAL(bytes, BUF_SIZE + (len * 4));

	offset = lseek(fd, 0, SEEK_CUR);
	printf("Seek offset is %zd, expected %zu\n", offset,
	       BUF_SIZE + (len * 4));
	CU_ASSERT_EQUAL(offset, BUF_SIZE + len * 4);

	pos = 0;
	while (pos < (len * 4)) {
		CU_ASSERT_NSTRING_EQUAL(fname, buf + pos, len);
		pos += len;
	}
	CU_ASSERT_NSTRING_EQUAL(big_string, buf + pos, BUF_SIZE);

	offset = lseek(fd, 0, SEEK_SET);
	printf("Seek offset is %zd, expected 0\n", offset);
	CU_ASSERT_EQUAL(offset, 0);

	memset(buf, 0, BUF_SIZE * 2);

	bytes = pread(fd, buf, len, len);
	printf("Read %zd bytes, expected %zu\n", bytes, len);
	CU_ASSERT_EQUAL(bytes, len);

	CU_ASSERT_STRING_EQUAL(fname, buf);

	offset = lseek(fd, 0, SEEK_CUR);
	printf("Seek offset is %zd, expected 0\n", offset);
	CU_ASSERT_EQUAL(offset, 0);

	memset(buf, 0, BUF_SIZE * 2);

	iov[0].iov_len = len;
	iov[0].iov_base = buf2;
	iov[1].iov_len = len;
	iov[1].iov_base = buf;

	bytes = readv(fd, iov, 2);
	printf("Read %zd bytes, expected %zu\n", bytes, len * 2);
	CU_ASSERT_EQUAL(bytes, len * 2);

	CU_ASSERT_STRING_EQUAL(fname, buf);
	CU_ASSERT_STRING_EQUAL(fname, buf2);

	free(buf);

	rc = close(fd);
	printf("Closed file, rc = %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);
}

#define CU_ASSERT_GOTO(cond, target)  \
	do {                          \
		CU_ASSERT(cond);      \
		if (!(cond))          \
			goto target;  \
	} while (0)

static void do_large_read(const char *fname, const char *expected,
			  char *buf, size_t size)
{
	ssize_t bytes;
	int fd;
	int rc;

	memset(buf, 0, size);
	fd = open(fname, O_RDONLY);
	CU_ASSERT_NOT_EQUAL(fd, -1);
	if (fd == -1)
		return;
	bytes = read(fd, buf, size);
	CU_ASSERT_EQUAL(bytes, size);
	CU_ASSERT(memcmp(expected, buf, size) == 0);
	rc = close(fd);
	CU_ASSERT_EQUAL(rc, 0);
}

static bool do_large_write(const char *fname, const char *buf, size_t len)
{
	ssize_t bytes;
	int fd;

	fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	CU_ASSERT_GOTO(fd != -1, done_err);
	bytes = write(fd, buf, len);
	CU_ASSERT_EQUAL(bytes, len);
	close(fd);

	return true;
done_err:
	return false;
}

static void do_large_io_test(const char *fname, size_t len)
{
	char *buf;
	char *buf2 = NULL;
	size_t test1_size = max_read_size * 2;
	size_t test2_size = test1_size + max_iov_read_size;
	size_t test3_size = test2_size + max_iov_read_size;
	size_t buf_size = test3_size;

	buf = malloc(buf_size);
	CU_ASSERT_GOTO(buf != NULL, done);

	buf2 = malloc(buf_size);
	CU_ASSERT_GOTO(buf2 != NULL, done);

	memset(buf, 'b', buf_size);

	CU_ASSERT_GOTO(do_large_write(fname, buf, test1_size), done);
	do_large_read(fname, buf, buf2, test1_size);
	CU_ASSERT_GOTO(do_large_write(fname, buf, test2_size), done);
	do_large_read(fname, buf, buf2, test2_size);
	CU_ASSERT_GOTO(do_large_write(fname, buf, test3_size), done);
	do_large_read(fname, buf, buf2, test3_size);
	do_large_read(fname, buf, buf2, test1_size);
	do_large_read(fname, buf, buf2, test2_size);
done:
	free(buf);
	free(buf2);
}

static void do_misc_tests(const char *fname, size_t len)
{
	struct stat stat_info;
	void *address;
	FILE *fp = NULL;
	size_t items;
	int rc;
	int fd;
	int new_fd;
	int status;

	rc = stat(fname, &stat_info);
	CU_ASSERT_EQUAL_FATAL(rc, 0);
	CU_ASSERT_NOT_EQUAL_FATAL(stat_info.st_size, 0);
	fd = open(fname, O_RDWR);
	printf("Opened %s, fd = %d\n", fname, fd);
	CU_ASSERT_NOT_EQUAL(fd, -1);

	status = dfuse_get_bypass_status(fd);
	CU_ASSERT_EQUAL(status, DFUSE_IO_BYPASS);

	new_fd = dup(fd);
	printf("Duped %d, new_fd = %d\n", fd, new_fd);
	CU_ASSERT_NOT_EQUAL(new_fd, -1);

	status = dfuse_get_bypass_status(new_fd);
	CU_ASSERT_EQUAL(status, DFUSE_IO_BYPASS);

	rc = close(new_fd);
	printf("close returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	status = dfuse_get_bypass_status(fd);
	CU_ASSERT_EQUAL(status, DFUSE_IO_BYPASS);

	new_fd = dup2(fd, 80);
	printf("dup2(%d, 80) returned %d\n", fd, new_fd);
	CU_ASSERT_EQUAL(new_fd, 80);

	status = dfuse_get_bypass_status(new_fd);
	CU_ASSERT_EQUAL(status, DFUSE_IO_BYPASS);

	rc = close(new_fd);
	printf("close returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	new_fd = fcntl(fd, F_DUPFD, 80);
	printf("fcntl(%d, F_DUPFD, 80) returned %d\n", fd, new_fd);
	CU_ASSERT(new_fd >= 80);

	status = dfuse_get_bypass_status(new_fd);
	printf("status = %d\n", status);
	CU_ASSERT_EQUAL(status, DFUSE_IO_BYPASS);

	rc = close(new_fd);
	printf("close returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	new_fd = fcntl(fd, F_DUPFD_CLOEXEC, 90);
	printf("fcntl(%d, F_DUPFD, 90) returned %d\n", fd, new_fd);
	CU_ASSERT(new_fd >= 90);

	status = dfuse_get_bypass_status(new_fd);
	CU_ASSERT_EQUAL(status, DFUSE_IO_BYPASS);

	rc = close(new_fd);
	printf("close returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	rc = fsync(fd);
	printf("fsync returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	status = dfuse_get_bypass_status(fd);
	CU_ASSERT_EQUAL(status, DFUSE_IO_BYPASS);

	rc = fdatasync(fd);
	printf("fdatasync returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	status = dfuse_get_bypass_status(fd);
	CU_ASSERT_EQUAL(status, DFUSE_IO_BYPASS);

	new_fd = dup(fd);
	printf("Duped %d, new_fd = %d\n", fd, new_fd);
	CU_ASSERT_NOT_EQUAL(new_fd, -1);

	status = dfuse_get_bypass_status(new_fd);
	CU_ASSERT_EQUAL(status, DFUSE_IO_BYPASS);

	address = mmap(NULL, BUF_SIZE, PROT_READ | PROT_WRITE,
		       MAP_SHARED, fd, 0);

	printf("mmap returned %p\n", address);
	if (address == MAP_FAILED && errno == ENODEV) {
		printf("mmap not supported on file system\n");
		goto skip_mmap;
	}
	CU_ASSERT_PTR_NOT_EQUAL_FATAL(address, MAP_FAILED);

	memset(address, '@', BUF_SIZE);

	rc = munmap(address, BUF_SIZE);
	printf("munmap returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	status = dfuse_get_bypass_status(fd);
	CU_ASSERT_EQUAL(status, DFUSE_IO_DIS_MMAP);

	/* dup'd descriptor should also change status */
	status = dfuse_get_bypass_status(new_fd);
	CU_ASSERT_EQUAL(status, DFUSE_IO_DIS_MMAP);
skip_mmap:
	rc = close(fd);
	printf("close returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	rc = close(new_fd);
	printf("close returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	fd = open(fname, O_RDWR);
	printf("Opened %s, fd = %d\n", fname, fd);
	CU_ASSERT_NOT_EQUAL(fd, -1);

	status = dfuse_get_bypass_status(fd);
	CU_ASSERT_EQUAL(status, DFUSE_IO_BYPASS);

	fp = fdopen(fd, "r");
	printf("fdopen returned %p\n", fp);
	CU_ASSERT_PTR_NOT_EQUAL(fp, NULL);

	status = dfuse_get_bypass_status(fd);
	CU_ASSERT_EQUAL(status, DFUSE_IO_DIS_STREAM);

	if (fp != NULL) {
		char buf[9] = {0};

		items = fread(buf, 1, 8, fp);
		printf("Read %zd items, expected 8\n", items);
		CU_ASSERT_EQUAL(items, 8);
		CU_ASSERT_STRING_EQUAL(buf, "@@@@@@@@");
	}
	if (fp != NULL) {
		rc = fclose(fp);
		printf("fclose returned %d\n", rc);
	} else {
		rc = close(new_fd);
		printf("close returned %d\n", rc);
	}
	CU_ASSERT_EQUAL(rc, 0);

	fd = open(fname, O_RDWR);
	printf("Opened %s, fd = %d\n", fname, fd);
	CU_ASSERT_NOT_EQUAL(fd, -1);

	status = dfuse_get_bypass_status(fd);
	CU_ASSERT_EQUAL(status, DFUSE_IO_BYPASS);

	rc = fcntl(fd, F_SETFL, O_APPEND);
	printf("fcntl F_SETFL returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	status = dfuse_get_bypass_status(fd);
	CU_ASSERT_EQUAL(status, DFUSE_IO_DIS_FCNTL);

	rc = fcntl(fd, F_GETFL);
	printf("fcntl F_GETFL returned %d\n", rc);
	CU_ASSERT(rc & O_APPEND);

	rc = close(fd);
	printf("close returned %d\n", rc);
	CU_ASSERT_EQUAL(rc, 0);

	status = dfuse_get_bypass_status(0);
	CU_ASSERT_EQUAL(status, DFUSE_IO_EXTERNAL);

	status = dfuse_get_bypass_status(1);
	CU_ASSERT_EQUAL(status, DFUSE_IO_EXTERNAL);

	status = dfuse_get_bypass_status(2);
	CU_ASSERT_EQUAL(status, DFUSE_IO_EXTERNAL);
}

/* Simple sanity test to ensure low-level POSIX APIs work */
void sanity(void)
{
	char *buf;
	size_t len;
	int fd;

	fflush(stdout);
	len = asprintf(&buf, "%s/sanity", mount_dir);
	CU_ASSERT_NOT_EQUAL_FATAL(len, -1);
	CU_ASSERT_PTR_NOT_NULL(buf);

	unlink(buf);
	fd = open(buf, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	CU_ASSERT_NOT_EQUAL_FATAL(fd, -1);

	do_write_tests(fd, buf, len);
	do_read_tests(buf, len);
	do_misc_tests(buf, len);
	do_large_io_test(buf, len);
	free(buf);
}

int main(int argc, char **argv)
{
	int failures;
	CU_pSuite pSuite = NULL;

	if (CU_initialize_registry() != CUE_SUCCESS) {
		printf("CU_initialize_registry() failed\n");
		return CU_get_error();
	}

	pSuite = CU_add_suite("IO interception library test",
			      NULL, NULL);

	if (!pSuite) {
		CU_cleanup_registry();
		printf("CU_add_suite() failed\n");
		return CU_get_error();
	}

	if (!CU_add_test(pSuite, "libioil sanity test", sanity)) {
		CU_cleanup_registry();
		printf("CU_add_test() failed\n");
		return CU_get_error();
	}

	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();
	failures = CU_get_number_of_failures();
	CU_cleanup_registry();

	return failures;
}
