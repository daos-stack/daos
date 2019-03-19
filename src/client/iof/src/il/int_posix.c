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
#include <stdarg.h>
#include <inttypes.h>
#include <libgen.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <string.h>
#include <gurt/list.h>
#include <cart/api.h>
#include "iof_mntent.h"
#include "intercept.h"
#include "iof_ioctl.h"
#include "iof_vector.h"
#include "iof_common.h"
#include "iof_ctrl_util.h"

FOREACH_INTERCEPT(IOIL_FORWARD_DECL)

static bool ioil_initialized;
static __thread int saved_errno;
static vector_t fd_table;
static const char *cnss_prefix;
static crt_context_t crt_ctx;
static int cnss_id;
static struct iof_service_group ionss_grp;
static struct iof_projection *projections;
static uint32_t projection_count;
static struct crt_proto_format *iof_proto;

#define BLOCK_SIZE 1024

#define SAVE_ERRNO(is_error)                 \
	do {                                 \
		if (is_error)                \
			saved_errno = errno; \
	} while (0)

#define RESTORE_ERRNO(is_error)              \
	do {                                 \
		if (is_error)                \
			errno = saved_errno; \
	} while (0)

static const char * const bypass_status[] = {
	"external",
	"on",
	"off-mmap",
	"off-flag",
	"off-fcntl",
	"off-stream",
	"off-rsrc",
};

struct fd_entry {
	struct iof_file_common common;
	off_t pos;
	int flags;
	int status;
};

int ioil_initialize_fd_table(int max_fds)
{
	int rc;

	rc = vector_init(&fd_table, sizeof(struct fd_entry), max_fds);

	if (rc != 0)
		IOF_LOG_ERROR("Could not allocate file descriptor table"
			      ", disabling kernel bypass: rc = %d", rc);
	return rc;
}

#define BUFSIZE 64

static int find_projections(void)
{
	struct iof_service_group *grp_info = &ionss_grp;
	char buf[IOF_CTRL_MAX_LEN];
	char tmp[BUFSIZE];
	int rc;
	int i = 0;
	uint32_t version;
	d_rank_t rank;
	uint32_t tag;

	rc = iof_ctrl_read_uint32(&version, "iof/ioctl_version");
	if (rc != 0) {
		IOF_LOG_ERROR("Could not read ioctl version, rc = %d", rc);
		return 1;
	}

	if (version != IOF_IOCTL_VERSION) {
		IOF_LOG_ERROR("IOCTL version mismatch: %d != %d", version,
			      IOF_IOCTL_VERSION);
		return 1;
	}

	rc = crt_group_config_path_set(cnss_prefix);
	if (rc != 0) {
		IOF_LOG_INFO("Could not set group config path, rc = %d", rc);
		return 1;
	}

	snprintf(tmp, BUFSIZE, "iof/ionss/%d/name", i);

	rc = iof_ctrl_read_str(buf, IOF_CTRL_MAX_LEN, tmp);
	if (rc != 0) {
		IOF_LOG_INFO("Could not get ionss name, rc = %d", rc);
		return 1;
	}

	/* Ok, now try to attach.  Note, this will change when we
	 * attach to multiple IONSS processes
	 */
	rc = crt_group_attach(buf, &grp_info->dest_grp);
	if (rc != 0) {
		IOF_LOG_INFO("Could not attach to ionss %s, rc = %d",
			     buf, rc);
		return 1;
	}

	rc = iof_lm_attach(grp_info->dest_grp, crt_ctx);
	if (rc != 0) {
		IOF_LOG_ERROR("Could not initialize failover, rc = %d",
			      rc);
		return 1;
	}

	grp_info->psr_ep.ep_grp = grp_info->dest_grp;

	snprintf(tmp, BUFSIZE, "iof/ionss/%d/psr_rank", i);
	rc = iof_ctrl_read_uint32(&rank, tmp);
	if (rc != 0) {
		IOF_LOG_ERROR("Could not read psr_rank, rc = %d", rc);
		return 1;
	}

	grp_info->psr_ep.ep_rank = rank;

	snprintf(tmp, BUFSIZE, "iof/ionss/%d/psr_tag", i);
	rc = iof_ctrl_read_uint32(&tag, tmp);
	if (rc != 0) {
		IOF_LOG_ERROR("Could not read psr_tag, rc = %d", rc);
		return 1;
	}
	grp_info->psr_ep.ep_tag = tag;

	grp_info->enabled = true;

	rc = iof_ctrl_read_uint32(&projection_count, "iof/projection_count");
	if (rc != 0) {
		IOF_LOG_ERROR("Could not read projection count, rc = %d", rc);
		return 1;
	}

	projections = calloc(projection_count, sizeof(*projections));
	if (projections == NULL) {
		IOF_LOG_ERROR("Could not allocate memory");
		return 1;
	}

	for (i = 0; i < projection_count; i++) {
		struct iof_projection *proj = &projections[i];

		proj->cli_fs_id = i;
		proj->crt_ctx = crt_ctx;
		proj->io_proto = iof_proto;

		snprintf(tmp, BUFSIZE, "iof/projections/%d/max_iov_write", i);
		rc = iof_ctrl_read_uint32(&proj->max_iov_write, tmp);
		if (rc != 0) {
			IOF_LOG_ERROR("Could not max_iov_write, rc = %d", rc);
			return 1;
		}

		snprintf(tmp, BUFSIZE, "iof/projections/%d/max_write", i);
		rc = iof_ctrl_read_uint32(&proj->max_write, tmp);
		if (rc != 0) {
			IOF_LOG_ERROR("Could not max_write, rc = %d", rc);
			return 1;
		}

		proj->grp = &ionss_grp;
		proj->enabled = true;
	}

	return 0;
}

static ssize_t pread_rpc(struct fd_entry *entry, char *buff, size_t len,
			 off_t offset)
{
	ssize_t bytes_read;
	int errcode;

	/* Just get rpc working then work out how to really do this */
	bytes_read = ioil_do_pread(buff, len, offset, &entry->common, &errcode);
	if (bytes_read < 0)
		saved_errno = errcode;
	return bytes_read;
}

/* Start simple and just loop */
static ssize_t preadv_rpc(struct fd_entry *entry, const struct iovec *iov,
			  int count, off_t offset)
{
	ssize_t bytes_read;
	int errcode;

	/* Just get rpc working then work out how to really do this */
	bytes_read = ioil_do_preadv(iov, count, offset, &entry->common,
				    &errcode);
	if (bytes_read < 0)
		saved_errno = errcode;
	return bytes_read;
}

static ssize_t pwrite_rpc(struct fd_entry *entry, const char *buff, size_t len,
			  off_t offset)
{
	ssize_t bytes_written;
	int errcode;

	/* Just get rpc working then work out how to really do this */
	bytes_written = ioil_do_writex(buff, len, offset, &entry->common,
				       &errcode);
	if (bytes_written < 0)
		saved_errno = errcode;

	return bytes_written;
}

/* Start simple and just loop */
static ssize_t pwritev_rpc(struct fd_entry *entry, const struct iovec *iov,
			   int count, off_t offset)
{
	ssize_t bytes_written;
	int errcode;

	/* Just get rpc working then work out how to really do this */
	bytes_written = ioil_do_pwritev(iov, count, offset, &entry->common,
					&errcode);
	if (bytes_written < 0)
		saved_errno = errcode;

	return bytes_written;
}

static pthread_once_t init_links_flag = PTHREAD_ONCE_INIT;

static void init_links(void)
{
	FOREACH_INTERCEPT(IOIL_FORWARD_MAP_OR_FAIL);
}

static __attribute__((constructor)) void ioil_init(void)
{
	char buf[IOF_CTRL_MAX_LEN];
	struct rlimit rlimit;
	int rc;

	pthread_once(&init_links_flag, init_links);

	iof_log_init("IL", "IOIL", NULL);

	/* Get maximum number of file descriptors */
	rc = getrlimit(RLIMIT_NOFILE, &rlimit);
	if (rc != 0) {
		IOF_LOG_ERROR("Could not get process file descriptor limit"
			      ", disabling kernel bypass");
		return;
	}

	rc = ioil_initialize_fd_table(rlimit.rlim_max);
	if (rc != 0) {
		IOF_LOG_ERROR("Could not create fd_table, rc = %d,"
			      ", disabling kernel bypass", rc);
		return;
	}

	rc = iof_ctrl_util_init(&cnss_prefix, &cnss_id);

	if (rc != 0) {
		IOF_LOG_ERROR("Could not find CNSS (rc = %d)."
			      " disabling kernel bypass", rc);
		return;
	}

	rc = iof_ctrl_read_str(buf, IOF_CTRL_MAX_LEN, "crt_protocol");
	if (rc == 0)
		setenv("CRT_PHY_ADDR_STR", buf, 1);

	rc = crt_init(NULL, CRT_FLAG_BIT_SINGLETON);
	if (rc != 0) {
		IOF_LOG_ERROR("Could not initialize crt, rc = %d,"
			      " disabling kernel bypass", rc);
		return;
	}

	rc = crt_context_create(&crt_ctx);
	if (rc != 0) {
		IOF_LOG_ERROR("Could not create crt context, rc = %d,"
			      " disabling kernel bypass", rc);
		crt_finalize();
		return;
	}

	/* TODO: This needs to call the crt_proto_query() to ensure the server
	 * supports the same version of the protocol
	 */
	rc = iof_io_register(&iof_proto, NULL);
	if (rc != 0) {
		crt_context_destroy(crt_ctx, 0);
		crt_finalize();
		IOF_LOG_ERROR("Could not create crt context, rc = %d,"
			      " disabling kernel bypass", rc);
		return;
	}

	rc = find_projections();
	if (rc != 0) {
		IOF_LOG_ERROR("Could not configure projections, rc = %d"
			      " disabling kernel bypass", rc);
		iof_ctrl_util_finalize();
		return;
	}

	IOF_LOG_INFO("Using IONSS: cnss_prefix at %s, cnss_id is %d",
		     cnss_prefix, cnss_id);

	__sync_synchronize();

	ioil_initialized = true;
}

static __attribute__((destructor)) void ioil_fini(void)
{
	if (ioil_initialized) {
		crt_group_detach(ionss_grp.dest_grp);
		crt_context_destroy(crt_ctx, 0);
		crt_finalize();
		iof_ctrl_util_finalize();
		free(projections);
	}
	ioil_initialized = false;

	__sync_synchronize();

	iof_log_close();

	vector_destroy(&fd_table);
}

static bool check_ioctl_on_open(int fd, struct fd_entry *entry, int flags,
				int status)
{
	struct iof_gah_info gah_info;
	int rc;

	if (fd == -1)
		return false;

	rc = ioctl(fd, IOF_IOCTL_GAH, &gah_info);
	if (rc != 0)
		return false;

	if (gah_info.version != IOF_IOCTL_VERSION) {
		IOF_LOG_INFO("IOF ioctl version mismatch (fd=%d): expected %d "
			     "got %d", fd, IOF_IOCTL_VERSION, gah_info.version);
		return false;
	}

	if (gah_info.cnss_id != cnss_id) {
		IOF_LOG_INFO("IOF ioctl (fd=%d) received from another CNSS: "
			     "expected %d got %d", fd, cnss_id,
			     gah_info.cnss_id);
		return false;
	}

	IOF_LOG_INFO("IOF file opened fd=%d." GAH_PRINT_STR ", bypass=%s",
		     fd, GAH_PRINT_VAL(gah_info.gah), bypass_status[status]);
	entry->common.gah = gah_info.gah;
	entry->common.projection = &projections[gah_info.cli_fs_id];
	entry->common.ep = entry->common.projection->grp->psr_ep;
	entry->pos = 0;
	entry->flags = flags;
	entry->status = IOF_IO_BYPASS;
	rc = vector_set(&fd_table, fd, entry);
	if (rc != 0) {
		IOF_LOG_INFO("Failed to track IOF file fd=%d." GAH_PRINT_STR
			     ", disabling kernel bypass",
			     rc, GAH_PRINT_VAL(gah_info.gah));
		/* Disable kernel bypass */
		entry->status = IOF_IO_DIS_RSRC;
	}
	return true;
}

static bool drop_reference_if_disabled(struct fd_entry *entry)
{
	if (entry->status == IOF_IO_BYPASS)
		return false;

	vector_decref(&fd_table, entry);

	return true;
}

IOF_PUBLIC int iof_open(const char *pathname, int flags, ...)
{
	struct fd_entry entry = {0};
	int fd;
	int status;
	unsigned int mode; /* mode_t gets "promoted" to unsigned int
			    * for va_arg routine
			    */

	if (flags & O_CREAT) {
		va_list ap;

		va_start(ap, flags);
		mode = va_arg(ap, unsigned int);
		va_end(ap);

		fd = __real_open(pathname, flags, mode);
	} else {
		fd =  __real_open(pathname, flags);
	}

	if (!ioil_initialized || (fd == -1))
		return fd;

	status = IOF_IO_BYPASS;
	/* Disable bypass for O_APPEND|O_PATH */
	if ((flags & (O_PATH | O_APPEND)) != 0)
		status = IOF_IO_DIS_FLAG;

	if (!check_ioctl_on_open(fd, &entry, flags, status))
		goto finish;

	if (flags & O_CREAT)
		IOF_LOG_INFO("open(pathname=%s, flags=0%o, mode=0%o) = "
			     "%d." GAH_PRINT_STR " intercepted, "
			     "bypass=%s", pathname, flags, mode, fd,
			     GAH_PRINT_VAL(entry.common.gah),
			     bypass_status[entry.status]);
	else
		IOF_LOG_INFO("open(pathname=%s, flags=0%o) = %d." GAH_PRINT_STR
			     " intercepted, bypass=%s", pathname, flags, fd,
			     GAH_PRINT_VAL(entry.common.gah),
			     bypass_status[entry.status]);

finish:
	return fd;
}

IOF_PUBLIC int iof_creat(const char *pathname, mode_t mode)
{
	struct fd_entry entry = {0};
	int fd;

	/* Same as open with O_CREAT|O_WRONLY|O_TRUNC */
	fd = __real_open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);

	if (!ioil_initialized || (fd == -1))
		return fd;

	if (!check_ioctl_on_open(fd, &entry, O_CREAT | O_WRONLY | O_TRUNC,
				 IOF_IO_BYPASS))
		goto finish;

	IOF_LOG_INFO("creat(pathname=%s, mode=0%o) = %d." GAH_PRINT_STR
		     " intercepted, bypass=%s", pathname, mode, fd,
		     GAH_PRINT_VAL(entry.common.gah),
		     bypass_status[entry.status]);

finish:
	return fd;
}

IOF_PUBLIC int iof_close(int fd)
{
	struct fd_entry *entry;
	int rc;

	rc = vector_remove(&fd_table, fd, &entry);

	if (rc != 0)
		goto do_real_close;

	IOF_LOG_INFO("close(fd=%d." GAH_PRINT_STR ") intercepted, bypass=%s",
		     fd, GAH_PRINT_VAL(entry->common.gah),
		     bypass_status[entry->status]);

	vector_decref(&fd_table, entry);

do_real_close:
	return __real_close(fd);
}

IOF_PUBLIC ssize_t iof_read(int fd, void *buf, size_t len)
{
	struct fd_entry *entry;
	ssize_t bytes_read;
	off_t oldpos;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_read;

	IOF_LOG_INFO("read(fd=%d." GAH_PRINT_STR ", buf=%p, len=%zu) "
		     "intercepted, bypass=%s", fd,
		     GAH_PRINT_VAL(entry->common.gah),
		     buf, len,
		     bypass_status[entry->status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_read;

	oldpos = entry->pos;
	bytes_read = pread_rpc(entry, buf, len, oldpos);
	if (bytes_read > 0)
		entry->pos = oldpos + bytes_read;
	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(bytes_read < 0);

	return bytes_read;

do_real_read:
	return __real_read(fd, buf, len);
}

IOF_PUBLIC ssize_t iof_pread(int fd, void *buf, size_t count, off_t offset)
{
	struct fd_entry *entry;
	ssize_t bytes_read;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_pread;

	IOF_LOG_INFO("pread(fd=%d." GAH_PRINT_STR ", buf=%p, count=%zu, "
		     "offset=%zd) intercepted, bypass=%s", fd,
		     GAH_PRINT_VAL(entry->common.gah), buf, count, offset,
		     bypass_status[entry->status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_pread;

	bytes_read = pread_rpc(entry, buf, count, offset);

	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(bytes_read < 0);

	return bytes_read;

do_real_pread:
	return __real_pread(fd, buf, count, offset);
}

IOF_PUBLIC ssize_t iof_write(int fd, const void *buf, size_t len)
{
	struct fd_entry *entry;
	ssize_t bytes_written;
	off_t oldpos;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_write;

	IOF_LOG_INFO("write(fd=%d." GAH_PRINT_STR ", buf=%p, len=%zu) "
		     "intercepted, bypass=%s", fd,
		     GAH_PRINT_VAL(entry->common.gah), buf, len,
		     bypass_status[entry->status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_write;

	oldpos = entry->pos;
	bytes_written = pwrite_rpc(entry, buf, len, entry->pos);
	if (bytes_written > 0)
		entry->pos = oldpos + bytes_written;
	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(bytes_written < 0);

	return bytes_written;

do_real_write:
	return __real_write(fd, buf, len);
}

IOF_PUBLIC ssize_t iof_pwrite(int fd, const void *buf, size_t count,
			      off_t offset)
{
	struct fd_entry *entry;
	ssize_t bytes_written;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_pwrite;

	IOF_LOG_INFO("pwrite(fd=%d." GAH_PRINT_STR ", buf=%p, count=%zu, "
		     "offset=%zd) intercepted, bypass=%s", fd,
		     GAH_PRINT_VAL(entry->common.gah), buf, count, offset,
		     bypass_status[entry->status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_pwrite;

	bytes_written = pwrite_rpc(entry, buf, count, offset);

	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(bytes_written < 0);

	return bytes_written;

do_real_pwrite:
	return __real_pwrite(fd, buf, count, offset);
}

IOF_PUBLIC off_t iof_lseek(int fd, off_t offset, int whence)
{
	struct fd_entry *entry;
	off_t new_offset = -1;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_lseek;

	IOF_LOG_INFO("lseek(fd=%d." GAH_PRINT_STR ", offset=%zd, whence=%d) "
		     "intercepted, bypass=%s", fd,
		     GAH_PRINT_VAL(entry->common.gah), offset, whence,
		     bypass_status[entry->status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_lseek;

	if (whence == SEEK_SET) {
		new_offset = offset;
	} else if (whence == SEEK_CUR) {
		new_offset = entry->pos + offset;
	} else {
		/* Let the system handle SEEK_END as well as non-standard
		 * values such as SEEK_DATA and SEEK_HOLE
		 */
		new_offset = __real_lseek(fd, offset, whence);
		if (new_offset >= 0)
			entry->pos = new_offset;
		goto cleanup;
	}

	if (new_offset < 0) {
		new_offset = (off_t)-1;
		errno = EINVAL;
	} else {
		entry->pos = new_offset;
	}

cleanup:

	SAVE_ERRNO(new_offset < 0);

	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(new_offset < 0);

	return new_offset;

do_real_lseek:
	return __real_lseek(fd, offset, whence);
}

IOF_PUBLIC ssize_t iof_readv(int fd, const struct iovec *vector, int iovcnt)
{
	struct fd_entry *entry;
	ssize_t bytes_read;
	off_t oldpos;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_readv;

	IOF_LOG_INFO("readv(fd=%d." GAH_PRINT_STR ", vector=%p, iovcnt=%d) "
		     "intercepted, bypass=%s", fd,
		     GAH_PRINT_VAL(entry->common.gah), vector, iovcnt,
		     bypass_status[entry->status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_readv;

	oldpos = entry->pos;
	bytes_read = preadv_rpc(entry, vector, iovcnt, entry->pos);
	if (bytes_read > 0)
		entry->pos = oldpos + bytes_read;
	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(bytes_read < 0);

	return bytes_read;

do_real_readv:
	return __real_readv(fd, vector, iovcnt);
}

IOF_PUBLIC ssize_t iof_preadv(int fd, const struct iovec *vector, int iovcnt,
			      off_t offset)
{
	struct fd_entry *entry;
	ssize_t bytes_read;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_preadv;

	IOF_LOG_INFO("preadv(fd=%d." GAH_PRINT_STR ", vector=%p, iovcnt=%d, "
		     "offset=%zd) intercepted, bypass=%s", fd,
		     GAH_PRINT_VAL(entry->common.gah), vector, iovcnt, offset,
		     bypass_status[entry->status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_preadv;

	bytes_read = preadv_rpc(entry, vector, iovcnt, offset);
	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(bytes_read < 0);

	return bytes_read;

do_real_preadv:
	return __real_preadv(fd, vector, iovcnt, offset);
}

IOF_PUBLIC ssize_t iof_writev(int fd, const struct iovec *vector, int iovcnt)
{
	struct fd_entry *entry;
	ssize_t bytes_written;
	off_t oldpos;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_writev;

	IOF_LOG_INFO("writev(fd=%d." GAH_PRINT_STR ", vector=%p, iovcnt=%d) "
		     "intercepted, bypass=%s", fd,
		     GAH_PRINT_VAL(entry->common.gah), vector, iovcnt,
		     bypass_status[entry->status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_writev;

	oldpos = entry->pos;
	bytes_written = pwritev_rpc(entry, vector, iovcnt, entry->pos);
	if (bytes_written > 0)
		entry->pos = oldpos + bytes_written;
	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(bytes_written < 0);

	return bytes_written;

do_real_writev:
	return __real_writev(fd, vector, iovcnt);
}

IOF_PUBLIC ssize_t iof_pwritev(int fd, const struct iovec *vector, int iovcnt,
			       off_t offset)
{
	struct fd_entry *entry;
	ssize_t bytes_written;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_pwritev;

	IOF_LOG_INFO("pwritev(fd=%d." GAH_PRINT_STR ", vector=%p, iovcnt=%d, "
		     "offset=%zd) intercepted, bypass=%s", fd,
		     GAH_PRINT_VAL(entry->common.gah), vector, iovcnt, offset,
		     bypass_status[entry->status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_pwritev;

	bytes_written = pwritev_rpc(entry, vector, iovcnt, offset);

	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(bytes_written < 0);

	return bytes_written;

do_real_pwritev:
	return __real_pwritev(fd, vector, iovcnt, offset);
}

IOF_PUBLIC void *iof_mmap(void *address, size_t length, int prot, int flags,
			  int fd, off_t offset)
{
	struct fd_entry *entry;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc == 0) {
		IOF_LOG_INFO("mmap(address=%p, length=%zu, prot=%d, flags=%d,"
			     " fd=%d." GAH_PRINT_STR ", offset=%zd) "
			     "intercepted, disabling kernel bypass ", address,
			     length, prot, flags, fd,
			     GAH_PRINT_VAL(entry->common.gah), offset);

		if (entry->pos != 0)
			__real_lseek(fd, entry->pos, SEEK_SET);
		/* Disable kernel bypass */
		entry->status = IOF_IO_DIS_MMAP;

		vector_decref(&fd_table, entry);
	}

	return __real_mmap(address, length, prot, flags, fd, offset);
}

IOF_PUBLIC int iof_fsync(int fd)
{
	struct fd_entry *entry;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fsync;

	IOF_LOG_INFO("fsync(fd=%d." GAH_PRINT_STR ") intercepted, bypass=%s",
		     fd, GAH_PRINT_VAL(entry->common.gah),
		     bypass_status[entry->status]);

	vector_decref(&fd_table, entry);

do_real_fsync:
	return __real_fsync(fd);
}

IOF_PUBLIC int iof_fdatasync(int fd)
{
	struct fd_entry *entry;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fdatasync;

	IOF_LOG_INFO("fdatasync(fd=%d." GAH_PRINT_STR ") intercepted, "
		     "bypass=%s", fd, GAH_PRINT_VAL(entry->common.gah),
		     bypass_status[entry->status]);

	vector_decref(&fd_table, entry);

do_real_fdatasync:
	return __real_fdatasync(fd);
}

IOF_PUBLIC int iof_dup(int oldfd)
{
	struct fd_entry *entry = NULL;
	int rc;
	int newfd = __real_dup(oldfd);

	if (newfd == -1)
		return -1;

	rc = vector_dup(&fd_table, oldfd, newfd, &entry);
	if (rc == 0 && entry != NULL) {
		IOF_LOG_INFO("dup(oldfd=%d) = %d." GAH_PRINT_STR
			     " intercepted, bypass=%s", oldfd, newfd,
			     GAH_PRINT_VAL(entry->common.gah),
			     bypass_status[entry->status]);
		vector_decref(&fd_table, entry);
	}

	return newfd;
}

IOF_PUBLIC int iof_dup2(int oldfd, int newfd)
{
	struct fd_entry *entry = NULL;
	int realfd = __real_dup2(oldfd, newfd);
	int rc;

	if (realfd == -1)
		return -1;

	rc = vector_dup(&fd_table, oldfd, realfd, &entry);
	if (rc == 0 && entry != NULL) {
		IOF_LOG_INFO("dup2(oldfd=%d, newfd=%d) = %d." GAH_PRINT_STR
			     " intercepted, bypass=%s", oldfd, newfd,
			     realfd, GAH_PRINT_VAL(entry->common.gah),
			     bypass_status[entry->status]);
		vector_decref(&fd_table, entry);
	}

	return realfd;
}

IOF_PUBLIC FILE * iof_fdopen(int fd, const char *mode)
{
	struct fd_entry *entry;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc == 0) {
		IOF_LOG_INFO("fdopen(fd=%d." GAH_PRINT_STR ", mode=%s) "
			     "intercepted, disabling kernel bypass", fd,
			     GAH_PRINT_VAL(entry->common.gah), mode);

		if (entry->pos != 0)
			__real_lseek(fd, entry->pos, SEEK_SET);

		/* Disable kernel bypass */
		entry->status = IOF_IO_DIS_STREAM;

		vector_decref(&fd_table, entry);
	}

	return __real_fdopen(fd, mode);
}

IOF_PUBLIC int iof_fcntl(int fd, int cmd, ...)
{
	va_list ap;
	void *arg;
	struct fd_entry *entry = NULL;
	int rc;
	int newfd = -1;
	int fdarg;

	va_start(ap, cmd);
	arg = va_arg(ap, void *);
	va_end(ap);

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		return __real_fcntl(fd, cmd, arg);

	if (cmd == F_SETFL) { /* We don't support this flag for interception */
		IOF_LOG_INFO("Removed IOF entry for fd=%d." GAH_PRINT_STR ": "
			     "F_SETFL not supported for kernel bypass", fd,
			     GAH_PRINT_VAL(entry->common.gah));
		if (!drop_reference_if_disabled(entry)) {
			/* Disable kernel bypass */
			entry->status = IOF_IO_DIS_FCNTL;
			vector_decref(&fd_table, entry);
		}
		return __real_fcntl(fd, cmd, arg);
	}

	vector_decref(&fd_table, entry);

	if (cmd != F_DUPFD && cmd != F_DUPFD_CLOEXEC)
		return __real_fcntl(fd, cmd, arg);

	va_start(ap, cmd);
	fdarg = va_arg(ap, int);
	va_end(ap);
	newfd = __real_fcntl(fd, cmd, fdarg);

	if (newfd == -1)
		return newfd;

	/* Ok, newfd is a duplicate of fd */
	rc = vector_dup(&fd_table, fd, newfd, &entry);
	if (rc == 0 && entry != NULL) {
		IOF_LOG_INFO("fcntl(fd=%d." GAH_PRINT_STR ", cmd=%d "
			     "/* F_DUPFD* */, arg=%d) intercepted, bypass=%s",
			     fd, GAH_PRINT_VAL(entry->common.gah), cmd, fdarg,
			     bypass_status[entry->status]);
		vector_decref(&fd_table, entry);
	}

	return newfd;
}

IOF_PUBLIC FILE * iof_fopen(const char *path, const char *mode)
{
	FILE *fp;
	struct fd_entry entry = {0};
	int fd;

	pthread_once(&init_links_flag, init_links);

	fp = __real_fopen(path, mode);

	if (!ioil_initialized || fp == NULL)
		return fp;

	fd = fileno(fp);

	if (fd == -1)
		goto finish;

	if (!check_ioctl_on_open(fd, &entry, O_CREAT | O_WRONLY | O_TRUNC,
				 IOF_IO_DIS_STREAM))
		goto finish;

	IOF_LOG_INFO("fopen(path=%s, mode=%s) = %p(fd=%d." GAH_PRINT_STR
		     ") intercepted, bypass=%s", path, mode, fp, fd,
		     GAH_PRINT_VAL(entry.common.gah),
		     bypass_status[entry.status]);

finish:
	return fp;
}

IOF_PUBLIC FILE * iof_freopen(const char *path, const char *mode, FILE *stream)
{
	FILE *newstream;
	struct fd_entry new_entry = {0};
	struct fd_entry *old_entry = {0};
	int oldfd;
	int newfd;
	int rc;

	if (!ioil_initialized)
		return __real_freopen(path, mode, stream);

	oldfd = fileno(stream);
	if (oldfd == -1)
		return __real_freopen(path, mode, stream);

	newstream = __real_freopen(path, mode, stream);
	if (newstream == NULL)
		return NULL;

	rc = vector_remove(&fd_table, oldfd, &old_entry);

	newfd = fileno(newstream);

	if (newfd == -1 ||
	    !check_ioctl_on_open(newfd, &new_entry, 0, IOF_IO_DIS_STREAM)) {
		if (rc == 0) {
			IOF_LOG_INFO("freopen(path=%s, mode=%s, stream=%p"
				     "(fd=%d." GAH_PRINT_STR ") = %p(fd=%d) "
				     "intercepted, bypass=%s", path, mode,
				     stream, oldfd,
				     GAH_PRINT_VAL(old_entry->common.gah),
				     newstream, newfd,
				     bypass_status[IOF_IO_DIS_STREAM]);
			vector_decref(&fd_table, old_entry);
		}
		return newstream;
	}

	if (rc == 0) {
		IOF_LOG_INFO("freopen(path=%s, mode=%s, stream=%p(fd=%d."
			     GAH_PRINT_STR ") = %p(fd=%d." GAH_PRINT_STR ")"
			     " intercepted, bypass=%s", path, mode, stream,
			     oldfd, GAH_PRINT_VAL(old_entry->common.gah),
			     newstream, newfd,
			     GAH_PRINT_VAL(new_entry.common.gah),
			     bypass_status[IOF_IO_DIS_STREAM]);
		vector_decref(&fd_table, old_entry);
	} else {
		IOF_LOG_INFO("freopen(path=%s, mode=%s, stream=%p(fd=%d)) "
			     "= %p(fd=%d." GAH_PRINT_STR ") intercepted, "
			     "bypass=%s", path, mode, stream, oldfd, newstream,
			     newfd, GAH_PRINT_VAL(new_entry.common.gah),
			     bypass_status[IOF_IO_DIS_STREAM]);
	}

	return newstream;
}

IOF_PUBLIC int iof_fclose(FILE *stream)
{
	struct fd_entry *entry = NULL;
	int fd;
	int rc;

	if (!ioil_initialized)
		goto do_real_fclose;

	fd = fileno(stream);

	if (fd == -1)
		goto do_real_fclose;

	rc = vector_remove(&fd_table, fd, &entry);

	if (rc != 0)
		goto do_real_fclose;

	IOF_LOG_INFO("fclose(stream=%p(fd=%d." GAH_PRINT_STR ")) intercepted, "
		     "bypass=%s", stream, fd, GAH_PRINT_VAL(entry->common.gah),
		     bypass_status[entry->status]);

	vector_decref(&fd_table, entry);

do_real_fclose:
	return __real_fclose(stream);
}

IOF_PUBLIC int iof_get_bypass_status(int fd)
{
	struct fd_entry *entry;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);

	if (rc != 0)
		return IOF_IO_EXTERNAL;

	rc = entry->status;

	vector_decref(&fd_table, entry);

	return rc;
}

FOREACH_INTERCEPT(IOIL_DECLARE_ALIAS)
FOREACH_ALIASED_INTERCEPT(IOIL_DECLARE_ALIAS64)
