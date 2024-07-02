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
 * and this contains a list of read_chunk_data entries, one for each bucket.  Buckets where all
 * slots have been requested are remove from the list and closed when the last request is completed.
 *
 * TODO: Currently there is no code to remove partially read buckets from the list so reading
 * one slot every chunk would leave the entire file contents in memory until close and mean long
 * list traversal times.
 */

#define CHUNK_SIZE (1024 * 1024)

struct read_chunk_data {
	struct dfuse_event   *ev;
	fuse_req_t            reqs[8];
	struct dfuse_obj_hdl *ohs[8];
	d_list_t              list;
	uint64_t              bucket;
	struct dfuse_eq      *eqt;
	int                   rc;
	int                   entered;
	ATOMIC int            exited;
	bool                  exiting;
	bool                  complete;
};

struct read_chunk_core {
	d_list_t entries;
};

/* Global lock for all chunk read operations.  Each inode has a struct read_chunk_core * entry
 * which is checked for NULL and set whilst holding this lock.  Each read_chunk_core then has
 * a list of read_chunk_data and again, this lock protects all lists on all inodes.  This avoids
 * the need for a per-inode lock which for many files would consume considerable memory but does
 * mean there is potentially some lock contention.  The lock however is only held for list
 * manipulation, no dfs or kernel calls are made whilst holding the lock.
 */
static pthread_mutex_t rc_lock = PTHREAD_MUTEX_INITIALIZER;

static void
chunk_free(struct read_chunk_data *cd)
{
	d_list_del(&cd->list);
	d_slab_release(cd->eqt->de_read_slab, cd->ev);
	D_FREE(cd);
}

/* Called when the last open file handle on a inode is closed.  This needs to free everything which
 * is complete and for anything that isn't flag it for deletion in the callback.
 *
 * Returns true if the feature was used.
 */
bool
read_chunk_close(struct dfuse_inode_entry *ie)
{
	struct read_chunk_data *cd, *cdn;
	bool                    rcb = false;

	D_MUTEX_LOCK(&rc_lock);
	if (!ie->ie_chunk)
		goto out;

	rcb = true;

	d_list_for_each_entry_safe(cd, cdn, &ie->ie_chunk->entries, list) {
		if (cd->complete) {
			chunk_free(cd);
		} else {
			cd->exiting = true;
		}
	}
	D_FREE(ie->ie_chunk);
out:
	D_MUTEX_UNLOCK(&rc_lock);
	return rcb;
}

static void
chunk_cb(struct dfuse_event *ev)
{
	struct read_chunk_data *cd = ev->de_cd;
	fuse_req_t              req;
	bool                    done = false;

	cd->rc = ev->de_ev.ev_error;

	if (cd->rc == 0 && (ev->de_len != CHUNK_SIZE)) {
		cd->rc = EIO;
		DS_WARN(cd->rc, "Unexpected short read bucket %ld (%#zx) expected %i got %zi",
			cd->bucket, cd->bucket * CHUNK_SIZE, CHUNK_SIZE, ev->de_len);
	}

	daos_event_fini(&ev->de_ev);

	do {
		int i;
		req = 0;

		D_MUTEX_LOCK(&rc_lock);

		if (cd->exiting) {
			chunk_free(cd);
			D_MUTEX_UNLOCK(&rc_lock);
			return;
		}

		cd->complete = true;
		for (i = 0; i < 8; i++) {
			if (cd->reqs[i]) {
				req         = cd->reqs[i];
				cd->reqs[i] = 0;
				break;
			}
		}

		D_MUTEX_UNLOCK(&rc_lock);

		if (req) {
			size_t position = (cd->bucket * CHUNK_SIZE) + (i * K128);

			if (cd->rc != 0) {
				DFUSE_REPLY_ERR_RAW(cd->ohs[i], req, cd->rc);
			} else {
				DFUSE_TRA_DEBUG(cd->ohs[i], "%#zx-%#zx read", position,
						position + K128 - 1);
				DFUSE_REPLY_BUFQ(cd->ohs[i], req, ev->de_iov.iov_buf + (i * K128),
						 K128);
			}

			if (atomic_fetch_add_relaxed(&cd->exited, 1) == 7)
				done = true;
		}
	} while (req && !done);

	if (done) {
		d_slab_release(cd->eqt->de_read_slab, cd->ev);
		D_FREE(cd);
	}
}

/* Submut a read to dfs.
 *
 * Returns true on success.
 */
static bool
chunk_fetch(fuse_req_t req, struct dfuse_obj_hdl *oh, struct read_chunk_data *cd, int slot)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_inode_entry *ie         = oh->doh_ie;
	struct dfuse_event       *ev;
	struct dfuse_eq          *eqt;
	int                       rc;
	daos_off_t                position = cd->bucket * CHUNK_SIZE;

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

	cd->ev         = ev;
	cd->eqt        = eqt;
	cd->reqs[slot] = req;
	cd->ohs[slot]  = oh;

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
	struct read_chunk_core   *cc;
	struct read_chunk_data   *cd;
	off_t                     last;
	uint64_t                  bucket;
	int                       slot;
	bool                      submit = false;
	bool                      rcb;

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

	D_MUTEX_LOCK(&rc_lock);
	if (ie->ie_chunk == NULL) {
		D_ALLOC_PTR(ie->ie_chunk);
		if (ie->ie_chunk == NULL)
			goto err;
		D_INIT_LIST_HEAD(&ie->ie_chunk->entries);
	}
	cc = ie->ie_chunk;

	d_list_for_each_entry(cd, &cc->entries, list)
		if (cd->bucket == bucket) {
			/* Remove from list to re-add again later. */
			d_list_del(&cd->list);
			goto found;
		}

	D_ALLOC_PTR(cd);
	if (cd == NULL)
		goto err;

	cd->bucket = bucket;
	submit     = true;

found:

	if (++cd->entered < 8) {
		/* Put on front of list for efficient searching */
		d_list_add(&cd->list, &cc->entries);
	}

	D_MUTEX_UNLOCK(&rc_lock);

	if (submit) {
		DFUSE_TRA_DEBUG(oh, "submit for bucket %ld[%d]", bucket, slot);
		rcb = chunk_fetch(req, oh, cd, slot);
	} else {
		struct dfuse_event *ev = NULL;

		/* Now check if this read request is complete or not yet, if it isn't then just
		 * save req in the right slot however if it is then reply here.  After the call to
		 * DFUSE_REPLY_* then no reference is held on either the open file or the inode so
		 * at that point they could be closed.
		 */
		rcb = true;

		D_MUTEX_LOCK(&rc_lock);
		if (cd->complete) {
			ev = cd->ev;
		} else {
			cd->reqs[slot] = req;
			cd->ohs[slot]  = oh;
		}
		D_MUTEX_UNLOCK(&rc_lock);

		if (ev) {
			if (cd->rc != 0) {
				/* Don't pass fuse an error here, rather return false and the read
				 * will be tried over the network.
				 */
				rcb = false;
			} else {
				DFUSE_TRA_DEBUG(oh, "%#zx-%#zx read", position,
						position + K128 - 1);
				DFUSE_REPLY_BUFQ(oh, req, ev->de_iov.iov_buf + (slot * K128), K128);
			}
			if (atomic_fetch_add_relaxed(&cd->exited, 1) == 7) {
				d_slab_release(cd->eqt->de_read_slab, cd->ev);
				D_FREE(cd);
			}
		}
	}

	return rcb;

err:
	D_MUTEX_UNLOCK(&rc_lock);
	return false;
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

	if (chunk_read(req, len, position, oh))
		return;

	eqt = pick_eqt(dfuse_info);

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
