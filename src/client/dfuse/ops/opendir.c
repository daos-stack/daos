/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_opendir(fuse_req_t req, struct dfuse_inode_entry *ie, struct fuse_file_info *fi)
{
	struct dfuse_info    *dfuse_info = fuse_req_userdata(req);
	struct dfuse_obj_hdl *oh;
	struct fuse_file_info fi_out = {0};
	int                   rc;

	D_ALLOC_PTR(oh);
	if (!oh)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(oh, ie, "open handle");

	dfuse_open_handle_init(dfuse_info, oh, ie);

	fi_out.fh = (uint64_t)oh;

#if HAVE_CACHE_READDIR
	/* If caching is enabled then always set the bit to enable caching as it might get
	 * populated, however only set the bit to use the cache based on last use.
	 */
	if (ie->ie_dfs->dfc_dentry_timeout > 0) {
		fi_out.cache_readdir = 1;

		if (dfuse_dcache_get_valid(ie, ie->ie_dfs->dfc_dentry_timeout))
			fi_out.keep_cache = 1;
	}
#endif

	atomic_fetch_add_relaxed(&ie->ie_open_count, 1);

	DFUSE_REPLY_OPEN_DIR(oh, req, &fi_out);
	return;
err:
	D_FREE(oh);
	DFUSE_REPLY_ERR_RAW(ie, req, rc);
}

void
dfuse_cb_releasedir(fuse_req_t req, struct dfuse_inode_entry *ino, struct fuse_file_info *fi)
{
	struct dfuse_info    *dfuse_info = fuse_req_userdata(req);
	struct dfuse_obj_hdl *oh         = (struct dfuse_obj_hdl *)fi->fh;

	/* Perform the opposite of what the ioctl call does, always change the open handle count
	 * but the inode only tracks number of open handles with non-zero ioctl counts
	 */

	if (atomic_load_relaxed(&oh->doh_il_calls) != 0)
		atomic_fetch_sub_relaxed(&oh->doh_ie->ie_il_count, 1);
	atomic_fetch_sub_relaxed(&oh->doh_ie->ie_open_count, 1);

	DFUSE_TRA_DEBUG(oh, "Kernel cache flags invalid %d started %d finished %d",
			oh->doh_kreaddir_invalid, oh->doh_kreaddir_started,
			oh->doh_kreaddir_finished);

	if ((!oh->doh_kreaddir_invalid) && oh->doh_kreaddir_finished) {
		DFUSE_TRA_DEBUG(oh, "Directory handle may have populated cache, saving");
		dfuse_dcache_set_time(oh->doh_ie);
	}

	DFUSE_REPLY_ZERO(oh, req);
	dfuse_dre_drop(dfuse_info, oh);
	if (oh->doh_evict_on_close) {
		int rc;

		rc = fuse_lowlevel_notify_inval_entry(dfuse_info->di_session, oh->doh_ie->ie_parent,
						      oh->doh_ie->ie_name,
						      strnlen(oh->doh_ie->ie_name, NAME_MAX));

		if (rc != 0)
			DFUSE_TRA_ERROR(oh->doh_ie, "inval_entry() returned: %d (%s)", rc,
					strerror(-rc));
	}
	dfuse_oh_free(dfuse_info, oh);
};
