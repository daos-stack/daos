/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ds3_internal.h"

int
ds3_bucket_list(daos_size_t *nbuck, struct ds3_bucket_info *buf, char *marker, ds3_t *ds3,
		daos_event_t *ev)
{
	if (ds3 == NULL || nbuck == NULL || buf == NULL || marker == NULL)
		return -EINVAL;

	int                         rc = 0;
	struct daos_pool_cont_info *conts;
	daos_size_t                 ncont = *nbuck;
	D_ALLOC_ARRAY(conts, *nbuck);
	if (conts == NULL) {
		return -ENOMEM;
	}

	// TODO: Handle markers and other bucket info
	// TODO handle DER_TRUNC
	rc = daos_pool_list_cont(ds3->poh, &ncont, conts, ev);
	if (rc != 0 && rc != -DER_TRUNC) {
		D_ERROR("Failed to list containers in pool, rc = %d\n", rc);
		rc = daos_der2errno(rc);
		goto err;
	}

	*nbuck = ncont;
	for (int i = 0; i < ncont; i++) {
		char *name = conts[i].pci_label;
		if (strcmp(name, METADATA_BUCKET) == 0) {
			// Skip metadata bucket
			(*nbuck)--;
			continue;
		}

		// copy bucket name
		strcpy(buf[i].name, conts[i].pci_label);

		// TODO load bucket info here
		// TODO handle marker
	}

err:
	D_FREE(conts);
	return -rc;
}

int
ds3_bucket_create(const char *name, struct ds3_bucket_info *info, dfs_attr_t *attr, ds3_t *ds3,
		  daos_event_t *ev)
{
	if (ds3 == NULL || name == NULL)
		return -EINVAL;

	int               rc = 0;

	struct ds3_bucket ds3b;

	// Create dfs container and open ds3b
	rc = dfs_cont_create_with_label(ds3->poh, name, attr, NULL, &ds3b.coh, &ds3b.dfs);
	if (rc != 0) {
		D_ERROR("Failed to create container, rc = %d\n", rc);
		return -rc;
	}

	rc = ds3_bucket_set_info(info, &ds3b, ev);
	if (rc != 0) {
		D_ERROR("Failed to put bucket info, rc = %d\n", rc);
		goto err;
	}

	// Create multipart index
	rc = dfs_mkdir(ds3->meta_dfs, ds3->meta_dirs[MULTIPART_DIR], name, DEFFILEMODE, 0);
	if (rc != 0 && rc != EEXIST) {
		D_ERROR("Failed to create multipart index, rc = %d\n", rc);
	}

err:
	ds3_bucket_close(&ds3b, ev);
	return -rc;
}

int
ds3_bucket_destroy(const char *name, ds3_t *ds3, daos_event_t *ev)
{
	return 0;
}

int
ds3_bucket_open(const char *name, ds3_bucket_t **ds3b, ds3_t *ds3, daos_event_t *ev)
{
	if (ds3 == NULL || name == NULL || ds3b == NULL)
		return -EINVAL;

	int           rc;
	ds3_bucket_t *ds3b_tmp;
	D_ALLOC_PTR(ds3b_tmp);
	if (ds3b_tmp == NULL)
		return -ENOMEM;

	// TODO: We need to cache open container handles
	rc = daos_cont_open(ds3->poh, name, DAOS_COO_RW, &ds3b_tmp->coh, &ds3b_tmp->cont_info, ev);
	if (rc != 0) {
		D_ERROR("Failed to open to container %s, rc = %d\n", name, rc);
		rc = daos_der2errno(rc);
		goto err_ds3b;
	}

	rc = dfs_mount(ds3->poh, ds3b_tmp->coh, O_RDWR, &ds3b_tmp->dfs);

	if (rc != 0) {
		goto err;
	}

	*ds3b = ds3b_tmp;
	return 0;

err:
	daos_cont_close(ds3b_tmp->coh, ev);
err_ds3b:
	D_FREE(ds3b);
	return -rc;
}

int
ds3_bucket_close(ds3_bucket_t *ds3b, daos_event_t *ev)
{
	int rc = 0;
	rc     = dfs_umount(ds3b->dfs);
	daos_cont_close(ds3b->coh, ev);
	D_FREE(ds3b);
	return -rc;
}

int
ds3_bucket_get_info(struct ds3_bucket_info *info, ds3_bucket_t *ds3b, daos_event_t *ev)
{
	return 0;
}

int
ds3_bucket_set_info(struct ds3_bucket_info *info, ds3_bucket_t *ds3b, daos_event_t *ev)
{
	if (ds3b == NULL || info == NULL)
		return -EINVAL;

	char const *const names[]  = {RGW_BUCKET_INFO};
	void const *const values[] = {info->encoded};
	size_t const      sizes[]  = {info->encoded_length};
	return -daos_cont_set_attr(ds3b->coh, 1, names, values, sizes, ev);
}

int
ds3_bucket_list_obj(daos_size_t *nobj, struct ds3_object_info *buf, const char *prefix,
		    const char *delim, char *marker, bool list_versions, ds3_bucket_t *ds3b,
		    daos_event_t *ev)
{
	return 0;
}
