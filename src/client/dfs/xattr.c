/**
 * (C) Copyright 2018-2024 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
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
	char            *xname = NULL;
	daos_handle_t    th    = DAOS_TX_NONE;
	d_sg_list_t      sgls[2];
	d_iov_t          sg_iovs[3];
	daos_iod_t       iods[2];
	daos_recx_t      recxs[2];
	daos_key_t       dkey;
	daos_handle_t    oh   = DAOS_HDL_INVAL;
	uint64_t         cond = 0;
	struct timespec  now;
	struct dfs_entry entry = {0};
	bool             exists;
	int              rc;

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
	if (size > 0 && value == NULL)
		return EINVAL;
	if (size > DFS_MAX_XATTR_LEN)
		return EINVAL;

	/** prefix name with x: to avoid collision with internal attrs */
	xname = concat("x:", name);
	if (xname == NULL)
		return ENOMEM;

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
	sgls[1].sg_nr     = 2;
	sgls[1].sg_nr_out = 0;
	sgls[1].sg_iovs   = &sg_iovs[1];

	/** if not default flag, check for xattr existence */
	if (flags != 0) {
		if (flags == XATTR_CREATE)
			cond |= DAOS_COND_AKEY_INSERT;
		if (flags == XATTR_REPLACE)
			cond |= DAOS_COND_AKEY_UPDATE;
	}
	cond |= DAOS_COND_DKEY_UPDATE;

	/**
	 * Use dtx in balanced mode to serialize converting the
	 * file to hardlink and setting/updating xattr in parallel.
	 */
	if (dfs->use_dtx) {
		rc = daos_tx_open(dfs->coh, &th, 0, NULL);
		if (rc) {
			D_ERROR("daos_tx_open() failed (%d)\n", rc);
			D_GOTO(out_free, rc = daos_der2errno(rc));
		}
	}

retry:
	if (DFS_IS_HARDLINK(obj->mode)) {
		/** For hardlinks, xattrs are stored in GIT with OID as dkey */
		if (!daos_handle_is_valid(dfs->git_oh))
			D_GOTO(out_tx, rc = ENOTSUP);
		d_iov_set(&dkey, &obj->oid, sizeof(daos_obj_id_t));
		oh = dfs->git_oh;
	} else {
		/** Open parent object and insert xattr in the entry of the object */
		rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RW, &oh, NULL);
		if (rc)
			D_GOTO(out_tx, rc = daos_der2errno(rc));

		/*
		 * Handle conversion of the file to hardlink remotely.
		 */
		if (dfs->use_dtx && S_ISREG(obj->mode)) {
			rc = fetch_entry(dfs->layout_v, oh, th, obj->name, strlen(obj->name), false,
					 &exists, &entry, 0, NULL, NULL, NULL);
			if (rc) {
				D_ERROR("Failed to fetch entry '%s' (%d)\n", obj->name, rc);
				D_GOTO(out_obj, rc);
			}
			if (!exists)
				D_GOTO(out_obj, rc = ENOENT);
			if (DFS_IS_HARDLINK(entry.mode)) {
				dfs_set_hardlink(&obj->mode);
				daos_obj_close(oh, NULL);
				oh = DAOS_HDL_INVAL;
				goto retry;
			}
		}

		/** set dkey as the entry name */
		d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));
	}

	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out_obj, rc = errno);
	d_iov_set(&sg_iovs[1], &now.tv_sec, sizeof(uint64_t));
	d_iov_set(&sg_iovs[2], &now.tv_nsec, sizeof(uint64_t));

	/** Since conditional updates are not possible within DTX explicitly check for existence of
	 * xattr before inserting/updating it.
	 */
	if (dfs->use_dtx) {
		daos_iod_t check_iod = {0};

		d_iov_set(&check_iod.iod_name, xname, strlen(xname));
		check_iod.iod_nr   = 1;
		check_iod.iod_type = DAOS_IOD_SINGLE;
		check_iod.iod_size = DAOS_REC_ANY;
		rc = daos_obj_fetch(oh, th, DAOS_COND_AKEY_FETCH, &dkey, 1, &check_iod, NULL, NULL,
				    NULL);
		if ((rc == 0) && (flags & XATTR_CREATE)) {
			D_ERROR("xattr %s already exists\n", name);
			D_GOTO(out_obj, rc = EEXIST);
		} else if ((rc == -DER_NONEXIST) && (flags & XATTR_REPLACE)) {
			D_ERROR("xattr %s does not exist\n", name);
			D_GOTO(out_obj, rc = ENOENT);
		} else if (rc && rc != -DER_NONEXIST) {
			D_ERROR("Failed to check xattr %s existence\n", name);
			D_GOTO(out_obj, rc = daos_der2errno(rc));
		}
		rc = 0;
	}

	/** update ctime in a separate update if DAOS_COND_AKEY_INSERT is used for the xattr */
	if (cond & DAOS_COND_AKEY_INSERT) {
		/** insert the xattr */
		rc = daos_obj_update(oh, th, dfs->use_dtx ? 0 : cond, &dkey, 1, &iods[0], &sgls[0],
				     NULL);
		if (rc) {
			D_ERROR("Failed to insert extended attribute %s\n", name);
			D_GOTO(out_obj, rc = daos_der2errno(rc));
		}
		/** update the ctime */
		rc = daos_obj_update(oh, th, dfs->use_dtx ? 0 : DAOS_COND_DKEY_UPDATE, &dkey, 1,
				     &iods[1], &sgls[1], NULL);
		if (rc) {
			D_ERROR("Failed to update ctime %s\n", name);
			D_GOTO(out_obj, rc = daos_der2errno(rc));
		}
	} else {
		/** replace the xattr and update the ctime */
		rc = daos_obj_update(oh, th, dfs->use_dtx ? 0 : cond, &dkey, 2, iods, sgls, NULL);
		if (rc) {
			D_ERROR("Failed to insert extended attribute %s\n", name);
			D_GOTO(out_obj, rc = daos_der2errno(rc));
		}
	}

	DFS_OP_STAT_INCR(dfs, DOS_SETXATTR);
out_obj:
	/** Only close parent handle; GIT handle is owned by the DFS mount. */
	if (!DFS_IS_HARDLINK(obj->mode) && daos_handle_is_valid(oh)) {
		daos_obj_close(oh, NULL);
		oh = DAOS_HDL_INVAL;
	}
out_tx:
	if (dfs->use_dtx) {
		if (rc == 0) {
			rc = daos_tx_commit(th, NULL);
			if (rc)
				rc = daos_der2errno(rc);
		}
		rc = check_tx(th, rc);
		if (rc == ERESTART)
			goto retry;
	}
out_free:
	D_FREE(xname);
	return rc;
}

int
dfs_getxattr(dfs_t *dfs, dfs_obj_t *obj, const char *name, void *value, daos_size_t *size)
{
	char            *xname = NULL;
	d_sg_list_t      sgl;
	d_iov_t          sg_iov;
	daos_iod_t       iod;
	daos_key_t       dkey;
	daos_handle_t    oh    = DAOS_HDL_INVAL;
	struct dfs_entry entry = {0};
	bool             exists;
	int              rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL)
		return EINVAL;
	if (name == NULL)
		return EINVAL;
	if (size == NULL)
		return EINVAL;
	if (strnlen(name, DFS_MAX_XATTR_NAME + 1) > DFS_MAX_XATTR_NAME)
		return EINVAL;

	xname = concat("x:", name);
	if (xname == NULL)
		return ENOMEM;

	/** set akey as the xattr name */
	d_iov_set(&iod.iod_name, xname, strlen(xname));
	iod.iod_nr    = 1;
	iod.iod_recxs = NULL;
	iod.iod_type  = DAOS_IOD_SINGLE;

	if (value != NULL && *size) {
		iod.iod_size = *size;
		d_iov_set(&sg_iov, value, *size);
		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs   = &sg_iov;
	} else {
		iod.iod_size = DAOS_REC_ANY;
	}

retry:
	if (DFS_IS_HARDLINK(obj->mode)) {
		/** For hardlinks, xattrs are stored in GIT with OID as dkey */
		if (!daos_handle_is_valid(dfs->git_oh))
			D_GOTO(out, rc = ENOTSUP);
		d_iov_set(&dkey, &obj->oid, sizeof(daos_obj_id_t));

		if (value != NULL && *size)
			rc = daos_obj_fetch(dfs->git_oh, dfs->th, DAOS_COND_AKEY_FETCH, &dkey, 1,
					    &iod, &sgl, NULL, NULL);
		else
			rc = daos_obj_fetch(dfs->git_oh, dfs->th, DAOS_COND_AKEY_FETCH, &dkey, 1,
					    &iod, NULL, NULL, NULL);
		if (rc) {
			DL_CDEBUG(rc == -DER_NONEXIST, DLOG_DBG, DLOG_ERR, rc,
				  "Failed to fetch xattr '%s' from GIT", name);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	} else {
		/** Open parent object and get xattr from the entry of the object */
		rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RO, &oh, NULL);
		if (rc)
			D_GOTO(out, rc = daos_der2errno(rc));

		/** set dkey as the entry name */
		d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

		if (value != NULL && *size)
			rc = daos_obj_fetch(oh, dfs->th, DAOS_COND_AKEY_FETCH, &dkey, 1, &iod, &sgl,
					    NULL, NULL);
		else
			rc = daos_obj_fetch(oh, dfs->th, DAOS_COND_AKEY_FETCH, &dkey, 1, &iod, NULL,
					    NULL, NULL);

		if (rc == -DER_NONEXIST && S_ISREG(obj->mode)) {
			/*
			 * Since this function deals with dfs_obj, the hardlink information in its
			 * mode field may be outdated. If xattr name does not exist in dentry then
			 * check whether the file is already converted into a hardlink. If so,
			 * update the mode with hardlink info. Retry if the hardlink bit is set.
			 */
			int rc2 =
			    fetch_entry(dfs->layout_v, oh, dfs->th, obj->name, strlen(obj->name),
					false, &exists, &entry, 0, NULL, NULL, NULL);
			daos_obj_close(oh, NULL);
			oh = DAOS_HDL_INVAL;
			if (rc2) {
				D_ERROR("Failed to fetch entry '%s' (%d)\n", obj->name, rc2);
				D_GOTO(out, rc = rc2);
			}
			if (!exists)
				D_GOTO(out, rc = daos_der2errno(rc));
			if (DFS_IS_HARDLINK(entry.mode)) {
				/** set hardlink bit; retry from GIT */
				dfs_set_hardlink(&obj->mode);
				goto retry;
			}
			/** genuine miss on a regular file: convert to POSIX errno */
			D_GOTO(out, rc = daos_der2errno(rc));
		}
		if (rc) {
			DL_CDEBUG(rc == -DER_NONEXIST, DLOG_DBG, DLOG_ERR, rc,
				  "Failed to fetch xattr '%s'", name);
			daos_obj_close(oh, NULL);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
		daos_obj_close(oh, NULL);
	}

	*size = iod.iod_size;
	DFS_OP_STAT_INCR(dfs, DOS_GETXATTR);

out:
	D_FREE(xname);
	if (rc == ENOENT)
		rc = ENODATA;
	return rc;
}

int
dfs_removexattr(dfs_t *dfs, dfs_obj_t *obj, const char *name)
{
	char            *xname = NULL;
	daos_key_t       dkey, akey;
	daos_handle_t    oh = DAOS_HDL_INVAL;
	uint64_t         cond;
	d_sg_list_t      sgl;
	d_iov_t          sg_iovs[2];
	daos_iod_t       iod;
	daos_recx_t      recxs[2];
	struct timespec  now;
	struct dfs_entry entry = {0};
	bool             exists;
	int              rc;

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

	/** set akey as the xattr name */
	d_iov_set(&akey, xname, strlen(xname));

	/** setup iod/sgl for ctime update (shared by both paths) */
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_recxs   = recxs;
	iod.iod_type    = DAOS_IOD_ARRAY;
	iod.iod_size    = 1;
	iod.iod_nr      = 2;
	recxs[0].rx_idx = CTIME_IDX;
	recxs[0].rx_nr  = sizeof(uint64_t);
	recxs[1].rx_idx = CTIME_NSEC_IDX;
	recxs[1].rx_nr  = sizeof(uint64_t);
	sgl.sg_nr       = 2;
	sgl.sg_nr_out   = 0;
	sgl.sg_iovs     = &sg_iovs[0];

retry:
	cond = DAOS_COND_DKEY_UPDATE | DAOS_COND_PUNCH;

	if (DFS_IS_HARDLINK(obj->mode)) {
		/** For hardlinks, xattrs are stored in GIT with OID as dkey */
		if (!daos_handle_is_valid(dfs->git_oh))
			D_GOTO(out_free, rc = ENOTSUP);
		d_iov_set(&dkey, &obj->oid, sizeof(daos_obj_id_t));

		rc = daos_obj_punch_akeys(dfs->git_oh, DAOS_TX_NONE, cond, &dkey, 1, &akey, NULL);
		if (rc) {
			D_CDEBUG(rc == -DER_NONEXIST, DLOG_DBG, DLOG_ERR,
				 "Failed to punch extended attribute '%s' from GIT\n", name);
			D_GOTO(out_free, rc = daos_der2errno(rc));
		}

		rc = clock_gettime(CLOCK_REALTIME, &now);
		if (rc)
			D_GOTO(out_free, rc = errno);
		d_iov_set(&sg_iovs[0], &now.tv_sec, sizeof(uint64_t));
		d_iov_set(&sg_iovs[1], &now.tv_nsec, sizeof(uint64_t));

		/** update ctime in GIT */
		rc = daos_obj_update(dfs->git_oh, DAOS_TX_NONE, DAOS_COND_DKEY_UPDATE, &dkey, 1,
				     &iod, &sgl, NULL);
		if (rc) {
			D_ERROR("Failed to update ctime in GIT, " DF_RC "\n", DP_RC(rc));
			D_GOTO(out_free, rc = daos_der2errno(rc));
		}
	} else {
		/** Open parent object and remove xattr from the entry of the object */
		rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RW, &oh, NULL);
		if (rc)
			D_GOTO(out_free, rc = daos_der2errno(rc));

		/** set dkey as the entry name */
		d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

		rc = daos_obj_punch_akeys(oh, DAOS_TX_NONE, cond, &dkey, 1, &akey, NULL);
		if (rc == -DER_NONEXIST && S_ISREG(obj->mode)) {
			/*
			 * Since this function deals with dfs_obj, the hardlink information in its
			 * mode field may be outdated. If xattr name does not exist in dentry then
			 * check whether the file is already converted into a hardlink. If so,
			 * update the mode with hardlink info. Retry if the hardlink bit is set.
			 */
			int rc2 = fetch_entry(dfs->layout_v, oh, DAOS_TX_NONE, obj->name,
					      strlen(obj->name), false, &exists, &entry, 0, NULL,
					      NULL, NULL);
			daos_obj_close(oh, NULL);
			oh = DAOS_HDL_INVAL;
			if (rc2) {
				D_ERROR("Failed to fetch entry '%s' (%d)\n", obj->name, rc2);
				D_GOTO(out_free, rc = rc2);
			}
			if (!exists)
				D_GOTO(out_free, rc = daos_der2errno(rc));
			if (DFS_IS_HARDLINK(entry.mode)) {
				/** entry became a hardlink; retry from GIT */
				dfs_set_hardlink(&obj->mode);
				goto retry;
			}
			/** genuine miss on a regular file: convert to POSIX errno */
			D_GOTO(out_free, rc = daos_der2errno(rc));
		}
		if (rc) {
			D_CDEBUG(rc == -DER_NONEXIST, DLOG_DBG, DLOG_ERR,
				 "Failed to punch extended attribute '%s'\n", name);
			daos_obj_close(oh, NULL);
			D_GOTO(out_free, rc = daos_der2errno(rc));
		}

		rc = clock_gettime(CLOCK_REALTIME, &now);
		if (rc) {
			daos_obj_close(oh, NULL);
			D_GOTO(out_free, rc = errno);
		}
		d_iov_set(&sg_iovs[0], &now.tv_sec, sizeof(uint64_t));
		d_iov_set(&sg_iovs[1], &now.tv_nsec, sizeof(uint64_t));

		/** update ctime */
		rc = daos_obj_update(oh, DAOS_TX_NONE, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl,
				     NULL);
		if (rc) {
			D_ERROR("Failed to update ctime, " DF_RC "\n", DP_RC(rc));
			daos_obj_close(oh, NULL);
			D_GOTO(out_free, rc = daos_der2errno(rc));
		}
		daos_obj_close(oh, NULL);
	}

	DFS_OP_STAT_INCR(dfs, DOS_RMXATTR);
out_free:
	D_FREE(xname);
	return rc;
}

int
dfs_listxattr(dfs_t *dfs, dfs_obj_t *obj, char *list, daos_size_t *size)
{
	daos_key_t       dkey;
	daos_handle_t    oh = DAOS_HDL_INVAL;
	daos_handle_t    list_oh;
	daos_key_desc_t  kds[ENUM_DESC_NR];
	daos_anchor_t    anchor = {0};
	daos_size_t      list_size, ret_size;
	char            *ptr_list;
	struct dfs_entry entry = {0};
	bool             exists;
	bool             is_hardlink;
	int              rc = 0;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL)
		return EINVAL;
	if (size == NULL)
		return EINVAL;

retry:
	list_size   = *size;
	ret_size    = 0;
	ptr_list    = list;
	is_hardlink = DFS_IS_HARDLINK(obj->mode);
	memset(&anchor, 0, sizeof(anchor));

	if (is_hardlink) {
		/** For hardlinks, xattrs are stored in GIT with OID as dkey */
		if (!daos_handle_is_valid(dfs->git_oh))
			return ENOTSUP;
		d_iov_set(&dkey, &obj->oid, sizeof(daos_obj_id_t));
		list_oh = dfs->git_oh;
	} else {
		/** Open parent object and list from entry */
		rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RO, &oh, NULL);
		if (rc)
			return daos_der2errno(rc);
		/** set dkey as the entry name */
		d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));
		list_oh = oh;
	}

	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t    number = ENUM_DESC_NR;
		uint32_t    i;
		d_iov_t     iov;
		char        enum_buf[ENUM_XDESC_BUF] = {0};
		d_sg_list_t sgl;
		char       *ptr;

		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		d_iov_set(&iov, enum_buf, ENUM_XDESC_BUF);
		sgl.sg_iovs = &iov;

		rc = daos_obj_list_akey(list_oh, dfs->th, &dkey, &number, kds, &sgl, &anchor, NULL);
		if (rc) {
			if (!is_hardlink)
				daos_obj_close(oh, NULL);
			D_GOTO(out, rc = daos_der2errno(rc));
		}

		if (number == 0)
			continue;

		for (ptr = enum_buf, i = 0; i < number; i++) {
			if (kds[i].kd_key_len < 2 || strncmp("x:", ptr, 2) != 0) {
				ptr += kds[i].kd_key_len;
				continue;
			}

			ret_size += kds[i].kd_key_len - 1;

			if (list == NULL) {
				ptr += kds[i].kd_key_len;
				continue;
			}
			if (list_size < kds[i].kd_key_len - 2) {
				ptr += kds[i].kd_key_len;
				continue;
			}

			memcpy(ptr_list, ptr + 2, kds[i].kd_key_len - 2);
			ptr_list[kds[i].kd_key_len - 2] = '\0';
			list_size -= kds[i].kd_key_len - 1;
			ptr_list += kds[i].kd_key_len - 1;
			ptr += kds[i].kd_key_len;
		}
	}

	/*
	 * Since this function deals with dfs_obj, the hardlink information in its
	 * mode field may be outdated. If list xattr does not return any xattr name,
	 * check and update the mode with hardlink info. Retry if the hardlink bit is set.
	 */
	if (!ret_size && !is_hardlink && S_ISREG(obj->mode)) {
		rc = fetch_entry(dfs->layout_v, oh, dfs->th, obj->name, strlen(obj->name), false,
				 &exists, &entry, 0, NULL, NULL, NULL);
		daos_obj_close(oh, NULL);
		oh = DAOS_HDL_INVAL;
		if (rc) {
			D_ERROR("Failed to fetch entry '%s' (%d)\n", obj->name, rc);
			D_GOTO(out, rc);
		}
		if (exists && DFS_IS_HARDLINK(entry.mode)) {
			/** set hardlink bit; retry listing from GIT */
			dfs_set_hardlink(&obj->mode);
			goto retry;
		}
	} else if (!is_hardlink) {
		daos_obj_close(oh, NULL);
		oh = DAOS_HDL_INVAL;
	}

	*size = ret_size;
	DFS_OP_STAT_INCR(dfs, DOS_LSXATTR);
out:
	return rc;
}
