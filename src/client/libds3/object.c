/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ds3_internal.h"

// helper
static bool
ends_with(const char *str, const char *suffix)
{
	if (!str || !suffix)
		return 0;
	size_t lenstr    = strlen(str);
	size_t lensuffix = strlen(suffix);
	if (lensuffix > lenstr)
		return 0;
	return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

int
ds3_obj_create(const char *key, ds3_obj_t **ds3o, ds3_bucket_t *ds3b)
{
	if (ds3b == NULL || key == NULL || ds3o == NULL)
		return -EINVAL;

	if (ends_with(key, LATEST_INSTANCE_SUFFIX)) {
		D_ERROR("Creating an object that ends with %s is not allowed.\n",
			LATEST_INSTANCE_SUFFIX);
		return -EINVAL;
	}

	// TODO: cache open file handles
	int        rc = 0;
	ds3_obj_t *ds3o_tmp;
	D_ALLOC_PTR(ds3o_tmp);
	if (ds3o_tmp == NULL)
		return -ENOMEM;

	char *path;
	D_STRNDUP(path, key, DS3_MAX_KEY - 1);
	if (path == NULL) {
		rc = ENOMEM;
		goto err_ds3o;
	}

	dfs_obj_t *parent      = NULL;
	char      *file_start  = strrchr(path, '/');
	char      *file_name   = path;
	char      *parent_path = NULL;
	if (file_start != NULL) {
		*file_start = '\0';
		file_name   = file_start + 1;
		parent_path = path;
	}
	mode_t mode = DEFFILEMODE;

	if (parent_path != NULL) {
		// Recursively open parent directories
		dfs_obj_t *dir_obj;
		char      *sptr = NULL;
		char      *dir;

		for (dir = strtok_r(parent_path, "/", &sptr); dir != NULL;
		     dir = strtok_r(NULL, "/", &sptr)) {
			// Create directory
			rc = dfs_mkdir(ds3b->dfs, parent, dir, mode, 0);

			if (rc != 0 && rc != EEXIST) {
				goto err_parent;
			}

			// Open directory
			rc = dfs_lookup_rel(ds3b->dfs, parent, dir, O_RDWR, &dir_obj, NULL, NULL);
			if (rc != 0) {
				goto err_parent;
			}

			// Next parent
			if (parent) {
				dfs_release(parent);
			}
			parent = dir_obj;
		}
	}

	// Finally create the file
	rc = dfs_open(ds3b->dfs, parent, file_name, mode | S_IFREG, O_RDWR | O_CREAT | O_TRUNC, 0,
		      0, NULL, &ds3o_tmp->dfs_obj);

	if (rc == 0 || rc == EEXIST) {
		rc    = 0;
		*ds3o = ds3o_tmp;
	}

err_parent:
	if (parent)
		dfs_release(parent);
	D_FREE(path);
err_ds3o:
	if (rc != 0)
		D_FREE(ds3o_tmp);
	return -rc;
}

int
ds3_obj_open(const char *key, ds3_obj_t **ds3o, ds3_bucket_t *ds3b)
{
	if (ds3b == NULL || key == NULL || ds3o == NULL)
		return -EINVAL;

	int        rc = 0;
	ds3_obj_t *ds3o_tmp;
	D_ALLOC_PTR(ds3o_tmp);
	if (ds3o_tmp == NULL)
		return -ENOMEM;

	// TODO: cache open file handles
	char *path;
	D_ALLOC_ARRAY(path, DS3_MAX_KEY);
	if (path == NULL) {
		rc = ENOMEM;
		goto err_ds3o;
	}

	if (key[0] == '/') {
		strcpy(path, "");
	} else {
		strcpy(path, "/");
	}
	strcat(path, key);

	rc = dfs_lookup(ds3b->dfs, path, O_RDWR, &ds3o_tmp->dfs_obj, NULL, NULL);

	if (rc == ENOENT) {
		if (ends_with(path, LATEST_INSTANCE_SUFFIX)) {
			// If we are trying to access the latest version, try accessing key with
			// null instance since it is possible that the bucket did not have
			// versioning before
			size_t suffix_location = strlen(path) - strlen(LATEST_INSTANCE_SUFFIX);
			path[suffix_location]  = '\0';
			rc = dfs_lookup(ds3b->dfs, path, O_RDWR, &ds3o_tmp->dfs_obj, NULL, NULL);
		}
	}

	if (rc == 0) {
		*ds3o = ds3o_tmp;
	}

	D_FREE(path);
err_ds3o:
	if (rc != 0)
		D_FREE(ds3o_tmp);
	return -rc;
}

int
ds3_obj_close(ds3_obj_t *ds3o)
{
	int rc = dfs_release(ds3o->dfs_obj);
	D_FREE(ds3o);
	return -rc;
}

int
ds3_obj_get_info(struct ds3_object_info *info, ds3_bucket_t *ds3b, ds3_obj_t *ds3o)
{
	return -dfs_getxattr(ds3b->dfs, ds3o->dfs_obj, RGW_DIR_ENTRY_XATTR, info->encoded,
			     &info->encoded_length);
}

int
ds3_obj_set_info(struct ds3_object_info *info, ds3_bucket_t *ds3b, ds3_obj_t *ds3o)
{
	return -dfs_setxattr(ds3b->dfs, ds3o->dfs_obj, RGW_DIR_ENTRY_XATTR, info->encoded,
			     info->encoded_length, 0);
}

int
ds3_obj_read(void *buf, daos_off_t off, daos_size_t *size, ds3_obj_t *ds3o, daos_event_t *ev)
{
	return 0;
}

int
ds3_obj_destroy(const char *key, ds3_bucket_t *ds3b)
{
	int   rc = 0;
	char *path;
	D_STRNDUP(path, key, DS3_MAX_KEY - 1);
	if (path == NULL) {
		return -ENOMEM;
	}

	dfs_obj_t  *parent      = NULL;
	char       *file_start  = strrchr(path, '/');
	const char *file_name   = path;
	const char *parent_path = NULL;
	if (file_start != NULL) {
		*file_start = '\0';
		file_name   = file_start + 1;
		parent_path = path;
	}

	char *lookup_path = NULL;
	if (parent_path != NULL) {
		D_ALLOC_ARRAY(lookup_path, DS3_MAX_KEY);
		if (lookup_path == NULL) {
			rc = ENOMEM;
			goto err_path;
		}

		strcpy(lookup_path, "/");
		strcat(lookup_path, path);
		rc = dfs_lookup(ds3b->dfs, lookup_path, O_RDWR, &parent, NULL, NULL);
		if (rc != 0) {
			goto err_parent;
		}
	}

	rc = dfs_remove(ds3b->dfs, parent, file_name, false, NULL);

err_parent:
	if (parent)
		dfs_release(parent);
	if (lookup_path)
		D_FREE(lookup_path);
err_path:
	D_FREE(path);
	return -rc;
}

int
ds3_obj_write(const void *buf, daos_off_t off, daos_size_t *size, ds3_obj_t *ds3o, daos_event_t *ev)
{
	return 0;
}

int
ds3_obj_mark_latest(const char *key, ds3_bucket_t *ds3b)
{
	if (ds3b == NULL || key == NULL)
		return -EINVAL;

	if (ends_with(key, LATEST_INSTANCE_SUFFIX)) {
		D_ERROR("Creating an object that ends with %s is not allowed.\n",
			LATEST_INSTANCE_SUFFIX);
		return -EINVAL;
	}

	int   rc = 0;
	char *path;
	D_STRNDUP(path, key, DS3_MAX_KEY - 1);
	if (path == NULL) {
		return -ENOMEM;
	}

	dfs_obj_t  *parent      = NULL;
	char       *file_start  = strrchr(path, '/');
	const char *file_name   = path;
	const char *parent_path = NULL;
	if (file_start != NULL) {
		*file_start = '\0';
		file_name   = file_start + 1;
		parent_path = path;
	}

	char *lookup_path = NULL;
	if (parent_path != NULL) {
		D_ALLOC_ARRAY(lookup_path, DS3_MAX_KEY);
		if (lookup_path == NULL) {
			rc = ENOMEM;
			goto err_path;
		}

		strcpy(lookup_path, "/");
		strcat(lookup_path, path);
		rc = dfs_lookup(ds3b->dfs, lookup_path, O_RDWR, &parent, NULL, NULL);
		if (rc != 0) {
			goto err_parent;
		}
	}

	// Build link name
	char *link_name = NULL;
	D_ALLOC_ARRAY(link_name, DS3_MAX_KEY);
	if (link_name == NULL) {
		rc = ENOMEM;
		goto err_parent;
	}

	// Remove instance
	char *suffix_start = strrchr(file_name, '[');
	if (suffix_start != NULL) {
		*suffix_start = '\0';
	}

	strcpy(link_name, file_name);
	strcat(link_name, LATEST_INSTANCE_SUFFIX);

	// Remove previous link
	dfs_remove(ds3b->dfs, parent, link_name, false, NULL);

	// Create the link
	dfs_obj_t *link;
	rc = dfs_open(ds3b->dfs, parent, link_name, DEFFILEMODE | S_IFLNK,
		      O_RDWR | O_CREAT | O_TRUNC, 0, 0, file_name, &link);

	// TODO Update an xattr with a list to all the version ids, ordered by
	// creation to handle deletion

	if (rc != 0)
		dfs_release(link);
	D_FREE(link_name);
err_parent:
	if (parent)
		dfs_release(parent);
	if (lookup_path)
		D_FREE(lookup_path);
err_path:
	D_FREE(path);
	return -rc;
}
