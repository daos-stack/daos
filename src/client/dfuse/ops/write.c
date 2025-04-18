/**
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2025 Google LLC
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

static void
dfuse_cb_write_complete(struct dfuse_event *ev)
{
	if (ev->de_req) {
		if (ev->de_ev.ev_error == 0)
			DFUSE_REPLY_WRITE(ev->de_oh, ev->de_req, ev->de_len);
		else
			DFUSE_REPLY_ERR_RAW(ev->de_oh, ev->de_req, ev->de_ev.ev_error);
	} else {
		D_RWLOCK_UNLOCK(&ev->de_oh->doh_ie->ie_wlock);
	}
	daos_event_fini(&ev->de_ev);
	d_slab_release(ev->de_eqt->de_write_slab, ev);
}

static int
dfuse_log_config_copy(char *dest, int len, void *config)
{
	int                 rc;
	struct fuse_bufvec *bufv = config;
	struct fuse_bufvec  ibuf = FUSE_BUFVEC_INIT(len);

	ibuf.buf[0].mem = dest;
	rc              = fuse_buf_copy(&ibuf, bufv, 0);
	if (rc != len) {
		DL_ERROR(rc, "Error copying input data from fuse buffer");
		return -DER_IO;
	}

	return 0;
}

void
dfuse_cb_write(fuse_req_t req, fuse_ino_t ino, struct fuse_bufvec *bufv, off_t position,
	       struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl *oh         = (struct dfuse_obj_hdl *)fi->fh;
	struct dfuse_info    *dfuse_info = fuse_req_userdata(req);
	size_t                len        = fuse_buf_size(bufv);
	struct fuse_bufvec    ibuf       = FUSE_BUFVEC_INIT(len);
	struct dfuse_eq      *eqt;
	int                   rc;
	struct dfuse_event   *ev;
	uint64_t              eqt_idx;
	bool                  wb_cache = false;

	if (ino == DFUSE_CTRL_INO) {
		rc = d_log_parse_config(bufv, len, dfuse_log_config_copy);
		if (rc != 0) {
			DL_ERROR(rc, "Could not parse log ctrl config");
			fuse_reply_err(req, daos_der2errno(rc));
		} else {
			fuse_reply_write(req, len);
		}
		return;
	}

	DFUSE_IE_STAT_ADD(oh->doh_ie, DS_WRITE);

	oh->doh_linear_read = false;

	eqt_idx = atomic_fetch_add_relaxed(&dfuse_info->di_eqt_idx, 1);

	eqt = &dfuse_info->di_eqt[eqt_idx % dfuse_info->di_eq_count];

	if (oh->doh_ie->ie_dfs->dfc_wb_cache) {
		D_RWLOCK_RDLOCK(&oh->doh_ie->ie_wlock);
		wb_cache = true;
	}

	DFUSE_TRA_DEBUG(oh, "%#zx-%#zx requested flags %#x", position, position + len - 1,
			bufv->buf[0].flags);

	/* Evict the metadata cache here so the lookup doesn't return stale size/time info */
	if (atomic_fetch_add_relaxed(&oh->doh_write_count, 1) == 0) {
		if (atomic_fetch_add_relaxed(&oh->doh_ie->ie_open_write_count, 1) == 0) {
			dfuse_mcache_evict(oh->doh_ie);
		}
	}

	ev = d_slab_acquire(eqt->de_write_slab);
	if (ev == NULL)
		D_GOTO(err, rc = ENOMEM);

	/* Declare a bufvec on the stack and have fuse copy into it.
	 * For page size and above this will read directly into the
	 * buffer, avoiding any copying of the data.
	 */
	ibuf.buf[0].mem = ev->de_iov.iov_buf;

	rc = fuse_buf_copy(&ibuf, bufv, 0);
	if (rc != len)
		D_GOTO(err, rc = EIO);

	ev->de_oh          = oh;
	ev->de_iov.iov_len = len;
	if (wb_cache)
		ev->de_req = 0;
	else
		ev->de_req = req;
	ev->de_len         = len;
	ev->de_complete_cb = dfuse_cb_write_complete;

	/* Check for potentially using readahead on this file, ie_truncated
	 * will only be set if caching is enabled so only check for the one
	 * flag rather than two here
	 */
	if (oh->doh_ie->ie_truncated) {
		if (oh->doh_ie->ie_start_off == 0 && oh->doh_ie->ie_end_off == 0) {
			oh->doh_ie->ie_start_off = position;
			oh->doh_ie->ie_end_off   = position + len;
		} else {
			if (oh->doh_ie->ie_start_off > position)
				oh->doh_ie->ie_start_off = position;
			if (oh->doh_ie->ie_end_off < position + len)
				oh->doh_ie->ie_end_off = position + len;
		}
	}

	if (len + position > oh->doh_ie->ie_stat.st_size)
		oh->doh_ie->ie_stat.st_size = len + position;

	rc = dfs_write(oh->doh_dfs, oh->doh_obj, &ev->de_sgl, position, &ev->de_ev);
	if (rc != 0)
		D_GOTO(err, rc);

	if (wb_cache)
		DFUSE_REPLY_WRITE(oh, req, len);

	/* Send a message to the async thread to wake it up and poll for events */
	sem_post(&eqt->de_sem);

	/* Now ensure there are more descriptors for the next request */
	d_slab_restock(eqt->de_write_slab);

	return;

err:
	if (wb_cache)
		D_RWLOCK_UNLOCK(&oh->doh_ie->ie_wlock);
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
	if (ev) {
		daos_event_fini(&ev->de_ev);
		d_slab_release(eqt->de_write_slab, ev);
	}
}
