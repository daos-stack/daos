/* Copyright (C) 2017-2019 Intel Corporation
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

#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <libgen.h>
#include <stdarg.h>

#define D_LOGFAC DD_FAC(cli)
#include "log.h"
#include "iof_atomic.h"
#include "iof_mntent.h"
#include "iof_ctrl_util.h"

static char *cnss_prefix;
static int ctrl_fd = -1;
static int cnss_id = -1;
static ATOMIC int init_count;
static int init_rc;
static pthread_once_t initialize_flag = PTHREAD_ONCE_INIT;

static int open_for_read(int *fd, const char *path)
{
	*fd = -1;

	if (ctrl_fd == -1)
		return -IOF_CTRL_NOT_INITIALIZED;

	*fd = openat(ctrl_fd, path, O_RDONLY);

	if (*fd == -1)
		return -IOF_CTRL_OPEN_FAILED;

	return 0;
}

static int open_stream_for_read(FILE **fp, const char *path)
{
	int ret;
	int fd;

	*fp = NULL;

	ret = open_for_read(&fd, path);
	if (ret != 0)
		return ret;

	*fp = fdopen(fd, "r");
	if (*fp == NULL) {
		close(fd);
		return -IOF_CTRL_OPEN_FAILED;
	}

	return 0;
}

static int open_stream_for_write(FILE **fp, const char *path)
{
	int fd;

	*fp = NULL;

	if (ctrl_fd == -1)
		return -IOF_CTRL_NOT_INITIALIZED;

	fd = openat(ctrl_fd, path, O_WRONLY);

	if (fd == -1)
		return -IOF_CTRL_OPEN_FAILED;

	*fp = fdopen(fd, "w");
	if (*fp == NULL) {
		close(fd);
		return -IOF_CTRL_OPEN_FAILED;
	}

	return 0;
}

int iof_ctrl_read_str(char *str, int len, const char *path)
{
	char *last;
	char buf[IOF_CTRL_MAX_LEN];
	ssize_t bytes_read;
	int fd;
	int ret;
	int buflen;

	ret = open_for_read(&fd, path);
	if (ret != 0)
		return ret;

	bytes_read = read(fd, buf, IOF_CTRL_MAX_LEN);

	if (bytes_read == -1)
		return -IOF_CTRL_IO_FAILED;
	buflen = IOF_CTRL_MAX_LEN;
	buf[buflen - 1] = 0;

	if (bytes_read > 0 && bytes_read < IOF_CTRL_MAX_LEN) {
		buflen = bytes_read + 1;
		buf[bytes_read] = 0;
		last = &buf[bytes_read - 1];
		while (*last == '\n') {
			buflen--;
			*last = 0;
			if (last == buf)
				break;
			last--;
		}
	}

	ret = 0;

	if (buflen > len)
		ret = buflen;
	else
		strcpy(str, buf);

	close(fd);

	return ret;
}

#define DECLARE_READ_FUNC(name, type, format)          \
	int name(type val, const char *path)           \
	{                                              \
		FILE *fp;                              \
		int ret;                               \
		if (path == NULL)                      \
			return -IOF_CTRL_INVALID_ARG;  \
		ret = open_stream_for_read(&fp, path); \
		if (ret != 0)                          \
			return ret;                    \
		ret = fscanf(fp, format, val);         \
		fclose(fp);                            \
		if (ret <= 0)                          \
			return -IOF_CTRL_IO_FAILED;    \
		return 0;                              \
	}

DECLARE_READ_FUNC(iof_ctrl_read_int64, int64_t *, "%" PRIi64)
DECLARE_READ_FUNC(iof_ctrl_read_uint64, uint64_t *, "%" PRIu64)
DECLARE_READ_FUNC(iof_ctrl_read_int32, int32_t *, "%i")
DECLARE_READ_FUNC(iof_ctrl_read_uint32, uint32_t *, "%u")

int iof_ctrl_write_strf(const char *path, const char *format, ...)
{
	va_list ap;
	FILE *fp;
	int ret;
	int flags;

	if (path == NULL)
		return -IOF_CTRL_INVALID_ARG;

	ret = open_stream_for_write(&fp, path);

	if (ret != 0)
		return ret;

	va_start(ap, format);
	ret = vfprintf(fp, format, ap);
	va_end(ap);
	flags = d_log_check(D_LOGFAC | DLOG_INFO);
	if (flags != 0) {
		va_start(ap, format);
		d_vlog(flags, format, ap);
		va_end(ap);
	}

	fclose(fp);

	if (ret <= 0)
		return -IOF_CTRL_IO_FAILED;

	return 0;
}

int iof_ctrl_trigger(const char *path)
{
	int ret;

	if (ctrl_fd == -1)
		return -IOF_CTRL_NOT_INITIALIZED;

	ret = utimensat(ctrl_fd, path, NULL, 0);

	if (ret == -1)
		return -IOF_CTRL_BAD_FILE;

	return 0;
}

int iof_ctrl_get_tracker_id(int *val, const char *path)
{
	FILE *fp;
	int ret;

	if (val == NULL || path == NULL)
		return -IOF_CTRL_INVALID_ARG;

	ret = open_stream_for_read(&fp, path);
	if (ret != 0)
		return ret;

	fscanf(fp, "%d", val);

	/* file closed on exit */

	return 0;
}

static int check_mnt(struct mntent *entry, void *priv)
{
	char *dir;
	char *cnss_dir;
	char *p;
	int rc;
	int saved_ctrl_fd;
	const char *cnss_env = (const char *)priv;

	p = strstr(entry->mnt_dir, "/.ctrl");
	if (p == NULL || strcmp(entry->mnt_type, "fuse.ctrl") ||
	    strcmp(entry->mnt_fsname, "CNSS"))
		return 0;

	IOF_LOG_INFO("Checking possible CNSS: ctrl dir at %s", entry->mnt_dir);
	dir = strdup(entry->mnt_dir);
	if (dir == NULL) {
		IOF_LOG_ERROR("Insufficient memory to find CNSS");
		return 0;
	}

	cnss_dir = strdup(dirname(dir));
	if (cnss_dir == NULL) {
		IOF_LOG_ERROR("Insufficient memory to find CNSS");
		free(dir);
		free(cnss_dir);
		return 0;
	}

	free(dir);

	if (cnss_env != NULL && strcmp(cnss_dir, cnss_env)) {
		IOF_LOG_INFO("Skipping CNSS: CNSS_PREFIX doesn't match");
		free(cnss_dir);
		return 0;
	}

	saved_ctrl_fd = ctrl_fd;

	ctrl_fd = open(entry->mnt_dir, O_RDONLY | O_DIRECTORY);
	if (ctrl_fd == -1) {
		IOF_LOG_INFO("Could not open %s to find CNSS: %s",
			     entry->mnt_dir, strerror(errno));
		rc = 0;
		goto cleanup;
	}

	rc = iof_ctrl_read_int32(&cnss_id, "cnss_id");
	if (rc != 0) {
		IOF_LOG_INFO("Could not read cnss id: rc = %d, errno = %s",
			     rc, strerror(errno));
		rc = 0;
		goto cleanup;
	}

	if (cnss_prefix != NULL) {
		IOF_LOG_ERROR("Multiple viable CNSS options not supported");
		goto handle_error;
	}

	cnss_prefix = cnss_dir;

	return 0;
handle_error:
	rc = 1; /* No need to keep searching */
	if (cnss_prefix != NULL &&  cnss_dir != cnss_prefix) {
		free(cnss_prefix);
		cnss_prefix = NULL;
	}
cleanup:
	if (cnss_dir != NULL)
		free(cnss_dir);
	if (ctrl_fd != -1)
		close(ctrl_fd);
	ctrl_fd = saved_ctrl_fd;

	return rc;
}

static void init_fs_util(void)
{
	char *cnss_env;

	cnss_env = getenv("CNSS_PREFIX");

	iof_mntent_foreach(check_mnt, cnss_env);

	if (cnss_prefix == NULL) {
		if (cnss_env != NULL)
			IOF_LOG_ERROR("CNSS_PREFIX is set but indicates"
				      " invalid CNSS. Is it set by mistake?");
		else
			IOF_LOG_ERROR("Could not detect active CNSS");

		init_rc = -IOF_CTRL_NOT_FOUND;
		return;
		/* If multiple users call init, we need to ensure
		 * we only initialize once
		 */
	}

	init_rc = 0;
}

int iof_ctrl_util_init(const char **prefix, int *id)
{
	if (prefix == NULL || id == NULL)
		return -IOF_CTRL_INVALID_ARG;

	/* If multiple users call init, we need to ensure
	 * we only initialize once but keep track of callers
	 * so we also only finalize once
	 */
	atomic_inc(&init_count);

	pthread_once(&initialize_flag, init_fs_util);

	*prefix = NULL;
	*id = -1;

	if (init_rc != 0)
		return init_rc;

	*prefix = cnss_prefix;
	*id = cnss_id;

	return 0;
}

int iof_ctrl_util_finalize(void)
{
	int count;

	count = atomic_fetch_sub(&init_count, 1);
	if (count != 1)
		return 0;

	if (cnss_prefix != NULL) {
		free(cnss_prefix);
		close(ctrl_fd);
	}
	ctrl_fd = -1;
	cnss_prefix = NULL;

	iof_log_close();
	return 0;
}

int iof_ctrl_util_test_init(const char *ctrl_path)
{
	ctrl_fd = open(ctrl_path, O_RDONLY | O_DIRECTORY);
	if (ctrl_fd == -1) {
		IOF_LOG_ERROR("Could not open %s for ctrl fs",
			      ctrl_path);
		return -IOF_CTRL_NOT_FOUND;
	}
	return 0;
}

int iof_ctrl_util_test_finalize(void)
{
	close(ctrl_fd);
	return 0;
}
