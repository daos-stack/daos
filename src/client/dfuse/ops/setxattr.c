/**
 * (C) Copyright 2019-2022 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

#include <daos_uns.h>

#define ACL_ACCESS	"system.posix_acl_access"
#define ACL_DEFAULT	"system.posix_acl_default"

void
dfuse_cb_setxattr(fuse_req_t req, struct dfuse_inode_entry *inode,
		  const char *name, const char *value, size_t size,
		  int flags)
{
	int	rc;
	bool	duns_attr = false;

	DFUSE_TRA_DEBUG(inode, "Attribute '%s'", name);

	if (strncmp(name, DUNS_XATTR_NAME, sizeof(DUNS_XATTR_NAME)) == 0) {
		struct duns_attr_t dattr = {};

		if (inode->ie_root) {
			DFUSE_TRA_WARNING(inode, "Attempt to set duns attr on container root");
			D_GOTO(err, rc = EINVAL);
		}

		/* Just check this is valid, but don't do anything with it */
		rc = duns_parse_attr((char *)value, size, &dattr);
		if (rc)
			D_GOTO(err, rc);
		duns_destroy_attr(&dattr);
		duns_attr = true;
	}

	if (strncmp(name, ACL_ACCESS, sizeof(ACL_ACCESS)) == 0)
		D_GOTO(err, rc = ENOTSUP);

	if (strncmp(name, ACL_DEFAULT, sizeof(ACL_DEFAULT)) == 0)
		D_GOTO(err, rc = ENOTSUP);

	rc = dfs_setxattr(inode->ie_dfs->dfs_ns, inode->ie_obj, name, value, size, flags);
	if (rc == 0) {
		/* Setting a UNS entry point renumbers the inode, so invalidate the dentry to
		 * force a fresh lookup.  Safe out of order: a racing lookup holds the parent
		 * shared while it instantiates, the invalidation takes it exclusive, so a stale
		 * dentry is always in place before the queued invalidation kills it.
		 */
		if (duns_attr && inode->ie_dfs->dfc_dentry_dir_timeout > 0) {
			struct dfuse_info *dfuse_info = fuse_req_userdata(req);

			dfuse_notify_inval_entry(dfuse_info, inode->ie_parent, inode->ie_name);
		}
		DFUSE_REPLY_ZERO(inode, req);
		return;
	}
err:
	DFUSE_REPLY_ERR_RAW(inode, req, rc);
}
