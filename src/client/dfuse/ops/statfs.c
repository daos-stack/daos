/**
 * (C) Copyright 2020 Intel Corporation.
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
dfuse_cb_statfs(fuse_req_t req, struct dfuse_inode_entry *inode)
{
	struct statvfs stbuf = {};
	daos_pool_info_t info = {.pi_bits = DPI_SPACE};
	int rc;

	if (daos_handle_is_valid(inode->ie_dfs->dfs_dfp->dfp_poh)) {
		rc = daos_pool_query(inode->ie_dfs->dfs_dfp->dfp_poh, NULL,
				     &info, NULL, NULL);
		if (rc != -DER_SUCCESS)
			D_GOTO(err, rc = daos_der2errno(rc));

		stbuf.f_blocks = info.pi_space.ps_space.s_total[DAOS_MEDIA_SCM] \
			+ info.pi_space.ps_space.s_total[DAOS_MEDIA_NVME];
		stbuf.f_bfree = info.pi_space.ps_space.s_free[DAOS_MEDIA_SCM] \
			+ info.pi_space.ps_space.s_free[DAOS_MEDIA_NVME];

		DFUSE_TRA_INFO(inode, "blocks %#lx free %#lx",
			       stbuf.f_blocks, stbuf.f_bfree);
	} else {
		stbuf.f_blocks = -1;
		stbuf.f_bfree = -1;
	}

	stbuf.f_bsize = 1;
	stbuf.f_frsize = 1;

	stbuf.f_files = -1;
	stbuf.f_ffree = -1;

	stbuf.f_namemax = 255;

	stbuf.f_bavail = stbuf.f_bfree;
	stbuf.f_favail = stbuf.f_ffree;

	DFUSE_REPLY_STATFS(inode, req, &stbuf);
	return;
err:
	DFUSE_REPLY_ERR_RAW(inode, req, rc);
}

