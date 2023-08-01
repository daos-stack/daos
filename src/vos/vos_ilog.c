/**
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * vos/vos_ilog.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include "vos_internal.h"

static int
vos_ilog_status_get(struct umem_instance *umm, uint32_t tx_id,
		    daos_epoch_t epoch, uint32_t intent, bool retry, void *args)
{
	int	rc;
	daos_handle_t coh;

	coh.cookie = (unsigned long)args;

	rc = vos_dtx_check_availability(coh, tx_id, epoch, intent, DTX_RT_ILOG, retry);
	if (rc < 0)
		return rc;

	switch (rc) {
	case ALB_UNAVAILABLE:
	case ALB_AVAILABLE_DIRTY:
		return ILOG_UNCOMMITTED;
	case ALB_AVAILABLE_CLEAN:
		return ILOG_COMMITTED;
	case ALB_AVAILABLE_ABORTED:
		break;
	default:
		D_ASSERTF(0, "Unexpected availability\n");
	}

	return ILOG_REMOVED;
}

static int
vos_ilog_is_same_tx(struct umem_instance *umm, uint32_t tx_id,
		    daos_epoch_t epoch, bool *same, void *args)
{
	bool			 standalone = umm->umm_pool->up_store.store_standalone;
	struct dtx_handle	*dth = vos_dth_get(standalone);
	uint32_t		 dtx = vos_dtx_get(standalone);
	daos_handle_t		 coh;

	coh.cookie = (unsigned long)args;
	*same = false;

	if (dtx_is_committed(tx_id, vos_hdl2cont(coh), epoch)) {
		/** If it's committed and the current update is not
		 * transactional, treat it as the same transaction and let the
		 * minor epoch handle any conflicts.
		 */
		if (!dtx_is_valid_handle(dth))
			*same = true;
	} else if (tx_id == dtx) {
		*same = true;
	}

	return 0;
}

static int
vos_ilog_add(struct umem_instance *umm, umem_off_t ilog_off, uint32_t *tx_id,
	     daos_epoch_t epoch, void *args)
{
	return vos_dtx_register_record(umm, ilog_off, DTX_RT_ILOG, tx_id);
}

static int
vos_ilog_del(struct umem_instance *umm, umem_off_t ilog_off, uint32_t tx_id,
	     daos_epoch_t epoch, bool deregister, void *args)
{
	daos_handle_t	coh;

	if (!deregister)
		return 0;

	coh.cookie = (unsigned long)args;
	vos_dtx_deregister_record(umm, coh, tx_id, epoch, ilog_off);
	return 0;
}

void
vos_ilog_desc_cbs_init(struct ilog_desc_cbs *cbs, daos_handle_t coh)
{
	cbs->dc_log_status_cb	= vos_ilog_status_get;
	cbs->dc_log_status_args	= (void *)(unsigned long)coh.cookie;
	cbs->dc_is_same_tx_cb = vos_ilog_is_same_tx;
	cbs->dc_is_same_tx_args = (void *)(unsigned long)coh.cookie;
	cbs->dc_log_add_cb = vos_ilog_add;
	cbs->dc_log_add_args = NULL;
	cbs->dc_log_del_cb = vos_ilog_del;
	cbs->dc_log_del_args = (void *)(unsigned long)coh.cookie;
}

/** Returns true if the entry is covered by a punch */
static inline bool
vos_ilog_punched(const struct ilog_entry *entry,
		 const struct vos_punch_record *punch)
{
	if (ilog_is_punch(entry))
		return vos_epc_punched(entry->ie_id.id_epoch,
				       entry->ie_id.id_punch_minor_eph,
				       punch);
	return vos_epc_punched(entry->ie_id.id_epoch,
			       entry->ie_id.id_update_minor_eph, punch);

}

/** Returns true if the entry is a punch and covers a punch */
static inline bool
vos_ilog_punch_covered(const struct ilog_entry *entry,
		       const struct vos_punch_record *punch)
{
	struct vos_punch_record	new_punch;

	if (!ilog_has_punch(entry))
		return false;

	new_punch.pr_epc = entry->ie_id.id_epoch;
	new_punch.pr_minor_epc = entry->ie_id.id_punch_minor_eph;

	return vos_epc_punched(punch->pr_epc, punch->pr_minor_epc, &new_punch);
}

static int
vos_parse_ilog(struct vos_ilog_info *info, const daos_epoch_range_t *epr,
	       daos_epoch_t bound, const struct vos_punch_record *punch) {
	struct ilog_entry	entry;
	struct vos_punch_record	*any_punch = &info->ii_prior_any_punch;
	daos_epoch_t		 entry_epc;

	D_ASSERT(punch->pr_epc <= epr->epr_hi);

	ilog_foreach_entry_reverse(&info->ii_entries, &entry) {
		if (entry.ie_status == ILOG_REMOVED)
			continue;

		info->ii_empty = false;

		/** If a punch epoch is passed in, and it is later than any
		 * punch in this log, treat it as a prior punch
		 */
		if (vos_ilog_punched(&entry, punch)) {
			info->ii_prior_punch = *punch;
			if (vos_epc_punched(any_punch->pr_epc,
					    any_punch->pr_minor_epc, punch))
				info->ii_prior_any_punch = *punch;
			break;
		}

		entry_epc = entry.ie_id.id_epoch;
		if (entry_epc > epr->epr_hi) {
			info->ii_full_scan = false;
			if (epr->epr_lo != 0) {
				/** If this is non-zero, we know this is used for punch check */
				D_DEBUG(DB_TRACE, "Detected ilog entries outside epoch range "
					DF_X64"-"DF_X64"\n", epr->epr_lo, epr->epr_hi);
				return 0;
			}
			if (ilog_has_punch(&entry)) {
				/** Entry is punched within uncertainty range,
				 * so restart the transaction.
				 */
				if (entry_epc <= bound)
					return -DER_TX_RESTART;

				if (entry.ie_status == ILOG_COMMITTED)
					info->ii_next_punch = entry_epc;
			} else if (entry_epc <= bound) {
				info->ii_uncertain_create = entry_epc;
			}
			continue;
		}

		if (entry.ie_status == -DER_INPROGRESS)
			return -DER_INPROGRESS;

		if (vos_ilog_punch_covered(&entry, &info->ii_prior_any_punch)) {
			info->ii_prior_any_punch.pr_epc = entry.ie_id.id_epoch;
			info->ii_prior_any_punch.pr_minor_epc =
				entry.ie_id.id_punch_minor_eph;
		}

		if (entry.ie_status == ILOG_UNCOMMITTED) {
			daos_epoch_t	epc = entry.ie_id.id_epoch;
			uint16_t	minor_epc =
				entry.ie_id.id_punch_minor_eph;

			/** Key is not visible at current entry but may be yet
			 *  visible at prior entry
			 */
			if (info->ii_uncommitted < entry.ie_id.id_epoch &&
			    epc > info->ii_create &&
			    !vos_epc_punched(epc, minor_epc,
					     &info->ii_prior_punch))
				info->ii_uncommitted = entry.ie_id.id_epoch;
			continue;
		}

		/** We we have a committed entry that exceeds uncommitted
		 *  epoch, clear the uncommitted epoch.
		 */
		if (entry.ie_id.id_epoch > info->ii_uncommitted)
			info->ii_uncommitted = 0;

		D_ASSERTF(entry.ie_status == ILOG_COMMITTED, "entry.ie_status is %d\n",
			  entry.ie_status);

		if (ilog_has_punch(&entry)) {
			info->ii_prior_punch.pr_epc = entry.ie_id.id_epoch;
			info->ii_prior_punch.pr_minor_epc =
				entry.ie_id.id_punch_minor_eph;
			if (!ilog_is_punch(&entry))
				info->ii_create = entry.ie_id.id_epoch;
			break;
		}

		info->ii_create = entry.ie_id.id_epoch;
	}

	if (epr->epr_lo != 0) {
		ilog_foreach_entry(&info->ii_entries, &entry) {
			if (entry.ie_id.id_epoch >= epr->epr_lo)
				break;

			if (entry.ie_status == ILOG_REMOVED)
				continue;

			info->ii_full_scan = false;
			D_DEBUG(DB_TRACE, "Detected ilog entries outside epoch range "DF_X64"-"
				DF_X64"\n", epr->epr_lo, epr->epr_hi);
			return 0;
		}
	}

	if (vos_epc_punched(info->ii_prior_punch.pr_epc,
			    info->ii_prior_punch.pr_minor_epc,
			    punch))
		info->ii_prior_punch = *punch;
	if (vos_epc_punched(info->ii_prior_any_punch.pr_epc,
			    info->ii_prior_any_punch.pr_minor_epc,
			    punch))
		info->ii_prior_any_punch = *punch;

	D_DEBUG(DB_TRACE, "After fetch at "DF_X64": create="DF_X64
		" prior_punch="DF_PUNCH" next_punch="DF_X64"%s\n", epr->epr_hi,
		info->ii_create, DP_PUNCH(&info->ii_prior_punch),
		info->ii_next_punch, info->ii_empty ? " is empty" : "");

	return 0;
}

static int
vos_ilog_fetch_internal(struct umem_instance *umm, daos_handle_t coh, uint32_t intent,
			struct ilog_df *ilog, const daos_epoch_range_t *epr, daos_epoch_t bound,
			bool has_cond, const struct vos_punch_record *punched,
			const struct vos_ilog_info *parent, struct vos_ilog_info *info)
{
	struct ilog_desc_cbs	 cbs;
	struct vos_punch_record	 punch = {0};
	int			 rc;

	vos_ilog_desc_cbs_init(&cbs, coh);
	rc = ilog_fetch(umm, ilog, &cbs, intent, has_cond, &info->ii_entries);
	if (rc == -DER_NONEXIST)
		goto init;
	if (rc != 0) {
		D_CDEBUG(rc == -DER_INPROGRESS, DB_IO, DLOG_ERR,
			 "Could not fetch ilog: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

init:
	info->ii_uncommitted = 0;
	info->ii_create = 0;
	info->ii_full_scan = true;
	info->ii_next_punch = 0;
	info->ii_uncertain_create = 0;
	info->ii_empty = true;
	info->ii_prior_punch.pr_epc = 0;
	info->ii_prior_punch.pr_minor_epc = 0;
	info->ii_prior_any_punch.pr_epc = 0;
	info->ii_prior_any_punch.pr_minor_epc = 0;
	if (punched != NULL)
		punch = *punched;
	if (parent != NULL) {
		info->ii_prior_any_punch = parent->ii_prior_any_punch;
		punch = parent->ii_prior_punch;
		info->ii_uncommitted = parent->ii_uncommitted;
	}

	if (rc == 0)
		rc = vos_parse_ilog(info, epr, bound, &punch);

	return rc;
}

int
vos_ilog_fetch_(struct umem_instance *umm, daos_handle_t coh, uint32_t intent, struct ilog_df *ilog,
		daos_epoch_t epoch, daos_epoch_t bound, bool has_cond,
		const struct vos_punch_record *punched,
		const struct vos_ilog_info *parent, struct vos_ilog_info *info)
{
	daos_epoch_range_t epr;

	epr.epr_lo = 0;
	epr.epr_hi = epoch;

	return vos_ilog_fetch_internal(umm, coh, intent, ilog, &epr, bound, has_cond,
				       punched, parent, info);
}

int
vos_ilog_check_(struct vos_ilog_info *info, const daos_epoch_range_t *epr_in,
		daos_epoch_range_t *epr_out, bool visible_only)
{
	if (epr_out && epr_out != epr_in)
		*epr_out = *epr_in;

	if (visible_only) {
		if (info->ii_create == 0)
			return -DER_NONEXIST;
		if (epr_out && epr_out->epr_lo < info->ii_create)
			epr_out->epr_lo = info->ii_create;
		return 0;
	}

	/* Caller wants to see punched entries so we will return 0 if either the
	 * entity is visible, has no incarnation log, or has a visible punch
	 */
	if (info->ii_empty) {
		/* mark whole thing as punched */
		info->ii_prior_punch.pr_epc = epr_in->epr_hi;
		info->ii_prior_punch.pr_minor_epc = VOS_MINOR_EPC_MAX;
		return 0;
	}

	if (info->ii_create == 0) {
		if (info->ii_prior_punch.pr_epc == 0)
			return -DER_NONEXIST;
		/* Punch isn't in range so ignore it */
		if (info->ii_prior_punch.pr_epc < epr_in->epr_lo)
			return -DER_NONEXIST;
		return 0;
	}

	/* Ok, entity exists.  Punch fields will be set appropriately so caller
	 * can interpret them.
	 */

	return 0;
}

static inline int
vos_ilog_update_check(struct vos_ilog_info *info, const daos_epoch_range_t *epr)
{
	if (info->ii_create <= info->ii_prior_any_punch.pr_epc)
		return -DER_NONEXIST;

	return 0;
}

int vos_ilog_update_(struct vos_container *cont, struct ilog_df *ilog,
		     const daos_epoch_range_t *epr, daos_epoch_t bound,
		     struct vos_ilog_info *parent, struct vos_ilog_info *info,
		     uint32_t cond, struct vos_ts_set *ts_set)
{
	struct dtx_handle	*dth = vos_dth_get(cont->vc_pool->vp_sysdb);
	daos_epoch_range_t	 max_epr = *epr;
	struct ilog_desc_cbs	 cbs;
	daos_handle_t		 loh;
	bool			 has_cond;
	int			 rc;

	if (parent != NULL) {
		D_ASSERT(parent->ii_prior_any_punch.pr_epc >=
			 parent->ii_prior_punch.pr_epc);

		if (parent->ii_prior_any_punch.pr_epc > max_epr.epr_lo)
			max_epr.epr_lo = parent->ii_prior_any_punch.pr_epc;
	}

	D_DEBUG(DB_TRACE, "Checking and updating incarnation log in range "
		DF_X64"-"DF_X64"\n", max_epr.epr_lo, max_epr.epr_hi);

	has_cond = cond == VOS_ILOG_COND_UPDATE || cond == VOS_ILOG_COND_INSERT;

	/** Do a fetch first.  The log may already exist */
	rc = vos_ilog_fetch(vos_cont2umm(cont), vos_cont2hdl(cont), DAOS_INTENT_UPDATE,
			    ilog, epr->epr_hi, bound, has_cond, NULL, parent, info);
	/** For now, if the state isn't settled, just retry with later timestamp. The state
	 *  should get settled quickly due to commit on share
	 */
	if (has_cond && info->ii_uncommitted)
		D_GOTO(done, rc = -DER_INPROGRESS);
	if (rc == -DER_TX_RESTART)
		goto done;
	if (rc == -DER_NONEXIST)
		goto update;
	if (rc != 0) {
		goto done;
	}

	rc = vos_ilog_update_check(info, &max_epr);
	if (rc == 0) {
		if (cond == VOS_ILOG_COND_INSERT)
			D_GOTO(done, rc = -DER_EXIST);
		goto done;
	}
	if (rc != -DER_NONEXIST) {
		D_ERROR("Check failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}
update:
	if (has_cond && rc == -DER_NONEXIST) {
		/* There is an uncertain create, so restart */
		if (info->ii_uncertain_create != 0)
			D_GOTO(done, rc = -DER_TX_RESTART);
		if (cond == VOS_ILOG_COND_UPDATE)
			D_GOTO(done, rc = -DER_NONEXIST);
	}

	vos_ilog_desc_cbs_init(&cbs, vos_cont2hdl(cont));
	rc = ilog_open(vos_cont2umm(cont), ilog, &cbs, &loh);
	if (rc != 0) {
		D_ERROR("Could not open incarnation log: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	rc = ilog_update(loh, &max_epr, epr->epr_hi, dtx_is_valid_handle(dth) ?
			 dth->dth_op_seq : VOS_SUB_OP_MAX, false);

	ilog_close(loh);

	if (rc == -DER_ALREADY && (dth == NULL || !dth->dth_already)) /* operation had no effect */
		rc = 0;
done:
	VOS_TX_LOG_FAIL(rc, "Could not update ilog %p at "DF_X64": "DF_RC"\n",
			ilog, epr->epr_hi, DP_RC(rc));

	/* No need to refetch the log.  The only field that is used by update
	 * is prior_any_punch.   This field will not be changed by ilog_update
	 * for the purpose of parsing the child log.
	 */

	return rc;
}

int
vos_ilog_punch_(struct vos_container *cont, struct ilog_df *ilog,
		const daos_epoch_range_t *epr, daos_epoch_t bound,
		struct vos_ilog_info *parent, struct vos_ilog_info *info,
		struct vos_ts_set *ts_set, bool leaf, bool replay)
{
	struct dtx_handle	*dth = vos_dth_get(cont->vc_pool->vp_sysdb);
	daos_epoch_range_t	 max_epr = *epr;
	struct ilog_desc_cbs	 cbs;
	daos_handle_t		 loh;
	int			 rc;
	uint16_t		 minor_epc = VOS_SUB_OP_MAX;
	bool			 has_cond;

	if (parent != NULL) {
		D_ASSERT(parent->ii_prior_any_punch.pr_epc >=
			 parent->ii_prior_punch.pr_epc);

		if (parent->ii_prior_any_punch.pr_epc > max_epr.epr_lo)
			max_epr.epr_lo = parent->ii_prior_any_punch.pr_epc;
	}

	if (ts_set != NULL && ts_set->ts_flags & VOS_OF_COND_PUNCH)
		has_cond = true;
	else
		has_cond = false;

	D_DEBUG(DB_TRACE, "Checking existence of incarnation log in range "
		DF_X64"-"DF_X64"\n", max_epr.epr_lo, max_epr.epr_hi);

	/** Do a fetch first.  The log may already exist */
	rc = vos_ilog_fetch(vos_cont2umm(cont), vos_cont2hdl(cont), DAOS_INTENT_PUNCH,
			    ilog, epr->epr_hi, bound, has_cond, NULL, parent, info);

	if (rc == -DER_TX_RESTART || info->ii_uncertain_create)
		return -DER_TX_RESTART;

	if (!has_cond) {
		if (leaf)
			goto punch_log;
		return 0;
	}

	/** If we get here, we need to check if the entry exists */
	D_ASSERT(ts_set->ts_flags & VOS_OF_COND_PUNCH);
	/** For now, if the state isn't settled, just retry with later
	 *  timestamp.   The state should get settled quickly when there
	 *  is conditional update and sharing.
	 */
	if (info->ii_uncommitted != 0)
		return -DER_INPROGRESS;

	if (rc == -DER_NONEXIST)
		return -DER_NONEXIST;
	if (rc != 0) {
		D_ERROR("Could not update ilog %p at "DF_X64": "DF_RC"\n",
			ilog, epr->epr_hi, DP_RC(rc));
		return rc;
	}

	rc = vos_ilog_update_check(info, &max_epr);
	if (rc == -DER_NONEXIST)
		return -DER_NONEXIST;
	if (rc != 0) {
		D_ERROR("Check failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}
	if (!leaf)
		return 0;

punch_log:
	vos_ilog_desc_cbs_init(&cbs, vos_cont2hdl(cont));
	rc = ilog_open(vos_cont2umm(cont), ilog, &cbs, &loh);
	if (rc != 0) {
		D_ERROR("Could not open incarnation log: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (dth) {
		minor_epc = dth->dth_op_seq;
	} else if (replay) {
		/* If it's a replay, punch lower than the max in case there
		 * are later visible updates with same major epoch.
		 */
		minor_epc--;
	}
	rc = ilog_update(loh, NULL, epr->epr_hi, minor_epc, true);

	ilog_close(loh);

	if (rc == -DER_ALREADY && (dth == NULL || !dth->dth_already)) /* operation had no effect */
		rc = 0;
	VOS_TX_LOG_FAIL(rc, "Could not update incarnation log: "DF_RC"\n",
			DP_RC(rc));

	return rc;
}

int
vos_ilog_aggregate(daos_handle_t coh, struct ilog_df *ilog, const daos_epoch_range_t *epr,
		   bool discard, bool uncommitted_only, const struct vos_punch_record *parent_punch,
		   struct vos_ilog_info *info)
{
	struct vos_container	*cont = vos_hdl2cont(coh);
	struct umem_instance	*umm = vos_cont2umm(cont);
	struct ilog_desc_cbs	 cbs;
	struct vos_punch_record	 punch_rec = {0, 0};
	int			 rc;

	if (parent_punch)
		punch_rec = *parent_punch;

	vos_ilog_desc_cbs_init(&cbs, coh);
	D_DEBUG(DB_TRACE, "log="DF_X64"\n", umem_ptr2off(umm, ilog));

	rc = ilog_aggregate(umm, ilog, &cbs, epr, discard, uncommitted_only, punch_rec.pr_epc,
			    punch_rec.pr_minor_epc, &info->ii_entries);

	if (rc != 0)
		return rc;

	return vos_ilog_fetch(umm, coh, DAOS_INTENT_PURGE, ilog, epr->epr_hi, 0, false,
			      &punch_rec, NULL, info);
}

bool
vos_ilog_is_punched(daos_handle_t coh, struct ilog_df *ilog, const daos_epoch_range_t *epr,
		    const struct vos_punch_record *parent_punch, struct vos_ilog_info *info)
{
	struct vos_container	*cont = vos_hdl2cont(coh);
	struct umem_instance	*umm = vos_cont2umm(cont);
	struct vos_punch_record	 punch_rec = {0, 0};
	int			 rc;

	if (parent_punch)
		punch_rec = *parent_punch;

	rc = vos_ilog_fetch_internal(umm, coh, DAOS_INTENT_PURGE, ilog, epr, 0, false,
				     &punch_rec, NULL, info);

	if (rc != 0 || !info->ii_full_scan || info->ii_create != 0 || info->ii_uncommitted != 0)
		return false;

	return true;
}

void
vos_ilog_fetch_init(struct vos_ilog_info *info)
{
	memset(info, 0, sizeof(*info));
	ilog_fetch_init(&info->ii_entries);
}

void
vos_ilog_fetch_move(struct vos_ilog_info *dest, struct vos_ilog_info *src)
{
	memcpy(dest, src, sizeof(*dest));
	ilog_fetch_move(&dest->ii_entries, &src->ii_entries);
}

/** Finalize incarnation log information */
void
vos_ilog_fetch_finish(struct vos_ilog_info *info)
{
	ilog_fetch_finish(&info->ii_entries);
}

int
vos_ilog_init(void)
{
	int	rc;

	rc = ilog_init();
	if (rc != 0) {
		D_ERROR("Failed to initialize incarnation log globals\n");
		return rc;
	}

	return 0;
}

int
vos_ilog_ts_add(struct vos_ts_set *ts_set, struct ilog_df *ilog,
		const void *record, daos_size_t rec_size)
{
	uint32_t	*idx = NULL;

	if (!vos_ts_in_tx(ts_set))
		return 0;

	if (ilog != NULL)
		idx = ilog_ts_idx_get(ilog);

	return vos_ts_set_add(ts_set, idx, record, rec_size);
}

void
vos_ilog_ts_mark(struct vos_ts_set *ts_set, struct ilog_df *ilog)
{
	uint32_t		*idx = ilog_ts_idx_get(ilog);

	vos_ts_set_mark_entry(ts_set, idx);
}

void
vos_ilog_ts_evict(struct ilog_df *ilog, uint32_t type, bool standalone)
{
	uint32_t	*idx;

	idx = ilog_ts_idx_get(ilog);

	return vos_ts_evict(idx, type, standalone);
}

void
vos_ilog_last_update(struct ilog_df *ilog, uint32_t type, daos_epoch_t *epc, bool standalone)
{
	struct vos_ts_entry	*se_entry = NULL;
	struct vos_wts_cache	*wcache;
	uint32_t		*idx;
	bool			 found;

	D_ASSERT(ilog != NULL);
	D_ASSERT(epc != NULL);
	idx = ilog_ts_idx_get(ilog);

	found = vos_ts_peek_entry(idx, type, &se_entry, standalone);
	if (found) {
		D_ASSERT(se_entry != NULL);
		wcache = &se_entry->te_w_cache;

		if (wcache->wc_ts_w[wcache->wc_w_high] != 0) {
			*epc = wcache->wc_ts_w[wcache->wc_w_high];
			return;
		}
		/* Not enough history */
	}

	/* Return EPOCH_MAX as last update timestamp on cache miss */
	*epc = DAOS_EPOCH_MAX;
}
