/**
 * (C) Copyright 2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * \file
 * DAOS cuFile Userspace Filesystem Plugin — Implementation
 *
 * Implements CUfileFSOps_t backed by libdfs for GPU direct storage access.
 *
 * Two I/O paths:
 * - GPU direct path: When cuFile provides rdma_info (non-NULL with valid RDMA
 *   descriptor), uses dfs_read_gpu()/dfs_write_gpu() which handle array layout
 *   and chunk splitting internally, calling daos_obj_fetch_gpu()/update_gpu()
 *   via CaRT HMEM bulk for GPU RDMA.
 *
 * - Host buffer path: When rdma_info is NULL (compat mode), uses standard
 *   dfs_read()/dfs_write() on the host-accessible buf provided by cuFile.
 *   The buf is either a bounce buffer or a BAR1-mapped view of GPU memory.
 *
 * Features:
 * - Mount caching: Multiple connect calls to the same pool/container reuse
 *   the same DFS mount via a refcounted global cache.
 * - Lazy init: daos_cufile_open(NULL, ...) auto-connects using DAOS_POOL
 *   and DAOS_CONT environment variables.
 * - Graceful reconnect: On stale handle errors (Mode 2 only), automatically
 *   reconnects the DFS mount and reopens the file, then retries the I/O once.
 * - fd-based mode (Mode 1): When used with dfuse, lazily resolves fd →
 *   DFS file handle by reading /proc/self/fd/N and parsing dfuse mounts.
 */

#include "cufile_internal.h"

#include <sys/ioctl.h>
#include <dfuse_ioctl.h>

/*
 * ============================================================================
 * Mount Cache — refcounted pool/container connection reuse
 * ============================================================================
 */

D_LIST_HEAD(mount_cache_list);
pthread_mutex_t mount_cache_lock = PTHREAD_MUTEX_INITIALIZER;

struct mount_cache_entry *
mount_cache_find_locked(const char *pool, const char *cont)
{
	struct mount_cache_entry *entry;

	d_list_for_each_entry(entry, &mount_cache_list, mce_link) {
		if (strcmp(entry->mce_pool, pool) == 0 && strcmp(entry->mce_cont, cont) == 0)
			return entry;
	}
	return NULL;
}

struct mount_cache_entry *
mount_cache_insert_locked(const char *pool, const char *cont, daos_cufile_mount_t *mount)
{
	struct mount_cache_entry *entry;

	entry = calloc(1, sizeof(*entry));
	if (entry == NULL)
		return NULL;

	entry->mce_pool = strdup(pool);
	entry->mce_cont = strdup(cont);
	if (entry->mce_pool == NULL || entry->mce_cont == NULL) {
		free(entry->mce_pool);
		free(entry->mce_cont);
		free(entry);
		return NULL;
	}

	entry->mce_mount    = mount;
	entry->mce_refcount = 1;
	d_list_add_tail(&entry->mce_link, &mount_cache_list);
	return entry;
}

void
mount_cache_remove_locked(struct mount_cache_entry *entry)
{
	d_list_del(&entry->mce_link);
	free(entry->mce_pool);
	free(entry->mce_cont);
	free(entry);
}

/*
 * ============================================================================
 * Internal Helpers
 * ============================================================================
 */

/**
 * Split an absolute path into parent directory path and basename.
 * Caller must free the returned strings.
 *
 * "/data/train/batch.pt" → parent="/data/train", name="batch.pt"
 * "/file.pt"             → parent="/", name="file.pt"
 */
int
path_split(const char *path, char **parent_path, char **base_name)
{
	char *path_copy1;
	char *path_copy2;
	char *dir;
	char *base;

	path_copy1 = strdup(path);
	path_copy2 = strdup(path);
	if (path_copy1 == NULL || path_copy2 == NULL) {
		free(path_copy1);
		free(path_copy2);
		return ENOMEM;
	}

	dir  = dirname(path_copy1);
	base = basename(path_copy2);

	*parent_path = strdup(dir);
	*base_name   = strdup(base);

	free(path_copy1);
	free(path_copy2);

	if (*parent_path == NULL || *base_name == NULL) {
		free(*parent_path);
		free(*base_name);
		return ENOMEM;
	}

	return 0;
}

/*
 * ============================================================================
 * Graceful Reconnection (Item 8)
 * ============================================================================
 */

/** Errors that indicate stale pool/container handles */
static bool
is_reconnectable_error(int err)
{
	return (err == EBADF || err == ENOTCONN || err == EIO);
}

/**
 * Attempt to reconnect a mount after handle eviction.
 * Caller must hold mount->cfm_lock.
 */
static int
reconnect_mount_locked(daos_cufile_mount_t *mount)
{
	int rc;

	if (mount->cfm_dfs) {
		dfs_disconnect(mount->cfm_dfs);
		mount->cfm_dfs = NULL;
	}

	rc = dfs_connect(mount->cfm_pool_label, NULL, mount->cfm_cont_label, O_RDWR, NULL, &mount->cfm_dfs);
	if (rc != 0) {
		D_ERROR("reconnect dfs_connect(%s/%s) failed: %d\n", mount->cfm_pool_label,
			mount->cfm_cont_label, rc);
		mount->cfm_connected = false;
		return rc;
	}

	D_INFO("reconnected to %s/%s\n", mount->cfm_pool_label, mount->cfm_cont_label);
	mount->cfm_connected = true;
	return 0;
}

/**
 * Reopen a file handle after mount reconnection.
 */
static int
reopen_handle(daos_cufile_handle_t *handle)
{
	dfs_obj_t *new_obj   = NULL;
	int        dfs_flags = 0;
	int        rc;

	if (handle->cfh_obj) {
		dfs_release(handle->cfh_obj);
		handle->cfh_obj = NULL;
	}

	if ((handle->cfh_flags & O_ACCMODE) == O_RDONLY)
		dfs_flags = O_RDONLY;
	else if ((handle->cfh_flags & O_ACCMODE) == O_WRONLY)
		dfs_flags = O_WRONLY;
	else
		dfs_flags = O_RDWR;

	rc = dfs_lookup(handle->cfh_mount->cfm_dfs, handle->cfh_path, dfs_flags, &new_obj, NULL, NULL);
	if (rc != 0) {
		D_ERROR("reopen dfs_lookup(%s) failed: %d\n", handle->cfh_path, rc);
		return rc;
	}

	handle->cfh_obj = new_obj;
	return 0;
}

/**
 * Try reconnect + reopen + retry for I/O errors.
 * Returns true if reconnect succeeded and I/O should be retried.
 */
static bool
try_reconnect(daos_cufile_handle_t *handle, int err)
{
	int rc;

	/* Mode 1 (fd-based): no reconnect — dfuse itself doesn't reconnect */
	if (handle->cfh_fd_mode)
		return false;

	if (!is_reconnectable_error(err))
		return false;

	D_INFO("attempting reconnect after error %d on %s\n", err, handle->cfh_path);

	pthread_mutex_lock(&handle->cfh_mount->cfm_lock);
	rc = reconnect_mount_locked(handle->cfh_mount);
	pthread_mutex_unlock(&handle->cfh_mount->cfm_lock);

	if (rc != 0)
		return false;

	rc = reopen_handle(handle);
	return (rc == 0);
}

/*
 * ============================================================================
 * GPU Direct I/O via rdma_info
 *
 * When cuFile provides rdma_info (non-NULL with valid desc_str), it indicates
 * the I/O is in GPU direct mode — 'buf' is GPU memory (BAR1-mapped).
 *
 * cuFileBufRegister() has already registered this GPU memory with the NIC
 * and the resulting RDMA key is in rdma_info->desc_str. We pass this key
 * via daos_mem_attr_t.ma_rkey to CaRT, which imports it via
 * HG_Bulk_import_rkey() rather than re-registering the same memory.
 * This avoids double NIC registration (expensive for GPU pinned memory).
 *
 * If ma_rkey is empty (rdma_info was NULL or invalid), CaRT falls back to
 * normal fi_mr_reg() with FI_HMEM_CUDA — still correct, just redundant
 * registration.
 *
 * The array object layout (chunk splitting) is handled internally by
 * dfs_read_gpu()/dfs_write_gpu().
 *
 * When rdma_info is NULL, the plugin falls back to the host buffer path
 * using standard dfs_read()/dfs_write() on the host-accessible buf.
 * ============================================================================
 */

/**
 * Check if rdma_info contains a valid GPU RDMA descriptor.
 */
static bool
has_gpu_rdma_info(cufileRDMAInfo_t *rdma_info)
{
	return (rdma_info != NULL && rdma_info->desc_str != NULL && rdma_info->desc_len > 0);
}

/**
 * GPU direct read — uses dfs_read_gpu() which handles array layout internally.
 */
static ssize_t
gpu_direct_read(struct daos_cufile_handle *dfh, char *buf, size_t size, daos_off_t offset,
		cufileRDMAInfo_t *rdma_info)
{
	daos_mem_attr_t mem_attr = {
	    .ma_mem_type  = DAOS_MEM_TYPE_CUDA,
	    .ma_device_id = 0,
	};
	d_sg_list_t sgl;
	d_iov_t     iov;
	daos_size_t read_size = 0;
	int         rc;

	/* Pass pre-registered RDMA key if available — avoids double registration */
	if (rdma_info != NULL && rdma_info->desc_str != NULL && rdma_info->desc_len > 0)
		d_iov_set(&mem_attr.ma_rkey, (void *)rdma_info->desc_str,
			  rdma_info->desc_len);

	d_iov_set(&iov, buf, size);
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &iov;

	rc = dfs_read_gpu(dfh->cfh_mount->cfm_dfs, dfh->cfh_obj, &sgl, offset, &read_size,
			  &mem_attr);
	if (rc != 0)
		return -rc;

	return (ssize_t)read_size;
}

/**
 * GPU direct write — uses dfs_write_gpu() which handles array layout internally.
 */
static ssize_t
gpu_direct_write(struct daos_cufile_handle *dfh, const char *buf, size_t size, daos_off_t offset,
		 cufileRDMAInfo_t *rdma_info)
{
	daos_mem_attr_t mem_attr = {
	    .ma_mem_type  = DAOS_MEM_TYPE_CUDA,
	    .ma_device_id = 0,
	};
	d_sg_list_t sgl;
	d_iov_t     iov;
	int         rc;

	/* Pass pre-registered RDMA key if available — avoids double registration */
	if (rdma_info != NULL && rdma_info->desc_str != NULL && rdma_info->desc_len > 0)
		d_iov_set(&mem_attr.ma_rkey, (void *)rdma_info->desc_str,
			  rdma_info->desc_len);

	d_iov_set(&iov, (void *)buf, size);
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &iov;

	rc = dfs_write_gpu(dfh->cfh_mount->cfm_dfs, dfh->cfh_obj, &sgl, offset, &mem_attr);
	if (rc != 0)
		return -rc;

	return (ssize_t)size;
}

/*
 * ============================================================================
 * CUfileFSOps_t Callbacks
 *
 * Two I/O paths:
 * - rdma_info present → GPU direct path (dfs_read_gpu/dfs_write_gpu via HMEM)
 * - rdma_info absent  → Host buffer path (dfs_read/dfs_write on buf)
 *
 * 'buf' is always host-accessible (bounce buffer or BAR1-mapped view).
 * The host buffer path always works. The GPU direct path provides zero-copy
 * when cuFile can provide RDMA descriptors for the GPU buffer.
 * ============================================================================
 */

static struct daos_cufile_handle *get_handle(void *handle);

static const char *
daos_cufile_fs_type(void *handle)
{
	(void)handle;
	return "DAOS";
}

int
daos_cufile_get_rdma_devlist(void *handle, void **hostaddrs)
{
	(void)handle;
	/* NULL means cuFile can use any available RDMA device */
	*hostaddrs = NULL;
	return 0;
}

static ssize_t
daos_cufile_read(void *handle, char *buf, size_t size, loff_t offset, cufileRDMAInfo_t *rdma_info)
{
	struct daos_cufile_handle *dfh = get_handle(handle);
	d_iov_t                    iov;
	d_sg_list_t                sgl;
	daos_size_t                read_size = 0;
	ssize_t                    ret;
	int                        rc;

	if (dfh == NULL || dfh->cfh_obj == NULL)
		return -EINVAL;
	if (size == 0)
		return 0;

	if (has_gpu_rdma_info(rdma_info)) {
		/* GPU direct path: RDMA via dfs_read_gpu() */
		ret = gpu_direct_read(dfh, buf, size, (daos_off_t)offset, rdma_info);
		if (ret < 0 && try_reconnect(dfh, (int)(-ret)))
			ret = gpu_direct_read(dfh, buf, size, (daos_off_t)offset, rdma_info);
		return ret;
	}

	/* Host buffer path: standard DFS I/O on host-accessible buf */
	d_iov_set(&iov, buf, size);
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &iov;

	rc = dfs_read(dfh->cfh_mount->cfm_dfs, dfh->cfh_obj, &sgl, (daos_off_t)offset, &read_size, NULL);

	/* Retry once on reconnectable errors */
	if (rc != 0 && try_reconnect(dfh, rc)) {
		read_size = 0;
		rc =
		    dfs_read(dfh->cfh_mount->cfm_dfs, dfh->cfh_obj, &sgl, (daos_off_t)offset, &read_size, NULL);
	}

	if (rc != 0)
		return -rc;

	return (ssize_t)read_size;
}

static ssize_t
daos_cufile_write(void *handle, const char *buf, size_t size, loff_t offset,
		  cufileRDMAInfo_t *rdma_info)
{
	struct daos_cufile_handle *dfh = get_handle(handle);
	d_iov_t                    iov;
	d_sg_list_t                sgl;
	ssize_t                    ret;
	int                        rc;

	if (dfh == NULL || dfh->cfh_obj == NULL)
		return -EINVAL;
	if (size == 0)
		return 0;

	if (has_gpu_rdma_info(rdma_info)) {
		/* GPU direct path: RDMA via dfs_write_gpu() */
		ret = gpu_direct_write(dfh, buf, size, (daos_off_t)offset, rdma_info);
		if (ret < 0 && try_reconnect(dfh, (int)(-ret)))
			ret = gpu_direct_write(dfh, buf, size, (daos_off_t)offset, rdma_info);
		return ret;
	}

	/* Host buffer path: standard DFS I/O on host-accessible buf */
	d_iov_set(&iov, (void *)buf, size);
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &iov;

	rc = dfs_write(dfh->cfh_mount->cfm_dfs, dfh->cfh_obj, &sgl, (daos_off_t)offset, NULL);

	/* Retry once on reconnectable errors */
	if (rc != 0 && try_reconnect(dfh, rc)) {
		rc = dfs_write(dfh->cfh_mount->cfm_dfs, dfh->cfh_obj, &sgl, (daos_off_t)offset, NULL);
	}

	if (rc != 0)
		return -rc;

	return (ssize_t)size;
}

/** Static ops table — singleton for the process lifetime */
static const CUfileFSOps_t daos_fs_ops = {
    .fs_type               = daos_cufile_fs_type,
    .getRDMADeviceList     = daos_cufile_get_rdma_devlist,
    .getRDMADevicePriority = NULL,
    .read                  = daos_cufile_read,
    .write                 = daos_cufile_write,
};

/*
 * ============================================================================
 * fd-based Mode (Mode 1) — lazy fd→DFS resolution for dfuse files
 *
 * When an application uses dfuse + USERSPACE_FS, it passes an fd as the handle.
 * Our callbacks detect this (fd is a small integer, not a valid pointer) and
 * lazily resolve it to a DFS file handle by:
 *   1. ioctl(fd, DFUSE_IOCTL_IL_SIZE) → get serialized pool/container/DFS sizes
 *   2. ioctl(fd, DFUSE_IOCTL_IL_DSIZE) → get DFS object handle size
 *   3. Fetch serialized pool/container/DFS/object handles via dfuse ioctls
 *   4. Import them with *_global2local() to get local handles
 *   5. Cache the result for subsequent I/O
 * ============================================================================
 */

/** fd-based handle cache entry */
struct fd_handle_entry {
	d_list_t              fhe_link;
	int                   fhe_fd;
	daos_cufile_handle_t *fhe_handle;    /* NULL while connecting */
	bool                  fhe_connecting; /* true = resolution in progress */
};

static D_LIST_HEAD(fd_handle_cache);
static pthread_mutex_t fd_handle_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  fd_handle_cache_cond = PTHREAD_COND_INITIALIZER;

/**
 * Check if a void* is likely an fd (small integer) vs a daos_cufile_handle_t*.
 * fd values are small non-negative integers; pointers are large values.
 */
static bool
is_fd_handle(void *handle)
{
	uintptr_t val = (uintptr_t)handle;

	/* fd values are typically < 65536; pointers are much larger */
	return (val < 65536);
}

/**
 * Resolve an fd to a daos_cufile_handle_t using dfuse ioctls.
 *
 * Uses dfuse ioctls to fetch serialized pool/container/DFS/object handles
 * directly from the dfuse fd, then imports them with *_global2local().
 *
 * Thread-safe: if another thread is already resolving the same fd, we wait
 * on a global condvar then retry the lookup.
 */
static daos_cufile_handle_t *
resolve_fd_to_handle(int fd)
{
	struct fd_handle_entry  *entry;
	daos_cufile_handle_t   *handle = NULL;
	struct dfuse_hs_reply   hs_reply;
	struct dfuse_hsd_reply  hsd_reply;
	daos_handle_t           poh = DAOS_HDL_INVAL;
	daos_handle_t           coh = DAOS_HDL_INVAL;
	dfs_t                  *dfs = NULL;
	dfs_obj_t              *obj = NULL;
	d_iov_t                 iov = {};
	char                   *buf = NULL;
	size_t                  buf_size;
	int                     cmd, rc;

retry:
	pthread_mutex_lock(&fd_handle_cache_lock);

	d_list_for_each_entry(entry, &fd_handle_cache, fhe_link) {
		if (entry->fhe_fd != fd)
			continue;

		if (entry->fhe_handle != NULL) {
			/* Already resolved */
			handle = entry->fhe_handle;
			pthread_mutex_unlock(&fd_handle_cache_lock);
			return handle;
		}

		/* Another thread is connecting — wait and retry */
		pthread_cond_wait(&fd_handle_cache_cond, &fd_handle_cache_lock);
		pthread_mutex_unlock(&fd_handle_cache_lock);
		goto retry;
	}

	/* Not found — insert placeholder and release lock */
	entry = calloc(1, sizeof(*entry));
	if (entry == NULL) {
		pthread_mutex_unlock(&fd_handle_cache_lock);
		return NULL;
	}
	entry->fhe_fd         = fd;
	entry->fhe_handle     = NULL;
	entry->fhe_connecting = true;
	d_list_add_tail(&entry->fhe_link, &fd_handle_cache);

	pthread_mutex_unlock(&fd_handle_cache_lock);

	/* Step 1: Get serialized handle sizes */
	rc = ioctl(fd, DFUSE_IOCTL_IL_SIZE, &hs_reply);
	if (rc != 0) {
		D_ERROR("DFUSE_IOCTL_IL_SIZE on fd %d failed: %s\n", fd, strerror(errno));
		goto fail;
	}
	if (hs_reply.fsr_version != DFUSE_IOCTL_VERSION) {
		D_ERROR("dfuse ioctl version mismatch: expected %d got %d\n",
			DFUSE_IOCTL_VERSION, hs_reply.fsr_version);
		goto fail;
	}

	buf_size = hs_reply.fsr_pool_size;
	if (hs_reply.fsr_cont_size > buf_size)
		buf_size = hs_reply.fsr_cont_size;
	if (hs_reply.fsr_dfs_size > buf_size)
		buf_size = hs_reply.fsr_dfs_size;

	D_ALLOC(buf, buf_size);
	if (buf == NULL)
		goto fail;

	/* Step 2: Fetch and import pool handle */
	cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_POH,
		   hs_reply.fsr_pool_size);
	rc = ioctl(fd, cmd, buf);
	if (rc != 0) {
		D_ERROR("Failed to fetch pool handle from dfuse: %s\n", strerror(errno));
		goto fail;
	}
	iov.iov_buf     = buf;
	iov.iov_buf_len = hs_reply.fsr_pool_size;
	iov.iov_len     = hs_reply.fsr_pool_size;
	rc = daos_pool_global2local(iov, &poh);
	if (rc != 0) {
		D_ERROR("daos_pool_global2local() failed: " DF_RC "\n", DP_RC(rc));
		goto fail;
	}

	/* Step 3: Fetch and import container handle */
	cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_COH,
		   hs_reply.fsr_cont_size);
	rc = ioctl(fd, cmd, buf);
	if (rc != 0) {
		D_ERROR("Failed to fetch cont handle from dfuse: %s\n", strerror(errno));
		goto fail;
	}
	iov.iov_buf     = buf;
	iov.iov_buf_len = hs_reply.fsr_cont_size;
	iov.iov_len     = hs_reply.fsr_cont_size;
	rc = daos_cont_global2local(poh, iov, &coh);
	if (rc != 0) {
		D_ERROR("daos_cont_global2local() failed: " DF_RC "\n", DP_RC(rc));
		goto fail;
	}

	/* Step 4: Fetch and import DFS handle */
	cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_DOH,
		   hs_reply.fsr_dfs_size);
	rc = ioctl(fd, cmd, buf);
	if (rc != 0) {
		D_ERROR("Failed to fetch DFS handle from dfuse: %s\n", strerror(errno));
		goto fail;
	}
	iov.iov_buf     = buf;
	iov.iov_buf_len = hs_reply.fsr_dfs_size;
	iov.iov_len     = hs_reply.fsr_dfs_size;
	rc = dfs_global2local(poh, coh, 0, iov, &dfs);
	if (rc != 0) {
		D_ERROR("dfs_global2local() failed: %d\n", rc);
		goto fail;
	}

	/* Step 5: Fetch and import DFS object handle */
	rc = ioctl(fd, DFUSE_IOCTL_IL_DSIZE, &hsd_reply);
	if (rc != 0) {
		D_ERROR("DFUSE_IOCTL_IL_DSIZE on fd %d failed: %s\n", fd, strerror(errno));
		goto fail;
	}
	if (hsd_reply.fsr_version != DFUSE_IOCTL_VERSION) {
		D_ERROR("dfuse ioctl version mismatch: expected %d got %d\n",
			DFUSE_IOCTL_VERSION, hsd_reply.fsr_version);
		goto fail;
	}
	if (hsd_reply.fsr_dobj_size > buf_size) {
		D_FREE(buf);
		buf_size = hsd_reply.fsr_dobj_size;
		D_ALLOC(buf, buf_size);
		if (buf == NULL)
			goto fail;
	}
	cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_DOOH,
		   hsd_reply.fsr_dobj_size);
	rc = ioctl(fd, cmd, buf);
	if (rc != 0) {
		D_ERROR("Failed to fetch DFS object handle from dfuse: %s\n",
			strerror(errno));
		goto fail;
	}
	iov.iov_buf     = buf;
	iov.iov_buf_len = hsd_reply.fsr_dobj_size;
	iov.iov_len     = hsd_reply.fsr_dobj_size;
	rc = dfs_obj_global2local(dfs, 0, iov, &obj);
	if (rc != 0) {
		D_ERROR("dfs_obj_global2local() failed: %d\n", rc);
		goto fail;
	}

	D_FREE(buf);

	/* Build a daos_cufile_handle from the imported handles */
	handle = calloc(1, sizeof(*handle));
	if (handle == NULL)
		goto fail;

	handle->cfh_obj     = obj;
	handle->cfh_mount   = calloc(1, sizeof(*handle->cfh_mount));
	if (handle->cfh_mount == NULL) {
		free(handle);
		handle = NULL;
		goto fail;
	}
	handle->cfh_mount->cfm_dfs       = dfs;
	handle->cfh_mount->cfm_poh       = poh;
	handle->cfh_mount->cfm_coh       = coh;
	handle->cfh_mount->cfm_connected = true;
	handle->cfh_fd_mode              = true;
	pthread_mutex_init(&handle->cfh_lock, NULL);

	/* Success — publish handle and wake waiters */
	pthread_mutex_lock(&fd_handle_cache_lock);
	entry->fhe_handle     = handle;
	entry->fhe_connecting = false;
	pthread_cond_broadcast(&fd_handle_cache_cond);
	pthread_mutex_unlock(&fd_handle_cache_lock);

	return handle;

fail:
	D_FREE(buf);
	if (obj != NULL)
		dfs_release(obj);
	if (dfs != NULL)
		dfs_umount(dfs);
	if (!daos_handle_is_inval(coh))
		daos_cont_close(coh, NULL);
	if (!daos_handle_is_inval(poh))
		daos_pool_disconnect(poh, NULL);

	/* Remove placeholder so another thread can retry */
	pthread_mutex_lock(&fd_handle_cache_lock);
	d_list_del(&entry->fhe_link);
	pthread_cond_broadcast(&fd_handle_cache_cond);
	pthread_mutex_unlock(&fd_handle_cache_lock);
	free(entry);

	return NULL;
}

/**
 * Get the daos_cufile_handle_t from a void* that could be either:
 * - A direct daos_cufile_handle_t* (Mode 2 — explicit API)
 * - An fd cast to void* (Mode 1 — dfuse fd-based)
 */
static struct daos_cufile_handle *
get_handle(void *handle)
{
	if (is_fd_handle(handle))
		return resolve_fd_to_handle((int)(intptr_t)handle);
	return (struct daos_cufile_handle *)handle;
}

/** Exported symbol for applications (declared in daos_cufile.h) */
const void *daos_cufile_ops = (const void *)&daos_fs_ops;

/*
 * ============================================================================
 * Public API — Connection and File Management
 * ============================================================================
 */

const void *
daos_cufile_get_ops(void)
{
	return (const void *)&daos_fs_ops;
}
