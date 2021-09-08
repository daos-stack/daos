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
	int dfs_flags = 0;
	int rc;

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

	if (to_set & (FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID)) {
		DFUSE_TRA_INFO(ie, "File uid/gid support not enabled");
		D_GOTO(err, rc = ENOTSUP);
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

	/* Only set this when caching is enabled as dfs doesn't fully support
	 * ctime, but rather uses mtime instead.  In practice this is only
	 * seen when using writeback cache.
	 *
	 * This is only seen of entries where caching is enabled, however
	 * if a file is opened with caching then the operation might then
	 * happen on the inode, not the file handle so simply check if
	 * caching might be enabled for the container.
	 */
	if (to_set & FUSE_SET_ATTR_CTIME) {
		if (!ie->ie_dfs->dfc_data_caching) {
			DFUSE_TRA_INFO(ie, "CTIME set without data caching");
			D_GOTO(err, rc = ENOTSUP);
		}
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

	attr->st_ino = ie->ie_stat.st_ino;

	DFUSE_REPLY_ATTR(ie, req, attr);
	return;
err:
	DFUSE_REPLY_ERR_RAW(ie, req, rc);
}
