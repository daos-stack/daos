/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_getattr(fuse_req_t req, struct dfuse_inode_entry *ie)
{
	struct stat	attr = {};
	int		rc;

	if (S_ISDIR(ie->ie_stat.st_mode) || !ie->ie_parent_ie) {
		rc = dfs_ostat(ie->ie_dfs->dfs_ns, ie->ie_obj, &stat);
		if (rc != 0)
			D_GOTO(err, rc);
	} else {
		rc = dfs_statx(ie->ie_dfs->dfs_ns, ie->ie_parent_ie->ie_obj,
			       ie->ie_name, &stat, &ie->ie_oid,
			       0, NULL, NULL, NULL);
		if (rc != 0)
			D_GOTO(err, rc);
	}

	if (ie->ie_unlinked) {
		DFUSE_TRA_DEBUG(ie, "File is unlinked, returning most recent data");
		DFUSE_REPLY_ATTR(ie, req, &ie->ie_stat);
		return;
	}

	rc = dfs_ostat(ie->ie_dfs->dfs_ns, ie->ie_obj, &attr);
	if (rc != 0)
		D_GOTO(err, rc);

	/* Copy the inode number from the inode struct, to avoid having to
	 * recompute it each time.
	 */
	attr.st_ino = ie->ie_stat.st_ino;

	/* Update the size as dfuse knows about it for future use.
	 *
	 * This size is used for detecting reads of zerod data for files
	 * so do not shrink the filesize here, potentially this getattr
	 * can race with a write, where the write would set the size, this
	 * getattr can fetch the stale size and then the write callback
	 * can complete, leaving dfuse thinking the filesize is smaller
	 * than it is.  As such do not shrink the filesize here to avoid
	 * DAOS-8333
	 */
	if (attr.st_size > ie->ie_stat.st_size)
		ie->ie_stat.st_size = attr.st_size;

	DFUSE_REPLY_ATTR(ie, req, &attr);

	return;
err:
	DFUSE_REPLY_ERR_RAW(ie, req, rc);
}
