/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

/* Optionally, modify requested mode bits so the file creator can access the file.  In single-user
 * dfuse, when accessing a container belonging to somebody else all files within that container
 * will belong to the container owner, and this includes any new files created.
 *
 * To avoid a case where a user is granted write permission to a container and is then able to
 * create files which they cannot then access detect this case and promote the user mode bits
 * to either group or other as appropriate so the creator of the file retains access.
 */

/* Number of initial groups to sample.  It doesn't really matter what this value is as if it's
 * not sufficient then a larger array will be allocated.  Set it large enough to be big enough
 * on a standard Linux setup
 */
#define START_GROUP_SIZE 8

int
_dfuse_mode_update(fuse_req_t req, struct dfuse_inode_entry *parent, mode_t *_mode)
{
	const struct fuse_ctx *ctx = fuse_req_ctx(req);
	mode_t mode = *_mode;

	/* First check the UID, if this is different then copy the mode bits from user to group */
	if (ctx->uid != parent->ie_stat.st_uid) {
		DFUSE_TRA_DEBUG(parent, "create with mismatched UID, setting group perms");
		if (mode & S_IRUSR)
			mode |= S_IRGRP;
		if (mode & S_IWUSR)
			mode |= S_IWGRP;
		if (mode & S_IXUSR)
			mode |= S_IXGRP;
	}

	/* Check the GID, if this is different then check all groups, of no groups match then copy
	 * bits from user to other
	 */
	if (ctx->gid != parent->ie_stat.st_gid) {
		int gcount;
		int gsize;
		gid_t glist[START_GROUP_SIZE];
		bool have_group_match = false;
		int i;

		DFUSE_TRA_DEBUG(parent, "create with mismatched GID");

		gcount = fuse_req_getgroups(req, START_GROUP_SIZE, glist);
		gsize = min(2, gcount);

		for (i = 0 ; i < gsize; i++)
			if (glist[i] == parent->ie_stat.st_gid)
				have_group_match = true;

		if (gcount > START_GROUP_SIZE) {
			gid_t *garray;

			D_ALLOC_ARRAY(garray, gcount);
			if (garray == NULL)
				return ENOMEM;

			gsize = fuse_req_getgroups(req, gcount, garray);
			gsize = min(gsize, gcount);

			for (i = 0 ; i < gsize; i++)
				if (glist[i] == parent->ie_stat.st_gid)
					have_group_match = true;

			if (gcount != gsize)
				DFUSE_TRA_WARNING(parent, "group count changed during sample %d %d",
						  gcount, gsize);
			D_FREE(garray);
		}

		if (!have_group_match) {
			DFUSE_TRA_DEBUG(parent, "No GIDs match, setting other perms");

			if (mode & S_IRUSR)
				mode |= S_IROTH;
			if (mode & S_IWUSR)
				mode |= S_IWOTH;
			if (mode & S_IXUSR)
				mode |= S_IXOTH;
		}
	}

	if (*_mode != mode)
		DFUSE_TRA_DEBUG(parent, "Updated mode from %#o to %#o", *_mode, mode);

	*_mode = mode;
	return 0;
}

void
dfuse_cb_create(fuse_req_t req, struct dfuse_inode_entry *parent,
		const char *name, mode_t mode, struct fuse_file_info *fi)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	const struct fuse_ctx		*ctx = fuse_req_ctx(req);
	struct dfuse_inode_entry	*ie = NULL;
	struct dfuse_obj_hdl		*oh = NULL;
	struct fuse_file_info		fi_out = {0};
	struct dfuse_cont		*dfs = parent->ie_dfs;
	int				rc;

	DFUSE_TRA_DEBUG(parent, "Parent:%#lx '%s'", parent->ie_stat.st_ino, name);

	/* O_LARGEFILE should always be set on 64 bit systems, and in fact is
	 * defined to 0 so IOF defines LARGEFILE to the value that O_LARGEFILE
	 * would otherwise be using and check that is set.
	 */
	if (!(fi->flags & LARGEFILE)) {
		DFUSE_TRA_INFO(parent, "O_LARGEFILE required 0%o", fi->flags);
		D_GOTO(err, rc = ENOTSUP);
	}

	/* Check for flags that do not make sense in this context. */
	if (fi->flags & DFUSE_UNSUPPORTED_CREATE_FLAGS) {
		DFUSE_TRA_INFO(parent, "unsupported flag requested 0%o", fi->flags);
		D_GOTO(err, rc = ENOTSUP);
	}

	/* Upgrade fd permissions from O_WRONLY to O_RDWR if wb caching is
	 * enabled so the kernel can do read-modify-write
	 */
	if (parent->ie_dfs->dfc_data_timeout != 0 && fs_handle->di_wb_cache &&
	    (fi->flags & O_ACCMODE) == O_WRONLY) {
		DFUSE_TRA_DEBUG(parent, "Upgrading fd to O_RDRW");
		fi->flags &= ~O_ACCMODE;
		fi->flags |= O_RDWR;
	}

	/* Check that only the flag for a regular file is specified */
	if (!S_ISREG(mode)) {
		DFUSE_TRA_INFO(parent, "unsupported mode requested 0%o", mode);
		D_GOTO(err, rc = ENOTSUP);
	}

	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(err, rc = ENOMEM);
	D_ALLOC_PTR(oh);
	if (!oh)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(ie, parent, "inode");
	DFUSE_TRA_UP(oh, ie, "open handle");
	ie->ie_dfs = dfs;

	ie->ie_stat.st_uid = ctx->uid;
	ie->ie_stat.st_gid = ctx->gid;

	dfuse_ie_init(fs_handle, ie);
	dfuse_open_handle_init(fs_handle, oh, ie);

	oh->doh_linear_read = false;

	if (!fs_handle->di_multi_user) {
		rc = _dfuse_mode_update(req, parent, &mode);
		if (rc != 0)
			D_GOTO(err, rc);
	}

	DFUSE_TRA_DEBUG(ie, "file '%s' flags 0%o mode 0%o", name, fi->flags, mode);

	rc = dfs_open_stat(dfs->dfs_ns, parent->ie_obj, name, mode, fi->flags, 0, 0, NULL,
			   &oh->doh_obj, &ie->ie_stat);
	if (rc)
		D_GOTO(err, rc);

	dfuse_cache_evict_dir(fs_handle, parent);

	/** duplicate the file handle for the fuse handle */
	rc = dfs_dup(dfs->dfs_ns, oh->doh_obj, O_RDWR, &ie->ie_obj);
	if (rc)
		D_GOTO(release, rc);

	oh->doh_writeable = true;

	if (dfs->dfc_data_timeout != 0) {
		if (fi->flags & O_DIRECT)
			fi_out.direct_io = 1;

	} else {
		fi_out.direct_io = 1;
	}

	if (dfs->dfc_direct_io_disable)
		fi_out.direct_io = 0;

	if (!fi_out.direct_io)
		oh->doh_caching = true;

	fi_out.fh = (uint64_t)oh;

	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_parent = parent->ie_stat.st_ino;
	ie->ie_truncated = false;

	LOG_FLAGS(ie, fi->flags);
	LOG_MODES(ie, mode);

	dfs_obj2id(ie->ie_obj, &ie->ie_oid);

	dfuse_compute_inode(dfs, &ie->ie_oid, &ie->ie_stat.st_ino);

	atomic_fetch_add_relaxed(&ie->ie_open_count, 1);

	/* Return the new inode data, and keep the parent ref */
	dfuse_reply_entry(fs_handle, ie, &fi_out, true, req);

	return;
release:
	dfs_release(oh->doh_obj);
err:
	DFUSE_REPLY_ERR_RAW(parent, req, rc);
	dfuse_oh_free(fs_handle, oh);
	dfuse_ie_free(fs_handle, ie);
}
