/**
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

int
dfuse_get_uid(struct dfuse_inode_entry *ie)
{
	struct uid_entry	entry = {0};
	daos_size_t		size = sizeof(entry);
	int rc;

	rc = dfs_getxattr(ie->ie_dfs->dfs_ns, ie->ie_obj, DFUSE_XID_XATTR_NAME, &entry, &size);

	if (rc == 0 && size != sizeof(entry)) {
		rc = EIO;
		goto out;
	}

	if (rc == ENODATA) {
		rc = 0;
		goto out;
	}

	if (rc != 0)
		D_GOTO(out, rc);

	ie->ie_stat.st_uid = entry.uid;
	ie->ie_stat.st_gid = entry.gid;

out:
	return rc;
}

int
ie_set_uid(struct dfuse_inode_entry *ie, fuse_req_t req)
{
	const struct fuse_ctx *ctx = fuse_req_ctx(req);
	struct uid_entry entry;
	int rc;

	entry.uid = ctx->uid;
	entry.gid = ctx->gid;

	rc = dfs_setxattr(ie->ie_dfs->dfs_ns, ie->ie_obj, DFUSE_XID_XATTR_NAME,
			  &entry, sizeof(entry), 0);

	if (rc == 0) {
		ie->ie_stat.st_uid = entry.uid;
		ie->ie_stat.st_gid = entry.gid;
	}
	return rc;
}

void
dfuse_cb_mknod_with_id(fuse_req_t req, struct dfuse_inode_entry *parent,
		       const char *name, mode_t mode)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*ie = NULL;
	daos_obj_id_t			oid;
	int				rc;
	int				cleanup_rc;

	DFUSE_TRA_INFO(parent,
		       "Parent:%lu '%s'", parent->ie_stat.st_ino, name);

	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(ie, parent, "inode");

	DFUSE_TRA_DEBUG(ie, "directory '%s' mode 0%o", name, mode);

	rc = dfs_open_stat(parent->ie_dfs->dfs_ns, parent->ie_obj, name, mode, O_CREAT | O_RDWR,
			   0, 0, NULL, &ie->ie_obj, &ie->ie_stat);
	if (rc)
		D_GOTO(err, rc);

	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_name[NAME_MAX] = '\0';
	ie->ie_parent = parent->ie_stat.st_ino;
	ie->ie_dfs = parent->ie_dfs;
	atomic_store_relaxed(&ie->ie_ref, 1);

	rc = ie_set_uid(ie, req);
	if (rc)
		D_GOTO(unlink, rc);

	dfs_obj2id(ie->ie_obj, &ie->ie_oid);

	dfuse_compute_inode(ie->ie_dfs, &ie->ie_oid, &ie->ie_stat.st_ino);

	/* Return the new inode data, and keep the parent ref */
	dfuse_reply_entry(fs_handle, ie, NULL, true, req);

	return;

unlink:
	cleanup_rc = dfs_remove(parent->ie_dfs->dfs_ns, parent->ie_obj, name, false, &oid);
	if (cleanup_rc != 0)
		DFUSE_TRA_ERROR(parent, "Created but could not unlink %s: %d, %s",
				name, rc, strerror(rc));

	dfs_release(ie->ie_obj);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	D_FREE(ie);
}
