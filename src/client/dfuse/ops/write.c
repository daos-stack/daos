/**
 * (C) Copyright 2016-2020 Intel Corporation.
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

static void
dfuse_cb_write_complete(struct dfuse_event *ev)
{
	if (ev->de_ev.ev_error == 0)
		DFUSE_REPLY_WRITE(ev, ev->de_req, ev->de_len);
	else
		DFUSE_REPLY_ERR_RAW(ev, ev->de_req,
				    daos_der2errno(ev->de_ev.ev_error));
	D_FREE(ev->de_iov.iov_buf);
}

void
dfuse_cb_write(fuse_req_t req, fuse_ino_t ino, struct fuse_bufvec *bufv,
	       off_t position, struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl		*oh = (struct dfuse_obj_hdl *)fi->fh;
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	const struct fuse_ctx		*fc = fuse_req_ctx(req);
	int				rc;
	struct dfuse_event		*ev;
	size_t				len = fuse_buf_size(bufv);
	struct fuse_bufvec		ibuf = FUSE_BUFVEC_INIT(len);

	DFUSE_TRA_INFO(oh, "%#zx-%#zx requested flags %#x pid=%d",
		       position, position + len - 1,
		       bufv->buf[0].flags, fc->pid);

	D_ALLOC_PTR(ev);
	if (ev == NULL)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(ev, oh, "write");

	/* Allocate temporary space for the data whilst they asynchronous
	 * operation is happening.
	 */

	/* Declare a bufvec on the stack and have fuse copy into it.
	 * For page size and above this will read directly into the
	 * buffer, avoiding any copying of the data.
	 */
	D_ALLOC(ibuf.buf[0].mem, len);
	if (ibuf.buf[0].mem == NULL)
		D_GOTO(err, rc = ENOMEM);

	rc = fuse_buf_copy(&ibuf, bufv, 0);
	if (rc != len)
		D_GOTO(err, rc = EIO);

	rc = daos_event_init(&ev->de_ev, fs_handle->dpi_eq, NULL);
	if (rc != -DER_SUCCESS)
		D_GOTO(err, rc = daos_der2errno(rc));

	ev->de_req = req;
	ev->de_len = len;
	ev->de_complete_cb = dfuse_cb_write_complete;

	ev->de_sgl.sg_nr = 1;
	d_iov_set(&ev->de_iov, ibuf.buf[0].mem, len);
	ev->de_sgl.sg_iovs = &ev->de_iov;

	/* Check for potentially using readahead on this file, ie_truncated
	 * will only be set if caching is enabled so only check for the one
	 * flag rather than two here
	 */
	if (oh->doh_ie->ie_truncated) {
		if (oh->doh_ie->ie_start_off == 0 &&
		    oh->doh_ie->ie_end_off == 0) {
			oh->doh_ie->ie_start_off = position;
			oh->doh_ie->ie_end_off = position + len;
		} else {
			if (oh->doh_ie->ie_start_off > position)
				oh->doh_ie->ie_start_off = position;
			if (oh->doh_ie->ie_end_off < position + len)
				oh->doh_ie->ie_end_off = position + len;
		}
	}

	rc = dfs_write(oh->doh_dfs, oh->doh_obj, &ev->de_sgl,
		       position, &ev->de_ev);
	if (rc != 0)
		D_GOTO(err, 0);

	/* Send a message to the async thread to wake it up and poll for events
	 */
	sem_post(&fs_handle->dpi_sem);
	return;

err:
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
	D_FREE(ibuf.buf[0].mem);
	D_FREE(ev);
}
