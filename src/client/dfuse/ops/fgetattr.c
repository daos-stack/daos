/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

static void
dfuse_cb_getattr_cb(struct dfuse_event *ev)
{
	struct dfuse_inode_entry *ie = ev->de_ie;

	if (ev->de_ev.ev_error != 0) {
		DFUSE_REPLY_ERR_RAW(ie, ev->de_req, ev->de_ev.ev_error);
		D_GOTO(release, 0);
	}

	ev->de_attr.st_ino = ie->ie_stat.st_ino;

	ie->ie_stat = ev->de_attr;

	if ((ie->ie_dfs->dfc_data_timeout != 0) &&
	    (dfuse_dcache_get_valid(ie, ie->ie_dfs->dfc_data_timeout))) {
		/* This tries to match the code in fuse_change_attributes in fs/fuse/inode.c,
		 * if the mtime or the size has changed then drop the data cache.
		 */
		if ((ie->ie_stat.st_size != ie->ie_dc.stat.st_size) ||
		    (d_timediff_ns(&ie->ie_stat.st_mtim, &ie->ie_dc.stat.st_mtim) != 0)) {
			DFUSE_TRA_INFO(ie, "Invalidating data cache");
			dfuse_dcache_evict(ie);
		}
	}

	DFUSE_REPLY_ATTR(ev->de_ie, ev->de_req, &ev->de_attr);
release:
	daos_event_fini(&ev->de_ev);
	D_FREE(ev);
}

void
dfuse_cb_getattr(fuse_req_t req, struct dfuse_inode_entry *ie)
{
	struct dfuse_info  *dfuse_info = fuse_req_userdata(req);
	struct dfuse_event *ev;
	uint64_t            eqt_idx;
	struct dfuse_eq    *eqt;
	int                 rc;

	if (ie->ie_unlinked) {
		DFUSE_TRA_DEBUG(ie, "File is unlinked, returning most recent data");
		DFUSE_REPLY_ATTR(ie, req, &ie->ie_stat);
		return;
	}

	eqt_idx = atomic_fetch_add_relaxed(&dfuse_info->di_eqt_idx, 1);
	eqt     = &dfuse_info->di_eqt[eqt_idx % dfuse_info->di_eq_count];
	D_ALLOC_PTR(ev);
	if (ev == NULL)
		D_GOTO(err, rc = ENOMEM);

	ev->de_req         = req;
	ev->de_complete_cb = dfuse_cb_getattr_cb;
	ev->de_ie          = ie;

	rc = daos_event_init(&ev->de_ev, eqt->de_eq, NULL);
	if (rc != -DER_SUCCESS)
		D_GOTO(ev, rc = daos_der2errno(rc));

	rc = dfs_ostatx(ie->ie_dfs->dfs_ns, ie->ie_obj, &ev->de_attr, &ev->de_ev);
	if (rc != 0)
		D_GOTO(ev, rc);

	sem_post(&eqt->de_sem);

	return;
ev:
	D_FREE(ev);
err:
	DFUSE_REPLY_ERR_RAW(ie, req, rc);
}
