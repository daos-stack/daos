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

#include <fuse3/fuse.h>
#include "dfuse_common.h"
#include "dfuse.h"

#define REQ_NAME request
#define POOL_NAME lookup_da
#define TYPE_NAME entry_req
#include "dfuse_ops.h"

bool
dfuse_entry_cb(struct dfuse_request *request)
{
	struct entry_req		*desc = container_of(request, struct entry_req, request);
	struct dfuse_projection_info	*fs_handle = desc->request.fsh;
	struct dfuse_entry_out		*out = request->out;
	struct fuse_entry_param		entry = {0};
	d_list_t			*rlink;
	bool				keep_ref = false;

	DFUSE_REQUEST_RESOLVE(request, out);
	if (request->rc)
		D_GOTO(out, 0);

	entry.attr = out->stat;
	entry.generation = 1;
	entry.ino = entry.attr.st_ino;

	desc->ie->stat = out->stat;
	DFUSE_TRA_UP(desc->ie, fs_handle, "inode");
	rlink = d_hash_rec_find_insert(&fs_handle->inode_ht,
				       &desc->ie->stat.st_ino,
				       sizeof(desc->ie->stat.st_ino),
				       &desc->ie->ie_htl);

	if (rlink == &desc->ie->ie_htl) {
		desc->ie = NULL;
		keep_ref = true;
	} else {
		/* The lookup has resulted in an existing file, so reuse that
		 * entry, drop the inode in the lookup descriptor and do not
		 * keep a reference on the parent.
		 * Note that this function will be called with a reference on
		 * the parent anyway, so keep that one, but drop one in the call
		 * to ie_close().
		 */
		atomic_fetch_sub(&desc->ie->ie_ref, 1);
		keep_ref = true;
		ie_close(fs_handle, desc->ie);
	}

	DFUSE_REPLY_ENTRY(request, entry);
	dfuse_da_release(desc->da, desc);
	return keep_ref;
out:
	DFUSE_REPLY_ERR(request, request->rc);
	dfuse_da_release(desc->da, desc);
	return false;
}

static const struct dfuse_request_api api = {
	.on_result	= dfuse_entry_cb,
};

void
dfuse_cb_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*ie;
	struct dfuse_inode_entry	*inode = NULL;
	mode_t				mode;
	d_list_t			*rlink;
	int rc;

	DFUSE_TRA_INFO(fs_handle, "Parent:%lu '%s'", parent, name);

	rlink = d_hash_rec_find(&fs_handle->inode_ht, &parent, sizeof(parent));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %lu",
				parent);
		D_GOTO(err, rc = EEXIST);
	}

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	DFUSE_TRA_INFO(ie, "parent");

	D_ALLOC_PTR(inode);
	if (!inode) {
		D_GOTO(out_defref, rc = ENOMEM);
	}

	rc = dfs_lookup(fs_handle->fsh_dfs,
			name, O_RDONLY, &inode->obj, &mode);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_INFO(fs_handle, "dfs_lookup() failed: %p %d",
			       fs_handle->fsh_dfs, rc);
		if (rc == -DER_NONEXIST) {
			D_GOTO(out_defref, rc = ENOENT);
		} else {
			D_GOTO(out_defref, rc = EIO);
		}
	}

	strncpy(inode->name, name, NAME_MAX);

	DFUSE_TRA_ERROR(fs_handle, "unexpected success");
	D_GOTO(out_defref, rc = EIO);
	return;

out_defref:
	d_hash_rec_decref(&fs_handle->inode_ht, rlink);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	D_FREE(inode);
}

void
dfuse_cb_lookup_old(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct TYPE_NAME		*desc = NULL;
	int rc;

	DFUSE_TRA_INFO(fs_handle, "Parent:%lu '%s'", parent, name);
	DFUSE_REQ_INIT_REQ(desc, fs_handle, api, req, rc);
	if (rc)
		D_GOTO(err, rc);

	DFUSE_TRA_INFO(desc, "ie %p", &desc->ie);

	desc->request.ir_inode_num = parent;

	strncpy(desc->ie->name, name, NAME_MAX);
	desc->ie->parent = parent;
	desc->da = fs_handle->lookup_da;

	rc = dfuse_fs_send(&desc->request);
	if (rc != 0)
		D_GOTO(err, 0);
	return;
err:
	if (desc)
		dfuse_da_release(fs_handle->lookup_da, desc);
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}
