/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_mknod(fuse_req_t req, struct dfuse_inode_entry *parent, const char *name, mode_t mode)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*ie;
	int				rc;

	DFUSE_TRA_DEBUG(parent, "Parent:%lu '%s'", parent->ie_stat.st_ino, name);

	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(ie, parent, "inode");

	DFUSE_TRA_DEBUG(ie, "file '%s' mode 0%o", name, mode);

	rc = _dfuse_mode_update(req, parent, &mode);
	if (rc != 0)
		D_GOTO(err, rc);

	rc = dfs_open_stat(parent->ie_dfs->dfs_ns, parent->ie_obj, name, mode,
			   O_CREAT | O_EXCL | O_RDWR, 0, 0, NULL, &ie->ie_obj, &ie->ie_stat);
	if (rc)
		D_GOTO(err, rc);

	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_parent = parent->ie_stat.st_ino;
	ie->ie_dfs = parent->ie_dfs;
	ie->ie_truncated = false;
	atomic_store_relaxed(&ie->ie_ref, 1);

	LOG_MODES(ie, mode);

	dfs_obj2id(ie->ie_obj, &ie->ie_oid);

	dfuse_compute_inode(ie->ie_dfs, &ie->ie_oid, &ie->ie_stat.st_ino);

	/* Return the new inode data, and keep the parent ref */
	dfuse_reply_entry(fs_handle, ie, NULL, true, req);

	return;
err:
	DFUSE_REPLY_ERR_RAW(parent, req, rc);
	D_FREE(ie);
}
