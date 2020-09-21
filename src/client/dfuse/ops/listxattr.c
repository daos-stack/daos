/**
 * (C) Copyright 2019-2020 Intel Corporation.
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
dfuse_cb_listxattr(fuse_req_t req, struct dfuse_inode_entry *inode,
		   size_t size)
{
	size_t out_size = 0;
	char *value = NULL;
	int rc;

	rc = dfs_listxattr(inode->ie_dfs->dfs_ns, inode->ie_obj, NULL,
			   &out_size);
	if (rc != 0)
		D_GOTO(err, rc);

	if (size == 0) {
		fuse_reply_xattr(req, out_size);
		return;
	}

	if (size < out_size)
		D_GOTO(err, rc = ERANGE);

	D_ALLOC(value, out_size);
	if (!value)
		D_GOTO(err, rc = ENOMEM);

	rc = dfs_listxattr(inode->ie_dfs->dfs_ns, inode->ie_obj, value,
			   &out_size);
	if (rc != 0)
		D_GOTO(err, rc);

	fuse_reply_buf(req, value, out_size);
	D_FREE(value);
	return;
err:
	if (value != NULL)
		D_FREE(value);
	DFUSE_REPLY_ERR_RAW(inode, req, rc);
}
