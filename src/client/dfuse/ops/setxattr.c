/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

#include "daos_uns.h"

void
dfuse_cb_setxattr(fuse_req_t req, struct dfuse_inode_entry *inode,
		  const char *name, const char *value, size_t size,
		  int flags)
{
	int	rc;
	bool	duns_attr = false;

	DFUSE_TRA_DEBUG(inode, "Attribute '%s'", name);

	if (strcmp(name, DUNS_XATTR_NAME) == 0) {
		struct duns_attr_t	dattr = {};

		/* Just check this is valid, but don't do anything with it */
		rc = duns_parse_attr((char *)value, size, &dattr);
		if (rc)
			D_GOTO(err, rc);

		duns_attr = true;
	}

	rc = dfs_setxattr(inode->ie_dfs->dfs_ns, inode->ie_obj, name, value,
			  size, flags);
	if (rc == 0) {
		/* Optionally remove the dentry to force a new lookup on access.
		 * If the xattr is to set a UNS entry point, and dentry_dir
		 * caching is enabled then invalidate the dentry here, to force
		 * a lookup which will check the xattr and return the linked
		 * container.  The fuse header says this potentially deadlocks
		 * however it does appear to work, and calling this after the
		 * reply will introduce a race condition that future lookups
		 * will be skipped.
		 */
		if (duns_attr && inode->ie_dfs->dfc_dentry_dir_timeout > 0) {
			struct dfuse_projection_info *fs_handle;

			fs_handle = fuse_req_userdata(req);
			rc = fuse_lowlevel_notify_inval_entry(fs_handle->dpi_info->di_session,
							      inode->ie_parent,
							      inode->ie_name,
							      strnlen(inode->ie_name, NAME_MAX));
			DFUSE_TRA_INFO(inode, "inval_entry() rc is %d", rc);
		}
		DFUSE_REPLY_ZERO(inode, req);
		return;
	}
err:
	DFUSE_REPLY_ERR_RAW(inode, req, rc);
}
