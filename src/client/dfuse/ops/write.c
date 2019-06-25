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
dfuse_cb_write(fuse_req_t req, fuse_ino_t ino, const char *buff, size_t len,
	       off_t position, struct fuse_file_info *fi)
{
	struct dfuse_projection_info	*fsh = fuse_req_userdata(req);
	struct dfuse_inode_entry	*ie;
	d_list_t			*rlink;
	d_iov_t				iov = {};
	d_sg_list_t			sgl = {};
	int				rc;

	sgl.sg_nr = 1;
	d_iov_set(&iov, (void *)buff, len);
	sgl.sg_iovs = &iov;

	if (fi && fi->fh) {
		struct dfuse_obj_hdl *oh = (struct dfuse_obj_hdl *)fi->fh;

		rc = dfs_write(oh->doh_dfs, oh->doh_obj, sgl, position);
		if (rc == 0)
			DFUSE_REPLY_WRITE(NULL, req, len);
		else
			DFUSE_REPLY_ERR_RAW(NULL, req, -rc);
		return;
	}

	rlink = d_hash_rec_find(&fsh->dpi_iet, &ino, sizeof(ino));
	if (!rlink) {
		DFUSE_TRA_ERROR(fsh, "Failed to find inode %lu", ino);
		DFUSE_REPLY_ERR_RAW(NULL, req, ENOENT);
		return;
	}

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	rc = dfs_write(ie->ie_dfs->dfs_ns, ie->ie_obj, sgl, position);
	if (rc == 0)
		DFUSE_REPLY_WRITE(ie, req, len);
	else
		DFUSE_REPLY_ERR_RAW(ie, req, -rc);

	d_hash_rec_decref(&fsh->dpi_iet, rlink);
}
