/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ds3_internal.h"

int
ds3_bucket_list_multipart(const char *bucket_name, uint32_t *nmp,
			  struct ds3_multipart_upload_info *mps, uint32_t *ncp,
			  struct ds3_common_prefix_info *cps, const char *prefix, const char *delim,
			  char *marker, bool *is_truncated, ds3_t *ds3)
{
	if (ds3 == NULL || nmp == NULL)
		return -EINVAL;

	// End
	if (*nmp == 0)
		return 0;

	int        rc = 0;
	dfs_obj_t *multipart_dir;
	rc = dfs_lookup_rel(ds3->meta_dfs, ds3->meta_dirs[MULTIPART_DIR], bucket_name, O_RDWR,
			    &multipart_dir, NULL, NULL);
	if (rc != 0) {
		return -rc;
	}

	struct dirent *dirents;
	D_ALLOC_ARRAY(dirents, *nmp);
	if (dirents == NULL) {
		rc = ENOMEM;
		goto err_dir;
	}

	// TODO handle more than max
	// TODO handle ordering
	// TODO handle marker
	daos_anchor_t anchor;
	daos_anchor_init(&anchor, 0);

	rc = dfs_readdir(ds3->meta_dfs, multipart_dir, &anchor, nmp, dirents);
	if (rc != 0) {
		goto err_dirents;
	}

	if (is_truncated != NULL) {
		*is_truncated = !daos_anchor_is_eof(&anchor);
	}

	char *key;
	D_ALLOC_ARRAY(key, DS3_MAX_KEY);
	if (key == NULL) {
		rc = ENOMEM;
		goto err_dirents;
	}

	uint32_t mpi           = 0;
	uint32_t cpi           = 0;
	int      prefix_length = strlen(prefix);
	for (uint32_t i = 0; i < *nmp; i++) {
		const char *upload_id = dirents[i].d_name;

		// Open upload dir
		dfs_obj_t  *upload_dir;
		rc = dfs_lookup_rel(ds3->meta_dfs, multipart_dir, upload_id, O_RDWR, &upload_dir,
				    NULL, NULL);
		if (rc != 0) {
			goto err_key;
		}

		// Read the key xattr
		size_t size;
		rc = dfs_getxattr(ds3->meta_dfs, upload_dir, RGW_KEY_XATTR, mps[i].key, &size);

		// Skip if file has no saved key
		if (rc != 0) {
			D_WARN("No key xattr, skipping upload_id= %s\n", upload_id);
			dfs_release(upload_dir);
			continue;
		}

		// Only add entries that start with prefix
		if (strncmp(key, prefix, prefix_length) == 0) {
			// if it has delim after prefix, add to common prefixes, otherwise add to
			// multipart uploads
			char *delim_pos = strstr(key + prefix_length, delim);
			if (delim_pos != NULL) {
				// Add to cps
				// Out of bounds
				if (cpi >= *ncp) {
					rc = EINVAL;
					dfs_release(upload_dir);
					goto err_key;
				}

				strncpy(cps[cpi].prefix, key, delim_pos - key);
				cpi++;
			} else {
				// Read dirent
				rc = dfs_getxattr(ds3->meta_dfs, upload_dir, RGW_DIR_ENTRY_XATTR,
						  mps[mpi].encoded, &mps[mpi].encoded_length);

				// Skip if upload has no dirent
				if (rc != 0) {
					D_WARN("No dirent, skipping upload_id= %s\n", upload_id);
					dfs_release(upload_dir);
					continue;
				}

				// Add to mps
				strcpy(mps[mpi].upload_id, upload_id);
				strcpy(mps[mpi].key, key);
				mpi++;
			}
		}

		// Close handle
		dfs_release(upload_dir);
	}

	// Set the number of read uploads
	*nmp = mpi;
	*ncp = cpi;

	// TODO Sort uploads
err_key:
	D_FREE(key);
err_dirents:
	D_FREE(dirents);
err_dir:
	dfs_release(multipart_dir);
	return -rc;
}

int
ds3_upload_list_parts(const char *bucket_name, const char *upload_id, uint32_t *npart,
		      struct ds3_multipart_part_info *parts, uint32_t *marker, bool *is_truncated,
		      ds3_t *ds3)
{
	int        rc = 0;
	dfs_obj_t *multipart_dir;
	dfs_obj_t *upload_dir;

	rc = dfs_lookup_rel(ds3->meta_dfs, ds3->meta_dirs[MULTIPART_DIR], bucket_name, O_RDWR,
			    &multipart_dir, NULL, NULL);
	if (rc != 0)
		return -rc;

	rc = dfs_lookup_rel(ds3->meta_dfs, multipart_dir, upload_id, O_RDWR, &upload_dir, NULL,
			    NULL);

	if (rc != 0) {
		goto err_multipart_dir;
	}

	struct dirent *dirents;
	D_ALLOC_ARRAY(dirents, *npart);
	if (dirents == NULL) {
		rc = ENOMEM;
		goto err_upload_dir;
	}

	daos_anchor_t anchor;
	daos_anchor_init(&anchor, 0);

	rc = dfs_readdir(ds3->meta_dfs, upload_dir, &anchor, npart, dirents);

	if (rc != 0) {
		goto err_dirents;
	}

	if (is_truncated != NULL) {
		*is_truncated = !daos_anchor_is_eof(&anchor);
	}

	uint32_t pi = 0;
	uint32_t last_num = 0;
	for (uint32_t i = 0; i < *npart; i++) {
		const char *part_name = dirents[i].d_name;

		// Parse part_name
		errno = 0;
		char          *err;
		const uint32_t part_num = strtol(part_name, &err, 10);
		if (errno || err != part_name + strlen(part_name)) {
			D_ERROR("bad part number: %s", part_name);
			rc = EINVAL;
			goto err_dirents;
		}

		// Skip entries that are not larger than marker
		if (part_num <= *marker) {
			continue;
		}

		last_num = max(part_num, last_num);

		dfs_obj_t *part_obj;
		rc = dfs_lookup_rel(ds3->meta_dfs, upload_dir, part_name, O_RDWR, &part_obj, NULL,
				    NULL);
		if (rc != 0) {
			goto err_dirents;
		}

		// The entry is a regular file, read the xattr and add to parts
		rc = dfs_getxattr(ds3->meta_dfs, part_obj, RGW_PART_XATTR, parts[pi].encoded,
				  &parts[pi].encoded_length);
		// Skip if the part has no info
		if (rc != 0) {
			rc = dfs_release(part_obj);
			continue;
		}

		parts[pi].part_num = part_num;
		pi++;

		// Close handles
		dfs_release(part_obj);
	}

	*npart = pi;
	// TODO since dirents are not ordered, would this skip certain parts?
	*marker = last_num;

err_dirents:
	D_FREE(dirents);
err_upload_dir:
	dfs_release(upload_dir);
err_multipart_dir:
	dfs_release(multipart_dir);
	return -rc;
}

int
ds3_upload_init()
{
	return 0;
}

int
ds3_upload_abort()
{
	return 0;
}

int
ds3_upload_complete()
{
	return 0;
}

int
ds3_upload_get_info()
{
	return 0;
}

int
ds3_upload_open_part()
{
	return 0;
}

int
ds3_upload_close_part()
{
	return 0;
}

int
ds3_upload_write_part()
{
	return 0;
}
