/**
 * (C) Copyright 2016-2023 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"
#include <daos/dfs_lib_int.h>

/* Handle a file that has been moved.
 *
 * Dfuse may not be aware of this file, but if it is find the inode and update it for the new
 * location.
 */

static void
dfuse_oid_moved(struct dfuse_info *dfuse_info, daos_obj_id_t *oid, struct dfuse_inode_entry *parent,
		const char *name, struct dfuse_inode_entry *newparent, const char *newname)
{
	struct dfuse_inode_entry *ie;
	struct dfuse_dentry       released = {0};
	ino_t                     ino;

	D_INIT_LIST_HEAD(&released.dd_list);

	dfuse_compute_inode(parent->ie_dfs, oid, &ino);

	DFUSE_TRA_DEBUG(dfuse_info, "Renamed file was %#lx", ino);

	ie = dfuse_inode_lookup(dfuse_info, ino);
	if (!ie)
		return;

	/* Replace old dentry with new, releasing any stale dentries */
	dfuse_ie_dentry_replace(ie, parent->ie_stat.st_ino, name, newparent->ie_stat.st_ino,
				newname, &released);

	/* Set the new parent and name in the DFS object */
	dfs_update_parentfd(ie->ie_obj, newparent->ie_obj, newname);

	/* Invalidate any released dentries from the cache */
	dfuse_ie_dentry_inval(dfuse_info, &released);

	/* Drop the ref again */
	dfuse_inode_decref(dfuse_info, ie);
}

void
dfuse_cb_rename(fuse_req_t req, struct dfuse_inode_entry *parent,
		const char *name, struct dfuse_inode_entry *newparent,
		const char *newname, unsigned int flags)
{
	struct dfuse_info *dfuse_info = fuse_req_userdata(req);
	daos_obj_id_t      moid       = {};
	daos_obj_id_t      oid        = {};
	bool               deleted    = true;
	int                rc;

	if (flags != 0) {
#ifdef RENAME_NOREPLACE
		if (flags != RENAME_NOREPLACE) {
			if (flags & RENAME_EXCHANGE)
				DFUSE_TRA_DEBUG(parent, "Unsupported flag RENAME_EXCHANGE");
			else
				DFUSE_TRA_INFO(parent, "Unsupported flags %#x", flags);
			D_GOTO(out, rc = ENOTSUP);
		}
#else
		DFUSE_TRA_INFO(parent, "Unsupported flags %#x", flags);
		D_GOTO(out, rc = ENOTSUP);
#endif
	}

	dfuse_cache_evict_dir(dfuse_info, parent);

	if (newparent) {
		dfuse_cache_evict_dir(dfuse_info, newparent);
	} else {
		newparent = parent;
	}

	rc = dfs_move_internal(parent->ie_dfs->dfs_ns, flags, parent->ie_obj, (char *)name,
			       newparent->ie_obj, (char *)newname, &moid, &oid, &deleted);
	if (rc)
		D_GOTO(out, rc);

	DFUSE_TRA_DEBUG(newparent, "Renamed " DF_DE " to " DF_DE, DP_DE(name), DP_DE(newname));

	/* update moid */
	dfuse_oid_moved(dfuse_info, &moid, parent, name, newparent, newname);

	/* Check if a file was unlinked and see if anything needs updating */
	if (oid.lo || oid.hi) {
		if (!deleted) {
			dfuse_hardlink_removed(dfuse_info, req, &oid, newparent, newname);
		} else {
			dfuse_oid_removed(dfuse_info, req, &oid, newparent, newname);
		}
	} else {
		DFUSE_REPLY_ZERO(newparent, req);
	}

	return;

out:
	DFUSE_REPLY_ERR_RAW(parent, req, rc);
}
