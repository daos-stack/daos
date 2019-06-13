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

void
dfuse_cb_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*ie;
	d_list_t			*rlink;
	struct dfuse_obj_hdl		*oh = NULL;
	int				rc;

	rlink = d_hash_rec_find(&fs_handle->dfpi_iet, &ino, sizeof(ino));
	if (!rlink) {
		DFUSE_FUSE_REPLY_ERR(req, ENOENT);
		return;
	}
	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	D_ALLOC_PTR(oh);
	if (!oh)
		D_GOTO(err, rc = ENOMEM);

	/** duplicate the file handle for the fuse handle */
	rc = dfs_dup(ie->ie_dfs->dffs_dfs, ie->ie_obj, fi->flags,
		     &oh->doh_obj);
	if (rc)
		D_GOTO(err, rc = -rc);

	oh->doh_dfs = ie->ie_dfs->dffs_dfs;
	fi->direct_io = 1;
	fi->fh = (uint64_t)oh;

	d_hash_rec_decref(&fs_handle->dfpi_iet, rlink);
	DFUSE_REPLY_OPEN(req, fi);

	return;
err:
	d_hash_rec_decref(&fs_handle->dfpi_iet, rlink);
	D_FREE(oh);
	DFUSE_FUSE_REPLY_ERR(req, rc);
}

void
dfuse_cb_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl		*oh = (struct dfuse_obj_hdl *)fi->fh;
	int				rc;

	/** duplicate the file handle for the fuse handle */
	rc = dfs_release(oh->doh_obj);
	if (rc == 0)
		D_FREE(oh);
	DFUSE_FUSE_REPLY_ERR(req, -rc);
}
