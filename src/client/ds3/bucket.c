/**
 * (C) Copyright 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ds3_internal.h"

int
ds3_bucket_list(daos_size_t *nbuck, struct ds3_bucket_info *buf, char *marker, bool *is_truncated,
		ds3_t *ds3, daos_event_t *ev)
{
	int				rc = 0;
	struct daos_pool_cont_info	*conts;
	daos_size_t			ncont;
	daos_size_t			bi = 0;
	daos_size_t			i = 0;
	ds3_bucket_t			*ds3b  = NULL;
	char				*name;

	if (ds3 == NULL || nbuck == NULL || buf == NULL || marker == NULL)
		return -EINVAL;

	ncont = *nbuck;
	D_ALLOC_ARRAY(conts, ncont);
	if (conts == NULL)
		return -ENOMEM;

	/* TODO: Handle markers */
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

	for (i = 0; i < ncont; i++) {
		name = conts[i].pci_label;
		if (strcmp(name, METADATA_BUCKET) == 0) {
			D_DEBUG(DB_ALL, "Skipping container %s because it is the metadata bucket",
				name);
			continue;
		}

		/* Copy bucket name */
		strcpy(buf[bi].name, name);

		/* Get info */
		rc = ds3_bucket_open(name, &ds3b, ds3, ev);
		if (rc != 0) {
			D_DEBUG(DB_ALL,
				"Skipping container %s because it could not be mounted by dfs",
				name);
			continue;
		}

		rc = ds3_bucket_get_info(&buf[bi], ds3b, ev);
		if (rc != 0) {
			D_DEBUG(DB_ALL, "Skipping container %s because it is not a ds3 bucket",
				name);
			rc = ds3_bucket_close(ds3b, ev);
			if (rc != 0)
				goto err;

			continue;
		}

		rc = ds3_bucket_close(ds3b, ev);
		if (rc != 0)
			goto err;

		bi++;
		/* TODO handle marker */
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
	int           rc2  = 0;
	ds3_bucket_t *ds3b = NULL;

	if (ds3 == NULL || name == NULL)
		return -EINVAL;

	/* Prevent attempting to create metadata bucket */
	if (strcmp(name, METADATA_BUCKET) == 0) {
		D_ERROR("Cannot create metadata bucket\n");
		return -EINVAL;
	}

	/* Create dfs container and open ds3b */
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

	/* Create multipart index */
	rc = dfs_mkdir(ds3->meta_dfs, ds3->meta_dirs[MULTIPART_DIR], name, DEFFILEMODE, 0);
	if (rc != 0 && rc != EEXIST)
		D_ERROR("Failed to create multipart index, rc = %d\n", rc);

err:
	rc2 = ds3_bucket_close(ds3b, ev);
	rc  = rc == 0 ? rc2 : rc;
	return -rc;
}

int
ds3_bucket_destroy(const char *name, bool force, ds3_t *ds3, daos_event_t *ev)
{
	int            rc      = 0;
	int            rc2     = 0;
	ds3_bucket_t  *ds3b    = NULL;
	uint32_t       nd      = 10;
	struct dirent *dirents = NULL;
	dfs_obj_t     *dir_obj = NULL;
	daos_anchor_t  anchor;

	if (ds3 == NULL || name == NULL)
		return -EINVAL;

	rc = ds3_bucket_open(name, &ds3b, ds3, ev);
	if (rc != 0)
		return -rc;

	/* Check if the bucket is empty */
	if (!force) {
		rc = dfs_lookup(ds3b->dfs, "/", O_RDWR, &dir_obj, NULL, NULL);
		if (rc != 0)
			goto err_ds3b;

		D_ALLOC_ARRAY(dirents, nd);
		if (dirents == NULL) {
			rc = ENOMEM;
			goto err_dir_obj;
		}

		daos_anchor_init(&anchor, 0);
		rc = dfs_readdir(ds3b->dfs, dir_obj, &anchor, &nd, dirents);
		if (rc != 0)
			goto err_dirents;

		/* The bucket is not empty */
		if (nd != 0) {
			rc = ENOTEMPTY;
			goto err_dirents;
		}
	}

	/* Remove the bucket's multipart directory */
	rc = dfs_remove(ds3->meta_dfs, ds3->meta_dirs[MULTIPART_DIR], name, true, NULL);
	if (rc != 0)
		goto err_dirents;

	/* Finally, destroy the bucket */
	rc = daos_cont_destroy(ds3->poh, name, true, NULL);
	rc = daos_der2errno(rc);

err_dirents:
	if (dirents)
		D_FREE(dirents);
err_dir_obj:
	if (dir_obj)
		rc2 = dfs_release(dir_obj);
	rc = rc == 0 ? rc2 : rc;
err_ds3b:
	rc2 = ds3_bucket_close(ds3b, ev);
	rc  = rc == 0 ? rc2 : rc;
	return -rc;
}

int
ds3_bucket_open(const char *name, ds3_bucket_t **ds3b, ds3_t *ds3, daos_event_t *ev)
{
	int           rc;
	ds3_bucket_t *ds3b_tmp;

	if (ds3 == NULL || name == NULL || ds3b == NULL)
		return -EINVAL;

	/* Prevent attempting to open metadata bucket */
	if (strcmp(name, METADATA_BUCKET) == 0) {
		D_ERROR("Cannot open metadata bucket\n");
		return -ENOENT;
	}

	D_ALLOC_PTR(ds3b_tmp);
	if (ds3b_tmp == NULL)
		return -ENOMEM;

	rc = dfs_connect(ds3->pool, NULL, name, O_RDWR, NULL, &ds3b_tmp->dfs);
	if (rc != 0)
		goto err_ds3b;

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
	char const *const names[]  = {RGW_BUCKET_INFO};
	daos_handle_t     coh;
	int               rc, rc2;

	if (ds3b == NULL || info == NULL)
		return -EINVAL;

	rc = dfs_cont_get(ds3b->dfs, &coh);
	if (rc != 0)
		return -rc;

	rc = daos_cont_get_attr(coh, 1, names, &info->encoded, &info->encoded_length, ev);
	if (rc) {
		rc = daos_der2errno(rc);
		goto out_put;
	}

out_put:
	rc2 = dfs_cont_put(ds3b->dfs, coh);
	if (rc == 0)
		rc = -rc2;
	return -rc;
}

int
ds3_bucket_set_info(struct ds3_bucket_info *info, ds3_bucket_t *ds3b, daos_event_t *ev)
{
	int               rc       = 0;
	char const *const names[]  = {RGW_BUCKET_INFO};
	daos_handle_t     coh;

	if (ds3b == NULL || info == NULL)
		return -EINVAL;

	rc = dfs_cont_get(ds3b->dfs, &coh);
	if (rc != 0)
		return -rc;

	rc = daos_cont_set_attr(coh, 1, names, (void *const)(&info->encoded),
				&info->encoded_length, ev);
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
	char          *file_start = NULL;
	const char    *path = "";
	char          *prefix_copy = NULL;
	const char    *prefix_rest = NULL;
	dfs_obj_t     *dir_obj;
	char          *lookup_path;
	struct dirent *dirents;
	daos_anchor_t  anchor;
	uint32_t       cpi;
	uint32_t       obji;
	uint32_t       i;
	const char    *name;
	dfs_obj_t     *entry_obj;
	mode_t         mode;
	char          *cpp;

	if (ds3b == NULL || nobj == NULL)
		return -EINVAL;
	if (prefix != NULL && strnlen(prefix, DS3_MAX_KEY) > DS3_MAX_KEY - 1)
		return -EINVAL;

	/* End */
	if (*nobj == 0)
		return 0;

	/* TODO: support the case when delim is not / */
	if (strcmp(delim, "/") != 0)
		return -EINVAL;

	if (prefix != NULL) {
		D_STRNDUP(prefix_copy, prefix, DS3_MAX_KEY_BUFF - 1);
		if (prefix_copy == NULL)
			return -ENOMEM;
		file_start  = strrchr(prefix_copy, delim[0]);
		prefix_rest = prefix_copy;
		if (file_start != NULL) {
			*file_start = '\0';
			path        = prefix_copy;
			prefix_rest = file_start + 1;
		}
	}

	D_ALLOC_ARRAY(lookup_path, DS3_MAX_KEY_BUFF);
	if (lookup_path == NULL) {
		rc = -ENOMEM;
		goto err_prefix;
	}

	strcpy(lookup_path, "/");
	strcat(lookup_path, path);
	rc = dfs_lookup(ds3b->dfs, lookup_path, O_RDWR, &dir_obj, NULL, NULL);
	if (rc != 0)
		goto err_path;

	D_ALLOC_ARRAY(dirents, *nobj);
	if (dirents == NULL) {
		rc = ENOMEM;
		goto err_dir_obj;
	}

	/**
	 * TODO handle bigger directories
	 * TODO handle ordering
	 * TODO handle marker
	 */
	daos_anchor_init(&anchor, 0);

	rc = dfs_readdir(ds3b->dfs, dir_obj, &anchor, nobj, dirents);
	if (rc != 0)
		goto err_dirents;

	if (is_truncated != NULL)
		*is_truncated = !daos_anchor_is_eof(&anchor);

	/**
	 * Go through the returned objects, if it is a regular file, add to objs. If it's a
	 * directory add to cps. Otherwise ignore.
	 */
	cpi  = 0;
	obji = 0;
	for (i = 0; i < *nobj; i++) {
		name = dirents[i].d_name;

		/**
		 * Skip entries that do not start with prefix_rest
		 * TODO handle how this affects max
		 */
		if (prefix_rest) {
			if (strncmp(name, prefix_rest, strlen(prefix_rest)) != 0)
				continue;
		}

		/* Open the file and check mode */
		rc = dfs_lookup_rel(ds3b->dfs, dir_obj, name, O_RDWR | O_NOFOLLOW, &entry_obj,
				    &mode, NULL);
		if (rc != 0)
			goto err_dirents;

		if (S_ISDIR(mode)) {
			/* The entry is a directory */

			/* Out of bounds */
			if (cpi >= *ncp) {
				rc = EINVAL;
				goto err_dirents;
			}

			/* Add to cps */
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
			/* The entry is a regular file */
			/* Read the xattr and add to objs */
			/* TODO make more efficient */
			rc = dfs_getxattr(ds3b->dfs, entry_obj, RGW_DIR_ENTRY_XATTR,
					  objs[obji].encoded, &objs[obji].encoded_length);
			/* Skip if file has no dirent */
			if (rc != 0) {
				D_DEBUG(DB_ALL, "No dirent, skipping entry= %s\n", name);
				rc = dfs_release(entry_obj);
				if (rc != 0)
					goto err_dirents;
				continue;
			}

			obji++;
		} else {
			/* Skip other types */
			D_DEBUG(DB_ALL, "Skipping entry = %s\n", name);
		}

		/* Close handles */
		rc = dfs_release(entry_obj);
		if (rc != 0)
			goto err_dirents;
	}

	/* Set the number of read objects */
	*nobj = obji;
	*ncp  = cpi;

err_dirents:
	D_FREE(dirents);
err_dir_obj:
	dfs_release(dir_obj);
err_path:
	D_FREE(lookup_path);
err_prefix:
	D_FREE(prefix_copy);
	return -rc;
}
