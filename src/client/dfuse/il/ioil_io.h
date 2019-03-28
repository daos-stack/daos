/* Copyright (C) 2017 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
