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
	uint32_t                  old_ref;

	ie = dfuse_inode_lookup_nf(dfuse_info, ino);

	/* dfuse_inode_lookup_nf does not hold a ref so decref by nlookup -1 using atomics and
	 * then a single decref at the end which may result in the inode being freed.
	 */

	nlookup--;

	old_ref = atomic_fetch_sub_relaxed(&ie->ie_ref, nlookup);

	D_ASSERT(old_ref > nlookup);

	DFUSE_TRA_DEBUG(ie, "inode %#lx count %u -> %lu", ino, old_ref, old_ref - nlookup - 1);

	d_hash_rec_decref(&dfuse_info->dpi_iet, &ie->ie_htl);
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
