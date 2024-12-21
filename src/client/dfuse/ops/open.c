/**
 * (C) Copyright 2016-2024 Intel Corporation.
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
	struct dfuse_event       *ev;
	bool                      preread = false;
	int                       rc;
	int                       flags;

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

	rc = active_ie_init(ie);
	if (rc)
		goto err;

	if (ie->ie_dfs->dfc_data_timeout != 0) {
		if (fi->flags & O_DIRECT)
			fi_out.direct_io = 1;

		/* If the file is already open or (potentially) in cache then allow any existing
		 * kernel cache to be used.  If not then use pre-read.
		 * This should mean that pre-read is only used on the first read, and on files
		 * which pre-existed in the container.
		 */

		/* TODO: This probably wants reflowing to not reference ie_open_count */
		if (atomic_load_relaxed(&ie->ie_open_count) > 1 ||
		    ((ie->ie_dcache_last_update.tv_sec != 0) &&
		     dfuse_dcache_get_valid(ie, ie->ie_dfs->dfc_data_timeout))) {
			fi_out.keep_cache = 1;
		} else {
			D_SPIN_LOCK(&ie->ie_active->lock);
			/**
			 * size > 4M no pre-read
			 * 1M <= size <= 4M depend on other files under the directory.
			 * size <= 1M pre-read in any case.
			 */
			if ((oh->doh_parent_dir &&
			     atomic_load_relaxed(&oh->doh_parent_dir->ie_linear_read) &&
			     ie->ie_stat.st_size > 0 &&
			     ie->ie_stat.st_size <= DFUSE_MAX_PRE_READ) ||
			    (ie->ie_stat.st_size > 0 &&
			     ie->ie_stat.st_size <= DFUSE_MAX_PRE_READ_ONCE)) {
				preread = true;
				/* Add the read extent to the list to make sure the following read
				 * will check the readahead list first.
				 */
				rc = dfuse_pre_read_init(dfuse_info, ie, &ev);
				if (rc != 0) {
					D_SPIN_UNLOCK(&ie->ie_active->lock);
					D_GOTO(decref, rc);
				}
				oh->doh_readahead_inflight = 1;
			}
			D_SPIN_UNLOCK(&ie->ie_active->lock);
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
	/*
	 * dfs_dup() just locally duplicates the file handle. If we have
	 * O_TRUNC flag, we need to truncate the file manually.
	 */
	if (fi->flags & O_TRUNC) {
		rc = dfs_punch(ie->ie_dfs->dfs_ns, ie->ie_obj, 0, DFS_MAX_FSIZE);
		if (rc)
			D_GOTO(ie_decref, rc);
		dfuse_dcache_evict(oh->doh_ie);
	}

	DFUSE_REPLY_OPEN(oh, req, &fi_out);

	/* No reference is held on oh here but if preread is true then a lock is held which prevents
	 * release from completing which also holds open the inode.
	 */
	if (preread)
		dfuse_pre_read(dfuse_info, oh, ev);

	return;
ie_decref:
	dfuse_pre_read_abort(dfuse_info, oh, ev, rc);
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

	/* Perform the opposite of what the ioctl call does, always change the open handle count
	 * but the inode only tracks number of open handles with non-zero ioctl counts
	 */

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

	/* Wait inflight readahead RPC finished before release */
	if (oh->doh_ie->ie_active != NULL) {
wait_readahead:
		D_SPIN_LOCK(&oh->doh_ie->ie_active->lock);
		if (oh->doh_readahead_inflight) {
			D_SPIN_UNLOCK(&oh->doh_ie->ie_active->lock);
			goto wait_readahead;
		}
		D_SPIN_UNLOCK(&oh->doh_ie->ie_active->lock);
	}

	if (oh->doh_evict_on_close) {
		ie = oh->doh_ie;
		atomic_fetch_add_relaxed(&ie->ie_ref, 1);
	}

	active_oh_decref(dfuse_info, oh);

	rc = dfs_release(oh->doh_obj);
	if (rc == 0) {
		DFUSE_REPLY_ZERO_OH(oh, req);
	} else {
		DFUSE_REPLY_ERR_RAW(dfuse_info, req, rc);
		oh->doh_ie = NULL;
	}
	if (oh->doh_parent_dir) {
		if (oh->doh_set_linear_read) {
			DFUSE_TRA_DEBUG(oh->doh_parent_dir, "Setting linear_read to %d",
					oh->doh_linear_read);

			atomic_store_relaxed(&oh->doh_parent_dir->ie_linear_read,
					     oh->doh_linear_read);
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
