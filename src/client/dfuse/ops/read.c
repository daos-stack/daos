/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

#define READAHEAD_SIZE (1024 * 1024)

static void
dfuse_cb_read_complete(struct dfuse_event *ev)
{
	if (ev->de_ev.ev_error != 0) {
		DFUSE_REPLY_ERR_RAW(ev, ev->de_req, ev->de_ev.ev_error);
		D_GOTO(free, 0);
	}

	if (ev->de_len == 0) {
		DFUSE_TRA_DEBUG(ev, "Truncated read, (EOF)");
		DFUSE_REPLY_BUF(ev, ev->de_req, ev->de_iov.iov_buf, ev->de_len);
		D_GOTO(free, 0);
	}

	if (ev->de_len < ev->de_req_len)
		DFUSE_TRA_DEBUG(ev, "Truncated read, requested %#zx returned %#zx", ev->de_req_len,
				ev->de_len);
	else if (ev->de_len < ev->de_iov.iov_buf_len)
		DFUSE_TRA_DEBUG(ev, "Truncated readhead, requested %#zx returned %#zx",
				ev->de_iov.iov_buf_len, ev->de_len);

	if (ev->de_len > ev->de_req_len) {
		struct dfuse_obj_hdl *oh;
		int                   rc;

		oh = ev->de_oh;
		rc = pthread_mutex_trylock(&oh->doh_ie->ie_dfs->dfs_read_mutex);
		if (rc == 0) {
			struct dfuse_projection_info *fs_handle;
			struct fuse_bufvec	     fb = {};
			off_t			     ra_start;
			off_t			     ra_len;

			fs_handle      = fuse_req_userdata(ev->de_req);
			ra_start       = ev->de_req_position + ev->de_req_len;
			ra_len         = ev->de_len - ev->de_req_len;

			fb.count       = 1;
			fb.buf[0].mem  = ev->de_iov.iov_buf + ev->de_req_len;
			fb.buf[0].size = ra_len;

			DFUSE_TRA_DEBUG(ev, "%#zx-%#zx was readahead", ra_start,
					ra_start + ra_len - 1);

			rc = fuse_lowlevel_notify_store(fs_handle->dpi_info->di_session,
							oh->doh_ie->ie_stat.st_ino, ra_start, &fb,
							0);
			if (rc == 0)
				DFUSE_TRA_DEBUG(ev, "notify_store returned %d", rc);
			else
				DFUSE_TRA_INFO(ev, "notify_store returned %d", rc);
			rc = pthread_mutex_unlock(&oh->doh_ie->ie_dfs->dfs_read_mutex);
			if (rc != 0)
				DFUSE_TRA_ERROR(ev, "Mutex unlock failed");
		}
		/* Now do the reply with only what the kernel requested */
		ev->de_len = ev->de_req_len;
	}

	DFUSE_REPLY_BUF(ev, ev->de_req, ev->de_iov.iov_buf, ev->de_len);
free:
	D_FREE(ev->de_iov.iov_buf);
}

void
dfuse_cb_read(fuse_req_t req, fuse_ino_t ino, size_t len, off_t position, struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl		*oh = (struct dfuse_obj_hdl *)fi->fh;
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	const struct fuse_ctx		*fc = fuse_req_ctx(req);
	void				*buff;
	int				rc;
	size_t				buff_len  = len;
	bool				mock_read = false;
	bool				readahead = false;
	struct dfuse_event		*ev = NULL;

	D_ASSERT(ino == oh->doh_ie->ie_stat.st_ino);

	D_ALLOC_PTR(ev);
	if (ev == NULL)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(ev, oh, "event");

	DFUSE_TRA_DEBUG(ev, "%#zx-%#zx requested pid=%d", position, position + len - 1, fc->pid);

	if (oh->doh_ie->ie_truncated && position + len < oh->doh_ie->ie_stat.st_size &&
	    ((oh->doh_ie->ie_start_off == 0 && oh->doh_ie->ie_end_off == 0) ||
	     position >= oh->doh_ie->ie_end_off || position + len <= oh->doh_ie->ie_start_off)) {
		off_t pos_ra = position + len + READAHEAD_SIZE;

		DFUSE_TRA_DEBUG(oh, "Returning zeros");
		mock_read = true;

		if (pos_ra <= oh->doh_ie->ie_stat.st_size &&
		    ((oh->doh_ie->ie_start_off == 0 && oh->doh_ie->ie_end_off == 0) ||
		     (position >= oh->doh_ie->ie_end_off || pos_ra <= oh->doh_ie->ie_start_off))) {
			readahead = true;
		}
	} else if (oh->doh_caching && len < (1024 * 1024) &&
		   oh->doh_ie->ie_stat.st_size > (1024 * 1024)) {
		/* Only do readahead if the requested size is less than 1Mb and
		 * the file size is > 1Mb
		 */

		readahead = true;
	}

	if (fs_handle->dpi_info->di_wb_cache)
		readahead = false;

	if (readahead)
		buff_len += READAHEAD_SIZE;

	D_ALLOC(buff, buff_len);
	if (!buff)
		D_GOTO(err, rc = ENOMEM);

	d_iov_set(&ev->de_iov, (void *)buff, buff_len);
	ev->de_sgl.sg_iovs  = &ev->de_iov;
	ev->de_oh           = oh;
	ev->de_req          = req;
	ev->de_req_len      = len;
	ev->de_req_position = position;
	ev->de_sgl.sg_nr    = 1;

	if (mock_read) {
		ev->de_len = buff_len;
		dfuse_cb_read_complete(ev);
		D_FREE(ev);
		return;
	}

	rc = daos_event_init(&ev->de_ev, fs_handle->dpi_eq, NULL);
	if (rc != -DER_SUCCESS)
		D_GOTO(err_buff, rc = daos_der2errno(rc));

	ev->de_complete_cb = dfuse_cb_read_complete;

	rc = dfs_read(oh->doh_dfs, oh->doh_obj, &ev->de_sgl, position, &ev->de_len, &ev->de_ev);
	if (rc != 0) {
		DFUSE_REPLY_ERR_RAW(oh, req, rc);
		D_FREE(buff);
		D_FREE(ev);
		return;
	}

	/* Send a message to the async thread to wake it up and poll for events */
	sem_post(&fs_handle->dpi_sem);
	return;
err_buff:
	D_FREE(buff);
err:
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
	D_FREE(ev);
}
