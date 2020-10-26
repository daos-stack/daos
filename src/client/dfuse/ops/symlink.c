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
dfuse_cb_symlink(fuse_req_t req, const char *link,
		 struct dfuse_inode_entry *parent,
		 const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*ie = NULL;
	int rc;

	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(ie, parent, "inode");

	rc = dfs_open(parent->ie_dfs->dfs_ns, parent->ie_obj, name,
		      S_IFLNK, O_CREAT, 0, 0, link, &ie->ie_obj);
	if (rc != 0)
		D_GOTO(err, rc);

	DFUSE_TRA_INFO(ie, "obj is %p", ie->ie_obj);

	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_name[NAME_MAX] = '\0';
	ie->ie_parent = parent->ie_stat.st_ino;
	ie->ie_dfs = parent->ie_dfs;
	atomic_store_relaxed(&ie->ie_ref, 1);

	rc = dfs_ostat(parent->ie_dfs->dfs_ns, ie->ie_obj, &ie->ie_stat);
	if (rc)
		D_GOTO(err, rc);

	DFUSE_TRA_INFO(ie, "Inserting inode %lu", ie->ie_stat.st_ino);

	dfuse_reply_entry(fs_handle, ie, NULL, req);

	return;
err:
	D_FREE(ie);
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}
