/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

/* Perhaps combine with dfuse_open_handle_init? */
int
active_ie_init(struct dfuse_inode_entry *ie)
{
	uint32_t oc;
	int      rc;

	oc = atomic_fetch_add_relaxed(&ie->ie_open_count, 1);

	DFUSE_TRA_DEBUG(ie, "Addref to %d", oc + 1);

	if (oc != 0)
		return -DER_SUCCESS;

	D_ALLOC_PTR(ie->ie_active);
	if (!ie->ie_active)
		return -DER_NOMEM;

	rc = D_MUTEX_INIT(&ie->ie_active->lock, NULL);
	if (rc != -DER_SUCCESS) {
		D_FREE(ie->ie_active);
		return rc;
	}
	D_INIT_LIST_HEAD(&ie->ie_active->chunks);
	return -DER_SUCCESS;
}

static void
ah_free(struct dfuse_inode_entry *ie)
{
	D_MUTEX_DESTROY(&ie->ie_active->lock);
	D_FREE(ie->ie_active);
}

bool
active_oh_decref(struct dfuse_obj_hdl *oh)
{
	uint32_t oc;
	bool     rcb;

	oc = atomic_fetch_sub_relaxed(&oh->doh_ie->ie_open_count, 1);
	D_ASSERTF(oc >= 1, "Invalid decref from %d on %p %p", oc, oh, oh->doh_ie);

	DFUSE_TRA_DEBUG(oh->doh_ie, "Decref to %d", oc - 1);

	/* TODO: What to return here? It's probably best to set oh->doh_linear_read directly */
	if (oc != 1)
		return true;

	rcb = read_chunk_close(oh->doh_ie);

	ah_free(oh->doh_ie);
	return rcb;
}

void
active_ie_decref(struct dfuse_inode_entry *ie)
{
	uint32_t oc;

	oc = atomic_fetch_sub_relaxed(&ie->ie_open_count, 1);
	D_ASSERTF(oc >= 1, "Invalid decref from %d on %p", oc, ie);

	DFUSE_TRA_DEBUG(ie, "Decref to %d", oc - 1);

	if (oc != 1)
		return;

	ah_free(ie);
}
