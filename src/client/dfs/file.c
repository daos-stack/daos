/**
 * (C) Copyright 2018-2024 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/** DFS file ops */

#define D_LOGFAC DD_FAC(dfs)

#include <daos/array.h>
#include <daos/common.h>
#include <daos/object.h>
#include "dfs_internal.h"

int
file_stat(dfs_t *dfs, daos_handle_t head_oh, daos_handle_t tail_oh, bool has_tail, daos_handle_t th,
	  daos_array_stbuf_t *stbuf)
{
	daos_array_stbuf_t tail_stbuf = {0};
	int                rc;

	rc = daos_array_stat(head_oh, th, stbuf, NULL);
	if (rc)
		return daos_der2errno(rc);

	if (!has_tail)
		return 0;

	if (!daos_handle_is_valid(tail_oh))
		return EIO;

	rc = daos_array_stat(tail_oh, th, &tail_stbuf, NULL);
	if (rc)
		return daos_der2errno(rc);

	stbuf->st_size += tail_stbuf.st_size;
	if (tail_stbuf.st_max_epoch > stbuf->st_max_epoch)
		stbuf->st_max_epoch = tail_stbuf.st_max_epoch;

	return 0;
}

int
file_stat_by_oid(dfs_t *dfs, daos_obj_id_t head_oid, daos_obj_id_t tail_oid, bool has_tail,
		 daos_size_t chunk_size, daos_handle_t th, daos_array_stbuf_t *stbuf)
{
	daos_handle_t head_oh = DAOS_HDL_INVAL;
	daos_handle_t tail_oh = DAOS_HDL_INVAL;
	int           rc;

	rc = daos_array_open_with_attr(dfs->coh, head_oid, th, DAOS_OO_RO, 1,
				       chunk_size ? chunk_size : dfs->attr.da_chunk_size, &head_oh,
				       NULL);
	if (rc) {
		D_ERROR("daos_array_open_with_attr() failed " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	if (has_tail) {
		if (daos_obj_id_is_nil(tail_oid)) {
			rc = EIO;
			D_GOTO(out, rc);
		}

		rc = daos_array_open_with_attr(dfs->coh, tail_oid, th, DAOS_OO_RO, 1,
					       chunk_size ? chunk_size : dfs->attr.da_chunk_size,
					       &tail_oh, NULL);
		if (rc) {
			D_ERROR("daos_array_open_with_attr() failed " DF_RC "\n", DP_RC(rc));
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	}

	rc = file_stat(dfs, head_oh, tail_oh, has_tail, th, stbuf);
out:
	if (daos_handle_is_valid(tail_oh)) {
		int rc2 = daos_array_close(tail_oh, NULL);

		if (rc == 0)
			rc = daos_der2errno(rc2);
	}
	if (daos_handle_is_valid(head_oh)) {
		int rc2 = daos_array_close(head_oh, NULL);

		if (rc == 0)
			rc = daos_der2errno(rc2);
	}

	return rc;
}

int
dfs_get_file_oh(dfs_obj_t *obj, daos_handle_t *oh)
{
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if (oh == NULL)
		return EINVAL;

	oh->cookie = obj->oh.cookie;
	return 0;
}

int
dfs_get_chunk_size(dfs_obj_t *obj, daos_size_t *chunk_size)
{
	daos_size_t cell_size;
	int         rc;

	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if (chunk_size == NULL)
		return EINVAL;

	rc = daos_array_get_attr(obj->oh, chunk_size, &cell_size);
	if (rc)
		return daos_der2errno(rc);

	D_ASSERT(cell_size == 1);
	return 0;
}

static int
set_chunk_size(dfs_t *dfs, dfs_obj_t *obj, daos_size_t csize)
{
	daos_handle_t oh;
	d_sg_list_t   sgl;
	d_iov_t       sg_iov;
	daos_iod_t    iod;
	daos_recx_t   recx;
	daos_key_t    dkey;
	int           rc, rc2;

	/** Open parent object and fetch entry of obj from it */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		return daos_der2errno(rc);

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

	/** set akey as the inode name */
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_nr    = 1;
	iod.iod_size  = 1;
	recx.rx_idx   = CSIZE_IDX;
	recx.rx_nr    = sizeof(daos_size_t);
	iod.iod_recxs = &recx;
	iod.iod_type  = DAOS_IOD_ARRAY;

	/** set sgl for update */
	d_iov_set(&sg_iov, &csize, sizeof(daos_size_t));
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &sg_iov;

	rc = daos_obj_update(oh, DAOS_TX_NONE, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to update chunk size: " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc = daos_der2errno(rc));
	}

out:
	rc2 = daos_obj_close(oh, NULL);
	if (rc == 0)
		rc = daos_der2errno(rc2);
	return rc;
}

int
dfs_obj_set_chunk_size(dfs_t *dfs, dfs_obj_t *obj, int flags, daos_size_t csize)
{
	int rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL)
		return EINVAL;
	if (!S_ISDIR(obj->mode))
		return ENOTSUP;
	if (csize == 0)
		csize = dfs->attr.da_chunk_size;

	rc = set_chunk_size(dfs, obj, csize);
	if (rc)
		return rc;

	/** if this is the root dir, we need to update the cached handle csize */
	if (daos_oid_cmp(obj->oid, dfs->root.oid) == 0)
		dfs->root.d.chunk_size = csize;

	return 0;
}

int
dfs_file_update_chunk_size(dfs_t *dfs, dfs_obj_t *obj, daos_size_t csize)
{
	int rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL)
		return EINVAL;
	if (!S_ISREG(obj->mode))
		return EINVAL;
	if (csize == 0)
		csize = dfs->attr.da_chunk_size;

	rc = set_chunk_size(dfs, obj, csize);
	if (rc)
		return rc;

	/* need to update the array handle chunk size */
	rc = daos_array_update_chunk_size(obj->oh, csize);
	if (rc)
		return daos_der2errno(rc);

	return 0;
}

int
dfs_get_size(dfs_t *dfs, dfs_obj_t *obj, daos_size_t *size)
{
	daos_array_stbuf_t stbuf = {0};
	int rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if (size == NULL)
		return EINVAL;

	rc = file_stat(dfs, obj->oh, obj->f.tail_oh, obj->f.has_tail, dfs->th, &stbuf);
	if (rc)
		return rc;

	*size = stbuf.st_size;
	return 0;
}

int
file_truncate_zero(dfs_obj_t *obj, daos_handle_t th)
{
	int rc;

	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;

	if (obj->f.has_tail) {
		rc = daos_array_set_size(obj->f.tail_oh, th, 0, NULL);
		if (rc)
			return daos_der2errno(rc);
	}

	rc = daos_array_set_size(obj->oh, th, 0, NULL);
	return daos_der2errno(rc);
}
