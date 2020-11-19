/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
	int rc;

	DFUSE_TRA_DEBUG(ie, "flags %#x", to_set);

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
		DFUSE_REPLY_ERR_RAW(ie, req, ENOTSUP);
		return;
	}

	rc = dfs_osetattr(ie->ie_dfs->dfs_ns, ie->ie_obj, attr, dfs_flags);
	if (rc) {
		DFUSE_REPLY_ERR_RAW(ie, req, rc);
		return;
	}

	DFUSE_REPLY_ATTR(ie, req, attr);
}
