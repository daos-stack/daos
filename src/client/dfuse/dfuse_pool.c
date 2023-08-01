/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"
#include "daos_fs.h"
#include "daos_api.h"
#include "daos_security.h"

/* Lookup a pool */
void
dfuse_pool_lookup(fuse_req_t req, struct dfuse_inode_entry *parent, const char *name)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_inode_entry *ie         = NULL;
	struct dfuse_cont        *dfc        = NULL;
	struct dfuse_pool        *dfp        = NULL;
	daos_prop_t              *prop       = NULL;
	struct daos_prop_entry   *prop_entry;
	daos_pool_info_t          pool_info = {};
	d_list_t                 *rlink;
	int                       rc;
	uuid_t                    pool;
	uuid_t                    cont = {};

	/*
	 * This code is only supposed to support one level of directory descent
	 * so check that the lookup is relative to the root of the sub-tree, and
	 * abort if not.
	 */
	D_ASSERT(parent->ie_stat.st_ino == parent->ie_dfs->dfs_ino);

	/*
	 * Dentry names with invalid uuids cannot possibly be added. In this
	 * case, return the negative dentry with a timeout to prevent future
	 * lookups.
	 */
	if (uuid_parse(name, pool) < 0) {
		struct fuse_entry_param entry = {.entry_timeout = 60};

		DFUSE_TRA_DEBUG(parent, "Invalid pool uuid '%s'", name);
		DFUSE_REPLY_ENTRY(parent, req, entry);
		return;
	}

	DFUSE_TRA_DEBUG(parent, "Lookup of " DF_UUID, DP_UUID(pool));

	rc = dfuse_pool_get_handle(dfuse_info, pool, &dfp);
	if (rc != 0)
		goto err;

	rc = dfuse_cont_open(dfuse_info, dfp, &cont, &dfc);
	if (rc != 0)
		goto err;

	/* Drop the reference on the pool */
	d_hash_rec_decref(&dfuse_info->di_pool_table, &dfp->dfp_entry);

	rlink = d_hash_rec_find(&dfuse_info->dpi_iet, &dfc->dfs_ino, sizeof(dfc->dfs_ino));
	if (rlink) {
		struct fuse_entry_param entry = {0};

		ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

		DFUSE_TRA_INFO(ie, "Reusing existing pool entry without reconnect");

		d_hash_rec_decref(&dfp->dfp_cont_table, &dfc->dfs_entry);
		entry.attr          = ie->ie_stat;
		entry.attr_timeout  = dfc->dfc_attr_timeout;
		entry.entry_timeout = dfc->dfc_dentry_dir_timeout;
		entry.generation    = 1;
		entry.ino           = entry.attr.st_ino;
		DFUSE_REPLY_ENTRY(ie, req, entry);
		return;
	}

	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(decref, rc = ENOMEM);

	DFUSE_TRA_UP(ie, parent, "inode");

	dfuse_ie_init(dfuse_info, ie);

	ie->ie_parent = parent->ie_stat.st_ino;
	strncpy(ie->ie_name, name, NAME_MAX);

	ie->ie_dfs = dfc;

	prop = daos_prop_alloc(0);
	if (prop == NULL) {
		DFUSE_TRA_ERROR(dfp, "Failed to allocate pool property");
		D_GOTO(decref, rc = ENOMEM);
	}

	rc = daos_pool_query(dfp->dfp_poh, NULL, &pool_info, prop, NULL);
	if (rc) {
		DFUSE_TRA_ERROR(dfp, "daos_pool_query() failed: (%d)", rc);
		D_GOTO(decref, rc = daos_der2errno(rc));
	}

	/* Convert the owner information to uid/gid */
	prop_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_OWNER);
	D_ASSERT(prop_entry != NULL);
	rc = daos_acl_principal_to_uid(prop_entry->dpe_str, &ie->ie_stat.st_uid);
	if (rc != 0) {
		DFUSE_TRA_ERROR(dfp, "Unable to convert owner to uid: (%d)", rc);
		D_GOTO(decref, rc = daos_der2errno(rc));
	}

	prop_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_OWNER_GROUP);
	D_ASSERT(prop_entry != NULL);
	rc = daos_acl_principal_to_gid(prop_entry->dpe_str, &ie->ie_stat.st_gid);
	if (rc != 0) {
		DFUSE_TRA_ERROR(dfp, "Unable to convert owner-group to gid: (%d)", rc);
		D_GOTO(decref, rc = daos_der2errno(rc));
	}

	/*
	 * TODO: This should inspect ACLs and correctly construct the st_mode
	 * value accordingly.
	 */
	ie->ie_stat.st_mode = 0700 | S_IFDIR;

	daos_prop_free(prop);

	ie->ie_stat.st_ino = dfc->dfs_ino;

	dfuse_reply_entry(dfuse_info, ie, NULL, true, req);

	return;
decref:
	d_hash_rec_decref(&dfuse_info->di_pool_table, &dfp->dfp_entry);
	dfuse_ie_free(dfuse_info, ie);
	daos_prop_free(prop);
err:
	if (rc == ENOENT) {
		struct fuse_entry_param entry = {0};

		entry.entry_timeout = parent->ie_dfs->dfc_ndentry_timeout;
		DFUSE_REPLY_ENTRY(parent, req, entry);
	} else {
		DFUSE_REPLY_ERR_RAW(parent, req, rc);
	}
}
