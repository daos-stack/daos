/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ds3_internal.h"

typedef struct ds3_obj_args {
	d_iov_t	iov;
	d_sg_list_t sg;
} ds3_obj_args_t;

/* helper */
static bool
ends_with(const char *str, const char *suffix)
{
	size_t lenstr;
	size_t lensuffix;

	if (!str || !suffix)
		return false;

	lenstr    = strlen(str);
	lensuffix = strlen(suffix);
	if (lensuffix > lenstr)
		return false;

	return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

int
ds3_obj_create(const char *key, ds3_obj_t **ds3o, ds3_bucket_t *ds3b)
{
	int        rc  = 0;
	int        rc2 = 0;
	ds3_obj_t *ds3o_tmp;
	char      *path;
	dfs_obj_t *parent = NULL;
	char      *file_start;
	char      *file_name;
	char      *parent_path = NULL;
	mode_t     mode        = DEFFILEMODE;
	dfs_obj_t *dir_obj;
	char      *sptr = NULL;
	char      *dir;

	if (ds3b == NULL || ds3o == NULL)
		return -EINVAL;
	if (key == NULL || strnlen(key, DS3_MAX_KEY) > DS3_MAX_KEY - 1)
		return -EINVAL;

	if (ends_with(key, LATEST_INSTANCE_SUFFIX)) {
		D_ERROR("Creating an object that ends with %s is not allowed.\n",
			LATEST_INSTANCE_SUFFIX);
		return -EINVAL;
	}

	/* TODO: cache open file handles */
	D_ALLOC_PTR(ds3o_tmp);
	if (ds3o_tmp == NULL)
		return -ENOMEM;

	D_STRNDUP(path, key, DS3_MAX_KEY_BUFF - 1);
	if (path == NULL) {
		rc = ENOMEM;
		goto err_ds3o;
	}

	file_start = strrchr(path, '/');
	file_name  = path;
	if (file_start != NULL) {
		*file_start = '\0';
		file_name   = file_start + 1;
		parent_path = path;
	}

	if (parent_path != NULL) {
		/* Recursively open parent directories */

		for (dir = strtok_r(parent_path, "/", &sptr); dir != NULL;
		     dir = strtok_r(NULL, "/", &sptr)) {
			/* Create directory */
			rc = dfs_mkdir(ds3b->dfs, parent, dir, mode, 0);

			if (rc != 0 && rc != EEXIST)
				goto err_parent;

			/* Open directory */
			rc = dfs_lookup_rel(ds3b->dfs, parent, dir, O_RDWR, &dir_obj, NULL, NULL);
			if (rc != 0)
				goto err_parent;

			/* Next parent */
			if (parent) {
				rc = dfs_release(parent);
				if (rc != 0)
					goto err_parent;
			}
			parent = dir_obj;
		}
	}

	/* Finally create the file */
	rc = dfs_open(ds3b->dfs, parent, file_name, mode | S_IFREG, O_RDWR | O_CREAT | O_TRUNC, 0,
		      0, NULL, &ds3o_tmp->dfs_obj);
	if (rc == 0)
		*ds3o = ds3o_tmp;

err_parent:
	if (parent)
		rc2 = dfs_release(parent);
	rc = rc == 0 ? rc2 : rc;
	D_FREE(path);
err_ds3o:
	if (rc != 0)
		D_FREE(ds3o_tmp);
	return -rc;
}

int
ds3_obj_open(const char *key, ds3_obj_t **ds3o, ds3_bucket_t *ds3b)
{
	int        rc = 0;
	ds3_obj_t *ds3o_tmp;
	char      *path;
	size_t     suffix_location;

	if (ds3b == NULL || ds3o == NULL)
		return -EINVAL;
	if (key == NULL || strnlen(key, DS3_MAX_KEY) > DS3_MAX_KEY - 1)
		return -EINVAL;

	D_ALLOC_PTR(ds3o_tmp);
	if (ds3o_tmp == NULL)
		return -ENOMEM;

	/* TODO: cache open file handles */
	D_ALLOC_ARRAY(path, DS3_MAX_KEY_BUFF);
	if (path == NULL) {
		rc = ENOMEM;
		goto err_ds3o;
	}

	if (key[0] == '/')
		strcpy(path, "");
	else
		strcpy(path, "/");
	strcat(path, key);

	rc = dfs_lookup(ds3b->dfs, path, O_RDWR, &ds3o_tmp->dfs_obj, NULL, NULL);
	if (rc == ENOENT) {
		if (ends_with(path, LATEST_INSTANCE_SUFFIX)) {
			/**
			 * If we are trying to access the latest version, try accessing key with
			 * null instance since it is possible that the bucket did not have
			 * versioning before
			 */
			suffix_location       = strlen(path) - strlen(LATEST_INSTANCE_SUFFIX);
			path[suffix_location] = '\0';
			rc = dfs_lookup(ds3b->dfs, path, O_RDWR, &ds3o_tmp->dfs_obj, NULL, NULL);
		}
	}
	if (rc == 0)
		*ds3o = ds3o_tmp;

	D_FREE(path);
err_ds3o:
	if (rc != 0)
		D_FREE(ds3o_tmp);
	return -rc;
}

int
ds3_obj_close(ds3_obj_t *ds3o)
{
	int rc = 0;

	if (ds3o == NULL)
		return -EINVAL;

	rc = dfs_release(ds3o->dfs_obj);
	D_FREE(ds3o);
	return -rc;
}

int
ds3_obj_get_info(struct ds3_object_info *info, ds3_bucket_t *ds3b, ds3_obj_t *ds3o)
{
	if (ds3b == NULL || info == NULL || ds3o == NULL)
		return -EINVAL;

	return -dfs_getxattr(ds3b->dfs, ds3o->dfs_obj, RGW_DIR_ENTRY_XATTR, info->encoded,
			     &info->encoded_length);
}

int
ds3_obj_set_info(struct ds3_object_info *info, ds3_bucket_t *ds3b, ds3_obj_t *ds3o)
{
	if (ds3b == NULL || info == NULL || ds3o == NULL)
		return -EINVAL;

	return -dfs_setxattr(ds3b->dfs, ds3o->dfs_obj, RGW_DIR_ENTRY_XATTR, info->encoded,
			     info->encoded_length, 0);
}

static int
ds3_obj_int_cb(void *args, daos_event_t *ev, int ret)
{
	D_FREE(args);
	return 0;
}

static int
ds3_obj_read_int(void *buf, daos_off_t off, daos_size_t *size, ds3_bucket_t *ds3b, ds3_obj_t *ds3o,
	     daos_event_t *ev)
{
	ds3_obj_args_t	*args;

	D_ALLOC_PTR(args);
	if (args == NULL)
		return -DER_NOMEM;

	d_iov_set(&args->iov, buf, *size);
	args->sg.sg_nr     = 1;
	args->sg.sg_iovs   = &args->iov;
	args->sg.sg_nr_out = 1;

	daos_event_register_comp_cb(ev, ds3_obj_int_cb, args);
	return -dfs_read(ds3b->dfs, ds3o->dfs_obj, &args->sg, off, size, ev);
}

int
ds3_obj_read(void *buf, daos_off_t off, daos_size_t *size, ds3_bucket_t *ds3b, ds3_obj_t *ds3o,
	     daos_event_t *ev)
{
	if (ds3b == NULL || buf == NULL || ds3o == NULL)
		return -EINVAL;

	if (ev == NULL) {
		d_iov_t     iov;
		d_sg_list_t rsgl;

		d_iov_set(&iov, buf, *size);
		rsgl.sg_nr     = 1;
		rsgl.sg_iovs   = &iov;
		rsgl.sg_nr_out = 1;
		return -dfs_read(ds3b->dfs, ds3o->dfs_obj, &rsgl, off, size, ev);
	}

	return ds3_obj_read_int(buf, off, size, ds3b, ds3o, ev);
}

int
ds3_obj_destroy(const char *key, ds3_bucket_t *ds3b)
{
	int         rc  = 0;
	int         rc2 = 0;
	char       *path;
	dfs_obj_t  *parent = NULL;
	char       *file_start;
	const char *file_name;
	const char *parent_path = NULL;
	char       *lookup_path = NULL;

	if (ds3b == NULL)
		return -EINVAL;
	if (key == NULL || strnlen(key, DS3_MAX_KEY) > DS3_MAX_KEY - 1)
		return -EINVAL;

	D_STRNDUP(path, key, DS3_MAX_KEY_BUFF - 1);
	if (path == NULL)
		return -ENOMEM;

	file_start = strrchr(path, '/');
	file_name  = path;
	if (file_start != NULL) {
		*file_start = '\0';
		file_name   = file_start + 1;
		parent_path = path;
	}

	if (parent_path != NULL) {
		D_ALLOC_ARRAY(lookup_path, DS3_MAX_KEY_BUFF);
		if (lookup_path == NULL) {
			rc = ENOMEM;
			goto err_path;
		}

		strcpy(lookup_path, "/");
		strcat(lookup_path, path);
		rc = dfs_lookup(ds3b->dfs, lookup_path, O_RDWR, &parent, NULL, NULL);
		if (rc != 0)
			goto err_parent;
	}

	rc = dfs_remove(ds3b->dfs, parent, file_name, false, NULL);

err_parent:
	if (parent)
		rc2 = dfs_release(parent);
	rc = rc == 0 ? rc2 : rc;
	if (lookup_path)
		D_FREE(lookup_path);
err_path:
	D_FREE(path);
	return -rc;
}

static int
ds3_obj_write_int(void *buf, daos_off_t off, daos_size_t *size, ds3_bucket_t *ds3b, ds3_obj_t *ds3o,
		  daos_event_t *ev)
{
	ds3_obj_args_t	*args;

	D_ALLOC_PTR(args);
	if (args == NULL)
		return -DER_NOMEM;

	d_iov_set(&args->iov, buf, *size);
	args->sg.sg_nr     = 1;
	args->sg.sg_iovs   = &args->iov;
	args->sg.sg_nr_out = 1;

	daos_event_register_comp_cb(ev, ds3_obj_int_cb, args);
	return -dfs_write(ds3b->dfs, ds3o->dfs_obj, &args->sg, off, ev);
}

int
ds3_obj_write(void *buf, daos_off_t off, daos_size_t *size, ds3_bucket_t *ds3b, ds3_obj_t *ds3o,
	      daos_event_t *ev)
{
	if (ds3b == NULL || buf == NULL || ds3o == NULL)
		return -EINVAL;

	if (ev == NULL) {
		d_iov_t     iov;
		d_sg_list_t wsgl;

		d_iov_set(&iov, buf, *size);
		wsgl.sg_nr   = 1;
		wsgl.sg_iovs = &iov;
		return -dfs_write(ds3b->dfs, ds3o->dfs_obj, &wsgl, off, ev);
	}

	return ds3_obj_write_int(buf, off, size, ds3b, ds3o, ev);
}

int
ds3_obj_mark_latest(const char *key, ds3_bucket_t *ds3b)
{
	int         rc  = 0;
	int         rc2 = 0;
	char       *path;
	dfs_obj_t  *parent = NULL;
	char       *file_start;
	const char *file_name;
	const char *parent_path = NULL;
	char       *link_name   = NULL;
	char       *lookup_path = NULL;
	int         name_length;
	char       *suffix_start;
	dfs_obj_t  *link;

	if (ds3b == NULL)
		return -EINVAL;
	if (key == NULL || strnlen(key, DS3_MAX_KEY) > DS3_MAX_KEY - 1)
		return -EINVAL;

	if (ends_with(key, LATEST_INSTANCE_SUFFIX)) {
		D_ERROR("Creating an object that ends with %s is not allowed.\n",
			LATEST_INSTANCE_SUFFIX);
		return -EINVAL;
	}

	D_STRNDUP(path, key, DS3_MAX_KEY_BUFF - 1);
	if (path == NULL)
		return -ENOMEM;

	file_start = strrchr(path, '/');
	file_name  = path;
	if (file_start != NULL) {
		*file_start = '\0';
		file_name   = file_start + 1;
		parent_path = path;
	}

	if (parent_path != NULL) {
		D_ALLOC_ARRAY(lookup_path, DS3_MAX_KEY_BUFF);
		if (lookup_path == NULL) {
			rc = ENOMEM;
			goto err_path;
		}

		strcpy(lookup_path, "/");
		strcat(lookup_path, path);
		rc = dfs_lookup(ds3b->dfs, lookup_path, O_RDWR, &parent, NULL, NULL);
		if (rc != 0)
			goto err_parent;
	}

	/* Build link name */
	D_ALLOC_ARRAY(link_name, DS3_MAX_KEY_BUFF);
	if (link_name == NULL) {
		rc = ENOMEM;
		goto err_parent;
	}

	/* Copy name without instance */
	name_length  = strlen(file_name);
	suffix_start = strrchr(file_name, '[');
	if (suffix_start != NULL)
		name_length = suffix_start - file_name;

	strncpy(link_name, file_name, name_length);
	strcat(link_name, LATEST_INSTANCE_SUFFIX);

	/* Remove previous link */
	rc = dfs_remove(ds3b->dfs, parent, link_name, false, NULL);
	if (rc != 0 && rc != ENOENT)
		goto err_link_name;

	/* Create the link */
	rc = dfs_open(ds3b->dfs, parent, link_name, DEFFILEMODE | S_IFLNK, O_RDWR | O_CREAT, 0, 0,
		      file_name, &link);

	/**
	 * TODO Update an xattr with a list to all the version ids, ordered by
	 * creation to handle deletion
	 */
	rc2 = dfs_release(link);
	rc  = rc == 0 ? rc2 : rc;

err_link_name:
	D_FREE(link_name);
err_parent:
	if (parent)
		rc2 = dfs_release(parent);
	rc = rc == 0 ? rc2 : rc;
	if (lookup_path)
		D_FREE(lookup_path);
err_path:
	D_FREE(path);
	return -rc;
}
