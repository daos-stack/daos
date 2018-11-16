/**
 * (C) Copyright 2017-2018 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#define D_LOGFAC	DD_FAC(vos)

#include "evt_priv.h"

/**
 * Prepare a iterator based on evtree open handle.
 * see daos_srv/evtree.h for the details.
 */
int
evt_iter_prepare(daos_handle_t toh, unsigned int options,
		 daos_epoch_range_t epr, daos_handle_t *ih)
{
	struct evt_iterator	*iter;
	struct evt_context	*tcx;
	int			 rc;

	tcx = evt_hdl2tcx(toh);
	if (tcx == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	if (options & EVT_ITER_EMBEDDED) {
		if (tcx->tc_ref != 1) {
			D_ERROR("Cannot share embedded iterator\n");
			D_GOTO(out, rc = -DER_BUSY);
		}

		iter = &tcx->tc_iter;
		evt_tcx_addref(tcx); /* +1 for caller */
		*ih = toh;
		rc = 0;

	} else {
		/* create a private context for this iterator */
		rc = evt_tcx_clone(tcx, &tcx);
		if (rc != 0)
			D_GOTO(out, rc);

		iter = &tcx->tc_iter;
		*ih = evt_tcx2hdl(tcx); /* +1 for caller */
		evt_tcx_decref(tcx); /* -1 for clone */
	}

	iter->it_state = EVT_ITER_INIT;
 out:
	return rc;
}

/**
 * Release a iterator.
 * see daos_srv/evtree.h for the details.
 */
int
evt_iter_finish(daos_handle_t ih)
{
	struct evt_iterator	*iter;
	struct evt_context	*tcx;
	int			 rc = 0;

	tcx = evt_hdl2tcx(ih);
	if (tcx == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	iter = &tcx->tc_iter;
	iter->it_state = EVT_ITER_NONE;

	evt_tcx_decref(tcx); /* -1 for prepare */
 out:
	return rc;
}

int
evt_iter_probe(daos_handle_t ih, enum evt_iter_opc opc, struct evt_rect *rect,
	       daos_anchor_t *anchor)
{
	struct evt_iterator	*iter;
	struct evt_context	*tcx;
	struct evt_rect		 rtmp;
	struct evt_entry_list	 entl;
	enum evt_find_opc	 fopc;
	int			 rc;

	tcx = evt_hdl2tcx(ih);
	if (tcx == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	iter = &tcx->tc_iter;
	if (iter->it_state < EVT_ITER_INIT)
		D_GOTO(out, rc = -DER_NO_HDL);

	memset(&rtmp, 0, sizeof(rtmp));
	switch (opc) {
	default:
		D_GOTO(out, rc = -DER_NOSYS);

	case EVT_ITER_FIRST:
		fopc = EVT_FIND_FIRST;
		/* provide an v-extent which covers everything */
		rtmp.rc_off_lo = 0;
		rtmp.rc_off_hi = ~0ULL;
		rtmp.rc_epc_lo = DAOS_EPOCH_MAX;
		break;

	case EVT_ITER_FIND:
		if (!rect && !anchor)
			D_GOTO(out, rc = -DER_INVAL);

		/* Requires the exactly same extent, we require user to
		 * start over if anything changed (clipped, aggregated).
		 */
		fopc = EVT_FIND_SAME;
		if (rect == NULL)
			rect = (struct evt_rect *)&anchor->da_buf[0];

		rtmp = *rect;
	}

	evt_ent_list_init(&entl);
	rc = evt_find_ent_list(tcx, fopc, &rtmp, &entl);
	if (rc != 0)
		D_GOTO(out, rc);

	if (evt_ent_list_empty(&entl)) {
		if (opc == EVT_ITER_FIND) /* cannot find the same extent */
			D_GOTO(out, rc = -DER_AGAIN);

		/* nothing in the tree */
		iter->it_state = EVT_ITER_FINI;
		rc = -DER_NONEXIST;
	} else {
		iter->it_state = EVT_ITER_READY;
		evt_ent_list_fini(&entl);
	}
 out:
	return rc;
}

static int
evt_iter_is_ready(struct evt_iterator *iter)
{
	D_DEBUG(DB_TRACE, "iterator state is %d\n", iter->it_state);

	switch (iter->it_state) {
	default:
		D_ASSERT(0);
	case EVT_ITER_NONE:
	case EVT_ITER_INIT:
		return -DER_NO_PERM;
	case EVT_ITER_FINI:
		return -DER_NONEXIST;
	case EVT_ITER_READY:
		return 0;
	}
}

static int
evt_iter_move(daos_handle_t ih, bool forward)
{
	struct evt_iterator	*iter;
	struct evt_context	*tcx;
	int			 rc;
	bool			 found;

	tcx = evt_hdl2tcx(ih);
	if (tcx == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	iter = &tcx->tc_iter;
	rc = evt_iter_is_ready(iter);
	if (rc != 0)
		D_GOTO(out, rc);

	found = evt_move_trace(tcx, forward, &iter->it_epr);
	if (!found) {
		iter->it_state = EVT_ITER_FINI;
		D_GOTO(out, rc = -DER_NONEXIST);
	}

	iter->it_state = EVT_ITER_READY;
 out:
	return rc;
}

/**
 * Move the iterator cursor to the next extent in the evtree.
 * See daos_srv/evtree.h for the details.
 */
int
evt_iter_next(daos_handle_t ih)
{
	return evt_iter_move(ih, true);
}

/**
 * Fetch the extent and its data address from the current iterator position.
 * See daos_srv/evtree.h for the details.
 */
int
evt_iter_fetch(daos_handle_t ih, struct evt_entry *entry,
	       daos_anchor_t *anchor)
{
	struct evt_iterator	*iter;
	struct evt_context	*tcx;
	struct evt_rect		*rect;
	struct evt_trace	*trace;
	int			 rc;

	tcx = evt_hdl2tcx(ih);
	if (tcx == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	iter = &tcx->tc_iter;
	rc = evt_iter_is_ready(iter);
	if (rc != 0)
		D_GOTO(out, rc);

	trace = &tcx->tc_trace[tcx->tc_depth - 1];
	rect  = evt_node_rect_at(tcx, trace->tr_node, trace->tr_at);

	if (entry)
		evt_fill_entry(tcx, trace->tr_node, trace->tr_at, NULL, entry);

	if (anchor) {
		struct evt_rect rtmp = *rect;

		memset(anchor, 0, sizeof(*anchor));
		memcpy(&anchor->da_buf[0], &rtmp, sizeof(rtmp));
		anchor->da_type = DAOS_ANCHOR_TYPE_HKEY;
	}
	rc = 0;
 out:
	return rc;
}
