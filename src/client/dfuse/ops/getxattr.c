/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_getxattr(fuse_req_t req, struct dfuse_inode_entry *inode,
		  const char *name, size_t size)
{
	size_t out_size = 0;
	char *value = NULL;
	int rc;

	DFUSE_TRA_DEBUG(inode, "Attribute '%s'", name);

	rc = dfs_getxattr(inode->ie_dfs->dfs_ns, inode->ie_obj, name, NULL,
			  &out_size);
	if (rc != 0)
		D_GOTO(err, rc);

	if (size == 0) {
		fuse_reply_xattr(req, out_size);
		return;
	}

	if (size < out_size)
		D_GOTO(err, rc = ERANGE);

	D_ALLOC(value, out_size);
	if (!value)
		D_GOTO(err, rc = ENOMEM);

	rc = dfs_getxattr(inode->ie_dfs->dfs_ns, inode->ie_obj, name, value,
			  &out_size);
	if (rc != 0)
		D_GOTO(free, rc);

	DFUSE_REPLY_BUF(inode, req, value, out_size);
	D_FREE(value);
	return;
free:
	D_FREE(value);
err:
	DFUSE_REPLY_ERR_RAW(inode, req, rc);
}
