/**
 * (C) Copyright 2019-2020 Intel Corporation.
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
/**
 * dtx: resync DTX status
 */
#define D_LOGFAC	DD_FAC(dtx)

#include <daos/placement.h>
#include <daos/pool_map.h>
#include <daos_srv/vos.h>
#include <daos_srv/pool.h>
#include <daos_srv/dtx_srv.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/container.h>
#include <abt.h>
#include "dtx_internal.h"

struct dtx_resync_entry {
	d_list_t		dre_link;
	daos_epoch_t		dre_epoch;
	daos_unit_oid_t		dre_oid;
	struct dtx_entry	dre_dte;
};

#define dre_xid		dre_dte.dte_xid

struct dtx_resync_head {
	d_list_t		drh_list;
	int			drh_count;
};

struct dtx_resync_args {
	struct ds_cont_child	*cont;
	uuid_t			 po_uuid;
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
dtx_resync_commit(uuid_t po_uuid, struct ds_cont_child *cont,
		  struct dtx_resync_head *drh, int count)
{
	struct dtx_resync_entry		 *dre;
	struct dtx_entry		**dte = NULL;
	int				  rc = 0;
	int				  i = 0;
	int				  j = 0;

	D_ASSERT(drh->drh_count >= count);

	D_ALLOC_ARRAY(dte, count);
	if (dte == NULL)
		return -DER_NOMEM;

	for (i = 0; i < count; i++) {
		dre = d_list_entry(drh->drh_list.next,
				   struct dtx_resync_entry, dre_link);

		/* Someone (the DTX owner or batched commit ULT) may have
		 * committed or aborted the DTX during we handling other
		 * DTXs. So double check the status before current commit.
		 */
		rc = vos_dtx_check(cont->sc_hdl, &dre->dre_xid,
				   NULL, NULL, false);

		/* Skip this DTX since it has been committed or aggregated. */
		if (rc == DTX_ST_COMMITTED || rc == -DER_NONEXIST)
			goto next;

		/* If we failed to check the status, then assume that it is
		 * not committed, then commit it (again), that is harmless.
		 */

		dte[j++] = dtx_entry_get(&dre->dre_dte);

next:
		dtx_dre_release(drh, dre);
	}

	if (j > 0) {
		rc = dtx_commit(po_uuid, cont->sc_uuid, dte, j, false);
		if (rc < 0)
			D_ERROR("Failed to commit the DTXs: rc = "DF_RC"\n",
				DP_RC(rc));

		for (i = 0; i < j; i++) {
			D_ASSERT(dte[i]->dte_refs == 1);

			dre = d_list_entry(dte[i], struct dtx_resync_entry,
					   dre_dte);
			D_FREE(dre);
		}
	} else {
		rc = 0;
	}

	D_FREE(dte);
	return rc;
}

static int
dtx_status_handle(struct dtx_resync_args *dra)
{
	struct ds_cont_child		*cont = dra->cont;
	struct dtx_resync_head		*drh = &dra->tables;
	struct dtx_resync_entry		*dre;
	struct dtx_resync_entry		*next;
	struct dtx_entry		*dte;
	int				 count = 0;
	int				 err = 0;
	int				 rc;

	if (drh->drh_count == 0)
		goto out;

	d_list_for_each_entry_safe(dre, next, &drh->drh_list, dre_link) {
		if (dre->dre_dte.dte_mbs->dm_grp_cnt > 1) {
			D_WARN("Not support to recover the DTX across more "
			       "1 modification groups %d, skip it "DF_DTI"\n",
			       dre->dre_dte.dte_mbs->dm_grp_cnt,
			       DP_DTI(&dre->dre_xid));
			dtx_dre_release(drh, dre);
			continue;
		}

		rc = ds_pool_check_leader(dra->po_uuid, &dre->dre_oid,
					  dra->version);
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

		rc = dtx_check(dra->po_uuid, cont->sc_uuid, &dre->dre_dte);

		/* The DTX has been committed or ready to be committed on
		 * some remote replica(s), let's commit the DTX globally.
		 */
		if (rc == DTX_ST_COMMITTED || rc == DTX_ST_PREPARED)
			goto commit;

		if (rc != -DER_NONEXIST) {
			D_WARN("Not sure about whether the DTX "DF_DTI
			       " can be committed or not: %d, skip it.\n",
			       DP_DTI(&dre->dre_xid), rc);
			dtx_dre_release(drh, dre);
			continue;
		}

		/* Someone (the DTX owner or batched commit ULT) may have
		 * committed or aborted the DTX during we handling other
		 * DTXs. So double check the status before next action.
		 */
		rc = vos_dtx_check(cont->sc_hdl, &dre->dre_xid,
				   NULL, NULL, false);

		/* Skip this DTX that it may has been committed or aborted. */
		if (rc == DTX_ST_COMMITTED || rc == -DER_NONEXIST) {
			dtx_dre_release(drh, dre);
			continue;
		}

		/* Skip this DTX if failed to get the status. */
		if (rc != DTX_ST_PREPARED) {
			D_WARN("Not sure about whether the DTX "DF_DTI
			       " can be abort or not: %d, skip it.\n",
			       DP_DTI(&dre->dre_xid), rc);
			dtx_dre_release(drh, dre);
			continue;
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
		dte = &dre->dre_dte;
		rc = dtx_abort(dra->po_uuid, cont->sc_uuid, dre->dre_epoch,
			       &dte, 1);
		if (rc < 0)
			err = rc;

		dtx_dre_release(drh, dre);
		continue;

commit:
		if (++count >= DTX_THRESHOLD_COUNT) {
			rc = dtx_resync_commit(dra->po_uuid, cont, drh, count);
			if (rc < 0)
				err = rc;
			count = 0;
		}
	}

	if (count > 0) {
		rc = dtx_resync_commit(dra->po_uuid, cont, drh, count);
		if (rc < 0)
			err = rc;
	}

out:
	if (err >= 0)
		/* Drain old committable DTX to help subsequent rebuild. */
		err = dtx_obj_sync(dra->po_uuid, cont->sc_uuid, cont,
				   NULL, dra->epoch);

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

	if (ent->ie_dtx_flags & DTE_LEADER && !dra->resync_all)
		return 0;

	/* Only handle the DTX that happened before the DTX resync. */
	if (ent->ie_dtx_ver >= dra->version)
		return 0;

	D_ASSERT(ent->ie_dtx_mbs_dsize > 0);
	D_ASSERT(ent->ie_dtx_tgt_cnt > 0);

	size = sizeof(*dre) + sizeof(*mbs) + ent->ie_dtx_mbs_dsize;
	D_ALLOC(dre, size);
	if (dre == NULL)
		return -DER_NOMEM;

	dre->dre_epoch = ent->ie_epoch;
	dre->dre_oid = ent->ie_dtx_oid;

	dte = &dre->dre_dte;
	mbs = (struct dtx_memberships *)(dte + 1);

	mbs->dm_tgt_cnt = ent->ie_dtx_tgt_cnt;
	mbs->dm_grp_cnt = ent->ie_dtx_grp_cnt;
	mbs->dm_data_size = ent->ie_dtx_mbs_dsize;
	mbs->dm_flags = ent->ie_dtx_mbs_flags;
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
	    cont->sc_stopping) {
		ABT_mutex_unlock(cont->sc_mutex);
		goto out;
	}
	cont->sc_dtx_resyncing = 1;
	cont->sc_dtx_resync_ver = ver;
	ABT_mutex_unlock(cont->sc_mutex);

	dra.cont = cont;
	uuid_copy(dra.po_uuid, po_uuid);
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

	rc = ds_cont_iter(po_hdl, co_uuid, dtx_iter_cb, &dra, VOS_ITER_DTX);

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
	param.ip_flags = VOS_IT_FOR_REBUILD;
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

	rc = dss_thread_collective(dtx_resync_one, arg, 0, DSS_ULT_REBUILD);
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
