/**
 * (C) Copyright 2020-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_mknod(fuse_req_t req, struct dfuse_inode_entry *parent, const char *name, mode_t mode)
{
	struct dfuse_info            *dfuse_info = fuse_req_userdata(req);
	const struct fuse_ctx        *ctx       = fuse_req_ctx(req);
	struct dfuse_inode_entry     *ie;
	int                           rc;

	DFUSE_TRA_DEBUG(parent, "Parent:%lu " DF_DE, parent->ie_stat.st_ino, DP_DE(name));

	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(ie, parent, "inode");

	DFUSE_TRA_DEBUG(ie, "file " DF_DE " mode 0%o", DP_DE(name), mode);

	rc = _dfuse_mode_update(req, parent, &mode);
	if (rc != 0)
		D_GOTO(err, rc);

	dfuse_ie_init(dfuse_info, ie);

	ie->ie_stat.st_uid = ctx->uid;
	ie->ie_stat.st_gid = ctx->gid;

	rc = dfs_open_stat(parent->ie_dfs->dfs_ns, parent->ie_obj, name, mode,
			   O_CREAT | O_EXCL | O_RDWR, 0, 0, NULL, &ie->ie_obj, &ie->ie_stat);
	if (rc)
		D_GOTO(err, rc);

	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_parent    = parent->ie_stat.st_ino;
	ie->ie_dfs       = parent->ie_dfs;
	ie->ie_truncated = false;

	LOG_MODES(ie, mode);

	dfs_obj2id(ie->ie_obj, &ie->ie_oid);

	dfuse_compute_inode(ie->ie_dfs, &ie->ie_oid, &ie->ie_stat.st_ino);

	/* Return the new inode data, and keep the parent ref */
	dfuse_reply_entry(dfuse_info, ie, NULL, true, req);

	return;
err:
	DFUSE_REPLY_ERR_RAW(parent, req, rc);
	dfuse_ie_free(dfuse_info, ie);
}
