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
dfuse_cb_unlink(fuse_req_t req, struct dfuse_inode_entry *parent,
		const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	int				rc;

	rc = dfs_remove(parent->ie_dfs->dffs_dfs, parent->ie_obj, name, false);

	if (rc == 0) {
		DFUSE_FUSE_REPLY_ZERO(req);
	} else {
		DFUSE_REPLY_ERR_RAW(fs_handle, req, -rc);
	}
}
