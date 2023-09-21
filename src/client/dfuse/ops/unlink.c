/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

/* Handle a file that has been unlinked via dfuse.  This means that either a unlink or rename call
 * caused the file to be deleted.
 * Takes the oid of the deleted file, and the parent/name where the delete happened.
 *
 * Will always call DFUSE_REPLY_ZERO() after updating local state but before updating kernel.
 */
void
dfuse_oid_unlinked(struct dfuse_info *dfuse_info, fuse_req_t req, daos_obj_id_t *oid,
		   struct dfuse_inode_entry *parent, const char *name)
{
	struct dfuse_inode_entry *ie;
	int                       rc;
	fuse_ino_t                ino;

	dfuse_compute_inode(parent->ie_dfs, oid, &ino);

	ie = dfuse_inode_lookup(dfuse_info, ino);
	if (!ie) {
		DFUSE_REPLY_ZERO(parent, req);
		return;
	}

	DFUSE_TRA_DEBUG(ie, "Setting inode as deleted");

	ie->ie_unlinked = true;

	DFUSE_REPLY_ZERO(parent, req);

	/* If caching is enabled then invalidate the data and attribute caches.  As this came a
	 * unlink/rename call the kernel will have just done a lookup and knows what was likely
	 * unlinked so will destroy it anyway, but there is a race here so try and destroy it
	 * even though most of the time we expect this to fail.
	 */
	rc = fuse_lowlevel_notify_inval_inode(dfuse_info->di_session, ino, 0, 0);
	if (rc && rc != -ENOENT)
		DFUSE_TRA_ERROR(ie, "inval_inode() returned: %d (%s)", rc, strerror(-rc));

	/* If the kernel was aware of this inode at an old location then remove that which should
	 * trigger a forget call.  Checking the test logs shows that we do see the forget anyway
	 * for cases where the kernel knows which file it deleted.
	 */
	if ((ie->ie_parent != parent->ie_stat.st_ino) ||
		(strncmp(ie->ie_name, name, NAME_MAX) != 0)) {
		DFUSE_TRA_DEBUG(ie, "Telling kernel to forget %#lx " DF_DE, ie->ie_parent,
				DP_DE(ie->ie_name));

		rc = fuse_lowlevel_notify_delete(dfuse_info->di_session, ie->ie_parent, ino,
						 ie->ie_name, strnlen(ie->ie_name, NAME_MAX));
		if (rc && rc != -ENOENT)
			DFUSE_TRA_ERROR(ie, "notify_delete() returned: %d (%s)", rc, strerror(-rc));
	}

	/* Drop the ref again */
	dfuse_inode_decref(dfuse_info, ie);
}

void
dfuse_cb_unlink(fuse_req_t req, struct dfuse_inode_entry *parent, const char *name)
{
	struct dfuse_info *dfuse_info = fuse_req_userdata(req);
	int                rc;
	daos_obj_id_t      oid = {};

	dfuse_cache_evict_dir(dfuse_info, parent);

	rc = dfs_remove(parent->ie_dfs->dfs_ns, parent->ie_obj, name, false, &oid);
	if (rc != 0) {
		DFUSE_REPLY_ERR_RAW(parent, req, rc);
		return;
	}

	D_ASSERT(oid.lo || oid.hi);

	dfuse_oid_unlinked(dfuse_info, req, &oid, parent, name);
}
