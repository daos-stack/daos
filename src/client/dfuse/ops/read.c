/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

static void
dfuse_cb_read_complete(struct dfuse_event *ev)
{
	if (ev->de_ev.ev_error != 0) {
		DFUSE_REPLY_ERR_RAW(ev, ev->de_req, ev->de_ev.ev_error);
		D_GOTO(release, 0);
	}

	if (ev->de_len == 0) {
		DFUSE_TRA_DEBUG(ev, "Truncated read, (EOF)");
		D_GOTO(reply, 0);
	}

	if (ev->de_len < ev->de_iov.iov_len)
		DFUSE_TRA_DEBUG(ev, "Truncated read, requested %#zx returned %#zx",
				ev->de_iov.iov_len, ev->de_len);

reply:
	DFUSE_REPLY_BUF(ev, ev->de_req, ev->de_iov.iov_buf, ev->de_len);
release:
	d_slab_release(ev->de_handle->dpi_read_slab, ev);
}

void
dfuse_cb_read(fuse_req_t req, fuse_ino_t ino, size_t len, off_t position, struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl         *oh        = (struct dfuse_obj_hdl *)fi->fh;
	struct dfuse_projection_info *fs_handle = fuse_req_userdata(req);
	const struct fuse_ctx        *fc        = fuse_req_ctx(req);
	int                           rc;
	bool                          mock_read = false;
	struct dfuse_event           *ev;

	D_ASSERT(ino == oh->doh_ie->ie_stat.st_ino);

	ev = d_slab_acquire(fs_handle->dpi_read_slab);
	if (ev == NULL)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(ev, oh, "event");

	DFUSE_TRA_DEBUG(ev, "%#zx-%#zx requested pid=%d", position, position + len - 1, fc->pid);

	if (oh->doh_ie->ie_truncated && position + len < oh->doh_ie->ie_stat.st_size &&
	    ((oh->doh_ie->ie_start_off == 0 && oh->doh_ie->ie_end_off == 0) ||
	     position >= oh->doh_ie->ie_end_off || position + len <= oh->doh_ie->ie_start_off)) {
		DFUSE_TRA_DEBUG(oh, "Returning zeros");
		mock_read = true;
	}

	if (len > ev->de_iov.iov_buf_len) {
		D_ERROR("Fuse read buffer not large enough %zx > %zx\n", len,
			ev->de_iov.iov_buf_len);
	}

	ev->de_oh           = oh;
	ev->de_iov.iov_len  = len;
	ev->de_req          = req;
	ev->de_req_position = position;
	ev->de_sgl.sg_nr    = 1;

	if (mock_read) {
		ev->de_len = len;
		dfuse_cb_read_complete(ev);
		return;
	}

	ev->de_complete_cb = dfuse_cb_read_complete;

	rc = dfs_read(oh->doh_dfs, oh->doh_obj, &ev->de_sgl, position, &ev->de_len, &ev->de_ev);
	if (rc != 0) {
		DFUSE_REPLY_ERR_RAW(oh, req, rc);
		d_slab_release(fs_handle->dpi_read_slab, ev);
		return;
	}

	/* Send a message to the async thread to wake it up and poll for events */
	sem_post(&fs_handle->dpi_sem);

	/* Now ensure there are more read descriptors for the next request */
	d_slab_restock(fs_handle->dpi_read_slab);

	return;
err:
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
	if (ev)
		d_slab_release(fs_handle->dpi_read_slab, ev);
}
