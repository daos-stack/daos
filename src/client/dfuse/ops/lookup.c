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
		  struct dfuse_inode_entry *ie,
		  struct fuse_file_info *fi_out,
		  fuse_req_t req)
{
	struct fuse_entry_param	entry = {0};
	d_list_t		*rlink;
	daos_obj_id_t		oid;
	int			rc;

	D_ASSERT(ie->ie_parent);
	D_ASSERT(ie->ie_dfs);

	if (ie->ie_stat.st_ino == 0) {
		rc = dfs_obj2id(ie->ie_obj, &oid);
		if (rc)
			D_GOTO(err, rc = -rc);
		rc = dfuse_lookup_inode(fs_handle, ie->ie_dfs, &oid,
					&ie->ie_stat.st_ino);
		if (rc)
			D_GOTO(err, rc = -rc);
	}

	entry.attr = ie->ie_stat;
	entry.generation = 1;
	entry.ino = entry.attr.st_ino;
	DFUSE_TRA_INFO(ie, "Inserting inode %lu", entry.ino);

	rlink = d_hash_rec_find_insert(&fs_handle->dpi_iet,
				       &ie->ie_stat.st_ino,
				       sizeof(ie->ie_stat.st_ino),
				       &ie->ie_htl);

	if (rlink != &ie->ie_htl) {
		struct dfuse_inode_entry *inode;

		inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

		/* The lookup has resulted in an existing file, so reuse that
		 * entry, drop the inode in the lookup descriptor and do not
		 * keep a reference on the parent.
		 */

		/* Update the existing object with the new name/parent */
		rc = dfs_update_parent(inode->ie_obj, ie->ie_obj, ie->ie_name);
		if (rc != -DER_SUCCESS) {
			DFUSE_TRA_ERROR(inode, "dfs_update_parent() failed %d",
					rc);
		}
		inode->ie_parent = ie->ie_parent;
		strncpy(inode->ie_name, ie->ie_name, NAME_MAX);

		atomic_fetch_sub(&ie->ie_ref, 1);
		ie->ie_parent = 0;

		ie_close(fs_handle, ie);
	}

	if (fi_out) {
		DFUSE_REPLY_CREATE(req, entry, fi_out);
	} else {
		DFUSE_REPLY_ENTRY(req, entry);
	}
	return;
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	dfs_release(ie->ie_obj);
}

bool
dfuse_cb_lookup(fuse_req_t req, struct dfuse_inode_entry *parent,
		const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*ie = NULL;
	mode_t				mode;
	int rc;

	DFUSE_TRA_INFO(fs_handle,
		       "Parent:%lu '%s'", parent->ie_stat.st_ino, name);

	DFUSE_TRA_INFO(parent, "parent");

	D_ALLOC_PTR(ie);
	if (!ie) {
		D_GOTO(err, rc = -ENOMEM);
	}

	ie->ie_parent = parent->ie_stat.st_ino;
	ie->ie_dfs = parent->ie_dfs;

	rc = dfs_lookup_rel(parent->ie_dfs->dfs_ns, parent->ie_obj, name,
			    O_RDONLY, &ie->ie_obj, &mode);
	if (rc) {
		DFUSE_TRA_INFO(fs_handle, "dfs_lookup() failed: (%s)",
			       strerror(-rc));
		D_GOTO(err, rc = -rc);
	}

	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_name[NAME_MAX] = '\0';
	atomic_fetch_add(&ie->ie_ref, 1);

	rc = dfs_ostat(parent->ie_dfs->dfs_ns, ie->ie_obj, &ie->ie_stat);
	if (rc)
		D_GOTO(err, rc = -rc);

	/* If the new entry is a link allocate an inode number here, as dfs
	 * does not assign it an object id to be able to save an inode.
	 *
	 * see comment in symlink.c
	 */
	if (S_ISLNK(ie->ie_stat.st_mode)) {
		ie->ie_stat.st_ino = atomic_fetch_add(&fs_handle->dpi_ino_next,
						      1);
	}

	dfuse_reply_entry(fs_handle, ie, NULL, req);
	return true;

err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	D_FREE(ie);
	return false;
}
