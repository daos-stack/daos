/**
 * (C) Copyright 2018-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/** DFS object metadata ops */

#define D_LOGFAC DD_FAC(dfs)

#include <daos/common.h>
#include <daos/object.h>

#include "dfs_internal.h"

int
readdir_int(dfs_t *dfs, dfs_obj_t *obj, daos_anchor_t *anchor, uint32_t *nr, struct dirent *dirs,
	    struct stat *stbufs)
{
	daos_key_desc_t *kds;
	char            *enum_buf;
	uint32_t         number, key_nr, i;
	d_sg_list_t      sgl;
	int              rc = 0;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL || !S_ISDIR(obj->mode))
		return ENOTDIR;
	if (*nr == 0)
		return 0;
	if (dirs == NULL || anchor == NULL)
		return EINVAL;

	D_ALLOC_ARRAY(kds, *nr);
	if (kds == NULL)
		return ENOMEM;

	D_ALLOC_ARRAY(enum_buf, *nr * DFS_MAX_NAME);
	if (enum_buf == NULL) {
		D_FREE(kds);
		return ENOMEM;
	}

	key_nr = 0;
	number = *nr;
	while (!daos_anchor_is_eof(anchor)) {
		d_iov_t iov;
		char   *ptr;

		memset(enum_buf, 0, (*nr) * DFS_MAX_NAME);

		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		d_iov_set(&iov, enum_buf, (*nr) * DFS_MAX_NAME);
		sgl.sg_iovs = &iov;

		rc = daos_obj_list_dkey(obj->oh, dfs->th, &number, kds, &sgl, anchor, NULL);
		if (rc)
			D_GOTO(out, rc = daos_der2errno(rc));

		for (ptr = enum_buf, i = 0; i < number; i++) {
			memcpy(dirs[key_nr].d_name, ptr, kds[i].kd_key_len);
			dirs[key_nr].d_name[kds[i].kd_key_len] = '\0';
			ptr += kds[i].kd_key_len;

			/** stat the entry if requested */
			if (stbufs) {
				if (dfs->dcache) {
					dfs_obj_t *ent;

					rc = dcache_find_insert_rel(dfs, obj, dirs[key_nr].d_name,
								    kds[i].kd_key_len, O_NOFOLLOW,
								    &ent, NULL, &stbufs[key_nr]);
					if (rc)
						D_GOTO(out, rc);
					drec_decref(dfs->dcache, ent);
				} else {
					rc = entry_stat(dfs, dfs->th, obj->oh, dirs[key_nr].d_name,
							kds[i].kd_key_len, NULL, true,
							&stbufs[key_nr], NULL);
					if (rc) {
						D_ERROR("Failed to stat entry '%s': %d (%s)\n",
							dirs[key_nr].d_name, rc, strerror(rc));
						D_GOTO(out, rc);
					}
				}
			}
			key_nr++;
		}
		number = *nr - key_nr;
		if (number == 0)
			break;
	}
	*nr = key_nr;
	DFS_OP_STAT_INCR(dfs, DOS_READDIR);

out:
	D_FREE(enum_buf);
	D_FREE(kds);
	return rc;
}

int
dfs_readdir(dfs_t *dfs, dfs_obj_t *obj, daos_anchor_t *anchor, uint32_t *nr, struct dirent *dirs)
{
	return readdir_int(dfs, obj, anchor, nr, dirs, NULL);
}

int
dfs_readdirplus(dfs_t *dfs, dfs_obj_t *obj, daos_anchor_t *anchor, uint32_t *nr,
		struct dirent *dirs, struct stat *stbufs)
{
	return readdir_int(dfs, obj, anchor, nr, dirs, stbufs);
}

int
dfs_dir_anchor_init(dfs_obj_t *obj, dfs_dir_anchor_t **_anchor)
{
	dfs_dir_anchor_t *anchor;

	if (obj == NULL || !S_ISDIR(obj->mode))
		return ENOTDIR;

	D_ALLOC_PTR(anchor);
	if (anchor == NULL)
		return ENOMEM;

	anchor->dda_dir = obj;
	daos_anchor_init(&anchor->dda_anchor_int, 0);
	anchor->dda_bucket_id     = 0;
	anchor->dda_bucket_offset = 0;
	*_anchor                  = anchor;
	return 0;
}

void
dfs_dir_anchor_reset(dfs_dir_anchor_t *anchor)
{
	daos_anchor_init(&anchor->dda_anchor_int, 0);
	anchor->dda_bucket_id     = 0;
	anchor->dda_bucket_offset = 0;
}

bool
dfs_dir_anchor_is_eof(dfs_dir_anchor_t *anchor)
{
	return daos_anchor_is_eof(&anchor->dda_anchor_int);
}

void
dfs_dir_anchor_destroy(dfs_dir_anchor_t *anchor)
{
	D_FREE(anchor);
}

int
dfs_readdir_s(dfs_t *dfs, dfs_obj_t *dir, dfs_dir_anchor_t *anchor, struct dirent *entry)
{
	uint32_t nr = 1;
	int      rc;

	if (daos_oid_cmp(dir->oid, anchor->dda_dir->oid) != 0)
		return EINVAL;
	if (daos_anchor_is_eof(&anchor->dda_anchor_int))
		return -1;

	rc = readdir_int(dfs, dir, &anchor->dda_anchor_int, &nr, entry, NULL);
	if (rc)
		return rc;

	/** if we did not enumerate anything, try again to make sure we hit EOF */
	if (nr == 0) {
		if (daos_anchor_is_eof(&anchor->dda_anchor_int))
			return -1; /** typically EOF code */
		else
			return EIO;
	}
	return rc;
#if 0
	/** if no caching, just use the internal anchor with 1 entry */
	if (dfs->dcache == NULL)
		return readdir_int(dfs, obj, &anchor->dda_anchor_int, 1, &dir, NULL);
	else
		dcache_readdir(dfs->dcache, obj, anchor, dir);
#endif
}

int
dfs_iterate(dfs_t *dfs, dfs_obj_t *obj, daos_anchor_t *anchor, uint32_t *nr, size_t size,
	    dfs_filler_cb_t op, void *udata)
{
	daos_key_desc_t *kds;
	d_sg_list_t      sgl;
	d_iov_t          iov;
	uint32_t         num, keys_nr;
	char            *enum_buf, *ptr;
	int              rc = 0;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL || !S_ISDIR(obj->mode))
		return ENOTDIR;
	if (size == 0 || *nr == 0)
		return 0;
	if (anchor == NULL)
		return EINVAL;

	num = *nr;
	D_ALLOC_ARRAY(kds, num);
	if (kds == NULL)
		return ENOMEM;

	/** Allocate a buffer to store the entry keys */
	D_ALLOC_ARRAY(enum_buf, size);
	if (enum_buf == NULL) {
		D_FREE(kds);
		return ENOMEM;
	}

	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, enum_buf, size);
	sgl.sg_iovs = &iov;
	keys_nr     = 0;
	ptr         = enum_buf;

	while (!daos_anchor_is_eof(anchor)) {
		uint32_t i;

		/*
		 * list num or less entries, but not more than we can fit in
		 * enum_buf
		 */
		rc = daos_obj_list_dkey(obj->oh, dfs->th, &num, kds, &sgl, anchor, NULL);
		if (rc)
			D_GOTO(out, rc = daos_der2errno(rc));

		/** for every entry, issue the filler cb */
		for (i = 0; i < num; i++) {
			if (op) {
				char term_char;

				term_char              = ptr[kds[i].kd_key_len];
				ptr[kds[i].kd_key_len] = '\0';
				rc                     = op(dfs, obj, ptr, udata);
				if (rc)
					D_GOTO(out, rc);

				ptr[kds[i].kd_key_len] = term_char;
			}

			/** advance pointer to next entry */
			ptr += kds[i].kd_key_len;
			/** adjust size of buffer data remaining */
			size -= kds[i].kd_key_len;
			keys_nr++;
		}
		num = *nr - keys_nr;
		/** stop if no more size or entries available to fill */
		if (size == 0 || num == 0)
			break;
		/** adjust iov for iteration */
		d_iov_set(&iov, ptr, size);
	}

	*nr = keys_nr;
	DFS_OP_STAT_INCR(dfs, DOS_READDIR);
out:
	D_FREE(kds);
	D_FREE(enum_buf);
	return rc;
}
