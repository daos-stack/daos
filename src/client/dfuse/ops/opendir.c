/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_opendir(fuse_req_t req, struct dfuse_inode_entry *ie, struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl *oh     = NULL;
	struct fuse_file_info fi_out = {0};
	int                   rc;

	D_ALLOC_PTR(oh);
	if (!oh)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(oh, ie, "open handle");

	/** duplicate the file handle for the fuse handle */
	rc = dfs_dup(ie->ie_dfs->dfs_ns, ie->ie_obj, fi->flags, &oh->doh_obj);
	if (rc)
		D_GOTO(err, rc);

	oh->doh_dfs = ie->ie_dfs->dfs_ns;
	oh->doh_ie  = ie;

	fi_out.fh = (uint64_t)oh;

#if HAVE_CACHE_READDIR
	if (ie->ie_dfs->dfc_dentry_timeout > 0)
		fi_out.cache_readdir = 1;
#endif

	atomic_fetch_add_relaxed(&ie->ie_open_count, 1);

	DFUSE_REPLY_OPEN(oh, req, &fi_out);
	return;
err:
	D_FREE(oh);
	DFUSE_REPLY_ERR_RAW(ie, req, rc);
}

void
dfuse_cb_releasedir(fuse_req_t req, struct dfuse_inode_entry *ino, struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl *oh = (struct dfuse_obj_hdl *)fi->fh;
	int                   rc;
	uint                  il_count;

	/* Perform the opposite of what the ioctl call does, always change the open handle count
	 * but the inode only tracks number of open handles with non-zero ioctl counts
	 */
	il_count = atomic_load_relaxed(&oh->doh_il_calls);

	if (il_count != 0) {
		atomic_fetch_sub_relaxed(&oh->doh_ie->ie_il_count, 1);
	}
	atomic_fetch_sub_relaxed(&oh->doh_ie->ie_open_count, 1);

	rc = dfs_release(oh->doh_obj);
	if (rc == 0)
		DFUSE_REPLY_ZERO(oh, req);
	else
		DFUSE_REPLY_ERR_RAW(oh, req, rc);
	D_FREE(oh->doh_dre);
	D_FREE(oh);
};
