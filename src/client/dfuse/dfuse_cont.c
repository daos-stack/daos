/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"
#include "daos_fs.h"
#include "daos_api.h"

/* Lookup a container within a pool */
static void
dfuse_cont_helper(fuse_req_t req, struct dfuse_inode_entry *parent,
		  const char *name, bool create)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*ie = NULL;
	struct dfuse_pool		*dfp = parent->ie_dfs->dfs_dfp;
	struct dfuse_cont		*dfc = NULL;
	d_list_t			*rlink;
	uuid_t				cont;
	int				rc;

	/* This code is only supposed to support one level of directory descent
	 * so check that the lookup is relative to the root of the sub-tree,
	 * and abort if not.
	 */
	D_ASSERT(parent->ie_stat.st_ino == parent->ie_dfs->dfs_ino);

	/* Dentry names where are not valid uuids cannot possibly be added so in
	 * this case return the negative dentry with a timeout to prevent future
	 * lookups.
	 */
	if (uuid_parse(name, cont) < 0) {
		struct fuse_entry_param entry = {.entry_timeout = 60};

		DFUSE_TRA_INFO(parent, "Invalid container uuid");
		DFUSE_REPLY_ENTRY(parent, req, entry);
		return;
	}

	DFUSE_TRA_DEBUG(parent, "Lookup of "DF_UUID" create %d",
			DP_UUID(cont), create);

	if (create) {
		D_ALLOC_PTR(dfc);
		if (!dfc)
			D_GOTO(err, rc = ENOMEM);

		rc = dfs_cont_create(dfp->dfp_poh, cont, NULL,
				     &dfc->dfs_coh, &dfc->dfs_ns);
		if (rc) {
			DFUSE_TRA_ERROR(parent,
					"dfs_cont_create() failed: (%d)",
					rc);
			D_FREE(dfc);
			D_GOTO(err, rc);
		}
	}

	rc = dfuse_cont_open(fs_handle, dfp, &cont, &dfc);
	if (rc)
		D_GOTO(err, rc);

	rlink = d_hash_rec_find(&fs_handle->dpi_iet,
				&dfc->dfs_ino,
				sizeof(dfc->dfs_ino));
	if (rlink) {
		struct fuse_entry_param entry = {0};

		ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

		DFUSE_TRA_INFO(ie,
			       "Reusing existing container entry without reconnect");

		/* Update the stat information, but copy in the
		 * inode value afterwards.
		 */
		rc = dfs_ostat(ie->ie_dfs->dfs_ns, ie->ie_obj,
			       &entry.attr);
		if (rc) {
			DFUSE_TRA_ERROR(ie,
					"dfs_ostat() failed: (%s)",
					strerror(rc));
			D_GOTO(decref, rc);
		}

		d_hash_rec_decref(&dfp->dfp_cont_table, &dfc->dfs_entry);
		entry.attr.st_ino = ie->ie_stat.st_ino;
		entry.attr_timeout = dfc->dfc_attr_timeout;
		entry.entry_timeout = dfc->dfc_dentry_dir_timeout;
		entry.generation = 1;
		entry.ino = entry.attr.st_ino;
		DFUSE_REPLY_ENTRY(ie, req, entry);
		return;
	}

	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(close, rc = ENOMEM);

	DFUSE_TRA_UP(ie, parent, "inode");

	rc = dfs_lookup(dfc->dfs_ns, "/", O_RDWR, &ie->ie_obj, NULL,
			&ie->ie_stat);
	if (rc) {
		DFUSE_TRA_ERROR(ie, "dfs_lookup() failed: (%s)", strerror(rc));
		D_GOTO(close, rc);
	}

	ie->ie_parent = parent->ie_stat.st_ino;
	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_name[NAME_MAX] = '\0';

	atomic_store_relaxed(&ie->ie_ref, 1);
	ie->ie_dfs = dfc;

	ie->ie_stat.st_ino = dfc->dfs_ino;

	dfs_obj2id(ie->ie_obj, &ie->ie_oid);

	dfuse_reply_entry(fs_handle, ie, NULL, true, req);
	return;
close:
	D_FREE(ie);
decref:
	d_hash_rec_decref(&dfp->dfp_cont_table, &dfc->dfs_entry);
err:
	if (rc == ENOENT) {
		struct fuse_entry_param entry = {0};

		entry.entry_timeout = parent->ie_dfs->dfc_ndentry_timeout;
		DFUSE_REPLY_ENTRY(parent, req, entry);
	} else {
		DFUSE_REPLY_ERR_RAW(parent, req, rc);
	}
}

void
dfuse_cont_lookup(fuse_req_t req, struct dfuse_inode_entry *parent,
		  const char *name)
{
	dfuse_cont_helper(req, parent, name, false);
}

void
dfuse_cont_mknod(fuse_req_t req, struct dfuse_inode_entry *parent,
		 const char *name, mode_t mode)
{
	if (!S_ISDIR(mode)) {
		DFUSE_REPLY_ERR_RAW(parent, req, ENOTSUP);
		return;
	}
	dfuse_cont_helper(req, parent, name, true);
}
