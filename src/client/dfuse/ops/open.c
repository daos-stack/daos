/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_projection_info *fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry     *ie;
	d_list_t		     *rlink;
	struct dfuse_obj_hdl         *oh     = NULL;
	struct fuse_file_info         fi_out = {0};
	int                           rc;

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

	/* Upgrade fd permissions from O_WRONLY to O_RDWR if wb caching is
	 * enabled so the kernel can do read-modify-write
	 */
	if (ie->ie_dfs->dfc_data_caching && fs_handle->dpi_info->di_wb_cache &&
	    (fi->flags & O_ACCMODE) == O_WRONLY) {
		DFUSE_TRA_DEBUG(ie, "Upgrading fd to O_RDRW");
		fi->flags &= ~O_ACCMODE;
		fi->flags |= O_RDWR;
	}

	/** duplicate the file handle for the fuse handle */
	rc = dfs_dup(ie->ie_dfs->dfs_ns, ie->ie_obj, fi->flags, &oh->doh_obj);
	if (rc)
		D_GOTO(free, rc);

	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		oh->doh_writeable = true;
	oh->doh_dfs = ie->ie_dfs->dfs_ns;
	oh->doh_ie  = ie;

	if (ie->ie_dfs->dfc_data_caching) {
		if (fi->flags & O_DIRECT)
			fi_out.direct_io = 1;
	} else {
		fi_out.direct_io = 1;
	}

	if (ie->ie_dfs->dfc_direct_io_disable)
		fi_out.direct_io = 0;

	if (!fi_out.direct_io)
		oh->doh_caching = true;

	fi_out.fh = (uint64_t)oh;

	LOG_FLAGS(ie, fi->flags);

	/*
	 * dfs_dup() just locally duplicates the file handle. If we have
	 * O_TRUNC flag, we need to truncate the file manually.
	 */
	if (fi->flags & O_TRUNC) {
		rc = dfs_punch(ie->ie_dfs->dfs_ns, ie->ie_obj, 0, DFS_MAX_FSIZE);
		if (rc)
			D_GOTO(release, rc);
	}

	atomic_fetch_add_relaxed(&ie->ie_open_count, 1);

	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	DFUSE_REPLY_OPEN(oh, req, &fi_out);

	return;
release:
	dfuse_dfs_release(fs_handle, oh->doh_obj, (void **)&oh);
free:
	D_FREE(oh);
err:
	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	DFUSE_REPLY_ERR_RAW(ie, req, rc);
}

void
dfuse_cb_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_projection_info *fs_handle = fuse_req_userdata(req);
	struct dfuse_obj_hdl *oh = (struct dfuse_obj_hdl *)fi->fh;
	int                   rc;

	/* Perform the opposite of what the ioctl call does, always change the open handle count
	 * but the inode only tracks number of open handles with non-zero ioctl counts
	 */

	if (atomic_load_relaxed(&oh->doh_il_calls) != 0)
		atomic_fetch_sub_relaxed(&oh->doh_ie->ie_il_count, 1);
	atomic_fetch_sub_relaxed(&oh->doh_ie->ie_open_count, 1);

	rc = dfs_release(oh->doh_obj);
	if (rc == 0) {
		DFUSE_REPLY_ZERO(oh, req);
		D_FREE(oh);
	} else {
		DFUSE_REPLY_ERR_RAW(oh, req, rc);
		dfuse_dfs_release(fs_handle, oh->doh_obj, (void **)&oh);
	}
}
