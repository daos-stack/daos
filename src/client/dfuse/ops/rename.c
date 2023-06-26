/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

/* Handle a file that has been moved.
 *
 * Dfuse may not be aware of this file, but if it is find the inode and update it for the new
 * location.
 */

static void
dfuse_oid_moved(struct dfuse_projection_info *fs_handle, daos_obj_id_t *oid,
		struct dfuse_inode_entry *parent, const char *name,
		struct dfuse_inode_entry *newparent,
		const char *newname)
{
	struct dfuse_inode_entry	*ie;
	d_list_t			*rlink;
	int				rc;
	ino_t ino;

	dfuse_compute_inode(parent->ie_dfs, oid, &ino);

	DFUSE_TRA_DEBUG(fs_handle, "Renamed file was %#lx", ino);

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &ino, sizeof(ino));
	if (!rlink)
		return;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	/* If the move is not from where we thought the file was then invalidate the old entry */
	if ((ie->ie_parent != parent->ie_stat.st_ino) ||
		(strncmp(ie->ie_name, name, NAME_MAX) != 0)) {
		DFUSE_TRA_DEBUG(ie, "Invalidating old name");

		rc = fuse_lowlevel_notify_inval_entry(fs_handle->di_session, ie->ie_parent,
						      ie->ie_name, strnlen(ie->ie_name, NAME_MAX));

		if (rc && rc != -ENOENT)
			DFUSE_TRA_ERROR(ie, "inval_entry() returned: %d (%s)", rc, strerror(-rc));
	}

	/* Update the inode entry data */
	ie->ie_parent = newparent->ie_stat.st_ino;
	strncpy(ie->ie_name, newname, NAME_MAX);

	/* Set the new parent and name */
	dfs_update_parentfd(ie->ie_obj, newparent->ie_obj, newname);

	/* Drop the ref again */
	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
}

void
dfuse_cb_rename(fuse_req_t req, struct dfuse_inode_entry *parent,
		const char *name, struct dfuse_inode_entry *newparent,
		const char *newname, unsigned int flags)
{
	struct dfuse_projection_info	*fs_handle;
	daos_obj_id_t			moid = {};
	daos_obj_id_t			oid = {};
	int				rc;

	fs_handle = fuse_req_userdata(req);

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

	dfuse_cache_evict_dir(fs_handle, parent);

	if (newparent) {
		dfuse_cache_evict_dir(fs_handle, newparent);
	} else {
		newparent = parent;
	}

	rc = dfs_move_internal(parent->ie_dfs->dfs_ns, flags, parent->ie_obj, (char *)name,
			       newparent->ie_obj, (char *)newname, &moid, &oid);
	if (rc)
		D_GOTO(out, rc);

	DFUSE_TRA_DEBUG(newparent, "Renamed '%s' to '%s' in %p", name, newname, newparent);

	/* update moid */
	dfuse_oid_moved(fs_handle, &moid, parent, name, newparent, newname);

	/* Check if a file was unlinked and see if anything needs updating */
	if (oid.lo || oid.hi)
		dfuse_oid_unlinked(fs_handle, req, &oid, newparent, newname);
	else
		DFUSE_REPLY_ZERO(newparent, req);

	return;

out:
	DFUSE_REPLY_ERR_RAW(parent, req, rc);
}
