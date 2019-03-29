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

#ifndef __IOF_IO_H__
#define __IOF_IO_H__

#include <unistd.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <stdio.h>
#include "ioil_defines.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* High level, POSIX equivalent API */
IOF_PUBLIC int iof_open(const char *, int, ...);
IOF_PUBLIC ssize_t iof_pread(int, void *, size_t, off_t);
IOF_PUBLIC ssize_t iof_pread(int, void *, size_t, off_t);
IOF_PUBLIC ssize_t iof_pwrite(int, const void *, size_t, off_t);
IOF_PUBLIC off_t iof_lseek(int, off_t, int);
IOF_PUBLIC ssize_t iof_preadv(int, const struct iovec *, int, off_t);
IOF_PUBLIC ssize_t iof_pwritev(int, const struct iovec *, int, off_t);
IOF_PUBLIC void *iof_mmap(void *, size_t, int, int, int, off_t);
IOF_PUBLIC int iof_close(int);
IOF_PUBLIC ssize_t iof_read(int, void *, size_t);
IOF_PUBLIC ssize_t iof_write(int, const void *, size_t);
IOF_PUBLIC ssize_t iof_readv(int, const struct iovec *, int);
IOF_PUBLIC ssize_t iof_writev(int, const struct iovec *, int);
IOF_PUBLIC int iof_fsync(int);
IOF_PUBLIC int iof_fdatasync(int);
IOF_PUBLIC int iof_dup(int);
IOF_PUBLIC int iof_dup2(int, int);
IOF_PUBLIC int iof_fcntl(int fd, int cmd, ...);
IOF_PUBLIC FILE *iof_fdopen(int, const char *);
IOF_PUBLIC FILE *iof_fopen(const char *, const char *);
IOF_PUBLIC FILE *iof_freopen(const char *, const char *, FILE *);
IOF_PUBLIC int iof_fclose(FILE *);

#endif /* __IOF_IO_H__ */
