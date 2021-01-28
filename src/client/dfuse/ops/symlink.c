/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_symlink(fuse_req_t req, const char *link,
		 struct dfuse_inode_entry *parent,
		 const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*ie = NULL;
	int rc;

	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(ie, parent, "inode");

	rc = dfs_open_stat(parent->ie_dfs->dfs_ns, parent->ie_obj, name,
			   S_IFLNK, O_CREAT | O_RDWR | O_EXCL,
			   0, 0, link, &ie->ie_obj, &ie->ie_stat);
	if (rc != 0)
		D_GOTO(err, rc);

	DFUSE_TRA_INFO(ie, "obj is %p", ie->ie_obj);

	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_name[NAME_MAX] = '\0';
	ie->ie_parent = parent->ie_stat.st_ino;
	ie->ie_dfs = parent->ie_dfs;
	atomic_store_relaxed(&ie->ie_ref, 1);

	dfuse_reply_entry(fs_handle, ie, NULL, true, req);

	return;
err:
	DFUSE_REPLY_ERR_RAW(ie, req, rc);
	D_FREE(ie);
}
