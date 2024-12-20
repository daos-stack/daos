/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

/* A lock is needed here, not for ie_open_count which is updated atomcially here and elsewhere
 * but to ensure that ie_active is also atomically updated with the reference count.
 */
static pthread_mutex_t alock = PTHREAD_MUTEX_INITIALIZER;

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
	D_INIT_LIST_HEAD(&ie->ie_active->open_reads);
	atomic_init(&ie->ie_active->read_count, 0);
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

void
active_oh_decref(struct dfuse_info *dfuse_info, struct dfuse_obj_hdl *oh)
{
	uint32_t oc;

	D_MUTEX_LOCK(&alock);

	oc = atomic_fetch_sub_relaxed(&oh->doh_ie->ie_open_count, 1);
	D_ASSERTF(oc >= 1, "Invalid decref from %d on %p %p", oc, oh, oh->doh_ie);

	DFUSE_TRA_DEBUG(oh->doh_ie, "Decref to %d", oc - 1);

	/* Leave set_linear_read as false in this case */
	if (oc != 1)
		goto out;

	if (read_chunk_close(oh->doh_ie))
		oh->doh_linear_read = true;

	/* Do not set linear read in the case where there's no reads or writes, this could be
	 * simple open/close calls but it could also be cache use so leave the setting unchanged
	 * in this case.
	 */
	if (oh->doh_linear_read) {
		if (oh->doh_ie->ie_active->read_count != 0)
			oh->doh_set_linear_read = true;
	} else {
		oh->doh_set_linear_read = true;
	}

	ah_free(dfuse_info, oh->doh_ie);
out:
	D_MUTEX_UNLOCK(&alock);
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
