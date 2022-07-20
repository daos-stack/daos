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
		return EINVAL;

	int                         rc = 0;
	struct daos_pool_cont_info *conts;
	daos_size_t                 ncont = *nbuck;
	D_ALLOC_ARRAY(conts, *nbuck);
	if (conts == NULL) {
		return -DER_NOMEM;
	}

	// TODO: Handle markers and other bucket info
	rc = daos_pool_list_cont(ds3->poh, &ncont, conts, ev);
	if (rc < 0 && rc != -DER_TRUNC) {
		D_ERROR("Failed to list containers in pool, rc = %d\n", rc);
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
	return rc;
}

int
ds3_bucket_create(const char *name, struct ds3_bucket_info *info, dfs_attr_t *attr, ds3_t *ds3,
		  daos_event_t *ev)
{
	int rc = 0;

	// Create dfs container
	rc = dfs_cont_create_with_label(ds3->poh, name, attr, NULL, NULL, NULL);
	if (rc != 0) {
		D_ERROR("Failed to create container, rc = %d\n", rc);
		return rc;
	}

	// TODO do something with info

	// Create multipart index
	rc = dfs_mkdir(ds3->meta_dfs, ds3->meta_dirs[MULTIPART_DIR], name, DEFFILEMODE, 0);
	if (rc != 0 && rc != EEXIST) {
		D_ERROR("Failed to create multipart index, rc = %d\n", rc);
	}

	return rc;
}

int
ds3_bucket_destroy(const char *name, ds3_t *ds3, daos_event_t *ev)
{
	return 0;
}

int
ds3_bucket_open(const char *name, ds3_bucket_t **ds3b, ds3_t *ds3, daos_event_t *ev)
{
	return 0;
}

int
ds3_bucket_close(ds3_bucket_t *ds3b, daos_event_t *ev)
{
	return 0;
}

int
ds3_bucket_get_info(struct ds3_bucket_info *info, ds3_bucket_t *ds3b, daos_event_t *ev)
{
	return 0;
}

int
ds3_bucket_set_info(struct ds3_bucket_info *info, ds3_bucket_t *ds3b, daos_event_t *ev)
{
	return 0;
}

int
ds3_bucket_list_obj(daos_size_t *nobj, struct ds3_object_info *buf, const char *prefix,
		    const char *delim, char *marker, bool list_versions, ds3_bucket_t *ds3b,
		    daos_event_t *ev)
{
	return 0;
}
