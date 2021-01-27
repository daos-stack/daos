/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"
#include "daos_api.h"

/* Check a DFS to see if an inode is already in place for it.  This is used
 * for looking up pools and containers to see if a record already exists to
 * allow reuse of already open handles.
 *
 * Does not store the DFS, but simply checks for matching copies, and extracts
 * the inode information from them.
 *
 * Return a inode_entry pointer, with reference held.
 */
int
dfuse_check_for_inode(struct dfuse_projection_info *fs_handle,
		      struct dfuse_dfs *dfs,
		      struct dfuse_inode_entry **_entry)
{
	d_list_t			*rlink;
	struct dfuse_inode_entry	*entry;

	rlink = d_hash_rec_find(&fs_handle->dpi_iet,
				&dfs->dfs_ino, sizeof(dfs->dfs_ino));
	if (!rlink)
		return -DER_NONEXIST;

	entry = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	*_entry = entry;

	return -DER_SUCCESS;
};

void
ie_close(struct dfuse_projection_info *fs_handle, struct dfuse_inode_entry *ie)
{
	int			rc;
	int			ref = atomic_load_relaxed(&ie->ie_ref);

	DFUSE_TRA_DEBUG(ie, "closing, inode %#lx ref %u, name '%s', parent %lu",
			ie->ie_stat.st_ino, ref, ie->ie_name, ie->ie_parent);

	D_ASSERT(ref == 0);

	if (ie->ie_obj) {
		rc = dfs_release(ie->ie_obj);
		if (rc) {
			DFUSE_TRA_ERROR(ie, "dfs_release() failed: (%s)",
					strerror(rc));
		}
	}

	if (ie->ie_root) {
		struct dfuse_dfs	*dfs = ie->ie_dfs;
		struct dfuse_pool	*dfp = dfs->dfs_dfp;

		D_MUTEX_LOCK(&fs_handle->dpi_info->di_lock);

		DFUSE_TRA_INFO(ie->ie_dfs, "Closing poh %d coh %d",
			       daos_handle_is_valid(dfp->dfp_poh),
			       daos_handle_is_valid(dfs->dfs_coh));

		if (daos_handle_is_valid(dfs->dfs_coh)) {
			rc = dfs_umount(dfs->dfs_ns);
			if (rc != 0)
				DFUSE_TRA_ERROR(dfs,
						"dfs_umount() failed (%d)",
						rc);

			rc = daos_cont_close(dfs->dfs_coh, NULL);
			if (rc != -DER_SUCCESS) {
				DFUSE_TRA_ERROR(dfs,
						"daos_cont_close() failed: (%d)",
						rc);
			}
		}

		d_list_del(&dfs->dfs_list);
		D_MUTEX_DESTROY(&dfs->dfs_read_mutex);
		D_FREE(dfs);

		if (d_list_empty(&dfp->dfp_dfs_list)) {
			if (daos_handle_is_valid(dfp->dfp_poh)) {
				rc = daos_pool_disconnect(dfp->dfp_poh, NULL);
				if (rc != -DER_SUCCESS) {
					DFUSE_TRA_ERROR(dfp,
							"daos_pool_disconnect() failed: (%d)",
							rc);
				}
			}
			d_list_del(&dfp->dfp_list);

			D_FREE(dfp);
		}
		D_MUTEX_UNLOCK(&fs_handle->dpi_info->di_lock);
	}

	DFUSE_TRA_DOWN(ie);

	D_FREE(ie);
}
