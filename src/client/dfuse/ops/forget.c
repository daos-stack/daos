/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

static void
dfuse_forget_one(struct dfuse_projection_info *fs_handle, fuse_ino_t ino, uintptr_t nlookup)
{
	d_list_t		 *rlink;
	int                       rc;
	struct dfuse_inode_entry *ie;
	uint                      open_ref;
	uint                      il_ref;

	/* One additional reference is needed because the rec_find() itself acquires one */
	nlookup++;

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &ino, sizeof(ino));
	if (!rlink) {
		DFUSE_TRA_WARNING(fs_handle, "Unable to find ref for %#lx %lu", ino, nlookup);
		return;
	}

	ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	il_ref = atomic_load_relaxed(&ie->ie_il_count);
	if (il_ref != 0) {
		DFUSE_TRA_ERROR(ie, "Forget with non-zero ioctl count %d", il_ref);
	}

	open_ref = atomic_load_relaxed(&ie->ie_open_count);
	if (open_ref != 0) {
		DFUSE_TRA_ERROR(ie, "Forget with non-zero open count %d", open_ref);
	}

	DFUSE_TRA_DEBUG(ie, "inode %#lx count %lu", ino, nlookup);

	rc = d_hash_rec_ndecref(&fs_handle->dpi_iet, nlookup, rlink);
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_ERROR(fs_handle, "Invalid refcount %lu on %p", nlookup, ie);
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
dfuse_cb_forget_multi(fuse_req_t req, size_t count, struct fuse_forget_data *forgets)
{
	struct dfuse_projection_info *fs_handle = fuse_req_userdata(req);
	int                           i;

	fuse_reply_none(req);

	DFUSE_TRA_DEBUG(fs_handle, "Forgetting %zi", count);

	for (i = 0; i < count; i++)
		dfuse_forget_one(fs_handle, forgets[i].ino, forgets[i].nlookup);
}
