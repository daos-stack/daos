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
dfuse_cb_readlink(fuse_req_t req, fuse_ino_t ino)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*inode;
	d_list_t			*rlink;
	char				*buf = NULL;
	size_t				size = 0;
	int				rc;

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &ino, sizeof(ino));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %lu", ino);
		D_GOTO(err, rc = ENOENT);
	}

	inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	rc = dfs_get_symlink_value(inode->ie_obj, NULL, &size);
	if (rc)
		D_GOTO(err, rc = -rc);

	D_ALLOC(buf, size + 1);

	rc = dfs_get_symlink_value(inode->ie_obj, &buf[0], &size);
	if (rc)
		D_GOTO(err, rc = -rc);

	DFUSE_REPLY_READLINK(req, buf);
	D_FREE(buf);
	return;

err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	D_FREE(buf);
}
