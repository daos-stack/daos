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

#include "dfuse_common.h"
#include "dfuse.h"

void
dfuse_cb_read(fuse_req_t req, fuse_ino_t ino, size_t len, off_t position,
	      struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl		*oh = (struct dfuse_obj_hdl *)fi->fh;
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	const struct fuse_ctx		*fc = fuse_req_ctx(req);
	d_iov_t				iov = {};
	d_sg_list_t			sgl = {};
	struct fuse_bufvec		fb = {};
	daos_size_t			size;
	void				*buff;
	int				rc;

	DFUSE_TRA_INFO(oh, "%#zx-%#zx pid=%d",
		       position, position + len - 1, fc->pid);

	D_ALLOC(buff, len);
	if (!buff) {
		DFUSE_REPLY_ERR_RAW(NULL, req, ENOMEM);
		return;
	}

	sgl.sg_nr = 1;
	d_iov_set(&iov, (void *)buff, len);
	sgl.sg_iovs = &iov;

	rc = dfs_read(oh->doh_dfs, oh->doh_obj, &sgl, position, &size, NULL);
	if (rc == 0)
		DFUSE_REPLY_BUF(oh, req, buff, size);
	else
		DFUSE_REPLY_ERR_RAW(oh, req, rc);
	D_FREE(buff);

	if (fc->pid != 0)
		return;

	position += len;
	len = 1024 * 128;

	/* Note that from this line onwards there is a race condition
	 * against close, now that this callback has replied the
	 * kernel is at liberty to close the file, and oh might be
	 * released by another thread as this readahead code is
	 * being executed.
	 */
	if (len + position - 1 > oh->doh_ie->ie_stat.st_size)
		return;

	DFUSE_TRA_INFO(oh, "Will try readahead");

	D_ALLOC(buff, len);
	if (!buff)
		return;

	fb.count = 1;
	fb.buf[0].mem = buff;
	fb.buf[0].size = len;

	sgl.sg_nr = 1;
	d_iov_set(&iov, (void *)buff, len);
	sgl.sg_iovs = &iov;

	rc = dfs_read(oh->doh_dfs, oh->doh_obj, &sgl, position, &size, NULL);
	if (rc != 0 || size != len)
		return;

	rc = fuse_lowlevel_notify_store(fs_handle->dpi_info->di_session, ino,
					position, &fb, 0);
	DFUSE_TRA_INFO(oh, "notfiy_store returned %d", rc);
	D_FREE(buff);
}
