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

#include "daos_uns.h"

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

	entry.attr_timeout = ie->ie_dfs->dfs_attr_timeout;
	entry.entry_timeout = ie->ie_dfs->dfs_attr_timeout;

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
	DFUSE_TRA_DEBUG(ie, "Inserting inode %lu", entry.ino);

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

		DFUSE_TRA_DEBUG(inode,
				"Maybe updating parent inode %lu dfs_root %lu",
				entry.ino, ie->ie_dfs->dfs_root);

		if (ie->ie_stat.st_ino == ie->ie_dfs->dfs_root) {
			DFUSE_TRA_DEBUG(inode, "Not updating parent");
		} else {
			rc = dfs_update_parent(inode->ie_obj, ie->ie_obj,
					       ie->ie_name);
			if (rc != -DER_SUCCESS)
				DFUSE_TRA_ERROR(inode,
						"dfs_update_parent() failed %d",
						rc);
		}
		inode->ie_parent = ie->ie_parent;
		strncpy(inode->ie_name, ie->ie_name, NAME_MAX + 1);

		atomic_fetch_sub_relaxed(&ie->ie_ref, 1);
		ie->ie_parent = 0;
		ie->ie_root = 0;
		ie_close(fs_handle, ie);
		ie = inode;
	}

	if (fi_out)
		DFUSE_REPLY_CREATE(ie, req, entry, fi_out);
	else
		DFUSE_REPLY_ENTRY(ie, req, entry);
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
	char			str[DUNS_MAX_XATTR_LEN];
	daos_size_t		str_len = DUNS_MAX_XATTR_LEN;
	struct duns_attr_t	dattr = {};
	struct dfuse_dfs	*dfs = NULL;
	struct dfuse_dfs	*dfsi;
	struct dfuse_pool	*dfp = NULL;
	struct dfuse_pool	*dfpi;
	int			new_pool = false;
	int			new_cont = false;
	int ret;

	rc = dfs_getxattr(ie->ie_dfs->dfs_ns, ie->ie_obj, DUNS_XATTR_NAME,
			  &str, &str_len);

	if (rc == ENODATA)
		return 0;
	if (rc)
		return rc;

	rc = duns_parse_attr(&str[0], str_len, &dattr);
	if (rc)
		return rc;

	if (dattr.da_type != DAOS_PROP_CO_LAYOUT_POSIX)
		return ENOTSUP;

	D_MUTEX_LOCK(&fs_handle->dpi_info->di_lock);

	/* Search the currently connect dfp list, if one matches then use that
	 * and drop the locally allocated one.  If there is no match then
	 * properly initialize the local one ready for use.
	 */
	d_list_for_each_entry(dfpi, &fs_handle->dpi_info->di_dfp_list,
			      dfp_list) {
		DFUSE_TRA_DEBUG(ie, "Checking dfp %p", dfpi);

		if (uuid_compare(dattr.da_puuid, dfpi->dfp_pool) != 0)
			continue;

		DFUSE_TRA_DEBUG(ie, "Reusing dfp %p", dfpi);
		dfp = dfpi;
		break;
	}

	if (!dfp) {
		D_ALLOC_PTR(dfp);
		if (dfp == NULL)
			D_GOTO(out_err, ret = ENOMEM);

		DFUSE_TRA_UP(dfp, ie->ie_dfs->dfs_dfp, "dfp");
		D_INIT_LIST_HEAD(&dfp->dfp_dfs_list);
		d_list_add(&dfp->dfp_list, &fs_handle->dpi_info->di_dfp_list);
		uuid_copy(dfp->dfp_pool, dattr.da_puuid);

		/* Connect to DAOS pool */
		rc = daos_pool_connect(dfp->dfp_pool,
				       fs_handle->dpi_info->di_group,
				       fs_handle->dpi_info->di_svcl, DAOS_PC_RW,
				       &dfp->dfp_poh, &dfp->dfp_pool_info,
				       NULL);
		if (rc != -DER_SUCCESS) {
			DFUSE_LOG_ERROR("Failed to connect to pool (%d)", rc);
			D_GOTO(out_err, ret = daos_der2errno(rc));
		}
		new_pool = true;
	}

	d_list_for_each_entry(dfsi, &dfp->dfp_dfs_list,	dfs_list) {
		if (uuid_compare(dattr.da_cuuid, dfsi->dfs_cont) != 0)
			continue;

		DFUSE_TRA_DEBUG(ie, "Reusing dfs %p", dfsi);
		dfs = dfsi;
		break;
	}

	if (!dfs) {
		D_ALLOC_PTR(dfs);
		if (dfs == NULL)
			D_GOTO(out_pool, ret = ENOMEM);

		dfs->dfs_ops = ie->ie_dfs->dfs_ops;
		DFUSE_TRA_UP(dfs, dfp, "dfs");
		d_list_add(&dfs->dfs_list, &dfp->dfp_dfs_list);
		uuid_copy(dfs->dfs_cont, dattr.da_cuuid);

		dfuse_dfs_init(dfs, ie->ie_dfs);

		/* Try to open the DAOS container (the mountpoint) */
		rc = daos_cont_open(dfp->dfp_poh, dfs->dfs_cont, DAOS_COO_RW,
				    &dfs->dfs_coh, &dfs->dfs_co_info,
				    NULL);
		if (rc) {
			DFUSE_LOG_ERROR("Failed container open (%d)",
					rc);
			D_GOTO(out_pool, ret = daos_der2errno(rc));
		}

		rc = dfs_mount(dfp->dfp_poh, dfs->dfs_coh, O_RDWR,
			       &dfs->dfs_ns);
		if (rc) {
			DFUSE_LOG_ERROR("dfs_mount failed (%d)", rc);
			D_GOTO(out_cont, ret = rc);
		}
		ie->ie_root = true;
	}

	rc = dfs_release(ie->ie_obj);
	if (rc) {
		DFUSE_TRA_ERROR(dfs, "dfs_release() failed: (%s)",
				strerror(rc));
		D_GOTO(out_umount, ret = rc);
	}

	rc = dfs_lookup(dfs->dfs_ns, "/", O_RDONLY, &ie->ie_obj,
			NULL, NULL);
	if (rc) {
		DFUSE_TRA_ERROR(dfs, "dfs_lookup() failed: (%s)",
				strerror(rc));
		D_GOTO(out_umount, ret = rc);
	}

	ie->ie_dfs = dfs;

	rc = dfs_obj2id(ie->ie_obj, &oid);
	if (rc)
		D_GOTO(out_umount, ret = rc);

	rc = dfuse_lookup_inode(fs_handle, dfs, NULL,
				&ie->ie_stat.st_ino);
	if (rc)
		D_GOTO(out_umount, ret = rc);

	dfs->dfs_root = ie->ie_stat.st_ino;
	dfs->dfs_dfp = dfp;

	DFUSE_TRA_INFO(dfs, "UNS entry point activated, root %lu",
		       dfs->dfs_root);

	D_MUTEX_UNLOCK(&fs_handle->dpi_info->di_lock);
	return 0;
out_umount:
	if (new_cont) {
		rc = dfs_umount(dfs->dfs_ns);
		if (rc)
			DFUSE_TRA_ERROR(dfs, "dfs_umount() failed %d", rc);
	}
out_cont:
	if (new_cont) {
		rc = daos_cont_close(dfs->dfs_coh, NULL);
		if (rc)
			DFUSE_TRA_ERROR(dfs, "daos_cont_close() failed %d", rc);
	}
out_pool:
	if (new_pool) {
		rc = daos_pool_disconnect(dfp->dfp_poh, NULL);
		if (rc)
			DFUSE_TRA_ERROR(dfs,
					"daos_pool_disconnect() failed %d", rc);
	}
out_err:
	if (new_cont) {
		d_list_del(&dfs->dfs_list);
		D_FREE(dfs);
	}
	if (new_pool) {
		d_list_del(&dfp->dfp_list);
		D_FREE(dfp);
	}
	D_MUTEX_UNLOCK(&fs_handle->dpi_info->di_lock);
	return ret;
}

void
dfuse_cb_lookup(fuse_req_t req, struct dfuse_inode_entry *parent,
		const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*ie = NULL;
	int				rc;

	DFUSE_TRA_DEBUG(fs_handle,
			"Parent:%lu '%s'", parent->ie_stat.st_ino, name);

	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(ie, parent, "inode");

	ie->ie_parent = parent->ie_stat.st_ino;
	ie->ie_dfs = parent->ie_dfs;

	rc = dfs_lookup_rel(parent->ie_dfs->dfs_ns, parent->ie_obj, name,
			    O_RDONLY, &ie->ie_obj, NULL, &ie->ie_stat);
	if (rc) {
		DFUSE_TRA_DEBUG(parent, "dfs_lookup() failed: (%s)",
				strerror(rc));

		if (rc == ENOENT && ie->ie_dfs->dfs_attr_timeout > 0) {
			struct fuse_entry_param entry = {};

			entry.entry_timeout = ie->ie_dfs->dfs_attr_timeout;

			DFUSE_REPLY_ENTRY(parent, req, entry);
			D_GOTO(free, 0);
		}

		D_GOTO(err, rc);
	}

	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_name[NAME_MAX] = '\0';
	atomic_store_relaxed(&ie->ie_ref, 1);

	if (S_ISDIR(ie->ie_stat.st_mode)) {
		rc = check_for_uns_ep(fs_handle, ie);
		DFUSE_TRA_DEBUG(ie,
				"check_for_uns_ep() returned %d", rc);
		if (rc)
			D_GOTO(err, rc);
	}

	dfuse_reply_entry(fs_handle, ie, NULL, req);
	return;

err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
free:
	D_FREE(ie);
}
