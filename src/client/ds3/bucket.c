/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ds3_internal.h"

int
ds3_bucket_list(daos_size_t *nbuck, struct ds3_bucket_info *buf, char *marker, bool *is_truncated,
		ds3_t *ds3, daos_event_t *ev)
{
	int                         rc = 0;
	struct daos_pool_cont_info *conts;
	daos_size_t                 ncont = *nbuck;
	daos_size_t                 bi    = 0;
	ds3_bucket_t               *ds3b  = NULL;

	if (ds3 == NULL || nbuck == NULL || buf == NULL || marker == NULL)
		return -EINVAL;

	D_ALLOC_ARRAY(conts, ncont);
	if (conts == NULL) {
		return -ENOMEM;
	}

	// TODO: Handle markers
	rc = daos_pool_list_cont(ds3->poh, &ncont, conts, ev);
	if (rc == 0) {
		*is_truncated = false;
	} else if (rc == -DER_TRUNC) {
		rc            = 0;
		*is_truncated = true;
	} else {
		D_ERROR("Failed to list containers in pool, rc = %d\n", rc);
		rc = daos_der2errno(rc);
		goto err;
	}

	for (int i = 0; i < ncont; i++) {
		char *name = conts[i].pci_label;
		if (strcmp(name, METADATA_BUCKET) == 0) {
			D_INFO("Skipping container: %s", name);
			continue;
		}

		// Copy bucket name
		strcpy(buf[bi].name, name);

		// Get info
		rc = ds3_bucket_open(name, &ds3b, ds3, ev);
		if (rc != 0) {
			D_INFO("Skipping container: %s", name);
			continue;
		}

		rc = ds3_bucket_get_info(&buf[bi], ds3b, ev);
		if (rc != 0) {
			D_INFO("Skipping container: %s", name);
			rc = ds3_bucket_close(ds3b, ev);
			continue;
		}

		rc = ds3_bucket_close(ds3b, ev);
		if (rc != 0)
			goto err;

		bi++;
		// TODO handle marker
	}

	*nbuck = bi;

err:
	D_FREE(conts);
	return -rc;
}

int
ds3_bucket_create(const char *name, struct ds3_bucket_info *info, dfs_attr_t *attr, ds3_t *ds3,
		  daos_event_t *ev)
{
	int           rc   = 0;
	ds3_bucket_t *ds3b = NULL;

	if (ds3 == NULL || name == NULL)
		return -EINVAL;

	// Prevent attempting to create metadata bucket
	if (strcmp(name, METADATA_BUCKET) == 0) {
		D_ERROR("Cannot create metadata bucket");
		return -EINVAL;
	}

	// Create dfs container and open ds3b
	rc = dfs_cont_create_with_label(ds3->poh, name, attr, NULL, NULL, NULL);
	if (rc != 0) {
		D_ERROR("Failed to create container, rc = %d\n", rc);
		return -rc;
	}

	rc = ds3_bucket_open(name, &ds3b, ds3, ev);
	if (rc != 0) {
		D_ERROR("Failed to open container, rc = %d\n", rc);
		return -rc;
	}

	rc = ds3_bucket_set_info(info, ds3b, ev);
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
	ds3_bucket_close(ds3b, ev);
	return -rc;
}

int
ds3_bucket_destroy(const char *name, bool force, ds3_t *ds3, daos_event_t *ev)
{
	int            rc      = 0;
	ds3_bucket_t  *ds3b    = NULL;
	uint32_t       nd      = 10;
	struct dirent *dirents = NULL;
	dfs_obj_t     *dir_obj = NULL;
	daos_anchor_t  anchor;

	if (ds3 == NULL || name == NULL)
		return -EINVAL;

	rc = ds3_bucket_open(name, &ds3b, ds3, ev);
	if (rc != 0) {
		return -rc;
	}

	// Check if the bucket is empty
	if (!force) {
		rc = dfs_lookup(ds3b->dfs, "/", O_RDWR, &dir_obj, NULL, NULL);
		if (rc != 0) {
			goto err_ds3b;
		}

		D_ALLOC_ARRAY(dirents, nd);
		if (dirents == NULL) {
			rc = ENOMEM;
			goto err_dir_obj;
		}

		daos_anchor_init(&anchor, 0);
		rc = dfs_readdir(ds3b->dfs, dir_obj, &anchor, &nd, dirents);
		if (rc != 0) {
			goto err_dirents;
		}

		// The bucket is not empty
		if (nd != 0) {
			rc = ENOTEMPTY;
			goto err_dirents;
		}
	}

	// Remove the bucket's multipart directory
	rc = dfs_remove(ds3->meta_dfs, ds3->meta_dirs[MULTIPART_DIR], name, true, NULL);
	if (rc != 0) {
		goto err_dirents;
	}

	// Finally, destroy the bucket
	rc = daos_cont_destroy(ds3->poh, name, true, NULL);
	rc = daos_der2errno(rc);

err_dirents:
	if (dirents)
		D_FREE(dirents);
err_dir_obj:
	if (dir_obj)
		dfs_release(dir_obj);
err_ds3b:
	ds3_bucket_close(ds3b, ev);
	return -rc;
}

int
ds3_bucket_open(const char *name, ds3_bucket_t **ds3b, ds3_t *ds3, daos_event_t *ev)
{
	int           rc;
	ds3_bucket_t *ds3b_tmp;

	if (ds3 == NULL || name == NULL || ds3b == NULL)
		return -EINVAL;

	// Prevent attempting to open metadata bucket
	if (strcmp(name, METADATA_BUCKET) == 0) {
		D_ERROR("Cannot open metadata bucket");
		return -ENOENT;
	}

	D_ALLOC_PTR(ds3b_tmp);
	if (ds3b_tmp == NULL)
		return -ENOMEM;

	rc = dfs_connect(ds3->pool, NULL, name, O_RDWR, NULL, &ds3b_tmp->dfs);
	if (rc != 0) {
		goto err_ds3b;
	}

	*ds3b = ds3b_tmp;
	return 0;

err_ds3b:
	D_FREE(ds3b_tmp);
	return -rc;
}

int
ds3_bucket_close(ds3_bucket_t *ds3b, daos_event_t *ev)
{
	int rc = 0;

	rc = dfs_disconnect(ds3b->dfs);
	D_FREE(ds3b);
	return -rc;
}

int
ds3_bucket_get_info(struct ds3_bucket_info *info, ds3_bucket_t *ds3b, daos_event_t *ev)
{
	int               rc       = 0;
	char const *const names[]  = {RGW_BUCKET_INFO};
	void *const       values[] = {info->encoded};
	size_t            sizes[]  = {info->encoded_length};
	daos_handle_t     coh;

	if (ds3b == NULL || info == NULL)
		return -EINVAL;

	rc = dfs_cont_get(ds3b->dfs, &coh);
	if (rc != 0) {
		return -rc;
	}

	rc = daos_cont_get_attr(coh, 1, names, values, sizes, ev);
	rc = daos_der2errno(rc);
	dfs_cont_put(ds3b->dfs, coh);
	return -rc;
}

int
ds3_bucket_set_info(struct ds3_bucket_info *info, ds3_bucket_t *ds3b, daos_event_t *ev)
{
	int               rc       = 0;
	char const *const names[]  = {RGW_BUCKET_INFO};
	void const *const values[] = {info->encoded};
	size_t const      sizes[]  = {info->encoded_length};
	daos_handle_t     coh;

	if (ds3b == NULL || info == NULL)
		return -EINVAL;

	rc = dfs_cont_get(ds3b->dfs, &coh);
	if (rc != 0) {
		return -rc;
	}

	rc = daos_cont_set_attr(coh, 1, names, values, sizes, ev);
	rc = daos_der2errno(rc);
	dfs_cont_put(ds3b->dfs, coh);
	return -rc;
}

int
ds3_bucket_list_obj(uint32_t *nobj, struct ds3_object_info *objs, uint32_t *ncp,
		    struct ds3_common_prefix_info *cps, const char *prefix, const char *delim,
		    char *marker, bool list_versions, bool *is_truncated, ds3_bucket_t *ds3b)
{
	int            rc = 0;
	char          *file_start;
	const char    *path        = "";
	const char    *prefix_rest = prefix;
	dfs_obj_t     *dir_obj;
	char          *lookup_path;
	struct dirent *dirents;
	daos_anchor_t  anchor;
	uint32_t       cpi  = 0;
	uint32_t       obji = 0;
	uint32_t       i;
	const char    *name;
	dfs_obj_t     *entry_obj;
	mode_t         mode;
	char          *cpp;

	if (ds3b == NULL || nobj == NULL)
		return -EINVAL;

	// End
	if (*nobj == 0)
		return 0;

	// TODO: support the case when delim is not /
	if (strcmp(delim, "/") != 0) {
		return -EINVAL;
	}

	file_start = strrchr(prefix, delim[0]);
	if (file_start != NULL) {
		*file_start = '\0';
		path        = prefix;
		prefix_rest = file_start + 1;
	}

	D_ALLOC_ARRAY(lookup_path, DS3_MAX_KEY);
	if (lookup_path == NULL)
		return -ENOMEM;

	strcpy(lookup_path, "/");
	strcat(lookup_path, path);
	rc = dfs_lookup(ds3b->dfs, lookup_path, O_RDWR, &dir_obj, NULL, NULL);
	if (rc != 0) {
		goto err_path;
	}

	D_ALLOC_ARRAY(dirents, *nobj);
	if (dirents == NULL) {
		rc = ENOMEM;
		goto err_dir_obj;
	}

	// TODO handle bigger directories
	// TODO handle ordering
	// TODO handle marker
	daos_anchor_init(&anchor, 0);

	rc = dfs_readdir(ds3b->dfs, dir_obj, &anchor, nobj, dirents);
	if (rc != 0) {
		goto err_dirents;
	}

	if (is_truncated != NULL) {
		*is_truncated = !daos_anchor_is_eof(&anchor);
	}

	for (i = 0; i < *nobj; i++) {
		name = dirents[i].d_name;

		// Skip entries that do not start with prefix_rest
		// TODO handle how this affects max
		if (strncmp(name, prefix_rest, strlen(prefix_rest)) != 0) {
			continue;
		}

		rc = dfs_lookup_rel(ds3b->dfs, dir_obj, name, O_RDWR | O_NOFOLLOW, &entry_obj,
				    &mode, NULL);
		if (rc != 0) {
			goto err_dirents;
		}

		if (S_ISDIR(mode)) {
			// The entry is a directory

			// Out of bounds
			if (cpi >= *ncp) {
				rc = EINVAL;
				goto err_dirents;
			}

			// Add to cps
			cpp = cps[cpi].prefix;
			if (strlen(path) != 0) {
				strcpy(cpp, path);
				strcat(cpp, delim);
			} else {
				strcpy(cpp, "");
			}
			strcat(cpp, name);
			strcat(cpp, delim);

			cpi++;
		} else if (S_ISREG(mode)) {
			// The entry is a regular file
			// Read the xattr and add to objs
			// TODO make more efficient
			rc = dfs_getxattr(ds3b->dfs, entry_obj, RGW_DIR_ENTRY_XATTR,
					  objs[obji].encoded, &objs[obji].encoded_length);
			// Skip if file has no dirent
			if (rc != 0) {
				D_WARN("No dirent, skipping entry= %s\n", name);
				dfs_release(entry_obj);
				continue;
			}

			obji++;
		} else {
			// Skip other types
			D_INFO("Skipping entry = %s\n", name);
		}

		// Close handles
		dfs_release(entry_obj);
	}

	// Set the number of read objects
	*nobj = obji;
	*ncp  = cpi;

err_dirents:
	D_FREE(dirents);
err_dir_obj:
	dfs_release(dir_obj);
err_path:
	D_FREE(lookup_path);
	return -rc;
}
