/**
 * (C) Copyright 2019 Intel Corporation.
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
	struct dtx_entry	dre_dte;
	daos_epoch_t		dre_epoch;
	uint64_t		dre_hash;
	uint32_t		dre_intent;
	uint32_t		dre_in_cache:1;
};

#define dre_oid		dre_dte.dte_oid
#define dre_xid		dre_dte.dte_xid

struct dtx_resync_head {
	d_list_t		drh_list;
	int			drh_count;
};

struct dtx_resync_args {
	struct ds_cont_child	*cont;
	uuid_t			 po_uuid;
	struct dtx_resync_head	 tables;
	uint32_t		 version;
};

static inline void
dtx_dre_release(struct dtx_resync_head *drh, struct dtx_resync_entry *dre)
{
	drh->drh_count--;
	d_list_del(&dre->dre_link);
	D_FREE_PTR(dre);
}

static int
dtx_resync_commit(uuid_t po_uuid, struct ds_cont_child *cont,
		  struct dtx_resync_head *drh, int count, uint32_t version)
{
	struct dtx_resync_entry		*dre;
	struct dtx_entry		*dte = NULL;
	int				 rc = 0;
	int				 i = 0;
	int				 j = 0;

	D_ASSERT(drh->drh_count >= count);

	D_ALLOC_ARRAY(dte, count);
	if (dte == NULL)
		return -DER_NOMEM;

	for (i = 0; i < count; i++) {
		dre = d_list_entry(drh->drh_list.next,
				   struct dtx_resync_entry, dre_link);
		/* Someone (the DTX owner or batched commit ULT) may have
		 * committed or aborted the DTX during we handling other
		 * DTXs. So double check the on-disk status before current
		 * commit.
		 */
		rc = vos_dtx_check(cont->sc_hdl, &dre->dre_xid);

		/* Skip this DTX since it has been committed or aggregated. */
		if (rc == DTX_ST_COMMITTED || rc == -DER_NONEXIST)
			goto next;

		if (rc != DTX_ST_PREPARED) {
			/* If we failed to check the on-disk status, commit
			 * it again, that is harmless. But we cannot add it
			 * to CoS cache.
			 */
			D_WARN("Fail to check DTX "DF_DTI" status: %d.\n",
			       DP_DTI(&dre->dre_xid), rc);
			goto commit;
		}

		if (dre->dre_in_cache)
			goto commit;

		rc = vos_dtx_lookup_cos(cont->sc_hdl, &dre->dre_oid,
					&dre->dre_xid, dre->dre_hash,
					dre->dre_intent == DAOS_INTENT_PUNCH ?
					true : false);
		if (rc == -DER_NONEXIST) {
			rc = vos_dtx_add_cos(cont->sc_hdl, &dre->dre_oid,
				&dre->dre_xid, dre->dre_hash, dre->dre_epoch, 0,
				dre->dre_intent == DAOS_INTENT_PUNCH ?
				true : false, false);
			if (rc < 0)
				D_WARN("Fail to add DTX "DF_DTI" to CoS cache: "
				       "rc = %d\n",  DP_DTI(&dre->dre_xid), rc);
		}

commit:
		dte[j].dte_xid = dre->dre_xid;
		dte[j].dte_oid = dre->dre_oid;
		++j;

next:
		dtx_dre_release(drh, dre);
	}

	if (j > 0) {
		rc = dtx_commit(po_uuid, cont->sc_uuid, dte, j, version);
		if (rc < 0)
			D_ERROR("Failed to commit the DTXs: rc = %d\n", rc);
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
	struct pl_obj_layout		*layout = NULL;
	struct dtx_resync_head		*drh = &dra->tables;
	struct dtx_resync_entry		*dre;
	struct dtx_resync_entry		*next;
	int				 count = 0;
	int				 err = 0;
	int				 rc;

	if (drh->drh_count == 0)
		return 0;

	d_list_for_each_entry_safe(dre, next, &drh->drh_list, dre_link) {
		if (layout != NULL) {
			pl_obj_layout_free(layout);
			layout = NULL;
		}

		rc = vos_dtx_lookup_cos(cont->sc_hdl, &dre->dre_oid,
					&dre->dre_xid, dre->dre_hash,
					dre->dre_intent == DAOS_INTENT_PUNCH ?
					true : false);
		/* If it is in CoS cache, no need to check remote replicas. */
		if (rc == 0) {
			dre->dre_in_cache = 1;
			goto commit;
		}

		rc = ds_pool_check_leader(dra->po_uuid, &dre->dre_oid,
					  dra->version, &layout);
		if (rc <= 0) {
			if (rc < 0)
				D_WARN("Not sure about the leader for the DTX "
				       DF_UOID"/"DF_DTI" (ver = %u): rc = %d, "
				       "skip it.\n",
				       DP_UOID(dre->dre_oid),
				       DP_DTI(&dre->dre_xid), dra->version, rc);
			else
				D_DEBUG(DB_TRACE, "Not the leader for the DTX "
					DF_UOID"/"DF_DTI" (ver = %u) skip it\n",
					DP_UOID(dre->dre_oid),
					DP_DTI(&dre->dre_xid), dra->version);
			dtx_dre_release(drh, dre);
			continue;
		}

		rc = dtx_check(dra->po_uuid, cont->sc_uuid,
			       &dre->dre_dte, layout);

		/* The DTX has been committed (or) ready to be committed on
		 * some remote replica(s), let's commit the it globally.
		 */
		if (rc == DTX_ST_COMMITTED || rc == DTX_ST_PREPARED)
			goto commit;

		if (rc != -DER_NONEXIST) {
			/* We are not sure about whether the DTX can be
			 * committed or not, then we have to skip it.
			 */
			D_WARN("Not sure about whether the DTX "DF_UOID
			       "/"DF_DTI" can be committed or not: %d\n",
			       DP_UOID(dre->dre_oid),
			       DP_DTI(&dre->dre_xid), rc);
			dtx_dre_release(drh, dre);
			continue;
		}

		/* Someone (the DTX owner or batched commit ULT) may have
		 * committed or aborted the DTX during we handling other
		 * DTXs. So double check the on-disk status before current
		 * commit.
		 */
		rc = vos_dtx_check(cont->sc_hdl, &dre->dre_xid);

		/* Skip this DTX since it has been committed or aborted or
		 * fail to get the status.
		 */
		if (rc != DTX_ST_PREPARED) {
			if (rc < 0 && rc != -DER_NONEXIST)
				D_WARN("Not sure about whether the DTX "DF_UOID
				       "/"DF_DTI" can be abort or not: %d\n",
				       DP_UOID(dre->dre_oid),
				       DP_DTI(&dre->dre_xid), rc);
			dtx_dre_release(drh, dre);
			continue;
		}

		rc = vos_dtx_lookup_cos(cont->sc_hdl, &dre->dre_oid,
					&dre->dre_xid, dre->dre_hash,
					dre->dre_intent == DAOS_INTENT_PUNCH ?
					true : false);
		if (rc == 0) {
			dre->dre_in_cache = 1;
			goto commit;
		}

		if (rc == -DER_NONEXIST) {
			/* If we abort multiple non-ready DTXs together, then
			 * there is race that one DTX may become committable
			 * when we abort some other DTX(s). To avoid complex
			 * rollback logic, let's abort the DTXs one by one.
			 */
			rc = dtx_abort(dra->po_uuid, cont->sc_uuid,
				       dre->dre_epoch, &dre->dre_dte, 1,
				       dra->version);
			if (rc < 0)
				err = rc;
		}

		dtx_dre_release(drh, dre);
		continue;

commit:
		if (++count >= DTX_THRESHOLD_COUNT) {
			rc = dtx_resync_commit(dra->po_uuid, cont, drh, count,
					       dra->version);
			if (rc < 0)
				err = rc;
			count = 0;
		}
	}

	if (count > 0) {
		rc = dtx_resync_commit(dra->po_uuid, cont, drh, count,
				       dra->version);
		if (rc < 0)
			err = rc;
	}

	if (layout != NULL)
		pl_obj_layout_free(layout);

	return err;
}

static int
dtx_iter_cb(uuid_t co_uuid, vos_iter_entry_t *ent, void *args)
{
	struct dtx_resync_args		*dra = args;
	struct dtx_resync_entry		*dre;

	/* We commit the DTXs periodically, there will be not too many DTXs
	 * to be checked when resync. So we can load all those uncommitted
	 * DTXs in RAM firstly, then check the state one by one. That avoid
	 * the race trouble between iteration of active-DTX tree and commit
	 * (or abort) the DTXs (that will change the active-DTX tree).
	 */

	D_ALLOC_PTR(dre);
	if (dre == NULL)
		return -DER_NOMEM;

	dre->dre_epoch = ent->ie_epoch;
	dre->dre_xid = ent->ie_xid;
	dre->dre_oid = ent->ie_oid;
	dre->dre_intent = ent->ie_dtx_intent;
	dre->dre_hash = ent->ie_dtx_hash;
	d_list_add_tail(&dre->dre_link, &dra->tables.drh_list);
	dra->tables.drh_count++;

	return 0;
}

int
dtx_resync(daos_handle_t po_hdl, uuid_t po_uuid, uuid_t co_uuid, uint32_t ver,
	   bool block)
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
	    cont->sc_destroying) { /* pool is being destroyed */
		ABT_mutex_unlock(cont->sc_mutex);
		goto out;
	}
	cont->sc_dtx_resyncing = 1;
	ABT_mutex_unlock(cont->sc_mutex);

	rc = vos_dtx_update_resync_gen(cont->sc_hdl);
	if (rc != 0)
		goto fail;

	dra.cont = cont;
	uuid_copy(dra.po_uuid, po_uuid);
	dra.version = ver;
	D_INIT_LIST_HEAD(&dra.tables.drh_list);
	dra.tables.drh_count = 0;

	D_DEBUG(DB_TRACE, "resync DTX scan "DF_UUID"/"DF_UUID" start.\n",
		DP_UUID(po_uuid), DP_UUID(co_uuid));

	rc = ds_cont_iter(po_hdl, co_uuid, dtx_iter_cb, &dra, VOS_ITER_DTX);

	/* Handle the DTXs that have been scanned even if some failure happend
	 * in above ds_cont_iter() step.
	 */
	rc1 = dtx_status_handle(&dra);

	D_ASSERT(d_list_empty(&dra.tables.drh_list));

	if (rc >= 0)
		rc = rc1;

fail:
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
