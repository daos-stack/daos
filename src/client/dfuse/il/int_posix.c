/**
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(il)
#include <stdarg.h>
#include <inttypes.h>
#include <libgen.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <sys/time.h>
#include <sys/resource.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <string.h>

#include "dfuse_log.h"
#include <gurt/list.h>
#include <gurt/atomic.h>
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
	pid_t           iog_init_tid;
	bool		iog_initialized;
	bool		iog_no_daos;
	bool		iog_daos_init;

	bool		iog_show_summary;	/**< Should a summary be shown at teardown */

	unsigned	iog_report_count;	/**< Number of operations that should be logged */

	ATOMIC uint64_t iog_file_count;  /**< Number of file opens intercepted */
	ATOMIC uint64_t iog_read_count;  /**< Number of read operations intercepted */
	ATOMIC uint64_t iog_write_count; /**< Number of write operations intercepted */
	ATOMIC uint64_t iog_fstat_count; /**< Number of fstat operations intercepted */
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

static const char *const bypass_status[] = {
    "external", "on", "off-mmap", "off-flag", "off-fcntl", "off-stream", "off-rsrc", "off-ioerr",
};

/* Unwind after close or error on container.  Closes container handle
 * and also pool handle if last container is closed.
 *
 * ioil_shrink_pool() is only used in ioil_fini() where stale pools
 * have been left open, for example if there are problems on close.
 */

static int
ioil_shrink_pool(struct ioil_pool *pool)
{
	if (daos_handle_is_valid(pool->iop_poh)) {
		int rc;

		rc = daos_pool_disconnect(pool->iop_poh, NULL);
		if (rc != 0) {
			D_ERROR("daos_pool_disconnect() failed, " DF_RC "\n", DP_RC(rc));
			return rc;
		}
		pool->iop_poh = DAOS_HDL_INVAL;
	}
	d_list_del(&pool->iop_pools);
	D_FREE(pool);
	return 0;
}

static int
ioil_shrink_cont(struct ioil_cont *cont, bool shrink_pool, bool force)
{
	struct ioil_pool	*pool;
	int			rc;

	if (cont->ioc_open_count != 0 && !force)
		return 0;

	if (cont->ioc_dfs != NULL) {
		DFUSE_TRA_DOWN(cont->ioc_dfs);
		rc = dfs_umount(cont->ioc_dfs);
		if (rc != 0) {
			D_ERROR("dfs_umount() failed, %d\n", rc);
			return rc;
		}
		cont->ioc_dfs = NULL;
	}

	if (daos_handle_is_valid(cont->ioc_coh)) {
		rc = daos_cont_close(cont->ioc_coh, NULL);
		if (rc != 0) {
			D_ERROR("daos_cont_close() failed, " DF_RC "\n", DP_RC(rc));
			return rc;
		}
		cont->ioc_coh = DAOS_HDL_INVAL;
	}

	pool = cont->ioc_pool;
	d_list_del(&cont->ioc_containers);
	D_FREE(cont);

	if (!shrink_pool)
		return 0;

	if (!d_list_empty(&pool->iop_container_head))
		return 0;

	return ioil_shrink_pool(pool);
}

static void
entry_array_close(void *arg) {
	struct fd_entry *entry = arg;

	DFUSE_TRA_DOWN(entry->fd_dfsoh);
	dfs_release(entry->fd_dfsoh);
	entry->fd_cont->ioc_open_count -= 1;

	/* Do not close container/pool handles at this point
	 * to allow for re-use.
	 * ioil_shrink_cont(entry->fd_cont, true, true);
	*/
}

static int
ioil_initialize_fd_table(int max_fds)
{
	int rc;

	rc = vector_init(&fd_table, sizeof(struct fd_entry), max_fds,
			 entry_array_close);
	if (rc != 0)
		DFUSE_LOG_ERROR("Could not allocate file descriptor table"
				", disabling kernel bypass: rc = "DF_RC,
				DP_RC(rc));
	return rc;
}

static ssize_t
pread_rpc(struct fd_entry *entry, char *buff, size_t len, off_t offset)
{
	ssize_t bytes_read;
	int errcode;
	int counter;

	counter = atomic_fetch_add_relaxed(&ioil_iog.iog_read_count, 1);

	if (counter < ioil_iog.iog_report_count)
		__real_fprintf(stderr, "[libioil] Intercepting read of size %zi\n", len);

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
	int counter;

	counter = atomic_fetch_add_relaxed(&ioil_iog.iog_read_count, 1);

	if (counter < ioil_iog.iog_report_count)
		__real_fprintf(stderr, "[libioil] Intercepting read\n");

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
	int counter;

	counter = atomic_fetch_add_relaxed(&ioil_iog.iog_write_count, 1);

	if (counter < ioil_iog.iog_report_count)
		__real_fprintf(stderr, "[libioil] Intercepting write of size %zi\n", len);

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
	int counter;

	counter = atomic_fetch_add_relaxed(&ioil_iog.iog_write_count, 1);

	if (counter < ioil_iog.iog_report_count)
		__real_fprintf(stderr, "[libioil] Intercepting write\n");

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
	uint64_t report_count = 0;

	pthread_once(&init_links_flag, init_links);

	D_INIT_LIST_HEAD(&ioil_iog.iog_pools_head);

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc)
		ioil_iog.iog_no_daos = true;

	DFUSE_TRA_ROOT(&ioil_iog, "il");

	ioil_iog.iog_init_tid = syscall(SYS_gettid);

	/* Get maximum number of file descriptors */
	rc = getrlimit(RLIMIT_NOFILE, &rlimit);
	if (rc != 0) {
		DFUSE_LOG_ERROR("Could not get process file descriptor limit"
				", disabling kernel bypass");
		return;
	}

	/* Check what progress to report on.  If the env is set but could not be
	 * parsed then just show the summary (report_count will be 0).
	 */
	rc = d_getenv_uint64_t("D_IL_REPORT", &report_count);
	if (rc != -DER_NONEXIST) {
		ioil_iog.iog_show_summary = true;
		ioil_iog.iog_report_count = report_count;
	}

	rc = ioil_initialize_fd_table(rlimit.rlim_max);
	if (rc != 0) {
		DFUSE_LOG_ERROR("Could not create fd_table, "
				"disabling kernel bypass, rc = "DF_RC,
				DP_RC(rc));
		return;
	}

	rc = pthread_mutex_init(&ioil_iog.iog_lock, NULL);
	if (rc)
		return;

	ioil_iog.iog_initialized = true;
}

static void
ioil_show_summary()
{
	D_INFO("Performed %"PRIu64" reads and %"PRIu64" writes from %"PRIu64" files\n",
	       ioil_iog.iog_read_count, ioil_iog.iog_write_count, ioil_iog.iog_file_count);

	if (ioil_iog.iog_file_count == 0 || !ioil_iog.iog_show_summary)
		return;

	__real_fprintf(stderr,
		       "[libioil] Performed %" PRIu64 " reads and %" PRIu64 " writes from %" PRIu64
		       " files\n",
		       ioil_iog.iog_read_count, ioil_iog.iog_write_count, ioil_iog.iog_file_count);
}

static __attribute__((destructor)) void
ioil_fini(void)
{
	struct ioil_pool *pool, *pnext;
	struct ioil_cont *cont, *cnext;
	int               rc;
	pid_t             tid = syscall(SYS_gettid);

	if (tid != ioil_iog.iog_init_tid) {
		DFUSE_TRA_INFO(&ioil_iog, "Ignoring destructor from alternate thread");
		return;
	}

	ioil_iog.iog_initialized = false;

	DFUSE_TRA_DOWN(&ioil_iog);
	vector_destroy(&fd_table);

	ioil_show_summary();

	/* Tidy up any open connections */
	d_list_for_each_entry_safe(pool, pnext,
				   &ioil_iog.iog_pools_head, iop_pools) {
		d_list_for_each_entry_safe(cont, cnext,
					   &pool->iop_container_head,
					   ioc_containers) {
			/* Retry disconnect on out of memory errors, this is mainly for fault
			 * injection testing.  Do not attempt to shrink the pool here as that
			 * is tried later, and if the container close succeeds but pool close
			 * fails the cont may not be valid afterwards.
			 */
			rc = ioil_shrink_cont(cont, false, true);
			if (rc == -DER_NOMEM)
				ioil_shrink_cont(cont, false, true);
		}
		rc = ioil_shrink_pool(pool);
		if (rc == -DER_NOMEM)
			ioil_shrink_pool(pool);
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

		if (errno != EISDIR)
			DFUSE_LOG_WARNING("ioctl call on %d failed %d %s", fd, err, strerror(err));
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
		rc = errno;

		DFUSE_LOG_WARNING("ioctl call on %d failed: %d (%s)", fd, rc, strerror(rc));
		D_FREE(iov.iov_buf);
		return rc;
	}

	iov.iov_buf_len = hsd_reply.fsr_dobj_size;
	iov.iov_len = iov.iov_buf_len;

	rc = dfs_obj_global2local(entry->fd_cont->ioc_dfs,
				  0,
				  iov,
				  &entry->fd_dfsoh);
	if (rc)
		DFUSE_LOG_WARNING("Failed to use dfs object handle: %d (%s)", rc, strerror(rc));

	D_FREE(iov.iov_buf);

	if (entry->fd_dfsoh)
		DFUSE_TRA_UP(entry->fd_dfsoh,
			     entry->fd_cont->ioc_dfs,
			     "open file");

	return rc;
}

#define NAME_LEN 128

/* Connect to a pool, helper function for ioil_fetch_cont_handles().
 *
 * Fetch the pool open handle for a pool from a fd, do this either
 * via ioctl if possible, or if not via a file in /tmp.
 */
static int
ioil_fetch_pool_handle(int fd, struct dfuse_hs_reply *hs_reply,
		       struct ioil_pool *pool)
{
	d_iov_t	iov = {};
	int	rc;
	int	cmd;
	ssize_t	rsize;

	D_ALLOC(iov.iov_buf, hs_reply->fsr_pool_size);
	if (!iov.iov_buf)
		return ENOMEM;

	/* Max size of ioctl is 16k */
	if (hs_reply->fsr_pool_size >= (16 * 1024)) {
		char fname[NAME_LEN];

		cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE,
			   DFUSE_IOCTL_REPLY_PFILE, NAME_LEN);

		errno = 0;
		rc = ioctl(fd, cmd, fname);
		if (rc != 0) {
			rc = errno;

			DFUSE_LOG_WARNING("ioctl call on %d failed: %d (%s)", fd, rc, strerror(rc));
			goto out;
		}
		errno = 0;
		fd = __real_open(fname, O_RDONLY);
		if (fd == -1)
			D_GOTO(out, rc = errno);
		rsize = __real_read(fd, iov.iov_buf, hs_reply->fsr_pool_size);
		if (rsize != hs_reply->fsr_pool_size)
			D_GOTO(out, rc = EAGAIN);
		unlink(fname);
	} else {
		cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE,
			   DFUSE_IOCTL_REPLY_POH, hs_reply->fsr_pool_size);

		errno = 0;
		rc = ioctl(fd, cmd, iov.iov_buf);
		if (rc != 0) {
			rc = errno;

			DFUSE_LOG_WARNING("ioctl call on %d failed: %d (%s)", fd, rc, strerror(rc));
			goto out;
		}
	}

	iov.iov_buf_len = hs_reply->fsr_pool_size;
	iov.iov_len = iov.iov_buf_len;

	rc = daos_pool_global2local(iov, &pool->iop_poh);
	if (rc) {
		DFUSE_LOG_WARNING("Failed to use pool handle: " DF_RC, DP_RC(rc));
		D_GOTO(out, rc = daos_der2errno(rc));
	}
out:
	D_FREE(iov.iov_buf);
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
		/* Fetch the pool handle via the ioctl or file.  Both dfuse
		 * and the local code can return EAGAIN if the pool handle
		 * changes in size during reading so handle this case here.
		 */
		rc = ioil_fetch_pool_handle(fd, &hs_reply, pool);
		if (rc == EAGAIN)
			rc = ioil_fetch_pool_handle(fd, &hs_reply, pool);
		if (rc != 0)
			return rc;
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
		DFUSE_LOG_WARNING("Failed to use cont handle "DF_RC,
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
		DFUSE_LOG_WARNING("Failed to use dfs handle: %d (%s)", rc, strerror(rc));
		D_FREE(iov.iov_buf);
		return rc;
	}

	DFUSE_TRA_UP(cont->ioc_dfs, &ioil_iog, "dfs");
	D_FREE(iov.iov_buf);

	return 0;
}

static bool
ioil_open_cont_handles(int fd, struct dfuse_il_reply *il_reply, struct ioil_cont *cont)
{
	int			rc;
	struct ioil_pool       *pool = cont->ioc_pool;
	char			uuid_str[37];
	int			dfs_flags = O_RDWR;

	if (daos_handle_is_inval(pool->iop_poh)) {
		uuid_unparse(il_reply->fir_pool, uuid_str);
		rc = daos_pool_connect(uuid_str, NULL, DAOS_PC_RO, &pool->iop_poh, NULL, NULL);
		if (rc)
			return false;
	}

	uuid_unparse(il_reply->fir_cont, uuid_str);
	rc = daos_cont_open(pool->iop_poh, uuid_str, DAOS_COO_RW, &cont->ioc_coh, NULL, NULL);
	if (rc == -DER_NO_PERM) {
		dfs_flags = O_RDONLY;
		rc = daos_cont_open(pool->iop_poh, uuid_str, DAOS_COO_RO, &cont->ioc_coh, NULL,
				    NULL);
	}
	if (rc)
		return false;

	rc = dfs_mount(pool->iop_poh, cont->ioc_coh, dfs_flags, &cont->ioc_dfs);
	if (rc)
		return false;

	DFUSE_TRA_UP(cont->ioc_dfs, &ioil_iog, "dfs");

	return true;
}

/* Wrapper function for daos_init()
 * Within ioil there are some use-cases where the caller opens files in sequence and expects back
 * specific file descriptors, specifically some configure scripts which hard-code fd numbers.  To
 * avoid problems here then if the fd being intercepted is low then pre-open a number of fds before
 * calling daos_init() and close them afterwards so that daos itself does not use and of the low
 * number file descriptors.
 * The DAOS logging uses fnctl calls to force it's FDs to higher numbers to avoid the same problems.
 * See DAOS-13381 for more details. Returns true on success
 */

#define IOIL_MIN_FD 10

static bool
call_daos_init(int fd)
{
	int  fds[IOIL_MIN_FD] = {};
	int  i                = 0;
	int  rc;
	bool rcb = false;

	if (fd < IOIL_MIN_FD) {
		fds[0] = __real_open("/", O_RDONLY);

		while (fds[i] < IOIL_MIN_FD) {
			fds[i + 1] = __real_dup(fds[i]);
			if (fds[i + 1] == -1) {
				DFUSE_LOG_DEBUG("Pre-opening files failed: %d (%s)", errno,
						strerror(errno));
				goto out;
			}
			i++;
			D_ASSERT(i < IOIL_MIN_FD);
		}
	}

	rc = daos_init();
	if (rc) {
		DFUSE_LOG_DEBUG("daos_init() failed, " DF_RC, DP_RC(rc));
		goto out;
	}
	rcb = true;

out:
	i = 0;
	while (fds[i] > 0) {
		__real_close(fds[i]);
		i++;
		D_ASSERT(i < IOIL_MIN_FD);
	}

	if (rcb)
		ioil_iog.iog_daos_init = true;
	else
		ioil_iog.iog_no_daos = true;

	return rcb;
}

/* Returns true on success */
static bool
check_ioctl_on_open(int fd, struct fd_entry *entry, int flags)
{
	struct dfuse_il_reply il_reply;
	int                   rc;
	struct ioil_pool     *pool;
	struct ioil_cont     *cont;
	bool                  pool_alloc = false;

	if (ioil_iog.iog_no_daos) {
		DFUSE_LOG_DEBUG("daos_init() has previously failed");
		return false;
	}

	errno = 0;
	rc = ioctl(fd, DFUSE_IOCTL_IL, &il_reply);
	if (rc != 0) {
		int err = errno;

		if (err != ENOTTY)
			DFUSE_LOG_DEBUG("ioctl call on %d failed %d %s", fd,
					err, strerror(err));
		return false;
	}

	if (il_reply.fir_version != DFUSE_IOCTL_VERSION) {
		DFUSE_LOG_WARNING("ioctl version mismatch (fd=%d): expected %d got %d", fd,
				  DFUSE_IOCTL_VERSION, il_reply.fir_version);
		return false;
	}

	rc = pthread_mutex_lock(&ioil_iog.iog_lock);
	D_ASSERT(rc == 0);

	if (!ioil_iog.iog_daos_init)
		if (!call_daos_init(fd))
			goto err;

	d_list_for_each_entry(pool, &ioil_iog.iog_pools_head, iop_pools) {
		if (uuid_compare(pool->iop_uuid, il_reply.fir_pool) != 0)
			continue;

		d_list_for_each_entry(cont, &pool->iop_container_head, ioc_containers) {
			if (uuid_compare(cont->ioc_uuid, il_reply.fir_cont) != 0)
				continue;

			D_GOTO(get_file, rc = 0);
		}
		D_GOTO(open_cont, rc = 0);
	}

	/* Allocate data for pool */
	D_ALLOC_PTR(pool);
	if (pool == NULL)
		D_GOTO(err, rc = ENOMEM);

	pool_alloc = true;
	uuid_copy(pool->iop_uuid, il_reply.fir_pool);
	D_INIT_LIST_HEAD(&pool->iop_container_head);

open_cont:

	D_ALLOC_PTR(cont);
	if (cont == NULL) {
		if (pool_alloc)
			D_FREE(pool);
		D_GOTO(err, rc = ENOMEM);
	}

	cont->ioc_pool = pool;
	uuid_copy(cont->ioc_uuid, il_reply.fir_cont);
	d_list_add(&cont->ioc_containers, &pool->iop_container_head);

	if (pool_alloc)
		d_list_add(&pool->iop_pools, &ioil_iog.iog_pools_head);

	rc = ioil_fetch_cont_handles(fd, cont);
	if (rc == EPERM || rc == EOVERFLOW) {
		bool rcb;

		DFUSE_LOG_DEBUG("ioil_fetch_cont_handles() failed, backing off");

		rcb = ioil_open_cont_handles(fd, &il_reply, cont);
		if (!rcb) {
			DFUSE_LOG_DEBUG("ioil_open_cont_handles() failed");
			D_GOTO(shrink, rc = rcb);
		}
	} else if (rc != 0) {
		D_ERROR("ioil_fetch_cont_handles() failed: %d (%s)\n", rc, strerror(rc));
		D_GOTO(shrink, rc);
	}

get_file:
	entry->fd_pos = 0;
	entry->fd_flags = flags;
	entry->fd_status = DFUSE_IO_BYPASS;
	entry->fd_cont = cont;

	/* Only intercept fstat if caching is not on for this file */
	if ((il_reply.fir_flags & DFUSE_IOCTL_FLAGS_MCACHE) == 0)
		entry->fd_fstat = true;

	DFUSE_LOG_DEBUG("Flags are %#lx %d", il_reply.fir_flags, entry->fd_fstat);

	/* Now open the file object to allow read/write */
	rc = fetch_dfs_obj_handle(fd, entry);
	if (rc == EISDIR)
		D_GOTO(err, rc);
	else if (rc)
		D_GOTO(shrink, rc);

	DFUSE_LOG_DEBUG("fd:%d flags %#lx fstat %s", fd, il_reply.fir_flags,
			entry->fd_fstat ? "yes" : "no");

	rc = vector_set(&fd_table, fd, entry);
	if (rc != 0) {
		DFUSE_LOG_DEBUG("Failed to track IOF file fd=%d., disabling kernel bypass", fd);
		/* Disable kernel bypass */
		entry->fd_status = DFUSE_IO_DIS_RSRC;
		D_GOTO(obj_close, rc);
	}

	DFUSE_LOG_DEBUG("Added entry for new fd %d", fd);

	cont->ioc_open_count += 1;

	rc = pthread_mutex_unlock(&ioil_iog.iog_lock);
	D_ASSERT(rc == 0);

	return true;

obj_close:
	dfs_release(entry->fd_dfsoh);

shrink:
	ioil_shrink_cont(cont, true, false);

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

/* Whilst it's not impossible that dfuse is backing these paths it's very unlikely so
 * simply skip them to avoid the extra ioctl cost.
 */
static bool
dfuse_check_valid_path(const char *path)
{
	if ((strncmp(path, "/sys/", 5) == 0) ||
		(strncmp(path, "/dev/", 5) == 0) ||
		strncmp(path, "/proc/", 6) == 0) {
		return false;
	}
	return true;
}

DFUSE_PUBLIC int
dfuse___open64_2(const char *pathname, int flags)
{
	struct fd_entry entry = {0};
	int             fd;

	fd = __real___open64_2(pathname, flags);

	if (!ioil_iog.iog_initialized || (fd == -1))
		return fd;

	if (!dfuse_check_valid_path(pathname)) {
		DFUSE_LOG_DEBUG("open_2(pathname=%s) ignoring by path", pathname);
		return fd;
	}

	/* Disable bypass for O_APPEND|O_PATH */
	if ((flags & (O_PATH | O_APPEND)) != 0) {
		DFUSE_LOG_DEBUG("open_2(pathname=%s) ignoring by flag", pathname);
		return fd;
	}

	if (!check_ioctl_on_open(fd, &entry, flags)) {
		DFUSE_LOG_DEBUG("open_2(pathname=%s) interception not possible", pathname);
		return fd;
	}

	atomic_fetch_add_relaxed(&ioil_iog.iog_file_count, 1);

	DFUSE_LOG_DEBUG("open_2(pathname=%s, flags=0%o) = %d. intercepted, fstat=%d, bypass=%s",
			pathname, flags, fd, entry.fd_fstat, bypass_status[entry.fd_status]);

	return fd;
}

DFUSE_PUBLIC int
dfuse___open_2(const char *pathname, int flags)
{
	struct fd_entry entry = {0};
	int             fd;

	fd = __real___open_2(pathname, flags);

	if (!ioil_iog.iog_initialized || (fd == -1))
		return fd;

	if (!dfuse_check_valid_path(pathname)) {
		DFUSE_LOG_DEBUG("open_2(pathname=%s) ignoring by path", pathname);
		return fd;
	}

	/* Disable bypass for O_APPEND|O_PATH */
	if ((flags & (O_PATH | O_APPEND)) != 0) {
		DFUSE_LOG_DEBUG("open_2(pathname=%s) ignoring by flag", pathname);
		return fd;
	}

	if (!check_ioctl_on_open(fd, &entry, flags)) {
		DFUSE_LOG_DEBUG("open_2(pathname=%s) interception not possible", pathname);
		return fd;
	}

	atomic_fetch_add_relaxed(&ioil_iog.iog_file_count, 1);

	DFUSE_LOG_DEBUG("open_2(pathname=%s, flags=0%o) = %d. intercepted, fstat=%d, bypass=%s",
			pathname, flags, fd, entry.fd_fstat, bypass_status[entry.fd_status]);

	return fd;
}

DFUSE_PUBLIC int
dfuse_open(const char *pathname, int flags, ...)
{
	struct fd_entry entry = {0};
	int             fd;
	unsigned int    mode; /* mode_t gets "promoted" to unsigned int for va_arg routine */

	if (flags & O_CREAT) {
		va_list ap;

		va_start(ap, flags);
		mode = va_arg(ap, unsigned int);
		va_end(ap);

		fd = __real_open(pathname, flags, mode);
	} else {
		fd = __real_open(pathname, flags);
		mode = 0;
	}

	if (!ioil_iog.iog_initialized || (fd == -1))
		return fd;

	if (!dfuse_check_valid_path(pathname)) {
		DFUSE_LOG_DEBUG("open(pathname=%s) ignoring by path", pathname);
		return fd;
	}

	if ((flags & (O_PATH | O_APPEND)) != 0) {
		DFUSE_LOG_DEBUG("open(pathname=%s) ignoring by flag", pathname);
		return fd;
	}

	if (!check_ioctl_on_open(fd, &entry, flags)) {
		DFUSE_LOG_DEBUG("open(pathname=%s) interception not possible", pathname);
		return fd;
	}

	atomic_fetch_add_relaxed(&ioil_iog.iog_file_count, 1);

	if (flags & O_CREAT)
		DFUSE_LOG_DEBUG("open(pathname=%s, flags=0%o, mode=0%o) = "
				"%d. intercepted, fstat=%d, bypass=%s",
				pathname, flags, mode, fd, entry.fd_fstat,
				bypass_status[entry.fd_status]);
	else
		DFUSE_LOG_DEBUG("open(pathname=%s, flags=0%o) = "
				"%d. intercepted, fstat=%d, bypass=%s",
				pathname, flags, fd, entry.fd_fstat,
				bypass_status[entry.fd_status]);

	return fd;
}

DFUSE_PUBLIC int
dfuse_openat(int dirfd, const char *pathname, int flags, ...)
{
	struct fd_entry entry = {0};
	int             fd;
	unsigned int    mode; /* mode_t gets "promoted" to unsigned int for va_arg routine */

	if (flags & O_CREAT) {
		va_list ap;

		va_start(ap, flags);
		mode = va_arg(ap, unsigned int);
		va_end(ap);

		fd = __real_openat(dirfd, pathname, flags, mode);
	} else {
		fd = __real_openat(dirfd, pathname, flags);
		mode = 0;
	}

	if (!ioil_iog.iog_initialized || (fd == -1))
		return fd;

	if (!dfuse_check_valid_path(pathname)) {
		DFUSE_LOG_DEBUG("openat(pathname=%s) ignoring by path", pathname);
		return fd;
	}

	if ((flags & (O_PATH | O_APPEND)) != 0) {
		DFUSE_LOG_DEBUG("openat(pathname=%s) ignoring by flag", pathname);
		return fd;
	}

	if (!check_ioctl_on_open(fd, &entry, flags)) {
		DFUSE_LOG_DEBUG("openat(pathname=%s) interception not possible", pathname);
		return fd;
	}

	atomic_fetch_add_relaxed(&ioil_iog.iog_file_count, 1);

	if (flags & O_CREAT)
		DFUSE_LOG_DEBUG("openat(pathname=%s, flags=0%o, mode=0%o) = "
				"%d. intercepted, fstat=%d, bypass=%s",
				pathname, flags, mode, fd, entry.fd_fstat,
				bypass_status[entry.fd_status]);
	else
		DFUSE_LOG_DEBUG("openat(pathname=%s, flags=0%o) = "
				"%d. intercepted, fstat=%d, bypass=%s",
				pathname, flags, fd, entry.fd_fstat,
				bypass_status[entry.fd_status]);
	return fd;
}

DFUSE_PUBLIC int
dfuse_mkstemp(char *template)
{
	struct fd_entry entry = {0};
	int             fd;

	fd = __real_mkstemp(template);

	if (!ioil_iog.iog_initialized || (fd == -1))
		return fd;

	if (!dfuse_check_valid_path(template)) {
		DFUSE_LOG_DEBUG("mkstemp(template=%s) ignoring by path", template);
		return fd;
	}

	if (!check_ioctl_on_open(fd, &entry, O_CREAT | O_EXCL | O_RDWR)) {
		DFUSE_LOG_DEBUG("mkstemp(template=%s) interception not possible", template);
		return fd;
	}

	atomic_fetch_add_relaxed(&ioil_iog.iog_file_count, 1);

	DFUSE_LOG_DEBUG("mkstemp(template=%s) = %d. intercepted, fstat=%d, bypass=%s",
			template, fd, entry.fd_fstat, bypass_status[entry.fd_status]);

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

	if (!dfuse_check_valid_path(pathname)) {
		DFUSE_LOG_DEBUG("creat(pathname=%s) ignoring by path", pathname);
		return fd;
	}

	if (!check_ioctl_on_open(fd, &entry, O_CREAT | O_WRONLY | O_TRUNC)) {
		DFUSE_LOG_DEBUG("creat(pathname=%s) interception not possible", pathname);
		return fd;
	}

	atomic_fetch_add_relaxed(&ioil_iog.iog_file_count, 1);

	DFUSE_LOG_DEBUG("creat(pathname=%s, mode=0%o) = %d. intercepted, bypass=%s",
			pathname, mode, fd, bypass_status[entry.fd_status]);

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
	ssize_t          bytes_read;
	off_t            oldpos;
	off_t            seekpos;
	int              rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_read;

	DFUSE_LOG_DEBUG("read(fd=%d, buf=%p, len=%zu) intercepted, bypass=%s", fd, buf, len,
			bypass_status[entry->fd_status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_read;

	oldpos = entry->fd_pos;
	bytes_read = pread_rpc(entry, buf, len, oldpos);
	if (bytes_read < 0)
		goto disable_file;
	else if (bytes_read > 0)
		entry->fd_pos = oldpos + bytes_read;
	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(bytes_read < 0);

	return bytes_read;
disable_file:
	/* The read failed to this file so disable I/O to this file, but do it in such a way that
	 * future reads can be handled by the kernel
	 */
	/* First seek to where the current position is, if there is an error here then ensure
	 * errno is set, but do not disable I/O as bypass could not work correctly.
	 */
	seekpos = __real_lseek(fd, entry->fd_pos, SEEK_SET);
	if (seekpos != entry->fd_pos) {
		if (seekpos != (off_t)-1)
			errno = EIO;
		return -1;
	}
	DFUSE_TRA_INFO(entry->fd_dfsoh, "Disabling interception on I/O error");
	entry->fd_status = DFUSE_IO_DIS_IOERR;
	vector_decref(&fd_table, entry);
	/* Fall through and do the read */
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

	if (drop_reference_if_disabled(entry))
		goto do_real_write;

	/* This function might get called from daos logging itself so do not log anything until
	 * after the disabled check above or the logging will recurse and deadlock.
	 */
	DFUSE_LOG_DEBUG("write(fd=%d, buf=%p, len=%zu) "
			"intercepted, bypass=%s", fd,
			buf, len, bypass_status[entry->fd_status]);

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

	DFUSE_LOG_DEBUG("lseek(fd=%d, offset=%zd, whence=%#x) intercepted, bypass=%s", fd, offset,
			whence, bypass_status[entry->fd_status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_lseek;

	if (whence == SEEK_SET) {
		new_offset = offset;
	} else if (whence == SEEK_CUR) {
		new_offset = entry->fd_pos + offset;
	} else if (whence == SEEK_END) {
		DFUSE_TRA_INFO(entry->fd_dfsoh, "Unsupported function, disabling SEEK_END");
		entry->fd_status = DFUSE_IO_DIS_STREAM;
		vector_decref(&fd_table, entry);
		return __real_lseek(fd, offset, whence);
	} else {
		DFUSE_TRA_INFO(entry->fd_dfsoh, "Unsupported function, disabling %d", whence);
		entry->fd_status = DFUSE_IO_DIS_STREAM;
		vector_decref(&fd_table, entry);
		return __real_lseek(fd, offset, whence);
	}

	if (new_offset < 0) {
		new_offset = (off_t)-1;
		errno = EINVAL;
	} else {
		entry->fd_pos = new_offset;
	}

	SAVE_ERRNO(new_offset < 0);

	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(new_offset < 0);

	return new_offset;

do_real_lseek:
	return __real_lseek(fd, offset, whence);
}

DFUSE_PUBLIC int
dfuse_fseek(FILE *stream, long offset, int whence)
{
	struct fd_entry *entry;
	off_t            new_offset = -1;
	int              rc;
	int              fd;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fseek;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fseek;

	DFUSE_LOG_DEBUG("fseek(fd=%d, offset=%zd, whence=%#x) intercepted, bypass=%s", fd, offset,
			whence, bypass_status[entry->fd_status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_fseek;

	if (whence == SEEK_SET) {
		new_offset    = offset;
		entry->fd_eof = false;
	} else if (whence == SEEK_CUR) {
		new_offset    = entry->fd_pos + offset;
		entry->fd_eof = false;
	} else if (whence == SEEK_END) {
		DFUSE_TRA_INFO(entry->fd_dfsoh,
			       "Unsupported function, disabling streaming SEEK_END");
		entry->fd_status = DFUSE_IO_DIS_STREAM;
		vector_decref(&fd_table, entry);
		return __real_fseek(stream, offset, whence);
	} else {
		DFUSE_TRA_INFO(entry->fd_dfsoh, "Unsupported function, disabling streaming %d",
			       whence);
		entry->fd_status = DFUSE_IO_DIS_STREAM;
		vector_decref(&fd_table, entry);
		return __real_fseek(stream, offset, whence);
	}

	if (new_offset < 0) {
		new_offset = (off_t)-1;
		errno      = EINVAL;
	} else {
		entry->fd_pos = new_offset;
	}

	SAVE_ERRNO(new_offset < 0);

	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(new_offset < 0);

	if (new_offset > 0)
		return 0;
	return new_offset;

do_real_fseek:
	return __real_fseek(stream, offset, whence);
}

DFUSE_PUBLIC int
dfuse_fseeko(FILE *stream, off_t offset, int whence)
{
	struct fd_entry *entry;
	off_t            new_offset = -1;
	int              rc;
	int              fd;

	DFUSE_TRA_DEBUG(stream, "fseeko(offset=%zd, whence=%#x) skipped", offset, whence);

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fseeko;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0) {
		DFUSE_TRA_DEBUG(stream, "fseeko(fd=%d, offset=%zd, whence=%#x) skipped", fd, offset,
				whence);
		goto do_real_fseeko;
	}

	DFUSE_TRA_DEBUG(entry->fd_dfsoh,
			"fseeko(fd=%d, offset=%zd, whence=%#x) intercepted, bypass=%s", fd, offset,
			whence, bypass_status[entry->fd_status]);

	if (drop_reference_if_disabled(entry))
		goto do_real_fseeko;

	if (whence == SEEK_SET) {
		new_offset    = offset;
		entry->fd_eof = false;
	} else if (whence == SEEK_CUR) {
		new_offset    = entry->fd_pos + offset;
		entry->fd_eof = false;
	} else if (whence == SEEK_END) {
		DFUSE_TRA_INFO(entry->fd_dfsoh,
			       "Unsupported function, disabling streaming SEEK_END");
		entry->fd_status = DFUSE_IO_DIS_STREAM;
		vector_decref(&fd_table, entry);
		return __real_fseeko(stream, offset, whence);
	} else {
		DFUSE_TRA_INFO(entry->fd_dfsoh, "Unsupported function, disabling streaming %d",
			       whence);
		entry->fd_status = DFUSE_IO_DIS_STREAM;
		vector_decref(&fd_table, entry);
		return __real_fseeko(stream, offset, whence);
	}

	if (new_offset < 0) {
		new_offset = (off_t)-1;
		errno      = EINVAL;
	} else {
		entry->fd_pos = new_offset;
	}

	SAVE_ERRNO(new_offset < 0);

	vector_decref(&fd_table, entry);

	RESTORE_ERRNO(new_offset < 0);

	if (new_offset > 0)
		return 0;
	rc = new_offset;
	DFUSE_TRA_DEBUG(stream, "returning %d", rc);
	return rc;

do_real_fseeko:
	rc = __real_fseeko(stream, offset, whence);
	DFUSE_TRA_DEBUG(stream, "returning %d", rc);
	return rc;
}

DFUSE_PUBLIC void
dfuse_rewind(FILE *stream)
{
	struct fd_entry *entry;
	int              rc;
	int              fd;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_rewind;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_rewind;

	if (drop_reference_if_disabled(entry))
		goto do_real_rewind;

	DFUSE_TRA_DEBUG(entry->fd_dfsoh, "rewind(fd=%d) intercepted, bypass=%s", fd,
			bypass_status[entry->fd_status]);

	entry->fd_pos = 0;
	entry->fd_err = 0;

	vector_decref(&fd_table, entry);

	return;

do_real_rewind:
	__real_rewind(stream);
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
dfuse_ftruncate(int fd, off_t length)
{
	struct fd_entry *entry;
	int              rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_ftruncate;

	DFUSE_LOG_DEBUG("ftuncate(fd=%d) intercepted, bypass=%s offset %#lx", fd,
			bypass_status[entry->fd_status], length);

	rc = dfs_punch(entry->fd_cont->ioc_dfs, entry->fd_dfsoh, length, DFS_MAX_FSIZE);

	vector_decref(&fd_table, entry);

	if (rc == -DER_SUCCESS)
		return 0;

	errno = rc;
	return -1;

do_real_ftruncate:
	return __real_ftruncate(fd, length);
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

/* If we intercept a streaming function that cannot be handled then log this and back-off to the
 * libc functions.  Ensure that the file position is updated correctly.
 * If fd_pos and offset are both non-zero then it means the intereption library has been partially
 * working so there is a conflict on where data has been served from which we need to identify.
 * fd_pos can either be 0 for files with no I/O or -1 on some error paths, do not do the seek
 * in either of these cases.
 * TODO: Add assert to check for this.
 */
#define DISABLE_STREAM(_entry, _stream)                                                            \
	do {                                                                                       \
		off_t            _offset;                                                          \
		int              _rc   = 0;                                                        \
		int              _err  = 0;                                                        \
		struct _IO_FILE *_file = (struct _IO_FILE *)(_stream);                             \
		(_entry)->fd_status    = DFUSE_IO_DIS_STREAM;                                      \
		_offset                = __real_ftello(_stream);                                   \
		if ((_entry)->fd_pos > 0) {                                                        \
			_rc = __real_fseeko(_stream, (_entry)->fd_pos, SEEK_SET);                  \
			if (_rc == -1)                                                             \
				_err = errno;                                                      \
		}                                                                                  \
		DFUSE_TRA_INFO((_entry)->fd_dfsoh, "disabling streaming %ld %ld rc=%d %d %s, %p",  \
			       _offset, (_entry)->fd_pos, _rc, _err, strerror(_err), (_stream));   \
		if (_file->_IO_read_base)                                                          \
			DFUSE_TRA_DEBUG((_entry)->fd_dfsoh, "Private data %p %p %p",               \
					_file->_IO_read_base, _file->_IO_read_ptr,                 \
					_file->_IO_read_end);                                      \
	} while (0)

/* Check if file data is being cached in memory, if it is then disable interception */
static inline bool
_stream_macros_used(FILE *stream)
{
	struct _IO_FILE *file = (struct _IO_FILE *)(stream);

	if (file->_IO_read_base)
		return true;

	return false;
}

DFUSE_PUBLIC FILE *
dfuse_fdopen(int fd, const char *mode)
{
	struct fd_entry *entry;
	FILE            *file;
	int              rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	DFUSE_TRA_DEBUG(entry->fd_dfsoh, "fdopen(fd=%d, mode=%s) intercepted", fd, mode);

	file = __real_fdopen(fd, mode);

	if (file && _stream_macros_used(file)) {
		DFUSE_TRA_WARNING(entry->fd_dfsoh,
				  "fdopen(fd=%d, mode=%s) buffers pre-loaded, disabling", fd, mode);
		DISABLE_STREAM(entry, file);
	}

	vector_decref(&fd_table, entry);

	return file;

do_real_fn:
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
	FILE           *fp;
	struct fd_entry entry = {0};
	int             fd;
	off_t           offset;

	pthread_once(&init_links_flag, init_links);

	fp = __real_fopen(path, mode);

	if (!ioil_iog.iog_initialized || fp == NULL) {
		DFUSE_LOG_DEBUG("fopen(pathname=%s) not initialized %p", path, fp);
		return fp;
	}

	fd = fileno(fp);
	if (fd == -1)
		return fp;

	/* If open in append mode then the initial offset is at the end of file, not the
	 * beginning so disable I/O at this point, in the same way we do for O_APPEND.
	 */
	offset = __real_ftello(fp);
	if (offset != 0) {
		DFUSE_LOG_DEBUG("fopen(pathname=%s) ignoring by offset %d %p", path, fd, fp);
		return fp;
	}

	if (!dfuse_check_valid_path(path)) {
		DFUSE_LOG_DEBUG("fopen(pathname=%s) ignoring by path %d %p", path, fd, fp);
		return fp;
	}

	if (_stream_macros_used(fp)) {
		DFUSE_LOG_WARNING("fopen(pathname=%s) buffers pre-loaded, disabling", path);
		return fp;
	}

	if (!check_ioctl_on_open(fd, &entry, O_CREAT | O_WRONLY | O_TRUNC)) {
		DFUSE_LOG_DEBUG("fopen(pathname=%s) interception not possible %d %p", path, fd, fp);
		return fp;
	}

	atomic_fetch_add_relaxed(&ioil_iog.iog_file_count, 1);

	DFUSE_TRA_DEBUG(entry.fd_dfsoh,
			"fopen(path='%s', mode=%s) = %p(fd=%d) intercepted, bypass=%s", path, mode,
			fp, fd, bypass_status[entry.fd_status]);

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

	if (newfd == -1 || !check_ioctl_on_open(newfd, &new_entry, 0)) {
		if (rc == 0) {
			DFUSE_LOG_DEBUG("freopen(path='%s', mode=%s, stream=%p"
					"(fd=%d) = %p(fd=%d) intercepted",
					path, mode, stream, oldfd, newstream, newfd);
			vector_decref(&fd_table, old_entry);
		}
		return newstream;
	}

	if (rc == 0) {
		DFUSE_LOG_DEBUG("freopen(path='%s', mode=%s, stream=%p(fd=%d) = %p(fd=%d)"
				" intercepted",
				path, mode, stream, oldfd, newstream, newfd);
		vector_decref(&fd_table, old_entry);
	} else {
		DFUSE_LOG_DEBUG("freopen(path='%s', mode=%s, stream=%p(fd=%d)) "
				"= %p(fd=%d) intercepted",
				path, mode, stream, oldfd, newstream, newfd);
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

	DFUSE_LOG_DEBUG("fclose(stream=%p(fd=%d)) intercepted, bypass=%s", stream, fd,
			bypass_status[entry->fd_status]);

	vector_decref(&fd_table, entry);

do_real_fclose:
	return __real_fclose(stream);
}

DFUSE_PUBLIC size_t
dfuse_fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	struct fd_entry *entry = NULL;
	ssize_t          bytes_read;
	off_t            oldpos;
	size_t           nread = 0;
	size_t           len;
	int              fd;
	int              rc;
	int              errcode = EIO;
	int              counter;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fread;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fread;

	if (drop_reference_if_disabled(entry))
		goto do_real_fread;

	DFUSE_TRA_DEBUG(entry->fd_dfsoh, "performing fread of %#zx %#zx from %#zx", size, nmemb,
			entry->fd_pos);

	len = nmemb * size;

	counter = atomic_fetch_add_relaxed(&ioil_iog.iog_read_count, 1);

	if (counter < ioil_iog.iog_report_count)
		__real_fprintf(stderr, "[libioil] Intercepting fread of size %zi\n", len);

	oldpos     = entry->fd_pos;
	bytes_read = ioil_do_pread(ptr, len, oldpos, entry, &errcode);
	if (bytes_read > 0) {
		nread         = bytes_read / size;
		entry->fd_pos = oldpos + (nread * size);
		if (nread != nmemb)
			entry->fd_eof = true;
	} else if (bytes_read < 0) {
		entry->fd_err = bytes_read;
	} else {
		entry->fd_eof = true;
	}

	vector_decref(&fd_table, entry);

	DFUSE_TRA_DEBUG(entry->fd_dfsoh, "performed %#zx reads", nread);

	return nread;

do_real_fread:
	return __real_fread(ptr, size, nmemb, stream);
}

DFUSE_PUBLIC size_t
dfuse_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	struct fd_entry *entry = NULL;
	size_t           len;
	int              fd;
	off_t            oldpos;
	int              rc;
	int              errcode = EIO;
	int              counter;
	ssize_t          bytes_written;
	size_t           nwrite = 0;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fwrite;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fwrite;

	if (drop_reference_if_disabled(entry))
		goto do_real_fwrite;

	if (_stream_macros_used(stream)) {
		DISABLE_STREAM(entry, stream);
		vector_decref(&fd_table, entry);
		goto do_real_fwrite;
	}

	len = nmemb * size;

	counter = atomic_fetch_add_relaxed(&ioil_iog.iog_write_count, 1);

	if (counter < ioil_iog.iog_report_count)
		__real_fprintf(stderr, "[libioil] Intercepting fwrite of size %zi\n", len);

	DFUSE_TRA_DEBUG(entry->fd_dfsoh, "Doing fwrite to %p at %#zx", stream, entry->fd_pos);
	oldpos        = entry->fd_pos;
	bytes_written = ioil_do_writex(ptr, len, oldpos, entry, &errcode);
	if (bytes_written > 0) {
		nwrite        = bytes_written / size;
		entry->fd_pos = oldpos + (nwrite * size);
	} else if (bytes_written < 0) {
		entry->fd_err = bytes_written;
	}

	vector_decref(&fd_table, entry);
	return nwrite;

do_real_fwrite:
	return __real_fwrite(ptr, size, nmemb, stream);
}

DFUSE_PUBLIC int
dfuse_feof(FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_feof;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_feof;

	if (drop_reference_if_disabled(entry))
		goto do_real_feof;

	rc = (int)entry->fd_eof;

	vector_decref(&fd_table, entry);

	return rc;
do_real_feof:
	return __real_feof(stream);
}

DFUSE_PUBLIC int
dfuse_ferror(FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_ferror;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_ferror;

	if (drop_reference_if_disabled(entry))
		goto do_real_ferror;

	rc = entry->fd_err;

	vector_decref(&fd_table, entry);

	return rc;
do_real_ferror:
	return __real_ferror(stream);
}

DFUSE_PUBLIC void
dfuse_clearerr(FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_clearerr;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_clearerr;

	if (drop_reference_if_disabled(entry))
		goto do_real_clearerr;

	entry->fd_err = 0;

	vector_decref(&fd_table, entry);

do_real_clearerr:
	__real_clearerr(stream);
}

DFUSE_PUBLIC int
dfuse___uflow(FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_uflow;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_uflow;

	if (drop_reference_if_disabled(entry))
		goto do_real_uflow;

	DISABLE_STREAM(entry, stream);

	vector_decref(&fd_table, entry);

do_real_uflow:
	return __real___uflow(stream);
}

DFUSE_PUBLIC int
dfuse___overflow(FILE *stream, int i)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	DISABLE_STREAM(entry, stream);

	vector_decref(&fd_table, entry);

do_real_fn:
	return __real___overflow(stream, i);
}

DFUSE_PUBLIC long
dfuse_ftell(FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;
	long             off;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_ftell;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_ftell;

	if (drop_reference_if_disabled(entry))
		goto do_real_ftell;

	/* Load the position from the interception library */
	off = entry->fd_pos;

	DFUSE_TRA_DEBUG(entry->fd_dfsoh, "Returning offset %ld", off);

	vector_decref(&fd_table, entry);

	return off;
do_real_ftell:
	return __real_ftell(stream);
}

DFUSE_PUBLIC off_t
dfuse_ftello(FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;
	off_t            off;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_ftello;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_ftello;

	if (drop_reference_if_disabled(entry))
		goto do_real_ftello;

	off = entry->fd_pos;

	vector_decref(&fd_table, entry);

	return off;
do_real_ftello:
	return __real_ftello(stream);
}

DFUSE_PUBLIC int
dfuse_fputc(int c, FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	DISABLE_STREAM(entry, stream);

	vector_decref(&fd_table, entry);

do_real_fn:
	return __real_fputc(c, stream);
}

DFUSE_PUBLIC int
dfuse_fputs(char *__str, FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	D_ERROR("Unsupported function\n");

	entry->fd_err = ENOTSUP;

	vector_decref(&fd_table, entry);

	errno = ENOTSUP;
	return EOF;

do_real_fn:
	return __real_fputs(__str, stream);
}

DFUSE_PUBLIC int
dfuse_fputws(const wchar_t *ws, FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	entry->fd_err = ENOTSUP;

	vector_decref(&fd_table, entry);

	errno = ENOTSUP;
	return -1;

do_real_fn:
	return __real_fputws(ws, stream);
}

DFUSE_PUBLIC int
dfuse_fgetc(FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	DISABLE_STREAM(entry, stream);

	vector_decref(&fd_table, entry);

do_real_fn:
	return __real_fgetc(stream);
}

DFUSE_PUBLIC int
dfuse_getc(FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	DISABLE_STREAM(entry, stream);

	vector_decref(&fd_table, entry);

do_real_fn:
	return __real_getc(stream);
}

DFUSE_PUBLIC int
dfuse_getc_unlocked(FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	DISABLE_STREAM(entry, stream);

	vector_decref(&fd_table, entry);

do_real_fn:
	return __real_getc_unlocked(stream);
}

DFUSE_PUBLIC wint_t
dfuse_getwc(FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	DISABLE_STREAM(entry, stream);

	vector_decref(&fd_table, entry);

do_real_fn:
	return __real_getwc(stream);
}

DFUSE_PUBLIC wint_t
dfuse_getwc_unlocked(FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	DISABLE_STREAM(entry, stream);

	vector_decref(&fd_table, entry);

do_real_fn:
	return __real_getwc_unlocked(stream);
}

DFUSE_PUBLIC wint_t
dfuse_fgetwc(FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	DISABLE_STREAM(entry, stream);

	vector_decref(&fd_table, entry);

do_real_fn:
	return __real_fgetwc(stream);
}

DFUSE_PUBLIC wint_t
dfuse_fgetwc_unlocked(FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	DISABLE_STREAM(entry, stream);

	vector_decref(&fd_table, entry);

do_real_fn:
	return __real_fgetwc_unlocked(stream);
}

DFUSE_PUBLIC char *
dfuse_fgets(char *str, int n, FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);

	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	DISABLE_STREAM(entry, stream);

	vector_decref(&fd_table, entry);

do_real_fn:
	return __real_fgets(str, n, stream);
}

DFUSE_PUBLIC wchar_t *
dfuse_fgetws(wchar_t *ws, int n, FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	entry->fd_err = ENOTSUP;

	vector_decref(&fd_table, entry);

	errno = ENOTSUP;
	return NULL;

do_real_fn:
	return __real_fgetws(ws, n, stream);
}

DFUSE_PUBLIC int
dfuse_ungetc(int c, FILE *stream)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	DISABLE_STREAM(entry, stream);

	vector_decref(&fd_table, entry);

do_real_fn:
	return __real_ungetc(c, stream);
}

DFUSE_PUBLIC int
dfuse_fscanf(FILE *stream, const char *format, ...)
{
	struct fd_entry *entry = NULL;
	va_list          ap;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	DISABLE_STREAM(entry, stream);

	vector_decref(&fd_table, entry);

do_real_fn:
	va_start(ap, format);
	rc = __real_vfscanf(stream, format, ap);
	va_end(ap);
	return rc;
}

DFUSE_PUBLIC int
dfuse_vfscanf(FILE *stream, const char *format, va_list arg)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	DISABLE_STREAM(entry, stream);

	vector_decref(&fd_table, entry);
do_real_fn:
	return __real_vfscanf(stream, format, arg);
}

DFUSE_PUBLIC int
dfuse_fprintf(FILE *stream, const char *format, ...)
{
	struct fd_entry *entry = NULL;
	va_list          ap;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	DISABLE_STREAM(entry, stream);

	vector_decref(&fd_table, entry);

do_real_fn:
	va_start(ap, format);
	rc = __real_vfprintf(stream, format, ap);
	va_end(ap);
	return rc;
}

DFUSE_PUBLIC int
dfuse_vfprintf(FILE *stream, const char *format, va_list arg)
{
	struct fd_entry *entry = NULL;
	int              fd;
	int              rc;

	fd = fileno(stream);
	if (fd == -1)
		goto do_real_fn;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fn;

	if (drop_reference_if_disabled(entry))
		goto do_real_fn;

	DISABLE_STREAM(entry, stream);

	vector_decref(&fd_table, entry);
do_real_fn:
	return __real_vfprintf(stream, format, arg);
}

DFUSE_PUBLIC int
dfuse___fxstat(int ver, int fd, struct stat *buf)
{
	struct fd_entry	*entry = NULL;
	int		counter;
	int		rc;

	rc = vector_get(&fd_table, fd, &entry);
	if (rc != 0)
		goto do_real_fstat;

	/* Turn off this feature if the kernel is doing metadata caching, in this case it's better
	 * to use the kernel cache and keep it up-to-date than query the severs each time.
	 */
	if (!entry->fd_fstat) {
		vector_decref(&fd_table, entry);
		goto do_real_fstat;
	}

	counter = atomic_fetch_add_relaxed(&ioil_iog.iog_fstat_count, 1);

	if (counter < ioil_iog.iog_report_count)
		__real_fprintf(stderr, "[libioil] Intercepting fstat\n");

	/* fstat needs to return both the device magic number and the inode
	 * neither of which can change over time, but they're also not known
	 * at this point.  For the first call to fstat do the real call
	 * through the kernel, then save these two entries for next time.
	 */
	if (entry->fd_dev == 0) {
		rc =  __real___fxstat(ver, fd, buf);

		DFUSE_TRA_DEBUG(entry->fd_dfsoh, "initial fstat() returned %d", rc);

		if (rc) {
			vector_decref(&fd_table, entry);
			return rc;
		}
		entry->fd_dev = buf->st_dev;
		entry->fd_ino = buf->st_ino;
		vector_decref(&fd_table, entry);
		return 0;
	}

	rc = dfs_ostat(entry->fd_cont->ioc_dfs, entry->fd_dfsoh, buf);

	DFUSE_TRA_DEBUG(entry->fd_dfsoh, "dfs_ostat() returned %d", rc);

	buf->st_ino = entry->fd_ino;
	buf->st_dev = entry->fd_dev;

	vector_decref(&fd_table, entry);

	if (rc) {
		errno = rc;
		return -1;
	}

	return 0;
do_real_fstat:
	return __real___fxstat(ver, fd, buf);
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
