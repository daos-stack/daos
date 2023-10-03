/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "ds3_internal.h"

/** Helper struct */
struct part_for_sort {
	uint32_t    part_num;
	const char *part_name;
};

/** Helper function */
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
	int            rc  = 0;
	int            rc2 = 0;
	dfs_obj_t     *multipart_dir;
	struct dirent *dirents;
	daos_anchor_t  anchor;
	char          *key;
	uint32_t       mpi;
	uint32_t       cpi;
	uint32_t       i;
	int            prefix_length;
	const char    *upload_id;
	dfs_obj_t     *upload_dir;
	daos_size_t    size;
	char          *delim_pos;

	if (bucket_name == NULL || ds3 == NULL || nmp == NULL || ncp == NULL)
		return -EINVAL;
	if (prefix != NULL && strnlen(prefix, DS3_MAX_KEY) > DS3_MAX_KEY - 1)
		return -EINVAL;
	if (delim != NULL && strnlen(delim, DS3_MAX_KEY) > DS3_MAX_KEY - 1)
		return -EINVAL;

	/* End */
	if (*nmp == 0) {
		if (is_truncated)
			*is_truncated = false;
		return 0;
	}

	rc = dfs_lookup_rel(ds3->meta_dfs, ds3->meta_dirs[MULTIPART_DIR], bucket_name, O_RDWR,
			    &multipart_dir, NULL, NULL);
	if (rc != 0)
		return -rc;

	D_ALLOC_ARRAY(dirents, *nmp);
	if (dirents == NULL) {
		rc = ENOMEM;
		goto err_dir;
	}

	/* TODO handle more than max */
	/* TODO handle ordering */
	/* TODO handle marker */
	daos_anchor_init(&anchor, 0);

	rc = dfs_readdir(ds3->meta_dfs, multipart_dir, &anchor, nmp, dirents);
	if (rc != 0)
		goto err_dirents;

	if (is_truncated != NULL)
		*is_truncated = !daos_anchor_is_eof(&anchor);

	D_ALLOC_ARRAY(key, DS3_MAX_KEY_BUFF);
	if (key == NULL) {
		rc = ENOMEM;
		goto err_dirents;
	}

	mpi           = 0;
	cpi           = 0;
	prefix_length = strlen(prefix);
	for (i = 0; i < *nmp; i++) {
		upload_id = dirents[i].d_name;

		/* Open upload dir */
		rc = dfs_lookup_rel(ds3->meta_dfs, multipart_dir, upload_id, O_RDWR, &upload_dir,
				    NULL, NULL);
		if (rc != 0)
			goto err_key;

		/* Read the key xattr */
		/* Skip if file has no saved key */
		rc = dfs_getxattr(ds3->meta_dfs, upload_dir, RGW_KEY_XATTR, key, &size);
		if (rc != 0) {
			D_DEBUG(DB_ALL, "No key xattr, skipping upload_id= %s\n", upload_id);
			rc = dfs_release(upload_dir);
			if (rc != 0)
				goto err_key;
			continue;
		}

		/* Only add entries that start with prefix */
		if (strncmp(key, prefix, prefix_length) == 0) {
			/*
			 * If it has delim after prefix, add to common prefixes, otherwise add to
			 * multipart uploads
			 */
			delim_pos = NULL;
			if (strlen(delim) != 0)
				delim_pos = strstr(key + prefix_length, delim);

			if (delim_pos != NULL) {
				/* Add to cps */
				/* Out of bounds */
				if (cpi >= *ncp) {
					rc = EINVAL;
					dfs_release(upload_dir);
					goto err_key;
				}

				strncpy(cps[cpi].prefix, key, delim_pos - key);
				cpi++;
			} else {
				/* Read dirent */
				/* Skip if upload has no dirent */
				rc = dfs_getxattr(ds3->meta_dfs, upload_dir, RGW_DIR_ENTRY_XATTR,
						  mps[mpi].encoded, &mps[mpi].encoded_length);
				if (rc != 0) {
					D_DEBUG(DB_ALL, "No dirent, skipping upload_id= %s\n",
						upload_id);
					rc = dfs_release(upload_dir);
					if (rc != 0)
						goto err_key;
					continue;
				}

				/* Add to mps */
				strcpy(mps[mpi].upload_id, upload_id);
				strcpy(mps[mpi].key, key);
				mpi++;
			}
		}

		/* Close handle */
		rc = dfs_release(upload_dir);
		if (rc != 0)
			goto err_key;
	}

	/* Set the number of read uploads */
	*nmp = mpi;
	*ncp = cpi;

	/* TODO Sort uploads */
err_key:
	D_FREE(key);
err_dirents:
	D_FREE(dirents);
err_dir:
	rc2 = dfs_release(multipart_dir);
	rc  = rc == 0 ? rc2 : rc;
	return -rc;
}

int
ds3_upload_list_parts(const char *bucket_name, const char *upload_id, uint32_t *npart,
		      struct ds3_multipart_part_info *parts, uint32_t *marker, bool *is_truncated,
		      ds3_t *ds3)
{
	int                   rc  = 0;
	int                   rc2 = 0;
	dfs_obj_t            *multipart_dir;
	dfs_obj_t            *upload_dir;
	uint32_t              nr = MULTIPART_MAX_PARTS;
	struct dirent        *dirents;
	daos_anchor_t         anchor;
	struct part_for_sort *pfs;
	uint32_t              pfi = 0;
	uint32_t              i;
	char		 *err;
	uint32_t              part_num;
	const char           *part_name;
	uint32_t              pi       = 0;
	uint32_t              last_num = 0;
	dfs_obj_t            *part_obj;

	if (bucket_name == NULL || upload_id == NULL || ds3 == NULL || npart == NULL)
		return -EINVAL;

	/* End */
	if (*npart == 0) {
		if (is_truncated)
			*is_truncated = false;
		return 0;
	}

	rc = dfs_lookup_rel(ds3->meta_dfs, ds3->meta_dirs[MULTIPART_DIR], bucket_name, O_RDWR,
			    &multipart_dir, NULL, NULL);
	if (rc != 0)
		return -rc;

	rc = dfs_lookup_rel(ds3->meta_dfs, multipart_dir, upload_id, O_RDWR, &upload_dir, NULL,
			    NULL);
	if (rc != 0)
		goto err_multipart_dir;

	D_ALLOC_ARRAY(dirents, nr);
	if (dirents == NULL) {
		rc = ENOMEM;
		goto err_upload_dir;
	}

	daos_anchor_init(&anchor, 0);

	rc = dfs_readdir(ds3->meta_dfs, upload_dir, &anchor, &nr, dirents);
	if (rc != 0)
		goto err_dirents;

	/* Pick the first *npart after marker */
	D_ALLOC_ARRAY(pfs, nr);

	/* Fill pfs */
	for (i = 0; i < nr; i++) {
		part_name = dirents[i].d_name;

		/* Parse part_name */
		errno = 0;

		part_num = strtol(part_name, &err, 10);
		if (errno || err != part_name + strlen(part_name)) {
			D_DEBUG(DB_ALL, "bad part number: %s", part_name);
			continue;
		}

		/* Skip entries that are not larger than marker */
		if (part_num <= *marker)
			continue;

		/* Add to pfs */
		pfs[pfi].part_name = part_name;
		pfs[pfi].part_num  = part_num;
		pfi++;
	}

	/* Sort pfs */
	qsort(pfs, pfi, sizeof(struct part_for_sort), compare_part_for_sort);

	for (i = 0; i < pfi; i++) {
		part_num  = pfs[i].part_num;
		part_name = pfs[i].part_name;
		last_num  = max(part_num, last_num);

		rc = dfs_lookup_rel(ds3->meta_dfs, upload_dir, part_name, O_RDWR, &part_obj, NULL,
				    NULL);
		if (rc != 0)
			goto err_pfs;

		/* Read the xattr and add to parts */
		rc = dfs_getxattr(ds3->meta_dfs, part_obj, RGW_PART_XATTR, parts[pi].encoded,
				  &parts[pi].encoded_length);
		/* Skip if the part has no info */
		if (rc != 0) {
			rc = dfs_release(part_obj);
			if (rc != 0)
				goto err_pfs;
			continue;
		}

		parts[pi].part_num = part_num;
		pi++;

		/* Close handles */
		rc = dfs_release(part_obj);
		if (rc != 0)
			goto err_pfs;

		/* Stop when we get to *npart parts. */
		if (pi >= *npart)
			break;
	}

	/* Assign read parts and next marker */
	*npart  = pi;
	*marker = last_num;
	if (is_truncated)
		*is_truncated = pi < pfi;

err_pfs:
	D_FREE(pfs);
err_dirents:
	D_FREE(dirents);
err_upload_dir:
	rc2 = dfs_release(upload_dir);
	rc  = rc == 0 ? rc2 : rc;
err_multipart_dir:
	rc2 = dfs_release(multipart_dir);
	rc  = rc == 0 ? rc2 : rc;
	return -rc;
}

int
ds3_upload_init(struct ds3_multipart_upload_info *info, const char *bucket_name, ds3_t *ds3)
{
	int        rc  = 0;
	int        rc2 = 0;
	dfs_obj_t *multipart_dir;
	dfs_obj_t *upload_dir;

	if (bucket_name == NULL || ds3 == NULL)
		return -EINVAL;

	rc = dfs_lookup_rel(ds3->meta_dfs, ds3->meta_dirs[MULTIPART_DIR], bucket_name, O_RDWR,
			    &multipart_dir, NULL, NULL);
	if (rc != 0)
		return -rc;

	rc = dfs_mkdir(ds3->meta_dfs, multipart_dir, info->upload_id, DEFFILEMODE, 0);
	if (rc != 0)
		goto err_multipart_dir;

	/* Insert an entry into bucket multipart index */
	rc = dfs_lookup_rel(ds3->meta_dfs, multipart_dir, info->upload_id, O_RDWR, &upload_dir,
			    NULL, NULL);
	if (rc != 0)
		goto err_multipart_dir;

	rc = dfs_setxattr(ds3->meta_dfs, upload_dir, RGW_DIR_ENTRY_XATTR, info->encoded,
			  info->encoded_length, 0);
	if (rc != 0)
		goto err_upload_dir;

	/* Set key */
	rc =
	    dfs_setxattr(ds3->meta_dfs, upload_dir, RGW_KEY_XATTR, info->key, strlen(info->key), 0);

err_upload_dir:
	rc2 = dfs_release(upload_dir);
	rc  = rc == 0 ? rc2 : rc;
err_multipart_dir:
	rc2 = dfs_release(multipart_dir);
	rc  = rc == 0 ? rc2 : rc;
	return -rc;
}

int
ds3_upload_remove(const char *bucket_name, const char *upload_id, ds3_t *ds3)
{
	int        rc  = 0;
	int        rc2 = 0;
	dfs_obj_t *multipart_dir;

	if (bucket_name == NULL || upload_id == NULL || ds3 == NULL)
		return -EINVAL;

	/* Remove upload from bucket multipart index */
	rc = dfs_lookup_rel(ds3->meta_dfs, ds3->meta_dirs[MULTIPART_DIR], bucket_name, O_RDWR,
			    &multipart_dir, NULL, NULL);
	if (rc != 0)
		return -rc;

	rc  = dfs_remove(ds3->meta_dfs, multipart_dir, upload_id, true, NULL);
	rc2 = dfs_release(multipart_dir);
	rc  = rc == 0 ? rc2 : rc;
	return -rc;
}

int
ds3_upload_get_info(struct ds3_multipart_upload_info *info, const char *bucket_name,
		    const char *upload_id, ds3_t *ds3)
{
	int         rc  = 0;
	int         rc2 = 0;
	dfs_obj_t  *multipart_dir;
	dfs_obj_t  *upload_dir;
	daos_size_t size;

	if (info == NULL || bucket_name == NULL || upload_id == NULL || ds3 == NULL)
		return -EINVAL;

	rc = dfs_lookup_rel(ds3->meta_dfs, ds3->meta_dirs[MULTIPART_DIR], bucket_name, O_RDWR,
			    &multipart_dir, NULL, NULL);
	if (rc != 0)
		return -rc;

	rc = dfs_lookup_rel(ds3->meta_dfs, multipart_dir, upload_id, O_RDWR, &upload_dir, NULL,
			    NULL);
	if (rc != 0)
		goto err_multipart_dir;

	rc = dfs_getxattr(ds3->meta_dfs, upload_dir, RGW_DIR_ENTRY_XATTR, info->encoded,
			  &info->encoded_length);
	if (rc != 0)
		goto err_upload_dir;

	/* Set key */
	rc = dfs_getxattr(ds3->meta_dfs, upload_dir, RGW_KEY_XATTR, info->key, &size);

err_upload_dir:
	rc2 = dfs_release(upload_dir);
	rc  = rc == 0 ? rc2 : rc;
err_multipart_dir:
	rc2 = dfs_release(multipart_dir);
	rc  = rc == 0 ? rc2 : rc;
	return -rc;
}

int
ds3_part_open(const char *bucket_name, const char *upload_id, uint64_t part_num, bool truncate,
	      ds3_part_t **ds3p, ds3_t *ds3)
{
	int         rc  = 0;
	int         rc2 = 0;
	ds3_part_t *ds3p_tmp;
	dfs_obj_t  *multipart_dir;
	dfs_obj_t  *upload_dir;
	char        part_name_str[7];
	int         flags;

	if (ds3p == NULL || bucket_name == NULL || upload_id == NULL || ds3 == NULL)
		return -EINVAL;

	D_ALLOC_PTR(ds3p_tmp);
	if (ds3p_tmp == NULL)
		return -ENOMEM;

	rc = dfs_lookup_rel(ds3->meta_dfs, ds3->meta_dirs[MULTIPART_DIR], bucket_name, O_RDWR,
			    &multipart_dir, NULL, NULL);
	if (rc != 0)
		goto err_ds3p;

	rc = dfs_lookup_rel(ds3->meta_dfs, multipart_dir, upload_id, O_RDWR, &upload_dir, NULL,
			    NULL);
	if (rc != 0)
		goto err_multipart_dir;

	sprintf(part_name_str, "%06lu", part_num);
	flags = truncate ? O_RDWR | O_CREAT | O_TRUNC : O_RDWR;

	rc = dfs_open(ds3->meta_dfs, upload_dir, part_name_str, DEFFILEMODE | S_IFREG, flags, 0, 0,
		      NULL, &ds3p_tmp->dfs_obj);
	if (rc == 0)
		*ds3p = ds3p_tmp;

	rc2 = dfs_release(upload_dir);
	rc  = rc == 0 ? rc2 : rc;
err_multipart_dir:
	rc2 = dfs_release(multipart_dir);
	rc  = rc == 0 ? rc2 : rc;
err_ds3p:
	if (rc != 0)
		D_FREE(ds3p_tmp);
	return -rc;
}

int
ds3_part_close(ds3_part_t *ds3p)
{
	int rc = 0;

	if (ds3p == NULL)
		return -EINVAL;

	rc = dfs_release(ds3p->dfs_obj);
	D_FREE(ds3p);
	return -rc;
}

int
ds3_part_write(void *buf, daos_off_t off, daos_size_t *size, ds3_part_t *ds3p, ds3_t *ds3,
	       daos_event_t *ev)
{
	d_sg_list_t wsgl;
	d_iov_t     iov;

	if (ds3p == NULL || buf == NULL || ds3 == NULL)
		return -EINVAL;

	d_iov_set(&iov, buf, *size);
	wsgl.sg_nr   = 1;
	wsgl.sg_iovs = &iov;
	return -dfs_write(ds3->meta_dfs, ds3p->dfs_obj, &wsgl, off, ev);
}

int
ds3_part_read(void *buf, daos_off_t off, daos_size_t *size, ds3_part_t *ds3p, ds3_t *ds3,
	      daos_event_t *ev)
{
	d_iov_t     iov;
	d_sg_list_t rsgl;

	if (ds3p == NULL || buf == NULL || ds3 == NULL)
		return -EINVAL;

	d_iov_set(&iov, buf, *size);
	rsgl.sg_nr     = 1;
	rsgl.sg_iovs   = &iov;
	rsgl.sg_nr_out = 1;
	return -dfs_read(ds3->meta_dfs, ds3p->dfs_obj, &rsgl, off, size, ev);
}

int
ds3_part_set_info(struct ds3_multipart_part_info *info, ds3_part_t *ds3p, ds3_t *ds3,
		  daos_event_t *ev)
{
	if (ds3p == NULL || info == NULL || ds3 == NULL)
		return -EINVAL;

	return -dfs_setxattr(ds3->meta_dfs, ds3p->dfs_obj, RGW_PART_XATTR, info->encoded,
			     info->encoded_length, 0);
}
