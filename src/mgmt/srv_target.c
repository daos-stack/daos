/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * Target Methods
 */
#define D_LOGFAC	DD_FAC(mgmt)

#include <sys/stat.h>
#include <fcntl.h>
#include <linux/falloc.h>
#include <sys/sysinfo.h>
#include <ftw.h>
#include <dirent.h>

#include <daos_srv/vos.h>
#include <daos_srv/pool.h>
#include <daos_srv/daos_mgmt_srv.h>
#include <daos_mgmt.h>

#include "srv_internal.h"
#include "srv_layout.h"		/* for a couple of constants only */

/** directory for newly created pool, reclaimed on restart */
static char *newborns_path;
/** directory for destroyed pool */
static char *zombies_path;

static int
tgt_vos_preallocate_parallel(uuid_t uuid, daos_size_t scm_size, int tgt_nr, bool *cancel_pending);

/* ds_pooltgts*
 * dpt_creates_ht tracks in-flight pool tgt creates
 * tgt create inserts a record into creates_ht ; and during tgt allocation
 * periodically checks if a tgt destroy is requested.
 * tgt destroy checks if a record exists, modifies it to ask create to stop ;
 * waits for create to remove the record (indicating create is done).
 * In-memory, not persistent.
 */
struct ds_pooltgts {
	ABT_mutex		dpt_mutex;
	ABT_cond		dpt_cv;
	struct d_hash_table	dpt_creates_ht;
};


struct ds_pooltgts_rec {
	uuid_t		dptr_uuid;
	bool		cancel_create;	/* ask create hdlr to stop prealloc */
	d_list_t	dptr_hlink;	/* in hash table */
};

static struct ds_pooltgts	*pooltgts;

static inline struct ds_pooltgts_rec *
pooltgts_obj(d_list_t *rlink)
{
	return container_of(rlink, struct ds_pooltgts_rec, dptr_hlink);
}

static bool
pooltgts_cmp_keys(struct d_hash_table *htable, d_list_t *rlink,
		  const void *key, unsigned int ksize)
{
	struct ds_pooltgts_rec *ptrec = pooltgts_obj(rlink);

	return uuid_compare(key, ptrec->dptr_uuid) == 0;
}

static d_hash_table_ops_t pooltgts_hops = {
	.hop_key_cmp		= pooltgts_cmp_keys,
};

static int
path_gen(const uuid_t pool_uuid, const char *dir, const char *fname, int *idx,
	 char **fpath)
{
	int	 size;
	int	 off;

	/** *fpath = dir + "/" + pool_uuid + "/" + fname + idx */

	/** DAOS_UUID_STR_SIZE includes the trailing '\0' */
	size = strlen(dir) + 1 /* "/" */ + DAOS_UUID_STR_SIZE;
	if (fname != NULL || idx != NULL)
		size += 1 /* "/" */;
	if (fname)
		size += strlen(fname);
	if (idx)
		size += snprintf(NULL, 0, "%d", *idx);

	D_ALLOC(*fpath, size);
	if (*fpath == NULL)
		return -DER_NOMEM;

	off = sprintf(*fpath, "%s", dir);
	off += sprintf(*fpath + off, "/");
	uuid_unparse_lower(pool_uuid, *fpath + off);
	off += DAOS_UUID_STR_SIZE - 1;
	if (fname != NULL || idx != NULL)
		off += sprintf(*fpath + off, "/");
	if (fname)
		off += sprintf(*fpath + off, "%s", fname);
	if (idx)
		sprintf(*fpath + off, "%d", *idx);

	return 0;
}

static inline int
dir_fsync(const char *path)
{
	int	fd;
	int	rc;

	fd = open(path, O_RDONLY|O_DIRECTORY);
	if (fd < 0) {
		rc = errno;
		D_CDEBUG(rc == ENOENT, DB_MGMT, DLOG_ERR, "failed to open %s for sync: %d\n", path,
			 rc);
		return daos_errno2der(rc);
	}

	rc = fsync(fd);
	if (rc < 0) {
		D_ERROR("failed to fync %s: %d\n", path, errno);
		rc = daos_errno2der(errno);
	}

	(void)close(fd);

	return rc;
}

static int
destroy_cb(const char *path, const struct stat *sb, int flag,
	   struct FTW *ftwbuf)
{
	int rc;

	if (ftwbuf->level == 0)
		return 0;

	if (flag == FTW_DP || flag == FTW_D)
		rc = rmdir(path);
	else
		rc = unlink(path);
	if (rc)
		D_ERROR("failed to remove %s\n", path);
	return rc;
}

static int
subtree_destroy(const char *path)
{
	int rc;

	rc = nftw(path, destroy_cb, 32, FTW_DEPTH | FTW_PHYS | FTW_MOUNT);
	if (rc)
		rc = daos_errno2der(errno);

	return rc;
}

struct tgt_destroy_args {
	struct d_uuid		 tda_id;
	struct dss_xstream	*tda_dx;
	char			*tda_path;
	int			 tda_rc;
};

static inline int
tgt_kill_pool(void *args)
{
	struct d_uuid	*id = args;

	return vos_pool_kill(id->uuid, 0);
}

/**
 * Iterate pools that have targets on this node by scanning the storage. \a cb
 * will be called with the UUID of each pool. When \a cb returns an rc,
 *
 *   - if rc == 0, the iteration continues;
 *   - if rc == 1, the iteration stops and returns 0;
 *   - otherwise, the iteration stops and returns rc.
 *
 * \param[in]	cb	callback called for each pool
 * \param[in]	arg	argument passed to each \a cb call
 */
static int
common_pool_iterate(const char *path, int (*cb)(uuid_t uuid, void *arg), void *arg)
{
	DIR    *storage;
	int	rc;
	int	rc_tmp;

	storage = opendir(path);
	if (storage == NULL) {
		D_ERROR("failed to open %s: %d\n", path, errno);
		return daos_errno2der(errno);
	}

	for (;;) {
		struct dirent  *entry;
		uuid_t		uuid;

		rc = 0;
		errno = 0;
		entry = readdir(storage);
		if (entry == NULL) {
			if (errno != 0) {
				D_ERROR("failed to read %s: %d\n",
					path, errno);
				rc = daos_errno2der(errno);
			}
			break;
		}

		/* A pool directory must have a valid UUID as its name. */
		rc = uuid_parse(entry->d_name, uuid);
		if (rc != 0)
			continue;

		rc = cb(uuid, arg);
		if (rc != 0) {
			if (rc == 1)
				rc = 0;
			break;
		}
	}

	rc_tmp = closedir(storage);
	if (rc_tmp != 0) {
		D_ERROR("failed to close %s: %d\n", path, errno);
		rc_tmp = daos_errno2der(errno);
	}

	return rc == 0 ? rc_tmp : rc;
}

int
ds_mgmt_tgt_pool_iterate(int (*cb)(uuid_t uuid, void *arg), void *arg)
{
	return common_pool_iterate(dss_storage_path, cb, arg);
}

static int
newborn_pool_iterate(int (*cb)(uuid_t uuid, void *arg), void *arg)
{
	return common_pool_iterate(newborns_path, cb, arg);
}

static int
zombie_pool_iterate(int (*cb)(uuid_t uuid, void *arg), void *arg)
{
	return common_pool_iterate(zombies_path, cb, arg);
}

struct dead_pool {
	d_list_t	dp_link;
	uuid_t		dp_uuid;
};

/* Remove leftover SPDK resources from pools not fully created/destroyed */
static int
cleanup_leftover_cb(uuid_t uuid, void *arg)
{
	d_list_t		*dead_list = arg;
	struct dead_pool	*dp;
	int			 rc;
	struct d_uuid		 id;

	/* destroy blobIDs */
	D_DEBUG(DB_MGMT, "Clear SPDK blobs for pool "DF_UUID"\n", DP_UUID(uuid));
	uuid_copy(id.uuid, uuid);
	rc = dss_thread_collective(tgt_kill_pool, &id, 0);
	if (rc != 0) {
		D_ERROR("tgt_kill_pool, rc: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	D_ALLOC_PTR(dp);
	if (dp == NULL)
		return -DER_NOMEM;

	uuid_copy(dp->dp_uuid, uuid);
	d_list_add(&dp->dp_link, dead_list);
	return rc;
}

static void
cleanup_dead_list(d_list_t *dead_list, const char *path)
{
	struct dead_pool	*dp, *tmp;
	char			*dead_dir;
	int			 rc;

	d_list_for_each_entry_safe(dp, tmp, dead_list, dp_link) {
		rc = path_gen(dp->dp_uuid, path, NULL, NULL, &dead_dir);

		d_list_del_init(&dp->dp_link);
		D_FREE(dp);
		if (rc) {
			D_ERROR("failed to gen path\n");
			continue;
		}

		D_INFO("Cleanup leftover pool: %s\n", dead_dir);
		(void)subtree_destroy(dead_dir);
		(void)rmdir(dead_dir);
		D_FREE(dead_dir);
	}
}

static void
cleanup_leftover_pools(bool zombie_only)
{
	d_list_t	dead_list;
	int		rc;

	D_INIT_LIST_HEAD(&dead_list);

	rc = zombie_pool_iterate(cleanup_leftover_cb, &dead_list);
	if (rc)
		D_ERROR("failed to delete SPDK blobs for ZOMBIES pools: "
			"%d, will try again\n", rc);
	cleanup_dead_list(&dead_list, zombies_path);

	if (zombie_only)
		return;

	rc = newborn_pool_iterate(cleanup_leftover_cb, &dead_list);
	if (rc)
		D_ERROR("failed to delete SPDK blobs for NEWBORNS pools: "
			"%d, will try again\n", rc);
	cleanup_dead_list(&dead_list, newborns_path);
}

static int
tgt_recreate(uuid_t pool_uuid, daos_size_t scm_size, int tgt_nr, daos_size_t rdb_blob_sz)
{
	char			*pool_newborn_path = NULL;
	char			*pool_path = NULL;
	char			*rdb_path = NULL;
	bool			 dummy_cancel_state = false;
	int			 rc;
	int			 fd;
	struct stat		 statbuf;

	D_ASSERT(bio_nvme_configured(SMD_DEV_TYPE_META));

	/** generate path to the target directory */
	rc = ds_mgmt_tgt_file(pool_uuid, NULL, NULL, &pool_path);
	if (rc) {
		D_ERROR("newborn path_gen failed for "DF_UUID": "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out;
	}

	/** Skip recreation if directory already exists */
	rc = stat(pool_path, &statbuf);
	if ((rc == 0) && (statbuf.st_mode & S_IFDIR))
		goto out;

	/** create the pool directory under NEWBORNS */
	rc = path_gen(pool_uuid, newborns_path, NULL,
		      NULL, &pool_newborn_path);
	if (rc) {
		D_ERROR(DF_UUID": path_gen failed for: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out;
	}
	rc = mkdir(pool_newborn_path, 0700);
	if (rc < 0 && errno != EEXIST) {
		rc = daos_errno2der(errno);
		D_ERROR("failed to created pool directory: "DF_RC"\n",
			DP_RC(rc));
		/* avoid tgt_destroy(), nothing to do */
		D_FREE(pool_newborn_path);
		goto out;
	}

	/** create VOS files */
	rc = tgt_vos_preallocate_parallel(pool_uuid, scm_size, tgt_nr,
					  &dummy_cancel_state);
	if (rc) {
		D_ERROR(DF_UUID": failed to create tgt vos files: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out;
	}

	if (rdb_blob_sz) {
		rc = path_gen(pool_uuid, newborns_path, RDB_FILE"pool",
			      NULL, &rdb_path);
		if (rdb_path == NULL) {
			D_ERROR(DF_UUID": cannot retrieve rdb file info: "DF_RC"\n",
				DP_UUID(pool_uuid), DP_RC(rc));
			rc = -DER_NONEXIST;
			goto out;
		}
		fd = open(rdb_path, O_RDWR|O_CREAT, 0600);
		if (fd < 0) {
			rc = daos_errno2der(errno);
			D_ERROR("failed to create/open the vos file %s:"DF_RC"\n",
				rdb_path, DP_RC(rc));
			goto out;
		}
		rc = fallocate(fd, 0, 0, rdb_blob_sz);
		close(fd);
		if (rc) {
			rc = daos_errno2der(errno);
			D_ERROR("fallocate on rdb file %s failed:"DF_RC"\n",
				rdb_path, DP_RC(rc));
			goto out;
		}
		D_FREE(rdb_path);
	}

	/** move away from NEWBORNS dir */
	rc = rename(pool_newborn_path, pool_path);
	if (rc < 0) {
		rc = daos_errno2der(errno);
		D_ERROR(DF_UUID": failed to rename pool directory: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out;
	}

	/** make sure the rename is persistent */
	(void)dir_fsync(pool_path);

out:
	D_FREE(pool_newborn_path);
	D_FREE(pool_path);

	return rc;
}

static int
recreate_pooltgts()
{
	struct smd_pool_info    *pool_info = NULL;
	struct smd_pool_info    *tmp;
	d_list_t                 pool_list;
	int			 rc = 0;
	int			 pool_list_cnt;
	daos_size_t		 rdb_blob_sz = 0;
	struct d_uuid		 id;

	D_ASSERT(bio_nvme_configured(SMD_DEV_TYPE_META));
	D_INIT_LIST_HEAD(&pool_list);
	rc = smd_pool_list(&pool_list, &pool_list_cnt);
	if (rc != 0) {
		D_ERROR("Failed to get pool info list from SMD\n");
		return rc;
	}

	d_list_for_each_entry_safe(pool_info, tmp, &pool_list, spi_link) {
		/* Cleanup Newborns */
		if ((pool_info->spi_blob_sz[SMD_DEV_TYPE_META] == 0) ||
		    (pool_info->spi_flags[SMD_DEV_TYPE_META] & SMD_POOL_IN_CREATION)) {
			D_INFO("cleaning up newborn pool "DF_UUID"\n",
			       DP_UUID(pool_info->spi_id));
			uuid_copy(id.uuid, pool_info->spi_id);
			rc = dss_thread_collective(tgt_kill_pool, &id, 0);
			if (rc) {
				D_ERROR("failed to cleanup newborn pool "DF_UUID": "DF_RC"\n",
					DP_UUID(pool_info->spi_id), DP_RC(rc));
			}
			continue;
		}

		D_INFO("recreating files for pool "DF_UUID"\n", DP_UUID(pool_info->spi_id));
		rc = smd_rdb_get_blob_sz(pool_info->spi_id, &rdb_blob_sz);
		if (rc && (rc != -DER_NONEXIST)) {
			D_ERROR(DF_UUID": failed to extract the size of rdb file: "DF_RC"\n",
				DP_UUID(pool_info->spi_id), DP_RC(rc));
			goto out;
		}
		rc = tgt_recreate(pool_info->spi_id, pool_info->spi_blob_sz[SMD_DEV_TYPE_META],
				  pool_info->spi_tgt_cnt[SMD_DEV_TYPE_META], rdb_blob_sz);
		if (rc)
			goto out;
	}
out:
	d_list_for_each_entry_safe(pool_info, tmp, &pool_list, spi_link) {
		d_list_del(&pool_info->spi_link);
		/* Frees spi_tgts, spi_blobs, and pool_info */
		smd_pool_free_info(pool_info);
	}
	return rc;
}

int
ds_mgmt_tgt_setup(void)
{
	mode_t	stored_mode;
	int	rc;

	/** create the path string */
	D_ASPRINTF(newborns_path, "%s/NEWBORNS", dss_storage_path);
	if (newborns_path == NULL)
		D_GOTO(err, rc = -DER_NOMEM);
	D_ASPRINTF(zombies_path, "%s/ZOMBIES", dss_storage_path);
	if (zombies_path == NULL)
		D_GOTO(err_newborns, rc = -DER_NOMEM);

	stored_mode = umask(0);
	/** create NEWBORNS directory if it does not exist already */
	rc = mkdir(newborns_path, S_IRWXU);
	if (rc < 0 && errno != EEXIST) {
		D_ERROR("failed to create NEWBORNS dir: %d\n", errno);
		umask(stored_mode);
		D_GOTO(err_zombies, rc = daos_errno2der(errno));
	}

	/** create ZOMBIES directory if it does not exist already */
	rc = mkdir(zombies_path, S_IRWXU);
	if (rc < 0 && errno != EEXIST) {
		D_ERROR("failed to create ZOMBIES dir: %d\n", errno);
		umask(stored_mode);
		D_GOTO(err_zombies, rc = daos_errno2der(errno));
	}
	umask(stored_mode);

	/** remove leftover from previous runs */
	cleanup_leftover_pools(false);

	if (bio_nvme_configured(SMD_DEV_TYPE_META)) {
		rc = recreate_pooltgts();
		if (rc) {
			D_ERROR("failed to create pool tgts: "DF_RC"\n", DP_RC(rc));
			goto err_zombies;
		}
	}

	/* create lock/cv and hash table to track outstanding pool creates */
	D_ALLOC_PTR(pooltgts);
	if (pooltgts == NULL) {
		D_GOTO(err_zombies, rc = -DER_NOMEM);
	}

	rc = ABT_mutex_create(&pooltgts->dpt_mutex);
	if (rc != ABT_SUCCESS) {
		D_ERROR("failed to create pooltgts mutex: %d\n", rc);
		rc = dss_abterr2der(rc);
		goto err_pooltgts;
	}

	rc = ABT_cond_create(&pooltgts->dpt_cv);
	if (rc != ABT_SUCCESS) {
		D_ERROR("failed to create pooltgts cv: %d\n", rc);
		rc = dss_abterr2der(rc);
		goto err_mutex;
	}
	rc = d_hash_table_create_inplace(D_HASH_FT_NOLOCK, 6 /* bits */,
					 NULL /* priv */, &pooltgts_hops,
					 &pooltgts->dpt_creates_ht);
	if (rc) {
		D_ERROR("failed to create hash table (creates) "DF_RC"\n",
			DP_RC(rc));
		goto err_cv;
	}

	rc = subtree_destroy(newborns_path);
	if (rc)
		/** only log error, will try again next time */
		D_ERROR("failed to cleanup NEWBORNS dir: %d, will try again\n",
			rc);
	rc = subtree_destroy(zombies_path);
	if (rc)
		/** only log error, will try again next time */
		D_ERROR("failed to cleanup ZOMBIES dir: %d, will try again\n",
			rc);
	return 0;

err_cv:
	ABT_cond_free(&pooltgts->dpt_cv);
err_mutex:
	ABT_mutex_free(&pooltgts->dpt_mutex);
err_pooltgts:
	D_FREE(pooltgts);
err_zombies:
	D_FREE(zombies_path);
err_newborns:
	D_FREE(newborns_path);
err:
	return rc;
}

void
ds_mgmt_tgt_cleanup(void)
{
	int rc;

	rc = d_hash_table_destroy_inplace(&pooltgts->dpt_creates_ht, true);
	if (rc) {
		D_ERROR("failed to destroy table: dpt_creates_ht: "DF_RC"\n",
			DP_RC(rc));
	}
	ABT_cond_free(&pooltgts->dpt_cv);
	ABT_mutex_free(&pooltgts->dpt_mutex);
	D_FREE(pooltgts);
	D_FREE(zombies_path);
	D_FREE(newborns_path);
}

/**
 * Generate path to a target file for pool \a pool_uuid with a filename set to
 * \a fname and suffixed by \a idx. \a idx can be NULL.
 */
int
ds_mgmt_tgt_file(const uuid_t pool_uuid, const char *fname, int *idx,
		 char **fpath)
{
	return path_gen(pool_uuid, dss_storage_path, fname, idx, fpath);
}

struct vos_pool_arg {
	uuid_t		vpa_uuid;
	daos_size_t	vpa_scm_size;
	daos_size_t	vpa_nvme_size;
};

static int
tgt_vos_create_one(void *varg)
{
	struct dss_module_info	*info = dss_get_module_info();
	struct vos_pool_arg	*vpa = varg;
	char			*path = NULL;
	int			 rc;

	rc = path_gen(vpa->vpa_uuid, newborns_path, VOS_FILE, &info->dmi_tgt_id,
		      &path);
	if (rc)
		return rc;

	rc = vos_pool_create(path, (unsigned char *)vpa->vpa_uuid,
			     vpa->vpa_scm_size, vpa->vpa_nvme_size, 0, NULL);
	if (rc)
		D_ERROR(DF_UUID": failed to init vos pool %s: %d\n",
			DP_UUID(vpa->vpa_uuid), path, rc);

	D_FREE(path);
	return rc;
}

static int
tgt_vos_preallocate(uuid_t uuid, daos_size_t scm_size, int tgt_id)
{
	char				*path = NULL;
	int				 fd = -1, rc;

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	rc = path_gen(uuid, newborns_path, VOS_FILE, &tgt_id, &path);
	if (rc)
		goto out;

	D_DEBUG(DB_MGMT, DF_UUID": creating vos file %s\n", DP_UUID(uuid), path);

	fd = open(path, O_CREAT|O_RDWR, 0600);
	if (fd < 0) {
		rc = daos_errno2der(errno);
		D_ERROR(DF_UUID": failed to create vos file %s: "
			DF_RC"\n", DP_UUID(uuid), path, DP_RC(rc));
		goto out;
	}

	/** Align to 4K or locking the region based on the size will fail */
	scm_size = D_ALIGNUP(scm_size, 1ULL << 12);
	/**
	 * Pre-allocate blocks for vos files in order to provide
	 * consistent performance and avoid entering into the backend
	 * filesystem allocator through page faults.
	 * Use fallocate(2) instead of posix_fallocate(3) since the
	 * latter is bogus with tmpfs.
	 */
	rc = fallocate(fd, 0, 0, scm_size);
	if (rc) {
		rc = daos_errno2der(errno);
		D_ERROR(DF_UUID": failed to allocate vos file %s with "
			"size: "DF_U64": "DF_RC"\n",
			DP_UUID(uuid), path, scm_size, DP_RC(rc));
		goto out;
	}

	rc = fsync(fd);
	(void)close(fd);
	fd = -1;
	if (rc) {
		rc = daos_errno2der(errno);
		D_ERROR(DF_UUID": failed to sync vos pool %s: "
			DF_RC"\n", DP_UUID(uuid), path, DP_RC(rc));
		goto out;
	}
out:
	if (fd != -1)
		close(fd);
	D_FREE(path);
	return rc;
}

struct tgt_vos_prealloc_args {
	uuid_t		tvpa_uuid;
	daos_size_t	tvpa_scm_size;
	int		tvpa_tgt_id;
};

struct tgt_vos_thrdlist {
	d_list_t			tvt_link;
	pthread_t			tvt_tid;
	struct tgt_vos_prealloc_args	tvt_args;
};

static void *
tgt_vos_preallocate_thrd_func(void *arg)
{
	struct tgt_vos_prealloc_args	*tvpa = (struct tgt_vos_prealloc_args *)arg;

	dss_bind_to_xstream_cpuset(tvpa->tvpa_tgt_id);
	return (void *)(uintptr_t)tgt_vos_preallocate(tvpa->tvpa_uuid, tvpa->tvpa_scm_size,
						      tvpa->tvpa_tgt_id);
}

static void
tgt_vos_preallocate_thrds_cleanup(d_list_t *head)
{
	struct tgt_vos_thrdlist *entry;
	int			 rc;

	d_list_for_each_entry(entry, head, tvt_link) {
		rc = pthread_cancel(entry->tvt_tid);
		if (rc) {
			rc = daos_errno2der(rc);
			D_ERROR("pthread_cancel failed: "DF_RC"\n", DP_RC(rc));
		}
	}

	d_list_for_each_entry(entry, head, tvt_link) {
		rc = pthread_join(entry->tvt_tid, NULL);
		if (rc) {
			rc = daos_errno2der(rc);
			D_ERROR("pthread_join failed: "DF_RC"\n", DP_RC(rc));
		}
	}
}

static int
tgt_vos_preallocate_sequential(uuid_t uuid, daos_size_t scm_size, int tgt_nr)
{
	int i, rc = 0;

	for (i = 0; i < tgt_nr; i++) {
		rc = tgt_vos_preallocate(uuid, scm_size, i);
		if (rc)
			break;
	}
	return rc;
}

static int
tgt_vos_preallocate_parallel(uuid_t uuid, daos_size_t scm_size, int tgt_nr, bool *cancel_pending)
{
	int				 i;
	int				 rc;
	int				 saved_rc = 0;
	int				 res;
	int				 old_cancelstate;
	struct tgt_vos_thrdlist		*entry, *tmp;
	struct tgt_vos_thrdlist		*thrds_list;

	D_LIST_HEAD(thrds_list_head);

	D_INIT_LIST_HEAD(&thrds_list_head);
	D_ALLOC_ARRAY(thrds_list, tgt_nr);

	/* Disable cancellation to manage other threads created within. */
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_cancelstate);

	for (i = 0; i < tgt_nr; i++) {
		entry = &thrds_list[i];
		uuid_copy(entry->tvt_args.tvpa_uuid, uuid);
		entry->tvt_args.tvpa_scm_size = scm_size;
		entry->tvt_args.tvpa_tgt_id = i;
		rc = pthread_create(&entry->tvt_tid, NULL, tgt_vos_preallocate_thrd_func,
				    &entry->tvt_args);
		if (rc) {
			saved_rc = daos_errno2der(rc);
			D_ERROR(DF_UUID": failed to create thread for target file "
				"creation: "DF_RC"\n", DP_UUID(uuid),
				DP_RC(saved_rc));
			goto out;
		}
		d_list_add_tail(&entry->tvt_link, &thrds_list_head);
		if (*cancel_pending == true) {
			saved_rc = -DER_CANCELED;
			goto out;
		}
	}

	while (!d_list_empty(&thrds_list_head)) {
		sched_yield();
		if (*cancel_pending == true) {
			saved_rc = -DER_CANCELED;
			goto out;
		}
		d_list_for_each_entry_safe(entry, tmp, &thrds_list_head, tvt_link) {
			rc = pthread_tryjoin_np(entry->tvt_tid, (void **)&res);
			if (rc == EBUSY)
				continue;
			else if (rc == 0)
				rc = res;
			else {
				rc = daos_errno2der(rc);
				D_ERROR("pthread_join failed: "DF_RC"\n", DP_RC(rc));
			}
			if (!saved_rc && rc)
				saved_rc = rc;
			d_list_del(&entry->tvt_link);
		}
	}
out:
	tgt_vos_preallocate_thrds_cleanup(&thrds_list_head);
	D_FREE(thrds_list);
	pthread_setcancelstate(old_cancelstate, NULL);
	return saved_rc;
}

int
ds_mgmt_tgt_create_post_reply(crt_rpc_t *rpc, void *priv)
{
	struct mgmt_tgt_create_out	*tc_out;

	tc_out = crt_reply_get(rpc);
	D_FREE(tc_out->tc_ranks.ca_arrays);

	return 0;
}

int
ds_mgmt_tgt_create_aggregator(crt_rpc_t *source, crt_rpc_t *result,
			      void *priv)
{
	struct mgmt_tgt_create_out	*tc_out;
	struct mgmt_tgt_create_out	*ret_out;
	d_rank_t			*tc_ranks;
	unsigned int			tc_ranks_nr;
	d_rank_t			*ret_ranks;
	unsigned int			ret_ranks_nr;
	d_rank_t			*new_ranks;
	unsigned int			new_ranks_nr;
	int				i;

	tc_out = crt_reply_get(source);
	tc_ranks = tc_out->tc_ranks.ca_arrays;
	tc_ranks_nr = tc_out->tc_ranks.ca_count;

	ret_out = crt_reply_get(result);
	ret_ranks = ret_out->tc_ranks.ca_arrays;
	ret_ranks_nr = ret_out->tc_ranks.ca_count;

	if (tc_out->tc_rc != 0)
		ret_out->tc_rc = tc_out->tc_rc;
	if (tc_ranks_nr == 0)
		return 0;

	new_ranks_nr = ret_ranks_nr + tc_ranks_nr;

	D_ALLOC_ARRAY(new_ranks, new_ranks_nr);
	if (new_ranks == NULL)
		return -DER_NOMEM;

	for (i = 0; i < new_ranks_nr; i++) {
		if (i < ret_ranks_nr)
			new_ranks[i] = ret_ranks[i];
		else
			new_ranks[i] = tc_ranks[i - ret_ranks_nr];
	}

	D_FREE(ret_ranks);

	ret_out->tc_ranks.ca_arrays = new_ranks;
	ret_out->tc_ranks.ca_count = new_ranks_nr;
	return 0;
}

struct tgt_create_args {
	char			*tca_newborn;
	char			*tca_path;
	struct ds_pooltgts_rec	*tca_ptrec;
	struct dss_xstream	*tca_dx;
	daos_size_t		 tca_scm_size;
	daos_size_t		 tca_nvme_size;
	int			 tca_rc;
};

static void *
tgt_create_preallocate(void *arg)
{
	struct tgt_create_args	*tca = arg;
	int			 rc;

	(void)dss_xstream_set_affinity(tca->tca_dx);

	/** generate path to the target directory */
	rc = ds_mgmt_tgt_file(tca->tca_ptrec->dptr_uuid, NULL, NULL,
			      &tca->tca_path);
	if (rc)
		goto out;

	/** check whether the target already exists */
	rc = access(tca->tca_path, F_OK);
	if (rc >= 0) {
		/** target already exists, let's reuse it for idempotence */

		/**
		 * flush again in case the previous one in tgt_create()
		 * failed
		 */
		rc = dir_fsync(tca->tca_path);
		D_DEBUG(DB_MGMT, "reuse existing tca_path: %s, dir_fsync rc: "DF_RC"\n",
			tca->tca_path, DP_RC(rc));
	} else if (errno == ENOENT) { /** target doesn't exist, create one */
		/** create the pool directory under NEWBORNS */
		rc = path_gen(tca->tca_ptrec->dptr_uuid, newborns_path, NULL,
			      NULL, &tca->tca_newborn);
		if (rc)
			goto out;

		rc = mkdir(tca->tca_newborn, 0700);
		if (rc < 0 && errno != EEXIST) {
			rc = daos_errno2der(errno);
			D_ERROR("failed to created pool directory: "DF_RC"\n",
				DP_RC(rc));
			/* avoid tgt_destroy(), nothing to do */
			D_FREE(tca->tca_newborn);
			goto out;
		}

		/** create VOS files */

		/**
		 * Create one VOS file per execution stream
		 * 16MB minimum per pmemobj file (SCM partition)
		 */
		D_ASSERT(dss_tgt_nr > 0);
		if (!bio_nvme_configured(SMD_DEV_TYPE_META)) {
			rc = tgt_vos_preallocate_sequential(tca->tca_ptrec->dptr_uuid,
							    max(tca->tca_scm_size / dss_tgt_nr,
								1 << 24), dss_tgt_nr);
		} else {
			rc = tgt_vos_preallocate_parallel(tca->tca_ptrec->dptr_uuid,
							  max(tca->tca_scm_size / dss_tgt_nr,
							      1 << 24), dss_tgt_nr,
							  &tca->tca_ptrec->cancel_create);
		}
		if (rc)
			goto out;
	} else {
		rc = daos_errno2der(errno);
	}
out:
	tca->tca_rc = rc;
	return NULL;
}

static int tgt_destroy(uuid_t pool_uuid, char *path);

/**
 * RPC handler for target creation
 */
void
ds_mgmt_hdlr_tgt_create(crt_rpc_t *tc_req)
{
	struct mgmt_tgt_create_in	*tc_in;
	struct mgmt_tgt_create_out	*tc_out;
	struct tgt_create_args		 tca = {0};
	d_rank_t			*rank = NULL;
	pthread_t			 thread;
	bool				 canceled_thread = false;
	int				 rc = 0;

	/** incoming request buffer */
	tc_in = crt_req_get(tc_req);
	D_DEBUG(DB_MGMT, DF_UUID": processing rpc %p\n",
		DP_UUID(tc_in->tc_pool_uuid), tc_req);

	/** reply buffer */
	tc_out = crt_reply_get(tc_req);
	D_ASSERT(tc_in != NULL && tc_out != NULL);

	/* cleanup lingering pools to free up space */
	cleanup_leftover_pools(true);

	/** insert record in dpt_creates_ht hash table (creates in progress) */
	D_ALLOC_PTR(tca.tca_ptrec);
	if (tca.tca_ptrec == NULL)
		D_GOTO(out_reply, rc = -DER_NOMEM);
	uuid_copy(tca.tca_ptrec->dptr_uuid, tc_in->tc_pool_uuid);
	tca.tca_ptrec->cancel_create = false;
	ABT_mutex_lock(pooltgts->dpt_mutex);
	rc = d_hash_rec_insert(&pooltgts->dpt_creates_ht,
			       tca.tca_ptrec->dptr_uuid, sizeof(uuid_t),
			       &tca.tca_ptrec->dptr_hlink, true);
	ABT_mutex_unlock(pooltgts->dpt_mutex);
	if (rc == -DER_EXIST) {
		D_ERROR(DF_UUID": already creating or cleaning up\n",
			DP_UUID(tc_in->tc_pool_uuid));
		D_GOTO(out_rec, rc = -DER_AGAIN);
	} else if (rc) {
		D_ERROR(DF_UUID": failed insert dpt_creates_ht: "DF_RC"\n",
			DP_UUID(tc_in->tc_pool_uuid), DP_RC(rc));
		goto out_rec;
	}
	D_DEBUG(DB_MGMT, DF_UUID": record inserted to dpt_creates_ht\n",
		DP_UUID(tca.tca_ptrec->dptr_uuid));

	tca.tca_scm_size  = tc_in->tc_scm_size;
	tca.tca_nvme_size = tc_in->tc_nvme_size;
	tca.tca_dx = dss_current_xstream();
	rc = pthread_create(&thread, NULL, tgt_create_preallocate, &tca);
	if (rc) {
		rc = daos_errno2der(rc);
		D_ERROR(DF_UUID": failed to create thread for target file "
			"creation: "DF_RC"\n", DP_UUID(tc_in->tc_pool_uuid),
			DP_RC(rc));
		goto out;
	}

	for (;;) {
		void *res;

		/* Cancel thread if tgt destroy occurs before done. */
		if (!canceled_thread && tca.tca_ptrec->cancel_create) {
			D_DEBUG(DB_MGMT, DF_UUID": received cancel request\n",
				DP_UUID(tc_in->tc_pool_uuid));
			rc = pthread_cancel(thread);
			if (rc) {
				rc = daos_errno2der(rc);
				D_ERROR("pthread_cancel failed: "DF_RC"\n",
					DP_RC(rc));
				break;
			}
			canceled_thread = true;
		}

		/* Try to join with thread - either canceled or normal exit. */
		rc = pthread_tryjoin_np(thread, &res);
		if (rc == 0) {
			rc = canceled_thread ? -DER_CANCELED : tca.tca_rc;
			break;
		}
		ABT_thread_yield();
	}
	/* check the result of tgt_create_preallocate() */
	if (rc == -DER_CANCELED) {
		D_DEBUG(DB_MGMT, DF_UUID": tgt preallocate thread canceled\n",
			DP_UUID(tc_in->tc_pool_uuid));
		goto out;
	} else if (rc) {
		D_ERROR(DF_UUID": tgt preallocate thread failed, "DF_RC"\n",
			DP_UUID(tc_in->tc_pool_uuid), DP_RC(rc));
		goto out;
	} else {
		D_INFO(DF_UUID": tgt preallocate thread succeeded\n", DP_UUID(tc_in->tc_pool_uuid));
	}

	if (tca.tca_newborn != NULL) {
		struct vos_pool_arg vpa = {0};

		D_ASSERT(dss_tgt_nr > 0);
		uuid_copy(vpa.vpa_uuid, tc_in->tc_pool_uuid);
		/* A zero size accommodates the existing file */
		vpa.vpa_scm_size = 0;
		vpa.vpa_nvme_size = tc_in->tc_nvme_size / dss_tgt_nr;
		rc = dss_thread_collective(tgt_vos_create_one, &vpa, 0);
		if (rc) {
			D_ERROR(DF_UUID": thread collective tgt_vos_create_one failed, "DF_RC"\n",
				DP_UUID(tc_in->tc_pool_uuid), DP_RC(rc));
			goto out;
		}

		/** ready for prime time, move away from NEWBORNS dir */
		rc = rename(tca.tca_newborn, tca.tca_path);
		if (rc < 0) {
			rc = daos_errno2der(errno);
			D_ERROR("failed to rename pool directory: "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}

		/** make sure the rename is persistent */
		(void)dir_fsync(tca.tca_path);
		/** Mark the pool as ready in smd */
		smd_pool_mark_ready(tc_in->tc_pool_uuid);
	}

	D_ALLOC_PTR(rank);
	if (rank == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = crt_group_rank(NULL, rank);
	if (rc)
		D_GOTO(out, rc);
	tc_out->tc_ranks.ca_arrays = rank;
	tc_out->tc_ranks.ca_count  = 1;

	rc = ds_pool_start(tc_in->tc_pool_uuid);
	if (rc) {
		D_ERROR(DF_UUID": failed to start pool: "DF_RC"\n",
			DP_UUID(tc_in->tc_pool_uuid), DP_RC(rc));
		D_GOTO(out, rc);
	} else {
		D_INFO(DF_UUID": started pool\n", DP_UUID(tc_in->tc_pool_uuid));
	}
out:
	if (rc && tca.tca_newborn != NULL) {
		/*
		 * Ensure partially created resources (e.g., SPDK blobs)
		 * not leaked
		 */
		(void)tgt_destroy(tca.tca_ptrec->dptr_uuid,
				  tca.tca_newborn);
		D_DEBUG(DB_MGMT, DF_UUID": cleaned up failed create targets\n",
			DP_UUID(tc_in->tc_pool_uuid));
	}
	D_FREE(tca.tca_newborn);
	D_FREE(tca.tca_path);
	ABT_mutex_lock(pooltgts->dpt_mutex);
	d_hash_rec_delete_at(&pooltgts->dpt_creates_ht,
			     &tca.tca_ptrec->dptr_hlink);
	ABT_cond_signal(pooltgts->dpt_cv);
	ABT_mutex_unlock(pooltgts->dpt_mutex);
	D_DEBUG(DB_MGMT, DF_UUID" record removed from dpt_creates_ht\n",
		DP_UUID(tca.tca_ptrec->dptr_uuid));
out_rec:
	D_FREE(tca.tca_ptrec);
out_reply:
	tc_out->tc_rc = rc;
	rc = crt_reply_send(tc_req);
	if (rc)
		D_FREE(rank);
}

static void *
tgt_destroy_cleanup(void *arg)
{
	struct tgt_destroy_args	*tda = arg;
	char			*zombie;
	int			 rc;

	(void)dss_xstream_set_affinity(tda->tda_dx);

	/** move target directory to ZOMBIES */
	rc = path_gen(tda->tda_id.uuid, zombies_path, NULL, NULL, &zombie);
	if (rc)
		goto out;

	rc = rename(tda->tda_path, zombie);
	if (rc < 0) {
		rc = daos_errno2der(errno);
		D_ERROR("Failed to rename %s to %s: "DF_RC"\n",
			tda->tda_path, zombie, DP_RC(rc));
		goto out;
	}

	/** make sure the rename is persistent */
	(void)dir_fsync(zombie);

	/**
	 * once successfully moved to the ZOMBIES directory, the target will
	 * take care of retrying on failure and thus always report success to
	 * the caller.
	 */
	if (tda->tda_rc == 0) {
		(void)subtree_destroy(zombie);
		(void)rmdir(zombie);
	} else {
		D_INFO("Defer cleanup for lingering pool:"DF_UUID"\n",
		       DP_UUID(tda->tda_id.uuid));
	}
out:
	D_FREE(zombie);
	tda->tda_rc = rc;
	return NULL;
}

static int
tgt_destroy(uuid_t pool_uuid, char *path)
{
	struct tgt_destroy_args	 tda = {0};
	pthread_t		 thread;
	int			 rc;

	/* destroy blobIDs first */
	uuid_copy(tda.tda_id.uuid, pool_uuid);
	rc = dss_thread_collective(tgt_kill_pool, &tda.tda_id, 0);
	if (rc && rc != -DER_BUSY)
		goto out;

	tda.tda_path = path;
	tda.tda_rc   = rc;
	tda.tda_dx   = dss_current_xstream();
	rc = pthread_create(&thread, NULL, tgt_destroy_cleanup, &tda);
	if (rc) {
		rc = daos_errno2der(errno);
		D_ERROR(DF_UUID": failed to create thread for target file "
			"cleanup: "DF_RC"\n", DP_UUID(pool_uuid),
			DP_RC(rc));
		goto out;
	}

	for (;;) {
		void *res;

		/* Try to join with thread - either canceled or normal exit. */
		rc = pthread_tryjoin_np(thread, &res);
		if (rc == 0) {
			rc = (res == PTHREAD_CANCELED) ? -DER_CANCELED : tda.tda_rc;
			break;
		}
		ABT_thread_yield();
	}
out:
	if (rc)
		D_ERROR(DF_UUID": tgt_destroy_cleanup() thread failed, "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
	else
		D_INFO(DF_UUID": tgt_destroy_cleanup() thread finished\n", DP_UUID(pool_uuid));
	return rc;
}

/**
 * RPC handler for target destroy
 */
void
ds_mgmt_hdlr_tgt_destroy(crt_rpc_t *td_req)
{
	struct mgmt_tgt_destroy_in	*td_in;
	struct mgmt_tgt_destroy_out	*td_out;
	char				*path;
	int				 rc;

	/** incoming request buffer */
	td_in = crt_req_get(td_req);
	D_DEBUG(DB_MGMT, DF_UUID": processing rpc %p\n",
		DP_UUID(td_in->td_pool_uuid), td_req);

	/** reply buffer */
	td_out = crt_reply_get(td_req);
	D_ASSERT(td_in != NULL && td_out != NULL);

	/* If create in-flight, request it be canceled ; then wait */
	ABT_mutex_lock(pooltgts->dpt_mutex);
	do {
		d_list_t		*rec = NULL;
		struct ds_pooltgts_rec	*ptrec = NULL;
		uint32_t		 nreqs = 0;

		rec = d_hash_rec_find(&pooltgts->dpt_creates_ht,
				      td_in->td_pool_uuid, sizeof(uuid_t));
		if (!rec)
			break;

		ptrec = pooltgts_obj(rec);
		nreqs++;
		D_DEBUG(DB_MGMT, DF_UUID": busy creating tgts, ask to cancel "
			"(request %u)\n", DP_UUID(td_in->td_pool_uuid), nreqs);
		ptrec->cancel_create = true;
		ABT_cond_wait(pooltgts->dpt_cv, pooltgts->dpt_mutex);
	} while (1);
	ABT_mutex_unlock(pooltgts->dpt_mutex);
	D_DEBUG(DB_MGMT, DF_UUID": ready to destroy targets\n",
		DP_UUID(td_in->td_pool_uuid));

	/*
	 * If there is a local PS replica, its RDB file will be deleted later
	 * together with the other pool files by the tgt_destroy call below; if
	 * there is no local PS replica, rc will be zero.
	 */
	rc = ds_pool_svc_stop(td_in->td_pool_uuid);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to stop pool service replica (if any): "DF_RC"\n",
			DP_UUID(td_in->td_pool_uuid), DP_RC(rc));
		goto out;
	}

	ds_pool_stop(td_in->td_pool_uuid);

	/** generate path to the target directory */
	rc = ds_mgmt_tgt_file(td_in->td_pool_uuid, NULL, NULL, &path);
	if (rc)
		D_GOTO(out, rc);

	/** check whether the target exists */
	rc = access(path, F_OK);
	if (rc >= 0) {
		/** target is still there, destroy it */
		rc = tgt_destroy(td_req->cr_input, path);
	} else if (errno == ENOENT) {
		char	*zombie;

		/**
		 * target is gone already, report success for idempotence
		 * that said, the previous flush in tgt_destroy() might have
		 * failed, so flush again.
		 */
		rc = path_gen(td_in->td_pool_uuid, zombies_path, NULL, NULL,
			      &zombie);
		if (rc)
			D_GOTO(out, rc);
		rc = dir_fsync(zombie);
		if (rc == -DER_NONEXIST)
			rc = 0;
		D_FREE(zombie);
	} else {
		rc = daos_errno2der(errno);
	}

	D_FREE(path);
out:
	td_out->td_rc = rc;
	crt_reply_send(td_req);
}

/**
 * Set parameter on a single target.
 */
void
ds_mgmt_tgt_params_set_hdlr(crt_rpc_t *rpc)
{
	struct mgmt_tgt_params_set_in	*in;
	struct mgmt_tgt_params_set_out	*out;
	int rc;

	in = crt_req_get(rpc);
	D_ASSERT(in != NULL);

	rc = dss_parameters_set(in->tps_key_id, in->tps_value);
	if (rc == 0 && in->tps_key_id == DMG_KEY_FAIL_LOC) {
		D_DEBUG(DB_MGMT, "Set param DMG_KEY_FAIL_VALUE=%"PRIu64"\n",
			in->tps_value_extra);
		rc = dss_parameters_set(DMG_KEY_FAIL_VALUE,
					in->tps_value_extra);
	}
	if (rc)
		D_ERROR("Set parameter failed key_id %d: rc %d\n",
			 in->tps_key_id, rc);

	out = crt_reply_get(rpc);
	out->srv_rc = rc;
	crt_reply_send(rpc);
}

static int
tgt_profile_task(void *arg)
{
	struct mgmt_profile_in *in = arg;
	int rc = 0;

	if (in->p_op == MGMT_PROFILE_START)
		rc = srv_profile_start(in->p_path, in->p_avg);
	else
		rc = srv_profile_stop();

	D_DEBUG(DB_MGMT, "profile task: rc "DF_RC"\n", DP_RC(rc));
	return rc;
}

/**
 * start/stop profile on a single target.
 */
void
ds_mgmt_tgt_profile_hdlr(crt_rpc_t *rpc)
{
	struct mgmt_profile_in	*in;
	struct mgmt_profile_out	*out;
	int rc;

	in = crt_req_get(rpc);
	D_ASSERT(in != NULL);

	rc = dss_task_collective(tgt_profile_task, in, 0);

	out = crt_reply_get(rpc);
	out->p_rc = rc;
	crt_reply_send(rpc);
}

/**
 * Do Mark on a single target.
 */
void
ds_mgmt_tgt_mark_hdlr(crt_rpc_t *rpc)
{
	struct mgmt_mark_in	*in;
	struct mgmt_mark_out	*out;

	in = crt_req_get(rpc);
	D_ASSERT(in != NULL);

	D_DEBUG(DB_TRACE, "Mark trace %s.\n", in->m_mark);

	out = crt_reply_get(rpc);
	out->m_rc = 0;
	crt_reply_send(rpc);
}

int
ds_mgmt_tgt_map_update_pre_forward(crt_rpc_t *rpc, void *arg)
{
	struct mgmt_tgt_map_update_in  *in = crt_req_get(rpc);

	return ds_mgmt_group_update(in->tm_servers.ca_arrays, in->tm_servers.ca_count,
				    in->tm_map_version);
}

void
ds_mgmt_hdlr_tgt_map_update(crt_rpc_t *rpc)
{
	struct mgmt_tgt_map_update_in  *in = crt_req_get(rpc);
	struct mgmt_tgt_map_update_out *out = crt_reply_get(rpc);
	uint32_t			version;
	int				rc;

	/*
	 * If ds_mgmt_tgt_map_update_pre_forward succeeded, in->tm_map_version
	 * should be <= the system group version.
	 */
	rc = crt_group_version(NULL /* grp */, &version);
	D_ASSERTF(rc == 0, "%d\n", rc);
	if (in->tm_map_version > version)
		out->tm_rc = 1;

	crt_reply_send(rpc);
}

int
ds_mgmt_tgt_map_update_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				  void *priv)
{
	struct mgmt_tgt_map_update_out *out_source = crt_reply_get(source);
	struct mgmt_tgt_map_update_out *out_result = crt_reply_get(result);

	out_result->tm_rc += out_source->tm_rc;
	return 0;
}
