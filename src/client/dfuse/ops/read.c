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

#define READAHEAD_SIZE (1024*1024)

void
dfuse_cb_read(fuse_req_t req, fuse_ino_t ino, size_t len, off_t position,
	      struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl		*oh = (struct dfuse_obj_hdl *)fi->fh;
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	const struct fuse_ctx		*fc = fuse_req_ctx(req);
	d_iov_t				iov[2] = {};
	d_sg_list_t			sgl = {};
	struct fuse_bufvec		fb = {};
	daos_size_t			size;
	void				*buff;
	void				*ahead_buff = NULL;
	int				rc;

	DFUSE_TRA_INFO(oh, "%#zx-%#zx requested pid=%d",
		       position, position + len - 1, fc->pid);

	DFUSE_TRA_DEBUG(oh, "Will try readahead file size %zi",
			oh->doh_ie->ie_stat.st_size);

	D_ALLOC(buff, len);
	if (!buff) {
		DFUSE_REPLY_ERR_RAW(NULL, req, ENOMEM);
		return;
	}

	/* TODO:
	 *
	 * fuse_lowlevel_notify_store() does not call free() on the buffer
	 * after use so a single buffer rather than two would work well here.
	 * We could combine the truncated and readahead logic by just having
	 * the trunctated check set a "skip-dfs_read" flag and just have one
	 * flow through this function rather than two.
	 * For the ie_truncated code a lot of zeroed data is being passed to
	 * the kernel so it would be possible to have a "zero-buffer" allcocated
	 * once and reuse it rather than a malloc + memset every time.
	 */

	if (oh->doh_ie->ie_truncated &&
	    position + len < oh->doh_ie->ie_stat.st_size &&
	    ((oh->doh_ie->ie_start_off == 0 && oh->doh_ie->ie_end_off == 0) ||
	     position >= oh->doh_ie->ie_end_off ||
	     position + len <= oh->doh_ie->ie_start_off)) {
		off_t pos_ra = position + len + READAHEAD_SIZE;

		DFUSE_TRA_DEBUG(oh, "Returning zeros");

		if (pos_ra <= oh->doh_ie->ie_stat.st_size &&
		    ((oh->doh_ie->ie_start_off == 0 && oh->doh_ie->ie_end_off == 0) ||
		     (position >= oh->doh_ie->ie_end_off ||
		      pos_ra <= oh->doh_ie->ie_start_off))) {
		    D_ALLOC(ahead_buff, READAHEAD_SIZE);

		    if (ahead_buff) {
			fb.count = 1;
			fb.buf[0].mem = ahead_buff;
			fb.buf[0].size = READAHEAD_SIZE;

			rc = fuse_lowlevel_notify_store(fs_handle->dpi_info->di_session, ino,
				position + len, &fb, 0);
			D_FREE(ahead_buff);
		    }
		}

		DFUSE_REPLY_BUF(oh, req, buff, len);
		D_FREE(buff);
		return;
	}

	sgl.sg_nr = 1;
	d_iov_set(&iov[0], (void *)buff, len);
	sgl.sg_iovs = iov;

	/* Only do readahead if the requested size is less than 1Mb and the file
	 * size is > 1Mb
	 */
	if ((oh->doh_ie->ie_dfs->dfs_attr_timeout > 0) &&
		len < (1024 * 1024) &&
		oh->doh_ie->ie_stat.st_size > (1024 * 1024)) {

		D_ALLOC(ahead_buff, READAHEAD_SIZE);
		if (ahead_buff != NULL) {
			d_iov_set(&iov[1], (void *)ahead_buff, READAHEAD_SIZE);
			sgl.sg_nr = 2;
		}
	}

	rc = dfs_read(oh->doh_dfs, oh->doh_obj, &sgl, position, &size, NULL);
	if (rc != -DER_SUCCESS) {
		DFUSE_REPLY_ERR_RAW(oh, req, rc);
		D_FREE(buff);
		D_FREE(ahead_buff);
		return;
	}

	if (size <= len) {
		DFUSE_REPLY_BUF(oh, req, buff, size);
		D_FREE(buff);
		D_FREE(ahead_buff);
		return;
	}

	fb.count = 1;
	fb.buf[0].mem = ahead_buff;
	fb.buf[0].size = size - len;

	DFUSE_TRA_INFO(oh, "%#zx-%#zx was readahead",
		       position + len, position + size - 1);

	rc = fuse_lowlevel_notify_store(fs_handle->dpi_info->di_session, ino,
					position + len, &fb, 0);
	if (rc == 0)
		DFUSE_TRA_DEBUG(oh, "notfiy_store returned %d", rc);
	else
		DFUSE_TRA_INFO(oh, "notfiy_store returned %d", rc);

	DFUSE_REPLY_BUF(oh, req, buff, len);
	D_FREE(buff);
	D_FREE(ahead_buff);
}
