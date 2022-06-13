/**
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#define FOREACH_ALIASED_INTERCEPT(ACTION)                                                          \
	ACTION(FILE *, fopen, (const char *, const char *))                                        \
	ACTION(FILE *, freopen, (const char *, const char *, FILE *))                              \
	ACTION(int, open, (const char *, int, ...))                                                \
	ACTION(int, openat, (int, const char *, int, ...))                                         \
	ACTION(ssize_t, pread, (int, void *, size_t, off_t))                                       \
	ACTION(ssize_t, pwrite, (int, const void *, size_t, off_t))                                \
	ACTION(off_t, lseek, (int, off_t, int))                                                    \
	ACTION(int, fseek, (FILE *, long, int))                                                    \
	ACTION(int, fseeko, (FILE *, off_t, int))                                                  \
	ACTION(ssize_t, preadv, (int, const struct iovec *, int, off_t))                           \
	ACTION(ssize_t, pwritev, (int, const struct iovec *, int, off_t))                          \
	ACTION(void *, mmap, (void *, size_t, int, int, int, off_t))                               \
	ACTION(int, ftruncate, (int, off_t))

#define FOREACH_SINGLE_INTERCEPT(ACTION)                                                           \
	ACTION(int, fclose, (FILE *))                                                              \
	ACTION(int, close, (int))                                                                  \
	ACTION(int, __open64_2, (const char *, int))                                               \
	ACTION(int, __open_2, (const char *, int))                                                 \
	ACTION(ssize_t, read, (int, void *, size_t))                                               \
	ACTION(ssize_t, write, (int, const void *, size_t))                                        \
	ACTION(ssize_t, readv, (int, const struct iovec *, int))                                   \
	ACTION(ssize_t, writev, (int, const struct iovec *, int))                                  \
	ACTION(int, fsync, (int))                                                                  \
	ACTION(int, fdatasync, (int))                                                              \
	ACTION(int, dup, (int))                                                                    \
	ACTION(int, dup2, (int, int))                                                              \
	ACTION(int, fcntl, (int, int, ...))                                                        \
	ACTION(FILE *, fdopen, (int, const char *))                                                \
	ACTION(int, __fxstat, (int, int, struct stat *))                                           \
	ACTION(int, mkstemp, (char *))                                                             \
	ACTION(int, ferror, (FILE *))                                                              \
	ACTION(void, clearerr, (FILE *))                                                           \
	ACTION(int, feof, (FILE *))                                                                \
	ACTION(long, ftell, (FILE *))                                                              \
	ACTION(void, rewind, (FILE *))                                                             \
	ACTION(off_t, ftello, (FILE *))                                                            \
	ACTION(size_t, fread, (void *, size_t, size_t, FILE *))                                    \
	ACTION(size_t, fwrite, (const void *ptr, size_t size, size_t nmemb, FILE *stream))         \
	ACTION(int, fputc, (int c, FILE *stream))                                                  \
	ACTION(int, fputs, (const char *str, FILE *stream))                                        \
	ACTION(int, fputws, (const wchar_t *ws, FILE *stream))                                     \
	ACTION(int, fgetc, (FILE *stream))                                                        \
	ACTION(int, getc, (FILE *stream))                                                         \
	ACTION(char *, fgets, (char *str, int, FILE *stream))                                      \
	ACTION(wchar_t *, fgetws, (const wchar_t *ws, FILE *stream))                               \
	ACTION(int, ungetc, (int, FILE *))                                                         \
	ACTION(int, fscanf, (FILE *, const char *, ...))                                           \
	ACTION(int, vfscanf, (FILE *, const char *, va_list))                                      \
	ACTION(int, fprintf, (FILE *, const char *, ...))                                          \
	ACTION(int, vfprintf, (FILE *, const char *, va_list ap))                                  \
	ACTION(int, __uflow, (FILE *))

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
#define IOIL_FORWARD_MAP_OR_FAIL(type, name, params)                                               \
	do {                                                                                       \
		if (__real_##name != NULL)                                                         \
			break;                                                                     \
		__real_##name = (__typeof__(__real_##name))dlsym(RTLD_NEXT, #name);                \
		if (__real_##name == NULL) {                                                       \
			fprintf(stderr, "libioil couldn't map " #name "\n");                       \
			exit(1);                                                                   \
		}                                                                                  \
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
