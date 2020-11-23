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

#define D_LOGFAC DD_FAC(il)
#include <stdarg.h>
#include <inttypes.h>
#include <libgen.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/resource.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <string.h>

#include "dfuse_log.h"
#include <gurt/list.h>
#include "intercept.h"
#include "dfuse_ioctl.h"
#include "dfuse_vector.h"
#include "dfuse_common.h"

#include "ioil.h"

FOREACH_INTERCEPT(IOIL_FORWARD_DECL)

struct ioil_pool {
	daos_handle_t	iop_poh;
	uuid_t		iop_uuid;
	d_list_t	iop_container_head;
	d_list_t	iop_pools;
};

struct ioil_global {
	pthread_mutex_t	iog_lock;
	d_list_t	iog_pools_head;
	bool		iog_initialized;
	bool		iog_no_daos;
	bool		iog_daos_init;
};

static vector_t	fd_table;

static struct ioil_global ioil_iog;

static __thread int saved_errno;

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

/* Unwind after close or error on container.  Closes container handle
 * and also pool handle if last container is closed.
 *
 */
static void
ioil_shrink(struct ioil_cont *cont)
{
	struct ioil_pool	*pool;
	int			rc;

	if (cont->ioc_open_count != 0)
		return;

	if (cont->ioc_dfs != NULL) {
		rc = dfs_umount(cont->ioc_dfs);
		if (rc != 0)
			D_ERROR("dfs_umount() failed, %d\n", rc);
	}

	if (!daos_handle_is_inval(cont->ioc_coh)) {
		rc = daos_cont_close(cont->ioc_coh, NULL);
		if (rc != 0)
			D_ERROR("daos_cont_close() failed, " DF_RC "\n",
				DP_RC(rc));
	}

	pool = cont->ioc_pool;
	d_list_del(&cont->ioc_containers);
	D_FREE(cont);

	if (!d_list_empty(&pool->iop_container_head))
		return;

	rc = daos_pool_disconnect(pool->iop_poh, NULL);
	if (rc != 0)
		D_ERROR("daos_pool_disconnect() failed, " DF_RC "\n",
			DP_RC(rc));

	d_list_del(&pool->iop_pools);
	D_FREE(pool);
}

static void
entry_array_close(void *arg) {
	struct fd_entry *entry = arg;

	DFUSE_LOG_DEBUG("entry %p closing array fd_count %d",
			entry, entry->fd_cont->ioc_open_count);


	DFUSE_TRA_DOWN(entry->fd_dfsoh);
	dfs_release(entry->fd_dfsoh);

	entry->fd_cont->ioc_open_count -= 1;

	ioil_shrink(entry->fd_cont);
}

static int
ioil_initialize_fd_table(int max_fds)
{
	int rc;

	rc = vector_init(&fd_table, sizeof(struct fd_entry), max_fds,
			 entry_array_close);
	if (rc != 0)
		DFUSE_LOG_ERROR("Could not allocate file descriptor table"
				", disabling kernel bypass: rc = %d", rc);
	return rc;
}

static ssize_t
pread_rpc(struct fd_entry *entry, char *buff, size_t len, off_t offset)
{
	ssize_t bytes_read;
	int errcode;

	/* Just get rpc working then work out how to really do this */
	bytes_read = ioil_do_pread(buff, len, offset, entry, &errcode);
	if (bytes_read < 0)
		saved_errno = errcode;
	return bytes_read;
}

/* Start simple and just loop */
static ssize_t
preadv_rpc(struct fd_entry *entry, const struct iovec *iov, int count,
	   off_t offset)
{
	ssize_t bytes_read;
	int errcode;

	/* Just get rpc working then work out how to really do this */
	bytes_read = ioil_do_preadv(iov, count, offset, entry,
				    &errcode);
	if (bytes_read < 0)
		saved_errno = errcode;
	return bytes_read;
}

static ssize_t
pwrite_rpc(struct fd_entry *entry, const char *buff, size_t len, off_t offset)
{
	ssize_t bytes_written;
	int errcode;

	/* Just get rpc working then work out how to really do this */
	bytes_written = ioil_do_writex(buff, len, offset, entry,
				       &errcode);
	if (bytes_written < 0)
		saved_errno = errcode;

	return bytes_written;
}

/* Start simple and just loop */
static ssize_t
pwritev_rpc(struct fd_entry *entry, const struct iovec *iov, int count,
	    off_t offset)
{
	ssize_t bytes_written;
	int errcode;

	/* Just get rpc working then work out how to really do this */
	bytes_written = ioil_do_pwritev(iov, count, offset, entry,
					&errcode);
	if (bytes_written < 0)
		saved_errno = errcode;

	return bytes_written;
}

static pthread_once_t init_links_flag = PTHREAD_ONCE_INIT;

/* This is also called from dfuse_fopen()
 * Calling anything that can open files in this function can cause deadlock
 * so just do what's necessary for setup, and then return.
 */
static void
init_links(void)
{
	FOREACH_INTERCEPT(IOIL_FORWARD_MAP_OR_FAIL);
}

static __attribute__((constructor)) void
ioil_init(void)
{
	struct rlimit rlimit;
	int rc;

	pthread_once(&init_links_flag, init_links);

	D_INIT_LIST_HEAD(&ioil_iog.iog_pools_head);

	daos_debug_init(DAOS_LOG_DEFAULT);

	DFUSE_TRA_ROOT(&ioil_iog, "il");

	/* Get maximum number of file descriptors */
	rc = getrlimit(RLIMIT_NOFILE, &rlimit);
	if (rc != 0) {
		DFUSE_LOG_ERROR("Could not get process file descriptor limit"
				", disabling kernel bypass");
		printf("Failed\n");
		return;
	}

	rc = ioil_initialize_fd_table(rlimit.rlim_max);
	if (rc != 0) {
		DFUSE_LOG_ERROR("Could not create fd_table, rc = %d,"
				", disabling kernel bypass", rc);
		printf("Failed.\n");
		return;
	}

	rc = pthread_mutex_init(&ioil_iog.iog_lock, NULL);
	if (rc)
		return;

	ioil_iog.iog_initialized = true;
}

static __attribute__((destructor)) void
ioil_fini(void)
{
	struct ioil_pool *pool, *pnext;
	struct ioil_cont *cont, *cnext;

	ioil_iog.iog_initialized = false;

	DFUSE_TRA_DOWN(&ioil_iog);
	vector_destroy(&fd_table);

	d_list_for_each_entry_safe(pool, pnext,
				   &ioil_iog.iog_pools_head, iop_pools) {
		d_list_for_each_entry_safe(cont, cnext,
					   &pool->iop_container_head,
					   ioc_containers) {
			ioil_shrink(cont);
		}
	}

	if (ioil_iog.iog_daos_init)
		daos_fini();
	ioil_iog.iog_daos_init = false;
	daos_debug_fini();
}

/* Get the object handle for the file itself */
static int
fetch_dfs_obj_handle(int fd, struct fd_entry *entry)
{
	struct dfuse_hsd_reply	hsd_reply;
	d_iov_t			iov = {};
	int			cmd;
	int			rc;

	errno = 0;
	rc = ioctl(fd, DFUSE_IOCTL_IL_DSIZE, &hsd_reply);
	if (rc != 0) {
		int err = errno;

		DFUSE_LOG_WARNING("ioctl call on %d failed %d %s", fd,
				  err, strerror(err));

		return err;
	}

	if (hsd_reply.fsr_version != DFUSE_IOCTL_VERSION) {
		DFUSE_LOG_WARNING("ioctl version mismatch (fd=%d): expected "
				  "%d got %d", fd, DFUSE_IOCTL_VERSION,
				  hsd_reply.fsr_version);
		return EIO;
	}

	D_ALLOC(iov.iov_buf, hsd_reply.fsr_dobj_size);
	if (!iov.iov_buf)
		return ENOMEM;

	cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE,
		   DFUSE_IOCTL_REPLY_DOOH, hsd_reply.fsr_dobj_size);

	errno = 0;
	rc = ioctl(fd, cmd, iov.iov_buf);
	if (rc != 0) {
		int err = errno;

		DFUSE_LOG_WARNING("ioctl call on %d failed %d %s", fd,
				  err, strerror(err));

		D_FREE(iov.iov_buf);
		return err;
	}

	iov.iov_buf_len = hsd_reply.fsr_dobj_size;
	iov.iov_len = iov.iov_buf_len;

	rc = dfs_obj_global2local(entry->fd_cont->ioc_dfs,
				  0,
				  iov,
				  &entry->fd_dfsoh);
	if (rc)
		DFUSE_LOG_WARNING("Failed to use dfs object handle %d", rc);

	D_FREE(iov.iov_buf);

	if (entry->fd_dfsoh)
		DFUSE_TRA_UP(entry->fd_dfsoh,
			     entry->fd_cont->ioc_dfs,
			     "open file");

	return rc;
}

/* Connect to a pool and container
 *
 * Pool and container should already be inserted into the lists,
 * container is not open at this point, but pool might be.
 */
static int
ioil_fetch_cont_handles(int fd, struct ioil_cont *cont)
{
	struct ioil_pool       *pool = cont->ioc_pool;
	struct dfuse_hs_reply	hs_reply;
	d_iov_t			iov = {};
	int			cmd;
	int			rc;

	errno = 0;
	rc = ioctl(fd, DFUSE_IOCTL_IL_SIZE, &hs_reply);
	if (rc != 0) {
		int err = errno;

		if (err == EPERM)
			DFUSE_LOG_DEBUG("ioctl call on %d failed %d %s", fd,
					err, strerror(err));
		else
			DFUSE_LOG_WARNING("ioctl call on %d failed %d %s", fd,
					  err, strerror(err));

		return err;
	}

	if (hs_reply.fsr_version != DFUSE_IOCTL_VERSION) {
		DFUSE_LOG_WARNING("ioctl version mismatch (fd=%d): expected "
				  "%d got %d", fd, DFUSE_IOCTL_VERSION,
				  hs_reply.fsr_version);
		return EIO;
	}

	DFUSE_LOG_DEBUG("ioctl returned %zi %zi",
			hs_reply.fsr_pool_size,
			hs_reply.fsr_cont_size);

	if (daos_handle_is_inval(pool->iop_poh)) {
		D_ALLOC(iov.iov_buf, hs_reply.fsr_pool_size);
		if (!iov.iov_buf)
			return ENOMEM;

		cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE,
			   DFUSE_IOCTL_REPLY_POH, hs_reply.fsr_pool_size);

		errno = 0;
		rc = ioctl(fd, cmd, iov.iov_buf);
		if (rc != 0) {
			int err = errno;

			DFUSE_LOG_WARNING("ioctl call on %d failed %d %s", fd,
					  err, strerror(err));

			D_FREE(iov.iov_buf);
			return err;
		}

		iov.iov_buf_len = hs_reply.fsr_pool_size;
		iov.iov_len = iov.iov_buf_len;

		rc = daos_pool_global2local(iov, &pool->iop_poh);
		D_FREE(iov.iov_buf);
		if (rc) {
			DFUSE_LOG_WARNING("Failed to use pool handle " DF_RC,
					  DP_RC(rc));
			return daos_der2errno(rc);
		}
	}

	D_ALLOC(iov.iov_buf, hs_reply.fsr_cont_size);
	if (!iov.iov_buf)
		return ENOMEM;

	cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE,
		   DFUSE_IOCTL_REPLY_COH, hs_reply.fsr_cont_size);

	errno = 0;
	rc = ioctl(fd, cmd, iov.iov_buf);
	if (rc != 0) {
		int err = errno;

		DFUSE_LOG_WARNING("ioctl call on %d failed %d %s", fd,
				  err, strerror(err));

		D_FREE(iov.iov_buf);
		return err;
	}

	iov.iov_buf_len = hs_reply.fsr_cont_size;
	iov.iov_len = iov.iov_buf_len;

	rc = daos_cont_global2local(pool->iop_poh, iov, &cont->ioc_coh);
	if (rc) {
		DFUSE_LOG_WARNING("Failed to use cont handle " DF_RC,
				  DP_RC(rc));
		D_FREE(iov.iov_buf);
		return daos_der2errno(rc);
	}

	D_FREE(iov.iov_buf);

	D_ALLOC(iov.iov_buf, hs_reply.fsr_dfs_size);
	if (!iov.iov_buf)
		return ENOMEM;
	cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE,
		   DFUSE_IOCTL_REPLY_DOH, hs_reply.fsr_dfs_size);

	errno = 0;
	rc = ioctl(fd, cmd, iov.iov_buf);
	if (rc != 0) {
		int err = errno;

		DFUSE_LOG_WARNING("ioctl call on %d failed %d %s", fd,
				  err, strerror(err));

		D_FREE(iov.iov_buf);
		return err;
	}

	iov.iov_buf_len = hs_reply.fsr_dfs_size;
	iov.iov_len = iov.iov_buf_len;

	rc = dfs_global2local(pool->iop_poh,
			      cont->ioc_coh,
			      0,
			      iov, &cont->ioc_dfs);
	if (rc) {
		DFUSE_LOG_WARNING("Failed to use dfs handle %d", rc);
		D_FREE(iov.iov_buf);
		return rc;
	}

	DFUSE_TRA_UP(cont->ioc_dfs, &ioil_iog, "dfs");
	D_FREE(iov.iov_buf);

	return 0;
}

static bool
ioil_open_cont_handles(int fd, struct dfuse_il_reply *il_reply,
		       struct ioil_cont *cont)
{
	int			rc;
	struct ioil_pool       *pool = cont->ioc_pool;

	if (daos_handle_is_inval(pool->iop_poh)) {
		rc = daos_pool_connect(il_reply->fir_pool, NULL, NULL,
				       DAOS_PC_RW, &pool->iop_poh, NULL, NULL);
		if (rc)
			return false;
	}

	rc = daos_cont_open(pool->iop_poh, il_reply->fir_cont, DAOS_COO_RW,
			    &cont->ioc_coh, NULL, NULL);
	if (rc)
		return false;

	rc = dfs_mount(pool->iop_poh, cont->ioc_coh, O_RDWR,
		       &cont->ioc_dfs);
	if (rc)
		return false;

	DFUSE_TRA_UP(cont->ioc_dfs, &ioil_iog, "dfs");

	return true;
}

static bool
check_ioctl_on_open(int fd, struct fd_entry *entry, int flags, int status)
{
	struct dfuse_il_reply	il_reply;
	int			rc;
	struct ioil_pool	*pool;
	struct ioil_cont	*cont;
	bool			pool_alloc = false;

	if (ioil_iog.iog_no_daos) {
		DFUSE_LOG_DEBUG("daos_init() has previously failed");
		return false;
	}

	errno = 0;
	rc = ioctl(fd, DFUSE_IOCTL_IL, &il_reply);
	if (rc != 0) {
		int err = errno;

		DFUSE_LOG_DEBUG("ioctl call on %d failed %d %s", fd,
				err, strerror(err));
		return false;
	}

	if (il_reply.fir_version != DFUSE_IOCTL_VERSION) {
		DFUSE_LOG_WARNING("ioctl version mismatch (fd=%d): expected "
				  "%d got %d", fd, DFUSE_IOCTL_VERSION,
				  il_reply.fir_version);
		return false;
	}

	rc = pthread_mutex_lock(&ioil_iog.iog_lock);
	D_ASSERT(rc == 0);

	if (!ioil_iog.iog_daos_init) {
		rc = daos_init();
		if (rc) {
			DFUSE_LOG_DEBUG("daos_init() failed, " DF_RC,
					DP_RC(rc));
			ioil_iog.iog_no_daos = true;
			return false;
		}
		ioil_iog.iog_daos_init = true;
	}

	d_list_for_each_entry(pool, &ioil_iog.iog_pools_head, iop_pools) {
		if (uuid_compare(pool->iop_uuid, il_reply.fir_pool) != 0)
			continue;

		d_list_for_each_entry(cont, &pool->iop_container_head,
				      ioc_containers) {
			if (uuid_compare(cont->ioc_uuid,
					 il_reply.fir_cont) != 0)
				continue;

			D_GOTO(get_file, 0);
		}
		D_GOTO(open_cont, 0);
	}

	/* Allocate data for pool */
	D_ALLOC_PTR(pool);
	if (pool == NULL)
		D_GOTO(err, 0);

	pool_alloc = true;
	uuid_copy(pool->iop_uuid, il_reply.fir_pool);
	D_INIT_LIST_HEAD(&pool->iop_container_head);

open_cont:

	D_ALLOC_PTR(cont);
	if (cont == NULL) {
		if (pool_alloc)
			D_FREE(pool);
		D_GOTO(err, 0);
	}

	cont->ioc_pool = pool;
	uuid_copy(cont->ioc_uuid, il_reply.fir_cont);
	d_list_add(&cont->ioc_containers, &pool->iop_container_head);

	if (pool_alloc)
		d_list_add(&pool->iop_pools, &ioil_iog.iog_pools_head);

	rc = ioil_fetch_cont_handles(fd, cont);
	if (rc == EPERM) {
		bool rcb;

		DFUSE_LOG_DEBUG("ioil_fetch_cont_handles() failed, backing off");

		rcb = ioil_open_cont_handles(fd, &il_reply, cont);
		if (!rcb) {
			DFUSE_LOG_DEBUG("ioil_open_cont_handles() failed");
			D_GOTO(shrink, 0);
		}
	} else if (rc != 0) {
		D_ERROR("ioil_fetch_cont_handles() failed, %d\n", rc);
		D_GOTO(shrink, 0);
	}

get_file:
	entry->fd_pos = 0;
	entry->fd_flags = flags;
	entry->fd_status = DFUSE_IO_BYPASS;
	entry->fd_cont = cont;

	/* Now open the file object to allow read/write */
	rc = fetch_dfs_obj_handle(fd, entry);
	if (rc)
		D_GOTO(shrink, 0);

	rc = vector_set(&fd_table, fd, entry);
	if (rc != 0) {
		DFUSE_LOG_DEBUG("Failed to track IOF file fd=%d., disabling kernel bypass",
				rc);
		/* Disable kernel bypass */
		entry->fd_status = DFUSE_IO_DIS_RSRC;
		D_GOTO(obj_close, 0);
	}

	DFUSE_LOG_DEBUG("Added entry for new fd %d", fd);

	cont->ioc_open_count += 1;

	pthread_mutex_unlock(&ioil_iog.iog_lock);

	return true;

obj_close:
	dfs_release(entry->fd_dfsoh);

shrink:
	ioil_shrink(cont);

err:
	rc = pthread_mutex_unlock(&ioil_iog.iog_lock);
	D_ASSERT(rc == 0);
	return false;
}

static bool
drop_reference_if_disabled(struct fd_entry *entry)
{
	if (entry->fd_status == DFUSE_IO_BYPASS)
		return false;

	vector_decref(&fd_table, entry);

	return true;
}

DFUSE_PUBLIC int
dfuse_open(const char *pathname, int flags, ...)
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
		mode = 0;
	}

	if (!ioil_iog.iog_initialized || (fd == -1))
		return fd;

	status = DFUSE_IO_BYPASS;
	/* Disable bypass for O_APPEND|O_PATH */
	if ((flags & (O_PATH | O_APPEND)) != 0)
		status = DFUSE_IO_DIS_FLAG;

	if (!check_ioctl_on_open(fd, &entry, flags, status)) {
		DFUSE_LOG_DEBUG("open(pathname=%s) interception not possible",
				pathname);
		goto finish;
	}

	if (flags & O_CREAT)
		DFUSE_LOG_DEBUG("open(pathname=%s, flags=0%o, mode=0%o) = "
				"%d. intercepted, bypass=%s",
				pathname, flags, mode, fd,
				bypass_status[entry.fd_status]);
	else
		DFUSE_LOG_DEBUG("open(pathname=%s, flags=0%o) = %d. intercepted, bypass=%s",
				pathname, flags, fd,
				bypass_status[entry.fd_status]);

finish:
	return fd;
}

DFUSE_PUBLIC int
dfuse_creat(const char *pathname, mode_t mode)
{
	struct fd_entry entry = {0};
	int fd;

	/* Same as open with O_CREAT|O_WRONLY|O_TRUNC */
	fd = __real_open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);

	if (!ioil_iog.iog_initialized || (fd == -1))
		return fd;

	if (!check_ioctl_on_open(fd, &entry, O_CREAT | O_WRONLY | O_TRUNC,
				 DFUSE_IO_BYPASS))
		goto finish;

	DFUSE_LOG_DEBUG("creat(pathname=%s, mode=0%o) = %d. intercepted, bypass=%s",
			pathname, mode, fd, bypass_status[entry.fd_status]);

finish:
	return fd;
}

DFUSE_PUBLIC int
dfuse_close(int fd)
{
	struct fd_entry *entry;
	int rc;

	rc = vector_remove(&fd_table, fd, &entry);

	if (rc != 0)
		goto do_real_close;

	DFUSE_LOG_DEBUG("close(fd=%d) intercepted, bypass=%s",
			fd, bypass_status[entry->fd_status]);

	/* This will drop a reference which will cause the array to be closed
	 * when the last duplicated fd is closed
	 */
	vector_decref(&fd_table, entry);

do_real_close:
	return __real_close(fd);
}

DFUSE_PUBLIC ssize_t
dfuse_read(int fd, void *buf, size_t len)
{
	struct fd_entry *entry;
	ssize_t bytes_read;
	off_t oldpos;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_read;

	DFUSE_LOG_DEBUG("read(fd=%d, buf=%p, len=%zu) "
			"intercepted, bypass=%s", fd,
			buf, len,
			bypass_status[entry->fd_status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_read;

	oldpos = entry->fd_pos;
	bytes_read = pread_rpc(entry, buf, len, oldpos);
	if (bytes_read > 0)
		entry->fd_pos = oldpos + bytes_read;
	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(bytes_read < 0);

	return bytes_read;

do_real_read:
	return __real_read(fd, buf, len);
}

DFUSE_PUBLIC ssize_t
dfuse_pread(int fd, void *buf, size_t count, off_t offset)
{
	struct fd_entry *entry;
	ssize_t bytes_read;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_pread;

	DFUSE_LOG_DEBUG("pread(fd=%d, buf=%p, count=%zu, "
			"offset=%zd) intercepted, bypass=%s", fd,
			buf, count, offset,
			bypass_status[entry->fd_status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_pread;

	bytes_read = pread_rpc(entry, buf, count, offset);

	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(bytes_read < 0);

	return bytes_read;

do_real_pread:
	return __real_pread(fd, buf, count, offset);
}

DFUSE_PUBLIC ssize_t
dfuse_write(int fd, const void *buf, size_t len)
{
	struct fd_entry *entry;
	ssize_t bytes_written;
	off_t oldpos;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_write;

	DFUSE_LOG_DEBUG("write(fd=%d, buf=%p, len=%zu) "
			"intercepted, bypass=%s", fd,
			buf, len, bypass_status[entry->fd_status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_write;

	oldpos = entry->fd_pos;
	bytes_written = pwrite_rpc(entry, buf, len, entry->fd_pos);
	if (bytes_written > 0)
		entry->fd_pos = oldpos + bytes_written;
	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(bytes_written < 0);

	return bytes_written;

do_real_write:
	return __real_write(fd, buf, len);
}

DFUSE_PUBLIC ssize_t
dfuse_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
	struct fd_entry *entry;
	ssize_t bytes_written;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_pwrite;

	DFUSE_LOG_DEBUG("pwrite(fd=%d, buf=%p, count=%zu, "
			"offset=%zd) intercepted, bypass=%s", fd,
			buf, count, offset,
			bypass_status[entry->fd_status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_pwrite;

	bytes_written = pwrite_rpc(entry, buf, count, offset);

	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(bytes_written < 0);

	return bytes_written;

do_real_pwrite:
	return __real_pwrite(fd, buf, count, offset);
}

DFUSE_PUBLIC off_t
dfuse_lseek(int fd, off_t offset, int whence)
{
	struct fd_entry *entry;
	off_t new_offset = -1;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_lseek;

	DFUSE_LOG_DEBUG("lseek(fd=%d, offset=%zd, whence=%d) "
			"intercepted, bypass=%s",
			fd, offset, whence, bypass_status[entry->fd_status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_lseek;

	if (whence == SEEK_SET) {
		new_offset = offset;
	} else if (whence == SEEK_CUR) {
		new_offset = entry->fd_pos + offset;
	} else {
		/* Let the system handle SEEK_END as well as non-standard
		 * values such as SEEK_DATA and SEEK_HOLE
		 */
		new_offset = __real_lseek(fd, offset, whence);
		if (new_offset >= 0)
			entry->fd_pos = new_offset;
		goto cleanup;
	}

	if (new_offset < 0) {
		new_offset = (off_t)-1;
		errno = EINVAL;
	} else {
		entry->fd_pos = new_offset;
	}

cleanup:

	SAVE_ERRNO(new_offset < 0);

	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(new_offset < 0);

	return new_offset;

do_real_lseek:
	return __real_lseek(fd, offset, whence);
}

DFUSE_PUBLIC ssize_t
dfuse_readv(int fd, const struct iovec *vector, int iovcnt)
{
	struct fd_entry *entry;
	ssize_t bytes_read;
	off_t oldpos;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_readv;

	DFUSE_LOG_DEBUG("readv(fd=%d, vector=%p, iovcnt=%d) "
			"intercepted, bypass=%s",
			fd, vector, iovcnt, bypass_status[entry->fd_status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_readv;

	oldpos = entry->fd_pos;
	bytes_read = preadv_rpc(entry, vector, iovcnt, entry->fd_pos);
	if (bytes_read > 0)
		entry->fd_pos = oldpos + bytes_read;
	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(bytes_read < 0);

	return bytes_read;

do_real_readv:
	return __real_readv(fd, vector, iovcnt);
}

DFUSE_PUBLIC ssize_t
dfuse_preadv(int fd, const struct iovec *vector, int iovcnt, off_t offset)
{
	struct fd_entry *entry;
	ssize_t bytes_read;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_preadv;

	DFUSE_LOG_DEBUG("preadv(fd=%d, vector=%p, iovcnt=%d, "
			"offset=%zd) intercepted, bypass=%s", fd, vector,
			iovcnt, offset, bypass_status[entry->fd_status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_preadv;

	bytes_read = preadv_rpc(entry, vector, iovcnt, offset);
	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(bytes_read < 0);

	return bytes_read;

do_real_preadv:
	return __real_preadv(fd, vector, iovcnt, offset);
}

DFUSE_PUBLIC ssize_t
dfuse_writev(int fd, const struct iovec *vector, int iovcnt)
{
	struct fd_entry *entry;
	ssize_t bytes_written;
	off_t oldpos;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_writev;

	DFUSE_LOG_DEBUG("writev(fd=%d, vector=%p, iovcnt=%d) "
			"intercepted, bypass=%s",
			fd, vector, iovcnt, bypass_status[entry->fd_status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_writev;

	oldpos = entry->fd_pos;
	bytes_written = pwritev_rpc(entry, vector, iovcnt, entry->fd_pos);
	if (bytes_written > 0)
		entry->fd_pos = oldpos + bytes_written;
	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(bytes_written < 0);

	return bytes_written;

do_real_writev:
	return __real_writev(fd, vector, iovcnt);
}

DFUSE_PUBLIC ssize_t
dfuse_pwritev(int fd, const struct iovec *vector, int iovcnt, off_t offset)
{
	struct fd_entry *entry;
	ssize_t bytes_written;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_pwritev;

	DFUSE_LOG_DEBUG("pwritev(fd=%d, vector=%p, iovcnt=%d, "
			"offset=%zd) intercepted, bypass=%s",
			fd, vector, iovcnt, offset,
			bypass_status[entry->fd_status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_pwritev;

	bytes_written = pwritev_rpc(entry, vector, iovcnt, offset);

	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(bytes_written < 0);

	return bytes_written;

do_real_pwritev:
	return __real_pwritev(fd, vector, iovcnt, offset);
}

DFUSE_PUBLIC void *
dfuse_mmap(void *address, size_t length, int prot, int flags, int fd,
	   off_t offset)
{
	struct fd_entry *entry;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc == 0) {
		DFUSE_LOG_DEBUG("mmap(address=%p, length=%zu, prot=%d, flags=%d,"
				" fd=%d, offset=%zd) "
				"intercepted, disabling kernel bypass ", address,
				length, prot, flags, fd, offset);

		if (entry->fd_pos != 0)
			__real_lseek(fd, entry->fd_pos, SEEK_SET);
		/* Disable kernel bypass */
		entry->fd_status = DFUSE_IO_DIS_MMAP;

		vector_decref(&fd_table, entry);
	}

	return __real_mmap(address, length, prot, flags, fd, offset);
}

DFUSE_PUBLIC int
dfuse_fsync(int fd)
{
	struct fd_entry *entry;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fsync;

	DFUSE_LOG_DEBUG("fsync(fd=%d) intercepted, bypass=%s",
			fd, bypass_status[entry->fd_status]);

	vector_decref(&fd_table, entry);

do_real_fsync:
	return __real_fsync(fd);
}

DFUSE_PUBLIC int
dfuse_fdatasync(int fd)
{
	struct fd_entry *entry;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fdatasync;

	DFUSE_LOG_DEBUG("fdatasync(fd=%d) intercepted, bypass=%s",
			fd, bypass_status[entry->fd_status]);

	vector_decref(&fd_table, entry);

do_real_fdatasync:
	return __real_fdatasync(fd);
}

DFUSE_PUBLIC int dfuse_dup(int oldfd)
{
	struct fd_entry *entry = NULL;
	int rc;
	int newfd = __real_dup(oldfd);

	if (newfd == -1)
		return -1;

	rc = vector_dup(&fd_table, oldfd, newfd, &entry);
	if (rc == 0 && entry != NULL) {
		DFUSE_LOG_DEBUG("dup(oldfd=%d) = %d intercepted, bypass=%s",
				oldfd, newfd, bypass_status[entry->fd_status]);
		vector_decref(&fd_table, entry);
	}

	return newfd;
}

DFUSE_PUBLIC int
dfuse_dup2(int oldfd, int newfd)
{
	struct fd_entry *entry = NULL;
	int realfd = __real_dup2(oldfd, newfd);
	int rc;

	if (realfd == -1)
		return -1;

	rc = vector_dup(&fd_table, oldfd, realfd, &entry);
	if (rc == 0 && entry != NULL) {
		DFUSE_LOG_DEBUG("dup2(oldfd=%d, newfd=%d) = %d."
				" intercepted, bypass=%s", oldfd, newfd,
				realfd, bypass_status[entry->fd_status]);
		vector_decref(&fd_table, entry);
	}

	return realfd;
}

DFUSE_PUBLIC FILE *
dfuse_fdopen(int fd, const char *mode)
{
	struct fd_entry *entry;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc == 0) {
		DFUSE_LOG_DEBUG("fdopen(fd=%d, mode=%s) intercepted, disabling kernel bypass",
				fd, mode);

		if (entry->fd_pos != 0)
			__real_lseek(fd, entry->fd_pos, SEEK_SET);

		/* Disable kernel bypass */
		entry->fd_status = DFUSE_IO_DIS_STREAM;

		vector_decref(&fd_table, entry);
	}

	return __real_fdopen(fd, mode);
}

DFUSE_PUBLIC int
dfuse_fcntl(int fd, int cmd, ...)
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
		DFUSE_LOG_DEBUG("Removed IL entry for fd=%d: "
				"F_SETFL not supported for kernel bypass", fd);

		if (!drop_reference_if_disabled(entry)) {
			/* Disable kernel bypass */
			entry->fd_status = DFUSE_IO_DIS_FCNTL;
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
		DFUSE_LOG_DEBUG("fcntl(fd=%d, cmd=%d "
				"/* F_DUPFD* */, arg=%d) intercepted, bypass=%s",
				fd, cmd, fdarg,
				bypass_status[entry->fd_status]);
		vector_decref(&fd_table, entry);
	}

	return newfd;
}

DFUSE_PUBLIC FILE *
dfuse_fopen(const char *path, const char *mode)
{
	FILE *fp;
	struct fd_entry entry = {0};
	int fd;

	pthread_once(&init_links_flag, init_links);

	fp = __real_fopen(path, mode);

	if (!ioil_iog.iog_initialized || fp == NULL)
		return fp;

	fd = fileno(fp);

	if (fd == -1)
		goto finish;

	if (!check_ioctl_on_open(fd, &entry, O_CREAT | O_WRONLY | O_TRUNC,
				 DFUSE_IO_DIS_STREAM))
		goto finish;

	DFUSE_LOG_DEBUG("fopen(path=%s, mode=%s) = %p(fd=%d) intercepted, bypass=%s",
			path, mode, fp, fd, bypass_status[entry.fd_status]);

finish:
	return fp;
}

DFUSE_PUBLIC FILE *
dfuse_freopen(const char *path, const char *mode, FILE *stream)
{
	FILE *newstream;
	struct fd_entry new_entry = {0};
	struct fd_entry *old_entry = {0};
	int oldfd;
	int newfd;
	int rc;

	if (!ioil_iog.iog_initialized)
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
	    !check_ioctl_on_open(newfd, &new_entry, 0, DFUSE_IO_DIS_STREAM)) {
		if (rc == 0) {
			DFUSE_LOG_DEBUG("freopen(path=%s, mode=%s, stream=%p"
					"(fd=%d) = %p(fd=%d) "
					"intercepted, bypass=%s", path, mode,
					stream, oldfd,
					newstream, newfd,
					bypass_status[DFUSE_IO_DIS_STREAM]);
			vector_decref(&fd_table, old_entry);
		}
		return newstream;
	}

	if (rc == 0) {
		DFUSE_LOG_DEBUG("freopen(path=%s, mode=%s, stream=%p(fd=%d) = %p(fd=%d)"
				" intercepted, bypass=%s", path, mode, stream,
				oldfd, newstream, newfd,
				bypass_status[DFUSE_IO_DIS_STREAM]);
		vector_decref(&fd_table, old_entry);
	} else {
		DFUSE_LOG_DEBUG("freopen(path=%s, mode=%s, stream=%p(fd=%d)) "
				"= %p(fd=%d) intercepted, "
				"bypass=%s", path, mode, stream, oldfd,
				newstream, newfd,
				bypass_status[DFUSE_IO_DIS_STREAM]);
	}

	return newstream;
}

DFUSE_PUBLIC int
dfuse_fclose(FILE *stream)
{
	struct fd_entry *entry = NULL;
	int fd;
	int rc;

	if (!ioil_iog.iog_initialized)
		goto do_real_fclose;

	fd = fileno(stream);

	if (fd == -1)
		goto do_real_fclose;

	rc = vector_remove(&fd_table, fd, &entry);

	if (rc != 0)
		goto do_real_fclose;

	DFUSE_LOG_DEBUG("fclose(stream=%p(fd=%d)) intercepted, "
			"bypass=%s", stream, fd,
			bypass_status[entry->fd_status]);

	vector_decref(&fd_table, entry);

do_real_fclose:
	return __real_fclose(stream);
}

DFUSE_PUBLIC int
dfuse_get_bypass_status(int fd)
{
	struct fd_entry *entry;
	int rc;

	rc = vector_get(&fd_table, fd, &entry);

	if (rc != 0)
		return DFUSE_IO_EXTERNAL;

	rc = entry->fd_status;

	vector_decref(&fd_table, entry);

	return rc;
}

FOREACH_INTERCEPT(IOIL_DECLARE_ALIAS)
FOREACH_ALIASED_INTERCEPT(IOIL_DECLARE_ALIAS64)
