/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*ie;
	d_list_t			*rlink;
	struct dfuse_obj_hdl		*oh = NULL;
	struct fuse_file_info	        fi_out = {0};
	int				rc;

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &ino, sizeof(ino));
	if (!rlink) {
		DFUSE_REPLY_ERR_RAW(fs_handle, req, ENOENT);
		return;
	}
	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	D_ALLOC_PTR(oh);
	if (!oh)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(oh, ie, "open handle");

	/** duplicate the file handle for the fuse handle */
	rc = dfs_dup(ie->ie_dfs->dfs_ns, ie->ie_obj, fi->flags,
		     &oh->doh_obj);
	if (rc)
		D_GOTO(err, rc);

	oh->doh_dfs = ie->ie_dfs->dfs_ns;
	oh->doh_ie = ie;

	if (fs_handle->dpi_info->di_direct_io) {
		if (ie->ie_dfs->dfs_attr_timeout == 0) {
			fi_out.direct_io = 1;
		} else {
			if (fi->flags & O_DIRECT)
				fi_out.direct_io = 1;
		}
	}

	fi_out.fh = (uint64_t)oh;

	LOG_FLAGS(ie, fi->flags);

	/*
	 * dfs_dup() just locally duplicates the file handle. If we have
	 * O_TRUNC flag, we need to truncate the file manually.
	 */
	if (fi->flags & O_TRUNC) {
		rc = dfs_punch(ie->ie_dfs->dfs_ns, ie->ie_obj, 0,
			       DFS_MAX_FSIZE);
		if (rc)
			D_GOTO(err, rc);
	}

	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	DFUSE_REPLY_OPEN(oh, req, &fi_out);

	return;
err:
	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	D_FREE(oh);
	DFUSE_REPLY_ERR_RAW(ie, req, rc);
}

void
dfuse_cb_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl	*oh = (struct dfuse_obj_hdl *)fi->fh;
	int			rc;

	/** Files should not have readdir buffers */
	D_ASSERT(oh->doh_buf == NULL);

	rc = dfs_release(oh->doh_obj);
	if (rc == 0)
		DFUSE_REPLY_ZERO(oh, req);
	else
		DFUSE_REPLY_ERR_RAW(oh, req, rc);
	D_FREE(oh);
}
