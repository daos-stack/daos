/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

static void
dfuse_forget_one(struct dfuse_info *dfuse_info, fuse_ino_t ino, uintptr_t nlookup)
{
	struct dfuse_inode_entry *ie;
	int                       rc;

	/* One additional reference is needed because the rec_find() itself acquires one */
	nlookup++;

	ie = dfuse_inode_lookup(dfuse_info, ino);
	if (!ie) {
		DFUSE_TRA_WARNING(dfuse_info, "Unable to find ref for %#lx %lu", ino, nlookup);
		return;
	}

	DFUSE_TRA_DEBUG(ie, "inode %#lx count %lu", ino, nlookup);

	rc = d_hash_rec_ndecref(&dfuse_info->dpi_iet, nlookup, &ie->ie_htl);
	if (rc != -DER_SUCCESS)
		DFUSE_TRA_ERROR(dfuse_info, "Invalid refcount %lu on %p", nlookup, ie);
}

void
dfuse_cb_forget(fuse_req_t req, fuse_ino_t ino, uintptr_t nlookup)
{
	struct dfuse_info *dfuse_info = fuse_req_userdata(req);

	fuse_reply_none(req);

	dfuse_forget_one(dfuse_info, ino, nlookup);
}

void
dfuse_cb_forget_multi(fuse_req_t req, size_t count, struct fuse_forget_data *forgets)
{
	struct dfuse_info *dfuse_info = fuse_req_userdata(req);
	int                i;

	fuse_reply_none(req);

	DFUSE_TRA_DEBUG(dfuse_info, "Forgetting %zi", count);

	D_RWLOCK_RDLOCK(&dfuse_info->di_forget_lock);

	for (i = 0; i < count; i++)
		dfuse_forget_one(dfuse_info, forgets[i].ino, forgets[i].nlookup);

	D_RWLOCK_UNLOCK(&dfuse_info->di_forget_lock);
}
