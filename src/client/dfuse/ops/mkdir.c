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

void
dfuse_cb_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*inode = NULL;
	struct dfuse_inode_entry	*parent_inode;
	d_list_t			*rlink = NULL;
	int rc;

	DFUSE_TRA_INFO(fs_handle, "Parent:%lu '%s'", parent, name);

	if (parent != 1) {
		D_GOTO(err, rc = ENOTSUP);
	}

	rlink = d_hash_rec_find(&fs_handle->inode_ht, &parent, sizeof(parent));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %lu",
				parent);
		D_GOTO(err, rc = ENOENT);
	}

	parent_inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	D_ALLOC_PTR(inode);
	if (!inode) {
		D_GOTO(err, rc = ENOMEM);
	}

	/* mkdir with the correct parent */
	rc = dfs_mkdir(fs_handle->fsh_dfs, parent_inode->obj, name, mode);
	if (rc != -DER_SUCCESS) {
		D_GOTO(err, 0);
	}

	strncpy(inode->name, name, NAME_MAX);
	inode->parent = parent;
	atomic_fetch_add(&inode->ie_ref, 1);

	/* This wants to use parent->obj but it isn't ready yet */
	rc = dfs_lookup(fs_handle->fsh_dfs, name, O_RDONLY, &inode->obj, &mode);
	if (rc != -DER_SUCCESS) {
		D_GOTO(err, 0);
	}

	rc = dfs_ostat(fs_handle->fsh_dfs, inode->obj, &inode->stat);
	if (rc != -DER_SUCCESS) {
		D_GOTO(release, 0);
	}

	/* Return the new inode data, and keep the parent ref */
	dfuse_register_inode(fs_handle, inode, req);

	return;
release:
	dfs_release(inode->obj);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	if (rlink) {
		d_hash_rec_decref(&fs_handle->inode_ht, rlink);

	}
	D_FREE(inode);
}
