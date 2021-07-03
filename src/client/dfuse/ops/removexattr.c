/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_removexattr(fuse_req_t req, struct dfuse_inode_entry *inode,
		     const char *name)
{
	int rc;

	DFUSE_TRA_DEBUG(inode, "Attribute '%s'", name);

	rc = dfs_removexattr(inode->ie_dfs->dfs_ns, inode->ie_obj, name);
	if (rc == 0) {
		DFUSE_REPLY_ZERO(inode, req);
		return;
	}

	DFUSE_REPLY_ERR_RAW(inode, req, rc);
}
