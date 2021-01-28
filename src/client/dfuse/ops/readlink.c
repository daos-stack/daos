/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_readlink(fuse_req_t req, fuse_ino_t ino)
{
	struct dfuse_projection_info	*fsh = fuse_req_userdata(req);
	struct dfuse_inode_entry	*inode;
	d_list_t			*rlink;
	char				*buf = NULL;
	size_t				size = 0;
	int				rc;

	rlink = d_hash_rec_find(&fsh->dpi_iet, &ino, sizeof(ino));
	if (!rlink) {
		DFUSE_TRA_ERROR(fsh, "Failed to find inode %#lx", ino);
		D_GOTO(err, rc = EIO);
	}

	inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	rc = dfs_get_symlink_value(inode->ie_obj, NULL, &size);
	if (rc)
		D_GOTO(release, rc);

	D_ALLOC(buf, size);
	if (!buf)
		D_GOTO(release, rc = ENOMEM);

	rc = dfs_get_symlink_value(inode->ie_obj, buf, &size);
	if (rc)
		D_GOTO(release, rc);

	DFUSE_REPLY_READLINK(inode, req, buf);

	d_hash_rec_decref(&fsh->dpi_iet, rlink);

	D_FREE(buf);
	return;
release:
	d_hash_rec_decref(&fsh->dpi_iet, rlink);
err:
	DFUSE_REPLY_ERR_RAW(fsh, req, rc);
	D_FREE(buf);
}
