/**
 * (C) Copyright 2016-2023 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"
#include <daos/dfs_lib_int.h>

/* Handle a hardlink being removed but the file still exists (other links remain).
 * Removes the dentry from the inode's tracking and replies to fuse.
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
		dfuse_ie_dentry_remove(ie, parent->ie_stat.st_ino, name);
		dfuse_inode_decref(dfuse_info, ie);
	}
	DFUSE_REPLY_ZERO(parent, req);
}

/* Handle a file that has been unlinked via dfuse.  This means that either a unlink or rename call
 * caused the file to be deleted.
 * Takes the oid of the deleted file, and the parent/name where the delete happened.
 * If deleted is true, the file was actually deleted (last link removed or regular file).
 * If deleted is false, only a hardlink was removed and the file still exists.
 *
 * Will always call DFUSE_REPLY_ZERO() after updating local state but before updating kernel.
 */
void
dfuse_oid_removed(struct dfuse_info *dfuse_info, fuse_req_t req, daos_obj_id_t *oid,
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
	/* Clear all dentries for deletion notification */
	dfuse_ie_dentry_clear(ie, &released);

	/* At this point the request is complete so the kernel is free to drop any refs on parent
	 * so it should not be accessed.
	 */
	DFUSE_REPLY_ZERO(parent, req);

	/* Delete all dentries from the kernel */
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

	dfuse_oid_removed(dfuse_info, req, &oid, parent, name);
}
