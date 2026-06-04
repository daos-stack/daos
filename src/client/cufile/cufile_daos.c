/**
 * (C) Copyright 2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * \file
 * DAOS cuFile Plugin — Mode 2 explicit API and transparent registration.
 *
 * Provides daos_cufile_connect/disconnect/open/close for applications that
 * manage DAOS connections explicitly, and daos_cufile_register/deregister
 * for single-call transparent setup.
 */

#include "cufile_internal.h"

int
daos_cufile_connect(const char *pool, const char *cont, daos_cufile_mount_t **mount)
{
	struct mount_cache_entry *entry;
	daos_cufile_mount_t      *m;
	int                       rc;

	if (pool == NULL || cont == NULL || mount == NULL)
		return EINVAL;

	/* Check cache for existing connection to this pool/container */
	pthread_mutex_lock(&mount_cache_lock);
	entry = mount_cache_find_locked(pool, cont);
	if (entry != NULL) {
		entry->mce_refcount++;
		*mount = entry->mce_mount;
		pthread_mutex_unlock(&mount_cache_lock);
		D_DEBUG(DB_IO, "reusing cached mount for %s/%s (refcount=%d)\n", pool, cont,
			entry->mce_refcount);
		return 0;
	}
	pthread_mutex_unlock(&mount_cache_lock);

	m = calloc(1, sizeof(*m));
	if (m == NULL)
		return ENOMEM;

	m->cfm_pool_label = strdup(pool);
	m->cfm_cont_label = strdup(cont);
	if (m->cfm_pool_label == NULL || m->cfm_cont_label == NULL) {
		rc = ENOMEM;
		goto err_labels;
	}

	rc = pthread_mutex_init(&m->cfm_lock, NULL);
	if (rc != 0)
		goto err_labels;

	/* Initialize DAOS client if not already done */
	rc = daos_init();
	if (rc) {
		D_ERROR("daos_init() failed: " DF_RC "\n", DP_RC(rc));
		rc = daos_der2errno(rc);
		goto err_mutex;
	}

	/* Use dfs_connect which handles pool+container open internally */
	rc = dfs_connect(pool, NULL, cont, O_RDWR, NULL, &m->cfm_dfs);
	if (rc != 0) {
		D_ERROR("dfs_connect(%s/%s) failed: %d\n", pool, cont, rc);
		goto err_daos;
	}

	m->cfm_connected = true;

	/* Re-check cache under lock — another thread may have connected
	 * the same pool/container while we were blocked in dfs_connect.
	 */
	pthread_mutex_lock(&mount_cache_lock);
	entry = mount_cache_find_locked(pool, cont);
	if (entry != NULL) {
		/* Another thread won the race — use its mount, discard ours */
		entry->mce_refcount++;
		*mount = entry->mce_mount;
		pthread_mutex_unlock(&mount_cache_lock);

		dfs_disconnect(m->cfm_dfs);
		daos_fini();
		pthread_mutex_destroy(&m->cfm_lock);
		free(m->cfm_pool_label);
		free(m->cfm_cont_label);
		free(m);
		return 0;
	}

	entry = mount_cache_insert_locked(pool, cont, m);
	pthread_mutex_unlock(&mount_cache_lock);

	if (entry == NULL)
		D_WARN("mount cache insert failed, mount not cached\n");

	*mount = m;
	return 0;

err_daos:
	daos_fini();
err_mutex:
	pthread_mutex_destroy(&m->cfm_lock);
err_labels:
	free(m->cfm_pool_label);
	free(m->cfm_cont_label);
	free(m);
	return rc;
}

void
daos_cufile_disconnect(daos_cufile_mount_t *mount)
{
	struct mount_cache_entry *entry;
	bool                      found = false;

	if (mount == NULL)
		return;

	/* Decrement refcount — only disconnect when it reaches zero */
	pthread_mutex_lock(&mount_cache_lock);
	d_list_for_each_entry(entry, &mount_cache_list, mce_link) {
		if (entry->mce_mount == mount) {
			entry->mce_refcount--;
			D_DEBUG(DB_IO, "mount %s/%s refcount=%d\n", entry->mce_pool,
				entry->mce_cont, entry->mce_refcount);
			if (entry->mce_refcount > 0) {
				pthread_mutex_unlock(&mount_cache_lock);
				return;
			}
			mount_cache_remove_locked(entry);
			found = true;
			break;
		}
	}
	pthread_mutex_unlock(&mount_cache_lock);

	if (!found)
		D_DEBUG(DB_IO, "mount not in cache, disconnecting directly\n");

	if (mount->cfm_connected && mount->cfm_dfs) {
		dfs_disconnect(mount->cfm_dfs);
		mount->cfm_dfs = NULL;
	}

	pthread_mutex_destroy(&mount->cfm_lock);
	free(mount->cfm_pool_label);
	free(mount->cfm_cont_label);
	daos_fini();
	free(mount);
}

int
daos_cufile_open(daos_cufile_mount_t *mount, const char *path, int flags,
		 daos_cufile_handle_t **handle)
{
	daos_cufile_handle_t *h;
	bool                  auto_mount = false;
	int                   dfs_flags  = 0;
	int                   rc;

	if (path == NULL || handle == NULL)
		return EINVAL;

	/* Lazy init: auto-connect from env vars when mount is NULL (item 7) */
	if (mount == NULL) {
		const char *env_pool = getenv("DAOS_POOL");
		const char *env_cont = getenv("DAOS_CONT");

		if (env_pool == NULL || env_cont == NULL) {
			D_ERROR("mount is NULL and DAOS_POOL/DAOS_CONT "
				"env vars not set\n");
			return EINVAL;
		}

		rc = daos_cufile_connect(env_pool, env_cont, &mount);
		if (rc != 0)
			return rc;
		auto_mount = true;
	}

	h = calloc(1, sizeof(*h));
	if (h == NULL) {
		rc = ENOMEM;
		goto err_mount;
	}

	h->cfh_mount      = mount;
	h->cfh_flags      = flags;
	h->cfh_auto_mount = auto_mount;

	/* Store path for reconnection (item 8) */
	h->cfh_path = strdup(path);
	if (h->cfh_path == NULL) {
		rc = ENOMEM;
		goto err_handle;
	}

	/* Build DFS access flags */
	if ((flags & O_ACCMODE) == O_RDONLY)
		dfs_flags = O_RDONLY;
	else if ((flags & O_ACCMODE) == O_WRONLY)
		dfs_flags = O_WRONLY;
	else
		dfs_flags = O_RDWR;

	if (flags & O_CREAT) {
		char      *parent_path = NULL;
		char      *base_name   = NULL;
		dfs_obj_t *parent_obj  = NULL;

		/* Split path into parent directory + file name */
		rc = path_split(path, &parent_path, &base_name);
		if (rc != 0)
			goto err_path;

		/* Lookup parent directory */
		rc = dfs_lookup(mount->cfm_dfs, parent_path, O_RDWR, &parent_obj, NULL, NULL);
		if (rc != 0) {
			D_ERROR("dfs_lookup(%s) failed: %d\n", parent_path, rc);
			free(parent_path);
			free(base_name);
			goto err_path;
		}

		/* Create/open the file under parent */
		dfs_flags |= O_CREAT;
		if (flags & O_EXCL)
			dfs_flags |= O_EXCL;
		if (flags & O_TRUNC)
			dfs_flags |= O_TRUNC;

		rc = dfs_open(mount->cfm_dfs, parent_obj, base_name, S_IFREG | 0644, dfs_flags,
			      0 /* default oclass */, 0 /* default chunk */, NULL, &h->cfh_obj);
		if (rc != 0)
			D_ERROR("dfs_open(%s/%s) failed: %d\n", parent_path, base_name, rc);

		dfs_release(parent_obj);
		free(parent_path);
		free(base_name);

		if (rc != 0)
			goto err_path;
	} else {
		/* Open existing file by full path */
		if (flags & O_TRUNC)
			dfs_flags |= O_TRUNC;

		rc = dfs_lookup(mount->cfm_dfs, path, dfs_flags, &h->cfh_obj, NULL, NULL);
		if (rc != 0) {
			D_ERROR("dfs_lookup(%s) failed: %d\n", path, rc);
			goto err_path;
		}
	}

	/* Handle O_TRUNC for existing files (dfs_open handles it for O_CREAT) */
	if ((flags & O_TRUNC) && !(flags & O_CREAT)) {
		rc = dfs_punch(mount->cfm_dfs, h->cfh_obj, 0, DFS_MAX_FSIZE);
		if (rc != 0) {
			D_ERROR("dfs_punch(%s) for O_TRUNC failed: %d\n", path, rc);
			dfs_release(h->cfh_obj);
			goto err_path;
		}
	}

	/* Handle O_APPEND — store flag; write offset calculated at I/O time */
	if (flags & O_APPEND)
		h->cfh_flags |= O_APPEND;

	*handle = h;
	return 0;

err_path:
	free(h->cfh_path);
err_handle:
	free(h);
err_mount:
	if (auto_mount)
		daos_cufile_disconnect(mount);
	return rc;
}

void
daos_cufile_close(daos_cufile_handle_t *handle)
{
	daos_cufile_mount_t *mount;
	bool                 auto_mount;

	if (handle == NULL)
		return;

	mount      = handle->cfh_mount;
	auto_mount = handle->cfh_auto_mount;

	if (handle->cfh_obj) {
		dfs_release(handle->cfh_obj);
		handle->cfh_obj = NULL;
	}

	free(handle->cfh_path);
	free(handle);

	/* Release auto-connected mount refcount */
	if (auto_mount)
		daos_cufile_disconnect(mount);
}

/*
 * ============================================================================
 * Transparent API — single-call setup for cuFile registration
 * ============================================================================
 */

int
daos_cufile_register(const char *path, int flags, const char *pool, const char *cont, void *cfh,
		     daos_cufile_reg_t **reg)
{
	daos_cufile_reg_t    *r;
	daos_cufile_mount_t  *mount = NULL;
	daos_cufile_handle_t *dfh   = NULL;
	int                   rc;

	if (path == NULL || cfh == NULL || reg == NULL)
		return EINVAL;

	r = calloc(1, sizeof(*r));
	if (r == NULL)
		return ENOMEM;

	/* Connect — use explicit args or fall back to env vars */
	if (pool != NULL && cont != NULL) {
		rc = daos_cufile_connect(pool, cont, &mount);
		if (rc != 0)
			goto err_free;
	}

	/* Open — NULL mount triggers lazy init from DAOS_POOL/DAOS_CONT */
	rc = daos_cufile_open(mount, path, flags, &dfh);
	if (rc != 0)
		goto err_disconnect;

	/* Build cuFile descriptor */
	r->dfh                = dfh;
	r->desc.type          = CU_FILE_HANDLE_TYPE_USERSPACE_FS;
	r->desc.handle.handle = (void *)dfh;
	r->desc.fs_ops        = (const CUfileFSOps_t *)daos_cufile_get_ops();

	/*
	 * Store the descriptor pointer in the caller's CUfileHandle_t.
	 * The actual cuFileHandleRegister() must be called by the application
	 * since it requires linking against libcufile. We provide the fully
	 * populated CUfileDescr_t so the app just passes it through:
	 *
	 *   CUfileDescr_t *desc = daos_cufile_get_desc(reg);
	 *   cuFileHandleRegister(&cfh, desc);
	 *
	 * For full transparency without libcufile linkage, we store the
	 * descriptor for retrieval.
	 */
	*reg = r;
	return 0;

err_disconnect:
	if (mount != NULL)
		daos_cufile_disconnect(mount);
err_free:
	free(r);
	return rc;
}

/**
 * Get the CUfileDescr_t from a registration handle.
 *
 * The returned pointer is suitable for passing to cuFileHandleRegister().
 * Valid until daos_cufile_deregister() is called.
 */
const void *
daos_cufile_get_desc(daos_cufile_reg_t *reg)
{
	if (reg == NULL)
		return NULL;
	return &reg->desc;
}

void
daos_cufile_deregister(daos_cufile_reg_t *reg)
{
	if (reg == NULL)
		return;

	if (reg->dfh != NULL)
		daos_cufile_close(reg->dfh);

	free(reg);
}
