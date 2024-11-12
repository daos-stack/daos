/**
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

static void
cb_read_helper(struct dfuse_event *ev, void *buff)
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

	if (ev->de_len == ev->de_req_len) {
		DFUSE_TRA_DEBUG(oh, "%#zx-%#zx read", ev->de_req_position,
				ev->de_req_position + ev->de_req_len - 1);
	} else {
		struct dfuse_inode_entry *ie = oh->doh_ie;

		ie->ie_active->seen_eof = true;
		if (ev->de_len == 0)
			ie->ie_active->file_size = ev->de_req_position;
		else
			ie->ie_active->file_size = ev->de_req_position + ev->de_len - 1;

		if (ev->de_len == 0)
			DFUSE_TRA_DEBUG(oh, "%#zx-%#zx requested (EOF)", ev->de_req_position,
					ev->de_req_position + ev->de_req_len - 1);
		else
			DFUSE_TRA_DEBUG(oh, "%#zx-%#zx read %#zx-%#zx not read (truncated)",
					ev->de_req_position, ev->de_req_position + ev->de_len - 1,
					ev->de_req_position + ev->de_len,
					ev->de_req_position + ev->de_req_len - 1);
	}

	DFUSE_REPLY_BUFQ(oh, ev->de_req, buff, ev->de_len);
release:
	daos_event_fini(&ev->de_ev);
}

static void
dfuse_cb_read_complete(struct dfuse_event *ev)
{
	struct dfuse_event *evs, *evn;

	D_SPIN_LOCK(&ev->de_oh->doh_ie->ie_active->lock);
	d_list_del(&ev->de_read_list);
	D_SPIN_UNLOCK(&ev->de_oh->doh_ie->ie_active->lock);

	d_list_for_each_entry(evs, &ev->de_read_slaves, de_read_list) {
		DFUSE_TRA_DEBUG(ev->de_oh, "concurrent network read %p", evs->de_oh);
		evs->de_len         = min(ev->de_len, evs->de_req_len);
		evs->de_ev.ev_error = ev->de_ev.ev_error;
		DFUSE_IE_STAT_ADD(ev->de_oh->doh_ie, DS_READ_CON);
		cb_read_helper(evs, ev->de_iov.iov_buf);
	}

	cb_read_helper(ev, ev->de_iov.iov_buf);

	d_list_for_each_entry_safe(evs, evn, &ev->de_read_slaves, de_read_list) {
		d_list_del(&evs->de_read_list);
		d_slab_restock(evs->de_eqt->de_read_slab);
		d_slab_release(evs->de_eqt->de_read_slab, evs);
	}

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

static struct dfuse_eq *
pick_eqt(struct dfuse_info *dfuse_info)
{
	uint64_t eqt_idx;

	eqt_idx = atomic_fetch_add_relaxed(&dfuse_info->di_eqt_idx, 1);
	return &dfuse_info->di_eqt[eqt_idx % dfuse_info->di_eq_count];
}

/* Chunk read and coalescing
 *
 * This code attempts to predict application and kernel I/O patterns and preemptively read file
 * data ahead of when it's requested.
 *
 * For some kernels read I/O size is limited to 128k when using the page cache or 1Mb when using
 * direct I/O.  To get around the performance impact of them detect when well aligned 128k reads
 * are received and read an entire buffers worth, then for future requests the data should already
 * be in cache.
 *
 * This code is entered when caching is enabled and reads are correctly size/aligned and not in the
 * last CHUNK_SIZE of a file.  When open then the inode contains a single read_chunk_core pointer
 * and this contains a list of read_chunk_data entries, one for each bucket.
 *
 * TODO: Currently there is no code to remove buckets from the list so all buckets will remain in
 * memory until close.
 */

#define CHUNK_SIZE (1024 * 1024)

#define MAX_REQ_COUNT 256

struct read_chunk_data {
	struct dfuse_event   *ev;
	bool                  slot_done[8];
	struct active_inode  *ia;
	d_list_t              list;
	uint64_t              bucket;
	struct dfuse_eq      *eqt;
	int                   rc;
	int                   entered;
	bool                  complete;
	int                   idx;
	struct {
		int                   slot;
		fuse_req_t            req;
		struct dfuse_obj_hdl *oh;
	} reqs[MAX_REQ_COUNT];
};

/* Called when the last open file handle on a inode is closed.  This needs to free everything which
 * is complete and for anything that isn't flag it for deletion in the callback.
 *
 * Returns true if the feature was used.
 */
bool
read_chunk_close(struct active_inode *active)
{
	struct read_chunk_data *cd, *cdn;

	if (d_list_empty(&active->chunks))
		return false;

	d_list_for_each_entry_safe(cd, cdn, &active->chunks, list) {
		D_ASSERT(cd->complete);
		d_list_del(&cd->list);
		d_slab_release(cd->eqt->de_read_slab, cd->ev);
		D_FREE(cd);
	}
	return true;
}

static void
chunk_cb(struct dfuse_event *ev)
{
	struct read_chunk_data *cd = ev->de_cd;
	struct active_inode    *ia = cd->ia;

	cd->rc = ev->de_ev.ev_error;

	if (cd->rc == 0 && (ev->de_len != CHUNK_SIZE)) {
		cd->rc = EIO;
		DS_WARN(cd->rc, "Unexpected short read bucket %ld (%#zx) expected %i got %zi",
			cd->bucket, cd->bucket * CHUNK_SIZE, CHUNK_SIZE, ev->de_len);
	}

	daos_event_fini(&ev->de_ev);

	/* Mark as complete so no more get put on list */
	D_SPIN_LOCK(&ia->lock);
	cd->complete = true;

	/* Mark the slot as replied to.  There's a race here as the slot hasn't been replied to
	 * however references are dropped by the DFUSE_REPLY macros below so an extra ref on active
	 * would be required.
	 */
	for (int i = 0; i < cd->idx; i++)
		cd->slot_done[cd->reqs[i].slot] = true;
	D_SPIN_UNLOCK(&ia->lock);

	for (int i = 0; i < cd->idx; i++) {
		int        slot;
		fuse_req_t req;
		size_t     position;

		slot = cd->reqs[i].slot;
		req  = cd->reqs[i].req;

		DFUSE_TRA_DEBUG(cd->reqs[i].oh, "Replying idx %d/%d for %ld[%d]", i, cd->idx,
				cd->bucket, slot);

		position = (cd->bucket * CHUNK_SIZE) + (slot * K128);
		DFUSE_IE_STAT_ADD(cd->reqs[i].oh->doh_ie, DS_READ_BUCKET);
		if (cd->rc != 0) {
			DFUSE_REPLY_ERR_RAW(cd->reqs[i].oh, req, cd->rc);
		} else {
			DFUSE_TRA_DEBUG(cd->reqs[i].oh, "%#zx-%#zx read", position,
					position + K128 - 1);
			DFUSE_REPLY_BUFQ(cd->reqs[i].oh, req, ev->de_iov.iov_buf + (slot * K128),
					 K128);
		}
	}
}

/* Submit a read to dfs.
 *
 * Returns true on success.
 */
static bool
chunk_fetch(fuse_req_t req, struct dfuse_inode_entry *ie, struct read_chunk_data *cd)
{
	struct dfuse_info  *dfuse_info = fuse_req_userdata(req);
	struct dfuse_event *ev;
	struct dfuse_eq    *eqt;
	int                 rc;
	daos_off_t          position = cd->bucket * CHUNK_SIZE;

	eqt = pick_eqt(dfuse_info);

	ev = d_slab_acquire(eqt->de_read_slab);
	if (ev == NULL) {
		cd->rc = ENOMEM;
		return false;
	}

	ev->de_iov.iov_len = CHUNK_SIZE;
	ev->de_req         = req;
	ev->de_cd          = cd;
	ev->de_sgl.sg_nr   = 1;
	ev->de_len         = 0;
	ev->de_complete_cb = chunk_cb;

	cd->ev  = ev;
	cd->eqt = eqt;

	rc = dfs_read(ie->ie_dfs->dfs_ns, ie->ie_obj, &ev->de_sgl, position, &ev->de_len,
		      &ev->de_ev);
	if (rc != 0)
		goto err;

	/* Send a message to the async thread to wake it up and poll for events */
	sem_post(&eqt->de_sem);

	/* Now ensure there are more descriptors for the next request */
	d_slab_restock(eqt->de_read_slab);

	return true;

err:
	daos_event_fini(&ev->de_ev);
	d_slab_release(eqt->de_read_slab, ev);
	cd->ev = NULL;
	cd->rc = rc;
	return false;
}

/* Try and do a bulk read.
 *
 * Returns true if it was able to handle the read.
 */
static bool
chunk_read(fuse_req_t req, size_t len, off_t position, struct dfuse_obj_hdl *oh)
{
	struct dfuse_inode_entry *ie = oh->doh_ie;
	struct read_chunk_data   *cd;
	off_t                     last;
	uint64_t                  bucket;
	int                       slot;
	bool                      submit = false;
	bool                      rcb;
	bool                      all_done = true;

	if (len != K128)
		return false;

	if ((position % K128) != 0)
		return false;

	last = D_ALIGNUP(position + len - 1, CHUNK_SIZE);

	if (last > oh->doh_ie->ie_stat.st_size)
		return false;

	bucket = D_ALIGNUP(position + len, CHUNK_SIZE);
	bucket = (bucket / CHUNK_SIZE) - 1;

	slot = (position / K128) % 8;

	DFUSE_TRA_DEBUG(oh, "read bucket %#zx-%#zx last %#zx size %#zx bucket %ld slot %d",
			position, position + len - 1, last, ie->ie_stat.st_size, bucket, slot);

	D_SPIN_LOCK(&ie->ie_active->lock);

	d_list_for_each_entry(cd, &ie->ie_active->chunks, list)
		if (cd->bucket == bucket) {
			d_list_del(&cd->list);
			goto found;
		}

	D_ALLOC_PTR(cd);
	if (cd == NULL)
		goto err;

	cd->ia     = ie->ie_active;
	cd->bucket = bucket;
	submit     = true;

	d_list_add(&cd->list, &ie->ie_active->chunks);

found:

	for (int i = 0; i < 8; i++) {
		if (!cd->slot_done[i])
			all_done = false;
	}

	if (all_done)
		d_list_add(&cd->list, &ie->ie_active->chunks);
	else
		d_list_add_tail(&cd->list, &ie->ie_active->chunks);

	if (submit) {
		cd->reqs[0].req  = req;
		cd->reqs[0].oh   = oh;
		cd->reqs[0].slot = slot;
		cd->idx++;

		/* Now check if this read request is complete or not yet, if it isn't then just
		 * save req in the right slot however if it is then reply here.  After the call to
		 * DFUSE_REPLY_* then no reference is held on either the open file or the inode so
		 * at that point they could be closed.
		 */

		DFUSE_TRA_DEBUG(oh, "submit for bucket %ld[%d]", bucket, slot);
		D_SPIN_UNLOCK(&ie->ie_active->lock);

		rcb = chunk_fetch(req, ie, cd);
	} else if (cd->complete) {
		cd->slot_done[slot] = true;

		D_SPIN_UNLOCK(&ie->ie_active->lock);

		if (cd->rc != 0) {
			/* Don't pass fuse an error here, rather return false and
			 * the read will be tried over the network.
			 */
			rcb = false;
		} else {
			oh->doh_linear_read_pos = max(oh->doh_linear_read_pos, position + K128);

			DFUSE_IE_STAT_ADD(oh->doh_ie, DS_READ_BUCKET_M);
			DFUSE_TRA_DEBUG(oh, "%#zx-%#zx read", position, position + K128 - 1);
			DFUSE_REPLY_BUFQ(oh, req, cd->ev->de_iov.iov_buf + (slot * K128), K128);
			rcb = true;
		}

	} else if (cd->idx < MAX_REQ_COUNT) {
		DFUSE_TRA_DEBUG(oh, "Using idx %d for %ld[%d]", cd->idx, bucket, slot);

		cd->reqs[cd->idx].req  = req;
		cd->reqs[cd->idx].oh   = oh;
		cd->reqs[cd->idx].slot = slot;
		cd->idx++;
		D_SPIN_UNLOCK(&ie->ie_active->lock);
		rcb = true;
	} else {
		D_SPIN_UNLOCK(&ie->ie_active->lock);

		/* Handle concurrent reads of the same slot, this wasn't
		 * expected but can happen so for now reject it at this
		 * level and have read perform a separate I/O for this slot.
		 * TODO: Handle DAOS-16686 here.
		 */
		rcb = false;
		DFUSE_TRA_WARNING(oh, "Too many outstanding reads");
	}

	return rcb;

err:
	D_SPIN_UNLOCK(&ie->ie_active->lock);
	return false;
}

void
dfuse_cb_read(fuse_req_t req, fuse_ino_t ino, size_t len, off_t position, struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl *oh         = (struct dfuse_obj_hdl *)fi->fh;
	struct active_inode  *active     = oh->doh_ie->ie_active;
	struct dfuse_info    *dfuse_info = fuse_req_userdata(req);
	bool                  mock_read  = false;
	struct dfuse_eq      *eqt;
	int                   rc;
	struct dfuse_event   *ev;
	bool                  reached_eof = false;

	DFUSE_IE_STAT_ADD(oh->doh_ie, DS_READ);

	if (oh->doh_linear_read_eof && position == oh->doh_linear_read_pos) {
		reached_eof = true;
	} else if (active->seen_eof) {
		if (position >= active->file_size)
			reached_eof = true;
	}

	if (reached_eof) {
		DFUSE_TRA_DEBUG(oh, "Returning EOF early without round trip %#zx", position);
		oh->doh_linear_read_eof = false;
#if 0
		/* Release uses this to set the bit on the directory so do not turn it off here
		* but I do need to check why it was set before.
		*/
		oh->doh_linear_read     = false;
#endif

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
		DFUSE_IE_STAT_ADD(oh->doh_ie, DS_READ_EOF_M);
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

	if (chunk_read(req, len, position, oh))
		return;

	eqt = pick_eqt(dfuse_info);

	ev = d_slab_acquire(eqt->de_read_slab);
	if (ev == NULL) {
		DFUSE_REPLY_ERR_RAW(oh, req, ENOMEM);
		return;
	}

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

	/* Check for open matching reads, if there are multiple readers of the same file offset
	 * then chain future requests off the first one to avoid extra network round-trips.  This
	 * can and does happen even with caching enabled if there are multiple client processes.
	 */
	D_SPIN_LOCK(&active->lock);
	{
		struct dfuse_event *evc;

		d_list_for_each_entry(evc, &active->open_reads, de_read_list) {
			if (ev->de_req_position == evc->de_req_position &&
			    ev->de_req_len <= evc->de_req_len) {
				d_list_add(&ev->de_read_list, &evc->de_read_slaves);
				D_SPIN_UNLOCK(&active->lock);
				return;
			}
		}
		d_list_add_tail(&ev->de_read_list, &active->open_reads);
	}
	D_SPIN_UNLOCK(&active->lock);

	rc = dfs_read(oh->doh_dfs, oh->doh_obj, &ev->de_sgl, position, &ev->de_len, &ev->de_ev);
	if (rc != 0) {
		ev->de_ev.ev_error = rc;
		dfuse_cb_read_complete(ev);
		return;
	}

	/* Send a message to the async thread to wake it up and poll for events */
	sem_post(&eqt->de_sem);

	/* Now ensure there are more descriptors for the next request */
	d_slab_restock(eqt->de_read_slab);

	return;
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
	size_t              len = oh->doh_ie->ie_stat.st_size;

	eqt = pick_eqt(dfuse_info);
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
