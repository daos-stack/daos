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
dfuse_reply_entry(struct dfuse_projection_info *fs_handle,
		  struct dfuse_inode_entry *inode,
		  bool create,
		  fuse_req_t req)
{
	struct fuse_entry_param		entry = {0};
	d_list_t			*rlink;
	daos_obj_id_t			oid;
	int				rc;

	if (!inode->parent) {
		DFUSE_TRA_ERROR(inode, "no parent");
		D_GOTO(err, rc = EIO);
	}

	if (!inode->ie_dfs) {
		DFUSE_TRA_ERROR(inode, "ie_dfs");
		D_GOTO(err, rc = EIO);
	}

	rc = dfs_obj2id(inode->obj, &oid);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(inode, "no oid");
		D_GOTO(err, rc = EIO);
	}

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
		 */
		atomic_fetch_sub(&inode->ie_ref, 1);
		inode->parent = 0;

		ie_close(fs_handle, inode);
	}

	if (create) {
		struct fuse_file_info fi = {0};

		DFUSE_REPLY_CREATE(req, entry, &fi);
	} else {
		DFUSE_REPLY_ENTRY(req, entry);
	}
	return;
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	dfs_release(inode->obj);
}

void
dfuse_cb_lookup(fuse_req_t req, struct dfuse_inode_entry *parent,
		const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*inode = NULL;
	mode_t				mode;
	int rc;

	DFUSE_TRA_INFO(fs_handle, "Parent:%lu '%s'", parent->parent, name);

	DFUSE_TRA_INFO(parent, "parent");

	D_ALLOC_PTR(inode);
	if (!inode) {
		D_GOTO(err, rc = ENOMEM);
	}
	inode->parent = parent->parent;
	inode->ie_dfs = parent->ie_dfs;

	rc = dfs_lookup_rel(parent->ie_dfs->dffs_dfs, parent->obj, name,
			    O_RDONLY, &inode->obj, &mode);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_INFO(fs_handle, "dfs_lookup() failed: %d",
			       rc);
		if (rc == -DER_NONEXIST) {
			D_GOTO(err, rc = ENOENT);
		} else {
			D_GOTO(err, rc = EIO);
		}
	}

	strncpy(inode->name, name, NAME_MAX);
	atomic_fetch_add(&inode->ie_ref, 1);

	rc = dfs_ostat(parent->ie_dfs->dffs_dfs, inode->obj, &inode->stat);
	if (rc != -DER_SUCCESS) {
		D_GOTO(err, 0);
	}

	dfuse_reply_entry(fs_handle, inode, false, req);
	return;

err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	D_FREE(inode);
}
