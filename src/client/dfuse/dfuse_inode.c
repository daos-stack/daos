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
#include "dfuse_ops.h"

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
	int			rc;
	int			ref = atomic_load_consume(&ie->ie_ref);

	DFUSE_TRA_DEBUG(ie, "closing, ref %u, parent %lu", ref, ie->parent);

	D_ASSERT(ref == 0);
	atomic_fetch_add(&ie->ie_ref, 1);

	drop_ino_ref(fs_handle, ie->parent);

	DFUSE_REQ_INIT(desc, fs_handle, api, in, rc);
	if (rc)
		D_GOTO(err, 0);

	DFUSE_TRA_UP(&desc->request, ie, "close_req");

	rc = dfuse_fs_send(&desc->request);
	if (rc != 0)
		D_GOTO(err, 0);

	DFUSE_TRA_DOWN(ie);
	return;

err:
	DFUSE_TRA_DOWN(ie);
	if (desc)
		dfuse_da_release(fs_handle->close_da, desc);
}
