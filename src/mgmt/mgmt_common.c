/*
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 * (C) Copyright 2025 Vdura Inc.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * Target Methods
 */
#define D_LOGFAC DD_FAC(mgmt)

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <daos_errno.h>
#include <daos_types.h>
#include <daos/debug.h>
#include <daos_srv/bio.h>
#include <daos_srv/mgmt_tgt_common.h>
#include <daos_srv/smd.h>

int
ds_mgmt_file(const char *dir, const uuid_t pool_uuid, const char *fname, int *idx, char **fpath)
{
	int size;
	int off;

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

	return DER_SUCCESS;
}

int
ds_mgmt_dir_fsync(const char *dir)
{
	int fd;
	int rc;

	fd = open(dir, O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		rc = errno;
		D_CDEBUG(rc == ENOENT, DB_MGMT, DLOG_ERR, "failed to open %s for sync: %d\n", dir,
			 rc);
		return daos_errno2der(rc);
	}

	rc = fsync(fd);
	if (rc < 0) {
		D_ERROR("failed to fync %s: %d\n", dir, errno);
		rc = daos_errno2der(errno);
	}

	(void)close(fd);

	return rc;
}

int
ds_mgmt_tgt_recreate(uuid_t pool_uuid, daos_size_t scm_size, int tgt_nr, daos_size_t rdb_blob_sz,
		     const char *storage_path, bind_cpu_fn_t bind_cpu_fn)
{
	char       *newborns_path      = NULL;
	char       *pool_newborns_path = NULL;
	char       *pool_path          = NULL;
	char       *rdb_path           = NULL;
	bool        dummy_cancel_state = false;
	int         rc;
	int         fd;
	struct stat statbuf;

	D_ASSERT(bio_nvme_configured(SMD_DEV_TYPE_META));

	/** generate path to the target directory */
	rc = ds_mgmt_file(storage_path, pool_uuid, NULL, NULL, &pool_path);
	if (rc) {
		D_ERROR("pool's path generation failed for " DF_UUID ": " DF_RC "\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		return rc;
	}

	/** Skip recreation if directory already exists */
	rc = stat(pool_path, &statbuf);
	if ((rc == 0) && (statbuf.st_mode & S_IFDIR))
		goto out;

	/** create the pool directory under NEWBORNS */
	D_ASPRINTF(newborns_path, "%s/" DIR_NEWBORNS "", storage_path);
	if (newborns_path == NULL) {
		rc = -DER_NOMEM;
		D_ERROR("newborns_path alloc failed for " DF_UUID ": " DF_RC "\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out;
	}

	rc = ds_mgmt_file(newborns_path, pool_uuid, NULL, NULL, &pool_newborns_path);
	if (rc) {
		D_ERROR(DF_UUID ": path generation failed for: " DF_RC "\n", DP_UUID(pool_uuid),
			DP_RC(rc));
		goto out;
	}
	rc = mkdir(pool_newborns_path, 0700);
	if (rc < 0 && errno != EEXIST) {
		rc = daos_errno2der(errno);
		D_ERROR("failed to created pool directory: " DF_RC "\n", DP_RC(rc));
		/* avoid tgt_destroy(), nothing to do */
		goto out;
	}

	/** create VOS files */
	rc = ds_mgmt_tgt_preallocate_parallel(pool_uuid, scm_size, tgt_nr, &dummy_cancel_state,
					      newborns_path, bind_cpu_fn);
	if (rc) {
		D_ERROR(DF_UUID ": failed to create tgt vos files: " DF_RC "\n", DP_UUID(pool_uuid),
			DP_RC(rc));
		goto out;
	}

	if (rdb_blob_sz) {
		rc = ds_mgmt_file(newborns_path, pool_uuid, RDB_FILE "pool", NULL, &rdb_path);
		if (rdb_path == NULL) {
			D_ERROR(DF_UUID ": cannot retrieve rdb file info: " DF_RC "\n",
				DP_UUID(pool_uuid), DP_RC(rc));
			rc = -DER_NONEXIST;
			goto out;
		}
		fd = open(rdb_path, O_RDWR | O_CREAT, 0600);
		if (fd < 0) {
			rc = daos_errno2der(errno);
			D_ERROR("failed to create/open the vos file %s:" DF_RC "\n", rdb_path,
				DP_RC(rc));
			goto out;
		}
		rc = fallocate(fd, 0, 0, rdb_blob_sz);
		close(fd);
		if (rc) {
			rc = daos_errno2der(errno);
			D_ERROR("fallocate on rdb file %s failed:" DF_RC "\n", rdb_path, DP_RC(rc));
			goto out;
		}
		D_FREE(rdb_path);
	}

	/** move away from NEWBORNS dir */
	rc = rename(pool_newborns_path, pool_path);
	if (rc < 0) {
		rc = daos_errno2der(errno);
		D_ERROR(DF_UUID ": failed to rename pool directory: " DF_RC "\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out;
	}

	/** make sure the rename is persistent */
	(void)ds_mgmt_dir_fsync(pool_path);

out:
	D_FREE(newborns_path);
	D_FREE(pool_newborns_path);
	D_FREE(pool_path);

	return rc;
}

int
ds_mgmt_tgt_preallocate(uuid_t uuid, daos_size_t scm_size, int tgt_id, const char *newborns_path)
{
	char *path = NULL;
	int   fd   = -1, rc;

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	rc = ds_mgmt_file(newborns_path, uuid, VOS_FILE, &tgt_id, &path);
	if (rc)
		goto out;

	D_DEBUG(DB_MGMT, DF_UUID ": creating vos file %s (%ld bytes)\n", DP_UUID(uuid), path,
		scm_size);

	fd = open(path, O_CREAT | O_RDWR, 0600);
	if (fd < 0) {
		rc = daos_errno2der(errno);
		D_ERROR(DF_UUID ": failed to create vos file %s: " DF_RC "\n", DP_UUID(uuid), path,
			DP_RC(rc));
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
		D_ERROR(DF_UUID ": failed to allocate vos file %s with "
				"size: " DF_U64 ": " DF_RC "\n",
			DP_UUID(uuid), path, scm_size, DP_RC(rc));
		goto out;
	}

	rc = fsync(fd);
	(void)close(fd);
	fd = -1;
	if (rc) {
		rc = daos_errno2der(errno);
		D_ERROR(DF_UUID ": failed to sync vos pool %s: " DF_RC "\n", DP_UUID(uuid), path,
			DP_RC(rc));
		goto out;
	}
out:
	if (fd != -1)
		close(fd);
	D_FREE(path);
	return rc;
}

struct tgt_prealloc_args {
	uuid_t        tvpa_uuid;
	daos_size_t   tvpa_scm_size;
	int           tvpa_tgt_id;
	const char   *tvpa_newborns_path;
	bind_cpu_fn_t tvpa_bind_cpu_fn;
};

struct tgt_thrdlist {
	d_list_t                 tvt_link;
	pthread_t                tvt_tid;
	struct tgt_prealloc_args tvt_args;
};

static void *
tgt_preallocate_thrd_func(void *arg)
{
	struct tgt_prealloc_args *tvpa = (struct tgt_prealloc_args *)arg;

	if (tvpa->tvpa_bind_cpu_fn)
		tvpa->tvpa_bind_cpu_fn(tvpa->tvpa_tgt_id);
	return (void *)(uintptr_t)ds_mgmt_tgt_preallocate(
	    tvpa->tvpa_uuid, tvpa->tvpa_scm_size, tvpa->tvpa_tgt_id, tvpa->tvpa_newborns_path);
}

static void
tgt_preallocate_thrds_cleanup(d_list_t *head)
{
	struct tgt_thrdlist *entry;
	int                  rc;

	d_list_for_each_entry(entry, head, tvt_link) {
		rc = pthread_cancel(entry->tvt_tid);
		if (rc) {
			rc = daos_errno2der(rc);
			D_ERROR("pthread_cancel failed: " DF_RC "\n", DP_RC(rc));
		}
	}

	d_list_for_each_entry(entry, head, tvt_link) {
		rc = pthread_join(entry->tvt_tid, NULL);
		if (rc) {
			rc = daos_errno2der(rc);
			D_ERROR("pthread_join failed: " DF_RC "\n", DP_RC(rc));
		}
	}
}

int
ds_mgmt_tgt_preallocate_sequential(uuid_t uuid, daos_size_t scm_size, int tgt_nr,
				   const char *newborns_path)
{
	int i, rc = 0;

	for (i = 0; i < tgt_nr; i++) {
		rc = ds_mgmt_tgt_preallocate(uuid, scm_size, i, newborns_path);
		if (rc)
			break;
	}
	return rc;
}

int
ds_mgmt_tgt_preallocate_parallel(uuid_t uuid, daos_size_t scm_size, int tgt_nr,
				 bool *cancel_pending, const char *newborns_path,
				 bind_cpu_fn_t bind_cpu_fn)
{
	int                  i;
	int                  rc;
	int                  saved_rc = 0;
	int                  res;
	int                  old_cancelstate;
	struct tgt_thrdlist *entry, *tmp;
	struct tgt_thrdlist *thrds_list;

	D_LIST_HEAD(thrds_list_head);

	D_INIT_LIST_HEAD(&thrds_list_head);
	D_ALLOC_ARRAY(thrds_list, tgt_nr);

	/* Disable cancellation to manage other threads created within. */
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_cancelstate);

	for (i = 0; i < tgt_nr; i++) {
		entry = &thrds_list[i];
		uuid_copy(entry->tvt_args.tvpa_uuid, uuid);
		entry->tvt_args.tvpa_scm_size      = scm_size;
		entry->tvt_args.tvpa_tgt_id        = i;
		entry->tvt_args.tvpa_newborns_path = newborns_path;
		entry->tvt_args.tvpa_bind_cpu_fn   = bind_cpu_fn;
		rc = pthread_create(&entry->tvt_tid, NULL, tgt_preallocate_thrd_func,
				    &entry->tvt_args);
		if (rc) {
			saved_rc = daos_errno2der(rc);
			D_ERROR(DF_UUID ": failed to create thread for target file "
					"creation: " DF_RC "\n",
				DP_UUID(uuid), DP_RC(saved_rc));
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
				D_ERROR("pthread_join failed: " DF_RC "\n", DP_RC(rc));
			}
			if (!saved_rc && rc)
				saved_rc = rc;
			d_list_del(&entry->tvt_link);
		}
	}
out:
	tgt_preallocate_thrds_cleanup(&thrds_list_head);
	D_FREE(thrds_list);
	pthread_setcancelstate(old_cancelstate, NULL);
	return saved_rc;
}
