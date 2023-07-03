/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_readlink(fuse_req_t req, fuse_ino_t ino)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_inode_entry *inode;
	char                     *buf  = NULL;
	size_t                    size = 0;
	int                       rc;

	inode = dfuse_inode_lookup(dfuse_info, ino);
	if (!inode) {
		DFUSE_TRA_ERROR(dfuse_info, "Failed to find inode %#lx", ino);
		D_GOTO(err, rc = EIO);
	}

	rc = dfs_get_symlink_value(inode->ie_obj, NULL, &size);
	if (rc)
		D_GOTO(release, rc);

	D_ALLOC(buf, size);
	if (!buf)
		D_GOTO(release, rc = ENOMEM);

	rc = dfs_get_symlink_value(inode->ie_obj, buf, &size);
	if (rc)
		D_GOTO(release, rc);

	DFUSE_REPLY_READLINK(inode, req, buf);

	dfuse_inode_decref(dfuse_info, inode);

	D_FREE(buf);
	return;
release:
	dfuse_inode_decref(dfuse_info, inode);
err:
	DFUSE_REPLY_ERR_RAW(dfuse_info, req, rc);
	D_FREE(buf);
}
