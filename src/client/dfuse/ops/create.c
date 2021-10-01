/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_create(fuse_req_t req, struct dfuse_inode_entry *parent,
		const char *name, mode_t mode, struct fuse_file_info *fi)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*ie = NULL;
	struct dfuse_obj_hdl		*oh = NULL;
	struct fuse_file_info		fi_out = {0};
	struct dfuse_cont		*dfs = parent->ie_dfs;
	int rc;

	DFUSE_TRA_INFO(parent, "Parent:%#lx '%s'", parent->ie_stat.st_ino,
		       name);

	/* O_LARGEFILE should always be set on 64 bit systems, and in fact is
	 * defined to 0 so IOF defines LARGEFILE to the value that O_LARGEFILE
	 * would otherwise be using and check that is set.
	 */
	if (!(fi->flags & LARGEFILE)) {
		DFUSE_TRA_INFO(parent, "O_LARGEFILE required 0%o",
			       fi->flags);
		D_GOTO(err, rc = ENOTSUP);
	}

	/* Check for flags that do not make sense in this context. */
	if (fi->flags & DFUSE_UNSUPPORTED_CREATE_FLAGS) {
		DFUSE_TRA_INFO(parent, "unsupported flag requested 0%o",
			       fi->flags);
		D_GOTO(err, rc = ENOTSUP);
	}

	/* Upgrade fd permissions from O_WRONLY to O_RDWR if wb caching is
	 * enabled so the kernel can do read-modify-write
	 */
	if (parent->ie_dfs->dfc_data_caching &&
		fs_handle->dpi_info->di_wb_cache &&
		(fi->flags & O_ACCMODE) == O_WRONLY) {
		DFUSE_TRA_INFO(parent, "Upgrading fd to O_RDRW");
		fi->flags &= ~O_ACCMODE;
		fi->flags |= O_RDWR;
	}

	/* Check that only the flag for a regular file is specified */
	if (!S_ISREG(mode)) {
		DFUSE_TRA_INFO(parent,
			       "unsupported mode requested 0%o",
			       mode);
		D_GOTO(err, rc = ENOTSUP);
	}

	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(err, rc = ENOMEM);
	D_ALLOC_PTR(oh);
	if (!oh)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(ie, parent, "inode");
	DFUSE_TRA_UP(oh, ie, "open handle");
	ie->ie_dfs = dfs;

	DFUSE_TRA_DEBUG(ie, "file '%s' flags 0%o mode 0%o", name, fi->flags,
			mode);

	rc = dfs_open_stat(dfs->dfs_ns, parent->ie_obj, name, mode,
			   fi->flags, 0, 0, NULL, &oh->doh_obj, &ie->ie_stat);
	if (rc)
		D_GOTO(err, rc);

	/** duplicate the file handle for the fuse handle */
	rc = dfs_dup(dfs->dfs_ns, oh->doh_obj, O_RDWR,
		     &ie->ie_obj);
	if (rc)
		D_GOTO(release, rc);

	oh->doh_dfs = dfs->dfs_ns;
	oh->doh_ie = ie;

	if (dfs->dfc_data_caching) {
		if (fi->flags & O_DIRECT)
			fi_out.direct_io = 1;
	} else {
		fi_out.direct_io = 1;
	}

	if (dfs->dfc_direct_io_disable)
		fi_out.direct_io = 0;

	if (!fi_out.direct_io)
		oh->doh_caching = true;

	fi_out.fh = (uint64_t)oh;

	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_parent = parent->ie_stat.st_ino;
	ie->ie_truncated = false;
	atomic_store_relaxed(&ie->ie_ref, 1);

	LOG_FLAGS(ie, fi->flags);
	LOG_MODES(ie, mode);

	dfs_obj2id(ie->ie_obj, &ie->ie_oid);

	dfuse_compute_inode(dfs, &ie->ie_oid,
			    &ie->ie_stat.st_ino);

	/* Return the new inode data, and keep the parent ref */
	dfuse_reply_entry(fs_handle, ie, &fi_out, true, req);

	return;
release:
	dfs_release(oh->doh_obj);
err:
	DFUSE_REPLY_ERR_RAW(parent, req, rc);
	D_FREE(oh);
	D_FREE(ie);
}
