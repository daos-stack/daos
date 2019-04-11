/**
 * (C) Copyright 2017-2019 Intel Corporation.
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
