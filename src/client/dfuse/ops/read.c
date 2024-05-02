/**
 * (C) Copyright 2016-2024 Intel Corporation.
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
				ev->de_req_position + ev->de_req_len - 1);

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
	d_slab_restock(ev->de_eqt->de_read_slab);
	d_slab_release(ev->de_eqt->de_read_slab, ev);
}

#define K128 (1024 * 128)

static bool
dfuse_readahead_reply(fuse_req_t req, size_t len, off_t position, struct dfuse_obj_hdl *oh)
{
	size_t reply_len;

	if (oh->doh_readahead->dra_rc) {
		DFUSE_REPLY_ERR_RAW(oh, req, oh->doh_readahead->dra_rc);
		return true;
	}

	if (!oh->doh_linear_read || oh->doh_readahead->dra_ev == NULL) {
		DFUSE_TRA_DEBUG(oh, "Pre read disabled");
		return false;
	}

	if (((position % K128) == 0) && ((len % K128) == 0)) {
		DFUSE_TRA_INFO(oh, "allowing out-of-order pre read");
		/* Do not closely track the read position in this case, just the maximum,
		 * later checks will determine if the file is read to the end.
		 */
		oh->doh_linear_read_pos = max(oh->doh_linear_read_pos, position + len);
	} else if (oh->doh_linear_read_pos != position) {
		DFUSE_TRA_DEBUG(oh, "disabling pre read");
		daos_event_fini(&oh->doh_readahead->dra_ev->de_ev);
		d_slab_release(oh->doh_readahead->dra_ev->de_eqt->de_pre_read_slab,
			       oh->doh_readahead->dra_ev);
		oh->doh_readahead->dra_ev = NULL;
		return false;
	} else {
		oh->doh_linear_read_pos = position + len;
	}

	if (position + len >= oh->doh_readahead->dra_ev->de_readahead_len) {
		oh->doh_linear_read_eof = true;
	}

	/* At this point there is a buffer of known length that contains the data, and a read
	 * request.
	 * If the attempted read is bigger than the data then it will be truncated.
	 * It the attempted read is smaller than the buffer it will be met in full.
	 */

	if (position + len < oh->doh_readahead->dra_ev->de_readahead_len) {
		reply_len = len;
		DFUSE_TRA_DEBUG(oh, "%#zx-%#zx read", position, position + reply_len - 1);
	} else {
		/* The read will be truncated */
		reply_len = oh->doh_readahead->dra_ev->de_readahead_len - position;
		DFUSE_TRA_DEBUG(oh, "%#zx-%#zx read %#zx-%#zx not read (truncated)", position,
				position + reply_len - 1, position + reply_len, position + len - 1);
	}

	DFUSE_IE_STAT_ADD(oh->doh_ie, DS_PRE_READ);
	DFUSE_REPLY_BUFQ(oh, req, oh->doh_readahead->dra_ev->de_iov.iov_buf + position, reply_len);
	return true;
}

void
dfuse_cb_read(fuse_req_t req, fuse_ino_t ino, size_t len, off_t position, struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl *oh         = (struct dfuse_obj_hdl *)fi->fh;
	struct dfuse_info    *dfuse_info = fuse_req_userdata(req);
	bool                  mock_read  = false;
	struct dfuse_eq      *eqt;
	int                   rc;
	struct dfuse_event   *ev;
	uint64_t              eqt_idx;

	DFUSE_IE_STAT_ADD(oh->doh_ie, DS_READ);

	if (oh->doh_linear_read_eof && position == oh->doh_linear_read_pos) {
		DFUSE_TRA_DEBUG(oh, "Returning EOF early without round trip %#zx", position);
		oh->doh_linear_read_eof = false;
		oh->doh_linear_read     = false;

		if (oh->doh_readahead) {
			D_MUTEX_LOCK(&oh->doh_readahead->dra_lock);
			ev = oh->doh_readahead->dra_ev;

			oh->doh_readahead->dra_ev = NULL;
			D_MUTEX_UNLOCK(&oh->doh_readahead->dra_lock);

			if (ev) {
				daos_event_fini(&ev->de_ev);
				d_slab_release(ev->de_eqt->de_pre_read_slab, ev);
				DFUSE_IE_STAT_ADD(oh->doh_ie, DS_PRE_READ);
			}
		}
		DFUSE_REPLY_BUFQ(oh, req, NULL, 0);
		return;
	}

	if (oh->doh_readahead) {
		bool replied;

		D_MUTEX_LOCK(&oh->doh_readahead->dra_lock);
		replied = dfuse_readahead_reply(req, len, position, oh);
		D_MUTEX_UNLOCK(&oh->doh_readahead->dra_lock);

		if (replied)
			return;
	}

	eqt_idx = atomic_fetch_add_relaxed(&dfuse_info->di_eqt_idx, 1);
	eqt = &dfuse_info->di_eqt[eqt_idx % dfuse_info->di_eq_count];

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

	DFUSE_IE_WFLUSH(oh->doh_ie);

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

static void
dfuse_cb_pre_read_complete(struct dfuse_event *ev)
{
	struct dfuse_obj_hdl *oh = ev->de_oh;

	oh->doh_readahead->dra_rc = ev->de_ev.ev_error;

	if (ev->de_ev.ev_error != 0) {
		oh->doh_readahead->dra_rc = ev->de_ev.ev_error;
		daos_event_fini(&ev->de_ev);
		d_slab_release(ev->de_eqt->de_pre_read_slab, ev);
		oh->doh_readahead->dra_ev = NULL;
	}

	/* If the length is not as expected then the file has been modified since the last stat so
	 * discard this cache and use regular reads.  Note that this will only detect files which
	 * have shrunk in size, not grown.
	 */
	if (ev->de_len != ev->de_readahead_len) {
		daos_event_fini(&ev->de_ev);
		d_slab_release(ev->de_eqt->de_pre_read_slab, ev);
		oh->doh_readahead->dra_ev = NULL;
	}

	D_MUTEX_UNLOCK(&oh->doh_readahead->dra_lock);
}

void
dfuse_pre_read(struct dfuse_info *dfuse_info, struct dfuse_obj_hdl *oh)
{
	struct dfuse_eq    *eqt;
	int                 rc;
	struct dfuse_event *ev;
	uint64_t            eqt_idx;
	size_t              len = oh->doh_ie->ie_stat.st_size;

	eqt_idx = atomic_fetch_add_relaxed(&dfuse_info->di_eqt_idx, 1);
	eqt     = &dfuse_info->di_eqt[eqt_idx % dfuse_info->di_eq_count];

	ev = d_slab_acquire(eqt->de_pre_read_slab);
	if (ev == NULL)
		D_GOTO(err, rc = ENOMEM);

	ev->de_iov.iov_len   = len;
	ev->de_req           = 0;
	ev->de_sgl.sg_nr     = 1;
	ev->de_oh            = oh;
	ev->de_readahead_len = len;
	ev->de_req_position  = 0;

	ev->de_complete_cb        = dfuse_cb_pre_read_complete;
	oh->doh_readahead->dra_ev = ev;

	rc = dfs_read(oh->doh_dfs, oh->doh_obj, &ev->de_sgl, 0, &ev->de_len, &ev->de_ev);
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
	oh->doh_readahead->dra_rc = rc;
	if (ev) {
		daos_event_fini(&ev->de_ev);
		d_slab_release(eqt->de_pre_read_slab, ev);
		oh->doh_readahead->dra_ev = NULL;
	}
	D_MUTEX_UNLOCK(&oh->doh_readahead->dra_lock);
}
