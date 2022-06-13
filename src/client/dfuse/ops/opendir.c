/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_opendir(fuse_req_t req, struct dfuse_inode_entry *ie, struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl *oh     = NULL;
	struct fuse_file_info fi_out = {0};
	int                   rc;

	D_ALLOC_PTR(oh);
	if (!oh)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(oh, ie, "open handle");

	/** duplicate the file handle for the fuse handle */
	rc = dfs_dup(ie->ie_dfs->dfs_ns, ie->ie_obj, fi->flags, &oh->doh_obj);
	if (rc)
		D_GOTO(err, rc);

	oh->doh_dfs = ie->ie_dfs->dfs_ns;
	oh->doh_ie  = ie;

	fi_out.fh = (uint64_t)oh;

#if HAVE_CACHE_READDIR
	/* TODO: Keep track of age of cache and selectively enable this bit.
	 * The kernel keeps the readdir data in the page cache which has no time based
	 * expiry mechanism, it would be better to keep track of when the last successful
	 * opendir/readir/closedir calls (excluding where seekdir was used) happened
	 * and set the keep_cache bit here based on how old the data in cache is.
	 * fuse can peek into the page cache so we could identify if there is data in cache
	 * or not but it's not possible to know which calls populated it so getting
	 * absolute timings is likely impossible but if dfuse assumes worst-case in terms
	 * of age this will mean that the cache timeouts are slightly shorter than
	 * configured which is unlikely to be a problem.
	 */
	if (ie->ie_dfs->dfc_dentry_timeout > 0) {
		fi_out.cache_readdir = 1;
		fi_out.keep_cache    = 1;
	}
#endif

	atomic_fetch_add_relaxed(&ie->ie_open_count, 1);

	DFUSE_REPLY_OPEN(oh, req, &fi_out);
	return;
err:
	D_FREE(oh);
	DFUSE_REPLY_ERR_RAW(ie, req, rc);
}

void
dfuse_cb_releasedir(fuse_req_t req, struct dfuse_inode_entry *ino, struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl *oh = (struct dfuse_obj_hdl *)fi->fh;
	int                   rc;

	/* Perform the opposite of what the ioctl call does, always change the open handle count
	 * but the inode only tracks number of open handles with non-zero ioctl counts
	 */

	if (atomic_load_relaxed(&oh->doh_il_calls) != 0)
		atomic_fetch_sub_relaxed(&oh->doh_ie->ie_il_count, 1);
	atomic_fetch_sub_relaxed(&oh->doh_ie->ie_open_count, 1);

	rc = dfs_release(oh->doh_obj);
	if (rc == 0)
		DFUSE_REPLY_ZERO(oh, req);
	else
		DFUSE_REPLY_ERR_RAW(oh, req, rc);
	D_FREE(oh->doh_dre);
	D_FREE(oh);
};
