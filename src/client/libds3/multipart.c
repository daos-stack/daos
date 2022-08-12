/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ds3_internal.h"

// Helper struct
struct part_for_sort {
	uint32_t    part_num;
	const char *part_name;
};

// Helper function
static int
compare_part_for_sort(const void *p1, const void *p2)
{
	const struct part_for_sort *ps1 = p1;
	const struct part_for_sort *ps2 = p2;
	return ps1->part_num - ps2->part_num;
}

int
ds3_bucket_list_multipart(const char *bucket_name, uint32_t *nmp,
			  struct ds3_multipart_upload_info *mps, uint32_t *ncp,
			  struct ds3_common_prefix_info *cps, const char *prefix, const char *delim,
			  char *marker, bool *is_truncated, ds3_t *ds3)
{
	if (bucket_name == NULL || ds3 == NULL || nmp == NULL || ncp == NULL)
		return -EINVAL;

	// End
	if (*nmp == 0) {
		if (is_truncated) {
			*is_truncated = false;
		}
		return 0;
	}

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
		daos_size_t size;
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
	if (bucket_name == NULL || upload_id == NULL || ds3 == NULL || npart == NULL)
		return -EINVAL;

	// End
	if (*npart == 0) {
		if (is_truncated) {
			*is_truncated = false;
		}
		return 0;
	}

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

	uint32_t       nr = MULTIPART_MAX_PARTS;
	struct dirent *dirents;
	D_ALLOC_ARRAY(dirents, nr);
	if (dirents == NULL) {
		rc = ENOMEM;
		goto err_upload_dir;
	}

	daos_anchor_t anchor;
	daos_anchor_init(&anchor, 0);

	rc = dfs_readdir(ds3->meta_dfs, upload_dir, &anchor, &nr, dirents);

	if (rc != 0) {
		goto err_dirents;
	}

	// Pick the first *npart after marker
	struct part_for_sort *pfs;
	D_ALLOC_ARRAY(pfs, nr);

	// Fill pfs
	uint32_t pfi = 0;
	for (uint32_t i = 0; i < nr; i++) {
		const char *part_name = dirents[i].d_name;

		// Parse part_name
		errno = 0;
		char          *err;
		const uint32_t part_num = strtol(part_name, &err, 10);
		if (errno || err != part_name + strlen(part_name)) {
			D_WARN("bad part number: %s", part_name);
			continue;
		}

		// Skip entries that are not larger than marker
		if (part_num <= *marker) {
			continue;
		}

		// Add to pfs
		pfs[pfi].part_name = part_name;
		pfs[pfi].part_num  = part_num;
		pfi++;
	}

	// Sort pfs
	qsort(pfs, pfi, sizeof(struct part_for_sort), compare_part_for_sort);

	uint32_t pi       = 0;
	uint32_t last_num = 0;
	for (uint32_t i = 0; i < pfi; i++) {
		uint32_t    part_num  = pfs[i].part_num;
		const char *part_name = pfs[i].part_name;
		last_num              = max(part_num, last_num);

		dfs_obj_t *part_obj;
		rc = dfs_lookup_rel(ds3->meta_dfs, upload_dir, part_name, O_RDWR, &part_obj, NULL,
				    NULL);
		if (rc != 0) {
			goto err_pfs;
		}

		// Read the xattr and add to parts
		rc = dfs_getxattr(ds3->meta_dfs, part_obj, RGW_PART_XATTR, parts[pi].encoded,
				  &parts[pi].encoded_length);
		// Skip if the part has no info
		if (rc != 0) {
			rc = 0;
			dfs_release(part_obj);
			continue;
		}

		parts[pi].part_num = part_num;
		pi++;

		// Close handles
		dfs_release(part_obj);

		// Stop when we get to *npart parts.
		if (pi >= *npart) {
			break;
		}
	}

	// Assign read parts and next marker
	*npart  = pi;
	*marker = last_num;
	if (is_truncated) {
		*is_truncated = pi == pfi;
	}

err_pfs:
	D_FREE(pfs);
err_dirents:
	D_FREE(dirents);
err_upload_dir:
	dfs_release(upload_dir);
err_multipart_dir:
	dfs_release(multipart_dir);
	return -rc;
}

int
ds3_upload_init(struct ds3_multipart_upload_info *info, const char *bucket_name, ds3_t *ds3)
{
	if (bucket_name == NULL || ds3 == NULL)
		return -EINVAL;

	int        rc;
	dfs_obj_t *multipart_dir;
	rc = dfs_lookup_rel(ds3->meta_dfs, ds3->meta_dirs[MULTIPART_DIR], bucket_name, O_RDWR,
			    &multipart_dir, NULL, NULL);
	if (rc != 0)
		return -rc;

	rc = dfs_mkdir(ds3->meta_dfs, multipart_dir, info->upload_id, DEFFILEMODE, 0);

	if (rc != 0)
		goto err_multipart_dir;

	// Insert an entry into bucket multipart index
	dfs_obj_t *upload_dir;
	rc = dfs_lookup_rel(ds3->meta_dfs, multipart_dir, info->upload_id, O_RDWR, &upload_dir,
			    NULL, NULL);
	if (rc != 0)
		goto err_multipart_dir;

	rc = dfs_setxattr(ds3->meta_dfs, upload_dir, RGW_DIR_ENTRY_XATTR, info->encoded,
			  info->encoded_length, 0);
	if (rc != 0)
		goto err_upload_dir;

	// Set key
	rc =
	    dfs_setxattr(ds3->meta_dfs, upload_dir, RGW_KEY_XATTR, info->key, strlen(info->key), 0);

err_upload_dir:
	dfs_release(upload_dir);
err_multipart_dir:
	dfs_release(multipart_dir);
	return -rc;
}

int
ds3_upload_abort(const char *bucket_name, const char *upload_id, ds3_t *ds3)
{
	if (bucket_name == NULL || upload_id == NULL || ds3 == NULL)
		return -EINVAL;

	// Remove upload from bucket multipart index
	int        rc = 0;
	dfs_obj_t *multipart_dir;
	rc = dfs_lookup_rel(ds3->meta_dfs, ds3->meta_dirs[MULTIPART_DIR], bucket_name, O_RDWR,
			    &multipart_dir, NULL, NULL);

	if (rc != 0)
		return -rc;

	rc = dfs_remove(ds3->meta_dfs, multipart_dir, upload_id, true, NULL);
	dfs_release(multipart_dir);
	return -rc;
}

int
ds3_upload_complete()
{
	return 0;
}

int
ds3_upload_get_info(struct ds3_multipart_upload_info *info, const char *bucket_name,
		    const char *upload_id, ds3_t *ds3)
{
	if (info == NULL || bucket_name == NULL || upload_id == NULL || ds3 == NULL)
		return -EINVAL;

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

	rc = dfs_getxattr(ds3->meta_dfs, upload_dir, RGW_DIR_ENTRY_XATTR, info->encoded,
			  &info->encoded_length);
	if (rc != 0)
		goto err_upload_dir;

	// Set key
	daos_size_t size;
	rc = dfs_getxattr(ds3->meta_dfs, upload_dir, RGW_KEY_XATTR, info->key, &size);

err_upload_dir:
	dfs_release(upload_dir);
err_multipart_dir:
	dfs_release(multipart_dir);
	return -rc;
}

int
ds3_upload_create_part(const char *bucket_name, const char *upload_id, uint32_t part_num,
		       ds3_part_t **ds3p, ds3_t *ds3)
{
	if (ds3p == NULL || bucket_name == NULL || upload_id == NULL || ds3 == NULL)
		return -EINVAL;

	int         rc = 0;
	ds3_part_t *ds3p_tmp;
	D_ALLOC_PTR(ds3p_tmp);
	if (ds3p_tmp == NULL)
		return -ENOMEM;

	dfs_obj_t *multipart_dir;
	dfs_obj_t *upload_dir;
	rc = dfs_lookup_rel(ds3->meta_dfs, ds3->meta_dirs[MULTIPART_DIR], bucket_name, O_RDWR,
			    &multipart_dir, NULL, NULL);

	if (rc != 0)
		goto err_ds3p;

	rc = dfs_lookup_rel(ds3->meta_dfs, multipart_dir, upload_id, O_RDWR, &upload_dir, NULL,
			    NULL);

	if (rc != 0) {
		goto err_multipart_dir;
	}

	char part_name_str[7];
	sprintf(part_name_str, "%06u", part_num);

	rc = dfs_open(ds3->meta_dfs, upload_dir, part_name_str, DEFFILEMODE | S_IFREG,
		      O_RDWR | O_CREAT | O_TRUNC, 0, 0, NULL, &ds3p_tmp->dfs_obj);

	if (rc == 0)
		*ds3p = ds3p_tmp;

	dfs_release(upload_dir);
err_multipart_dir:
	dfs_release(multipart_dir);
err_ds3p:
	if (rc != 0)
		D_FREE(ds3p_tmp);
	return -rc;
}

int
ds3_upload_close_part(ds3_part_t *ds3p)
{
	if (ds3p == NULL)
		return -EINVAL;

	int rc = dfs_release(ds3p->dfs_obj);
	D_FREE(ds3p);
	return -rc;
}

int
ds3_upload_write_part(void *buf, daos_off_t off, daos_size_t *size, ds3_part_t *ds3p, ds3_t *ds3,
	      daos_event_t *ev)
{
	if (ds3p == NULL || buf == NULL || ds3 == NULL)
		return -EINVAL;

	d_sg_list_t wsgl;
	d_iov_t     iov;
	d_iov_set(&iov, buf, *size);
	wsgl.sg_nr   = 1;
	wsgl.sg_iovs = &iov;
	return -dfs_write(ds3->meta_dfs, ds3p->dfs_obj, &wsgl, off, ev);
}

int
ds3_upload_read_part(void *buf, daos_off_t off, daos_size_t *size, ds3_part_t *ds3p, ds3_t *ds3,
	      daos_event_t *ev)
{
	if (ds3p == NULL || buf == NULL || ds3 == NULL)
		return -EINVAL;

	d_iov_t iov;
	d_iov_set(&iov, buf, *size);

	d_sg_list_t rsgl;
	rsgl.sg_nr     = 1;
	rsgl.sg_iovs   = &iov;
	rsgl.sg_nr_out = 1;
	return -dfs_read(ds3->meta_dfs, ds3p->dfs_obj, &rsgl, off, size, ev);
}
