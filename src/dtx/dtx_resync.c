/**
 * (C) Copyright 2019-2021 Intel Corporation.
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
	uint32_t		 version;
	uint32_t		 resync_all:1;
};

static inline void
dtx_dre_release(struct dtx_resync_head *drh, struct dtx_resync_entry *dre)
{
	drh->drh_count--;
	d_list_del(&dre->dre_link);
	if (--(dre->dre_dte.dte_refs) == 0)
		D_FREE(dre);
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
		rc = vos_dtx_check(cont->sc_hdl, &dre->dre_xid,
				   NULL, NULL, NULL, NULL, false);

		/* Skip this DTX since it has been committed or aggregated. */
		if (rc == DTX_ST_COMMITTED || rc == DTX_ST_COMMITTABLE ||
		    rc == -DER_NONEXIST)
			goto next;

		/* If we failed to check the status, then assume that it is
		 * not committed, then commit it (again), that is harmless.
		 */

		dtes[j] = dtx_entry_get(&dre->dre_dte);
		dcks[j].oid = dre->dre_oid;
		dcks[j].dkey_hash = dre->dre_dkey_hash;
		j++;

next:
		dtx_dre_release(drh, dre);
	}

	if (j > 0) {
		rc = dtx_commit(cont, dtes, dcks, j);
		if (rc < 0)
			D_ERROR("Failed to commit the DTXs: rc = "DF_RC"\n",
				DP_RC(rc));

		for (i = 0; i < j; i++) {
			D_ASSERT(dtes[i]->dte_refs == 1);

			dre = d_list_entry(dtes[i], struct dtx_resync_entry,
					   dre_dte);
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
dtx_target_alive(struct ds_pool *pool, uint32_t id)
{
	struct pool_target	*target;
	int			 rc;

	ABT_rwlock_rdlock(pool->sp_lock);
	rc = pool_map_find_target(pool->sp_map, id, &target);
	if (rc != 1) {
		D_WARN("Cannot find target %u because of empty pool map\n", id);
		ABT_rwlock_unlock(pool->sp_lock);
		return -DER_UNINIT;
	}
	ABT_rwlock_unlock(pool->sp_lock);

	return target->ta_comp.co_status == PO_COMP_ST_UPIN ? 1 : 0;
}

static int
dtx_is_leader(struct ds_pool *pool, struct dtx_resync_args *dra,
	      struct dtx_resync_entry *dre)
{
	struct dtx_memberships	*mbs = dre->dre_dte.dte_mbs;
	int			 rc;

	/* Old leader is still alive, then current server is not the leader. */
	if (mbs->dm_flags & DMF_CONTAIN_LEADER) {
		rc = dtx_target_alive(pool, mbs->dm_tgts[0].ddt_id);
		if (rc != 0)
			return rc > 0 ? 0 : rc;
	}

	/* XXX: need more work when we support to elect DTX leader from
	 *	data shard for EC object in the future.
	 */
	return ds_pool_check_dtx_leader(pool, &dre->dre_oid,
					pool->sp_map_version, false);
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

			rc = dtx_target_alive(pool, group->drg_ids[j]);
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
dtx_status_handle_one(struct ds_cont_child *cont, struct dtx_entry *dte,
		      daos_epoch_t epoch, int *tgt_array, int *err)
{
	int	rc = 0;

	rc = dtx_check(cont, dte, epoch);
	switch (rc) {
	case DTX_ST_COMMITTED:
	case DTX_ST_COMMITTABLE:
		/* The DTX has been committed on some remote replica(s),
		 * let's commit the DTX globally.
		 */
		return DSHR_NEED_COMMIT;
	case -DER_INPROGRESS:
	case -DER_TIMEDOUT:
		D_WARN("Other participants not sure about whether the "
		       "DTX "DF_DTI" is committed or not, need retry.\n",
		       DP_DTI(&dte->dte_xid));
		return DSHR_NEED_RETRY;
	case DTX_ST_PREPARED: {
		struct dtx_memberships	*mbs = dte->dte_mbs;

		/* If the transaction across multiple redundancy groups,
		 * need to check whether there are enough alive targets.
		 */
		if (mbs->dm_grp_cnt > 1) {
			rc = dtx_verify_groups(cont->sc_pool->spc_pool, mbs,
					       &dte->dte_xid, tgt_array);
			if (rc < 0)
				goto out;

			if (rc > 0)
				return DSHR_NEED_COMMIT;

			/* XXX: For the distributed transaction that lose too
			 *	many particiants (the whole redundancy group),
			 *	it's difficult to make decision whether commit
			 *	or abort the DTX. we need more human knowledge
			 *	to manually recover related things.
			 *
			 *	Then we mark the TX as corrupted via special
			 *	dtx_abort() with 0 @epoch.
			 */
			rc = dtx_abort(cont, 0, &dte, 1);
			if (rc < 0 && err != NULL)
				*err = rc;

			return DSHR_CORRUPT;
		}

		return DSHR_NEED_COMMIT;
	}
	case -DER_NONEXIST:
		/* Someone (the DTX owner or batched commit ULT) may have
		 * committed or aborted the DTX during we handling other
		 * DTXs. So double check the status before next action.
		 */
		rc = vos_dtx_check(cont->sc_hdl, &dte->dte_xid,
				   NULL, NULL, NULL, NULL, false);

		/* Skip this DTX that it may has been committed or aborted. */
		if (rc == DTX_ST_COMMITTED || rc == DTX_ST_COMMITTABLE ||
		    rc == -DER_NONEXIST)
			D_GOTO(out, rc = DSHR_COMMITTED);

		/* Skip this DTX if failed to get the status. */
		if (rc != DTX_ST_PREPARED) {
			D_WARN("Not sure about whether the DTX "DF_DTI
			       " can be abort or not: %d, skip it.\n",
			       DP_DTI(&dte->dte_xid), rc);
			D_GOTO(out, rc = (rc > 0 ? -DER_IO : rc));
		}

		/* To be aborted. It is possible that the client has resent
		 * related RPC to the new leader, but such DTX is still not
		 * committable yet. Here, the resync logic will abort it by
		 * race during the new leader waiting for other replica(s).
		 * The dtx_abort() logic will abort the local DTX firstly.
		 * When the leader get replies from other replicas, it will
		 * check whether local DTX is still valid or not.
		 *
		 * If we abort multiple non-ready DTXs together, then there
		 * is race that one DTX may become committable when we abort
		 * some other DTX(s). To avoid complex rollback logic, let's
		 * abort the DTXs one by one, not batched.
		 */
		rc = dtx_abort(cont, epoch, &dte, 1);

		D_DEBUG(DB_TRACE,
			"As the new leader for TX "DF_DTI", abort it: "
			DF_RC"\n", DP_DTI(&dte->dte_xid), DP_RC(rc));

		if (rc < 0) {
			if (err != NULL)
				*err = rc;

			return DSHR_ABORT_FAILED;
		}

		return DSHR_ABORTED;
	default:
		D_WARN("Not sure about whether the DTX "DF_DTI
		       " can be committed or not: %d, skip it.\n",
		       DP_DTI(&dte->dte_xid), rc);
		if (rc > 0)
			rc = -DER_IO;
		break;
	}

out:
	return rc;
}

static int
dtx_status_handle(struct dtx_resync_args *dra)
{
	struct ds_cont_child		*cont = dra->cont;
	struct dtx_resync_head		*drh = &dra->tables;
	struct dtx_resync_entry		*dre;
	struct dtx_resync_entry		*next;
	struct ds_pool			*pool = cont->sc_pool->spc_pool;
	int				*tgt_array = NULL;
	int				 tgt_cnt;
	int				 count = 0;
	int				 err = 0;
	int				 rc;

	if (drh->drh_count == 0)
		goto out;

	ABT_rwlock_wrlock(pool->sp_lock);
	tgt_cnt = pool_map_target_nr(pool->sp_map);
	ABT_rwlock_unlock(pool->sp_lock);
	D_ASSERT(tgt_cnt != 0);

	D_ALLOC_ARRAY(tgt_array, tgt_cnt);
	if (tgt_array == NULL)
		D_GOTO(out, err = -DER_NOMEM);

	d_list_for_each_entry_safe(dre, next, &drh->drh_list, dre_link) {
		if (cont->sc_closing)
			goto out;

		if (dre->dre_dte.dte_mbs->dm_dte_flags & DTE_LEADER)
			goto commit;

		rc = dtx_is_leader(pool, dra, dre);
		if (rc <= 0) {
			if (rc < 0)
				D_WARN("Not sure about the leader for the DTX "
				       DF_DTI" (ver = %u): rc = %d, skip it.\n",
				       DP_DTI(&dre->dre_xid), dra->version, rc);
			else
				D_DEBUG(DB_TRACE, "Not the leader for the DTX "
					DF_DTI" (ver = %u) skip it.\n",
					DP_DTI(&dre->dre_xid), dra->version);
			dtx_dre_release(drh, dre);
			continue;
		}

		rc = dtx_status_handle_one(cont, &dre->dre_dte, dre->dre_epoch,
					   tgt_array, &err);
		switch (rc) {
		case DSHR_NEED_COMMIT:
			goto commit;
		case DSHR_NEED_RETRY:
			d_list_del(&dre->dre_link);
			d_list_add_tail(&dre->dre_link, &drh->drh_list);
			continue;
		case DSHR_COMMITTED:
		case DSHR_ABORTED:
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
	}

out:
	D_FREE(tgt_array);

	while ((dre = d_list_pop_entry(&drh->drh_list, struct dtx_resync_entry,
				       dre_link)) != NULL)
		dtx_dre_release(drh, dre);

	if (err >= 0 && !cont->sc_closing)
		/* Drain old committable DTX to help subsequent rebuild. */
		err = dtx_obj_sync(cont, NULL, dra->epoch);

	return err;
}

static int
dtx_iter_cb(uuid_t co_uuid, vos_iter_entry_t *ent, void *args)
{
	struct dtx_resync_args		*dra = args;
	struct dtx_resync_entry		*dre;
	struct dtx_entry		*dte;
	struct dtx_memberships		*mbs;
	size_t				 size;

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

	if (dra->resync_all) {
		/* For open container. */
		if (ent->ie_dtx_flags & DTE_LEADER) {
			/* Leader: handle the DTX that happened before current
			 * DTX resync.
			 */
			if (ent->ie_epoch < dra->epoch)
				return 0;
		} else {
			/* Non-leader: handle the DTX with old version. */
			if (ent->ie_dtx_ver >= dra->version)
				return 0;
		}
	} else {
		/* For pool map refresh. */
		/* Leader: do nothing. */
		if (ent->ie_dtx_flags & DTE_LEADER)
			return 0;

		/* Non-leader: handle the DTX with old version. */
		if (ent->ie_dtx_ver >= dra->version)
			return 0;
	}

	D_ASSERT(ent->ie_dtx_mbs_dsize > 0);
	D_ASSERT(ent->ie_dtx_tgt_cnt > 0);

	size = sizeof(*dre) + sizeof(*mbs) + ent->ie_dtx_mbs_dsize;
	D_ALLOC(dre, size);
	if (dre == NULL)
		return -DER_NOMEM;

	dre->dre_epoch = ent->ie_epoch;
	dre->dre_oid = ent->ie_dtx_oid;
	dre->dre_dkey_hash = ent->ie_dkey_hash;

	dte = &dre->dre_dte;
	mbs = (struct dtx_memberships *)(dte + 1);

	mbs->dm_tgt_cnt = ent->ie_dtx_tgt_cnt;
	mbs->dm_grp_cnt = ent->ie_dtx_grp_cnt;
	mbs->dm_data_size = ent->ie_dtx_mbs_dsize;
	mbs->dm_flags = ent->ie_dtx_mbs_flags;
	mbs->dm_dte_flags = ent->ie_dtx_flags;
	memcpy(mbs->dm_data, ent->ie_dtx_mbs, ent->ie_dtx_mbs_dsize);

	dte->dte_xid = ent->ie_dtx_xid;
	dte->dte_ver = ent->ie_dtx_ver;
	dte->dte_refs = 1;
	dte->dte_mbs = mbs;

	d_list_add_tail(&dre->dre_link, &dra->tables.drh_list);
	dra->tables.drh_count++;

	return 0;
}

int
dtx_resync(daos_handle_t po_hdl, uuid_t po_uuid, uuid_t co_uuid, uint32_t ver,
	   bool block, bool resync_all)
{
	struct ds_cont_child		*cont = NULL;
	struct dtx_resync_args		 dra = { 0 };
	d_rank_t			 myrank;
	int				 rc = 0;
	int				 rc1 = 0;
	bool				 resynced = false;

	rc = ds_cont_child_lookup(po_uuid, co_uuid, &cont);
	if (rc != 0) {
		D_ERROR("Failed to open container for resync DTX "
			DF_UUID"/"DF_UUID": rc = %d\n",
			DP_UUID(po_uuid), DP_UUID(co_uuid), rc);
		return rc;
	}

	ABT_mutex_lock(cont->sc_mutex);

	while (cont->sc_dtx_resyncing) {
		if (!block) {
			ABT_mutex_unlock(cont->sc_mutex);
			goto out;
		}
		D_DEBUG(DB_TRACE, "Waiting for resync of "DF_UUID"\n",
			DP_UUID(co_uuid));
		ABT_cond_wait(cont->sc_dtx_resync_cond, cont->sc_mutex);
		resynced = true;
	}
	if (resynced || /* Someone just did the DTX resync*/
	    cont->sc_closing) {
		ABT_mutex_unlock(cont->sc_mutex);
		goto out;
	}

	crt_group_rank(NULL, &myrank);
	if (myrank == daos_fail_value_get() &&
	    DAOS_FAIL_CHECK(DAOS_DTX_SRV_RESTART)) {
		dss_set_start_epoch();
		vos_dtx_cache_reset(cont->sc_hdl);
	}

	cont->sc_dtx_resyncing = 1;
	cont->sc_dtx_resync_ver = ver;
	ABT_mutex_unlock(cont->sc_mutex);

	dra.cont = cont;
	dra.version = ver;
	dra.epoch = crt_hlc_get();
	if (resync_all)
		dra.resync_all = 1;
	else
		dra.resync_all = 0;
	D_INIT_LIST_HEAD(&dra.tables.drh_list);
	dra.tables.drh_count = 0;

	D_DEBUG(DB_TRACE, "resync DTX scan "DF_UUID"/"DF_UUID" start.\n",
		DP_UUID(po_uuid), DP_UUID(co_uuid));

	rc = ds_cont_iter(po_hdl, co_uuid, dtx_iter_cb, &dra, VOS_ITER_DTX, 0);

	/* Handle the DTXs that have been scanned even if some failure happened
	 * in above ds_cont_iter() step.
	 */
	rc1 = dtx_status_handle(&dra);

	D_ASSERT(d_list_empty(&dra.tables.drh_list));

	if (rc >= 0)
		rc = rc1;

	D_DEBUG(DB_TRACE, "resync DTX scan "DF_UUID"/"DF_UUID" stop: rc = %d\n",
		DP_UUID(po_uuid), DP_UUID(co_uuid), rc);

	ABT_mutex_lock(cont->sc_mutex);
	cont->sc_dtx_resyncing = 0;
	ABT_cond_broadcast(cont->sc_dtx_resync_cond);
	ABT_mutex_unlock(cont->sc_mutex);

out:
	ds_cont_child_put(cont);
	return rc;
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
	rc = dtx_resync(iter_param->ip_hdl, arg->pool_uuid, entry->ie_couuid,
			arg->version, true, false);
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
	vos_iter_param_t		 param = { 0 };
	struct vos_iter_anchors		 anchor = { 0 };
	struct dtx_container_scan_arg	 cb_arg = { 0 };
	int				 rc;

	child = ds_pool_child_lookup(arg->pool_uuid);
	if (child == NULL)
		D_GOTO(out, rc = -DER_NONEXIST);

	cb_arg.arg = *arg;
	param.ip_hdl = child->spc_hdl;
	param.ip_flags = VOS_IT_FOR_MIGRATION;
	rc = vos_iterate(&param, VOS_ITER_COUUID, false, &anchor,
			 container_scan_cb, NULL, &cb_arg, NULL);

	ds_pool_child_put(child);
out:
	D_DEBUG(DB_TRACE, DF_UUID" iterate pool done: rc %d\n",
		DP_UUID(arg->pool_uuid), rc);

	return rc;
}

void
dtx_resync_ult(void *data)
{
	struct dtx_scan_args	*arg = data;
	struct ds_pool		*pool;
	int			rc = 0;

	pool = ds_pool_lookup(arg->pool_uuid);
	D_ASSERT(pool != NULL);
	if (pool->sp_dtx_resync_version >= arg->version) {
		D_DEBUG(DB_MD, DF_UUID" ignore dtx resync version %u/%u\n",
			DP_UUID(arg->pool_uuid), pool->sp_dtx_resync_version,
			arg->version);
		D_GOTO(out_put, rc);
	}
	D_DEBUG(DB_MD, DF_UUID" update dtx resync version %u->%u\n",
		DP_UUID(arg->pool_uuid), pool->sp_dtx_resync_version,
		arg->version);

	rc = dss_thread_collective(dtx_resync_one, arg, 0);
	if (rc) {
		/* If dtx resync fails, then let's still update
		 * sp_dtx_resync_version, so the rebuild can go ahead,
		 * though it might fail, instead of hanging here.
		 */
		D_ERROR("dtx resync collective "DF_UUID" %d.\n",
			DP_UUID(arg->pool_uuid), rc);
	}
	pool->sp_dtx_resync_version = arg->version;
out_put:
	ds_pool_put(pool);
	D_FREE_PTR(arg);
}
