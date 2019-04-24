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
dfuse_cb_create(fuse_req_t req, fuse_ino_t parent, const char *name,
		mode_t mode, struct fuse_file_info *fi)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*inode = NULL;
	struct dfuse_inode_entry	*parent_inode;
	struct dfuse_file_handle	*handle = NULL;
	d_list_t			*rlink = NULL;
	int rc;

	DFUSE_TRA_INFO(fs_handle, "Parent:%lu '%s'", parent, name);

	D_GOTO(err, rc = ENOTSUP);

	/* O_LARGEFILE should always be set on 64 bit systems, and in fact is
	 * defined to 0 so IOF defines LARGEFILE to the value that O_LARGEFILE
	 * would otherwise be using and check that is set.
	 */
	if (!(fi->flags & LARGEFILE)) {
		DFUSE_TRA_INFO(req, "O_LARGEFILE required 0%o",
			       fi->flags);
		D_GOTO(err, rc = ENOTSUP);
	}

	/* Check for flags that do not make sense in this context.
	 */
	if (fi->flags & DFUSE_UNSUPPORTED_CREATE_FLAGS) {
		DFUSE_TRA_INFO(req, "unsupported flag requested 0%o",
			       fi->flags);
		D_GOTO(err, rc = ENOTSUP);
	}

	/* Check that only the flag for a regular file is specified */
	if ((mode & S_IFMT) != S_IFREG) {
		DFUSE_TRA_INFO(req, "unsupported mode requested 0%o",
			       mode);
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

	D_ALLOC_PTR(handle);
	if (!handle) {
		D_GOTO(err, rc = ENOMEM);
	}

	DFUSE_TRA_INFO(parent_inode, "parent");

	handle->common.projection = &fs_handle->proj;

	DFUSE_TRA_INFO(handle, "file '%s' flags 0%o mode 0%o", name, fi->flags,
		       mode);

	rc = dfs_open(fs_handle->fsh_dfs, parent_inode->obj, name, mode,
		      O_CREAT, 0, 0, NULL, &inode->obj);
	if (rc != -DER_SUCCESS) {
		D_GOTO(release, 0);
	}

	/* TODO: Add a dfs_dup() call to get a object for the handle as well
	 * as the inode.
	 */
	strncpy(inode->name, name, NAME_MAX);
	inode->parent = parent;
	atomic_fetch_add(&inode->ie_ref, 1);

	rc = dfs_ostat(fs_handle->fsh_dfs, inode->obj, &inode->stat);
	if (rc != -DER_SUCCESS) {
		D_GOTO(release, 0);
	}

	LOG_FLAGS(handle, fi->flags);
	LOG_MODES(handle, mode);

	/* Return the new inode data, and keep the parent ref */
	dfuse_reply_entry(fs_handle, inode, handle, req);

	return;
release:
	dfs_release(inode->obj);

err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	if (rlink) {
		d_hash_rec_decref(&fs_handle->inode_ht, rlink);

	}
	D_FREE(inode);
	D_FREE(handle);
}
