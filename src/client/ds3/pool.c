/**
 * (C) Copyright 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ds3_internal.h"
#include <sys/stat.h>
#include <fcntl.h>

int
ds3_init(void)
{
	return -dfs_init();
}

int
ds3_fini(void)
{
	return -dfs_fini();
}

/**
 * Helper functions for metadata directories management
 */

const char *
meta_dir_name(enum meta_dir dir)
{
#define X(a, b)                                                                                    \
	case a:                                                                                    \
		return b;
	switch (dir) {
		METADATA_DIR_LIST
	default:
		return "";
	}
#undef X
}

static int
create_metadir(dfs_t *dfs, const char *dir)
{
	int rc;

	rc = dfs_mkdir(dfs, NULL, dir, DEFFILEMODE, 0);
	if (rc == EEXIST)
		rc = 0;
	if (rc != 0)
		D_ERROR("failed to create meta dir %s, rc = %d\n", dir, rc);

	return rc;
}

static int
open_metadir(dfs_t *dfs, const char *dir, dfs_obj_t **obj)
{
	int rc;

	rc = dfs_lookup_rel(dfs, NULL, dir, O_RDWR, obj, NULL, NULL);
	if (rc != 0)
		D_ERROR("failed to open meta dir %s, rc = %d\n", dir, rc);

	return rc;
}

static int
close_metadir(const char *dir, dfs_obj_t *obj)
{
	int rc;

	rc = dfs_release(obj);
	if (rc != 0)
		D_ERROR("failed to release meta dir %s, rc = %d\n", dir, rc);

	return rc;
}

/**
 * Primary S3 pool methods
 */

int
ds3_connect(const char *pool, const char *sys, ds3_t **ds3, daos_event_t *ev)
{
	int	rc, rc2;
	ds3_t	*ds3_tmp;

	if (ds3 == NULL || pool == NULL)
		return -EINVAL;

	D_ALLOC_PTR(ds3_tmp);
	if (ds3_tmp == NULL)
		return -ENOMEM;

	/** Copy pool name */
	strncpy(ds3_tmp->pool, pool, DAOS_PROP_LABEL_MAX_LEN);
	ds3_tmp->pool[DAOS_PROP_LABEL_MAX_LEN] = '\0';

	/** Connect to the pool first */
	rc = daos_pool_connect(pool, sys, DAOS_PC_RW, &ds3_tmp->poh, &ds3_tmp->pinfo, ev);
	if (rc != 0) {
		D_ERROR("Failed to connect to pool %s, rc = %d\n", pool, rc);
		rc = daos_der2errno(rc);
		goto err_ds3;
	}

	/** Connect to metatata container, create if it doesn't exist */
	rc = dfs_connect(ds3_tmp->pool, NULL, METADATA_BUCKET, O_CREAT | O_RDWR, NULL,
			 &ds3_tmp->meta_dfs);
	if (rc == 0) {
/** Create inner directories */
#define X(a, b)                                                                                    \
	do {                                                                                       \
		rc = create_metadir(ds3_tmp->meta_dfs, b);                                         \
		if (rc)                                                                            \
			goto err;                                                                  \
	} while (0);

		METADATA_DIR_LIST

#undef X
	} else {
		D_ERROR("Failed to create metadata container in pool %s, rc = %d\n", pool, rc);
		goto err_poh;
	}

/** Open metadata dirs */
#define X(a, b)                                                                                    \
	do {                                                                                       \
		rc = open_metadir(ds3_tmp->meta_dfs, b, &ds3_tmp->meta_dirs[a]);                   \
		if (rc)                                                                            \
			goto err;                                                                  \
	} while (0);

	METADATA_DIR_LIST

#undef X

	*ds3 = ds3_tmp;
	return 0;

err:
#define X(a, b) close_metadir(b, ds3_tmp->meta_dirs[a]);

	METADATA_DIR_LIST

#undef X

	rc2 = dfs_disconnect(ds3_tmp->meta_dfs);
	if (rc2)
		D_ERROR("dfs_disconnect() Failed %d (%s)\n", rc2, strerror(rc2));
err_poh:
	rc2 = daos_pool_disconnect(ds3_tmp->poh, NULL);
	if (rc2)
		D_ERROR("daos_pool_disconnect() Failed "DF_RC"\n", DP_RC(rc2));
err_ds3:
	D_FREE(ds3_tmp);
	return -rc;
}

int
ds3_disconnect(ds3_t *ds3, daos_event_t *ev)
{
	int rc = 0;

	if (ds3 == NULL)
		return 0;

#define X(a, b) close_metadir(b, ds3->meta_dirs[a]);

	METADATA_DIR_LIST

#undef X

	rc = dfs_disconnect(ds3->meta_dfs);
	if (rc)
		D_ERROR("dfs_disconnect() Failed %d (%s)\n", rc, strerror(rc));
	rc = daos_pool_disconnect(ds3->poh, ev);
	if (rc) {
		D_ERROR("daos_pool_disconnect() Failed "DF_RC"\n", DP_RC(rc));
		rc = daos_der2errno(rc);
	}
	D_FREE(ds3);
	return -rc;
}
