/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(vos)

#include "evt_priv.h"

#define evt_iter_is_sorted(iter) \
	(((iter)->it_options & (EVT_ITER_VISIBLE | EVT_ITER_COVERED)) != 0)

static int
evt_validate_options(unsigned int options)
{
	if ((options & EVT_ITER_SKIP_HOLES) == 0)
		return 0;
	if (options & EVT_ITER_COVERED)
		goto error;
	if (options & EVT_ITER_VISIBLE)
		return 0;
error:
	D_ERROR("EVT_ITER_SKIP_HOLES is only valid with EVT_ITER_VISIBLE\n");
	return -DER_INVAL;
}

/**
 * Prepare a iterator based on evtree open handle.
 * see daos_srv/evtree.h for the details.
 */
int
evt_iter_prepare(daos_handle_t toh, unsigned int options,
		 const struct evt_filter *filter, daos_handle_t *ih)
{
	struct evt_iterator	*iter;
	struct evt_context	*tcx;
	int			 rc;

	rc = evt_validate_options(options);
	if (rc != 0)
		return rc;

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
	iter->it_options = options;
	iter->it_forward = true;
	if (evt_iter_is_sorted(iter))
		iter->it_forward = (options & EVT_ITER_REVERSE) == 0;
	iter->it_skip_move = 0;
	iter->it_filter.fr_ex.ex_hi = ~(0ULL);
	iter->it_filter.fr_ex.ex_lo = 0;
	iter->it_filter.fr_epr.epr_lo = 0;
	iter->it_filter.fr_epr.epr_hi = DAOS_EPOCH_MAX;
	iter->it_filter.fr_epoch = DAOS_EPOCH_MAX;
	iter->it_filter.fr_punch_epc = 0;
	iter->it_filter.fr_punch_minor_epc = 0;
	if (filter)
		iter->it_filter = *filter;
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

static int
evt_iter_probe_find(struct evt_iterator *iter, const struct evt_rect *rect)
{
	struct evt_entry_array	*enta;
	struct evt_rect		 rect2;
	int			 start;
	int			 end;
	int			 mid;
	int			 cmp = 0;

	enta = &iter->it_entries;
	start = 0;
	end = enta->ea_ent_nr - 1;

	if (start == end) {
		mid = start;
		evt_ent2rect(&rect2, evt_ent_array_get(enta, mid));
		cmp = evt_rect_cmp(rect, &rect2);
	}

	while (start != end) {
		mid = start + ((end + 1 - start) / 2);
		evt_ent2rect(&rect2, evt_ent_array_get(enta, mid));
		cmp = evt_rect_cmp(rect, &rect2);

		if (cmp == 0)
			break;
		if (cmp < 0) {
			if (end == mid) {
				mid = start;
				evt_ent2rect(&rect2,
					     evt_ent_array_get(enta, mid));
				cmp = evt_rect_cmp(rect, &rect2);
				break;
			}
			end = mid;
		} else {
			start = mid;
		}
	}

	if (cmp == 0)
		return mid;

	/* We didn't find the entry.  Find next entry instead */
	if (iter->it_forward) { /* Grab GT */
		if (cmp > 0) { /* current is GT mid, so increment */
			mid++;
			if (mid == enta->ea_ent_nr)
				return -1;
		} /* Otherwise mid is GT */
	} else { /* Grab LT */
		if (cmp < 0) /* current is LT mid, so decrement */
			mid--; /* will be -1 if out of bounds */
		/* Otherwise mid is LT */
	}

	return mid;
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

static uint32_t
evt_iter_intent(struct evt_iterator *iter)
{
	if (iter->it_options & EVT_ITER_FOR_PURGE)
		return DAOS_INTENT_PURGE;
	if (iter->it_options & EVT_ITER_FOR_MIGRATION)
		return DAOS_INTENT_MIGRATION;
	return DAOS_INTENT_DEFAULT;
}

static int
evt_iter_move(struct evt_context *tcx, struct evt_iterator *iter)
{
	struct evt_desc		*desc;
	uint32_t		 intent;
	int			 rc = 0;
	int			 rc1;
	bool			 found;

	intent = evt_iter_intent(iter);
	if (evt_iter_is_sorted(iter)) {
		for (;;) {
			struct evt_entry	*entry;

			iter->it_index +=
				iter->it_forward ? 1 : -1;
			if (iter->it_index < 0 ||
			    iter->it_index == iter->it_entries.ea_ent_nr) {
				iter->it_state = EVT_ITER_FINI;
				D_GOTO(out, rc = -DER_NONEXIST);
			}

			entry = evt_ent_array_get(&iter->it_entries,
						  iter->it_index);
			if (entry->en_avail_rc < 0)
				return entry->en_avail_rc;

			if (entry->en_avail_rc == ALB_UNAVAILABLE)
				continue;

			if (iter->it_options & EVT_ITER_SKIP_HOLES &&
			    bio_addr_is_hole(&entry->en_addr))
				continue;

			break;
		}
		goto ready;
	}

	while ((found = evt_move_trace(tcx))) {
		struct evt_trace	*trace;
		struct evt_node		*nd;
		struct evt_node_entry	*ne;
		struct evt_rect		 rect;

		trace = &tcx->tc_trace[tcx->tc_depth - 1];
		nd = evt_off2node(tcx, trace->tr_node);
		if (evt_node_is_leaf(tcx, nd)) {
			ne = evt_node_entry_at(tcx, nd, trace->tr_at);
			desc = evt_off2desc(tcx, ne->ne_child);
			rc1 = evt_desc_log_status(tcx, ne->ne_rect.rd_epc, desc,
						  intent);
			if (rc1 < 0)
				return rc1;

			if (rc1 == ALB_UNAVAILABLE)
				continue;
		}

		evt_nd_off_rect_read_at(tcx, trace->tr_node, trace->tr_at,
					&rect);
		if (evt_filter_rect(&iter->it_filter, &rect, true))
			continue;
		break;
	}

	if (!found) {
		iter->it_state = EVT_ITER_FINI;
		D_GOTO(out, rc = -DER_NONEXIST);
	}

ready:
	iter->it_state = EVT_ITER_READY;
 out:
	return rc;
}

static int
evt_iter_skip_holes(struct evt_context *tcx, struct evt_iterator *iter)
{
	struct evt_entry_array	*enta;
	struct evt_entry	*entry;

	if (iter->it_options & EVT_ITER_SKIP_HOLES) {
		enta = &iter->it_entries;
		entry = evt_ent_array_get(enta, iter->it_index);

		if (bio_addr_is_hole(&entry->en_addr))
			return evt_iter_move(tcx, iter);
	}
	return 0;
}

static int
evt_iter_probe_sorted(struct evt_context *tcx, struct evt_iterator *iter,
		      int opc, const struct evt_rect *rect,
		      const daos_anchor_t *anchor)
{
	struct evt_entry_array	*enta;
	struct evt_entry	*entry;
	struct evt_rect		 rtmp;
	uint32_t		 intent;
	int			 flags = 0;
	int			 rc = 0;
	int			 index;

	if (iter->it_options & EVT_ITER_VISIBLE)
		flags = EVT_VISIBLE;
	if (iter->it_options & EVT_ITER_COVERED)
		flags |= EVT_COVERED;

	rtmp.rc_ex.ex_lo = iter->it_filter.fr_ex.ex_lo;
	rtmp.rc_ex.ex_hi = iter->it_filter.fr_ex.ex_hi;
	rtmp.rc_epc = DAOS_EPOCH_MAX;

	enta = &iter->it_entries;
	intent = evt_iter_intent(iter);
	rc = evt_ent_array_fill(tcx, EVT_FIND_ALL, intent, &iter->it_filter,
				&rtmp, enta);
	if (rc == 0)
		rc = evt_ent_array_sort(tcx, enta, &iter->it_filter, flags);

	if (rc != 0)
		return rc;

	if (enta->ea_ent_nr == 0) {
		iter->it_state = EVT_ITER_FINI;
		return -DER_NONEXIST;
	}

	if (opc == EVT_ITER_FIRST) {
		index = iter->it_forward ? 0 : enta->ea_ent_nr - 1;
		iter->it_index = index;
		/* Mark the last entry */
		entry = evt_ent_array_get(enta, enta->ea_ent_nr - 1 - index);
		entry->en_visibility |= EVT_LAST;
		goto out;
	}

	if (opc != EVT_ITER_FIND) {
		D_ERROR("Unknown op code for evt iterator: %d\n", opc);
		return -DER_NOSYS;
	}

	rect = rect ? rect : (struct evt_rect *)&anchor->da_buf[0];
	/** If entry doesn't exist, it will return next entry */
	index = evt_iter_probe_find(iter, rect);
	if (index == -1)
		return -DER_NONEXIST;
	iter->it_index = index;
out:
	iter->it_state = EVT_ITER_READY;
	return evt_iter_skip_holes(tcx, iter);
}

static void
ent_array_reset(struct evt_context *tcx, struct evt_entry_array *ent_array)
{
	ent_array->ea_ent_nr = 0;
	ent_array->ea_inob = tcx->tc_inob;
}

int
evt_iter_probe(daos_handle_t ih, enum evt_iter_opc opc,
	       const struct evt_rect *rect, const daos_anchor_t *anchor)
{
	struct vos_iterator	*oiter = vos_hdl2iter(ih);
	struct evt_iterator	*iter;
	struct evt_context	*tcx;
	struct evt_entry_array	*enta;
	struct evt_rect		 rtmp;
	enum evt_find_opc	 fopc;
	int			 rc;

	tcx = evt_hdl2tcx(ih);
	if (tcx == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	iter = &tcx->tc_iter;
	if (iter->it_state < EVT_ITER_INIT)
		D_GOTO(out, rc = -DER_NO_HDL);

	enta = &iter->it_entries;
	ent_array_reset(tcx, enta);

	if (evt_iter_is_sorted(iter))
		return evt_iter_probe_sorted(tcx, iter, opc, rect, anchor);

	memset(&rtmp, 0, sizeof(rtmp));

	switch (opc) {
	default:
		D_GOTO(out, rc = -DER_NOSYS);
	case EVT_ITER_FIRST:
		fopc = EVT_FIND_FIRST;
		/* An extent that covers everything */
		rtmp.rc_ex.ex_lo = 0;
		rtmp.rc_ex.ex_hi = ~0ULL;
		rtmp.rc_epc = DAOS_EPOCH_MAX;
		rtmp.rc_minor_epc = EVT_MINOR_EPC_MAX;
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

	rc = evt_ent_array_fill(tcx, fopc, vos_iter_intent(oiter),
				&iter->it_filter, &rtmp, enta);
	if (rc != 0)
		D_GOTO(out, rc);

	if (enta->ea_ent_nr == 0) {
		if (opc == EVT_ITER_FIND) /* cannot find the same extent */
			D_GOTO(out, rc = -DER_AGAIN);

		/* nothing in the tree */
		iter->it_state = EVT_ITER_FINI;
		rc = -DER_NONEXIST;
	} else {
		iter->it_state = EVT_ITER_READY;
		iter->it_skip_move = 0;
	}
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
	struct evt_iterator	*iter;
	struct evt_context	*tcx;
	int			 rc;

	tcx = evt_hdl2tcx(ih);
	if (tcx == NULL)
		return -DER_NO_HDL;

	iter = &tcx->tc_iter;
	rc = evt_iter_is_ready(iter);
	if (rc != 0)
		return rc;

	if (iter->it_skip_move) {
		D_ASSERT(!evt_iter_is_sorted(iter));
		iter->it_skip_move = 0;
		return 0;
	}

	return evt_iter_move(tcx, iter);
}

int
evt_iter_empty(daos_handle_t ih)
{
	struct evt_context	*tcx;

	tcx = evt_hdl2tcx(ih);
	if (tcx == NULL)
		return -DER_NO_HDL;

	return tcx->tc_depth == 0;
}

int evt_iter_delete(daos_handle_t ih, struct evt_entry *ent)
{
	struct evt_context	*tcx;
	struct evt_iterator	*iter;
	struct evt_trace	*trace;
	struct evt_rect		 rect;
	int			 rc;
	int			 i;
	bool			 reset = false;

	tcx = evt_hdl2tcx(ih);
	if (tcx == NULL)
		return -DER_NO_HDL;

	iter = &tcx->tc_iter;

	if (evt_iter_is_sorted(iter))
		return -DER_NOSYS;

	rc = evt_iter_is_ready(iter);
	if (rc != 0)
		return rc;


	if (ent != NULL) {
		unsigned int inob;

		rc = evt_iter_fetch(ih, &inob, ent, NULL);
		if (rc)
			return rc;
	}

	trace = &tcx->tc_trace[tcx->tc_depth - 1];
	for (i = 0; i < tcx->tc_depth; i++) {
		trace->tr_tx_added = false;
		trace--;
	}

	rc = evt_tx_begin(tcx);
	if (rc != 0)
		return rc;

	rc = evt_node_delete(tcx);
	if (rc == -DER_NONEXIST) {
		rc = 0;
		reset = true;
	}

	rc = evt_tx_end(tcx, rc);

	if (rc != 0)
		goto out;

	/** Ok, now check the trace */
	if (tcx->tc_depth == 0 || reset) {
		iter->it_state = EVT_ITER_FINI;
		goto out;
	}

	iter->it_skip_move = 1;
	trace = &tcx->tc_trace[tcx->tc_depth - 1];
	evt_nd_off_rect_read_at(tcx, trace->tr_node, trace->tr_at, &rect);
	if (!evt_filter_rect(&iter->it_filter, &rect, true))
		goto out;

	D_DEBUG(DB_TRACE, "Skipping to next unfiltered entry\n");

	/* Skip to first unfiltered entry */
	evt_iter_move(tcx, iter);

out:
	return rc;
}

D_CASSERT(sizeof(((daos_anchor_t *)0)->da_buf) >= sizeof(struct evt_rect));

/**
 * Fetch the extent and its data address from the current iterator position.
 * See daos_srv/evtree.h for the details.
 */
int
evt_iter_fetch(daos_handle_t ih, unsigned int *inob, struct evt_entry *entry,
	       daos_anchor_t *anchor)
{
	struct evt_iterator	*iter;
	struct evt_context	*tcx;
	struct evt_node		*node;
	struct evt_rect		 rect;
	struct evt_trace	*trace;
	int			 rc;

	tcx = evt_hdl2tcx(ih);
	if (tcx == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	if (entry == NULL || inob == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	iter = &tcx->tc_iter;
	rc = evt_iter_is_ready(iter);
	if (rc != 0)
		D_GOTO(out, rc);

	if (evt_iter_is_sorted(iter)) {
		*entry = *evt_ent_array_get(&iter->it_entries, iter->it_index);
		evt_ent2rect(&rect, entry);
		goto set_anchor;
	}
	trace = &tcx->tc_trace[tcx->tc_depth - 1];
	node = evt_off2node(tcx, trace->tr_node);
	evt_node_rect_read_at(tcx, node, trace->tr_at, &rect);

	if (entry)
		evt_entry_fill(tcx, node, trace->tr_at, NULL,
			       evt_iter_intent(iter), entry);

	/* There is no visibility flag for unsorted entries but go ahead and
	 * set it EVT_COVERED if user has specified a punch epoch in the filter
	 */
	if (evt_entry_punched(entry, &iter->it_filter))
		entry->en_visibility = EVT_COVERED;
set_anchor:
	*inob = tcx->tc_inob;

	if (anchor) {
		memset(anchor, 0, sizeof(*anchor));
		memcpy(&anchor->da_buf[0], &rect, sizeof(rect));
		anchor->da_type = DAOS_ANCHOR_TYPE_HKEY;
	}
	rc = 0;
 out:
	return rc;
}
