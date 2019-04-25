/**
 * (C) Copyright 2017-2019 Intel Corporation.
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

int
find_inode(struct dfuse_request *request)
{
	struct dfuse_projection_info *fs_handle = request->fsh;
	struct dfuse_inode_entry *ie;
	d_list_t *rlink;

	rlink = d_hash_rec_find(&fs_handle->inode_ht,
				&request->ir_inode_num,
				sizeof(request->ir_inode_num));
	if (!rlink)
		return ENOENT;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	request->ir_inode = ie;
	return 0;
}

static void
drop_ino_ref(struct dfuse_projection_info *fs_handle, ino_t ino)
{
	d_list_t *rlink;

	rlink = d_hash_rec_find(&fs_handle->inode_ht, &ino, sizeof(ino));

	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Could not find entry %lu", ino);
		return;
	}
	d_hash_rec_ndecref(&fs_handle->inode_ht, 2, rlink);
}

void
ie_close(struct dfuse_projection_info *fs_handle, struct dfuse_inode_entry *ie)
{
	int			rc;
	int			ref = atomic_load_consume(&ie->ie_ref);

	DFUSE_TRA_DEBUG(ie, "closing, inode %lu ref %u, name '%s', parent %lu",
			ie->stat.st_ino, ref, ie->name, ie->parent);

	D_ASSERT(ref == 0);

	if (ie->parent != 0) {
		drop_ino_ref(fs_handle, ie->parent);
	}

	rc = dfs_release(ie->obj);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(ie, "dfs_release failed: %d", rc);
	}
	D_FREE(ie);
}
