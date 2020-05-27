/**
 * (C) Copyright 2017-2020 Intel Corporation.
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

#ifndef __INTERCEPT_H__
#define __INTERCEPT_H__
#include <unistd.h>
#include <stdlib.h>
#include <sys/uio.h>
#include "dfuse_log.h"
#include "ioil_io.h"
#include "ioil_api.h"

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
	DFUSE_PUBLIC type name params __attribute__((weak, alias("dfuse_" #name)));

#define IOIL_DECLARE_ALIAS64(type, name, params) \
	DFUSE_PUBLIC type name##64 params __attribute__((weak, alias(#name)));

/* Initialize the __real_##name function pointer */
#define IOIL_FORWARD_MAP_OR_FAIL(type, name, params)                        \
	do {                                                                \
		if (__real_##name != NULL)                                  \
			break;                                              \
		__real_##name = (__typeof__(__real_##name))dlsym(RTLD_NEXT, \
								 #name);    \
		if (__real_ ## name == NULL) {                              \
			fprintf(stderr,                                     \
				"libioil couldn't map " #name "\n");       \
			exit(1);                                            \
		}                                                           \
	} while (0);

#else /* !IOIL_PRELOAD */
#define IOIL_FORWARD_DECL(type, name, params)  \
	extern type __real_##name params;

#define IOIL_DECL(name) __wrap_##name

#define IOIL_FORWARD_MAP_OR_FAIL(type, name, params) (void)0;

#define IOIL_DECLARE_ALIAS(type, name, params) \
	DFUSE_PUBLIC type __wrap_##name params \
		__attribute__((weak, alias("dfuse_" #name)));

#define IOIL_DECLARE_ALIAS64(type, name, params)                \
	DFUSE_PUBLIC type __wrap_##name##64 params                \
		__attribute__((weak, alias("__wrap_" #name)));

#endif /* IOIL_PRELOAD */

#endif /* __INTERCEPT_H__ */
