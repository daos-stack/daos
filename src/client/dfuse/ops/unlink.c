/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_unlink(fuse_req_t req, struct dfuse_inode_entry *parent,
		const char *name)
{
	int				rc;

	rc = dfs_remove(parent->ie_dfs->dfs_ns, parent->ie_obj, name, false,
			NULL);
	if (rc == 0)
		DFUSE_REPLY_ZERO(parent, req);
	else
		DFUSE_REPLY_ERR_RAW(parent, req, rc);
}
