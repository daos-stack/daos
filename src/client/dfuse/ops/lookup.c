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
			D_GOTO(err, rc);
		rc = dfuse_lookup_inode(fs_handle, ie->ie_dfs, &oid,
					&ie->ie_stat.st_ino);
		if (rc)
			D_GOTO(err, rc);
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

		DFUSE_TRA_INFO(inode,
			       "Maybe updating parent %lu %lu",
			       entry.ino, ie->ie_dfs->dfs_root);

		if (ie->ie_stat.st_ino == ie->ie_dfs->dfs_root) {
			DFUSE_TRA_INFO(inode, "Not updating parent");
		} else {
			rc = dfs_update_parent(inode->ie_obj, ie->ie_obj,
					       ie->ie_name);
			if (rc != -DER_SUCCESS)
				DFUSE_TRA_ERROR(inode,
						"dfs_update_parent() failed %d",
						rc);
		}
		inode->ie_parent = ie->ie_parent;
		strncpy(inode->ie_name, ie->ie_name, NAME_MAX+1);

		atomic_fetch_sub(&ie->ie_ref, 1);
		ie->ie_parent = 0;

		ie_close(fs_handle, ie);
	}

	if (fi_out)
		DFUSE_REPLY_CREATE(req, entry, fi_out);
	else
		DFUSE_REPLY_ENTRY(req, entry);
	return;
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	dfs_release(ie->ie_obj);
	d_hash_rec_decref(&fs_handle->dpi_iet, &ie->ie_htl);
}

/* Check for and set a unified namespace entry point.
 *
 * This function will check for and configure a inode as
 * a new entry point of possible, and modify the inode
 * as required.
 *
 * On failure it will return error.
 *
 */
static int
check_for_uns_ep(struct dfuse_projection_info *fs_handle,
		 struct dfuse_inode_entry *ie)
{
	daos_obj_id_t		oid;
	int			rc;
	char			pool[40] = {};
	size_t			pool_size = 40;
	char			cont[40] = {};
	size_t			cont_size = 40;
	struct dfuse_dfs	*dfs = NULL;
	int ret;

	rc = dfs_getxattr(ie->ie_dfs->dfs_ns, ie->ie_obj, DFUSE_UNS_POOL_ATTR,
			  &pool, &pool_size);

	if (rc == ENODATA)
		return 0;
	if (rc)
		return rc;

	if (pool_size != 36)
		return EINVAL;

	rc = dfs_getxattr(ie->ie_dfs->dfs_ns, ie->ie_obj,
			  DFUSE_UNS_CONTAINER_ATTR, &cont, &cont_size);
	if (rc == ENODATA)
		return 0;
	if (rc)
		return rc;

	if (cont_size != 36)
		return EINVAL;

	DFUSE_TRA_DEBUG(ie, "'%s' '%s'", pool, cont);

	D_ALLOC_PTR(dfs);
	if (dfs == NULL)
		return ENOMEM;

	if (uuid_parse(pool, dfs->dfs_pool) < 0) {
		DFUSE_LOG_ERROR("Invalid pool uuid");
		D_GOTO(out_err, ret = EINVAL);
	}

	if (uuid_parse(cont, dfs->dfs_cont) < 0) {
		DFUSE_LOG_ERROR("Invalid container uuid");
		D_GOTO(out_err, ret = EINVAL);
	}

	dfs->dfs_ops = ie->ie_dfs->dfs_ops;

	/* Connect to DAOS pool */
	rc = daos_pool_connect(dfs->dfs_pool, fs_handle->dpi_info->di_group,
			       fs_handle->dpi_info->di_svcl, DAOS_PC_RW,
			       &dfs->dfs_poh, &dfs->dfs_pool_info,
			       NULL);
	if (rc != -DER_SUCCESS) {
		DFUSE_LOG_ERROR("Failed to connect to pool (%d)", rc);
		D_GOTO(out_err, ret = rc);
	}

	/* Try to open the DAOS container (the mountpoint) */
	rc = daos_cont_open(dfs->dfs_poh, dfs->dfs_cont, DAOS_COO_RW,
			    &dfs->dfs_coh, &dfs->dfs_co_info,
			    NULL);
	if (rc) {
		DFUSE_LOG_ERROR("Failed container open (%d)",
				rc);
		D_GOTO(out_pool, ret = rc);
	}

	rc = dfs_mount(dfs->dfs_poh, dfs->dfs_coh, O_RDWR,
		       &dfs->dfs_ns);
	if (rc) {
		daos_cont_close(dfs->dfs_coh, NULL);
		DFUSE_LOG_ERROR("dfs_mount failed (%d)", rc);
		D_GOTO(out_cont, ret = rc);
	}

	rc = dfs_release(ie->ie_obj);
	if (rc) {
		DFUSE_TRA_ERROR(ie, "dfs_release() failed: (%s)",
				strerror(rc));
		D_GOTO(out_umount, ret = rc);
	}

	rc = dfs_lookup(dfs->dfs_ns, "/", O_RDONLY, &ie->ie_obj,
			NULL, NULL);
	if (rc) {
		DFUSE_TRA_ERROR(ie, "dfs_lookup() failed: (%s)",
				strerror(rc));
		D_GOTO(out_umount, ret = rc);
	}

	ie->ie_dfs = dfs;

	rc = dfs_obj2id(ie->ie_obj, &oid);
	if (rc)
		D_GOTO(out_umount, ret = rc);

	D_MUTEX_LOCK(&fs_handle->dpi_info->di_lock);
	d_list_add(&dfs->dfs_list, &fs_handle->dpi_info->di_dfs_list);
	D_MUTEX_UNLOCK(&fs_handle->dpi_info->di_lock);

	rc = dfuse_lookup_inode(fs_handle, dfs, &oid,
				&ie->ie_stat.st_ino);
	if (rc)
		D_GOTO(out_umount, ret = rc);

	dfs->dfs_root = ie->ie_stat.st_ino;

	DFUSE_TRA_INFO(ie, "UNS entry point activated, root %lu",
		       dfs->dfs_root);

	return 0;

out_umount:
	rc = dfs_umount(dfs->dfs_ns);
	if (rc)
		DFUSE_TRA_ERROR(dfs, "dfs_umount() failed %d", rc);
out_cont:
	rc = daos_cont_close(dfs->dfs_coh, NULL);
	if (rc)
		DFUSE_TRA_ERROR(dfs, "daos_cont_close() failed %d", rc);
out_pool:
	rc = daos_pool_disconnect(dfs->dfs_poh, NULL);
	if (rc)
		DFUSE_TRA_ERROR(dfs, "daos_pool_disconnect() failed %d", rc);
out_err:
	D_FREE(dfs);
	return ret;
}

void
dfuse_cb_lookup(fuse_req_t req, struct dfuse_inode_entry *parent,
		const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*ie = NULL;
	int				rc;

	DFUSE_TRA_INFO(fs_handle,
		       "Parent:%lu '%s'", parent->ie_stat.st_ino, name);

	DFUSE_TRA_INFO(parent, "parent");

	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(ie, parent, "inode");

	ie->ie_parent = parent->ie_stat.st_ino;
	ie->ie_dfs = parent->ie_dfs;

	rc = dfs_lookup_rel(parent->ie_dfs->dfs_ns, parent->ie_obj, name,
			    O_RDONLY, &ie->ie_obj, NULL, &ie->ie_stat);
	if (rc) {
		DFUSE_TRA_INFO(fs_handle, "dfs_lookup() failed: (%s)",
			       strerror(rc));
		D_GOTO(err, rc);
	}

	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_name[NAME_MAX] = '\0';
	atomic_fetch_add(&ie->ie_ref, 1);

	if (S_ISDIR(ie->ie_stat.st_mode)) {
		rc = check_for_uns_ep(fs_handle, ie);
		if (rc) {
			D_GOTO(err, 0);
			DFUSE_TRA_INFO(ie,
				       "check_for_uns_ep() returned %d", rc);
		}
	}

	dfuse_reply_entry(fs_handle, ie, NULL, req);
	return;

err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	D_FREE(ie);
	return;
}
