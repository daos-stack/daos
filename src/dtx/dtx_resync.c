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
	struct daos_tx_entry	dre_dte;
	uint64_t		dre_hash;
	uint32_t		dre_intent;
};

#define dre_oid		dre_dte.dte_oid
#define dre_xid		dre_dte.dte_xid

struct dtx_resync_head {
	d_list_t		drh_list;
	int			drh_count;
};

struct dtx_resync_args {
	struct ds_cont		*cont;
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
dtx_resync_commit(uuid_t po_uuid, struct ds_cont *cont,
		  struct dtx_resync_head *drh, int count, uint32_t version,
		  bool block)
{
	struct dtx_resync_entry		*dre;
	int				 rc = 0;
	int				 i = 0;

	D_ASSERT(drh->drh_count >= count);

	/* If we are in block mode, then we need to commit the DTXs as fast as
	 * possible, so just add them into the CoS cache, then the general DTX
	 * commit mechanism will commit them some time later.
	 */
	if (block) {
		int	err;

		do {
			dre = d_list_entry(drh->drh_list.next,
					   struct dtx_resync_entry, dre_link);
			if (dre->dre_hash == 0)
				/* For punch object or the case that the dkey
				 * hash is just zero, commit them immediately
				 * without caching.
				 */
				err = dtx_commit(po_uuid, cont->sc_uuid,
						 &dre->dre_dte, 1, version);
			else
				err = vos_dtx_add_cos(cont->sc_hdl,
					&dre->dre_oid, &dre->dre_xid,
					dre->dre_hash, dre->dre_intent ==
					DAOS_INTENT_PUNCH ? true : false);
			if (err != 0) {
				D_ERROR("Failed to %s the DTX " DF_UOID"/"DF_DTI
					": rc = %d\n",
					dre->dre_hash == 0 ? "commit" : "cache",
					DP_UOID(dre->dre_oid),
					DP_DTI(&dre->dre_xid), err);
				rc = err;
			}
			dtx_dre_release(drh, dre);
		} while (++i < count);
	} else {
		struct daos_tx_entry	*dte = NULL;

		D_ALLOC_ARRAY(dte, count);
		if (dte == NULL)
			return -DER_NOMEM;

		do {
			dre = d_list_entry(drh->drh_list.next,
					   struct dtx_resync_entry, dre_link);
			dte[i].dte_xid = dre->dre_xid;
			dte[i].dte_oid = dre->dre_oid;
			dtx_dre_release(drh, dre);
		} while (++i < count);

		rc = dtx_commit(po_uuid, cont->sc_uuid, dte, count, version);
		if (rc < 0)
			D_ERROR("Failed to commit the DTX: rc = %d\n", rc);
		D_FREE(dte);
	}

	return rc > 0 ? 0 : rc;
}

static int
dtx_status_handle(struct dtx_resync_args *dra, bool block)
{
	struct ds_cont			*cont = dra->cont;
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
		int	local_rc;

		if (layout != NULL) {
			pl_obj_layout_free(layout);
			layout = NULL;
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
		if (rc != DTX_ST_COMMITTED && rc != DTX_ST_PREPARED &&
		    rc != DTX_ST_INIT) {
			/* We are not sure about whether the DTX can be
			 * committed or not, then we have to skip it.
			 */
			D_WARN("Not sure about whether the DTX "DF_UOID
			       "/"DF_DTI" can be committed or not: %d (1)\n",
			       DP_UOID(dre->dre_oid),
			       DP_DTI(&dre->dre_xid), rc);
			dtx_dre_release(drh, dre);
			continue;
		}

		/* There is CPU yield during DTX operation (check/commit/abort)
		 * remotely. Then the other DTXs may become committable by race.
		 * So re-check the remaining DTXs status.
		 */
		local_rc = vos_dtx_lookup_cos(cont->sc_hdl, &dre->dre_oid,
			&dre->dre_xid, dre->dre_hash,
			dre->dre_intent == DAOS_INTENT_PUNCH ? true : false);
		if (local_rc == 0) {
			/* The DTX is in CoS cache (committable), do nothing. */
			dtx_dre_release(drh, dre);
			continue;
		}

		/* The DTX is not in CoS cache, but it has been committed on
		 * some remote replica(s), then let's commit the it globally.
		 */
		if (rc == DTX_ST_COMMITTED)
			goto commit;

		if (local_rc != -DER_NONEXIST) {
			D_WARN("Not sure about whether the DTX "DF_UOID
			       "/"DF_DTI" can be committed or not: %d (2)\n",
			       DP_UOID(dre->dre_oid),
			       DP_DTI(&dre->dre_xid), rc);
			dtx_dre_release(drh, dre);
			continue;
		}

		/* It is possible that other ULT has committed the DTX during
		 * we handle other DTXs, then such DTX will not be in the CoS
		 * cache. Let's re-check the on-disk DTX table.
		 *
		 * Set the @dkey_hash parameter as zero, then it will skip CoS
		 * cache since we just did that in above check.
		 */
		local_rc = vos_dtx_check_committable(cont->sc_hdl,
					&dre->dre_oid, &dre->dre_xid, 0, false);
		switch (local_rc) {
		case DTX_ST_COMMITTED:
			/* The DTX has been committed by other, do nothing. */
			dtx_dre_release(drh, dre);
			continue;
		case DTX_ST_PREPARED:
			/* Both local and remote replicas are 'prepared', then
			 * it is committable.
			 */
			if (rc == DTX_ST_PREPARED)
				goto commit;

			/* Fall through. */
		case DTX_ST_INIT:
			/* If we abort multiple non-ready DTXs together, then
			 * there is race that one DTX may become committable
			 * when we abort some other DTX(s). To avoid complex
			 * rollback logic, let's abort the DTXs one by one.
			 */
			rc = dtx_abort(dra->po_uuid, cont->sc_uuid,
				       &dre->dre_dte, 1, dra->version);
			dtx_dre_release(drh, dre);
			if (rc < 0)
				err = rc;
			continue;
		default:
			D_WARN("Not sure about whether the DTX "DF_UOID
			       "/"DF_DTI" can be committed or not: %d (3)\n",
			       DP_UOID(dre->dre_oid),
			       DP_DTI(&dre->dre_xid), rc);
			dtx_dre_release(drh, dre);
			continue;
		}

commit:
		if (++count >= DTX_THRESHOLD_COUNT) {
			rc = dtx_resync_commit(dra->po_uuid, cont,
					       drh, count, dra->version, block);
			if (rc < 0)
				err = rc;
			count = 0;
		}
	}

	if (count > 0) {
		rc = dtx_resync_commit(dra->po_uuid, cont, drh, count,
				       dra->version, block);
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

	/* Ignore new DTX after the rebuild/recovery start.
	 *
	 * XXX: The time(NULL) based checking may be untrusted because of
	 *	potential clock drift. We may consider to replace it with
	 *	hybrid-clock based mechanism in future when it is ready.
	 */
	if (ent->ie_dtx_sec > dra->cont->sc_dtx_resync_time)
		return 0;

	/* We commit the DTXs periodically, there will be not too many DTXs
	 * to be checked when resync. So we can load all those uncommitted
	 * DTXs in RAM firstly, then check the state one by one. That avoid
	 * the race trouble between iteration of active-DTX tree and commit
	 * (or abort) the DTXs (that will change the active-DTX tree).
	 */

	D_ALLOC_PTR(dre);
	if (dre == NULL)
		return -DER_NOMEM;

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
	struct ds_cont			*cont = NULL;
	struct dtx_resync_args		 dra = { 0 };
	struct dtx_resync_entry		*dre;
	struct dtx_resync_entry		*next;
	int				 rc = 0;
	int				 rc1 = 0;
	bool				 shared = false;

	rc = ds_cont_lookup(po_uuid, co_uuid, &cont);
	if (rc != 0) {
		D_ERROR("Failed to open container for resync DTX "
			DF_UUID"/"DF_UUID": rc = %d\n",
			DP_UUID(po_uuid), DP_UUID(co_uuid), rc);
		return rc;
	}

	while (cont->sc_dtx_resyncing) {
		if (!block)
			goto out;

		shared = true;
		/* Someone is resyncing the DTXs, let's wait and retry. */
		ABT_thread_yield();
	}

	/* Some others just did the DTX resync, needs not to repeat. */
	if (shared)
		goto out;

	cont->sc_dtx_resyncing = 1;
	cont->sc_dtx_resync_time = time(NULL);

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
	rc1 = dtx_status_handle(&dra, block);

	d_list_for_each_entry_safe(dre, next, &dra.tables.drh_list, dre_link)
		dtx_dre_release(&dra.tables, dre);

	if (rc >= 0)
		rc = rc1;

	D_DEBUG(DB_TRACE, "resync DTX scan "DF_UUID"/"DF_UUID" stop: rc = %d\n",
		DP_UUID(po_uuid), DP_UUID(co_uuid), rc);

	cont->sc_dtx_resyncing = 0;

out:
	ds_cont_put(cont);
	return rc;
}
