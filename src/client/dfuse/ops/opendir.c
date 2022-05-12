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
	if (ie->ie_dfs->dfc_dentry_timeout > 0) {
		struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);

		DFUSE_TRA_INFO(ie, "Caching readdir");
		fi_out.cache_readdir = 1;
		fi_out.keep_cache = 1;

		/* If there are open directory handles already then save this opendir request and
		 * reply to it later after the closedir has completed.
		 */
		D_MUTEX_LOCK(&fs_handle->dpi_op_lock);

		if (oh->doh_ie->ie_odir) {
			DFUSE_TRA_WARNING(oh, "Delaying opendir reply");
			oh->doh_od_req = req;
			d_list_add_tail(&oh->doh_dir_list, &ie->ie_odir_list);
			D_MUTEX_UNLOCK(&fs_handle->dpi_op_lock);
			return;
		}
		oh->doh_ie->ie_odir = true;
		D_MUTEX_UNLOCK(&fs_handle->dpi_op_lock);
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
#if HAVE_CACHE_READDIR
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
#endif
	struct dfuse_obj_hdl		*oh = (struct dfuse_obj_hdl *)fi->fh;
	int				rc;
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

#if HAVE_CACHE_READDIR
	D_MUTEX_LOCK(&fs_handle->dpi_op_lock);
	if (oh->doh_ie->ie_odir) {
		struct dfuse_obj_hdl	*qoh, *next;
		struct fuse_file_info	fi_out = {0};

		fi_out.cache_readdir = 1;
		fi_out.keep_cache = 1;
		d_list_for_each_entry_safe(qoh, next, &oh->doh_ie->ie_odir_list, doh_dir_list) {
			DFUSE_TRA_WARNING(oh, "Late opendir reply");
			fi_out.fh = (uint64_t)qoh;
			/* Once the reply is sent this thread might not schedule again until after
			 * the closedir of the other directory, so do the list_del() before the
			 * reply.
			 */
			d_list_del_init(&qoh->doh_dir_list);
			DFUSE_REPLY_OPEN(qoh, qoh->doh_od_req, &fi_out);
		}
		oh->doh_ie->ie_odir = false;
	}
	D_MUTEX_UNLOCK(&fs_handle->dpi_op_lock);
#endif

	D_FREE(oh);
};
