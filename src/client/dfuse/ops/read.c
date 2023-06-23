/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

static void
dfuse_cb_read_complete(struct dfuse_event *ev)
{
	struct dfuse_obj_hdl *oh = ev->de_oh;

	if (ev->de_ev.ev_error != 0) {
		DFUSE_REPLY_ERR_RAW(oh, ev->de_req, ev->de_ev.ev_error);
		D_GOTO(release, 0);
	}

	if (oh->doh_linear_read) {
		if (oh->doh_linear_read_pos != ev->de_req_position) {
			oh->doh_linear_read = false;
		} else {
			oh->doh_linear_read_pos = ev->de_req_position + ev->de_len;
			if (ev->de_len < ev->de_req_len) {
				oh->doh_linear_read_eof = true;
			}
		}
	}

	if (ev->de_len == 0) {
		DFUSE_TRA_DEBUG(oh, "%#zx-%#zx requested (EOF)", ev->de_req_position,
				ev->de_req_position + ev->de_iov.iov_buf_len - 1);

		DFUSE_REPLY_BUFQ(oh, ev->de_req, ev->de_iov.iov_buf, ev->de_len);
		D_GOTO(release, 0);
	}

	if (ev->de_len == ev->de_req_len)
		DFUSE_TRA_DEBUG(oh, "%#zx-%#zx read", ev->de_req_position,
				ev->de_req_position + ev->de_req_len - 1);
	else
		DFUSE_TRA_DEBUG(oh, "%#zx-%#zx read %#zx-%#zx not read (truncated)",
				ev->de_req_position, ev->de_req_position + ev->de_len - 1,
				ev->de_req_position + ev->de_len,
				ev->de_req_position + ev->de_req_len - 1);

	DFUSE_REPLY_BUFQ(oh, ev->de_req, ev->de_iov.iov_buf, ev->de_len);
release:
	daos_event_fini(&ev->de_ev);
	d_slab_release(ev->de_eqt->de_read_slab, ev);
}

void
dfuse_cb_read(fuse_req_t req, fuse_ino_t ino, size_t len, off_t position, struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl         *oh        = (struct dfuse_obj_hdl *)fi->fh;
	struct dfuse_projection_info *fs_handle = fuse_req_userdata(req);
	bool                          mock_read = false;
	struct dfuse_eq              *eqt;
	int                           rc;
	struct dfuse_event           *ev;
	uint64_t                      eqt_idx;

	if (oh->doh_linear_read_eof && position == oh->doh_linear_read_pos) {
		DFUSE_TRA_DEBUG(oh, "Returning EOF early without round trip %#zx", position);
		oh->doh_linear_read_eof = false;
		oh->doh_linear_read     = false;
		DFUSE_REPLY_BUFQ(oh, req, NULL, 0);
		return;
	}

	eqt_idx = atomic_fetch_add_relaxed(&fs_handle->di_eqt_idx, 1);

	eqt = &fs_handle->di_eqt[eqt_idx % fs_handle->di_eq_count];

	ev = d_slab_acquire(eqt->de_read_slab);
	if (ev == NULL)
		D_GOTO(err, rc = ENOMEM);

	if (oh->doh_ie->ie_truncated && position + len < oh->doh_ie->ie_stat.st_size &&
	    ((oh->doh_ie->ie_start_off == 0 && oh->doh_ie->ie_end_off == 0) ||
	     position >= oh->doh_ie->ie_end_off || position + len <= oh->doh_ie->ie_start_off)) {
		DFUSE_TRA_DEBUG(oh, "Returning zeros");
		mock_read = true;
	}

	/* DFuse requests a buffer size of "0" which translates to 1024*1024 at the time of writing
	 * however this may change over time.  If the kernel ever starts requesting larger reads
	 * then dfuse will need to be updated to pre-allocate larger buffers.  Add a warning here,
	 * not that DFuse won't function correctly but if this value ever changes then DFuse will
	 * need updating to make full use of larger buffer sizes.
	 */
	if (len > ev->de_iov.iov_buf_len) {
		D_WARN("Fuse read buffer not large enough %zx > %zx\n", len,
		       ev->de_iov.iov_buf_len);
	}

	ev->de_iov.iov_len  = len;
	ev->de_req          = req;
	ev->de_sgl.sg_nr    = 1;
	ev->de_oh           = oh;
	ev->de_req_len      = len;
	ev->de_req_position = position;

	if (mock_read) {
		ev->de_len = len;
		dfuse_cb_read_complete(ev);
		return;
	}

	ev->de_complete_cb = dfuse_cb_read_complete;

	rc = dfs_read(oh->doh_dfs, oh->doh_obj, &ev->de_sgl, position, &ev->de_len, &ev->de_ev);
	if (rc != 0) {
		D_GOTO(err, rc);
		return;
	}

	/* Send a message to the async thread to wake it up and poll for events */
	sem_post(&eqt->de_sem);

	/* Now ensure there are more descriptors for the next request */
	d_slab_restock(eqt->de_read_slab);

	return;
err:
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
	if (ev) {
		daos_event_fini(&ev->de_ev);
		d_slab_release(eqt->de_read_slab, ev);
	}
}
