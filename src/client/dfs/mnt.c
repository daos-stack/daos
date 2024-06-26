/**
 * (C) Copyright 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/** DFS init, fini, mount related operations */

#define D_LOGFAC DD_FAC(dfs)

#include <math.h>
#include <daos/common.h>
#include <daos/container.h>

#include "dfs_internal.h"

/** protect against concurrent dfs_init/fini calls */
static pthread_mutex_t      module_lock = PTHREAD_MUTEX_INITIALIZER;

/** refcount on how many times dfs_init has been called */
static int                  module_initialized;

/** hashtable for pool open handles */
static struct d_hash_table *poh_hash;

/** hashtable for container open handles */
static struct d_hash_table *coh_hash;

static inline struct dfs_mnt_hdls *
hdl_obj(d_list_t *rlink)
{
	return container_of(rlink, struct dfs_mnt_hdls, entry);
}

static bool
key_cmp(struct d_hash_table *htable, d_list_t *rlink, const void *key, unsigned int ksize)
{
	struct dfs_mnt_hdls *hdl = hdl_obj(rlink);

	return (strncmp(hdl->value, key, ksize) == 0);
}

static void
rec_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	hdl_obj(rlink)->ref++;
}

static bool
rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfs_mnt_hdls *hdl = hdl_obj(rlink);

	D_ASSERT(hdl->ref > 0);
	hdl->ref--;
	return (hdl->ref == 0);
}

static void
rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfs_mnt_hdls *hdl = hdl_obj(rlink);
	int                  rc;

	D_ASSERT(d_hash_rec_unlinked(&hdl->entry));
	D_ASSERT(hdl->ref == 0);

	if (hdl->type == DFS_H_POOL) {
		rc = daos_pool_disconnect(hdl->handle, NULL);
		if (rc)
			D_ERROR("daos_pool_connect() Failed " DF_RC "\n", DP_RC(rc));
	} else if (hdl->type == DFS_H_CONT) {
		rc = daos_cont_close(hdl->handle, NULL);
		if (rc)
			D_ERROR("daos_cont_close() Failed " DF_RC "\n", DP_RC(rc));
	} else {
		D_ASSERT(0);
	}
	D_FREE(hdl);
}

static uint32_t
rec_hash(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfs_mnt_hdls *hdl = hdl_obj(rlink);

	return d_hash_string_u32(hdl->value, strlen(hdl->value));
}

static d_hash_table_ops_t hdl_hash_ops = {.hop_key_cmp    = key_cmp,
					  .hop_rec_addref = rec_addref,
					  .hop_rec_decref = rec_decref,
					  .hop_rec_free   = rec_free,
					  .hop_rec_hash   = rec_hash};

bool
dfs_is_init()
{
	D_MUTEX_LOCK(&module_lock);
	if (module_initialized > 0) {
		D_MUTEX_UNLOCK(&module_lock);
		return true;
	}
	D_MUTEX_UNLOCK(&module_lock);
	return false;
}

int
dfs_init(void)
{
	int rc;

	D_MUTEX_LOCK(&module_lock);
	if (module_initialized > 0) {
		/** already initialized, report success */
		module_initialized++;
		D_GOTO(unlock, rc = 0);
	}

	rc = daos_init();
	if (rc)
		D_GOTO(unlock, rc = daos_der2errno(rc));

	rc = d_hash_table_create(D_HASH_FT_EPHEMERAL | D_HASH_FT_LRU, 4, NULL, &hdl_hash_ops,
				 &poh_hash);
	if (rc) {
		D_ERROR("Failed to init pool handle hash " DF_RC "\n", DP_RC(rc));
		D_GOTO(out_fini, rc = daos_der2errno(rc));
	}

	rc = d_hash_table_create(D_HASH_FT_EPHEMERAL | D_HASH_FT_LRU, 4, NULL, &hdl_hash_ops,
				 &coh_hash);
	if (rc) {
		D_ERROR("Failed to init container handle hash " DF_RC "\n", DP_RC(rc));
		D_GOTO(out_poh, rc = daos_der2errno(rc));
	}

	module_initialized++;
	D_GOTO(unlock, rc = 0);

out_poh:
	d_hash_table_destroy(poh_hash, false);
out_fini:
	daos_fini();
unlock:
	D_MUTEX_UNLOCK(&module_lock);
	return rc;
}

int
dfs_fini(void)
{
	d_list_t *rlink;
	int       rc;

	D_MUTEX_LOCK(&module_lock);
	if (module_initialized == 0) {
		/** calling fini without init, report an error */
		D_GOTO(unlock, rc = EINVAL);
	} else if (module_initialized > 1) {
		module_initialized--;
		D_GOTO(unlock, rc = 0);
	}

	while (1) {
		rlink = d_hash_rec_first(coh_hash);
		if (rlink == NULL)
			break;

		d_hash_rec_decref(coh_hash, rlink);
	}
	d_hash_table_destroy(coh_hash, false);
	coh_hash = NULL;

	while (1) {
		rlink = d_hash_rec_first(poh_hash);
		if (rlink == NULL)
			break;

		d_hash_rec_decref(poh_hash, rlink);
	}
	d_hash_table_destroy(poh_hash, false);
	poh_hash = NULL;

	rc = daos_fini();
	if (rc)
		D_GOTO(unlock, rc = daos_der2errno(rc));

	module_initialized = 0;
unlock:
	D_MUTEX_UNLOCK(&module_lock);
	return rc;
}

struct dfs_mnt_hdls *
dfs_hdl_lookup(const char *str, int type, const char *pool)
{
	d_list_t *rlink = NULL;

	if (type == DFS_H_POOL) {
		rlink = d_hash_rec_find(poh_hash, str, strlen(str) + 1);
	} else if (type == DFS_H_CONT) {
		char        cont_pool[DAOS_PROP_LABEL_MAX_LEN * 2 + 2];
		daos_size_t len;

		D_ASSERT(pool);
		len = snprintf(cont_pool, DAOS_PROP_LABEL_MAX_LEN * 2 + 2, "%s/%s", pool, str);
		D_ASSERT(len == strlen(pool) + strlen(str) + 1);
		rlink = d_hash_rec_find(coh_hash, cont_pool, len + 1);
	} else {
		D_ASSERT(0);
	}
	if (rlink == NULL)
		return NULL;

	return hdl_obj(rlink);
}

void
dfs_hdl_release(struct dfs_mnt_hdls *hdl)
{
	if (hdl->type == DFS_H_POOL)
		d_hash_rec_decref(poh_hash, &hdl->entry);
	if (hdl->type == DFS_H_CONT)
		d_hash_rec_decref(coh_hash, &hdl->entry);
}

int
dfs_hdl_insert(const char *str, int type, const char *pool, daos_handle_t *oh,
	       struct dfs_mnt_hdls **_hdl)
{
	struct dfs_mnt_hdls *hdl;
	d_list_t            *rlink;
	int                  rc = 0;

	D_ALLOC_PTR(hdl);
	if (hdl == NULL)
		return ENOMEM;

	hdl->type                           = type;
	hdl->handle.cookie                  = oh->cookie;
	hdl->ref                            = 2;
	hdl->value[DAOS_PROP_LABEL_MAX_LEN] = 0;

	if (type == DFS_H_POOL) {
		strncpy(hdl->value, str, DAOS_PROP_LABEL_MAX_LEN + 1);
		rlink = d_hash_rec_find_insert(poh_hash, hdl->value, strlen(hdl->value) + 1,
					       &hdl->entry);
		if (rlink != &hdl->entry) {
			/** handle EEXIST case */
			rc = daos_pool_disconnect(*oh, NULL);
			if (rc)
				D_GOTO(err_free, rc = daos_der2errno(rc));
			D_FREE(hdl);
			hdl        = container_of(rlink, struct dfs_mnt_hdls, entry);
			oh->cookie = hdl->handle.cookie;
		}
	} else if (type == DFS_H_CONT) {
		char        cont_pool[DAOS_PROP_LABEL_MAX_LEN * 2 + 1];
		daos_size_t len;

		D_ASSERT(pool);
		len = snprintf(cont_pool, strlen(pool) + strlen(str) + 2, "%s/%s", pool, str);
		D_ASSERT(len == strlen(pool) + strlen(str) + 1);
		strncpy(hdl->value, cont_pool, len + 1);
		rlink = d_hash_rec_find_insert(coh_hash, hdl->value, len + 1, &hdl->entry);
		if (rlink != &hdl->entry) {
			/** handle EEXIST case */
			rc = daos_cont_close(*oh, NULL);
			if (rc)
				D_GOTO(err_free, rc = daos_der2errno(rc));
			D_FREE(hdl);
			hdl        = container_of(rlink, struct dfs_mnt_hdls, entry);
			oh->cookie = hdl->handle.cookie;
		}
	} else {
		D_ASSERT(0);
	}

	*_hdl = hdl;
	return 0;
err_free:
	D_FREE(hdl);
	return rc;
}

int
dfs_hdl_cont_destroy(const char *pool, const char *cont, bool force)
{
	d_list_t            *rlink = NULL;
	struct dfs_mnt_hdls *hdl;
	char                 cont_pool[DAOS_PROP_LABEL_MAX_LEN * 2 + 1];
	daos_size_t          len;

	if (coh_hash == NULL)
		return 0;

	D_ASSERT(pool);
	len = snprintf(cont_pool, strlen(pool) + strlen(cont) + 2, "%s/%s", pool, cont);
	D_ASSERT(len == strlen(pool) + strlen(cont) + 1);

	if (force) {
		if (!d_hash_rec_delete(coh_hash, cont_pool, len + 1))
			return ENOENT;
	}

	rlink = d_hash_rec_find(coh_hash, cont_pool, len + 1);
	if (rlink == NULL)
		return ENOENT;

	hdl = hdl_obj(rlink);
	if (hdl->ref > 2) {
		D_ERROR("Container handle is still open or DFS mount still connected\n");
		return EBUSY;
	}
	d_hash_rec_decref(coh_hash, rlink);
	d_hash_rec_decref(coh_hash, rlink);
	return 0;
}

void
dfs_free_sb_layout(daos_iod_t *iods[])
{
	D_FREE(*iods);
}

int
dfs_connect(const char *pool, const char *sys, const char *cont, int flags, dfs_attr_t *attr,
	    dfs_t **_dfs)
{
	daos_handle_t        poh         = {0};
	daos_handle_t        coh         = {0};
	bool                 pool_h_bump = false;
	bool                 cont_h_bump = false;
	struct dfs_mnt_hdls *pool_hdl    = NULL;
	struct dfs_mnt_hdls *cont_hdl    = NULL;
	dfs_t               *dfs         = NULL;
	int                  amode, cmode;
	int                  rc, rc2;

	if (_dfs == NULL || pool == NULL || cont == NULL)
		return EINVAL;

	if (!dfs_is_init()) {
		D_ERROR("dfs_init() must be called before dfs_connect() can be used\n");
		return EACCES;
	}

	amode = (flags & O_ACCMODE);

	pool_hdl = dfs_hdl_lookup(pool, DFS_H_POOL, NULL);
	if (pool_hdl == NULL) {
		/** Connect to pool */
		rc = daos_pool_connect(pool, sys, (amode == O_RDWR) ? DAOS_PC_RW : DAOS_PC_RO, &poh,
				       NULL, NULL);
		if (rc) {
			D_ERROR("Failed to connect to pool %s " DF_RC "\n", pool, DP_RC(rc));
			D_GOTO(err, rc = daos_der2errno(rc));
		}

		rc = dfs_hdl_insert(pool, DFS_H_POOL, NULL, &poh, &pool_hdl);
		if (rc)
			D_GOTO(err, rc);
	} else {
		poh.cookie = pool_hdl->handle.cookie;
	}
	pool_h_bump = true;

	cmode = (amode == O_RDWR) ? DAOS_COO_RW : DAOS_COO_RO;

	cont_hdl = dfs_hdl_lookup(cont, DFS_H_CONT, pool);
	if (cont_hdl == NULL) {
		rc = daos_cont_open(poh, cont, cmode, &coh, NULL, NULL);
		if (rc == -DER_NONEXIST && (flags & O_CREAT)) {
			uuid_t cuuid;

			rc = dfs_cont_create_with_label(poh, cont, attr, &cuuid, &coh, &dfs);
			/** if someone got there first, re-open */
			if (rc == EEXIST) {
				rc = daos_cont_open(poh, cont, cmode, &coh, NULL, NULL);
				if (rc) {
					D_ERROR("Failed to open container %s " DF_RC "\n", cont,
						DP_RC(rc));
					D_GOTO(err, rc = daos_der2errno(rc));
				}
				goto mount;
			} else if (rc) {
				D_ERROR("Failed to create DFS container: %d\n", rc);
			}
		} else if (rc == 0) {
			int b;
mount:
			/*
			 * It could be that someone has created the container but has not created
			 * the SB yet (cont create and sb create are not transactional), so try a
			 * few times to mount with some backoff.
			 */
			for (b = 0; b < 7; b++) {
				rc = dfs_mount(poh, coh, amode, &dfs);
				if (rc == ENOENT)
					usleep(pow(10, b));
				else
					break;
			}
			if (rc) {
				D_ERROR("Failed to mount DFS: %d (%s)\n", rc, strerror(rc));
				D_GOTO(err, rc);
			}
		} else {
			D_ERROR("Failed to open container %s " DF_RC "\n", cont, DP_RC(rc));
			D_GOTO(err, rc = daos_der2errno(rc));
		}

		rc = dfs_hdl_insert(cont, DFS_H_CONT, pool, &coh, &cont_hdl);
		if (rc)
			D_GOTO(err, rc);
	} else {
		cont_h_bump = true;
		rc          = dfs_mount(poh, cont_hdl->handle, amode, &dfs);
		if (rc) {
			D_ERROR("Failed to mount DFS: %d (%s)\n", rc, strerror(rc));
			D_GOTO(err, rc);
		}
	}

	dfs->pool_hdl = pool_hdl;
	dfs->cont_hdl = cont_hdl;
	dfs->mounted  = DFS_MOUNT_ALL;
	*_dfs         = dfs;

	return rc;

err:
	if (dfs) {
		rc2 = dfs_umount(dfs);
		if (rc2)
			D_ERROR("dfs_umount() Failed %d\n", rc2);
	}

	if (cont_h_bump) {
		dfs_hdl_release(cont_hdl);
	} else if (daos_handle_is_valid(coh)) {
		rc2 = daos_cont_close(coh, NULL);
		if (rc2)
			D_ERROR("daos_cont_close() Failed " DF_RC "\n", DP_RC(rc2));
	}

	if (pool_h_bump) {
		dfs_hdl_release(pool_hdl);
	} else if (daos_handle_is_valid(poh)) {
		rc2 = daos_pool_disconnect(poh, NULL);
		if (rc2)
			D_ERROR("daos_pool_disconnect() Failed " DF_RC "\n", DP_RC(rc2));
	}

	return rc;
}

int
dfs_disconnect(dfs_t *dfs)
{
	int rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->mounted != DFS_MOUNT_ALL) {
		D_ERROR("DFS is not mounted with dfs_connect() or dfs_global2local_all()\n");
		return EINVAL;
	}

	dfs_hdl_release(dfs->cont_hdl);
	dfs_hdl_release(dfs->pool_hdl);

	/** set mounted flag MOUNT to be able to just umount */
	dfs->mounted = DFS_MOUNT;
	rc           = dfs_umount(dfs);
	if (rc) {
		D_ERROR("dfs_umount() Failed %d\n", rc);
		D_GOTO(out, rc);
	}
out:
	return rc;
}

int
dfs_destroy(const char *pool, const char *sys, const char *cont, int force, daos_event_t *ev)
{
	daos_handle_t        poh         = {0};
	bool                 pool_h_bump = false;
	struct dfs_mnt_hdls *pool_hdl    = NULL;
	int                  rc, rc2;

	if (pool == NULL || cont == NULL)
		return EINVAL;

	if (!dfs_is_init()) {
		D_ERROR("dfs_init() must be called before dfs_destroy() can be used\n");
		return EACCES;
	}

	pool_hdl = dfs_hdl_lookup(pool, DFS_H_POOL, NULL);
	if (pool_hdl == NULL) {
		/** Connect to pool */
		rc = daos_pool_connect(pool, sys, DAOS_PC_RW, &poh, NULL, NULL);
		if (rc) {
			D_ERROR("Failed to connect to pool %s " DF_RC "\n", pool, DP_RC(rc));
			D_GOTO(err, rc = daos_der2errno(rc));
		}

		rc = dfs_hdl_insert(pool, DFS_H_POOL, NULL, &poh, &pool_hdl);
		if (rc)
			D_GOTO(err, rc);
	} else {
		poh.cookie = pool_hdl->handle.cookie;
	}
	pool_h_bump = true;

	rc = dfs_hdl_cont_destroy(pool, cont, force);
	if (rc != 0 && rc != ENOENT) {
		D_ERROR("Failed to destroy cont hash entry: %d (%s)\n", rc, strerror(rc));
		return rc;
	}

	rc = daos_cont_destroy(poh, cont, force, ev);
	if (rc) {
		D_ERROR("Failed to destroy container %s " DF_RC "\n", cont, DP_RC(rc));
		D_GOTO(err, rc = daos_der2errno(rc));
	}
	dfs_hdl_release(pool_hdl);
	return rc;
err:
	if (pool_h_bump) {
		dfs_hdl_release(pool_hdl);
	} else if (daos_handle_is_valid(poh)) {
		rc2 = daos_pool_disconnect(poh, NULL);
		if (rc2)
			D_ERROR("daos_pool_disconnect() Failed " DF_RC "\n", DP_RC(rc2));
	}
	return rc;
}

int
dfs_mount(daos_handle_t poh, daos_handle_t coh, int flags, dfs_t **_dfs)
{
	dfs_t                     *dfs;
	daos_prop_t               *prop;
	struct daos_prop_entry    *entry;
	struct daos_prop_co_roots *roots;
	struct dfs_entry           root_dir;
	int                        amode, omode;
	int                        rc;
	int                        i;
	uint32_t  props[] = {DAOS_PROP_CO_LAYOUT_TYPE, DAOS_PROP_CO_ROOTS, DAOS_PROP_CO_REDUN_FAC};
	const int num_props = ARRAY_SIZE(props);

	if (_dfs == NULL)
		return EINVAL;

	amode = (flags & O_ACCMODE);
	omode = get_daos_obj_mode(flags);
	if (omode == -1)
		return EINVAL;

	prop = daos_prop_alloc(num_props);
	if (prop == NULL)
		return ENOMEM;

	for (i = 0; i < num_props; i++)
		prop->dpp_entries[i].dpe_type = props[i];

	rc = daos_cont_query(coh, NULL, prop, NULL);
	if (rc) {
		D_ERROR("daos_cont_query() failed, " DF_RC "\n", DP_RC(rc));
		D_GOTO(err_prop, rc = daos_der2errno(rc));
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_LAYOUT_TYPE);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_LAYOUT_POSIX) {
		D_ERROR("container is not of type POSIX\n");
		D_GOTO(err_prop, rc = EINVAL);
	}

	D_ALLOC_PTR(dfs);
	if (dfs == NULL)
		D_GOTO(err_prop, rc = ENOMEM);

	dfs->poh   = poh;
	dfs->coh   = coh;
	dfs->amode = amode;

	rc = D_MUTEX_INIT(&dfs->lock, NULL);
	if (rc != 0)
		D_GOTO(err_dfs, rc = daos_der2errno(rc));

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_ROOTS);
	D_ASSERT(entry != NULL);
	roots = (struct daos_prop_co_roots *)entry->dpe_val_ptr;
	if (daos_obj_id_is_nil(roots->cr_oids[0]) || daos_obj_id_is_nil(roots->cr_oids[1])) {
		D_ERROR("Invalid superblock or root object ID\n");
		D_GOTO(err_dfs, rc = EIO);
	}

	dfs->super_oid       = roots->cr_oids[0];
	dfs->root.oid        = roots->cr_oids[1];
	dfs->root.parent_oid = dfs->super_oid;

	/** Verify SB */
	rc = open_sb(coh, false, false, omode, dfs->super_oid, &dfs->attr, &dfs->super_oh,
		     &dfs->layout_v);
	if (rc)
		D_GOTO(err_dfs, rc);

	/** set oid hints for files and dirs */
	if (dfs->attr.da_hints[0] != 0) {
		entry = daos_prop_entry_get(prop, DAOS_PROP_CO_REDUN_FAC);
		D_ASSERT(entry != NULL);

		rc = get_oclass_hints(dfs->attr.da_hints, &dfs->dir_oclass_hint,
				      &dfs->file_oclass_hint, entry->dpe_val);
		if (rc)
			D_GOTO(err_prop, rc);
	}

	/*
	 * If container was created with balanced mode, only balanced mode
	 * mounting should be allowed.
	 */
	if ((dfs->attr.da_mode & MODE_MASK) == DFS_BALANCED) {
		if ((flags & MODE_MASK) != DFS_BALANCED) {
			D_ERROR("Can't use non-balanced mount flag on a POSIX"
				" container created with balanced mode.\n");
			D_GOTO(err_super, rc = EPERM);
		}
		dfs->use_dtx = true;
		D_DEBUG(DB_ALL, "DFS mount in Balanced mode.\n");
	} else {
		if ((dfs->attr.da_mode & MODE_MASK) != DFS_RELAXED) {
			D_ERROR("Invalid DFS mode in Superblock\n");
			D_GOTO(err_super, rc = EINVAL);
		}

		if ((flags & MODE_MASK) == DFS_BALANCED) {
			dfs->use_dtx = true;
			D_DEBUG(DB_ALL, "DFS mount in Balanced mode.\n");
		} else {
			dfs->use_dtx = false;
			D_DEBUG(DB_ALL, "DFS mount in Relaxed mode.\n");
		}
	}

	/*
	 * For convenience, keep env variable option for now that overrides the
	 * default input setting, only if container was created with relaxed
	 * mode.
	 */
	if ((dfs->attr.da_mode & MODE_MASK) == DFS_RELAXED)
		d_getenv_bool("DFS_USE_DTX", &dfs->use_dtx);

	/** Check if super object has the root entry */
	strcpy(dfs->root.name, "/");
	rc = open_dir(dfs, NULL, amode, flags, &root_dir, 1, &dfs->root);
	if (rc) {
		D_ERROR("Failed to open root object: %d (%s)\n", rc, strerror(rc));
		D_GOTO(err_super, rc);
	}

	dfs->root_stbuf.st_nlink        = 1;
	dfs->root_stbuf.st_size         = sizeof(root_dir);
	dfs->root_stbuf.st_mode         = dfs->root.mode;
	dfs->root_stbuf.st_uid          = root_dir.uid;
	dfs->root_stbuf.st_gid          = root_dir.gid;
	dfs->root_stbuf.st_mtim.tv_sec  = root_dir.mtime;
	dfs->root_stbuf.st_mtim.tv_nsec = root_dir.mtime_nano;
	dfs->root_stbuf.st_ctim.tv_sec  = root_dir.ctime;
	dfs->root_stbuf.st_ctim.tv_nsec = root_dir.ctime_nano;
	if (tspec_gt(dfs->root_stbuf.st_ctim, dfs->root_stbuf.st_mtim)) {
		dfs->root_stbuf.st_atim.tv_sec  = root_dir.ctime;
		dfs->root_stbuf.st_atim.tv_nsec = root_dir.ctime_nano;
	} else {
		dfs->root_stbuf.st_atim.tv_sec  = root_dir.mtime;
		dfs->root_stbuf.st_atim.tv_nsec = root_dir.mtime_nano;
	}

	/** if RW, allocate an OID for the namespace */
	if (amode == O_RDWR) {
		rc = daos_cont_alloc_oids(coh, 1, &dfs->oid.lo, NULL);
		if (rc) {
			D_ERROR("daos_cont_alloc_oids() Failed, " DF_RC "\n", DP_RC(rc));
			D_GOTO(err_root, rc = daos_der2errno(rc));
		}

		/*
		 * if this is the first time we allocate on this container,
		 * account 0 for SB, 1 for root obj.
		 */
		if (dfs->oid.lo == RESERVED_LO)
			dfs->oid.hi = ROOT_HI + 1;
		else
			dfs->oid.hi = 0;
	}

	dfs->mounted = DFS_MOUNT;
	*_dfs        = dfs;

	if (amode == O_RDONLY) {
		bool d_enable_dcache = false;

		d_getenv_bool("DFS_ENABLE_DCACHE", &d_enable_dcache);
		if (d_enable_dcache) {
			rc = dcache_create(dfs, DCACHE_SIZE_BITS, 0, 0, 0);
			if (rc) {
				D_ERROR("Failed to create dcache: %d (%s)\n", rc, strerror(rc));
				goto err_root;
			}
		}
	}

	daos_prop_free(prop);
	return rc;
err_root:
	daos_obj_close(dfs->root.oh, NULL);
err_super:
	daos_obj_close(dfs->super_oh, NULL);
err_dfs:
	D_FREE(dfs);
err_prop:
	daos_prop_free(prop);
	return rc;
}

int
dfs_umount(dfs_t *dfs)
{
	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->mounted != DFS_MOUNT) {
		D_ERROR("DFS is not mounted with dfs_mount() or dfs_global2local()\n");
		return EINVAL;
	}

	if (dfs->dcache) {
		int rc;

		rc = dcache_destroy(dfs);
		if (rc != 0) {
			D_ERROR("Failed to destroy dcache: %d (%s)\n", rc, strerror(rc));
			return rc;
		}
	}
	D_MUTEX_LOCK(&dfs->lock);
	if (dfs->poh_refcount != 0) {
		D_ERROR("Pool open handle refcount not 0\n");
		D_MUTEX_UNLOCK(&dfs->lock);
		return EBUSY;
	}
	if (dfs->coh_refcount != 0) {
		D_ERROR("Cont open handle refcount not 0\n");
		D_MUTEX_UNLOCK(&dfs->lock);
		return EBUSY;
	}
	D_MUTEX_UNLOCK(&dfs->lock);

	daos_obj_close(dfs->root.oh, NULL);
	daos_obj_close(dfs->super_oh, NULL);

	D_FREE(dfs->prefix);
	D_MUTEX_DESTROY(&dfs->lock);
	D_FREE(dfs);

	return 0;
}

int
dfs_pool_get(dfs_t *dfs, daos_handle_t *poh)
{
	if (dfs == NULL || !dfs->mounted)
		return EINVAL;

	D_MUTEX_LOCK(&dfs->lock);
	dfs->poh_refcount++;
	D_MUTEX_UNLOCK(&dfs->lock);

	*poh = dfs->poh;
	return 0;
}

int
dfs_pool_put(dfs_t *dfs, daos_handle_t poh)
{
	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (poh.cookie != dfs->poh.cookie) {
		D_ERROR("Pool handle is not the same as the DFS Mount handle\n");
		return EINVAL;
	}

	D_MUTEX_LOCK(&dfs->lock);
	if (dfs->poh_refcount <= 0) {
		D_ERROR("Invalid pool handle refcount\n");
		D_MUTEX_UNLOCK(&dfs->lock);
		return EINVAL;
	}
	dfs->poh_refcount--;
	D_MUTEX_UNLOCK(&dfs->lock);

	return 0;
}

int
dfs_cont_get(dfs_t *dfs, daos_handle_t *coh)
{
	if (dfs == NULL || !dfs->mounted)
		return EINVAL;

	D_MUTEX_LOCK(&dfs->lock);
	dfs->coh_refcount++;
	D_MUTEX_UNLOCK(&dfs->lock);

	*coh = dfs->coh;
	return 0;
}

int
dfs_cont_put(dfs_t *dfs, daos_handle_t coh)
{
	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (coh.cookie != dfs->coh.cookie) {
		D_ERROR("Cont handle is not the same as the DFS Mount handle\n");
		return EINVAL;
	}

	D_MUTEX_LOCK(&dfs->lock);
	if (dfs->coh_refcount <= 0) {
		D_ERROR("Invalid cont handle refcount\n");
		D_MUTEX_UNLOCK(&dfs->lock);
		return EINVAL;
	}
	dfs->coh_refcount--;
	D_MUTEX_UNLOCK(&dfs->lock);

	return 0;
}

int
dfs_query(dfs_t *dfs, dfs_attr_t *attr)
{
	if (dfs == NULL || !dfs->mounted || attr == NULL)
		return EINVAL;

	memcpy(attr, &dfs->attr, sizeof(dfs_attr_t));

	return 0;
}

/* Structure of global buffer for dfs */
struct dfs_glob {
	uint32_t         magic;
	bool             use_dtx;
	dfs_layout_ver_t layout_v;
	int32_t          amode;
	uid_t            uid;
	gid_t            gid;
	uint64_t         id;
	daos_size_t      chunk_size;
	daos_oclass_id_t oclass;
	daos_oclass_id_t dir_oclass;
	daos_oclass_id_t file_oclass;
	uuid_t           cont_uuid;
	uuid_t           coh_uuid;
	daos_obj_id_t    super_oid;
	daos_obj_id_t    root_oid;
};

static inline void
swap_dfs_glob(struct dfs_glob *dfs_params)
{
	D_ASSERT(dfs_params != NULL);

	D_SWAP32S(&dfs_params->magic);
	D_SWAP32S(&dfs_params->use_dtx);
	D_SWAP16S(&dfs_params->layout_v);
	D_SWAP32S(&dfs_params->amode);
	D_SWAP32S(&dfs_params->uid);
	D_SWAP32S(&dfs_params->gid);
	D_SWAP64S(&dfs_params->id);
	D_SWAP64S(&dfs_params->chunk_size);
	D_SWAP16S(&dfs_params->oclass);
	D_SWAP16S(&dfs_params->dir_oclass);
	D_SWAP16S(&dfs_params->file_oclass);
	/* skip cont_uuid */
	/* skip coh_uuid */
}

static inline daos_size_t
dfs_glob_buf_size()
{
	return sizeof(struct dfs_glob);
}

int
dfs_local2global(dfs_t *dfs, d_iov_t *glob)
{
	struct dfs_glob *dfs_params;
	uuid_t           coh_uuid;
	uuid_t           cont_uuid;
	daos_size_t      glob_buf_size;
	int              rc = 0;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;

	if (glob == NULL) {
		D_ERROR("Invalid parameter, NULL glob pointer.\n");
		return EINVAL;
	}

	if (glob->iov_buf != NULL &&
	    (glob->iov_buf_len == 0 || glob->iov_buf_len < glob->iov_len)) {
		D_ERROR("Invalid parameter of glob, iov_buf %p, iov_buf_len "
			"" DF_U64 ", iov_len " DF_U64 ".\n",
			glob->iov_buf, glob->iov_buf_len, glob->iov_len);
		return EINVAL;
	}

	glob_buf_size = dfs_glob_buf_size();

	if (glob->iov_buf == NULL) {
		glob->iov_buf_len = glob_buf_size;
		return 0;
	}

	rc = dc_cont_hdl2uuid(dfs->coh, &coh_uuid, &cont_uuid);
	if (rc != 0)
		return daos_der2errno(rc);

	if (glob->iov_buf_len < glob_buf_size) {
		D_DEBUG(DB_ANY,
			"Larger glob buffer needed (" DF_U64 " bytes "
			"provided, " DF_U64 " required).\n",
			glob->iov_buf_len, glob_buf_size);
		glob->iov_buf_len = glob_buf_size;
		return ENOBUFS;
	}
	glob->iov_len = glob_buf_size;

	/* init global handle */
	dfs_params              = (struct dfs_glob *)glob->iov_buf;
	dfs_params->magic       = DFS_GLOB_MAGIC;
	dfs_params->use_dtx     = dfs->use_dtx;
	dfs_params->layout_v    = dfs->layout_v;
	dfs_params->amode       = dfs->amode;
	dfs_params->super_oid   = dfs->super_oid;
	dfs_params->root_oid    = dfs->root.oid;
	dfs_params->uid         = dfs->uid;
	dfs_params->gid         = dfs->gid;
	dfs_params->id          = dfs->attr.da_id;
	dfs_params->chunk_size  = dfs->attr.da_chunk_size;
	dfs_params->oclass      = dfs->attr.da_oclass_id;
	dfs_params->dir_oclass  = dfs->attr.da_dir_oclass_id;
	dfs_params->file_oclass = dfs->attr.da_file_oclass_id;
	uuid_copy(dfs_params->coh_uuid, coh_uuid);
	uuid_copy(dfs_params->cont_uuid, cont_uuid);

	return 0;
}

int
dfs_global2local(daos_handle_t poh, daos_handle_t coh, int flags, d_iov_t glob, dfs_t **_dfs)
{
	dfs_t           *dfs;
	struct dfs_glob *dfs_params;
	int              obj_mode;
	uuid_t           coh_uuid;
	uuid_t           cont_uuid;
	int              rc = 0;

	if (_dfs == NULL)
		return EINVAL;

	if (glob.iov_buf == NULL || glob.iov_buf_len < glob.iov_len ||
	    glob.iov_len != dfs_glob_buf_size()) {
		D_ERROR("Invalid parameter of glob, iov_buf %p, "
			"iov_buf_len " DF_U64 ", iov_len " DF_U64 ".\n",
			glob.iov_buf, glob.iov_buf_len, glob.iov_len);
		return EINVAL;
	}

	dfs_params = (struct dfs_glob *)glob.iov_buf;
	if (dfs_params->magic == D_SWAP32(DFS_GLOB_MAGIC)) {
		swap_dfs_glob(dfs_params);
		D_ASSERT(dfs_params->magic == DFS_GLOB_MAGIC);

	} else if (dfs_params->magic != DFS_GLOB_MAGIC) {
		D_ERROR("Bad magic value: %#x.\n", dfs_params->magic);
		return EINVAL;
	}

	D_ASSERT(dfs_params != NULL);

	/** Check container uuid mismatch */
	rc = dc_cont_hdl2uuid(coh, &coh_uuid, &cont_uuid);
	if (rc != 0)
		return daos_der2errno(rc);
	if (uuid_compare(cont_uuid, dfs_params->cont_uuid) != 0) {
		D_ERROR("Container uuid mismatch, in coh: " DF_UUID ", "
			"in dfs_params:" DF_UUID "\n",
			DP_UUID(cont_uuid), DP_UUID(dfs_params->cont_uuid));
		return EINVAL;
	}

	/** Create the DFS handle with no RPCs */
	D_ALLOC_PTR(dfs);
	if (dfs == NULL)
		return ENOMEM;

	dfs->poh                    = poh;
	dfs->coh                    = coh;
	dfs->use_dtx                = dfs_params->use_dtx;
	dfs->layout_v               = dfs_params->layout_v;
	dfs->amode                  = (flags == 0) ? dfs_params->amode : (flags & O_ACCMODE);
	dfs->uid                    = dfs_params->uid;
	dfs->gid                    = dfs_params->gid;
	dfs->attr.da_id             = dfs_params->id;
	dfs->attr.da_chunk_size     = dfs_params->chunk_size;
	dfs->attr.da_oclass_id      = dfs_params->oclass;
	dfs->attr.da_dir_oclass_id  = dfs_params->dir_oclass;
	dfs->attr.da_file_oclass_id = dfs_params->file_oclass;

	dfs->super_oid       = dfs_params->super_oid;
	dfs->root.oid        = dfs_params->root_oid;
	dfs->root.parent_oid = dfs->super_oid;
	if (daos_obj_id_is_nil(dfs->super_oid) || daos_obj_id_is_nil(dfs->root.oid)) {
		D_ERROR("Invalid superblock or root object ID\n");
		D_FREE(dfs);
		return EIO;
	}

	/** allocate a new oid on the next file or dir creation */
	dfs->oid.lo = 0;
	dfs->oid.hi = MAX_OID_HI;

	rc = D_MUTEX_INIT(&dfs->lock, NULL);
	if (rc != 0) {
		D_FREE(dfs);
		return daos_der2errno(rc);
	}

	/** Open SB object */
	rc = daos_obj_open(coh, dfs->super_oid, DAOS_OO_RO, &dfs->super_oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() failed, " DF_RC "\n", DP_RC(rc));
		D_GOTO(err_dfs, rc = daos_der2errno(rc));
	}

	/* Open Root Object */
	strcpy(dfs->root.name, "/");
	dfs->root.mode = S_IFDIR | 0755;

	obj_mode = get_daos_obj_mode(flags ? flags : dfs_params->amode);
	rc       = daos_obj_open(coh, dfs->root.oid, obj_mode, &dfs->root.oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() failed, " DF_RC "\n", DP_RC(rc));
		daos_obj_close(dfs->super_oh, NULL);
		D_GOTO(err_dfs, rc = daos_der2errno(rc));
	}

	dfs->mounted = DFS_MOUNT;
	*_dfs        = dfs;

	return rc;
err_dfs:
	D_MUTEX_DESTROY(&dfs->lock);
	D_FREE(dfs);
	return rc;
}

int
dfs_local2global_all(dfs_t *dfs, d_iov_t *glob)
{
	d_iov_t     pool_iov = {NULL, 0, 0};
	d_iov_t     cont_iov = {NULL, 0, 0};
	d_iov_t     dfs_iov  = {NULL, 0, 0};
	daos_size_t pool_len, cont_len, total_size;
	char       *ptr;
	int         rc = 0;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (glob == NULL) {
		D_ERROR("Invalid parameter, NULL glob pointer.\n");
		return EINVAL;
	}
	if (glob->iov_buf != NULL && (glob->iov_buf_len == 0)) {
		D_ERROR("Invalid parameter of glob, iov_buf %p, iov_buf_len"
			"" DF_U64 ", iov_len " DF_U64 ".\n",
			glob->iov_buf, glob->iov_buf_len, glob->iov_len);
		return EINVAL;
	}

	rc = daos_pool_local2global(dfs->poh, &pool_iov);
	if (rc)
		return daos_der2errno(rc);

	rc = daos_cont_local2global(dfs->coh, &cont_iov);
	if (rc)
		return daos_der2errno(rc);

	rc = dfs_local2global(dfs, &dfs_iov);
	if (rc)
		return rc;

	pool_len   = strlen(dfs->pool_hdl->value) + 1;
	cont_len   = strlen(dfs->cont_hdl->value) + 1;
	total_size = pool_iov.iov_buf_len + cont_iov.iov_buf_len + dfs_iov.iov_buf_len + pool_len +
		     cont_len + sizeof(daos_size_t) * 5;

	if (glob->iov_buf == NULL) {
		glob->iov_buf_len = total_size;
		return 0;
	}

	ptr = glob->iov_buf;

	/** format: pool label - pool hdl size - pool hdl */
	strcpy(ptr, dfs->pool_hdl->value);
	ptr += pool_len;
	*((daos_size_t *)ptr) = pool_iov.iov_buf_len;
	ptr += sizeof(daos_size_t);
	pool_iov.iov_buf = ptr;
	pool_iov.iov_len = pool_iov.iov_buf_len;
	rc               = daos_pool_local2global(dfs->poh, &pool_iov);
	if (rc)
		return daos_der2errno(rc);
	ptr += pool_iov.iov_buf_len;

	/** format: cont label - cont hdl size - cont hdl */
	strcpy(ptr, dfs->cont_hdl->value);
	ptr += cont_len;
	*((daos_size_t *)ptr) = cont_iov.iov_buf_len;
	ptr += sizeof(daos_size_t);
	cont_iov.iov_buf = ptr;
	cont_iov.iov_len = cont_iov.iov_buf_len;
	rc               = daos_cont_local2global(dfs->coh, &cont_iov);
	if (rc)
		return daos_der2errno(rc);
	ptr += cont_iov.iov_buf_len;

	*((daos_size_t *)ptr) = dfs_iov.iov_buf_len;
	ptr += sizeof(daos_size_t);
	dfs_iov.iov_buf = ptr;
	dfs_iov.iov_len = dfs_iov.iov_buf_len;
	rc              = dfs_local2global(dfs, &dfs_iov);
	if (rc)
		return rc;

	return 0;
}

int
dfs_global2local_all(int flags, d_iov_t glob, dfs_t **_dfs)
{
	char                *ptr;
	d_iov_t              pool_iov = {NULL, 0, 0};
	d_iov_t              cont_iov = {NULL, 0, 0};
	d_iov_t              dfs_iov  = {NULL, 0, 0};
	daos_size_t          pool_len, cont_len;
	char                 pool[DAOS_PROP_LABEL_MAX_LEN + 1];
	char                 cont[DAOS_PROP_LABEL_MAX_LEN + 1];
	bool                 pool_h_bump = false;
	bool                 cont_h_bump = false;
	struct dfs_mnt_hdls *pool_hdl    = NULL;
	struct dfs_mnt_hdls *cont_hdl    = NULL;
	daos_handle_t        poh         = {0};
	daos_handle_t        coh         = {0};
	dfs_t               *dfs         = NULL;
	int                  rc, rc2;

	if (_dfs == NULL)
		return EINVAL;
	if (!dfs_is_init()) {
		D_ERROR("dfs_init() must be called before dfs_global2local_all() can be used\n");
		return EACCES;
	}
	if (glob.iov_buf == NULL || glob.iov_buf_len < glob.iov_len) {
		D_ERROR("Invalid parameter of glob, iov_buf %p, "
			"iov_buf_len " DF_U64 ", iov_len " DF_U64 ".\n",
			glob.iov_buf, glob.iov_buf_len, glob.iov_len);
		return EINVAL;
	}

	ptr = (char *)glob.iov_buf;

	strncpy(pool, ptr, DAOS_PROP_LABEL_MAX_LEN + 1);
	pool[DAOS_PROP_LABEL_MAX_LEN] = 0;
	pool_len                      = strlen(pool) + 1;
	ptr += pool_len;
	pool_iov.iov_buf_len = *((daos_size_t *)ptr);
	ptr += sizeof(daos_size_t);
	pool_iov.iov_buf = ptr;
	pool_iov.iov_len = pool_iov.iov_buf_len;
	rc               = daos_pool_global2local(pool_iov, &poh);
	if (rc)
		D_GOTO(err, rc = daos_der2errno(rc));
	ptr += pool_iov.iov_buf_len;
	rc = dfs_hdl_insert(pool, DFS_H_POOL, NULL, &poh, &pool_hdl);
	if (rc)
		D_GOTO(err, rc);
	pool_h_bump = true;

	strncpy(cont, ptr, DAOS_PROP_LABEL_MAX_LEN + 1);
	cont[DAOS_PROP_LABEL_MAX_LEN] = 0;
	cont_len                      = strlen(cont) + 1;
	ptr += cont_len;
	cont_iov.iov_buf_len = *((daos_size_t *)ptr);
	ptr += sizeof(daos_size_t);
	cont_iov.iov_buf = ptr;
	cont_iov.iov_len = cont_iov.iov_buf_len;
	rc               = daos_cont_global2local(poh, cont_iov, &coh);
	if (rc)
		D_GOTO(err, rc = daos_der2errno(rc));
	ptr += cont_iov.iov_buf_len;
	rc = dfs_hdl_insert(cont, DFS_H_CONT, pool, &coh, &cont_hdl);
	if (rc)
		D_GOTO(err, rc);
	cont_h_bump = true;

	dfs_iov.iov_buf_len = *((daos_size_t *)ptr);
	ptr += sizeof(daos_size_t);
	dfs_iov.iov_buf = ptr;
	dfs_iov.iov_len = dfs_iov.iov_buf_len;
	rc              = dfs_global2local(poh, coh, flags, dfs_iov, &dfs);
	if (rc)
		D_GOTO(err, rc);

	dfs->pool_hdl = pool_hdl;
	dfs->cont_hdl = cont_hdl;
	dfs->mounted  = DFS_MOUNT_ALL;

	*_dfs = dfs;

	return rc;

err:
	if (dfs) {
		rc2 = dfs_umount(dfs);
		if (rc2)
			D_ERROR("dfs_umount() Failed %d\n", rc2);
	}

	if (cont_h_bump) {
		dfs_hdl_release(cont_hdl);
	} else if (daos_handle_is_valid(coh)) {
		rc2 = daos_cont_close(coh, NULL);
		if (rc2)
			D_ERROR("daos_cont_close() Failed " DF_RC "\n", DP_RC(rc2));
	}

	if (pool_h_bump) {
		dfs_hdl_release(pool_hdl);
	} else if (daos_handle_is_valid(poh)) {
		rc2 = daos_pool_disconnect(poh, NULL);
		if (rc2)
			D_ERROR("daos_pool_disconnect() Failed " DF_RC "\n", DP_RC(rc2));
	}

	return rc;
}

int
dfs_set_prefix(dfs_t *dfs, const char *prefix)
{
	if (dfs == NULL || !dfs->mounted)
		return EINVAL;

	if (prefix == NULL) {
		D_FREE(dfs->prefix);
		return 0;
	}

	if (prefix[0] != '/' || strnlen(prefix, DFS_MAX_PATH) > DFS_MAX_PATH - 1)
		return EINVAL;

	D_STRNDUP(dfs->prefix, prefix, DFS_MAX_PATH - 1);
	if (dfs->prefix == NULL)
		return ENOMEM;

	dfs->prefix_len = strlen(dfs->prefix);
	if (dfs->prefix[dfs->prefix_len - 1] == '/')
		dfs->prefix_len--;

	return 0;
}
