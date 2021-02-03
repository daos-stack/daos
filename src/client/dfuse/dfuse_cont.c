/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"
#include "daos_fs.h"
#include "daos_api.h"

#define ATTR_COUNT 4
/* Called once after container connect, after dfs_mount() */
int
dfuse_cont_init(struct dfuse_dfs *dfs)
{
	char const *const names[ATTR_COUNT] = {"dfuse-attr-timeout",
					       "dfuse-dentry",
					       "dfuse-ndentry",
					       "dfuse-data-cache"};
	size_t	size;
	char	*buff;
	int	rc;
	int	i;
	unsigned int value;

	D_ALLOC(buff, 128);
	if (buff == NULL)
		return ENOMEM;

	for (i = 0; i < ATTR_COUNT; i++) {
		size = 128;

		rc = daos_cont_get_attr(dfs->dfs_coh, 1, &names[i],
					(void * const*)&buff,
					&size, NULL);
		if (rc == -DER_NONEXIST) {
			continue;
		} else if (rc != -DER_SUCCESS) {
			D_GOTO(out, rc = daos_der2errno(rc));
			DFUSE_TRA_WARNING(dfs, "Failed to load value for '%s' "
					  DF_RC, names[i], DP_RC(rc));
		}

		if (i == 3) {
			dfs->dfs_data_caching = true;
			continue;
		}

		/* DAOS-6709 */
		buff[size] = '\0';

		rc = dfuse_parse_time(buff, &value);
		if (rc != 0) {
			DFUSE_TRA_WARNING(dfs, "Failed to parse '%s' for '%s'",
					  buff, names[i]);
			continue;
		}
		DFUSE_TRA_INFO(dfs, "setting '%s' is %u", names[i], value);
		if (i == 0)
			dfs->dfs_attr_timeout = value;
		else if (i == 1)
			dfs->dfs_dentry_timeout = value;
		else if (i == 2)
			dfs->dfs_ndentry_timeout = value;
	}
	rc = 0;
out:
	D_FREE(buff);
	return rc;
}

/* Lookup a container within a pool */
static void
dfuse_cont_open(fuse_req_t req, struct dfuse_inode_entry *parent,
		const char *name, bool create)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*ie = NULL;
	struct dfuse_pool		*dfp = parent->ie_dfs->dfs_dfp;
	struct dfuse_dfs		*dfs;
	struct dfuse_dfs		*dfsi;
	int				rc;

	/* This code is only supposed to support one level of directory descent
	 * so check that the lookup is relative to the root of the sub-tree,
	 * and abort if not.
	 */
	D_ASSERT(parent->ie_stat.st_ino == parent->ie_dfs->dfs_ino);

	D_ALLOC_PTR(dfs);
	if (!dfs)
		D_GOTO(err, rc = ENOMEM);

	dfuse_dfs_init(dfs, parent->ie_dfs);

	dfs->dfs_dfp = dfp;

	/* Dentry names where are not valid uuids cannot possibly be added so in
	 * this case return the negative dentry with a timeout to prevent future
	 * lookups.
	 */
	if (uuid_parse(name, dfs->dfs_cont) < 0) {
		struct fuse_entry_param entry = {.entry_timeout = 60};

		DFUSE_TRA_INFO(parent, "Invalid container uuid");
		DFUSE_REPLY_ENTRY(parent, req, entry);
		D_FREE(dfs);
		return;
	}

	DFUSE_TRA_UP(dfs, fs_handle, "dfs");

	D_MUTEX_LOCK(&fs_handle->dpi_info->di_lock);

	if (create) {
		rc = dfs_cont_create(dfp->dfp_poh, dfs->dfs_cont,
				     NULL, &dfs->dfs_coh, &dfs->dfs_ns);
		if (rc) {
			DFUSE_TRA_ERROR(dfs,
					"dfs_cont_create() failed: (%d)",
					rc);
			D_GOTO(err_unlock, rc);
		}
		d_list_add(&dfs->dfs_list, &dfp->dfp_dfs_list);
		goto alloc_ie;
	}

	d_list_for_each_entry(dfsi, &dfp->dfp_dfs_list, dfs_list) {
		struct fuse_entry_param	entry = {0};

		DFUSE_TRA_DEBUG(parent, "Checking %p", dfsi);

		if (uuid_compare(dfsi->dfs_cont, dfs->dfs_cont) != 0)
			continue;

		DFUSE_TRA_INFO(parent,	"Found existing container dfs %p",
			       dfsi);

		rc = dfuse_check_for_inode(fs_handle, dfsi->dfs_ino, &ie);
		D_ASSERT(rc == -DER_SUCCESS);

		DFUSE_TRA_INFO(ie, "Reusing existing container");

		/* Update the stat information, but copy in the
		 * inode value afterwards.
		 */
		rc = dfs_ostat(ie->ie_dfs->dfs_ns, ie->ie_obj, &entry.attr);
		if (rc) {
			DFUSE_TRA_ERROR(ie,
					"dfs_ostat() failed: (%s)",
					strerror(rc));
			D_GOTO(err_unlock, rc);
		}

		entry.attr.st_ino = ie->ie_stat.st_ino;
		entry.generation = 1;
		entry.ino = entry.attr.st_ino;
		entry.attr_timeout = dfsi->dfs_attr_timeout;
		entry.entry_timeout = dfsi->dfs_dentry_timeout;

		DFUSE_REPLY_ENTRY(ie, req, entry);
		D_MUTEX_UNLOCK(&fs_handle->dpi_info->di_lock);
		D_FREE(dfs);
		return;
	}

	rc = daos_cont_open(dfp->dfp_poh, dfs->dfs_cont,
			    DAOS_COO_RW, &dfs->dfs_coh, NULL, NULL);
	if (rc == -DER_NONEXIST) {
		DFUSE_TRA_INFO(dfs, "daos_cont_open() failed: (%d)", rc);
		D_GOTO(err_unlock, rc = daos_der2errno(rc));
	} else if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(dfs, "daos_cont_open() failed: (%d)", rc);
		D_GOTO(err_unlock, rc = daos_der2errno(rc));
	}

	rc = dfs_mount(dfp->dfp_poh, dfs->dfs_coh, O_RDWR, &dfs->dfs_ns);
	if (rc) {
		DFUSE_TRA_ERROR(ie, "dfs_mount() failed: (%s)", strerror(rc));
		D_GOTO(close, rc);
	}

	rc = dfuse_cont_init(dfs);
	if (rc) {
		DFUSE_TRA_ERROR(ie, "cont_init() failed: (%s)", strerror(rc));
		D_GOTO(close, rc);
	}

	d_list_add(&dfs->dfs_list, &dfp->dfp_dfs_list);

alloc_ie:
	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(close, rc = ENOMEM);

	ie->ie_root = true;

	DFUSE_TRA_UP(ie, parent, "inode");

	rc = dfs_lookup(dfs->dfs_ns, "/", O_RDWR, &ie->ie_obj, NULL,
			&ie->ie_stat);
	if (rc) {
		DFUSE_TRA_ERROR(ie, "dfs_lookup() failed: (%s)", strerror(rc));
		D_GOTO(close, rc);
	}

	ie->ie_parent = parent->ie_stat.st_ino;
	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_name[NAME_MAX] = '\0';

	atomic_store_relaxed(&ie->ie_ref, 1);
	ie->ie_dfs = dfs;

	dfs->dfs_ino = atomic_fetch_add_relaxed(&fs_handle->dpi_ino_next, 1);
	ie->ie_stat.st_ino = dfs->dfs_ino;
	dfs->dfs_ops = &dfuse_dfs_ops;

	dfuse_reply_entry(fs_handle, ie, NULL, true, req);
	D_MUTEX_UNLOCK(&fs_handle->dpi_info->di_lock);
	return;

close:
	daos_cont_close(dfs->dfs_coh, NULL);
	D_FREE(ie);
err_unlock:
	D_MUTEX_UNLOCK(&fs_handle->dpi_info->di_lock);
err:
	if (rc == ENOENT) {
		struct fuse_entry_param entry = {0};

		entry.entry_timeout = dfs->dfs_ndentry_timeout;
		DFUSE_REPLY_ENTRY(parent, req, entry);
	} else {
		DFUSE_REPLY_ERR_RAW(parent, req, rc);
	}
	D_FREE(dfs);
}

void
dfuse_cont_lookup(fuse_req_t req, struct dfuse_inode_entry *parent,
		  const char *name)
{
	dfuse_cont_open(req, parent, name, false);
}

void
dfuse_cont_mknod(fuse_req_t req, struct dfuse_inode_entry *parent,
		 const char *name, mode_t mode)
{
	if (!S_ISDIR(mode)) {
		DFUSE_REPLY_ERR_RAW(parent, req, ENOTSUP);
		return;
	}
	dfuse_cont_open(req, parent, name, true);
}
