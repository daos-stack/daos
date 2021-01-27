/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

