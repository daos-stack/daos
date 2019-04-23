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
dfuse_register_inode(struct dfuse_projection_info *fs_handle,
		     struct dfuse_inode_entry *inode,
		     fuse_req_t req)
{
	struct fuse_entry_param		entry = {0};
	d_list_t			*rlink;
	daos_obj_id_t oid = dfs_obj2id(inode->obj);

	inode->stat.st_ino = (ino_t)oid.hi;

	entry.attr = inode->stat;
	entry.generation = 1;
	entry.ino = entry.attr.st_ino;
	DFUSE_TRA_INFO(inode, "Inserting inode %lu", entry.ino);

	rlink = d_hash_rec_find_insert(&fs_handle->inode_ht,
				       &inode->stat.st_ino,
				       sizeof(inode->stat.st_ino),
				       &inode->ie_htl);

	if (rlink != &inode->ie_htl) {
		/* The lookup has resulted in an existing file, so reuse that
		 * entry, drop the inode in the lookup descriptor and do not
		 * keep a reference on the parent.
		 * Note that this function will be called with a reference on
		 * the parent anyway, so keep that one, but drop one in the call
		 * to ie_close().
		 */
		atomic_fetch_sub(&inode->ie_ref, 1);

		ie_close(fs_handle, inode);
	}

	DFUSE_REPLY_ENTRY(req, entry);
}

void
dfuse_cb_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*inode = NULL;
	struct dfuse_inode_entry	*parent_inode;
	mode_t				mode;
	d_list_t			*rlink;
	int rc;

	DFUSE_TRA_INFO(fs_handle, "Parent:%lu '%s'", parent, name);

	rlink = d_hash_rec_find(&fs_handle->inode_ht, &parent, sizeof(parent));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %lu",
				parent);
		D_GOTO(err, rc = ENOENT);
	}

	parent_inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	DFUSE_TRA_INFO(parent_inode, "parent");

	D_ALLOC_PTR(inode);
	if (!inode) {
		D_GOTO(out_decref, rc = ENOMEM);
	}
	inode->parent = parent;

	rc = dfs_lookup_rel(fs_handle->fsh_dfs, parent_inode->obj, name,
			    O_RDONLY, &inode->obj, &mode);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_INFO(fs_handle, "dfs_lookup() failed: %p %d",
			       fs_handle->fsh_dfs, rc);
		if (rc == -DER_NONEXIST) {
			D_GOTO(out_decref, rc = ENOENT);
		} else {
			D_GOTO(out_decref, rc = EIO);
		}
	}

	strncpy(inode->name, name, NAME_MAX);
	atomic_fetch_add(&inode->ie_ref, 1);

	rc = dfs_ostat(fs_handle->fsh_dfs, inode->obj, &inode->stat);
	if (rc != -DER_SUCCESS) {
		D_GOTO(out_decref, 0);
	}

	dfuse_register_inode(fs_handle, inode, req);
	d_hash_rec_decref(&fs_handle->inode_ht, rlink);
	return;

out_decref:
	d_hash_rec_decref(&fs_handle->inode_ht, rlink);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	D_FREE(inode);
}
