/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ds3_internal.h"

int
ds3_user_set(const char *name, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev)
{
	if (ds3 == NULL || name == NULL || info == NULL)
		return -EINVAL;

	// Remove old user data
	ds3_user_remove(name, info, ds3, ev);

	// Open user file
	int        rc;
	dfs_obj_t *user_obj;
	mode_t     mode = DEFFILEMODE;
	rc              = dfs_open(ds3->meta_dfs, ds3->meta_dirs[USERS_DIR], name, S_IFREG | mode,
				   O_RDWR | O_CREAT | O_TRUNC, 0, 0, NULL, &user_obj);
	if (rc != 0) {
		D_ERROR("Failed to open user file, name = %s, rc = %d\n", name, rc);
		goto err_ret;
	}

	// Write user data
	d_sg_list_t wsgl;
	d_iov_t     iov;
	d_iov_set(&iov, info->encoded, info->encoded_length);
	wsgl.sg_nr   = 1;
	wsgl.sg_iovs = &iov;
	rc           = dfs_write(ds3->meta_dfs, user_obj, &wsgl, 0, ev);
	dfs_release(user_obj);
	if (rc != 0) {
		D_ERROR("Failed to write to user file, name = %s, rc = %d\n", name, rc);
		goto err_ret;
	}

	// Build user path
	char *user_path;
	D_ALLOC_ARRAY(user_path, DS3_MAX_KEY);
	strcpy(user_path, "../");
	strcat(user_path, meta_dir_name(USERS_DIR));
	strcat(user_path, "/");
	strcat(user_path, name);

	// Store access key in access key index
	for (int i = 0; i < info->access_ids_nr; i++) {
		rc = dfs_open(ds3->meta_dfs, ds3->meta_dirs[ACCESS_KEYS_DIR], info->access_ids[i],
			      S_IFLNK | mode, O_RDWR | O_CREAT | O_TRUNC, 0, 0, user_path,
			      &user_obj);
		if (rc != 0) {
			D_ERROR("Failed to create symlink, name = %s, rc = %d\n",
				info->access_ids[i], rc);
			goto err;
		}
		dfs_release(user_obj);
	}

	// Store email in email index
	if (strlen(info->email) != 0) {
		rc =
		    dfs_open(ds3->meta_dfs, ds3->meta_dirs[EMAILS_DIR], info->email, S_IFLNK | mode,
			     O_RDWR | O_CREAT | O_TRUNC, 0, 0, user_path, &user_obj);
		if (rc != 0) {
			D_ERROR("Failed to create symlink, name = %s, rc = %d\n", info->email, rc);
			goto err;
		}
		dfs_release(user_obj);
	}

err:
	D_FREE(user_path);
err_ret:
	return -rc;
}

int
ds3_user_remove(const char *name, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev)
{
	if (ds3 == NULL || name == NULL || info == NULL)
		return -EINVAL;

	int rc = 0;

	// Remove access keys
	for (int i = 0; i < info->access_ids_nr; i++) {
		if (dfs_access(ds3->meta_dfs, ds3->meta_dirs[ACCESS_KEYS_DIR], info->access_ids[i],
			       W_OK) == 0) {
			rc = dfs_remove(ds3->meta_dfs, ds3->meta_dirs[ACCESS_KEYS_DIR],
					info->access_ids[i], false, NULL);
			if (rc != 0) {
				D_ERROR("Failed to remove symlink, name = %s, rc = %d\n",
					info->access_ids[i], rc);
				return -rc;
			}
		}
	}

	// Remove email if it exists
	if (strlen(info->email) != 0) {
		if (dfs_access(ds3->meta_dfs, ds3->meta_dirs[EMAILS_DIR], info->email, W_OK) == 0) {
			rc = dfs_remove(ds3->meta_dfs, ds3->meta_dirs[EMAILS_DIR], info->email,
					false, NULL);
			if (rc != 0) {
				D_ERROR("Failed to remove symlink, name = %s, rc = %d\n",
					info->email, rc);
				return -rc;
			}
		}
	}

	// Remove the user object
	if (dfs_access(ds3->meta_dfs, ds3->meta_dirs[USERS_DIR], name, W_OK) == 0) {
		rc = dfs_remove(ds3->meta_dfs, ds3->meta_dirs[USERS_DIR], name, false, NULL);
		if (rc != 0) {
			D_ERROR("Failed to remove user file, name = %s, rc = %d\n", name, rc);
			return -rc;
		}
	}

	return 0;
}

/**
 * Helper function to read user by metadir
 */
static int
ds3_read_user(const char *name, enum meta_dir by, struct ds3_user_info *info, ds3_t *ds3,
	      daos_event_t *ev)
{
	if (ds3 == NULL || name == NULL || info == NULL)
		return -EINVAL;

	// Open file
	int        rc;
	dfs_obj_t *user_obj;
	rc = dfs_lookup_rel(ds3->meta_dfs, ds3->meta_dirs[by], name, O_RDWR, &user_obj, NULL, NULL);
	if (rc != 0) {
		return -ENOENT;
	}

	// Reserve buffers
	d_iov_t     iov;
	d_sg_list_t rsgl;
	d_iov_set(&iov, info->encoded, info->encoded_length);
	rsgl.sg_nr     = 1;
	rsgl.sg_iovs   = &iov;
	rsgl.sg_nr_out = 1;

	// Read file
	rc = dfs_read(ds3->meta_dfs, user_obj, &rsgl, 0, &info->encoded_length, ev);
	if (rc != 0) {
		D_ERROR("Failed to read user file, name = %s, rc = %d\n", name, rc);
	}

	// Close file
	dfs_release(user_obj);
	return -rc;
}

int
ds3_user_get(const char *name, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev)
{
	return ds3_read_user(name, USERS_DIR, info, ds3, ev);
}

int
ds3_user_get_by_email(const char *email, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev)
{
	return ds3_read_user(email, EMAILS_DIR, info, ds3, ev);
}

int
ds3_user_get_by_key(const char *key, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev)
{
	return ds3_read_user(key, ACCESS_KEYS_DIR, info, ds3, ev);
}
