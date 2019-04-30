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
 * dtx: DTX common logic
 */
#define D_LOGFAC	DD_FAC(dtx)

#include <abt.h>
#include <uuid/uuid.h>
#include <daos_srv/pool.h>
#include <daos_srv/container.h>
#include <daos_srv/vos.h>
#include <daos_srv/dtx_srv.h>
#include <daos_srv/daos_server.h>
#include "dtx_internal.h"

struct dtx_batched_commit_args {
	d_list_t		 dbca_link;
	struct ds_pool_child	*dbca_pool;
	struct ds_cont		*dbca_cont;
	uint32_t		 dbca_shares;
};

void
dtx_aggregate(void *arg)
{
	struct ds_cont		*cont = arg;

	while (!cont->sc_closing) {
		int	rc;

		rc = vos_dtx_aggregate(cont->sc_hdl, DTX_AGG_YIELD_INTERVAL,
				       DTX_AGG_THRESHOLD_AGE_LOWER);
		if (rc != 0)
			break;

		ABT_thread_yield();
	}

	cont->sc_dtx_aggregating = 0;
	ds_cont_put(cont);
}

static inline void
dtx_free_committable(struct dtx_entry *dtes)
{
	D_FREE(dtes);
}

static inline void
dtx_free_dbca(struct dtx_batched_commit_args *dbca)
{
	d_list_del(&dbca->dbca_link);
	ds_cont_put(dbca->dbca_cont);
	ds_pool_child_put(dbca->dbca_pool);
	D_FREE_PTR(dbca);
}

static void
dtx_flush_committable(struct dss_module_info *dmi,
		      struct dtx_batched_commit_args *dbca)
{
	struct ds_pool_child	*pool = dbca->dbca_pool;
	struct ds_cont		*cont = dbca->dbca_cont;
	int			 rc;

	do {
		struct dtx_entry	*dtes = NULL;

		rc = vos_dtx_fetch_committable(cont->sc_hdl,
					       DTX_THRESHOLD_COUNT, &dtes);
		if (rc <= 0)
			break;

		rc = dtx_commit(pool->spc_uuid, cont->sc_uuid,
				dtes, rc, pool->spc_map_version);
		dtx_free_committable(dtes);
	} while (rc >= 0 && cont->sc_closing);

	if (rc < 0)
		D_ERROR(DF_UUID": Fail to flush CoS cache: rc = %d\n",
			DP_UUID(cont->sc_uuid), rc);

	if (cont->sc_dtx_flush_cbdata != NULL) {
		ABT_future	future = cont->sc_dtx_flush_cbdata;

		rc = ABT_future_set(future, NULL);
		D_ASSERTF(rc == ABT_SUCCESS, "ABT_future_set failed for DTX "
			  "flush on "DF_UUID": rc = %d\n",
			  DP_UUID(cont->sc_uuid), rc);
	}

	if (dbca->dbca_shares == 0) {
		D_ASSERT(cont->sc_closing);

		dtx_free_dbca(dbca);
	} else {
		d_list_move_tail(&dbca->dbca_link, &dmi->dmi_dtx_batched_list);
	}
}

void
dtx_batched_commit(void *arg)
{
	struct dss_module_info		*dmi = dss_get_module_info();
	struct dtx_batched_commit_args	*dbca;

	while (1) {
		ABT_bool			 state;
		struct ds_cont			*cont;
		struct dtx_entry		*dtes = NULL;
		struct dtx_stat			 stat = { 0 };
		int				 rc;

		if (d_list_empty(&dmi->dmi_dtx_batched_list))
			goto check;

		dbca = d_list_entry(dmi->dmi_dtx_batched_list.next,
				    struct dtx_batched_commit_args, dbca_link);
		cont = dbca->dbca_cont;
		if (cont->sc_closing) {
			dtx_flush_committable(dmi, dbca);
			goto check;
		}

		d_list_move_tail(&dbca->dbca_link, &dmi->dmi_dtx_batched_list);
		vos_dtx_stat(cont->sc_hdl, &stat);

		if ((stat.dtx_committable_count > DTX_THRESHOLD_COUNT) ||
		    (stat.dtx_oldest_committable_time != 0 &&
		     dtx_hlc_age2sec(stat.dtx_oldest_committable_time) >
		     DTX_COMMIT_THRESHOLD_AGE)) {
			rc = vos_dtx_fetch_committable(cont->sc_hdl,
						DTX_THRESHOLD_COUNT, &dtes);
			if (rc > 0) {
				rc = dtx_commit(dbca->dbca_pool->spc_uuid,
					cont->sc_uuid, dtes, rc,
					dbca->dbca_pool->spc_map_version);
				dtx_free_committable(dtes);

				if (cont->sc_closing) {
					dtx_flush_committable(dmi, dbca);
					goto check;
				}

				if (!cont->sc_dtx_aggregating)
					vos_dtx_stat(cont->sc_hdl, &stat);
			}
		}

		if (!cont->sc_dtx_aggregating &&
		    ((stat.dtx_committed_count > DTX_AGG_THRESHOLD_CNT) ||
		     (stat.dtx_oldest_committed_time != 0 &&
		      dtx_hlc_age2sec(stat.dtx_oldest_committed_time) >
				DTX_AGG_THRESHOLD_AGE_UPPER))) {
			ds_cont_get(cont);
			cont->sc_dtx_aggregating = 1;
			rc = dss_ult_create(dtx_aggregate, cont,
				DSS_ULT_AGGREGATE, DSS_TGT_SELF, 0, NULL);
			if (rc != 0) {
				cont->sc_dtx_aggregating = 0;
				ds_cont_put(cont);
			}
		}

check:
		rc = ABT_future_test(dmi->dmi_xstream->dx_shutdown, &state);
		D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
		if (state == ABT_TRUE)
			break;

		ABT_thread_yield();
	}

	while (!d_list_empty(&dmi->dmi_dtx_batched_list)) {
		dbca = d_list_entry(dmi->dmi_dtx_batched_list.next,
				    struct dtx_batched_commit_args, dbca_link);
		dtx_free_dbca(dbca);
	}
}

/**
 * Prepare the DTX handle in DRAM.
 *
 * XXX: Currently, we only support to prepare the DTX against single DAOS
 *	object and single dkey.
 *
 * \param dti		[IN]	The DTX identifier.
 * \param oid		[IN]	The target object (shard) ID.
 * \param coh		[IN]	Container open handle.
 * \param epoch		[IN]	Epoch for the DTX.
 * \param dkey_hash	[IN]	Hash of the dkey to be modified if applicable.
 * \param conflict	[IN]	Hash of the dkey to be modified if applicable.
 * \param dti_cos	[IN,OUT]The DTX array to be committed because of shared.
 * \param dti_cos_count [IN,OUT]The @dti_cos array size.
 * \param pm_ver	[IN]	Pool map version for the DTX.
 * \param intent	[IN]	The intent of related modification.
 * \param leader	[IN]	The target (to be modified) is leader or not.
 * \param dth		[OUT]	Pointer to the DTX handle.
 *
 * \return			Zero on success, negative value if error.
 */
int
dtx_begin(struct dtx_id *dti, daos_unit_oid_t *oid, daos_handle_t coh,
	  daos_epoch_t epoch, uint64_t dkey_hash,
	  struct dtx_conflict_entry *conflict, struct dtx_id *dti_cos,
	  int dti_cos_count, uint32_t pm_ver, uint32_t intent, bool leader,
	  struct dtx_handle **dthp)
{
	struct dtx_handle	*dth;

	if (leader) {
		/* XXX: For leader case, we need to find out the potential
		 *	conflict DTXs in the CoS cache, and append them to
		 *	the dispatched RPC to non-leaders. Then non-leader
		 *	replicas can commit them before real modifications
		 *	to avoid availability trouble.
		 */
		D_ASSERT(dti_cos == NULL);
		D_ASSERT(dti_cos_count == 0);

		dti_cos_count = vos_dtx_list_cos(coh, oid, dkey_hash,
				intent == DAOS_INTENT_UPDATE ?
				DCLT_PUNCH : DCLT_PUNCH | DCLT_UPDATE,
				DTX_THRESHOLD_COUNT, &dti_cos);
		if (dti_cos_count < 0)
			return dti_cos_count;

		if (dti_cos_count > 0 && dti_cos == NULL) {
			/* There are too many conflict DTXs to be committed,
			 * as to cannot be taken via the normal IO RPC. The
			 * background dedicated DTXs batched commit ULT has
			 * not committed them in time. Let's retry later.
			 */
			D_DEBUG(DB_TRACE, "Too many pontential conflict DTXs "
				"for the given "DF_DTI", let's retry later.\n",
				DP_DTI(dti));
			return -DER_INPROGRESS;
		}
	}

	D_ALLOC_PTR(dth);
	if (dth == NULL) {
		if (leader && dti_cos != NULL)
			D_FREE(dti_cos);

		return -DER_NOMEM;
	}

	dth->dth_xid = *dti;
	dth->dth_oid = *oid;
	dth->dth_coh = coh;
	dth->dth_epoch = epoch;
	D_INIT_LIST_HEAD(&dth->dth_shares);
	dth->dth_handled_time = crt_hlc_get();
	dth->dth_dkey_hash = dkey_hash;
	dth->dth_ver = pm_ver;
	dth->dth_intent = intent;
	dth->dth_sync = 0;
	dth->dth_leader = (leader ? 1 : 0);
	dth->dth_non_rep = 0;
	dth->dth_dti_cos = dti_cos;
	dth->dth_dti_cos_count = dti_cos_count;
	dth->dth_conflict = conflict;
	dth->dth_ent = UMOFF_NULL;
	dth->dth_obj = UMOFF_NULL;

	*dthp = dth;
	D_DEBUG(DB_TRACE, "Start the DTX "DF_DTI" for object "DF_OID
		" ver %u, dkey %llu, dti_cos_count %d, intent %s, %s\n",
		DP_DTI(&dth->dth_xid), DP_OID(oid->id_pub), dth->dth_ver,
		(unsigned long long)dth->dth_dkey_hash, dti_cos_count,
		dth->dth_intent == DAOS_INTENT_PUNCH ? "Punch" : "Update",
		leader ? "leader" : "non-leader");

	return 0;
}

int
dtx_end(struct dtx_handle *dth, struct ds_cont_hdl *cont_hdl,
	struct ds_cont *cont, int result)
{
	int	rc = 0;

	if (result < 0) {
		if (!dth->dth_leader && dth->dth_dti_cos_count > 0) {
			/* XXX: For non-leader replica, even if we fail to
			 *	make related modification for some reason,
			 *	we still need to commit the DTXs for CoS.
			 *	Because other replica may have already
			 *	committed them. For leader case, it is
			 *	not important even if we miss to commit
			 *	the CoS DTXs, because they are still in
			 *	CoS cache, and can be committed next time.
			 */
			rc = vos_dtx_commit(cont->sc_hdl, dth->dth_dti_cos,
					    dth->dth_dti_cos_count);
			if (rc != 0)
				D_ERROR(DF_UUID": Fail to DTX CoS commit: %d\n",
					DP_UUID(cont->sc_uuid), rc);
		}

		goto fail;
	}

	if (!dth->dth_leader || dth->dth_non_rep ||  dtx_is_null(dth->dth_ent))
		goto out;

	/* If the DTX is started befoe DTX resync operation (for rebuild),
	 * then it is possbile that the DTX resync ULT may have aborted
	 * current DTX before remote replica(s) modification by race. So
	 * let's check DTX status locally before marking as 'committable'.
	 */
	if (dth->dth_handled_time <= cont->sc_dtx_resync_time) {
		rc = vos_dtx_check_committable(cont->sc_hdl, NULL,
					       &dth->dth_xid, 0, false);
		if (rc < 0) {
			result = (rc == -DER_NONEXIST ? -DER_INPROGRESS : rc);
			D_GOTO(fail, result);
		}
	}

	rc = vos_dtx_add_cos(dth->dth_coh, &dth->dth_oid, &dth->dth_xid,
			     dth->dth_dkey_hash, dth->dth_handled_time,
			     dth->dth_intent == DAOS_INTENT_PUNCH ?
			     true : false);
	if (rc != 0) {
		D_ERROR(DF_UUID": Fail to add DTX "DF_DTI" to CoS cache: %d. "
			"Try to commit it sychronously.\n",
			DP_UUID(cont->sc_uuid), DP_DTI(&dth->dth_xid), rc);
		dth->dth_sync = 1;
	}

	if (dth->dth_sync) {
		rc = dtx_commit(cont_hdl->sch_pool->spc_uuid,
				cont->sc_uuid, &dth->dth_dte, 1,
				cont_hdl->sch_pool->spc_map_version);
		if (rc != 0) {
			D_ERROR(DF_UUID": Fail to sync commit DTX "DF_DTI
				": rc = %d\n", DP_UUID(cont->sc_uuid),
				DP_DTI(&dth->dth_xid), rc);
			D_GOTO(fail, result = rc);
		}
	}

fail:
	if (result < 0 && dth->dth_leader)
		dtx_abort(cont_hdl->sch_pool->spc_uuid, cont->sc_uuid,
			  &dth->dth_dte, 1,
			  cont_hdl->sch_pool->spc_map_version);
out:
	D_DEBUG(DB_TRACE,
		"Stop the DTX "DF_DTI" ver %u, dkey %llu, intent %s, "
		"%s, %s, %s: rc = %d\n",
		DP_DTI(&dth->dth_xid), dth->dth_ver,
		(unsigned long long)dth->dth_dkey_hash,
		dth->dth_intent == DAOS_INTENT_PUNCH ? "Punch" : "Update",
		dth->dth_sync ? "sync" : "async",
		dth->dth_non_rep ? "non-replicated" : "replicated",
		dth->dth_leader ? "leader" : "non-leader", result);

	if (dth->dth_leader && dth->dth_dti_cos != NULL)
		D_FREE(dth->dth_dti_cos);
	D_FREE_PTR(dth);

	return result > 0 ? 0 : result;
}

/**
 * Handle the conflict between current DTX and former uncommmitted DTXs.
 *
 * Current Commit on Share (CoS) mechanism cannot guarantee all related
 * DTXs to be handled in advance for current modification. If some confict
 * is detected after the RPC dispatching, the non-leader replica(s) will
 * return failures to the leader replica, then the leader needs to check
 * whether the conflict is caused by committable DTX(s) or not. if yes,
 * then commit them (via appending them to CoS list), otherwise, either
 * fail out (if leader also failed because of confilict) or abort them
 * if the leader replica executes related modification successfully.
 *
 * \param coh		[IN]	Container open handle.
 * \param dth		[IN]	The DTX handle.
 * \param po_uuid	[IN]	Pool UUID.
 * \param co_uuid	[IN]	Container UUID.
 * \param count		[IN]	The @dces array size.
 * \param version	[IN]	Current pool map version.
 *
 * \return			Zero on success, negative value if error.
 */
int
dtx_conflict(daos_handle_t coh, struct dtx_handle *dth, uuid_t po_uuid,
	     uuid_t co_uuid, struct dtx_conflict_entry *dces, int count,
	     uint32_t version)
{
	daos_unit_oid_t		*oid = &dth->dth_oid;
	struct dtx_id		*commit_ids = NULL;
	struct dtx_entry	*abort_dtes = NULL;
	int			 commit_cnt = 0;
	int			 abort_cnt = 0;
	int			 rc = 0;
	int			 i;

	D_ASSERT(dth->dth_leader);

	D_ALLOC_ARRAY(commit_ids, count);
	if (commit_ids == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC_ARRAY(abort_dtes, count);
	if (abort_dtes == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < count; i++) {
		int	j;
		bool	skip = false;

		for (j = 0; j < i; j++) {
			if (daos_dti_equal(&dces[i].dce_xid,
					   &dces[j].dce_xid)) {
				skip = true;
				break;
			}
		}

		if (!skip) {
			rc = vos_dtx_lookup_cos(coh, oid, &dces[i].dce_xid,
						dces[i].dce_dkey, true);
			if (rc != -DER_NONEXIST)
				goto found;

			rc = vos_dtx_lookup_cos(coh, oid,
						&dces[i].dce_xid,
						dces[i].dce_dkey, false);
			if (rc != -DER_NONEXIST)
				goto found;

			rc = vos_dtx_check_committable(coh, NULL,
						&dces[i].dce_xid,
						dces[i].dce_dkey, true);
			if (rc == DTX_ST_COMMITTED)
				rc = 0;
			else if (rc >= 0)
				rc = -DER_NONEXIST;

found:
			if (rc == 0) {
				daos_dti_copy(&commit_ids[commit_cnt++],
					      &dces[i].dce_xid);
				continue;
			}

			if (rc == -DER_NONEXIST) {
				daos_dti_copy(&abort_dtes[abort_cnt].dte_xid,
					      &dces[i].dce_xid);
				abort_dtes[abort_cnt++].dte_oid = *oid;
				continue;
			}

			goto out;
		}
	}

	if (commit_cnt > 0) {
		struct dtx_id	*dti_cos;
		int		 dti_cos_count;

		/* Append the committable DTXs' ID to the CoS list. */
		dti_cos_count = dth->dth_dti_cos_count + commit_cnt;
		D_ALLOC_ARRAY(dti_cos, dti_cos_count);
		if (dti_cos == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		if (dth->dth_dti_cos != NULL) {
			memcpy(dti_cos, dth->dth_dti_cos,
			       sizeof(struct dtx_id) * dth->dth_dti_cos_count);

			D_FREE(dth->dth_dti_cos);
		}

		memcpy(dti_cos + dth->dth_dti_cos_count, commit_ids,
		       sizeof(struct dtx_id) * commit_cnt);
		dth->dth_dti_cos_count = dti_cos_count;
		dth->dth_dti_cos = dti_cos;
	}

	if (abort_cnt > 0) {
		rc = dtx_abort(po_uuid, co_uuid, abort_dtes, abort_cnt,
			       version);
		if (rc == -DER_NONEXIST)
			rc = 0;
	}

out:
	if (commit_ids != NULL)
		D_FREE(commit_ids);
	if (abort_dtes != NULL)
		D_FREE(abort_dtes);

	return rc > 0 ? 0 : rc;
}

int
dtx_batched_commit_register(struct ds_cont_hdl *hdl)
{
	struct ds_cont			*cont = hdl->sch_cont;
	struct dtx_batched_commit_args	*dbca;
	d_list_t			*head;

	D_ASSERT(cont != NULL);

	if (hdl->sch_dtx_registered)
		return 0;

	head = &dss_get_module_info()->dmi_dtx_batched_list;
	d_list_for_each_entry(dbca, head, dbca_link) {
		if (uuid_compare(dbca->dbca_cont->sc_uuid,
				 cont->sc_uuid) == 0)
			goto out;
	}

	D_ALLOC_PTR(dbca);
	if (dbca == NULL)
		return -DER_NOMEM;

	ds_cont_get(cont);
	dbca->dbca_cont = cont;
	dbca->dbca_pool = ds_pool_child_get(hdl->sch_pool);
	d_list_add_tail(&dbca->dbca_link, head);

out:
	cont->sc_closing = 0;
	hdl->sch_dtx_registered = 1;
	dbca->dbca_shares++;

	return 0;
}

void
dtx_batched_commit_deregister(struct ds_cont_hdl *hdl)
{
	struct ds_cont			*cont = hdl->sch_cont;
	struct dtx_batched_commit_args	*dbca;
	d_list_t			*head;
	ABT_future			 future;
	int				 rc;

	if (cont == NULL)
		return;

	if (!hdl->sch_dtx_registered)
		return;

	if (cont->sc_closing) {
		D_ASSERT(cont->sc_dtx_flush_cbdata != NULL);

		future = cont->sc_dtx_flush_cbdata;
		goto wait;
	}

	head = &dss_get_module_info()->dmi_dtx_batched_list;
	d_list_for_each_entry(dbca, head, dbca_link) {
		if (uuid_compare(dbca->dbca_cont->sc_uuid,
				 cont->sc_uuid) != 0)
			continue;

		if (--(dbca->dbca_shares) > 0)
			goto out;

		/* Notify the dtx_batched_commit ULT to flush the
		 * committable DTXs via setting @sc_closing as 1.
		 *
		 * Then current ULT will wait here until the DTXs
		 * have been committed by dtx_batched_commit ULT
		 * that will wakeup current ULT.
		 */
		D_ASSERT(cont->sc_dtx_flush_cbdata == NULL);
		D_ASSERT(cont->sc_dtx_flush_wait_count == 0);

		rc = ABT_future_create(1, NULL, &future);
		cont->sc_closing = 1;
		if (rc != ABT_SUCCESS) {
			D_ERROR("ABT_future_create failed for DTX flush on "
				DF_UUID" %d\n", DP_UUID(cont->sc_uuid), rc);
			goto out;
		}

		cont->sc_dtx_flush_cbdata = future;
		goto wait;
	}

	D_ASSERT(0);

wait:
	cont->sc_dtx_flush_wait_count++;
	rc = ABT_future_wait(future);
	D_ASSERTF(rc == ABT_SUCCESS, "ABT_future_wait failed "
		  "for DTX flush (2) on "DF_UUID": rc = %d\n",
		  DP_UUID(cont->sc_uuid), rc);

	if (--(cont->sc_dtx_flush_wait_count) == 0) {
		cont->sc_dtx_flush_cbdata = NULL;
		ABT_future_free(&future);
	}

out:
	hdl->sch_dtx_registered = 0;
}

int
dtx_handle_resend(daos_handle_t coh, daos_unit_oid_t *oid,
		  struct dtx_id *dti, uint64_t dkey_hash, bool punch)
{
	int	rc;

	rc = vos_dtx_check_committable(coh, oid, dti, dkey_hash, punch);
	switch (rc) {
	case DTX_ST_PREPARED:
		return 0;
	case DTX_ST_INIT:
		/* XXX: The INIT DTX in SCM must be for an in-updating
		 *	object/key that was waiting for the bulk transfer.
		 *
		 *	Currently, we do NOT support server re-integration,
		 *	then we will ignore the case of client resending
		 *	RPC to the restarted server. So we can handle the
		 *	DTX_ST_INIT case the same as DTX_ST_PREPARED.
		 *
		 *	We need to check whether the RPC's time-stamp is
		 *	older than the server re-integration time or not
		 *	in the furture.
		 */
		return 0;
	case DTX_ST_COMMITTED:
		return -DER_ALREADY;
	case -DER_NONEXIST:
		if (time(NULL) - dti->dti_sec > DTX_AGG_THRESHOLD_AGE_LOWER) {
			D_DEBUG(DB_IO, "Not sure about whether the old RPC "
				DF_DTI" is resent or not.\n", DP_DTI(dti));
			return -DER_TIMEDOUT;
		}
		/* fall through */
	default:
		return rc >= 0 ? -DER_INVAL : rc;
	}
}
