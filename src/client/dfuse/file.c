/**
 * (C) Copyright 2024 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

/* Protect ie_active transitions as well as write-back errors and per-handle reporting cursors.
 * ie_open_count is updated atomically here and elsewhere.
 */
static pthread_mutex_t alock = PTHREAD_MUTEX_INITIALIZER;

uint64_t
dfuse_wb_error_sample(struct dfuse_inode_entry *ie)
{
	uint64_t seq;

	D_MUTEX_LOCK(&alock);
	seq = ie->ie_wb_error_seq;
	D_MUTEX_UNLOCK(&alock);
	return seq;
}

void
dfuse_wb_error_record(struct dfuse_inode_entry *ie, int error)
{
	D_ASSERT(error != 0);

	D_MUTEX_LOCK(&alock);
	ie->ie_wb_error = error;
	ie->ie_wb_error_seq++;
	D_MUTEX_UNLOCK(&alock);
}

int
dfuse_wb_error_check(struct dfuse_obj_hdl *oh)
{
	struct dfuse_inode_entry *ie = oh->doh_ie;
	int                       rc = 0;

	D_MUTEX_LOCK(&alock);
	if (oh->doh_wb_error_seq != ie->ie_wb_error_seq) {
		rc                   = ie->ie_wb_error;
		oh->doh_wb_error_seq = ie->ie_wb_error_seq;
	}
	D_MUTEX_UNLOCK(&alock);
	return rc;
}

/* Perhaps combine with dfuse_open_handle_init? */
int
active_ie_init(struct dfuse_inode_entry *ie, bool *preread)
{
	uint32_t oc;
	int      rc = -DER_SUCCESS;

	D_MUTEX_LOCK(&alock);

	oc = atomic_fetch_add_relaxed(&ie->ie_open_count, 1);

	DFUSE_TRA_DEBUG(ie, "Addref to %d", oc + 1);

	if (oc != 0) {
		if (preread && *preread)
			*preread = false;
		goto out;
	}

	D_ALLOC_PTR(ie->ie_active);
	if (!ie->ie_active)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = D_SPIN_INIT(&ie->ie_active->lock, 0);
	if (rc != -DER_SUCCESS) {
		D_FREE(ie->ie_active);
		goto out;
	}
	D_INIT_LIST_HEAD(&ie->ie_active->chunks);
	if (preread && *preread) {
		D_ALLOC_PTR(ie->ie_active->readahead);
		if (ie->ie_active->readahead) {
			D_INIT_LIST_HEAD(&ie->ie_active->readahead->req_list);
			atomic_fetch_add_relaxed(&ie->ie_open_count, 1);
		}
	}
	/* Take a reference on the inode to prevent it being released */
	atomic_fetch_add_relaxed(&ie->ie_ref, 1);
out:
	D_MUTEX_UNLOCK(&alock);
	return rc;
}

static void
ah_free(struct dfuse_info *dfuse_info, struct dfuse_inode_entry *ie)
{
	struct active_inode *active = ie->ie_active;

	if (active->readahead) {
		struct dfuse_event *ev;

		D_ASSERT(active->readahead->complete);
		D_ASSERT(d_list_empty(&active->readahead->req_list));

		ev = active->readahead->dra_ev;

		if (ev) {
			daos_event_fini(&ev->de_ev);
			d_slab_release(ev->de_eqt->de_pre_read_slab, ev);
		}
		D_FREE(active->readahead);
	}

	D_SPIN_DESTROY(&active->lock);
	D_FREE(ie->ie_active);
	dfuse_inode_decref(dfuse_info, ie);
}

bool
active_oh_decref(struct dfuse_info *dfuse_info, struct dfuse_obj_hdl *oh)
{
	uint32_t oc;
	bool     rcb = true;

	D_MUTEX_LOCK(&alock);

	oc = atomic_fetch_sub_relaxed(&oh->doh_ie->ie_open_count, 1);
	D_ASSERTF(oc >= 1, "Invalid decref from %d on %p %p", oc, oh, oh->doh_ie);

	DFUSE_TRA_DEBUG(oh->doh_ie, "Decref to %d", oc - 1);

	if (oc != 1)
		goto out;

	rcb = read_chunk_close(oh->doh_ie);

	ah_free(dfuse_info, oh->doh_ie);
out:
	D_MUTEX_UNLOCK(&alock);
	return rcb;
}

void
active_ie_decref(struct dfuse_info *dfuse_info, struct dfuse_inode_entry *ie)
{
	uint32_t oc;
	D_MUTEX_LOCK(&alock);

	oc = atomic_fetch_sub_relaxed(&ie->ie_open_count, 1);
	D_ASSERTF(oc >= 1, "Invalid decref from %d on %p", oc, ie);

	DFUSE_TRA_DEBUG(ie, "Decref to %d", oc - 1);

	if (oc != 1)
		goto out;

	read_chunk_close(ie);

	ah_free(dfuse_info, ie);
out:
	D_MUTEX_UNLOCK(&alock);
}

/* Reset read state after truncate, including for an inode with no open handle. */
void
active_ie_set_truncated(struct dfuse_inode_entry *ie, bool truncated)
{
	D_MUTEX_LOCK(&alock);
	if (ie->ie_active) {
		read_chunk_invalidate(ie, 0, DFS_MAX_FSIZE);
		D_SPIN_LOCK(&ie->ie_active->lock);
	}
	ie->ie_truncated = truncated;
	ie->ie_start_off = 0;
	ie->ie_end_off   = 0;
	if (ie->ie_active)
		D_SPIN_UNLOCK(&ie->ie_active->lock);
	D_MUTEX_UNLOCK(&alock);
}
