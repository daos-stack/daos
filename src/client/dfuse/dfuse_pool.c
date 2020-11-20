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
#include "daos_fs.h"
#include "daos_api.h"
#include "daos_security.h"

/* Lookup a pool */
void
dfuse_pool_lookup(fuse_req_t req, struct dfuse_inode_entry *parent,
		  const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_info		*dfuse_info = fs_handle->dpi_info;
	struct dfuse_inode_entry	*ie = NULL;
	struct dfuse_dfs		*dfs = NULL;
	struct dfuse_dfs		*dfsi;
	struct dfuse_pool		*dfp = NULL;
	struct dfuse_pool		*dfpi;
	daos_prop_t			*prop = NULL;
	struct daos_prop_entry		*prop_entry;
	daos_pool_info_t		pool_info = {};
	struct fuse_entry_param		entry = {0};
	int				rc;

	/*
	 * This code is only supposed to support one level of directory descent
	 * so check that the lookup is relative to the root of the sub-tree, and
	 * abort if not.
	 */
	D_ASSERT(parent->ie_stat.st_ino == parent->ie_dfs->dfs_root);

	D_ALLOC_PTR(dfp);
	if (!dfp)
		D_GOTO(err, rc = ENOMEM);

	D_INIT_LIST_HEAD(&dfp->dfp_dfs_list);

	/*
	 * Dentry names with invalid uuids cannot possibly be added. In this
	 * case, return the negative dentry with a timeout to prevent future
	 * lookups.
	 */
	if (uuid_parse(name, dfp->dfp_pool) < 0) {
		entry.entry_timeout = 60;
		DFUSE_TRA_INFO(parent, "Invalid container uuid");
		DFUSE_REPLY_ENTRY(parent, req, entry);
		D_FREE(dfp);
		return;
	}

	D_MUTEX_LOCK(&fs_handle->dpi_info->di_lock);

	d_list_for_each_entry(dfpi,
			      &fs_handle->dpi_info->di_dfp_list,
			      dfp_list) {
		if (uuid_compare(dfp->dfp_pool, dfpi->dfp_pool) != 0)
			continue;

		d_list_for_each_entry(dfsi,
				      &dfpi->dfp_dfs_list,
				      dfs_list) {
			{
				if (uuid_is_null(dfsi->dfs_cont) != 1)
					continue;

				DFUSE_TRA_INFO(dfpi, "Found existing pool");

				rc = dfuse_check_for_inode(fs_handle, dfsi,
							   &ie);
				D_ASSERT(rc == -DER_SUCCESS);

				DFUSE_TRA_INFO(ie,
					       "Reusing existing pool entry without reconnect");
				entry.attr = ie->ie_stat;
				entry.generation = 1;
				entry.ino = entry.attr.st_ino;
				DFUSE_REPLY_ENTRY(ie, req, entry);
				D_MUTEX_UNLOCK(&fs_handle->dpi_info->di_lock);
				D_FREE(dfp);
				return;
			}
		}
	}

	D_ALLOC_PTR(dfs);
	if (!dfs)
		D_GOTO(err_unlock, rc = ENOMEM);

	dfuse_dfs_init(dfs, parent->ie_dfs);

	d_list_add(&dfs->dfs_list, &dfp->dfp_dfs_list);
	dfs->dfs_dfp = dfp;

	DFUSE_TRA_UP(dfp, parent->ie_dfs->dfs_dfp, "dfp");

	DFUSE_TRA_UP(dfs, dfp, "dfs");

	rc = daos_pool_connect(dfp->dfp_pool, dfuse_info->di_group,
			       dfuse_info->di_svcl, DAOS_PC_RW,
			       &dfp->dfp_poh, &dfp->dfp_pool_info,
			       NULL);
	if (rc) {
		DFUSE_TRA_ERROR(dfp, "daos_pool_connect() failed: (%d)", rc);

		/* This is the error you get when the agent isn't started
		 * and EHOSTUNREACH seems to better reflect this than ENOTDIR
		 */
		if (rc == -DER_BADPATH)
			D_GOTO(err_unlock, rc = EHOSTUNREACH);

		D_GOTO(err_unlock, rc = daos_der2errno(rc));
	}

	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(close, rc = ENOMEM);

	ie->ie_root = true;

	DFUSE_TRA_UP(ie, parent, "inode");

	ie->ie_parent = parent->ie_stat.st_ino;
	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_name[NAME_MAX] = '\0';

	atomic_store_relaxed(&ie->ie_ref, 1);
	ie->ie_dfs = dfs;

	prop = daos_prop_alloc(0);
	if (prop == NULL) {
		DFUSE_TRA_ERROR(dfp, "Failed to allocate pool property");
		D_GOTO(close, rc = ENOMEM);
	}

	rc = daos_pool_query(dfp->dfp_poh, NULL, &pool_info, prop, NULL);
	if (rc) {
		DFUSE_TRA_ERROR(ie, "daos_pool_query() failed: (%d)", rc);
		D_GOTO(close, rc = daos_der2errno(rc));
	}

	/* Convert the owner information to uid/gid */
	prop_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_OWNER);
	D_ASSERT(prop_entry != NULL);
	rc = daos_acl_principal_to_uid(prop_entry->dpe_str,
				       &ie->ie_stat.st_uid);
	if (rc != 0) {
		DFUSE_TRA_ERROR(dfp, "Unable to convert owner to uid: (%d)",
				rc);
		D_GOTO(close, rc = daos_der2errno(rc));
	}

	prop_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_OWNER_GROUP);
	D_ASSERT(prop_entry != NULL);
	rc = daos_acl_principal_to_gid(prop_entry->dpe_str,
				       &ie->ie_stat.st_gid);
	if (rc != 0) {
		DFUSE_TRA_ERROR(dfp,
				"Unable to convert owner-group to gid: (%d)",
				rc);
		D_GOTO(close, rc = daos_der2errno(rc));
	}

	/*
	 * TODO: This should inspect ACLs and correctly construct the st_mode
	 * value accordingly.
	 */
	ie->ie_stat.st_mode = 0700 | S_IFDIR;

	daos_prop_free(prop);

	d_list_add(&dfp->dfp_list, &fs_handle->dpi_info->di_dfp_list);

	dfs->dfs_ino = atomic_fetch_add_relaxed(&fs_handle->dpi_ino_next, 1);

	dfs->dfs_root = dfs->dfs_ino;
	ie->ie_stat.st_ino = dfs->dfs_ino;
	dfs->dfs_ops = &dfuse_cont_ops;

	dfuse_reply_entry(fs_handle, ie, NULL, req);

	D_MUTEX_UNLOCK(&fs_handle->dpi_info->di_lock);
	return;
close:
	daos_pool_disconnect(dfp->dfp_poh, NULL);
	D_FREE(ie);
	daos_prop_free(prop);
err_unlock:
	D_MUTEX_UNLOCK(&fs_handle->dpi_info->di_lock);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	D_FREE(dfs);
	D_FREE(dfp);
}
