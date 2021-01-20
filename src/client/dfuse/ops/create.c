/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
	struct fuse_file_info	        fi_out = {0};
	int rc;

	DFUSE_TRA_INFO(parent, "Parent:%lu '%s'", parent->ie_stat.st_ino,
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

	/* Check for flags that do not make sense in this context.
	 */
	if (fi->flags & DFUSE_UNSUPPORTED_CREATE_FLAGS) {
		DFUSE_TRA_INFO(parent, "unsupported flag requested 0%o",
			       fi->flags);
		D_GOTO(err, rc = ENOTSUP);
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

	DFUSE_TRA_DEBUG(ie, "file '%s' flags 0%o mode 0%o", name, fi->flags,
			mode);

	rc = dfs_open_stat(parent->ie_dfs->dfs_ns, parent->ie_obj, name, mode,
			   fi->flags, 0, 0, NULL, &oh->doh_obj, &ie->ie_stat);
	if (rc)
		D_GOTO(err, rc);

	/** duplicate the file handle for the fuse handle */
	rc = dfs_dup(parent->ie_dfs->dfs_ns, oh->doh_obj, O_RDWR,
		     &ie->ie_obj);
	if (rc)
		D_GOTO(release, rc);

	oh->doh_dfs = parent->ie_dfs->dfs_ns;
	oh->doh_ie = ie;

	if (fs_handle->dpi_info->di_direct_io) {
		if (parent->ie_dfs->dfs_attr_timeout == 0) {
			fi_out.direct_io = 1;
		} else {
			if (fi->flags & O_DIRECT)
				fi_out.direct_io = 1;
		}
	}

	fi_out.fh = (uint64_t)oh;

	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_name[NAME_MAX] = '\0';
	ie->ie_parent = parent->ie_stat.st_ino;
	ie->ie_dfs = parent->ie_dfs;
	ie->ie_truncated = false;
	atomic_store_relaxed(&ie->ie_ref, 1);

	LOG_FLAGS(ie, fi->flags);
	LOG_MODES(ie, mode);

	/* Return the new inode data, and keep the parent ref */
	dfuse_reply_entry(fs_handle, ie, &fi_out, true, req);

	return;
release:
	dfs_release(ie->ie_obj);
err:
	DFUSE_REPLY_ERR_RAW(parent, req, rc);
	D_FREE(oh);
	D_FREE(ie);
}
