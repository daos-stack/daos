/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

static void
dfuse_forget_one(struct dfuse_projection_info *fs_handle,
		 fuse_ino_t ino, uintptr_t nlookup)
{
	d_list_t *rlink;
	int rc;

	/* One additional reference is needed because the rec_find() itself
	 * acquires one
	 */
	nlookup++;

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &ino, sizeof(ino));
	if (!rlink) {
		DFUSE_TRA_WARNING(fs_handle, "Unable to find ref for %#lx %lu",
				  ino, nlookup);
		return;
	}

	DFUSE_TRA_DEBUG(container_of(rlink, struct dfuse_inode_entry, ie_htl),
			"ino %lu count %lu",
			ino, nlookup);

	rc = d_hash_rec_ndecref(&fs_handle->dpi_iet, nlookup, rlink);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(fs_handle, "Invalid refcount %lu on %p",
				nlookup,
				container_of(rlink, struct dfuse_inode_entry, ie_htl));
	}
}

void
dfuse_cb_forget(fuse_req_t req, fuse_ino_t ino, uintptr_t nlookup)
{
	struct dfuse_projection_info *fs_handle = fuse_req_userdata(req);

	fuse_reply_none(req);

	dfuse_forget_one(fs_handle, ino, nlookup);
}

void
dfuse_cb_forget_multi(fuse_req_t req, size_t count,
		      struct fuse_forget_data *forgets)
{
	struct dfuse_projection_info *fs_handle = fuse_req_userdata(req);
	int i;

	fuse_reply_none(req);

	DFUSE_TRA_INFO(fs_handle, "Forgetting %zi", count);

	for (i = 0; i < count; i++)
		dfuse_forget_one(fs_handle, forgets[i].ino, forgets[i].nlookup);
}
