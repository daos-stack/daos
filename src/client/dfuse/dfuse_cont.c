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
dfuse_cont_open(fuse_req_t req, struct dfuse_inode_entry *parent,
		const char *name, bool create)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_info		*dfuse_info = fs_handle->dfuse_info;
	struct dfuse_inode_entry	*ie = NULL;
	struct dfuse_dfs		*dfs = NULL;
	uuid_t				co_uuid;
	dfs_t				*ddfs;
	mode_t				mode;
	int rc;

	/* This code is only supposed to support one level of directory descent
	 * so check that the lookup is relative to the root of the sub-tree,
	 * and abort if not.
	 */
	if (parent->ie_stat.st_ino != parent->ie_dfs->dffs_root) {
		DFUSE_TRA_ERROR(parent, "Called on non sub-tree root");
		D_GOTO(err, rc = EIO);
	}

	if (uuid_parse(name, co_uuid) < 0) {
		DFUSE_LOG_ERROR("Invalid container uuid");
		D_GOTO(err, rc = ENOENT);
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

	D_ALLOC_PTR(ie);
	if (!ie) {
		D_GOTO(close, rc = ENOMEM);
	}

	rc = dfs_mount(dfuse_info->dfi_poh, dfs->dffs_coh, O_RDWR, &ddfs);
	if (rc != -DER_SUCCESS) {
		DFUSE_LOG_ERROR("dfs_mount failed (%d)", rc);
		D_GOTO(close, 0);
	}

	dfs->dffs_dfs = ddfs;

	rc = dfs_lookup(dfs->dffs_dfs, "/", O_RDONLY, &ie->ie_obj, &mode);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(ie, "dfs_lookup() failed: %d",
				rc);
		D_GOTO(close, 0);
	}

	ie->ie_parent = parent->ie_stat.st_ino;
	strncpy(ie->ie_name, name, NAME_MAX);

	rc = dfs_ostat(dfs->dffs_dfs, ie->ie_obj, &ie->ie_stat);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(ie, "dfs_ostat() failed: %d",
				rc);
		D_GOTO(release, 0);
	}

	atomic_fetch_add(&ie->ie_ref, 1);
	ie->ie_dfs = dfs;

	rc = dfuse_lookup_inode(fs_handle,
				ie->ie_dfs,
				NULL,
				&ie->ie_stat.st_ino);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(ie, "no ino");
		D_GOTO(release, rc = EIO);
	}

	dfs->dffs_root = ie->ie_stat.st_ino;
	dfs->dffs_ops = &dfuse_dfs_ops;

	dfuse_reply_entry(fs_handle, ie, false, req);
	return true;
release:
	dfs_release(ie->ie_obj);
close:
	daos_cont_close(dfs->dffs_coh, NULL);
	D_FREE(ie);

err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	D_FREE(dfs);
	return false;
}

void
dfuse_cont_lookup(fuse_req_t req, struct dfuse_inode_entry *parent,
		  const char *name)
{
	dfuse_cont_open(req, parent, name, false);
}

bool
dfuse_cont_mkdir(fuse_req_t req, struct dfuse_inode_entry *parent,
		 const char *name, mode_t mode)
{
	return dfuse_cont_open(req, parent, name, true);
}
