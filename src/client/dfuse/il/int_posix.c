/**
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(il)
#include <stdarg.h>
#include <inttypes.h>
#include <libgen.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <sys/time.h>
#include <sys/resource.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <string.h>

#include <gurt/list.h>
#include <gurt/atomic.h>
#include "intercept.h"

#include "ioil.h"

FOREACH_INTERCEPT(IOIL_FORWARD_DECL)

static pthread_once_t init_links_flag = PTHREAD_ONCE_INIT;

#define delay() (void)0

/* This is also called from dfuse_fopen()
 * Calling anything that can open files in this function can cause deadlock
 * so just do what's necessary for setup, and then return.
 */
static void
init_links(void)
{
	FOREACH_INTERCEPT(IOIL_FORWARD_MAP_OR_FAIL);
}

static __attribute__((constructor)) void
ioil_init(void)
{
	pthread_once(&init_links_flag, init_links);
}

DFUSE_PUBLIC int
dfuse___open64_2(const char *pathname, int flags)
{
	delay();
	ioil_init();

	return __real___open64_2(pathname, flags);
}

DFUSE_PUBLIC int
dfuse___open_2(const char *pathname, int flags)
{
	delay();
	ioil_init();

	return __real___open_2(pathname, flags);
}

DFUSE_PUBLIC int
dfuse_open(const char *pathname, int flags, ...)
{
	int		fd;
	unsigned int mode; /* mode_t gets "promoted" to unsigned int
			    * for va_arg routine
			    */

	delay();
	ioil_init();

	if (flags & O_CREAT) {
		va_list ap;

		va_start(ap, flags);
		mode = va_arg(ap, unsigned int);
		va_end(ap);

		fd = __real_open(pathname, flags, mode);
	} else {
		fd = __real_open(pathname, flags);
	}

	return fd;
}

DFUSE_PUBLIC int
dfuse_openat(int dirfd, const char *pathname, int flags, ...)
{
	int fd;
	unsigned int mode; /* mode_t gets "promoted" to unsigned int
			    * for va_arg routine
			    */

	ioil_init();
	delay();

	if (flags & O_CREAT) {
		va_list ap;

		va_start(ap, flags);
		mode = va_arg(ap, unsigned int);
		va_end(ap);

		fd = __real_openat(dirfd, pathname, flags, mode);
	} else {
		fd = __real_openat(dirfd, pathname, flags);
	}

	return fd;

}

DFUSE_PUBLIC int
dfuse_mkstemp(char *template)
{
	delay();

	return __real_mkstemp(template);
}

DFUSE_PUBLIC int
dfuse_creat(const char *pathname, mode_t mode)
{
	delay();

	/* Same as open with O_CREAT|O_WRONLY|O_TRUNC */
	return __real_open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);

}

DFUSE_PUBLIC int
dfuse_close(int fd)
{
	delay();
	return __real_close(fd);
}

DFUSE_PUBLIC ssize_t
dfuse_read(int fd, void *buf, size_t len)
{
	delay();
	return __real_read(fd, buf, len);
}

DFUSE_PUBLIC ssize_t
dfuse_pread(int fd, void *buf, size_t count, off_t offset)
{
	delay();
	return __real_pread(fd, buf, count, offset);
}

DFUSE_PUBLIC ssize_t
dfuse_write(int fd, const void *buf, size_t len)
{
	delay();
	return __real_write(fd, buf, len);
}

DFUSE_PUBLIC ssize_t
dfuse_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
	delay();
	return __real_pwrite(fd, buf, count, offset);
}

DFUSE_PUBLIC off_t
dfuse_lseek(int fd, off_t offset, int whence)
{
	delay();
	return __real_lseek(fd, offset, whence);
}

DFUSE_PUBLIC ssize_t
dfuse_readv(int fd, const struct iovec *vector, int iovcnt)
{
	delay();
	return __real_readv(fd, vector, iovcnt);
}

DFUSE_PUBLIC ssize_t
dfuse_preadv(int fd, const struct iovec *vector, int iovcnt, off_t offset)
{
	delay();
	return __real_preadv(fd, vector, iovcnt, offset);
}

DFUSE_PUBLIC ssize_t
dfuse_writev(int fd, const struct iovec *vector, int iovcnt)
{
	delay();
	return __real_writev(fd, vector, iovcnt);
}

DFUSE_PUBLIC ssize_t
dfuse_pwritev(int fd, const struct iovec *vector, int iovcnt, off_t offset)
{
	delay();
	return __real_pwritev(fd, vector, iovcnt, offset);
}

DFUSE_PUBLIC void *
dfuse_mmap(void *address, size_t length, int prot, int flags, int fd,
	   off_t offset)
{
	delay();
	return __real_mmap(address, length, prot, flags, fd, offset);
}

DFUSE_PUBLIC int
dfuse_ftruncate(int fd, off_t length)
{
	delay();
	return __real_ftruncate(fd, length);
}

DFUSE_PUBLIC int
dfuse_fsync(int fd)
{
	delay();
	return __real_fsync(fd);
}

DFUSE_PUBLIC int
dfuse_fdatasync(int fd)
{
	delay();
	return __real_fdatasync(fd);
}

DFUSE_PUBLIC int dfuse_dup(int oldfd)
{
	delay();
	return __real_dup(oldfd);
}

DFUSE_PUBLIC int
dfuse_dup2(int oldfd, int newfd)
{
	delay();
	return __real_dup2(oldfd, newfd);
}

DFUSE_PUBLIC FILE *
dfuse_fdopen(int fd, const char *mode)
{
	delay();
	ioil_init();
	return __real_fdopen(fd, mode);
}

DFUSE_PUBLIC int
dfuse_fcntl(int fd, int cmd, ...)
{
	va_list ap;
	void *arg;

	va_start(ap, cmd);
	arg = va_arg(ap, void *);
	va_end(ap);

	delay();
	return __real_fcntl(fd, cmd, arg);
}

DFUSE_PUBLIC FILE *
dfuse_fopen(const char *path, const char *mode)
{
	ioil_init();
	delay();
	return __real_fopen(path, mode);

}

DFUSE_PUBLIC FILE *
dfuse_freopen(const char *path, const char *mode, FILE *stream)
{
	delay();
	ioil_init();

	return __real_freopen(path, mode, stream);
}

DFUSE_PUBLIC int
dfuse_fclose(FILE *stream)
{
	delay();
	return __real_fclose(stream);
}

DFUSE_PUBLIC int
dfuse___fxstat(int ver, int fd, struct stat *buf)
{
	delay();
	return __real___fxstat(ver, fd, buf);
}

FOREACH_INTERCEPT(IOIL_DECLARE_ALIAS)
FOREACH_ALIASED_INTERCEPT(IOIL_DECLARE_ALIAS64)
