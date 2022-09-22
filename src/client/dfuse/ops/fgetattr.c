/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_getattr(fuse_req_t req, struct dfuse_inode_entry *ie, struct dht_call *save)
{
	struct dfuse_projection_info *fs_handle = fuse_req_userdata(req);
	struct stat                   attr      = {};
	int                           rc;

	if (ie->ie_unlinked) {
		DFUSE_TRA_DEBUG(ie, "File is unlinked, returning most recent data");
		DFUSE_REPLY_ATTR(ie, req, &ie->ie_stat);
		if (save)
			dh_hash_decrefx(fs_handle, save);
		return;
	}

	rc = dfs_ostat(ie->ie_dfs->dfs_ns, ie->ie_obj, &attr);
	if (rc != 0)
		D_GOTO(err, rc);

	attr.st_ino = ie->ie_stat.st_ino;

	ie->ie_stat = attr;

	if (save)
		dh_hash_try_decrefx(fs_handle, save);

	DFUSE_REPLY_ATTR(ie, req, &attr);

	if (save)
		dh_hash_decrefx(fs_handle, save);

	return;
err:
	DFUSE_REPLY_ERR_RAW(ie, req, rc);
	if (save)
		dh_hash_decrefx(fs_handle, save);
}
