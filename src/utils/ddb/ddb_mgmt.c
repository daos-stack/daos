/**
 * (C) Copyright 2025 Vdura Inc.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <ftw.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/sysinfo.h>
#include <linux/magic.h>

#include <gurt/debug.h>
#include <daos_srv/control.h>
#include <daos_srv/smd.h>
#include <daos_srv/mgmt_tgt_common.h>
#include <daos_srv/bio.h>

#include "ddb_mgmt.h"

#define DDB_PROV_MEM_BUF_MAX 256

int
ddb_auto_calculate_scm_mount_size(unsigned int *scm_mount_size)
{
	struct smd_pool_info *pool_info = NULL;
	struct smd_pool_info *tmp;
	d_list_t              pool_list;
	int                   rc = 0;
	int                   pool_list_cnt;
	uint64_t              pool_size;
	uint64_t              total_size;
	const unsigned long   GiB = (1ul << 30);

	D_ASSERT(scm_mount_size != NULL);
	D_ASSERT(bio_nvme_configured(SMD_DEV_TYPE_META));
	D_INIT_LIST_HEAD(&pool_list);
	rc = smd_pool_list(&pool_list, &pool_list_cnt);
	if (rc != 0) {
		D_ERROR("Failed to get pool info list from SMD\n");
		return rc;
	}

	total_size = 0;
	d_list_for_each_entry_safe(pool_info, tmp, &pool_list, spi_link) {
		if ((pool_info->spi_blob_sz[SMD_DEV_TYPE_META] == 0) ||
		    (pool_info->spi_flags[SMD_DEV_TYPE_META] & SMD_POOL_IN_CREATION)) {
			continue;
		}
		D_ASSERT(pool_info->spi_scm_sz > 0);

		/** Align to 4K */
		pool_size = (D_ALIGNUP(pool_info->spi_scm_sz, 1ULL << 12)) *
			    pool_info->spi_tgt_cnt[SMD_DEV_TYPE_META];
		total_size += pool_size;
		D_INFO("Pool " DF_UUID " required scm size: " DF_U64 "", DP_UUID(pool_info->spi_id),
		       pool_size);
	}

	d_list_for_each_entry_safe(pool_info, tmp, &pool_list, spi_link) {
		d_list_del(&pool_info->spi_link);
		/* Frees spi_tgts, spi_blobs, and pool_info */
		smd_pool_free_info(pool_info);
	}

	*scm_mount_size = (D_ALIGNUP(total_size, GiB) / GiB);
	return rc;
}

static int
destroy_cb(const char *path, const struct stat *sb, int flag, struct FTW *ftwbuf)
{
	int rc;

	if (ftwbuf->level == 0)
		return 0;

	switch (flag) {
	case FTW_DP:
		rc = rmdir(path);
		break;
	case FTW_F:
		rc = unlink(path);
		break;
	default:
		D_ERROR("Cannot delete %s. Unexpected file type: %#x", path, flag);
		return EINVAL;
	}

	if (rc)
		D_ERROR("Failed to remove %s", path);
	return rc;
}

int
ddb_clear_dir(const char *dir)
{
	int rc;

	rc = nftw(dir, destroy_cb, 32, FTW_DEPTH | FTW_PHYS | FTW_MOUNT);
	if (rc)
		rc = (errno == 0 ? rc : daos_errno2der(errno));

	return rc;
}

int
ddb_is_mountpoint(const char *path)
{
	int           rc = 0;
	struct stat   st_path, st_parent;
	char          parent_path[DDB_PROV_MEM_BUF_MAX];

	D_ASSERT(path != NULL);
	if (access(path, F_OK) != 0)
		return daos_errno2der(errno);

	rc = snprintf(parent_path, sizeof(parent_path), "%s/..", path);
	if (unlikely(rc >= sizeof(parent_path)))
		return -DER_EXCEEDS_PATH_LEN;

	rc = stat(path, &st_path);
	if (rc)
		return daos_errno2der(errno);

	rc = stat(parent_path, &st_parent);
	if (rc)
		return daos_errno2der(errno);

	return (st_path.st_dev == st_parent.st_dev) ? 0 : 1;
}

int
ddb_recreate_pooltgts(const char *scm_mount)
{
	struct smd_pool_info *pool_info = NULL;
	struct smd_pool_info *tmp;
	d_list_t              pool_list;
	int                   rc = 0;
	int                   pool_list_cnt;

	D_ASSERT(bio_nvme_configured(SMD_DEV_TYPE_META));
	D_INIT_LIST_HEAD(&pool_list);
	rc = smd_pool_list(&pool_list, &pool_list_cnt);
	if (rc != 0) {
		D_ERROR("Failed to get pool info list from SMD");
		return rc;
	}

	d_list_for_each_entry_safe(pool_info, tmp, &pool_list, spi_link) {
		if ((pool_info->spi_blob_sz[SMD_DEV_TYPE_META] == 0) ||
		    (pool_info->spi_flags[SMD_DEV_TYPE_META] & SMD_POOL_IN_CREATION)) {
			continue;
		}

		D_INFO("Recreating files for the pool " DF_UUID "", DP_UUID(pool_info->spi_id));
		D_ASSERT(pool_info->spi_scm_sz > 0);
		/* specify rdb_blob_sz as zero to skip rdb file creation */
		rc = ds_mgmt_tgt_recreate(pool_info->spi_id, pool_info->spi_scm_sz,
					  pool_info->spi_tgt_cnt[SMD_DEV_TYPE_META], 0, scm_mount,
					  NULL);
		if (rc) {
			break;
		}
	}

	d_list_for_each_entry_safe(pool_info, tmp, &pool_list, spi_link) {
		d_list_del(&pool_info->spi_link);
		smd_pool_free_info(pool_info);
	}
	return rc;
}

static int
ddb_mkdir(const char *path, mode_t __mode)
{
	int    rc;
	mode_t stored_mode;

	stored_mode = umask(0);
	rc          = mkdir(path, __mode);
	umask(stored_mode);
	if (rc < 0 && errno != EEXIST)
		return daos_errno2der(errno);
	return 0;
}

int
ddb_dirs_prepare(const char *scm_mount)
{
	int    rc = 0;
	char   newborns_path[DDB_PROV_MEM_BUF_MAX];
	char   zombies_path[DDB_PROV_MEM_BUF_MAX];

	/* create the path string */
	rc = snprintf(newborns_path, sizeof(newborns_path), "%s/" DIR_NEWBORNS "", scm_mount);
	if (unlikely(rc >= sizeof(newborns_path)))
		return -DER_EXCEEDS_PATH_LEN;

	rc = snprintf(zombies_path, sizeof(zombies_path), "%s/" DIR_ZOMBIES "", scm_mount);
	if (unlikely(rc >= sizeof(zombies_path)))
		return -DER_EXCEEDS_PATH_LEN;

	rc = ddb_mkdir(newborns_path, S_IRWXU);
	if (rc)
		return rc;

	rc = ddb_mkdir(zombies_path, S_IRWXU);
	if (rc)
		return rc;

	/* clear remaining dir(s)/file(s) */
	rc = ddb_clear_dir(newborns_path);
	if (rc)
		D_ERROR("Failed to clear directory %s. " DF_RC "", newborns_path, DP_RC(rc));

	rc = ddb_clear_dir(zombies_path);
	if (rc)
		D_ERROR("Failed to clear directory %s. " DF_RC "", zombies_path, DP_RC(rc));

	return rc;
}

int
ddb_mount(const char *scm_mount, unsigned int scm_mount_size)
{
	char options[DDB_PROV_MEM_BUF_MAX] = {0};
	int  rc;

	memset(options, 0, sizeof(options));
	rc = snprintf(options, sizeof(options), "mpol=prefer:0,size=%ug,huge=always",
		      scm_mount_size);
	if (unlikely(rc >= sizeof(options))) {
		D_ERROR("The mount options too long. (%d >= %lu)", rc, sizeof(options));
		return -DER_EXCEEDS_PATH_LEN;
	}
	rc = mount("tmpfs", scm_mount, "tmpfs", MS_NOATIME, (void *)options);
	if (rc) {
		D_ERROR("Failed to mount tmpfs on %s: %s", scm_mount, strerror(errno));
		return daos_errno2der(errno);
	}

	return DER_SUCCESS;
}
