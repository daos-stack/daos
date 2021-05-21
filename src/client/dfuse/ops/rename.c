/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_rename(fuse_req_t req, struct dfuse_inode_entry *parent,
		const char *name, struct dfuse_inode_entry *newparent,
		const char *newname, unsigned int flags)
{
	struct dfuse_projection_info	*fs_handle;
	int				rc;

	fs_handle = fuse_req_userdata(req);

	if (flags != 0)
		D_GOTO(out, rc = ENOTSUP);

	if (!newparent)
		newparent = parent;

	rc = dfs_move(parent->ie_dfs->dfs_ns, parent->ie_obj, (char *)name,
		      newparent->ie_obj, (char *)newname, NULL);
	if (rc)
		D_GOTO(out, rc);

	DFUSE_TRA_INFO(parent, "Renamed %s to %s in %p",
		       name, newname, newparent);

	DFUSE_REPLY_ZERO(parent, req);

	if (parent->ie_dfs->dfc_dentry_timeout ||
		parent->ie_dfs->dfc_dentry_dir_timeout ||
		parent->ie_dfs->dfc_ndentry_timeout) {
		/* The caller has a reference on newparent */
		rc = fuse_lowlevel_notify_inval_entry(fs_handle->dpi_info->di_session,
						      newparent->ie_stat.st_ino,
						      newname, strnlen(newname, NAME_MAX));
		if (rc)
			DFUSE_TRA_ERROR(parent,
					"inval_entry failed %d", rc);
	}
	return;

out:
	DFUSE_REPLY_ERR_RAW(parent, req, rc);
}
