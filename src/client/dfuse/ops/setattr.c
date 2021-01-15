/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_setattr(fuse_req_t req, struct dfuse_inode_entry *ie,
		 struct stat *attr, int to_set)
{
	int dfs_flags = 0;
	struct stat attr_in = *attr;
	int rc;

	DFUSE_TRA_DEBUG(ie, "flags %#x", to_set);

	/* The uid and gid flags are handled differently, unless
	 * multi-user is enabled they're not supported at all, if
	 * it is enabled then they're handled by extended
	 * attributes.
	 */
	if (to_set & (FUSE_SET_ATTR_GID | FUSE_SET_ATTR_UID)) {
		struct uid_entry entry;
		daos_size_t size = sizeof(entry);
		bool set_uid = to_set & FUSE_SET_ATTR_UID;
		bool set_gid = to_set & FUSE_SET_ATTR_GID;
		bool set_both = set_gid && set_uid;

		if (!ie->ie_dfs->dfs_multi_user)
			D_GOTO(err, rc = ENOTSUP);

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

		rc = dfs_setxattr(ie->ie_dfs->dfs_ns, ie->ie_obj,
				  DFUSE_XID_XATTR_NAME,
				  &entry, sizeof(entry), 0);

		to_set &= ~(FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID);
		if (to_set == 0) {
			rc = dfs_ostat(ie->ie_dfs->dfs_ns, ie->ie_obj, attr);
			if (rc != 0)
				D_GOTO(err, 0);

			attr->st_uid = entry.uid;
			attr->st_gid = entry.gid;

			D_GOTO(reply, 0);
		}

		/* Fall through and do the set of the setattr here */
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

	if (to_set & FUSE_SET_ATTR_SIZE) {
		DFUSE_TRA_DEBUG(ie, "size %#lx",
				attr->st_size);
		to_set &= ~(FUSE_SET_ATTR_SIZE);
		dfs_flags |= DFS_SET_ATTR_SIZE;
		if (ie->ie_dfs->dfs_attr_timeout > 0 &&
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

	attr->st_uid = attr_in.st_uid;
	attr->st_gid = attr_in.st_uid;

reply:
	attr->st_ino = ie->ie_stat.st_ino;
	DFUSE_REPLY_ATTR(ie, req, attr);
	return;

err:
	DFUSE_REPLY_ERR_RAW(ie, req, rc);
}
