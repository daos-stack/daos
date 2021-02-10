/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __IOIL_IO_H__
#define __IOIL_IO_H__

#include <unistd.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <stdio.h>
#include "ioil_defines.h"

/* High level, POSIX equivalent API */
DFUSE_PUBLIC int dfuse_open(const char *, int, ...);
DFUSE_PUBLIC ssize_t dfuse_pread(int, void *, size_t, off_t);
DFUSE_PUBLIC ssize_t dfuse_pread(int, void *, size_t, off_t);
DFUSE_PUBLIC ssize_t dfuse_pwrite(int, const void *, size_t, off_t);
DFUSE_PUBLIC off_t dfuse_lseek(int, off_t, int);
DFUSE_PUBLIC ssize_t dfuse_preadv(int, const struct iovec *, int, off_t);
DFUSE_PUBLIC ssize_t dfuse_pwritev(int, const struct iovec *, int, off_t);
DFUSE_PUBLIC void *dfuse_mmap(void *, size_t, int, int, int, off_t);
DFUSE_PUBLIC int dfuse_close(int);
DFUSE_PUBLIC ssize_t dfuse_read(int, void *, size_t);
DFUSE_PUBLIC ssize_t dfuse_write(int, const void *, size_t);
DFUSE_PUBLIC ssize_t dfuse_readv(int, const struct iovec *, int);
DFUSE_PUBLIC ssize_t dfuse_writev(int, const struct iovec *, int);
DFUSE_PUBLIC int dfuse_fsync(int);
DFUSE_PUBLIC int dfuse_fdatasync(int);
DFUSE_PUBLIC int dfuse_dup(int);
DFUSE_PUBLIC int dfuse_dup2(int, int);
DFUSE_PUBLIC int dfuse_fcntl(int fd, int cmd, ...);
DFUSE_PUBLIC FILE *dfuse_fdopen(int, const char *);
DFUSE_PUBLIC FILE *dfuse_fopen(const char *, const char *);
DFUSE_PUBLIC FILE *dfuse_freopen(const char *, const char *, FILE *);
DFUSE_PUBLIC int dfuse_fclose(FILE *);

#endif /* __IOIL_IO_H__ */
