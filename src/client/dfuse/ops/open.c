/**
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2025 Google LLC
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_inode_entry *ie;
	struct dfuse_obj_hdl     *oh;
	struct fuse_file_info     fi_out = {0};
	int                       rc;
	bool                      prefetch = false;
	bool                      preread  = false;
	int                       flags;

	if (ino == DFUSE_LOG_CTRL_INO) {
		fi_out.fh          = ino;
		fi_out.direct_io   = 1;
		fi_out.nonseekable = 1;
		fuse_reply_open(req, &fi_out);
		return;
	}

	ie = dfuse_inode_lookup_nf(dfuse_info, ino);

	DFUSE_IE_STAT_ADD(ie, DS_OPEN);

	D_ALLOC_PTR(oh);
	if (!oh)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(oh, ie, "open handle");

	dfuse_open_handle_init(dfuse_info, oh, ie);
	oh->doh_parent_dir = dfuse_inode_lookup(dfuse_info, ie->ie_parent);

	/* Upgrade fd permissions from O_WRONLY to O_RDWR if wb caching is
	 * enabled so the kernel can do read-modify-write
	 */
	if (ie->ie_dfs->dfc_data_timeout != 0 && dfuse_info->di_wb_cache &&
	    (fi->flags & O_ACCMODE) == O_WRONLY) {
		DFUSE_TRA_DEBUG(ie, "Upgrading fd to O_RDRW");
		fi->flags &= ~O_ACCMODE;
		fi->flags |= O_RDWR;
	}

	LOG_FLAGS(ie, fi->flags);

	flags = fi->flags;

	if (flags & O_APPEND)
		flags &= ~O_APPEND;

	/** duplicate the file handle for the fuse handle */
	rc = dfs_dup(ie->ie_dfs->dfs_ns, ie->ie_obj, flags, &oh->doh_obj);
	if (rc)
		D_GOTO(err, rc);

	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		oh->doh_writeable = true;

	if (ie->ie_dfs->dfc_data_timeout != 0) {
		if (fi->flags & O_DIRECT)
			fi_out.direct_io = 1;

		/* If the file is already open or (potentially) in cache then allow any existing
		 * kernel cache to be used.  If not then use pre-read.
		 * This should mean that pre-read is only used on the first read, and on files
		 * which pre-existed in the container.
		 */

		/* TODO: This probably wants reflowing to not reference ie_open_count */
		if (atomic_load_relaxed(&ie->ie_open_count) > 0 ||
		    ((ie->ie_dcache_last_update.tv_sec != 0) &&
		     dfuse_dcache_get_valid(ie, ie->ie_dfs->dfc_data_timeout))) {
			fi_out.keep_cache = 1;
		} else {
			prefetch = true;
		}
	} else if (ie->ie_dfs->dfc_data_otoc) {
		/* Open to close caching, this allows the use of shared mmap */
		fi_out.direct_io  = 0;
		fi_out.keep_cache = 0;

		if (fi->flags & O_DIRECT)
			fi_out.direct_io = 1;
	} else {
		fi_out.direct_io = 1;
	}

	if (ie->ie_dfs->dfc_direct_io_disable)
		fi_out.direct_io = 0;

	if (!fi_out.direct_io)
		oh->doh_caching = true;

	fi_out.fh = (uint64_t)oh;

	/* Pre-read this for files up to the max read size. */
	if (prefetch && oh->doh_parent_dir &&
	    atomic_load_relaxed(&oh->doh_parent_dir->ie_linear_read) && ie->ie_stat.st_size > 0 &&
	    ie->ie_stat.st_size <= DFUSE_MAX_PRE_READ) {
		preread = true;
	}

	rc = active_ie_init(ie, &preread);
	if (rc)
		goto err;

	/*
	 * dfs_dup() just locally duplicates the file handle. If we have
	 * O_TRUNC flag, we need to truncate the file manually.
	 */
	if (fi->flags & O_TRUNC) {
		rc = dfs_punch(ie->ie_dfs->dfs_ns, ie->ie_obj, 0, DFS_MAX_FSIZE);
		if (rc)
			D_GOTO(decref, rc);
		dfuse_dcache_evict(oh->doh_ie);
	}

	DFUSE_REPLY_OPEN(oh, req, &fi_out);

	/* No reference is held on oh here but if preread is true then a lock is held which prevents
	 * release from completing which also holds open the inode.
	 */
	if (preread)
		dfuse_pre_read(dfuse_info, ie);

	return;
decref:
	active_ie_decref(dfuse_info, ie);
err:
	dfuse_oh_free(dfuse_info, oh);
	DFUSE_REPLY_ERR_RAW(ie, req, rc);
}

/* Release a file handle, called after close() by an application.
 *
 * Can be invoked concurrently on the same inode.
 */
void
dfuse_cb_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_obj_hdl     *oh         = (struct dfuse_obj_hdl *)fi->fh;
	struct dfuse_inode_entry *ie         = NULL;
	int                       rc;
	uint32_t                  il_calls;

	if (ino == DFUSE_LOG_CTRL_INO) {
		fuse_reply_err(req, 0);
		return;
	}
	/* Perform the opposite of what the ioctl call does, always change the open handle count
	 * but the inode only tracks number of open handles with non-zero ioctl counts
	 */

	D_ASSERT(oh->doh_ie->ie_active);

	DFUSE_TRA_DEBUG(oh, "Closing %d", oh->doh_caching);

	DFUSE_IE_WFLUSH(oh->doh_ie);

	/* If the file was read from then set the data cache time for future use, however if the
	 * file was written to then evict the metadata cache.
	 * The problem here is that if the file was written to then the contents will be in the
	 * kernel cache however dfuse has no visibility over file size and before replying with
	 * data from the cache the kernel will call stat() to get an up-to-date file size, so
	 * after write dfuse may continue to tell the kernel incorrect file sizes.  To avoid this
	 * evict the metadata cache so the size is refreshed on next access.
	 */
	il_calls = atomic_load_relaxed(&oh->doh_il_calls);

	if (atomic_load_relaxed(&oh->doh_write_count) != 0) {
		if (oh->doh_caching) {
			if (il_calls == 0) {
				DFUSE_TRA_DEBUG(oh, "Evicting metadata cache, setting data cache");
				dfuse_mcache_evict(oh->doh_ie);
				dfuse_dcache_set_time(oh->doh_ie);
			} else {
				DFUSE_TRA_DEBUG(oh, "Evicting cache");
				dfuse_cache_evict(oh->doh_ie);
			}
		}
		atomic_fetch_sub_relaxed(&oh->doh_ie->ie_open_write_count, 1);
	} else {
		if (oh->doh_caching) {
			if (il_calls == 0) {
				DFUSE_TRA_DEBUG(oh, "Saving data cache");
				dfuse_dcache_set_time(oh->doh_ie);
			} else {
				DFUSE_TRA_DEBUG(oh, "Evicting cache");
				dfuse_cache_evict(oh->doh_ie);
			}
		}
	}
	DFUSE_TRA_DEBUG(oh, "il_calls %d, caching %d,", il_calls, oh->doh_caching);
	if (il_calls != 0) {
		atomic_fetch_sub_relaxed(&oh->doh_ie->ie_il_count, 1);
	}

	if (oh->doh_evict_on_close) {
		ie = oh->doh_ie;
		atomic_fetch_add_relaxed(&ie->ie_ref, 1);
	}

	if (active_oh_decref(dfuse_info, oh))
		oh->doh_linear_read = true;

	rc = dfs_release(oh->doh_obj);
	if (rc == 0) {
		DFUSE_REPLY_ZERO_OH(oh, req);
	} else {
		DFUSE_REPLY_ERR_RAW(dfuse_info, req, rc);
		oh->doh_ie = NULL;
	}
	if (oh->doh_parent_dir) {
		bool use_linear_read = false;
		bool set_linear_read = true;

		if (oh->doh_linear_read) {
			/* If the file was not read from then this could indicate a cached read
			 * so do not disable pre-read for the directory.
			 */
			if (!oh->doh_linear_read_eof)
				set_linear_read = false;
			use_linear_read = true;
		}

		if (set_linear_read) {
			DFUSE_TRA_DEBUG(oh->doh_parent_dir, "Setting linear_read to %d",
					use_linear_read);

			atomic_store_relaxed(&oh->doh_parent_dir->ie_linear_read, use_linear_read);
		}

		dfuse_inode_decref(dfuse_info, oh->doh_parent_dir);
	}
	if (ie) {
		rc = fuse_lowlevel_notify_inval_entry(dfuse_info->di_session, ie->ie_parent,
						      ie->ie_name, strnlen(ie->ie_name, NAME_MAX));

		if (rc != 0 && rc != -ENOENT)
			DHS_ERROR(ie, -rc, "inval_entry() error");
		dfuse_inode_decref(dfuse_info, ie);
	}
	dfuse_oh_free(dfuse_info, oh);
}
