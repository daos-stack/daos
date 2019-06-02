/**
 * (C) Copyright 2016-2019 Intel Corporation.
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

bool
dfuse_cb_mkdir(fuse_req_t req, struct dfuse_inode_entry *parent,
	       const char *name, mode_t mode)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*ie = NULL;
	int				rc;

	DFUSE_TRA_INFO(fs_handle,
		       "Parent:%lu '%s'", parent->ie_stat.st_ino, name);

	D_ALLOC_PTR(ie);
	if (!ie) {
		D_GOTO(err, rc = ENOMEM);
	}

	DFUSE_TRA_INFO(parent, "parent, mode %d", mode);

	rc = dfs_open(parent->ie_dfs->dffs_dfs, parent->ie_obj, name,
		      mode | S_IFDIR, O_CREAT, 0, 0, NULL, &ie->ie_obj);
	if (rc) {
		D_GOTO(err, rc = -rc);
	}

	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_name[NAME_MAX] = '\0';
	ie->ie_parent = parent->ie_stat.st_ino;
	ie->ie_dfs = parent->ie_dfs;
	atomic_fetch_add(&ie->ie_ref, 1);

	rc = dfs_ostat(parent->ie_dfs->dffs_dfs, ie->ie_obj, &ie->ie_stat);
	if (rc) {
		D_GOTO(release, rc = -rc);
	}

	/* Return the new inode data, and keep the parent ref */
	dfuse_reply_entry(fs_handle, ie, NULL, req);

	return true;
release:
	dfs_release(ie->ie_obj);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	D_FREE(ie);

	return false;
}
