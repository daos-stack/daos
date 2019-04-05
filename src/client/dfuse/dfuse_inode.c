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

#define POOL_NAME close_da
#define TYPE_NAME common_req
#define REQ_NAME request
#define STAT_KEY release
#include "dfuse_ops.h"

/* Find a GAH from a inode, return 0 if found */
int
find_gah(struct dfuse_projection_info *fs_handle, ino_t ino,
	 struct ios_gah *gah)
{
	struct dfuse_inode_entry *ie;
	d_list_t *rlink;

	if (ino == 1) {
		D_MUTEX_LOCK(&fs_handle->gah_lock);
		*gah = fs_handle->gah;
		D_MUTEX_UNLOCK(&fs_handle->gah_lock);
		return 0;
	}

	rlink = d_hash_rec_find(&fs_handle->inode_ht, &ino, sizeof(ino));
	if (!rlink)
		return ENOENT;

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	DFUSE_TRA_INFO(ie, "Inode %lu " GAH_PRINT_STR, ie->stat.st_ino,
		       GAH_PRINT_VAL(ie->gah));

	D_MUTEX_LOCK(&fs_handle->gah_lock);
	*gah = ie->gah;
	D_MUTEX_UNLOCK(&fs_handle->gah_lock);

	/* Once the GAH has been copied drop the reference on the parent inode
	 */
	d_hash_rec_decref(&fs_handle->inode_ht, rlink);
	return 0;
}

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

	DFUSE_TRA_INFO(ie, "Using inode %lu " GAH_PRINT_STR " parent %lu",
		       ie->stat.st_ino, GAH_PRINT_VAL(ie->gah), ie->parent);

	request->ir_inode = ie;
	return 0;
}

/* Drop a reference on the GAH in the hash table
 *
 * TODO: Merge this with dfuse_forget_one()
 *
 */
static void
drop_ino_ref(struct dfuse_projection_info *fs_handle, ino_t ino)
{
	d_list_t *rlink;

	if (ino == 1)
		return;

	if (ino == 0)
		return;

	rlink = d_hash_rec_find(&fs_handle->inode_ht, &ino, sizeof(ino));

	if (!rlink) {
		DFUSE_TRA_WARNING(fs_handle, "Could not find entry %lu", ino);
		return;
	}
	d_hash_rec_ndecref(&fs_handle->inode_ht, 2, rlink);
}

static bool
ie_close_cb(struct dfuse_request *request)
{
	struct TYPE_NAME	*desc = CONTAINER(request);

	DFUSE_TRA_DOWN(request);
	dfuse_da_release(desc->request.fsh->close_da, desc);
	return false;
}

static const struct dfuse_request_api api = {
	.on_result	= ie_close_cb,
};

void ie_close(struct dfuse_projection_info *fs_handle,
	      struct dfuse_inode_entry *ie)
{
	struct TYPE_NAME	*desc = NULL;
	struct dfuse_gah_in	*in;
	int			rc;
	int			ref = atomic_load_consume(&ie->ie_ref);

	DFUSE_TRA_DEBUG(ie, "closing, ref %u, parent %lu", ref, ie->parent);

	D_ASSERT(ref == 0);
	atomic_fetch_add(&ie->ie_ref, 1);

	drop_ino_ref(fs_handle, ie->parent);

	DFUSE_TRA_INFO(ie, GAH_PRINT_STR, GAH_PRINT_VAL(ie->gah));

	DFUSE_REQ_INIT(desc, fs_handle, api, in, rc);
	if (rc)
		D_GOTO(err, 0);

	DFUSE_TRA_UP(&desc->request, ie, "close_req");

	D_MUTEX_LOCK(&fs_handle->gah_lock);
	in->gah = ie->gah;
	D_MUTEX_UNLOCK(&fs_handle->gah_lock);

	rc = dfuse_fs_send(&desc->request);
	if (rc != 0)
		D_GOTO(err, 0);

	DFUSE_TRA_DOWN(ie);
	return;

err:
	DFUSE_TRA_ERROR(ie, "Failed to close " GAH_PRINT_STR " %d",
			GAH_PRINT_VAL(ie->gah), rc);

	DFUSE_TRA_DOWN(ie);
	if (desc)
		dfuse_da_release(fs_handle->close_da, desc);
}
