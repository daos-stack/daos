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
 * This file defines path manipulation and creation functions
 *
 * src/util/path.c
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#ifdef __APPLE__
# include <sys/syslimits.h>
# include <sys/param.h>
static inline char *
get_exe_path(void)
{
	uint32_t	newlen = 0;
	char		*exepath;

	_NSGetExecutablePath(NULL, &newlen);
	exepath = malloc(newlen + 1);
	if (exepath == NULL)
		return NULL;

	newlen++;
	_NSGetExecutablePath(exepath, &newlen);

	return exepath;
}

#else
#include <limits.h>
#include <stdlib.h>
#include <linux/limits.h>
static inline char *
get_exe_path(void)
{
	return realpath("/proc/self/exe", NULL);
}
#endif
#include "crt_util/path.h"

static pthread_once_t init_exe_once = PTHREAD_ONCE_INIT;

static char *full_exe_path;
static char *exe_basename;

static void
init_exe(void)
{
	char *tmp;

	full_exe_path = get_exe_path();

	if (full_exe_path == NULL)
		return;

	tmp = strdup(full_exe_path);
	exe_basename = strdup(basename(tmp));
	free(tmp);
}

const char *
crt_get_exe_path()
{
	pthread_once(&init_exe_once, init_exe);

	return full_exe_path;
}

const char *
crt_get_exe_name()
{
	pthread_once(&init_exe_once, init_exe);

	return exe_basename;
}

/* Create all directories in pathname that don't exist */
static int
create_all_dirs(const char *pathname)
{
	char		*orig = NULL;
	char		*cur;
	struct stat	file_stat;
	bool		do_exit = 0;

	if (pathname == NULL || *pathname != '/')
		return -CER_BADPATH;

	orig = strdup(pathname);

	if (orig == NULL)
		return -CER_NOMEM;

	cur = orig;
	if (*cur == '\0') {
		free(orig);
		return 0;
	}

	/* Skip first '/' */
	cur++;

	while (1) {

		if (*cur == '/' || *cur == '\0') {

			if (*cur == '\0')
				do_exit = 1;

			*cur = '\0';

			/* There is a potential race between two threads
			 * creating the directory.   Therefore, we need
			 * to try to create it first.
			 *
			 * Four possible outcomes
			 *   1. mkdir fails because of permissions (BAD)
			 *   2. mkdir fails because a file exists (BAD)
			 *   3. mkdir fails because a directory exists (GOOD)
			 *   4. mkdir succeeds (GOOD)
			 */
			if (mkdir(orig, 0700) != 0) {
				if (stat(orig, &file_stat) != 0) {
					/* Case 1: Can't stat the file so
					 * the directory doesn't exist.
					 * Probably don't have permission
					 * to create it.
					 */
					free(orig);
					return -CER_NO_PERM;
				}

				if (!S_ISDIR(file_stat.st_mode)) {
					/* Case 2: A file of the same name
					 * exists.
					 */
					free(orig);
					return -CER_NOTDIR;
				}
				/* Case 3: Directory already exists */
			}
			/* else, Case 4: mkdir succeeds */

			if (do_exit) {
				free(orig);
				return 0;
			}

			*cur = '/';
		}
		cur++;
	}


	return 0;
}

int
crt_check_directory(const char *path, char **newpath, bool try_create)
{
	char		*temp = NULL;
	const char	*pathptr = path;
	struct stat	stat_info;

	if (path == NULL)
		return -CER_INVAL;

	if (try_create)
		create_all_dirs(path);

	if (newpath != NULL) {
		*newpath = NULL;

		temp = realpath(path, NULL);

		if (temp == NULL) {
			switch (errno) {
			case EACCES:
				return -CER_NO_PERM;
			default:
				return -CER_BADPATH;
			}
		}

		pathptr = temp;
	}

	if (stat(pathptr, &stat_info) != 0) {
		free(temp);
		return -CER_BADPATH;
	}

	if (!S_ISDIR(stat_info.st_mode)) {
		free(temp);
		return -CER_NOTDIR;
	}

	if (newpath != NULL)
		*newpath = temp;

	return 0;
}

int
crt_create_subdirs(const char *prefix, const char *subdir, char **full_path)
{
	char		*temp;
	struct stat	buf;
	int		ret;

	if (full_path == NULL)
		return -CER_INVAL;

	*full_path = NULL;

	if (subdir == NULL)
		return -CER_INVAL;

	ret = crt_check_directory(prefix, NULL, false);
	if (ret != 0)
		return ret;

	if (strcmp(subdir, "") == 0) {
		*full_path = strdup(prefix);
		return 0;
	}

	ret = asprintf(&temp, "%s/%s", prefix, subdir);

	if (ret == -1)
		return -CER_NOMEM;

	/* If the whole path already exists, there is nothing more to do */
	if (stat(temp, &buf) == 0) {
		if (!S_ISDIR(buf.st_mode)) {
			free(temp);
			return -CER_NOTDIR;
		}
		*full_path = temp;
		return 0;
	}

	/* Walks through the path name from left to right, checking
	 * a directory at a time.   If any along the way don't exist
	 * and can't be created, it returns an error.
	 */
	ret = create_all_dirs(temp);

	if (ret != 0) {
		free(temp);
		return ret;
	}

	*full_path = temp;
	return 0;
}

int
crt_prepend_cwd(const char *path, char **prepended)
{
	char	*temp;
	int	rc;

	if (prepended == NULL)
		return -CER_INVAL;

	*prepended = NULL;

	if (path == NULL)
		return -CER_INVAL;

	if (*path == '/')
		return 0;

	/* Make the relative path an absolute path.
	 * Note that this does no checking that the final
	 * result is a valid path.  This will be validated
	 * later.
	 */
	temp = crt_getcwd();

	if (temp == NULL)
		return -CER_NOMEM;

	rc = asprintf(prepended, "%s/%s", temp, path);

	free(temp);

	if (rc == -1) {
		*prepended = NULL;
		return -CER_NOMEM;
	}

	return 0;
}

char *
crt_getcwd(void)
{
	char	buf[PATH_MAX + 1];
	char	*tmp;

	tmp = getcwd(buf, PATH_MAX + 1);

	if (tmp == NULL)
		return NULL;

	return strdup(tmp);
}

int
crt_normalize_in_place(char *path)
{
	char	*dest;
	char	*src;
	char	last;

	if (path == NULL)
		return -CER_INVAL;

	dest = src = path;
	last = '\0';

	/* Remove single .'s */
	while (*src != '\0') {

		if (*src == '.' && *(src + 1) != '.') {
			/* Don't remove long strings of '.' characters */
			if (((last == '/' || last == '\0') &&
			(*(src+1) == '/' || *(src+1) == '\0'))) {
				/* skip the . */
				src++;
				continue;
			}
		} else if (*src == '.')
			*dest++ = *src++;

		last = *src;
		*dest++ = *src++;
	}

	*dest = '\0';
	dest = src = path;
	last = '\0';

	while (*src != '\0') {

		if (last == '/' && *src == '/') {
			/* skip duplicates */
			src++;
			continue;
		}

		last = *src;
		*dest = *src;
		dest++;
		src++;
	}

	*dest = '\0';

	return 0;
}

static __attribute__((destructor)) void
path_fini(void)
{
	/* Free up the globals */
	free(exe_basename);
	free(full_exe_path);
}
