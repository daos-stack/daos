/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
dfuse_cb_getattr(fuse_req_t req, struct dfuse_inode_entry *ie)
{
	struct stat	stat = {};
	int		rc;

	rc = dfs_ostat(ie->ie_dfs->dfs_ns, ie->ie_obj, &stat);
	if (rc)
		D_GOTO(err, rc);

	/* Copy the inode number from the inode struct, to avoid having to
	 * recompute it each time.
	 */
	stat.st_ino = ie->ie_stat.st_ino;
	DFUSE_REPLY_ATTR(ie, req, &stat);

	return;
err:
	DFUSE_REPLY_ERR_RAW(ie, req, rc);
}
