/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_symlink(fuse_req_t req, const char *link, struct dfuse_inode_entry *parent,
		 const char *name)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	const struct fuse_ctx    *ctx        = fuse_req_ctx(req);
	struct dfuse_inode_entry *ie;
	int                       rc;

	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(ie, parent, "inode");

	dfuse_ie_init(dfuse_info, ie);

	ie->ie_stat.st_uid = ctx->uid;
	ie->ie_stat.st_gid = ctx->gid;

	rc = dfs_open_stat(parent->ie_dfs->dfs_ns, parent->ie_obj, name, S_IFLNK,
			   O_CREAT | O_RDWR | O_EXCL, 0, 0, link, &ie->ie_obj, &ie->ie_stat);
	if (rc != 0)
		D_GOTO(err, rc);

	DFUSE_TRA_DEBUG(ie, "obj is %p", ie->ie_obj);

	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_parent = parent->ie_stat.st_ino;
	ie->ie_dfs    = parent->ie_dfs;

	dfs_obj2id(ie->ie_obj, &ie->ie_oid);

	dfuse_compute_inode(ie->ie_dfs, &ie->ie_oid, &ie->ie_stat.st_ino);

	dfuse_reply_entry(dfuse_info, ie, NULL, true, req);

	return;
err:
	DFUSE_REPLY_ERR_RAW(ie, req, rc);
	D_FREE(ie);
}
