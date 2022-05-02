/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ds3_internal.h"
#include <sys/stat.h>
#include <fcntl.h>

int
ds3_init(void)
{
	return dfs_init();
}

int
ds3_fini(void)
{
	return dfs_fini();
}

/**
 * Helper functions for metadata directories management
 */

static int
create_metadir(dfs_t *dfs, const char *dir)
{
	int ret;

	ret = dfs_mkdir(dfs, NULL, dir, DEFFILEMODE, 0);
	if (ret != 0 && ret != EEXIST)
		D_ERROR("failed to create meta dir %s, rc = %d\n", dir, ret);

	return ret;
}

static int
open_metadir(dfs_t *dfs, const char *dir, dfs_obj_t **obj)
{
	int ret;

	ret = dfs_lookup_rel(dfs, NULL, dir, O_RDWR, obj, NULL, NULL);
	if (ret != 0)
		D_ERROR("failed to open meta dir %s, rc = %d\n", dir, ret);

	return ret;
}

static int
close_metadir(const char *dir, dfs_obj_t *obj)
{
	int ret;

	ret = dfs_release(obj);
	if (ret != 0)
		D_ERROR("failed to release meta dir %s, rc = %d\n", dir, ret);

	return ret;
}

/**
 * Primary S3 pool methods
 */

int
ds3_connect(const char *pool, const char *sys, ds3_t **ds3, daos_event_t *ev)
{
	int			ret;
	ds3_t			*ds3_tmp;

	D_ALLOC_PTR(ds3_tmp);
	if (ds3_tmp == NULL)
		return ENOMEM;

	/** Connect to the pool first */
	ret = daos_pool_connect(pool, sys, DAOS_PC_RW, &ds3_tmp->poh, &ds3_tmp->pinfo, NULL);
	if (ret != 0) {
		D_ERROR("Failed to connect to pool %s, rc = %d\n", pool, ret);
		ret = daos_der2errno(ret);
		goto err_poh;
	}

	/** Check whether mdata container already exist */
	ret = dfs_cont_create_with_label(ds3_tmp->poh, METADATA_BUCKET, NULL, NULL,
					 &ds3_tmp->meta_coh, &ds3_tmp->meta_dfs);
	if (ret == 0) {
		/** Create inner directories */
		#define X(a, b)						\
		do {							\
			ret = create_metadir(ds3_tmp->meta_dfs, b);	\
			if (ret)					\
				goto err;				\
		} while (0);

		METADATA_DIR_LIST

		#undef X
	} else if (ret == EEXIST) {
		/** Metadata container exists, mount it */
		ret = daos_cont_open(ds3_tmp->poh, METADATA_BUCKET, DAOS_COO_RW, &ds3_tmp->meta_coh,
				     NULL, NULL);
		if (ret != 0) {
			D_ERROR("Failed to open metadata container for pool %s, rc = %d\n", pool,
				ret);
			ret = daos_der2errno(ret);
			goto err_poh;
		}

		ret = dfs_mount(ds3_tmp->poh, ds3_tmp->meta_coh, O_RDWR, &ds3_tmp->meta_dfs);
		if (ret != 0) {
			D_ERROR("Failed to mount metadata container for pool %s, rc = %d\n", pool,
				ret);
			ret = daos_der2errno(ret);
			goto err_coh;
		}

	} else {
		D_ERROR("Failed to create metadata container in pool %s, rc = %d\n", pool, ret);
		ret = daos_der2errno(ret);
		goto err_poh;
	}

	/** Open metadata dirs */
	#define  X(a, b)								\
	do {										\
		ret = open_metadir(ds3_tmp->meta_dfs, b, &ds3_tmp->meta_dirs[a]);	\
		if (ret)								\
			goto err;							\
	} while (0);

	METADATA_DIR_LIST

	#undef X

	*ds3 = ds3_tmp;
	return 0;

err:
	#define  X(a, b) close_metadir(b, ds3_tmp->meta_dirs[a]);

	METADATA_DIR_LIST

	#undef X

	dfs_umount(ds3_tmp->meta_dfs);
err_coh:
	daos_cont_close(ds3_tmp->meta_coh, NULL);
err_poh:
	daos_pool_disconnect(ds3_tmp->poh, NULL);
	return ret;
}

int
ds3_disconnect(ds3_t *ds3, daos_event_t *ev)
{
	return 0;
}
