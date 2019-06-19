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
dfuse_cb_opendir(fuse_req_t req, struct dfuse_inode_entry *ino,
		 struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl		*oh = NULL;
	int				rc;

	D_ALLOC_PTR(oh);
	if (!oh)
		D_GOTO(err, rc = ENOMEM);

	/** duplicate the file handle for the fuse handle */
	rc = dfs_dup(ino->ie_dfs->dffs_dfs, ino->ie_obj, fi->flags,
		     &oh->doh_obj);
	if (rc)
		D_GOTO(err, rc = -rc);

	oh->doh_dfs = ino->ie_dfs->dffs_dfs;
	fi->fh = (uint64_t)oh;

	DFUSE_REPLY_OPEN(req, fi);
	return;
err:
	D_FREE(oh);
	DFUSE_FUSE_REPLY_ERR(req, rc);
}

void
dfuse_cb_releasedir(fuse_req_t req, struct dfuse_inode_entry *ino,
		    struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl		*oh = (struct dfuse_obj_hdl *)fi->fh;
	int				rc;

	rc = dfs_release(oh->doh_obj);
	if (rc == 0)
		D_FREE(oh);
	DFUSE_FUSE_REPLY_ERR(req, -rc);
}
