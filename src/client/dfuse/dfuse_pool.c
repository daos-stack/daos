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
#include "daos_security.h"

/* Lookup a pool */
bool
dfuse_pool_lookup(fuse_req_t req, struct dfuse_inode_entry *parent,
		  const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_info		*dfuse_info = fs_handle->dfuse_info;
	struct dfuse_inode_entry	*ie = NULL;
	struct dfuse_dfs		*dfs = NULL;
	daos_prop_t			*prop = NULL;
	struct daos_prop_entry		*prop_entry;
	uuid_t				pool_uuid;
	daos_pool_info_t		pool_info;
	int				rc;

	/* This code is only supposed to support one level of directory descent
	 * so check that the lookup is relative to the root of the sub-tree,
	 * and abort if not.
	 */
	D_ASSERT(parent->ie_stat.st_ino == parent->ie_dfs->dffs_root);

	/* Dentry names where are not valid uuids cannot possibly be added so in
	 * this case return the negative dentry with a timeout to prevent future
	 * lookups.
	 */
	if (uuid_parse(name, pool_uuid) < 0) {
		struct fuse_entry_param entry = {.entry_timeout = 60};

		DFUSE_LOG_ERROR("Invalid container uuid");
		DFUSE_REPLY_ENTRY(req, entry);
		return false;
	}

	D_ALLOC_PTR(dfs);
	if (!dfs) {
		D_GOTO(err, rc = ENOMEM);
	}
	strncpy(dfs->dffs_pool, name, NAME_MAX);
	dfs->dffs_pool[NAME_MAX] = '\0';

	{
		struct fuse_entry_param	entry = {0};

		rc = dfuse_check_for_inode(fs_handle, dfs, &ie);
		if (rc == -DER_SUCCESS) {

			DFUSE_TRA_INFO(ie, "Reusing existing pool entry "
				       "without reconnect");

			D_FREE(dfs);

			entry.attr = ie->ie_stat;
			entry.generation = 1;
			entry.ino = entry.attr.st_ino;
			DFUSE_REPLY_ENTRY(req, entry);
			return true;
		}
	}

	rc = daos_pool_connect(pool_uuid, dfuse_info->dfi_dfd.group,
			       dfuse_info->dfi_dfd.svcl, DAOS_PC_RW,
			       &dfs->dffs_poh, &dfs->dffs_pool_info,
			       NULL);
	if (rc != -DER_SUCCESS) {
		DFUSE_LOG_ERROR("daos_pool_connect() failed: (%d)",
				rc);
		D_GOTO(err, rc = daos_der2errno(rc));
	}

	D_ALLOC_PTR(ie);
	if (!ie) {
		D_GOTO(close, rc = ENOMEM);
	}

	ie->ie_parent = parent->ie_stat.st_ino;
	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_name[NAME_MAX] = '\0';

	atomic_fetch_add(&ie->ie_ref, 1);
	ie->ie_dfs = dfs;

	prop = daos_prop_alloc(0);
	if (prop == NULL) {
		DFUSE_LOG_ERROR("Failed to allocate pool property");
		D_GOTO(close, rc = ENOMEM);
	}

	rc = daos_pool_query(dfs->dffs_poh, NULL, &pool_info, prop, NULL);
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
		DFUSE_LOG_ERROR("Unable to convert owner to uid: (%d)", rc);
		D_GOTO(close, rc);
	}

	prop_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_OWNER_GROUP);
	D_ASSERT(prop_entry != NULL);
	rc = daos_acl_principal_to_gid(prop_entry->dpe_str,
				       &ie->ie_stat.st_gid);
	if (rc != 0) {
		DFUSE_LOG_ERROR("Unable to convert owner-group to gid: (%d)",
				rc);
		D_GOTO(close, rc);
	}

	/* TODO: This should inspect ACLs and correctly construct a correct
	 * st_mode value accordingly.
	 */
	ie->ie_stat.st_mode = 0700 | S_IFDIR;

	daos_prop_free(prop);

	rc = dfuse_lookup_inode(fs_handle,
				ie->ie_dfs,
				NULL,
				&ie->ie_stat.st_ino);
	if (rc) {
		DFUSE_TRA_ERROR(ie, "dfuse_lookup_inode() failed: (%d)", rc);
		D_GOTO(close, rc);
	}

	dfs->dffs_root = ie->ie_stat.st_ino;
	dfs->dffs_ops = &dfuse_cont_ops;

	dfuse_reply_entry(fs_handle, ie, false, req);
	return true;
close:
	daos_pool_disconnect(dfs->dffs_poh, NULL);
	D_FREE(ie);
	daos_prop_free(prop);

err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	D_FREE(dfs);
	return false;
}
