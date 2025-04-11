/**
 * (C) Copyright 2019-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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

struct dtx_resync_entry {
	d_list_t		dre_link;
	daos_epoch_t		dre_epoch;
	daos_unit_oid_t		dre_oid;
	uint32_t		dre_inline_mbs:1;
	uint64_t		dre_dkey_hash;
	struct dtx_entry	dre_dte;
};

#define dre_xid		dre_dte.dte_xid

struct dtx_resync_head {
	d_list_t		drh_list;
	int			drh_count;
};

struct dtx_resync_args {
	struct ds_cont_child	*cont;
	struct dtx_resync_head	 tables;
	daos_epoch_t		 epoch;
	uint32_t		 resync_version;
	uint32_t		 discard_version;
};

static inline void
dtx_dre_release(struct dtx_resync_head *drh, struct dtx_resync_entry *dre)
{
	drh->drh_count--;
	d_list_del(&dre->dre_link);
	if (--(dre->dre_dte.dte_refs) == 0) {
		if (dre->dre_inline_mbs == 0)
			D_FREE(dre->dre_dte.dte_mbs);
		D_FREE(dre);
	}
}

static int
dtx_resync_commit(struct ds_cont_child *cont,
		  struct dtx_resync_head *drh, int count)
{
	struct dtx_resync_entry		 *dre;
	struct dtx_entry		**dtes = NULL;
	struct dtx_cos_key		 *dcks = NULL;
	int				  rc = 0;
	int				  i = 0;
	int				  j = 0;

	D_ASSERT(drh->drh_count >= count);

	D_ALLOC_ARRAY(dtes, count);
	if (dtes == NULL)
		return -DER_NOMEM;

	D_ALLOC_ARRAY(dcks, count);
	if (dcks == NULL) {
		D_FREE(dtes);
		return -DER_NOMEM;
	}

	for (i = 0; i < count; i++) {
		dre = d_list_entry(drh->drh_list.next,
				   struct dtx_resync_entry, dre_link);

		/* Someone (the DTX owner or batched commit ULT) may have
		 * committed or aborted the DTX during we handling other
		 * DTXs. So double check the status before current commit.
		 */
		rc = vos_dtx_check(cont->sc_hdl, &dre->dre_xid, NULL, NULL, NULL, false);

		/*
		 * Skip this DTX since it has been committed or aggregated.
		 * If we failed to check the status, then assume that it is
		 * not committed, then commit it (again), that is harmless.
		 */
		if (rc != DTX_ST_COMMITTED && rc != -DER_NONEXIST) {
			dtes[j] = dtx_entry_get(&dre->dre_dte);
			dcks[j].oid = dre->dre_oid;
			dcks[j].dkey_hash = dre->dre_dkey_hash;
			j++;
		}

		dtx_dre_release(drh, dre);
	}

	if (j > 0) {
		rc = dtx_commit(cont, dtes, dcks, j, true);
		if (rc < 0)
			D_ERROR("Failed to commit the DTXs: rc = "DF_RC"\n",
				DP_RC(rc));

		for (i = 0; i < j; i++) {
			D_ASSERT(dtes[i]->dte_refs == 1);

			dre = d_list_entry(dtes[i], struct dtx_resync_entry,
					   dre_dte);
			if (dre->dre_inline_mbs == 0)
				D_FREE(dre->dre_dte.dte_mbs);
			D_FREE(dre);
		}
	} else {
		rc = 0;
	}

	D_FREE(dtes);
	D_FREE(dcks);
	return rc;
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

int
dtx_status_handle_one(struct ds_cont_child *cont, struct dtx_entry *dte, daos_unit_oid_t oid,
		      uint64_t dkey_hash, daos_epoch_t epoch, int *tgt_array, int *err)
{
	struct dtx_memberships	*mbs = dte->dte_mbs;
	struct dtx_coll_entry	*dce = NULL;
	int			 rc = 0;

	if (mbs->dm_flags & DMF_COLL_TARGET) {
		rc = dtx_coll_prep(cont->sc_pool_uuid, oid, &dte->dte_xid, mbs,
				   dss_get_module_info()->dmi_tgt_id, dte->dte_ver,
				   cont->sc_pool->spc_map_version, true, true, &dce);
		if (rc != 0) {
			D_ERROR("Failed to prepare the bitmap (and hints) for collective DTX "
				DF_DTI": "DF_RC"\n", DP_DTI(&dte->dte_xid), DP_RC(rc));
			goto out;
		}

		rc = dtx_coll_check(cont, dce, epoch);
	} else {
		rc = dtx_check(cont, dte, epoch);
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
		/* Yield to give more chance for network recovery. */
		ABT_thread_yield();
		D_GOTO(out, rc = DSHR_NEED_RETRY);
	case -DER_INPROGRESS:
	case -DER_TIMEDOUT:
		D_WARN("Other participants not sure about whether the "
		       "DTX "DF_DTI" is committed or not, need retry.\n",
		       DP_DTI(&dte->dte_xid));
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
				rc = dtx_coll_abort(cont, dce, 0);
			else
				rc = dtx_abort(cont, dte, 0);
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
		rc = vos_dtx_check(cont->sc_hdl, &dte->dte_xid, NULL, NULL, NULL, false);

		/* Skip the DTX that may has been committed or aborted. */
		if (rc == DTX_ST_COMMITTED || rc == DTX_ST_COMMITTABLE || rc == -DER_NONEXIST)
			D_GOTO(out, rc = DSHR_IGNORE);

		/* Skip this DTX if failed to get the status. */
		if (rc != DTX_ST_PREPARED && rc != DTX_ST_INITED) {
			D_WARN("Not sure about whether the DTX "DF_DTI
			       " can be abort or not: %d, skip it.\n",
			       DP_DTI(&dte->dte_xid), rc);
			D_GOTO(out, rc = (rc > 0 ? -DER_IO : rc));
		}

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
			rc = dtx_coll_abort(cont, dce, epoch);
		else
			rc = dtx_abort(cont, dte, epoch);

		D_DEBUG(DB_TRACE, "As new leader for DTX "DF_DTI", abort it (2): "DF_RC"\n",
			DP_DTI(&dte->dte_xid), DP_RC(rc));

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
	if (rc == DSHR_NEED_COMMIT && mbs->dm_flags & DMF_COLL_TARGET) {
		struct dtx_cos_key	dck;

		dck.oid = oid;
		dck.dkey_hash = dkey_hash;
		rc = dtx_coll_commit(cont, dce, &dck, true);
	}

	dtx_coll_entry_put(dce);
	return rc;
}

static int
dtx_status_handle(struct dtx_resync_args *dra)
{
	struct ds_cont_child		*cont = dra->cont;
	struct dtx_resync_head		*drh = &dra->tables;
	struct dtx_resync_entry		*dre;
	struct dtx_resync_entry		*next;
	struct dtx_memberships		*mbs;
	struct ds_pool			*pool = cont->sc_pool->spc_pool;
	int				*tgt_array = NULL;
	int				 tgt_cnt;
	int				 count = 0;
	int				 err = 0;
	int				 rc;

	if (drh->drh_count == 0)
		goto out;

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
			err = vos_dtx_abort(cont->sc_hdl, &dre->dre_xid, dre->dre_epoch);
			if (err == -DER_NONEXIST)
				err = 0;
			if (err != 0)
				D_ERROR("Failed to discard stale DTX "DF_DTI" with ver %d/%d: "
					DF_RC"\n", DP_DTI(&dre->dre_xid), dre->dre_dte.dte_ver,
					dra->discard_version, DP_RC(err));
			dtx_dre_release(drh, dre);
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
				dtx_dre_release(drh, dre);
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
			dtx_dre_release(drh, dre);
			continue;
		}

		rc = dtx_status_handle_one(cont, &dre->dre_dte, dre->dre_oid, dre->dre_dkey_hash,
					   dre->dre_epoch, tgt_array, &err);

		if (unlikely(cont->sc_stopping))
			D_GOTO(out, err = -DER_CANCELED);

		switch (rc) {
		case DSHR_NEED_COMMIT:
			goto commit;
		case DSHR_NEED_RETRY:
			d_list_del(&dre->dre_link);
			d_list_add_tail(&dre->dre_link, &drh->drh_list);
			continue;
		case DSHR_IGNORE:
		case DSHR_ABORT_FAILED:
		case DSHR_CORRUPT:
		default:
			dtx_dre_release(drh, dre);
			continue;
		}

commit:
		D_DEBUG(DB_TRACE, "As the new leader for TX "
			DF_DTI", try to commit it.\n", DP_DTI(&dre->dre_xid));

		if (++count >= DTX_THRESHOLD_COUNT) {
			rc = dtx_resync_commit(cont, drh, count);
			if (rc < 0)
				err = rc;
			count = 0;
		}
	}

	if (count > 0) {
		rc = dtx_resync_commit(cont, drh, count);
		if (rc < 0)
			err = rc;
		count = 0;
	}

	/* The last DTX entry may be re-added to the list because of DSHR_NEED_RETRY. */
	if (unlikely(!d_list_empty(&drh->drh_list)))
		goto again;

out:
	D_FREE(tgt_array);

	while ((dre = d_list_pop_entry(&drh->drh_list, struct dtx_resync_entry,
				       dre_link)) != NULL)
		dtx_dre_release(drh, dre);

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

	/* Skip unprepared entry which version is at least not older than discard version. */
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
	dra->tables.drh_count++;

	return 0;
}

int
dtx_resync(daos_handle_t po_hdl, uuid_t po_uuid, uuid_t co_uuid, uint32_t ver, bool block)
{
	struct ds_cont_child		*cont = NULL;
	struct ds_pool			*pool;
	struct pool_target		*target;
	struct dtx_resync_args		 dra = { 0 };
	d_rank_t			 myrank;
	int				 rc = 0;
	int				 rc1 = 0;

	rc = ds_cont_child_lookup(po_uuid, co_uuid, &cont);
	if (rc != 0) {
		D_ERROR("Failed to open container for resync DTX "
			DF_UUID"/"DF_UUID": rc = %d\n",
			DP_UUID(po_uuid), DP_UUID(co_uuid), rc);
		return rc;
	}

	D_DEBUG(DB_MD, "Enter DTX resync (%s) for "DF_UUID"/"DF_UUID" with ver %u\n",
		block ? "block" : "non-block", DP_UUID(po_uuid), DP_UUID(co_uuid), ver);

	crt_group_rank(NULL, &myrank);

	pool = cont->sc_pool->spc_pool;
	if (pool->sp_disable_dtx_resync) {
		D_DEBUG(DB_MD, "Skip DTX resync (%s) for " DF_UUID "/" DF_UUID " with ver %u\n",
			block ? "block" : "non-block", DP_UUID(po_uuid), DP_UUID(co_uuid), ver);
		goto out;
	}

	ABT_rwlock_rdlock(pool->sp_lock);
	rc = pool_map_find_target_by_rank_idx(pool->sp_map, myrank,
					      dss_get_module_info()->dmi_tgt_id, &target);
	D_ASSERT(rc == 1);

	if (target->ta_comp.co_status == PO_COMP_ST_UP) {
		dra.discard_version = target->ta_comp.co_in_ver;
		D_DEBUG(DB_MD, "DTX resync for "DF_UUID"/"DF_UUID" discard version: %u\n",
			DP_UUID(po_uuid), DP_UUID(co_uuid), dra.discard_version);
	}

	ABT_rwlock_unlock(pool->sp_lock);

	ABT_mutex_lock(cont->sc_mutex);

	while (cont->sc_dtx_resyncing) {
		if (!block) {
			ABT_mutex_unlock(cont->sc_mutex);
			goto out;
		}
		D_DEBUG(DB_TRACE, "Waiting for resync of "DF_UUID"\n",
			DP_UUID(co_uuid));
		ABT_cond_wait(cont->sc_dtx_resync_cond, cont->sc_mutex);
	}

	if (myrank == daos_fail_value_get() && DAOS_FAIL_CHECK(DAOS_DTX_SRV_RESTART)) {
		dss_set_start_epoch();
		vos_dtx_cache_reset(cont->sc_hdl, true);

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
	dra.tables.drh_count = 0;

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

	D_DEBUG(DB_MD, "Start DTX resync (%s) scan for "DF_UUID"/"DF_UUID" with ver %u\n",
		block ? "block" : "non-block", DP_UUID(po_uuid), DP_UUID(co_uuid), ver);

	rc = ds_cont_iter(po_hdl, co_uuid, dtx_iter_cb, &dra, VOS_ITER_DTX, 0);

	/* Handle the DTXs that have been scanned even if some failure happened
	 * in above ds_cont_iter() step.
	 */
	rc1 = dtx_status_handle(&dra);

	D_ASSERT(d_list_empty(&dra.tables.drh_list));

	if (rc >= 0)
		rc = rc1;

	if (rc >= 0)
		vos_set_dtx_resync_version(cont->sc_hdl, ver);

	D_DEBUG(DB_MD, "Stop DTX resync (%s) scan for "DF_UUID"/"DF_UUID" with ver %u: rc = %d\n",
		block ? "block" : "non-block", DP_UUID(po_uuid), DP_UUID(co_uuid), ver, rc);

fail:
	ABT_mutex_lock(cont->sc_mutex);
	cont->sc_dtx_resyncing = 0;
	ABT_cond_broadcast(cont->sc_dtx_resync_cond);
	ABT_mutex_unlock(cont->sc_mutex);

out:
	D_DEBUG(DB_MD, "Exit DTX resync (%s) for "DF_UUID"/"DF_UUID" with ver %u, rc = %d\n",
		block ? "block" : "non-block", DP_UUID(po_uuid), DP_UUID(co_uuid), ver, rc);

	ds_cont_child_put(cont);
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
	struct dtx_container_scan_arg	*scan_arg = data;
	struct dtx_scan_args		*arg = &scan_arg->arg;
	int				rc;

	if (uuid_compare(scan_arg->co_uuid, entry->ie_couuid) == 0) {
		D_DEBUG(DB_REBUILD, DF_UUID" already scan\n",
			DP_UUID(scan_arg->co_uuid));
		return 0;
	}

	uuid_copy(scan_arg->co_uuid, entry->ie_couuid);
	rc = dtx_resync(iter_param->ip_hdl, arg->pool_uuid, entry->ie_couuid, arg->version, true);
	if (rc)
		D_ERROR(DF_UUID" dtx resync failed: rc %d\n",
			DP_UUID(arg->pool_uuid), rc);

	/* Since dtx_resync might yield, let's reprobe anyway */
	*acts |= VOS_ITER_CB_YIELD;

	return rc;
}

static int
dtx_resync_one(void *data)
{
	struct dtx_scan_args		*arg = data;
	struct ds_pool_child		*child;
	vos_iter_param_t		*param = NULL;
	struct vos_iter_anchors		*anchor = NULL;
	struct dtx_container_scan_arg	 cb_arg = { 0 };
	int				 rc;

	child = ds_pool_child_lookup(arg->pool_uuid);
	if (child == NULL)
		D_GOTO(out, rc = -DER_NONEXIST);

	if (unlikely(child->spc_no_storage))
		D_GOTO(out, rc = 0);

	D_ALLOC_PTR(param);
	if (param == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC_PTR(anchor);
	if (anchor == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	cb_arg.arg = *arg;
	param->ip_hdl = child->spc_hdl;
	param->ip_flags = VOS_IT_FOR_MIGRATION;
	rc = vos_iterate(param, VOS_ITER_COUUID, false, anchor,
			 container_scan_cb, NULL, &cb_arg, NULL);

out:
	D_FREE(param);
	D_FREE(anchor);
	if (child != NULL)
		ds_pool_child_put(child);

	D_DEBUG(DB_TRACE, DF_UUID" iterate pool done: rc %d\n",
		DP_UUID(arg->pool_uuid), rc);

	return rc;
}

void
dtx_resync_ult(void *data)
{
	struct dtx_scan_args	*arg = data;
	struct ds_pool		*pool = NULL;
	int			rc;

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
	D_DEBUG(DB_MD, DF_UUID" update dtx resync version %u->%u\n",
		DP_UUID(arg->pool_uuid), pool->sp_dtx_resync_version,
		arg->version);

	/* Delay 5 seconds for DTX resync. */
	if (DAOS_FAIL_CHECK(DAOS_DTX_RESYNC_DELAY))
		dss_sleep(5 * 1000);

	rc = ds_pool_thread_collective(arg->pool_uuid, PO_COMP_ST_DOWN | PO_COMP_ST_DOWNOUT |
				       PO_COMP_ST_NEW, dtx_resync_one, arg, DSS_ULT_DEEP_STACK);
	if (rc) {
		/* If dtx resync fails, then let's still update
		 * sp_dtx_resync_version, so the rebuild can go ahead,
		 * though it might fail, instead of hanging here.
		 */
		D_ERROR("dtx resync collective "DF_UUID" %d.\n",
			DP_UUID(arg->pool_uuid), rc);
	}
	pool->sp_dtx_resync_version = arg->version;

out:
	if (pool != NULL)
		ds_pool_put(pool);
	D_FREE(arg);
}
