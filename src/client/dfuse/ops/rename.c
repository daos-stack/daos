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
dfuse_cb_rename(fuse_req_t req, struct dfuse_inode_entry *parent,
		const char *name, struct dfuse_inode_entry *newparent,
		const char *newname, unsigned int flags)
{
	int rc;

	if (flags != 0)
		D_GOTO(out, rc = ENOTSUP);

	if (!newparent)
		newparent = parent;

	rc = dfs_move(parent->ie_dfs->dfs_ns, parent->ie_obj, (char *)name,
		      newparent->ie_obj, (char *)newname, NULL);
	if (rc)
		D_GOTO(out, rc);

	DFUSE_TRA_INFO(parent, "Renamed %s to %s in %p",
		       name, newname, newparent);

	DFUSE_REPLY_ZERO(parent, req);
	return;

out:
	DFUSE_REPLY_ERR_RAW(parent, req, rc);
}
