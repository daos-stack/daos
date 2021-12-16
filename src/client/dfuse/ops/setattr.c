/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_setattr(fuse_req_t req, struct dfuse_inode_entry *ie,
		 struct stat *attr, int to_set)
{
	int			dfs_flags = 0;
	int			rc;

	DFUSE_TRA_DEBUG(ie, "flags %#x", to_set);

	if (ie->ie_unlinked) {
		DFUSE_TRA_DEBUG(ie, "File is unlinked, returning most recent data");

		/* This will happen on close with caching enabled if there are writes through the
		 * cache so accept these two entries only and reject anything else.  This allows
		 * the read/write case to work on unlinked files without triggering an error.
		 */
		if (to_set & ~(FUSE_SET_ATTR_MTIME | FUSE_SET_ATTR_CTIME))
			D_GOTO(err, rc = ENOENT);

		if (to_set & FUSE_SET_ATTR_MTIME)
			ie->ie_stat.st_mtim = attr->st_mtim;

		if (to_set & FUSE_SET_ATTR_CTIME)
			ie->ie_stat.st_ctim = attr->st_ctim;

		DFUSE_REPLY_ATTR(ie, req, &ie->ie_stat);
		return;
	}

	/* The uid and gid flags are handled differently, unless
	 * multi-user is enabled they're not supported at all, if
	 * it is enabled then they're handled by extended
	 * attributes.
	 */
	if (to_set & (FUSE_SET_ATTR_GID | FUSE_SET_ATTR_UID)) {
		struct uid_entry	entry;
		daos_size_t		size = sizeof(entry);
		bool			set_uid = to_set & FUSE_SET_ATTR_UID;
		bool			set_gid = to_set & FUSE_SET_ATTR_GID;
		bool			set_both = set_gid && set_uid;

		DFUSE_TRA_DEBUG(ie, "uid flags %#x uid %d gid %d",
				(to_set & (FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID)),
				attr->st_uid, attr->st_gid);

		if (!ie->ie_dfs->dfs_multi_user) {

			if (((to_set & FUSE_SET_ATTR_UID) && ie->ie_stat.st_uid != attr->st_uid) ||
				((to_set & FUSE_SET_ATTR_GID) && ie->ie_stat.st_gid != attr->st_gid)) {
				DFUSE_TRA_INFO(ie, "File uid/gid support not enabled");
				D_GOTO(err, rc = ENOTSUP);
			}

			to_set &= ~(FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID);

		} else {

			/* Set defaults based on current file ownership */
			entry.uid = ie->ie_stat.st_uid;
			entry.gid = ie->ie_stat.st_gid;

			if (!set_both) {
				rc = dfs_getxattr(ie->ie_dfs->dfs_ns, ie->ie_obj,
						DFUSE_XID_XATTR_NAME, &entry, &size);
				if (rc && rc != ENODATA)
					D_GOTO(err, rc);
			}

			if (set_uid)
				entry.uid = attr->st_uid;

			if (set_gid)
				entry.gid = attr->st_gid;

			rc = dfs_setxattr(ie->ie_dfs->dfs_ns, ie->ie_obj, DFUSE_XID_XATTR_NAME,
					  &entry, sizeof(entry), 0);

			if (to_set == 0) {
				rc = dfs_ostat(ie->ie_dfs->dfs_ns, ie->ie_obj, attr);
				if (rc != 0)
					D_GOTO(err, 0);

				attr->st_uid = entry.uid;
				attr->st_gid = entry.gid;

				D_GOTO(reply, 0);
			}
		}
		to_set &= ~(FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID);
	}

	if (to_set & FUSE_SET_ATTR_MODE) {
		DFUSE_TRA_DEBUG(ie, "mode %#o %#o",
				attr->st_mode, ie->ie_stat.st_mode);

		to_set &= ~FUSE_SET_ATTR_MODE;
		dfs_flags |= DFS_SET_ATTR_MODE;
	}

	if (to_set & FUSE_SET_ATTR_ATIME) {
		DFUSE_TRA_DEBUG(ie, "atime %#lx",
				attr->st_atime);
		to_set &= ~(FUSE_SET_ATTR_ATIME | FUSE_SET_ATTR_ATIME_NOW);
		dfs_flags |= DFS_SET_ATTR_ATIME;
	}

	if (to_set & FUSE_SET_ATTR_MTIME) {
		DFUSE_TRA_DEBUG(ie, "mtime %#lx",
				attr->st_mtime);
		to_set &= ~(FUSE_SET_ATTR_MTIME | FUSE_SET_ATTR_MTIME_NOW);
		dfs_flags |= DFS_SET_ATTR_MTIME;
	}

	/* Set this when requested, however dfs doesn't support ctime, only mtime.
	 *
	 * This is only seen on entries where caching is enabled, however it can happen
	 * for either data or metadata caching, so just accept it always.
	 * Update, it can happen with metadata caching, but not data caching.
	 */
	if (to_set & FUSE_SET_ATTR_CTIME) {
		DFUSE_TRA_DEBUG(ie, "ctime %#lx", attr->st_ctime);
		to_set &= ~FUSE_SET_ATTR_CTIME;
		attr->st_mtime = attr->st_ctime;
		dfs_flags |= DFS_SET_ATTR_MTIME;
	}

	if (to_set & FUSE_SET_ATTR_SIZE) {
		DFUSE_TRA_DEBUG(ie, "size %#lx",
				attr->st_size);
		to_set &= ~FUSE_SET_ATTR_SIZE;
		dfs_flags |= DFS_SET_ATTR_SIZE;
		if (ie->ie_dfs->dfc_data_caching &&
		    ie->ie_stat.st_size == 0 && attr->st_size > 0) {
			DFUSE_TRA_DEBUG(ie, "truncating 0-size file");
			ie->ie_truncated = true;
			ie->ie_start_off = 0;
			ie->ie_end_off = 0;
			ie->ie_stat.st_size = attr->st_size;
		} else {
			ie->ie_truncated = false;
		}
	}

	if (to_set) {
		DFUSE_TRA_WARNING(ie, "Unknown flags %#x", to_set);
		D_GOTO(err, rc = ENOTSUP);
	}

	rc = dfs_osetattr(ie->ie_dfs->dfs_ns, ie->ie_obj, attr, dfs_flags);
	if (rc)
		D_GOTO(err, rc);

	if (ie->ie_dfs->dfs_multi_user) {
		rc = dfuse_get_uid(ie);
		if (rc)
			D_GOTO(err, rc);
		attr->st_uid = ie->ie_stat.st_uid;
		attr->st_gid = ie->ie_stat.st_gid;
	}


reply:
	attr->st_ino = ie->ie_stat.st_ino;

	/* Update the size as dfuse knows about it for future use, but only if it was set as part
	 * of this call.  See DAOS-8333
	 */
	if (dfs_flags & DFS_SET_ATTR_SIZE)
		ie->ie_stat.st_size = attr->st_size;

	DFUSE_REPLY_ATTR(ie, req, attr);
	return;
err:
	DFUSE_REPLY_ERR_RAW(ie, req, rc);
}
