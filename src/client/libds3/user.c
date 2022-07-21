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
		return EINVAL;

	int        rc;

	// Open user file
	dfs_obj_t *user_obj;
	mode_t     mode = DEFFILEMODE;
	rc              = dfs_open(ds3->meta_dfs, ds3->meta_dirs[USERS_DIR], name, S_IFREG | mode,
				   O_RDWR | O_CREAT | O_TRUNC, 0, 0, NULL, &user_obj);
	if (rc != 0) {
		D_ERROR("Failed to open user file, name = %s, rc = %d\n", name, rc);
		return rc;
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
		return rc;
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
	if (strlen(info->email) == 0) {
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
	return rc;
}

int
ds3_user_remove(const char *name, ds3_t *ds3, daos_event_t *ev)
{
	return 0;
}

int
ds3_user_get(const char *name, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev)
{
	return 0;
}

int
ds3_user_get_by_email(const char *email, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev)
{
	return 0;
}

int
ds3_user_get_by_key(const char *key, struct ds3_user_info *info, ds3_t *ds3, daos_event_t *ev)
{
	return 0;
}
