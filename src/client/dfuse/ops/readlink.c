/**
 * (C) Copyright 2016-2023 Intel Corporation.
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

	inode = dfuse_inode_lookup_nf(dfuse_info, ino);

	DFUSE_IE_STAT_ADD(inode, DS_READLINK);

	rc = dfs_get_symlink_value(inode->ie_obj, NULL, &size);
	if (rc)
		D_GOTO(err, rc);

	D_ALLOC(buf, size);
	if (!buf)
		D_GOTO(err, rc = ENOMEM);

	rc = dfs_get_symlink_value(inode->ie_obj, buf, &size);
	if (rc)
		D_GOTO(err, rc);

	DFUSE_REPLY_READLINK(inode, req, buf);

	D_FREE(buf);
	return;
err:
	DFUSE_REPLY_ERR_RAW(inode, req, rc);
	D_FREE(buf);
}
