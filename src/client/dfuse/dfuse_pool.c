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
#include "daos_fs.h"
#include "daos_api.h"

/* Lookup a container within a pool */
static bool
dfuse_pool_connect(fuse_req_t req, struct dfuse_inode_entry *parent,
		const char *name, bool create)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_info *dfuse_info = fs_handle->dfuse_info;
	struct dfuse_inode_entry	*inode = NULL;
	struct dfuse_dfs		*dfs = NULL;
	uuid_t				co_uuid;
	dfs_t				*ddfs;
	mode_t				mode;
	int rc;

	/* This code is only supposed to support one level of directory descent
	 * so check that the lookup is relative to the root of the sub-tree,
	 * and abort if not.
	 */
	if (parent->stat.st_ino == parent->ie_dfs->dffs_root) {
		DFUSE_TRA_ERROR(parent, "Called on non sub-tree root");
		D_GOTO(err, rc = EIO);
	}

	if (uuid_parse(name, co_uuid) < 0) {
		DFUSE_LOG_ERROR("Invalid container uuid");
		D_GOTO(err, rc = ENOENT);
	}

	/* If looking up an existing file then check to see if dfuse is already
	 * connected to the name.
	 */
	if (!create) {
		struct dfuse_d_child *ddc;

		dfs = parent->ie_dfs;

		d_list_for_each_entry(ddc, &dfs->dffs_child, ddc_list) {
			if ((strncmp(name, ddc->ddc_name, NAME_MAX)) == 0) {
				DFUSE_TRA_ERROR(parent, "Found entry");
				D_GOTO(stat, 0);
			}
		}
	}

	D_ALLOC_PTR(dfs);
	if (!dfs) {
		D_GOTO(err, rc = ENOMEM);
	}
	strncpy(dfs->dffs_cont, name, NAME_MAX);

	if (create) {
		rc = daos_cont_create(dfuse_info->dfi_poh, co_uuid,
				      NULL, NULL);
		if (rc != -DER_SUCCESS) {
			D_GOTO(err, 0);
		}
	}

	rc = daos_cont_open(dfuse_info->dfi_poh, co_uuid,
			    DAOS_COO_RW, &dfs->dffs_coh, &dfs->dffs_co_info,
			    NULL);
	if (rc == -DER_NONEXIST) {
		D_GOTO(err, rc = ENOENT);
	} else if (rc != -DER_SUCCESS) {
		D_GOTO(err, 0);
	}

	D_ALLOC_PTR(inode);
	if (!inode) {
		D_GOTO(close, rc = ENOMEM);
	}

	rc = dfs_mount(dfuse_info->dfi_poh, dfs->dffs_coh, O_RDWR, &ddfs);
	if (rc != -DER_SUCCESS) {
		DFUSE_LOG_ERROR("dfs_mount failed (%d)", rc);
		D_GOTO(close, 0);
	}

	dfs->dffs_dfs = ddfs;

	rc = dfs_lookup(dfs->dffs_dfs, "/", O_RDONLY, &inode->obj, &mode);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(inode, "dfs_lookup() failed: %d",
				rc);
		D_GOTO(close, 0);
	}

	inode->parent = parent->stat.st_ino;
	strncpy(inode->name, name, NAME_MAX);
	dfs->dffs_root = inode->stat.st_ino;
	dfs->dffs_ops = &dfuse_dfs_ops;

stat:
	rc = dfs_ostat(dfs->dffs_dfs, inode->obj, &inode->stat);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(inode, "dfs_ostat() failed: %d",
				rc);
		D_GOTO(release, 0);
	}

	atomic_fetch_add(&inode->ie_ref, 1);
	inode->ie_dfs = dfs;
	inode->stat.st_ino = 2;

	dfuse_reply_entry(fs_handle, inode, false, req);
	return true;
release:
	dfs_release(inode->obj);
close:
	daos_cont_close(dfs->dffs_coh, NULL);
	D_FREE(inode);

err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	D_FREE(dfs);
	return false;
}

void
dfuse_pool_lookup(fuse_req_t req, struct dfuse_inode_entry *parent,
		const char *name)
{
	dfuse_pool_connect(req, parent, name, false);
}

bool
dfuse_pool_mkdir(fuse_req_t req, struct dfuse_inode_entry *parent,
		 const char *name, mode_t mode)
{
	return dfuse_pool_connect(req, parent, name, true);
}
