/* Copyright (C) 2017-2018 Intel Corporation
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
#ifndef __INTERCEPT_H__
#define __INTERCEPT_H__
#include <unistd.h>
#include <stdlib.h>
#include <sys/uio.h>
#include "log.h"
#include "ios_gah.h"
#include "iof_fs.h"
#include "iof_io.h"
#include "iof_api.h"

/* Low level I/O functions we intercept
 *
 * We purposefully skip the following:
 * fileno
 * fileno_unlocked
 * sync
 * munmap
 * msync
 * mremap
 * select
 * all aio routines (for now)
 * fcntl (for now though we likely need for dup)
 */
#define FOREACH_ALIASED_INTERCEPT(ACTION)                                     \
	ACTION(FILE *,  fopen,     (const char *, const char *))              \
	ACTION(FILE *,  freopen,   (const char *, const char *, FILE *))      \
	ACTION(int,     open,      (const char *, int, ...))                  \
	ACTION(ssize_t, pread,     (int, void *, size_t, off_t))              \
	ACTION(ssize_t, pwrite,    (int, const void *, size_t, off_t))        \
	ACTION(off_t,   lseek,     (int, off_t, int))                         \
	ACTION(ssize_t, preadv,    (int, const struct iovec *, int, off_t))   \
	ACTION(ssize_t, pwritev,   (int, const struct iovec *, int, off_t))   \
	ACTION(void *,  mmap,      (void *, size_t, int, int, int, off_t))

#define FOREACH_SINGLE_INTERCEPT(ACTION)                                      \
	ACTION(int,     fclose,    (FILE *))                                  \
	ACTION(int,     close,     (int))                                     \
	ACTION(ssize_t, read,      (int, void *, size_t))                     \
	ACTION(ssize_t, write,     (int, const void *, size_t))               \
	ACTION(ssize_t, readv,     (int, const struct iovec *, int))          \
	ACTION(ssize_t, writev,    (int, const struct iovec *, int))          \
	ACTION(int,     fsync,     (int))                                     \
	ACTION(int,     fdatasync, (int))                                     \
	ACTION(int,     dup,       (int))                                     \
	ACTION(int,     dup2,      (int, int))                                \
	ACTION(int,     fcntl,     (int fd, int cmd, ...))                    \
	ACTION(FILE *,  fdopen,    (int, const char *))

#define FOREACH_INTERCEPT(ACTION)            \
	FOREACH_SINGLE_INTERCEPT(ACTION)     \
	FOREACH_ALIASED_INTERCEPT(ACTION)

#ifdef IOIL_PRELOAD
#include <dlfcn.h>

#define IOIL_FORWARD_DECL(type, name, params)  \
	static type(*__real_##name) params;

#define IOIL_DECL(name) name

#define IOIL_DECLARE_ALIAS(type, name, params) \
	IOF_PUBLIC type name params __attribute__((weak, alias("iof_" #name)));

#define IOIL_DECLARE_ALIAS64(type, name, params) \
	IOF_PUBLIC type name##64 params __attribute__((weak, alias(#name)));

/* Initialize the __real_##name function pointer */
#define IOIL_FORWARD_MAP_OR_FAIL(type, name, params)                        \
	do {                                                                \
		if (__real_##name != NULL)                                  \
			break;                                              \
		__real_##name = (__typeof__(__real_##name))dlsym(RTLD_NEXT, \
								 #name);    \
		if (__real_ ## name == NULL) {                              \
			fprintf(stderr,                                     \
				"libiofil couldn't map " #name "\n");       \
			exit(1);                                            \
		}                                                           \
	} while (0);

#else /* !IOIL_PRELOAD */
#define IOIL_FORWARD_DECL(type, name, params)  \
	extern type __real_##name params;

#define IOIL_DECL(name) __wrap_##name

#define IOIL_FORWARD_MAP_OR_FAIL(type, name, params) (void)0;

#define IOIL_DECLARE_ALIAS(type, name, params) \
	IOF_PUBLIC type __wrap_##name params \
		__attribute__((weak, alias("iof_" #name)));

#define IOIL_DECLARE_ALIAS64(type, name, params)                \
	IOF_PUBLIC type __wrap_##name##64 params                \
		__attribute__((weak, alias("__wrap_" #name)));

#endif /* IOIL_PRELOAD */

ssize_t ioil_do_pread(char *buff, size_t len, off_t position,
		      struct iof_file_common *f_info, int *errcode);
ssize_t ioil_do_preadv(const struct iovec *iov, int count, off_t position,
		       struct iof_file_common *f_info, int *errcode);
ssize_t ioil_do_writex(const char *buff, size_t len, off_t position,
		       struct iof_file_common *f_info, int *errcode);
ssize_t ioil_do_pwritev(const struct iovec *iov, int count, off_t position,
			struct iof_file_common *f_info, int *errcode);

#endif /* __INTERCEPT_H__ */
