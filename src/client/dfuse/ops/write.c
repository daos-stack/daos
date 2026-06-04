/**
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
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
	struct dfuse_event   *ev = NULL;
	uint64_t              eqt_idx;
	bool                  wb_cache = false;
	bool                  first_write      = false;
	bool                  first_open_write = false;
	off_t                 end_position;

	DFUSE_IE_STAT_ADD(oh->doh_ie, DS_WRITE);

	if (len == 0) {
		DFUSE_REPLY_WRITE(oh, req, (size_t)0);
		return;
	}

	if (position < 0)
		D_GOTO(err, rc = EINVAL);

	if (__builtin_add_overflow(position, (off_t)len, &end_position))
		D_GOTO(err, rc = EFBIG);

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
	first_write = (atomic_fetch_add_relaxed(&oh->doh_write_count, 1) == 0);
	if (first_write) {
		first_open_write =
		    (atomic_fetch_add_relaxed(&oh->doh_ie->ie_open_write_count, 1) == 0);
		if (first_open_write) {
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
	if (len > ev->de_iov.iov_buf_len) {
		D_WARN("Fuse write buffer not large enough %zx > %zx\n", len,
		       ev->de_iov.iov_buf_len);
		D_GOTO(err, rc = EIO);
	}

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

	rc = dfs_write(oh->doh_dfs, oh->doh_obj, &ev->de_sgl, position, &ev->de_ev);
	if (rc != 0)
		D_GOTO(err, rc);

	/* Check for potentially using readahead on this file, ie_truncated
	 * will only be set if caching is enabled so only check for the one
	 * flag rather than two here
	 */
	if (oh->doh_ie->ie_truncated) {
		if (oh->doh_ie->ie_start_off == 0 && oh->doh_ie->ie_end_off == 0) {
			oh->doh_ie->ie_start_off = position;
			oh->doh_ie->ie_end_off   = end_position;
		} else {
			if (oh->doh_ie->ie_start_off > position)
				oh->doh_ie->ie_start_off = position;
			if (oh->doh_ie->ie_end_off < end_position)
				oh->doh_ie->ie_end_off = end_position;
		}
	}

	if (end_position > oh->doh_ie->ie_stat.st_size)
		oh->doh_ie->ie_stat.st_size = end_position;

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

	if (first_write) {
		if (first_open_write)
			atomic_fetch_sub_relaxed(&oh->doh_ie->ie_open_write_count, 1);
		atomic_fetch_sub_relaxed(&oh->doh_write_count, 1);
	}

	DFUSE_REPLY_ERR_RAW(oh, req, rc);
	if (ev) {
		daos_event_fini(&ev->de_ev);
		d_slab_release(eqt->de_write_slab, ev);
	}
}
