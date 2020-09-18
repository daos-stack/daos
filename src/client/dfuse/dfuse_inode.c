/**
 * (C) Copyright 2017-2020 Intel Corporation.
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
#include "daos_api.h"

/* Lookup a the union inode number for the specific dfs/oid combination
 * allocating a new one if necessary.
 */
int
dfuse_lookup_inode(struct dfuse_projection_info *fs_handle,
		   struct dfuse_dfs *dfs,
		   daos_obj_id_t *oid,
		   ino_t *_ino)
{
	struct dfuse_inode_record	*dfir;
	d_list_t			*rlink;
	int				rc = 0;

	D_ALLOC_PTR(dfir);
	if (!dfir)
		D_GOTO(out, rc = ENOMEM);

	if (oid) {
		dfir->ir_id.irid_oid.lo = oid->lo;
		dfir->ir_id.irid_oid.hi = oid->hi;
	}

	dfir->ir_ino = atomic_fetch_add_relaxed(&fs_handle->dpi_ino_next, 1);
	dfir->ir_id.irid_dfs = dfs;

	rlink = d_hash_rec_find_insert(&fs_handle->dpi_irt,
				       &dfir->ir_id,
				       sizeof(dfir->ir_id),
				       &dfir->ir_htl);

	if (rlink != &dfir->ir_htl) {
		D_FREE(dfir);
		dfir = container_of(rlink, struct dfuse_inode_record, ir_htl);
	}

	*_ino = dfir->ir_ino;
out:
	return rc;
};

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
	struct dfuse_inode_record	*dfir;
	struct dfuse_inode_record_id	ir_id = {0};
	d_list_t			*rlink;
	struct dfuse_inode_entry	*entry;

	ir_id.irid_dfs = dfs;

	rlink = d_hash_rec_find(&fs_handle->dpi_irt,
				&ir_id,
				sizeof(ir_id));

	if (!rlink)
		return -DER_NONEXIST;

	dfir = container_of(rlink, struct dfuse_inode_record, ir_htl);

	rlink = d_hash_rec_find(&fs_handle->dpi_iet,
				&dfir->ir_ino,
				sizeof(dfir->ir_ino));
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

	DFUSE_TRA_DEBUG(ie, "closing, inode %lu ref %u, name '%s', parent %lu",
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

		DFUSE_TRA_INFO(ie->ie_dfs, "Closing dfs_root %d %d",
			       !daos_handle_is_inval(dfp->dfp_poh),
			       !daos_handle_is_inval(dfs->dfs_coh));

		if (!daos_handle_is_inval(dfs->dfs_coh)) {
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
			if (!daos_handle_is_inval(dfp->dfp_poh)) {
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
