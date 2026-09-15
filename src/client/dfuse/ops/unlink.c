/**
 * (C) Copyright 2016-2023 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

/* Handle removal of one hardlink where the file still exists because other links remain.  The file
 * object was not deleted, so unlike dfuse_oid_unlinked() the inode is left intact and not marked
 * unlinked - just reply to the kernel, which already dropped the removed name.
 */
void
dfuse_hardlink_removed(struct dfuse_info *dfuse_info, fuse_req_t req, daos_obj_id_t *oid,
		       struct dfuse_inode_entry *parent, const char *name)
{
	struct dfuse_inode_entry *ie;
	fuse_ino_t                ino;

	dfuse_compute_inode(parent->ie_dfs, oid, &ino);

	ie = dfuse_inode_lookup(dfuse_info, ino);
	if (ie) {
		DFUSE_TRA_DEBUG(ie, "Hardlink " DF_DE " removed, file still exists", DP_DE(name));
		dfuse_ie_dentry_remove(ie, parent->ie_stat.st_ino, name);
		dfuse_inode_decref(dfuse_info, ie);
	}

	DFUSE_REPLY_ZERO(parent, req);
}

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
	struct dfuse_dentry       released = {0};
	fuse_ino_t                ino;
	fuse_ino_t                parent_ino = parent->ie_stat.st_ino;

	D_INIT_LIST_HEAD(&released.dd_list);

	dfuse_compute_inode(parent->ie_dfs, oid, &ino);

	ie = dfuse_inode_lookup(dfuse_info, ino);
	if (!ie) {
		DFUSE_REPLY_ZERO(parent, req);
		return;
	}

	DFUSE_TRA_DEBUG(ie, "Setting inode as deleted");

	ie->ie_unlinked = true;

	/* Snapshot every cached name so all of them can be removed from the kernel. */
	dfuse_ie_dentry_snapshot(ie, &released);

	/* At this point the request is complete so the kernel is free to drop any refs on parent
	 * so it should not be accessed.
	 */
	DFUSE_REPLY_ZERO(parent, req);

	/* Invalidate cached data/attrs and delete every known name, except the one the kernel
	 * already handled via this unlink/rename.
	 */
	dfuse_ie_inode_delete(dfuse_info, ie, &released, parent_ino, name);

	/* Drop the ref again */
	dfuse_inode_decref(dfuse_info, ie);
}

void
dfuse_cb_unlink(fuse_req_t req, struct dfuse_inode_entry *parent, const char *name)
{
	struct dfuse_info *dfuse_info = fuse_req_userdata(req);
	int                rc;
	daos_obj_id_t      oid     = {};
	bool               deleted = true;

	dfuse_cache_evict_dir(dfuse_info, parent);

	rc = dfs_remove_internal(parent->ie_dfs->dfs_ns, parent->ie_obj, name, false, &oid,
				 &deleted);
	if (rc != 0) {
		DFUSE_REPLY_ERR_RAW(parent, req, rc);
		return;
	}

	D_ASSERT(oid.lo || oid.hi);

	if (!deleted) {
		dfuse_hardlink_removed(dfuse_info, req, &oid, parent, name);
		return;
	}

	dfuse_oid_unlinked(dfuse_info, req, &oid, parent, name);
}
