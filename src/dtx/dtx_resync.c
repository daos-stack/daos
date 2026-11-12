/**
 * (C) Copyright 2019-2024 Intel Corporation.
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dtx: resync DTX status
 */
#define D_LOGFAC	DD_FAC(dtx)

#include <daos/placement.h>
#include <daos/pool_map.h>
#include <daos_srv/vos.h>
#include <daos_srv/pool.h>
#include <daos_srv/dtx_srv.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/container.h>
#include <abt.h>
#include "dtx_internal.h"

#define dre_xid		dre_dte.dte_xid

struct dtx_resync_head {
	d_list_t drh_list;
	d_list_t drh_pending;
	int      drh_last_err;
};

struct dtx_resync_args {
	struct ds_cont_child  *cont;
	struct dtx_resync_head tables;
	daos_epoch_t           epoch;
	uint32_t               resync_version;
	uint32_t               discard_version;
};

static void
dtx_resync_commit(struct ds_cont_child *cont, struct dtx_entry **dtes, struct dtx_resync_head *drh,
		  int count)
{
	struct dtx_resync_entry *dre;
	struct dtx_resync_entry *next;
	int                      rc = 0;
	int                      i;

	D_ASSERT(count <= DTX_THRESHOLD_COUNT);

	i = 0;
	d_list_for_each_entry(dre, &drh->drh_list, dre_link) {
		dtes[i] = &dre->dre_dte;
		if (++i >= count)
			break;
	}

	rc = dtx_commit(cont, dtes, NULL, count, false);
	if (rc < 0)
		D_ERROR("Failed to commit for DTX resync: " DF_RC "\n", DP_RC(rc));

	i = 0;
	d_list_for_each_entry_safe(dre, next, &drh->drh_list, dre_link) {
		if (rc < 0) {
			dre->dre_err = rc;
			d_list_del(&dre->dre_link);
			d_list_add_tail(&dre->dre_link, &drh->drh_pending);
		} else {
			dtx_dre_release(dre);
		}

		if (++i >= count)
			break;
	}
}

static int
dtx_is_leader(struct ds_pool *pool, struct dtx_resync_args *dra,
	      struct dtx_resync_entry *dre)
{
	struct dtx_memberships	*mbs = dre->dre_dte.dte_mbs;
	struct pool_target	*target = NULL;
	int			rc;

	if (mbs == NULL)
		return 1;

	rc = dtx_leader_get(pool, mbs, &dre->dre_oid, dre->dre_dte.dte_ver, &target);
	if (rc < 0)
		D_GOTO(out, rc);

	if (dss_self_rank() != target->ta_comp.co_rank ||
	    dss_get_module_info()->dmi_tgt_id != target->ta_comp.co_index)
		return 0;

	return 1;

out:
	D_WARN(DF_UOID" failed to get new leader: "DF_RC"\n", DP_UOID(dre->dre_oid), DP_RC(rc));
	return rc;
}

static int
dtx_verify_groups(struct ds_pool *pool, struct dtx_memberships *mbs,
		  struct dtx_id *xid, int *tgt_array)
{
	struct dtx_redundancy_group	*group;
	int				 i, j, k;
	int				 rc;
	bool				 rdonly = true;

	group = (void *)mbs->dm_data +
		sizeof(struct dtx_daos_target) * mbs->dm_tgt_cnt;

	for (i = 0; i < mbs->dm_grp_cnt; i++) {
		if (!(group->drg_flags & DGF_RDONLY))
			rdonly = false;

		for (j = 0, k = 0; j < group->drg_tgt_cnt; j++) {
			if (tgt_array != NULL &&
			    tgt_array[group->drg_ids[j]] > 0)
				continue;

			if (tgt_array != NULL &&
			    tgt_array[group->drg_ids[j]] < 0) {
				k++;
				continue;
			}

			rc = ds_pool_target_status_check(pool, group->drg_ids[j],
							 (uint8_t)PO_COMP_ST_UPIN, NULL);
			if (rc < 0)
				return rc;

			if (rc > 0) {
				if (tgt_array != NULL)
					tgt_array[group->drg_ids[j]] = 1;
			} else {
				if (tgt_array != NULL)
					tgt_array[group->drg_ids[j]] = -1;
				k++;
			}
		}

		/* For read only TX, if some redundancy group totally lost,
		 * we still can make the commit/abort decision based on the
		 * others. Although the decision may be different from the
		 * original case, it will not correctness issue.
		 */
		if (k >= group->drg_redundancy && !rdonly) {
			D_WARN("The DTX "DF_DTI" has %d redundancy group, "
			       "the No.%d lost too many members %d/%d/%d, "
			       "cannot recover such DTX.\n",
			       DP_DTI(xid), mbs->dm_grp_cnt, i,
			       group->drg_tgt_cnt, group->drg_redundancy, k);
			return 0;
		}

		group = (void *)group + sizeof(*group) +
			sizeof(uint32_t) * group->drg_tgt_cnt;
	}

	return 1;
}

static int
dtx_resync_local_check(struct ds_cont_child *cont, struct dtx_id *xid, uint32_t version,
		       uint32_t intent)
{
	int rc;

	rc = vos_dtx_check(cont->sc_hdl, xid, NULL, &version, NULL, intent);
	if (rc == -DER_NONEXIST)
		return DSHR_IGNORE;

	if (rc == -DER_INPROGRESS)
		/*
		 * For resync case, "-DER_INPROGRESS" means that current DTX is being processed:
		 * committing, aborting or preparing. Anyway, DTX resync will ignore it to avoid
		 * conflict.
		 */
		return DSHR_IGNORE;

	if (rc == DTX_ST_PREPARED)
		return 0;

	return rc < 0 ? rc : DSHR_IGNORE;
}

static int
dtx_status_handle_one(struct ds_cont_child *cont, struct dtx_resync_head *drh,
		      struct dtx_resync_entry *dre, int *tgt_array, int *err)
{
	struct dtx_entry       *dte     = &dre->dre_dte;
	struct dtx_memberships *mbs     = dte->dte_mbs;
	struct dtx_coll_entry  *dce     = NULL;
	daos_epoch_t            epoch   = dre->dre_epoch;
	uint32_t                version = cont->sc_pool->spc_map_version;
	int                     rc      = 0;

	/*
	 * Current DTX may has been committed or aborted during checking other DTX entries.
	 * So check it locally before senting out CHECK RPCs to remote targets. Similarly,
	 * double check locally after CHECK RPC replied.
	 */
	rc = dtx_resync_local_check(cont, &dte->dte_xid, version, DCI_RESYNC_LEADER);
	if (rc != 0)
		goto out;

	if (mbs->dm_flags & DMF_COLL_TARGET) {
		rc = dtx_coll_prep(cont->sc_pool_uuid, dre->dre_oid, &dte->dte_xid, mbs,
				   dss_get_module_info()->dmi_tgt_id, dte->dte_ver, version, true,
				   true, &dce);
		if (rc != 0) {
			D_ERROR("Failed to prepare the bitmap (and hints) for collective DTX "
				DF_DTI": "DF_RC"\n", DP_DTI(&dte->dte_xid), DP_RC(rc));
			goto out;
		}

		rc = dtx_coll_check(cont, dce, epoch, version);
	} else {
		rc = dtx_check(cont, dte, epoch, version);
	}

	switch (rc) {
	case DTX_ST_COMMITTED:
	case DTX_ST_COMMITTABLE:
		/* The DTX has been committed on some remote replica(s),
		 * let's commit the DTX globally.
		 */
		D_GOTO(out, rc = DSHR_NEED_COMMIT);
	case -DER_OOG:
	case -DER_HG:
		D_WARN("Need retry resync for DTX " DF_DTI " because of " DF_RC "\n",
		       DP_DTI(&dte->dte_xid), DP_RC(rc));

		/* If repeatedly hit the same network failure, let's wait for a while before retry. */
		if (drh->drh_last_err == rc)
			dss_sleep(50);

		/* Fall through. */
	case -DER_INPROGRESS:
	case -DER_TIMEDOUT:
		D_GOTO(out, rc = DSHR_NEED_RETRY);
	case DTX_ST_PREPARED: {
		/* If the transaction across multiple redundancy groups,
		 * need to check whether there are enough alive targets.
		 */
		if (mbs->dm_grp_cnt > 1) {
			rc = dtx_verify_groups(cont->sc_pool->spc_pool, mbs,
					       &dte->dte_xid, tgt_array);
			if (rc < 0)
				goto out;

			if (rc > 0)
				D_GOTO(out, rc = DSHR_NEED_COMMIT);

			/* XXX: For the distributed transaction that lose too
			 *	many particiants (the whole redundancy group),
			 *	it's difficult to make decision whether commit
			 *	or abort the DTX. we need more human knowledge
			 *	to manually recover related things.
			 *
			 *	Then we mark the TX as corrupted via special
			 *	dtx_abort() with 0 @epoch.
			 */
			if (mbs->dm_flags & DMF_COLL_TARGET)
				rc = dtx_coll_abort(cont, dce, 0, 0);
			else
				rc = dtx_abort(cont, dte, 0, 0);
			if (rc < 0 && err != NULL)
				*err = rc;

			D_GOTO(out, rc = DSHR_CORRUPT);
		}

		D_GOTO(out, rc = DSHR_NEED_COMMIT);
	}
	case -DER_NONEXIST:
		/* Someone (the DTX owner or batched commit ULT) may have
		 * committed or aborted the DTX during we handling other
		 * DTXs. So double check the status before next action.
		 */
		rc = dtx_resync_local_check(cont, &dte->dte_xid, version, DCI_RESYNC_ABORT);
		if (rc != 0)
			goto out;

		/* To be aborted. It is possible that the client has resent
		 * related RPC to the new leader, but such DTX is still not
		 * committable yet. Here, the resync logic will abort it by
		 * race during the new leader waiting for other replica(s).
		 *
		 * So when the leader get replies from other replicas, it
		 * needs to check whether local DTX is still valid or not.
		 *
		 * If we abort multiple non-ready DTXs together, then there
		 * is race that one DTX may become committable when we abort
		 * some other DTX(s). To avoid complex rollback logic, let's
		 * abort the DTXs one by one, not batched.
		 */
		if (mbs->dm_flags & DMF_COLL_TARGET)
			rc = dtx_coll_abort(cont, dce, epoch, version);
		else
			rc = dtx_abort(cont, dte, epoch, version);

		DL_CDEBUG(rc != 0, DLOG_ERR, DB_TRACE, rc,
			  "As new leader for DTX " DF_DTI ", abort it (2)", DP_DTI(&dte->dte_xid));

		if (rc < 0) {
			if (err != NULL)
				*err = rc;

			D_GOTO(out, rc = DSHR_ABORT_FAILED);
		}

		D_GOTO(out, rc = DSHR_IGNORE);
	default:
		D_WARN("Not sure about whether the DTX "DF_DTI
		       " can be committed or not: %d, skip it.\n",
		       DP_DTI(&dte->dte_xid), rc);
		if (rc > 0)
			rc = -DER_IO;
		break;
	}

out:
	drh->drh_last_err = rc;

	if (rc == DSHR_NEED_COMMIT) {
		/*
		 * IO handler for resent may have changed the DTX by race, re-check it locally.
		 * If current DTX has already been committed or be marked as committable by IO
		 * handler locally, then DTX resync logic will skip it.
		 */
		rc = dtx_resync_local_check(cont, &dte->dte_xid, version, DCI_RESYNC_COMMIT);
		if (rc == 0) {
			rc = DSHR_NEED_COMMIT;
			if (dce != NULL)
				dre->dre_dce = dtx_coll_entry_get(dce);
		}
	}

	dtx_coll_entry_put(dce);
	return rc;
}

int
dtx_handle_resync_pending(struct ds_cont_child *cont, d_list_t *head, uint32_t seq,
			  struct sched_request *sr)
{
	struct dtx_resync_entry *dre;
	struct dtx_resync_entry *next;
	struct dtx_entry        *dte;
	int                      rc;

	d_list_for_each_entry_safe(dre, next, head, dre_link) {
		if (dre->dre_seq == seq)
			break;

		dre->dre_seq = seq;

		if (dre->dre_dce != NULL) {
			rc = dtx_coll_commit(cont, dre->dre_dce, NULL, false);
		} else {
			/*
			 * Some DTX entries in the resync pending list may have been committed
			 * successfully, but not easy to distinguish them when added them into
			 * the pending list. Let's re-commit one by one to filter out them. It
			 * maybe inefficient, but consider that the case of DTX commit failure
			 * during DTX resync is very rare, then it is no matter.
			 */
			dte = &dre->dre_dte;
			rc  = dtx_commit(cont, &dte, NULL, 1, false);
		}

		if (rc == 0) {
			dtx_dre_release(dre);
		} else {
			/*
			 * If failed to commit, move it to the tail of cont::sc_dtx_resync_pending
			 * and re-commit next time via batched commit logic.
			 */
			dre->dre_err = rc;
			d_list_del(&dre->dre_link);
			d_list_add_tail(&dre->dre_link, &cont->sc_dtx_resync_pending);
		}

		if (sr != NULL && dss_ult_exiting(sr))
			return -DER_SHUTDOWN;
	}

	return 0;
}

static int
dtx_status_handle(struct dtx_resync_args *dra)
{
	struct dtx_resync_entry *dre;
	struct dtx_resync_entry *next;
	struct dtx_memberships  *mbs;
	struct ds_cont_child    *cont      = dra->cont;
	struct dtx_resync_head  *drh       = &dra->tables;
	struct ds_pool          *pool      = cont->sc_pool->spc_pool;
	struct dtx_entry       **dtes      = NULL;
	int                     *tgt_array = NULL;
	int                      tgt_cnt;
	int                      count = 0;
	int                      err   = 0;
	int                      rc;

	if (d_list_empty(&drh->drh_list))
		goto out;

	D_ALLOC_ARRAY(dtes, DTX_THRESHOLD_COUNT);
	if (dtes == NULL)
		D_GOTO(out, err = -DER_NOMEM);

	ABT_rwlock_rdlock(pool->sp_lock);
	tgt_cnt = pool_map_target_nr(pool->sp_map);
	ABT_rwlock_unlock(pool->sp_lock);
	D_ASSERT(tgt_cnt != 0);

	D_ALLOC_ARRAY(tgt_array, tgt_cnt);
	if (tgt_array == NULL)
		D_GOTO(out, err = -DER_NOMEM);

again:
	d_list_for_each_entry_safe(dre, next, &drh->drh_list, dre_link) {
		if (dre->dre_dte.dte_ver < dra->discard_version) {
			/* Use the latest known version to abort DTX locally to avoid rejection. */
			err = vos_dtx_abort(cont->sc_hdl, &dre->dre_xid, dre->dre_epoch,
					    cont->sc_pool->spc_map_version);
			if (err == -DER_NONEXIST)
				err = 0;
			if (err != 0)
				D_ERROR("Failed to discard stale DTX "DF_DTI" with ver %d/%d: "
					DF_RC"\n", DP_DTI(&dre->dre_xid), dre->dre_dte.dte_ver,
					dra->discard_version, DP_RC(err));
			dtx_dre_release(dre);
			continue;
		}

		if (dre->dre_dte.dte_mbs == NULL) {
			rc = vos_dtx_load_mbs(cont->sc_hdl, &dre->dre_xid, NULL,
					      &dre->dre_dte.dte_mbs);
			if (rc != 0) {
				if (rc < 0 && rc != -DER_NONEXIST)
					D_WARN("Failed to load mbs, do not know the leader for DTX "
					       DF_DTI" (ver = %u/%u/%u): rc = %d, skip it.\n",
					       DP_DTI(&dre->dre_xid), dra->resync_version,
					       dra->discard_version, dre->dre_dte.dte_ver, rc);
				dtx_dre_release(dre);
				continue;
			}
		}

		mbs = dre->dre_dte.dte_mbs;
		D_ASSERT(mbs->dm_tgt_cnt > 0);

		if (mbs->dm_dte_flags & DTE_PARTIAL_COMMITTED)
			goto commit;

		rc = dtx_is_leader(pool, dra, dre);
		if (rc <= 0) {
			if (rc < 0)
				D_WARN("Not sure about the leader for the DTX "
				       DF_DTI" (ver = %u/%u/%u): rc = %d, skip it.\n",
				       DP_DTI(&dre->dre_xid), dra->resync_version,
				       dra->discard_version, dre->dre_dte.dte_ver, rc);
			else
				D_DEBUG(DB_TRACE, "Not the leader for the DTX "
					DF_DTI" (ver = %u/%u/%u) skip it.\n",
					DP_DTI(&dre->dre_xid), dra->resync_version,
					dra->discard_version, dre->dre_dte.dte_ver);
			dtx_dre_release(dre);
			continue;
		}

		rc = dtx_status_handle_one(cont, drh, dre, tgt_array, &err);

		if (unlikely(cont->sc_stopping))
			D_GOTO(out, err = -DER_CANCELED);

		switch (rc) {
		case DSHR_NEED_COMMIT:
			if (mbs->dm_flags & DMF_COLL_TARGET) {
				rc = dtx_coll_commit(cont, dre->dre_dce, NULL, false);
				if (rc != 0) {
					dre->dre_err = rc;
					d_list_del(&dre->dre_link);
					d_list_add_tail(&dre->dre_link, &drh->drh_pending);
				} else {
					dtx_dre_release(dre);
				}
				continue;
			}

			goto commit;
		case DSHR_NEED_RETRY:
			d_list_del(&dre->dre_link);
			d_list_add_tail(&dre->dre_link, &drh->drh_list);
			continue;
		case DSHR_IGNORE:
		case DSHR_ABORT_FAILED:
		case DSHR_CORRUPT:
		default:
			dtx_dre_release(dre);
			continue;
		}

commit:
		D_DEBUG(DB_TRACE, "As the new leader for TX "
			DF_DTI", try to commit it.\n", DP_DTI(&dre->dre_xid));

		if (++count >= DTX_THRESHOLD_COUNT) {
			dtx_resync_commit(cont, dtes, drh, count);
			count = 0;
		}
	}

	if (count > 0) {
		dtx_resync_commit(cont, dtes, drh, count);
		count = 0;
	}

	/* The last DTX entry may be re-added to the list because of DSHR_NEED_RETRY. */
	if (unlikely(!d_list_empty(&drh->drh_list)))
		goto again;

out:
	D_FREE(tgt_array);
	D_FREE(dtes);

	/* Handle former left pending committable DTX entries. */
	if (!cont->sc_resync_pending && unlikely(!d_list_empty(&cont->sc_dtx_resync_pending))) {
		while ((dre = d_list_pop_entry(&cont->sc_dtx_resync_pending,
					       struct dtx_resync_entry, dre_link)) != NULL) {
			dre->dre_seq = 0;
			d_list_add_tail(&dre->dre_link, &drh->drh_pending);
		}
	}

	/* Try to re-commit those pending DTX entries before exit DTX resync. */
	dtx_handle_resync_pending(cont, &drh->drh_pending, 1, NULL);

	if (unlikely(!d_list_empty(&cont->sc_dtx_resync_pending)) && err == 0) {
		dre = d_list_entry(cont->sc_dtx_resync_pending.next, struct dtx_resync_entry,
				   dre_link);
		err = dre->dre_err;
	}

	while ((dre = d_list_pop_entry(&drh->drh_list, struct dtx_resync_entry,
				       dre_link)) != NULL)
		dtx_dre_release(dre);

	if (err >= 0 && dra->resync_version != dra->discard_version)
		/* Drain old committable DTX to help subsequent rebuild. */
		err = dtx_obj_sync(cont, NULL, dra->epoch);

	if (err == -DER_NONEXIST)
		err = 0;

	return err;
}

static int
dtx_iter_cb(uuid_t co_uuid, vos_iter_entry_t *ent, void *args)
{
	struct dtx_resync_args		*dra = args;
	struct dtx_resync_entry		*dre;
	struct dtx_entry		*dte;
	struct dtx_memberships		*mbs;

	/* We commit the DTXs periodically, there will be not too many DTXs
	 * to be checked when resync. So we can load all those uncommitted
	 * DTXs in RAM firstly, then check the state one by one. That avoid
	 * the race trouble between iteration of active-DTX tree and commit
	 * (or abort) the DTXs (that will change the active-DTX tree).
	 */

	D_ASSERT(!(ent->ie_dtx_flags & DTE_INVALID));

	/* Skip corrupted entry that will be handled via other special tool. */
	if (ent->ie_dtx_flags & DTE_CORRUPTED)
		return 0;

	/* Skip orphan entry that will be handled via other special tool. */
	if (ent->ie_dtx_flags & DTE_ORPHAN)
		return 0;

	if (ent->ie_dtx_ver < dra->discard_version) {
		D_ALLOC_PTR(dre);
		if (dre == NULL)
			return -DER_NOMEM;

		dte = &dre->dre_dte;
		goto out;
	}

	/* Current DTX resync is only for discarding old DTX entries. */
	if (dra->resync_version == dra->discard_version)
		return 0;

	/*
	 * The DTX version maybe refreshed via obj_handle_resend(). It means that either the
	 * DTX is generated against the latest pool map or related IO RPC is resent by client
	 * after pool map changed. Under both cases, DTX resync should not handle such DTX to
	 * avoid making conflict commit/abort decision (against regular IO handler) by race.
	 *
	 * But for the case of engine restart quickly (without pool map change), DTX resync
	 * that is triggered by the initial pool map from PS leader needs to handle all the
	 * DTX entries before engine restart. Because DTX 'committable' status is cached in
	 * DRAM and will be lost after engine restart. If do not synchronize them, related
	 * IO (both regular and rebuild) may miss the latest valid data.
	 *
	 * Clock drift may cause epoch difference among servers, that is general issue for
	 * dss_get_start_epoch(), will be considered in other independent ticket.
	 */
	if ((ent->ie_dtx_ver > dra->resync_version) ||
	    (ent->ie_dtx_ver == dra->resync_version && ent->ie_epoch > dss_get_start_epoch()))
		return 0;

	if (ent->ie_dtx_tgt_cnt == 0)
		return 0;

	/*
	 * Current DTX resync may be shared by pool map refresh and container open. If it is
	 * sponsored by pool map refresh, then it is possible that resync version is smaller
	 * than some DTX entries that also need to be resynced. So here, we only trust epoch.
	 */
	if (ent->ie_epoch > dra->epoch)
		return 0;

	D_ASSERT(ent->ie_dtx_mbs_dsize > 0);

	if (ent->ie_dtx_mbs_dsize > DTX_INLINE_MBS_SIZE)
		D_ALLOC_PTR(dre);
	else
		D_ALLOC(dre, sizeof(*dre) + sizeof(*mbs) + ent->ie_dtx_mbs_dsize);

	if (dre == NULL)
		return -DER_NOMEM;

	dre->dre_oid = ent->ie_dtx_oid;
	dre->dre_dkey_hash = ent->ie_dkey_hash;

	dte = &dre->dre_dte;

	if (ent->ie_dtx_mbs_dsize > DTX_INLINE_MBS_SIZE) {
		dre->dre_inline_mbs = 0;
		dte->dte_mbs = NULL;
	} else {
		mbs = (struct dtx_memberships *)(dte + 1);

		mbs->dm_tgt_cnt = ent->ie_dtx_tgt_cnt;
		mbs->dm_grp_cnt = ent->ie_dtx_grp_cnt;
		mbs->dm_data_size = ent->ie_dtx_mbs_dsize;
		mbs->dm_flags = ent->ie_dtx_mbs_flags;
		mbs->dm_dte_flags = ent->ie_dtx_flags;
		memcpy(mbs->dm_data, ent->ie_dtx_mbs, ent->ie_dtx_mbs_dsize);

		dre->dre_inline_mbs = 1;
		dte->dte_mbs = mbs;
	}

out:
	dre->dre_epoch = ent->ie_epoch;
	dte->dte_ver = ent->ie_dtx_ver;
	dte->dte_xid = ent->ie_dtx_xid;
	dte->dte_refs = 1;
	d_list_add_tail(&dre->dre_link, &dra->tables.drh_list);

	return 0;
}

int
dtx_resync(daos_handle_t po_hdl, struct ds_cont_child *cont, uint32_t ver)
{
	struct ds_pool			*pool;
	struct pool_target		*target;
	struct dtx_resync_args		 dra = { 0 };
	d_rank_t			 myrank;
	int				 rc = 0;
	int				 rc1 = 0;

	D_DEBUG(DB_MD, "Enter DTX resync for " DF_UUID "/" DF_UUID " with ver %u\n",
		DP_UUID(cont->sc_pool_uuid), DP_UUID(cont->sc_uuid), ver);

	crt_group_rank(NULL, &myrank);

	pool = cont->sc_pool->spc_pool;
	if (pool->sp_disable_dtx_resync) {
		/* Directly set resync version when DTX resync is disabled. */
		vos_set_dtx_resync_version(cont->sc_hdl, ver, true);
		D_DEBUG(DB_MD, "Skip DTX resync for " DF_UUID "/" DF_UUID " with ver %u\n",
			DP_UUID(cont->sc_pool_uuid), DP_UUID(cont->sc_uuid), ver);
		goto out;
	}

	ABT_rwlock_rdlock(pool->sp_lock);
	rc = pool_map_find_target_by_rank_idx(pool->sp_map, myrank,
					      dss_get_module_info()->dmi_tgt_id, &target);
	D_ASSERT(rc == 1);

	if (target->ta_comp.co_status == PO_COMP_ST_UP) {
		dra.discard_version = target->ta_comp.co_in_ver;
		D_DEBUG(DB_MD, "DTX resync for " DF_UUID "/" DF_UUID " discard version: %u\n",
			DP_UUID(cont->sc_pool_uuid), DP_UUID(cont->sc_uuid), dra.discard_version);
	}

	ABT_rwlock_unlock(pool->sp_lock);

	ABT_mutex_lock(cont->sc_mutex);

	while (cont->sc_dtx_resyncing) {
		D_DEBUG(DB_TRACE, "Waiting for resync of " DF_UUID "\n", DP_UUID(cont->sc_uuid));
		ABT_cond_wait(cont->sc_dtx_resync_cond, cont->sc_mutex);
	}

	if (myrank == daos_fail_value_get() && DAOS_FAIL_CHECK(DAOS_DTX_SRV_RESTART)) {
		dss_set_start_epoch();
		vos_dtx_cache_reset(cont->sc_hdl, true);
		cont->sc_dtx_indexed = 0;

		while (1) {
			rc = vos_dtx_cmt_reindex(cont->sc_hdl);
			if (rc > 0)
				break;

			/* Simplify failure handling just for test. */
			D_ASSERT(rc == 0);

			ABT_thread_yield();
		}
	}

	cont->sc_dtx_resyncing = 1;
	ABT_mutex_unlock(cont->sc_mutex);

	dra.cont = cont;
	dra.resync_version = ver;
	dra.epoch = d_hlc_get();
	D_INIT_LIST_HEAD(&dra.tables.drh_list);
	D_INIT_LIST_HEAD(&dra.tables.drh_pending);

	/*
	 * Trigger DTX reindex. That will avoid DTX_CHECK from others being blocked.
	 * It is harmless even if (committed) DTX entries have already been re-indexed.
	 */
	if (!dtx_cont_opened(cont)) {
		rc = start_dtx_reindex_ult(cont);
		if (rc != 0) {
			D_ERROR(DF_UUID": Failed to trigger DTX reindex, ver %u/%u: "DF_RC"\n",
				DP_UUID(cont->sc_uuid), dra.discard_version, ver, DP_RC(rc));
			goto fail;
		}
	}

	D_DEBUG(DB_MD, "Start DTX resync scan for " DF_UUID "/" DF_UUID " with ver %u\n",
		DP_UUID(cont->sc_pool_uuid), DP_UUID(cont->sc_uuid), ver);

	rc = ds_cont_iter(po_hdl, cont->sc_uuid, dtx_iter_cb, &dra, VOS_ITER_DTX, 0);

	/* Handle the DTXs that have been scanned even if some failure happened
	 * in above ds_cont_iter() step.
	 */
	rc1 = dtx_status_handle(&dra);

	D_ASSERT(d_list_empty(&dra.tables.drh_list));

	if (rc >= 0)
		rc = rc1;

	if (rc >= 0)
		vos_set_dtx_resync_version(cont->sc_hdl, ver, true);

	DL_CDEBUG(rc != 0, DLOG_ERR, DB_MD, rc,
		  "Stop DTX resync scan for " DF_UUID "/" DF_UUID " with ver %u",
		  DP_UUID(cont->sc_pool_uuid), DP_UUID(cont->sc_uuid), ver);

fail:
	ABT_mutex_lock(cont->sc_mutex);
	cont->sc_dtx_resyncing = 0;
	ABT_cond_broadcast(cont->sc_dtx_resync_cond);
	ABT_mutex_unlock(cont->sc_mutex);

out:
	DL_CDEBUG(rc != 0, DLOG_ERR, DB_MD, rc,
		  "Exit DTX resync scan for " DF_UUID "/" DF_UUID " with ver %u",
		  DP_UUID(cont->sc_pool_uuid), DP_UUID(cont->sc_uuid), ver);

	return rc > 0 ? 0 : rc;
}

struct dtx_container_scan_arg {
	uuid_t			co_uuid;
	struct dtx_scan_args	arg;
};

static int
container_scan_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		  vos_iter_type_t type, vos_iter_param_t *iter_param,
		  void *data, unsigned *acts)
{
	struct dtx_container_scan_arg *scan_arg = data;
	struct dtx_scan_args          *arg      = &scan_arg->arg;
	struct ds_cont_child          *cont     = NULL;
	int                            rc;

	if (uuid_compare(scan_arg->co_uuid, entry->ie_couuid) == 0)
		return 0;

	rc = ds_cont_child_lookup(arg->pool_uuid, entry->ie_couuid, &cont);
	if (rc != 0) {
		if (rc == -DER_NONEXIST || rc == -DER_CONT_NONEXIST || rc == -DER_CONT_DESTROYING ||
		    rc == -DER_SHUTDOWN)
			rc = 0;
		goto out;
	}

	uuid_copy(scan_arg->co_uuid, entry->ie_couuid);
	if (arg->for_orphan) {
again:
		rc = dtx_cleanup_internal(cont, NULL, arg->version, true);
		if (rc == -DER_INPROGRESS || rc == -DER_OOG || rc == -DER_HG) {
			D_WARN("Cleanup DTX for " DF_UUID "/" DF_UUID " is blocked " DF_RC "\n",
			       DP_UUID(arg->pool_uuid), DP_UUID(entry->ie_couuid), DP_RC(rc));
			ABT_thread_yield();
			goto again;
		}
	} else {
		rc = dtx_resync(iter_param->ip_hdl, cont, arg->version);
	}
	if (rc == 0)
		/* Since dtx_{cleanup,resync} might yield, let's reprobe anyway */
		*acts |= VOS_ITER_CB_YIELD;

	ds_cont_child_put(cont);

out:
	DL_CDEBUG(rc != 0, DLOG_ERR, DB_MD, rc, "%s DTX for " DF_UUID "/" DF_UUID,
		  arg->for_orphan ? "cleanup" : "resync", DP_UUID(arg->pool_uuid),
		  DP_UUID(entry->ie_couuid));

	return rc;
}

static int
dtx_resync_one(void *data)
{
	struct ds_pool_child         *child;
	struct dtx_scan_args         *arg    = data;
	vos_iter_param_t              param  = {0};
	struct vos_iter_anchors       anchor = {0};
	struct dtx_container_scan_arg cb_arg = {0};
	int                           rc;

	child = ds_pool_child_lookup(arg->pool_uuid);
	if (child == NULL) {
		D_WARN("Cannot find pool " DF_UUID " for rebuild with version %u\n",
		       DP_UUID(arg->pool_uuid), arg->version);
		/* Pool may be destroyed or stopped by race, return 0 instead of failure. */
		D_GOTO(out, rc = 0);
	}

	if (unlikely(child->spc_no_storage))
		D_GOTO(out, rc = 0);

	cb_arg.arg     = *arg;
	param.ip_hdl   = child->spc_hdl;
	param.ip_flags = VOS_IT_FOR_MIGRATION;

	rc = vos_iterate(&param, VOS_ITER_COUUID, false, &anchor, container_scan_cb, NULL, &cb_arg,
			 NULL);

out:
	if (child != NULL)
		ds_pool_child_put(child);

	D_DEBUG(DB_TRACE, DF_UUID" iterate pool done: rc %d\n",
		DP_UUID(arg->pool_uuid), rc);

	return rc;
}

void
dtx_resync_ult(void *data)
{
	struct dtx_scan_args *arg   = data;
	struct ds_pool       *pool  = NULL;
	int                   retry = 0;
	int                   rc;

again:
	rc = ds_pool_lookup(arg->pool_uuid, &pool);
	if (rc != 0) {
		D_WARN("Cannot find the pool "DF_UUID" for DTX resync: "DF_RC"\n",
		       DP_UUID(arg->pool_uuid), DP_RC(rc));
		goto out;
	}

	if (pool->sp_dtx_resync_version >= arg->version) {
		D_DEBUG(DB_MD, DF_UUID" ignore dtx resync version %u/%u\n",
			DP_UUID(arg->pool_uuid), pool->sp_dtx_resync_version,
			arg->version);
		goto out;
	}

	/* Delay 5 seconds for DTX resync. */
	if (DAOS_FAIL_CHECK(DAOS_DTX_RESYNC_DELAY))
		dss_sleep(5 * 1000);

	D_DEBUG(DB_MD, DF_UUID " start dtx resync: version %u vs %u, retry %d times.\n",
		DP_UUID(arg->pool_uuid), pool->sp_dtx_resync_version, arg->version, retry);

	rc = ds_pool_thread_collective(arg->pool_uuid, PO_COMP_ST_DOWN | PO_COMP_ST_DOWNOUT |
				       PO_COMP_ST_NEW, dtx_resync_one, arg, DSS_ULT_DEEP_STACK);
	if (rc != 0) {
		if (retry++ % 10 == 0)
			D_ERROR("Fail to resync DTX for " DF_UUID " ver %u, tried %u: " DF_RC "\n",
				DP_UUID(arg->pool_uuid), arg->version, retry, DP_RC(rc));
		/*
		 * NOTE: Do not update sp_dtx_resync_version if DTX resync failed; otherwise
		 *	 subsequent rebuild may lost data silently that must be avoided.
		 *
		 *	 If pool map was refreshed during current DTX resync, then it is no matter
		 *	 to failout since the DTX resync for new pool map is on the way. Otherwise
		 *	 let's retry DTX resync until done or pool shutdown or destroyed.
		 */
		if (pool->sp_map_version > arg->version)
			goto out;

		ds_pool_put(pool);
		pool = NULL;
		dss_sleep(3000);

		goto again;
	}

	if (pool->sp_dtx_resync_version < arg->version) {
		D_DEBUG(DB_MD, DF_UUID " update dtx resync version %u->%u\n",
			DP_UUID(arg->pool_uuid), pool->sp_dtx_resync_version, arg->version);
		pool->sp_dtx_resync_version = arg->version;
	}

out:
	if (pool != NULL)
		ds_pool_put(pool);
	D_FREE(arg);
}

int
dtx_cleanup_orphan(uuid_t po_uuid, uint32_t pm_ver)
{
	struct dtx_scan_args arg;
	int                  rc = 0;

	uuid_copy(arg.pool_uuid, po_uuid);
	arg.version    = pm_ver;
	arg.for_orphan = true;
	rc             = dtx_resync_one(&arg);

	DL_CDEBUG(rc != 0, DLOG_ERR, DB_MD, rc, "DTX cleanup orphan for " DF_UUID " with ver %u",
		  DP_UUID(po_uuid), pm_ver);

	return rc;
}
