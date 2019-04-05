/**
 * (C) Copyright 2016-2019 Intel Corporation.
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

#include <sys/ioctl.h>

#include "dfuse_common.h"

#include <gurt/common.h>
#include "dfuse.h"
#include "dfuse_ioctl.h"

static void
handle_gah_ioctl(int cmd, struct dfuse_file_handle *handle,
		 struct dfuse_gah_info *gah_info)
{
	struct dfuse_projection_info *fs_handle = handle->open_req.fsh;

	/* DFUSE_IOCTL_GAH has size of gah embedded.  FUSE should have
	 * allocated that many bytes in data
	 */
	DFUSE_TRA_INFO(handle, "Requested " GAH_PRINT_STR " fs_id=%d,"
		       " cli_fs_id=%d",
		       GAH_PRINT_VAL(handle->common.gah),
		       fs_handle->fs_id,
		       fs_handle->proj.cli_fs_id);
	gah_info->version = DFUSE_IOCTL_VERSION;
	gah_info->cnss_id = getpid();
	gah_info->cli_fs_id = fs_handle->proj.cli_fs_id;
}

void
dfuse_cb_ioctl(fuse_req_t req, fuse_ino_t ino, int cmd, void *arg,
	       struct fuse_file_info *fi, unsigned int flags,
	       const void *in_buf, size_t in_bufsz, size_t out_bufsz)
{
	struct dfuse_file_handle *handle = (void *)fi->fh;
	struct dfuse_gah_info gah_info = {0};
	int ret = EIO;

	DFUSE_TRA_INFO(handle, "ioctl cmd=%#x " GAH_PRINT_STR, cmd,
		       GAH_PRINT_VAL(handle->common.gah));

	if (cmd == TCGETS) {
		DFUSE_TRA_DEBUG(handle, "Ignoring TCGETS ioctl");
		D_GOTO(out_err, ret = ENOTTY);
	}

	if (cmd != DFUSE_IOCTL_GAH) {
		DFUSE_TRA_INFO(handle, "Real ioctl support is not implemented");
		D_GOTO(out_err, ret = ENOTSUP);
	}

	handle_gah_ioctl(cmd, handle, &gah_info);

	DFUSE_REPLY_IOCTL(handle, req, gah_info);
	return;

out_err:
	DFUSE_REPLY_ERR_RAW(handle, req, ret);
}
