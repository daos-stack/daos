/**
 * (C) Copyright 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/** DFS extended attributes */

#define D_LOGFAC DD_FAC(dfs)

#include <sys/xattr.h>
#include <linux/xattr.h>
#include <daos/common.h>

#include "dfs_internal.h"

int
dfs_setxattr(dfs_t *dfs, dfs_obj_t *obj, const char *name, const void *value, daos_size_t size,
	     int flags)
{
	char           *xname = NULL;
	daos_handle_t   th    = DAOS_TX_NONE;
	d_sg_list_t     sgls[2];
	d_iov_t         sg_iovs[3];
	daos_iod_t      iods[2];
	daos_recx_t     recxs[2];
	daos_key_t      dkey;
	daos_handle_t   oh;
	uint64_t        cond = 0;
	struct timespec now;
	int             rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (obj == NULL)
		return EINVAL;
	if (name == NULL)
		return EINVAL;
	if (strnlen(name, DFS_MAX_XATTR_NAME + 1) > DFS_MAX_XATTR_NAME)
		return EINVAL;
	if (size > DFS_MAX_XATTR_LEN)
		return EINVAL;

	/** prefix name with x: to avoid collision with internal attrs */
	xname = concat("x:", name);
	if (xname == NULL)
		return ENOMEM;

	/** Open parent object and insert xattr in the entry of the object */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		D_GOTO(free, rc = daos_der2errno(rc));

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

	/** add xattr iod & sgl */
	d_iov_set(&iods[0].iod_name, xname, strlen(xname));
	iods[0].iod_nr    = 1;
	iods[0].iod_recxs = NULL;
	iods[0].iod_type  = DAOS_IOD_SINGLE;
	iods[0].iod_size  = size;
	if (value == NULL)
		d_iov_set(&sg_iovs[0], NULL, 0);
	else
		d_iov_set(&sg_iovs[0], (void *)value, size);
	sgls[0].sg_nr     = 1;
	sgls[0].sg_nr_out = 0;
	sgls[0].sg_iovs   = &sg_iovs[0];

	/** add ctime iod & sgl */
	d_iov_set(&iods[1].iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iods[1].iod_recxs = recxs;
	iods[1].iod_type  = DAOS_IOD_ARRAY;
	iods[1].iod_size  = 1;
	iods[1].iod_nr    = 2;
	recxs[0].rx_idx   = CTIME_IDX;
	recxs[0].rx_nr    = sizeof(uint64_t);
	recxs[1].rx_idx   = CTIME_NSEC_IDX;
	recxs[1].rx_nr    = sizeof(uint64_t);
	rc                = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out, rc = errno);
	sgls[1].sg_nr     = 2;
	sgls[1].sg_nr_out = 0;
	sgls[1].sg_iovs   = &sg_iovs[1];
	d_iov_set(&sg_iovs[1], &now.tv_sec, sizeof(uint64_t));
	d_iov_set(&sg_iovs[2], &now.tv_nsec, sizeof(uint64_t));

	/** if not default flag, check for xattr existence */
	if (flags != 0) {
		if (flags == XATTR_CREATE)
			cond |= DAOS_COND_AKEY_INSERT;
		if (flags == XATTR_REPLACE)
			cond |= DAOS_COND_AKEY_UPDATE;
	}
	cond |= DAOS_COND_DKEY_UPDATE;

	/** update ctime in a separate update if DAOS_COND_AKEY_INSERT is used for the xattr */
	if (cond & DAOS_COND_AKEY_INSERT) {
		/** insert the xattr */
		rc = daos_obj_update(oh, th, cond, &dkey, 1, &iods[0], &sgls[0], NULL);
		if (rc) {
			D_ERROR("Failed to insert extended attribute %s\n", name);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
		/** update the ctime */
		rc = daos_obj_update(oh, th, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iods[1], &sgls[1],
				     NULL);
		if (rc) {
			D_ERROR("Failed to update ctime %s\n", name);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	} else {
		/** replace the xattr and update the ctime */
		rc = daos_obj_update(oh, th, cond, &dkey, 2, iods, sgls, NULL);
		if (rc) {
			D_ERROR("Failed to insert extended attribute %s\n", name);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	}

	DFS_OP_STAT_INCR(dfs, DOS_SETXATTR);
out:
	daos_obj_close(oh, NULL);
free:
	D_FREE(xname);
	return rc;
}

int
dfs_getxattr(dfs_t *dfs, dfs_obj_t *obj, const char *name, void *value, daos_size_t *size)
{
	char         *xname = NULL;
	d_sg_list_t   sgl;
	d_iov_t       sg_iov;
	daos_iod_t    iod;
	daos_key_t    dkey;
	daos_handle_t oh;
	int           rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL)
		return EINVAL;
	if (name == NULL)
		return EINVAL;
	if (strnlen(name, DFS_MAX_XATTR_NAME + 1) > DFS_MAX_XATTR_NAME)
		return EINVAL;

	xname = concat("x:", name);
	if (xname == NULL)
		return ENOMEM;

	/** Open parent object and get xattr from the entry of the object */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RO, &oh, NULL);
	if (rc)
		D_GOTO(out, rc = daos_der2errno(rc));

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

	/** set akey as the xattr name */
	d_iov_set(&iod.iod_name, xname, strlen(xname));
	iod.iod_nr    = 1;
	iod.iod_recxs = NULL;
	iod.iod_type  = DAOS_IOD_SINGLE;

	if (*size) {
		iod.iod_size = *size;

		/** set sgl for fetch */
		if (value == NULL)
			d_iov_set(&sg_iov, NULL, 0);
		else
			d_iov_set(&sg_iov, value, *size);
		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs   = &sg_iov;

		rc = daos_obj_fetch(oh, dfs->th, DAOS_COND_AKEY_FETCH, &dkey, 1, &iod, &sgl, NULL,
				    NULL);
	} else {
		iod.iod_size = DAOS_REC_ANY;

		rc = daos_obj_fetch(oh, dfs->th, DAOS_COND_AKEY_FETCH, &dkey, 1, &iod, NULL, NULL,
				    NULL);
	}
	if (rc) {
		DL_CDEBUG(rc == -DER_NONEXIST, DLOG_DBG, DLOG_ERR, rc, "Failed to fetch xattr '%s'",
			  name);
		D_GOTO(close, rc = daos_der2errno(rc));
	}

	*size = iod.iod_size;
	DFS_OP_STAT_INCR(dfs, DOS_GETXATTR);

close:
	daos_obj_close(oh, NULL);
out:
	D_FREE(xname);
	if (rc == ENOENT)
		rc = ENODATA;
	return rc;
}

int
dfs_removexattr(dfs_t *dfs, dfs_obj_t *obj, const char *name)
{
	char           *xname = NULL;
	daos_handle_t   th    = DAOS_TX_NONE;
	daos_key_t      dkey, akey;
	daos_handle_t   oh;
	uint64_t        cond = 0;
	d_sg_list_t     sgl;
	d_iov_t         sg_iovs[2];
	daos_iod_t      iod;
	daos_recx_t     recxs[2];
	struct timespec now;
	int             rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (obj == NULL)
		return EINVAL;
	if (name == NULL)
		return EINVAL;
	if (strnlen(name, DFS_MAX_XATTR_NAME + 1) > DFS_MAX_XATTR_NAME)
		return EINVAL;

	xname = concat("x:", name);
	if (xname == NULL)
		return ENOMEM;

	/** Open parent object and remove xattr from the entry of the object */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		D_GOTO(free, rc = daos_der2errno(rc));

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));
	/** set akey as the xattr name */
	d_iov_set(&akey, xname, strlen(xname));

	cond = DAOS_COND_DKEY_UPDATE | DAOS_COND_PUNCH;
	rc   = daos_obj_punch_akeys(oh, th, cond, &dkey, 1, &akey, NULL);
	if (rc) {
		D_CDEBUG(rc == -DER_NONEXIST, DLOG_DBG, DLOG_ERR,
			 "Failed to punch extended attribute '%s'\n", name);
		D_GOTO(out, rc = daos_der2errno(rc));
	}

	/** update ctime */
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_recxs   = recxs;
	iod.iod_type    = DAOS_IOD_ARRAY;
	iod.iod_size    = 1;
	iod.iod_nr      = 2;
	recxs[0].rx_idx = CTIME_IDX;
	recxs[0].rx_nr  = sizeof(uint64_t);
	recxs[1].rx_idx = CTIME_NSEC_IDX;
	recxs[1].rx_nr  = sizeof(uint64_t);
	rc              = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out, rc = errno);
	sgl.sg_nr     = 2;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &sg_iovs[0];
	d_iov_set(&sg_iovs[0], &now.tv_sec, sizeof(uint64_t));
	d_iov_set(&sg_iovs[1], &now.tv_nsec, sizeof(uint64_t));

	rc = daos_obj_update(oh, th, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to update mode, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc = daos_der2errno(rc));
	}

	DFS_OP_STAT_INCR(dfs, DOS_RMXATTR);
out:
	daos_obj_close(oh, NULL);
free:
	D_FREE(xname);
	return rc;
}

int
dfs_listxattr(dfs_t *dfs, dfs_obj_t *obj, char *list, daos_size_t *size)
{
	daos_key_t      dkey;
	daos_handle_t   oh;
	daos_key_desc_t kds[ENUM_DESC_NR];
	daos_anchor_t   anchor = {0};
	daos_size_t     list_size, ret_size;
	char           *ptr_list;
	int             rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL)
		return EINVAL;

	/** Open parent object and list from entry */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		return daos_der2errno(rc);

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

	list_size = *size;
	ret_size  = 0;
	ptr_list  = list;

	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t    number = ENUM_DESC_NR;
		uint32_t    i;
		d_iov_t     iov;
		char        enum_buf[ENUM_XDESC_BUF] = {0};
		d_sg_list_t sgl;
		char       *ptr;

		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		d_iov_set(&iov, enum_buf, ENUM_DESC_BUF);
		sgl.sg_iovs = &iov;

		rc = daos_obj_list_akey(oh, dfs->th, &dkey, &number, kds, &sgl, &anchor, NULL);
		if (rc)
			D_GOTO(out, rc = daos_der2errno(rc));

		if (number == 0)
			continue;

		for (ptr = enum_buf, i = 0; i < number; i++) {
			if (strncmp("x:", ptr, 2) != 0) {
				ptr += kds[i].kd_key_len;
				continue;
			}

			ret_size += kds[i].kd_key_len - 1;

			if (list == NULL)
				continue;
			if (list_size < kds[i].kd_key_len - 2)
				continue;

			memcpy(ptr_list, ptr + 2, kds[i].kd_key_len - 2);
			ptr_list[kds[i].kd_key_len - 2] = '\0';
			list_size -= kds[i].kd_key_len - 1;
			ptr_list += kds[i].kd_key_len - 1;
			ptr += kds[i].kd_key_len;
		}
	}

	*size = ret_size;
	DFS_OP_STAT_INCR(dfs, DOS_LSXATTR);
out:
	daos_obj_close(oh, NULL);
	return rc;
}
