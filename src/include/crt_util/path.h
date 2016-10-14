/* Copyright (C) 2016 Intel Corporation
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
 *
 * src/include/crt_util/path.h
 */
#ifndef __CRT_PATH_H__
#define __CRT_PATH_H__
#include "crt_errno.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Return the full path to the executable */
const char *crt_get_exe_path();
/* Return the basename of the executable */
const char *crt_get_exe_name();

/* Ensures the existence of a directory, creating it if indicated.
 * If the directory exists and it is a directory, it returns 0.
 * If real_path is not NULL, it also allocates and returns the
 * normalized path with all links resolved
 */
int crt_check_directory(const char *directory, char **real_path,
			bool try_create);

/* If the path is a relative path, it allocates "prepended" and prepends
 * the current directory.  User is responsible to free the allocated memory.
 * If no allocation is necessary, it will simply set checked to NULL and return
 * 0.
 * This doesn't access the file system and doesn't check the validity of the
 * path.
 */
int crt_prepend_cwd(const char *path, char **prepended);

/* Check that prefix is a directory.   Append subdir and create any
 * directories that don't already exist.   Returns an error if the resulting
 * path doesn't exist upon returning or a required memory allocation failed.
 * \param[in] prefix A directory prefix
 * \param[in] subdir A subdirectory to create.  Can contain multiple elements
 * \param[out] full_path If not NULL, a string is allocated to store the
 * resulting path.
 */
int crt_create_subdirs(const char *prefix, const char *subdir,
		       char **full_path);

/* Allocate a buffer and return the current working directory.  User should
 * free the memory
 */
char *crt_getcwd(void);

/* Best effort, in place, absolute path normalization.  The file system is not
 * accessed so symbolic links and '../' entries are not removed.
 * \param[in,out] path path to normalize
 */
int crt_normalize_in_place(char *path);

#if defined(__cplusplus)
}
#endif

#endif /* __CRT_PATH_H__ */
