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
 * dtx: DTX common logic
 */
#define D_LOGFAC	DD_FAC(dtx)

#include <abt.h>
#include <uuid/uuid.h>
#include <daos/btree_class.h>
#include <daos_srv/pool.h>
#include <daos_srv/container.h>
#include <daos_srv/vos.h>
#include <daos_srv/dtx_srv.h>
#include <daos_srv/daos_server.h>
#include "dtx_internal.h"

struct dtx_batched_commit_args {
	d_list_t		 dbca_link;
	struct ds_cont_child	*dbca_cont;
	void			*dbca_deregistering;
};

static void
dtx_stat(struct ds_cont_child *cont, struct dtx_stat *stat)
{
	vos_dtx_stat(cont->sc_hdl, stat);

	stat->dtx_committable_count = cont->sc_dtx_committable_count;
	stat->dtx_oldest_committable_time = dtx_cos_oldest(cont);
}

void
dtx_aggregate(void *arg)
{
	struct ds_cont_child	*cont = arg;

	while (1) {
		struct dtx_stat		stat = { 0 };
		int			rc;

		rc = vos_dtx_aggregate(cont->sc_hdl);
		if (rc != 0)
			break;

		ABT_thread_yield();

		if (cont->sc_open == 0)
			break;

		dtx_stat(cont, &stat);

		if (stat.dtx_committed_count <= DTX_AGG_THRESHOLD_CNT_LOWER)
			break;

		if (stat.dtx_committed_count >= DTX_AGG_THRESHOLD_CNT_UPPER)
			continue;

		if (stat.dtx_oldest_committed_time == 0 ||
		    dtx_hlc_age2sec(stat.dtx_oldest_committed_time) <=
		    DTX_AGG_THRESHOLD_AGE_LOWER)
			break;
	}

	cont->sc_dtx_aggregating = 0;
	ds_cont_child_put(cont);
}

static inline void
dtx_free_committable(struct dtx_entry **dtes)
{
	D_FREE(dtes);
}

static inline void
dtx_free_dbca(struct dtx_batched_commit_args *dbca)
{
	struct ds_cont_child	*cont = dbca->dbca_cont;

	if (!daos_handle_is_inval(cont->sc_dtx_cos_hdl)) {
		dbtree_destroy(cont->sc_dtx_cos_hdl, NULL);
		cont->sc_dtx_cos_hdl = DAOS_HDL_INVAL;
	}

	D_ASSERT(cont->sc_dtx_committable_count == 0);
	D_ASSERT(d_list_empty(&cont->sc_dtx_cos_list));

	d_list_del(&dbca->dbca_link);
	ds_cont_child_put(cont);
	D_FREE_PTR(dbca);
}

static void
dtx_flush_on_deregister(struct dss_module_info *dmi,
			struct dtx_batched_commit_args *dbca)
{
	struct ds_cont_child	*cont = dbca->dbca_cont;
	struct ds_pool_child	*pool = cont->sc_pool;
	int			 rc;

	D_ASSERT(dbca->dbca_deregistering != NULL);
	do {
		struct dtx_entry	**dtes = NULL;

		rc = dtx_fetch_committable(cont, DTX_THRESHOLD_COUNT,
					   NULL, DAOS_EPOCH_MAX, &dtes);
		if (rc <= 0)
			break;

		rc = dtx_commit(pool->spc_uuid, cont->sc_uuid,
				dtes, rc, true);
		dtx_free_committable(dtes);
	} while (rc >= 0);

	if (rc < 0)
		D_ERROR(DF_UUID": Fail to flush CoS cache: rc = %d\n",
			DP_UUID(cont->sc_uuid), rc);

	/*
	 * dtx_batched_commit_deregister() set force flush and wait for
	 * flush done, then free the dbca.
	 */
	d_list_del_init(&dbca->dbca_link);
	rc = ABT_future_set(dbca->dbca_deregistering, NULL);
	D_ASSERTF(rc == ABT_SUCCESS, "ABT_future_set failed for DTX "
		  "flush on "DF_UUID": rc = %d\n", DP_UUID(cont->sc_uuid), rc);
}

void
dtx_batched_commit(void *arg)
{
	struct dss_module_info		*dmi = dss_get_module_info();
	struct dtx_batched_commit_args	*dbca;

	while (1) {
		struct dtx_entry		**dtes = NULL;
		struct ds_cont_child		 *cont;
		struct dtx_stat			  stat = { 0 };
		int				  rc;

		if (d_list_empty(&dmi->dmi_dtx_batched_list))
			goto check;

		dbca = d_list_entry(dmi->dmi_dtx_batched_list.next,
				    struct dtx_batched_commit_args, dbca_link);
		cont = dbca->dbca_cont;
		if (dbca->dbca_deregistering != NULL) {
			dtx_flush_on_deregister(dmi, dbca);
			goto check;
		}

		d_list_move_tail(&dbca->dbca_link, &dmi->dmi_dtx_batched_list);
		dtx_stat(cont, &stat);

		if ((stat.dtx_committable_count > DTX_THRESHOLD_COUNT) ||
		    (stat.dtx_oldest_committable_time != 0 &&
		     dtx_hlc_age2sec(stat.dtx_oldest_committable_time) >
		     DTX_COMMIT_THRESHOLD_AGE)) {
			rc = dtx_fetch_committable(cont, DTX_THRESHOLD_COUNT,
						   NULL, DAOS_EPOCH_MAX, &dtes);
			if (rc > 0) {
				rc = dtx_commit(cont->sc_pool->spc_uuid,
						cont->sc_uuid, dtes, rc, true);
				dtx_free_committable(dtes);

				if (dbca->dbca_deregistering) {
					dtx_flush_on_deregister(dmi, dbca);
					goto check;
				}

				if (!cont->sc_dtx_aggregating)
					dtx_stat(cont, &stat);
			}
		}

		if (!cont->sc_dtx_aggregating &&
		    (stat.dtx_committed_count >= DTX_AGG_THRESHOLD_CNT_UPPER ||
		     (stat.dtx_committed_count > DTX_AGG_THRESHOLD_CNT_LOWER &&
		      stat.dtx_oldest_committed_time != 0 &&
		      dtx_hlc_age2sec(stat.dtx_oldest_committed_time) >=
				DTX_AGG_THRESHOLD_AGE_UPPER))) {
			ds_cont_child_get(cont);
			cont->sc_dtx_aggregating = 1;
			rc = dss_ult_create(dtx_aggregate, cont, DSS_ULT_GC,
					    DSS_TGT_SELF, 0, NULL);
			if (rc != 0) {
				cont->sc_dtx_aggregating = 0;
				ds_cont_child_put(cont);
			}
		}

check:
		if (dss_xstream_exiting(dmi->dmi_xstream))
			break;
		ABT_thread_yield();
	}

	while (!d_list_empty(&dmi->dmi_dtx_batched_list)) {
		dbca = d_list_entry(dmi->dmi_dtx_batched_list.next,
				    struct dtx_batched_commit_args, dbca_link);
		dtx_free_dbca(dbca);
	}
}

/* Return the epoch uncertainty upper bound. */
static daos_epoch_t
dtx_epoch_bound(struct dtx_epoch *epoch)
{
	daos_epoch_t limit;

	if (!(epoch->oe_flags & DTX_EPOCH_UNCERTAIN))
		/*
		 * We are told that the epoch has no uncertainty, even if it's
		 * still within the potential uncertainty window.
		 */
		return epoch->oe_value;

	limit = crt_hlc_epsilon_get_bound(epoch->oe_first);
	if (epoch->oe_value >= limit)
		/*
		 * The epoch is already out of the potential uncertainty
		 * window.
		 */
		return epoch->oe_value;

	return limit;
}

/** VOS reserves highest two minor epoch values for internal use so we must
 *  limit the number of dtx sub modifications to avoid conflict.
 */
#define DTX_SUB_MOD_MAX	(((uint16_t)-1) - 2)

/**
 * Init local dth handle.
 */
static int
dtx_handle_init(struct dtx_id *dti, daos_handle_t coh, struct dtx_epoch *epoch,
		uint16_t sub_modification_cnt, uint32_t pm_ver,
		daos_unit_oid_t *leader_oid, struct dtx_id *dti_cos,
		int dti_cos_cnt, struct dtx_memberships *mbs, bool leader,
		bool solo, bool sync, struct dtx_handle *dth)
{
	if (sub_modification_cnt > DTX_SUB_MOD_MAX) {
		D_ERROR("Too many modifications in a single transaction:"
			"%u > %u\n", sub_modification_cnt, DTX_SUB_MOD_MAX);
		return -DER_OVERFLOW;
	}

	dth->dth_xid = *dti;
	dth->dth_coh = coh;

	if (!dtx_epoch_chosen(epoch)) {
		D_ERROR("initializing DTX "DF_DTI" with invalid epoch: value="
			DF_U64" first="DF_U64" flags=%x\n",
			DP_DTI(dti), epoch->oe_value, epoch->oe_first,
			epoch->oe_flags);
		return -DER_INVAL;
	}
	dth->dth_epoch = epoch->oe_value;
	dth->dth_epoch_bound = dtx_epoch_bound(epoch);

	dth->dth_leader_oid = *leader_oid;
	dth->dth_ver = pm_ver;
	dth->dth_refs = 1;
	dth->dth_mbs = mbs;

	dth->dth_resent = 0;
	dth->dth_solo = solo ? 1 : 0;
	dth->dth_modify_shared = 0;
	dth->dth_active = 0;
	dth->dth_touched_leader_oid = 0;
	dth->dth_local_tx_started = 0;
	dth->dth_local_retry = 0;

	dth->dth_dti_cos = dti_cos;
	dth->dth_dti_cos_count = dti_cos_cnt;
	dth->dth_ent = NULL;
	dth->dth_flags = leader ? DTE_LEADER : 0;

	if (sync) {
		dth->dth_flags |= DTE_BLOCK;
		dth->dth_sync = 1;
	} else {
		dth->dth_sync = 0;
	}

	dth->dth_modification_cnt = sub_modification_cnt;

	dth->dth_op_seq = 0;
	dth->dth_oid_cnt = 0;
	dth->dth_oid_cap = 0;
	dth->dth_oid_array = NULL;

	dth->dth_dkey_hash = 0;

	return vos_dtx_rsrvd_init(dth);
}

static int
dtx_insert_oid(struct dtx_handle *dth, daos_unit_oid_t *oid, bool touch_leader)
{
	int	start = 0;
	int	end = dth->dth_oid_cnt - 1;
	int	at;
	int	rc = 0;

	do {
		at = (start + end) / 2;
		rc = daos_unit_oid_compare(dth->dth_oid_array[at], *oid);
		if (rc == 0)
			return 0;

		if (rc > 0)
			end = at - 1;
		else
			start = at + 1;
	} while (start <= end);

	if (dth->dth_oid_cnt == dth->dth_oid_cap) {
		daos_unit_oid_t		*oid_array;

		D_ALLOC_ARRAY(oid_array, dth->dth_oid_cap << 1);
		if (oid_array == NULL)
			return -DER_NOMEM;

		if (rc > 0) {
			/* Insert before dth->dth_oid_array[at]. */
			if (at > 0)
				memcpy(&oid_array[0], &dth->dth_oid_array[0],
				       sizeof(*oid) * at);
			oid_array[at] = *oid;
			memcpy(&oid_array[at + 1], &dth->dth_oid_array[at],
			       sizeof(*oid) * (dth->dth_oid_cnt - at));
		} else {
			/* Insert after dth->dth_oid_array[at]. */
			memcpy(&oid_array[0], &dth->dth_oid_array[0],
			       sizeof(*oid) * (at + 1));
			oid_array[at + 1] = *oid;
			if (at < dth->dth_oid_cnt - 1)
				memcpy(&oid_array[at + 2],
				&dth->dth_oid_array[at + 1],
				sizeof(*oid) * (dth->dth_oid_cnt - 1 - at));
		}

		D_FREE(dth->dth_oid_array);
		dth->dth_oid_array = oid_array;
		dth->dth_oid_cap <<= 1;

		goto out;
	}

	if (rc > 0) {
		/* Insert before dth->dth_oid_array[at]. */
		memmove(&dth->dth_oid_array[at + 1],
			&dth->dth_oid_array[at],
			sizeof(*oid) * (dth->dth_oid_cnt - at));
		dth->dth_oid_array[at] = *oid;
	} else {
		/* Insert after dth->dth_oid_array[at]. */
		if (at < dth->dth_oid_cnt - 1)
			memmove(&dth->dth_oid_array[at + 2],
				&dth->dth_oid_array[at + 1],
				sizeof(*oid) * (dth->dth_oid_cnt - 1 - at));
		dth->dth_oid_array[at + 1] = *oid;
	}

out:
	if (touch_leader)
		dth->dth_touched_leader_oid = 1;

	dth->dth_oid_cnt++;

	return 0;
}

/**
 * Initialize the DTX handle for per modification based part.
 *
 * \param dth		[IN]	Pointer to the DTX handle.
 * \param oid		[IN]	The target object (shard) ID.
 * \param dkey_hash	[IN]	Hash of the dkey to be modified if applicable.
 */
int
dtx_sub_init(struct dtx_handle *dth, daos_unit_oid_t *oid, uint64_t dkey_hash)
{
	int	rc = 0;

	if (!dtx_is_valid_handle(dth))
		return 0;

	D_ASSERT(dth->dth_op_seq < (uint16_t)(-1));

	dth->dth_op_seq++;
	dth->dth_dkey_hash = dkey_hash;

	rc = daos_unit_oid_compare(dth->dth_leader_oid, *oid);
	if (rc == 0) {
		if (dth->dth_oid_array == NULL)
			dth->dth_touched_leader_oid = 1;

		if (dth->dth_touched_leader_oid)
			goto out;

		rc = dtx_insert_oid(dth, oid, true);

		D_GOTO(out, rc);
	}

	if (dth->dth_oid_array == NULL) {
		D_ASSERT(dth->dth_oid_cnt == 0);

		/* 4 slots by default to hold rename case. */
		dth->dth_oid_cap = 4;
		D_ALLOC_ARRAY(dth->dth_oid_array, dth->dth_oid_cap);
		if (dth->dth_oid_array == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		if (!dth->dth_touched_leader_oid) {
			dth->dth_oid_array[0] = *oid;
			dth->dth_oid_cnt = 1;

			D_GOTO(out, rc = 0);
		}

		dth->dth_oid_cnt = 2;

		if (rc > 0) {
			dth->dth_oid_array[0] = *oid;
			dth->dth_oid_array[1] = dth->dth_leader_oid;
		} else {
			dth->dth_oid_array[0] = dth->dth_leader_oid;
			dth->dth_oid_array[1] = *oid;
		}

		D_GOTO(out, rc = 0);
	}

	rc = dtx_insert_oid(dth, oid, false);

out:
	D_DEBUG(DB_IO, "Sub init DTX "DF_DTI" for object "DF_UOID
		" dkey %lu, opc seq %d: "DF_RC"\n",
		DP_DTI(&dth->dth_xid), DP_UOID(*oid),
		(unsigned long)dkey_hash, dth->dth_op_seq, DP_RC(rc));

	return rc;
}

/**
 * Prepare the leader DTX handle in DRAM.
 *
 * \param cont		[IN]	Pointer to the container.
 * \param dti		[IN]	The DTX identifier.
 * \param epoch		[IN]	Epoch for the DTX.
 * \param sub_modification_cnt
 *			[IN]	Sub modifications count
 * \param pm_ver	[IN]	Pool map version for the DTX.
 * \param leader_oid	[IN]	The object ID is used to elect the DTX leader.
 * \param dti_cos	[IN]	The DTX array to be committed because of shared.
 * \param dti_cos_cnt	[IN]	The @dti_cos array size.
 * \param tgts		[IN]	targets for distribute transaction.
 * \param tgt_cnt	[IN]	number of targets.
 * \param solo		[IN]	single operand or not.
 * \param sync		[IN]	sync mode or not.
 * \param mbs		[IN]	DTX participants information.
 * \param dth		[OUT]	Pointer to the DTX handle.
 *
 * \return			Zero on success, negative value if error.
 */
int
dtx_leader_begin(struct ds_cont_child *cont, struct dtx_id *dti,
		 struct dtx_epoch *epoch, uint16_t sub_modification_cnt,
		 uint32_t pm_ver, daos_unit_oid_t *leader_oid,
		 struct dtx_id *dti_cos, int dti_cos_cnt,
		 struct daos_shard_tgt *tgts, int tgt_cnt, bool solo, bool sync,
		 struct dtx_memberships *mbs, struct dtx_leader_handle *dlh)
{
	struct dtx_handle	*dth = &dlh->dlh_handle;
	int			 rc;
	int			 i;

	memset(dlh, 0, sizeof(*dlh));

	/* Single replica case. */
	if (tgt_cnt == 0) {
		if (!daos_is_zero_dti(dti))
			goto init;

		return 0;
	}

	dlh->dlh_future = ABT_FUTURE_NULL;
	D_ALLOC_ARRAY(dlh->dlh_subs, tgt_cnt);
	if (dlh->dlh_subs == NULL)
		return -DER_NOMEM;

	for (i = 0; i < tgt_cnt; i++)
		dlh->dlh_subs[i].dss_tgt = tgts[i];
	dlh->dlh_sub_cnt = tgt_cnt;

	if (daos_is_zero_dti(dti))
		return 0;

init:
	rc = dtx_handle_init(dti, cont->sc_hdl, epoch, sub_modification_cnt,
			     pm_ver, leader_oid, dti_cos, dti_cos_cnt, mbs,
			     true, solo, sync, dth);

	D_DEBUG(DB_IO, "Start %s DTX "DF_DTI" sub_reqs %d, ver %u, leader "
		DF_UOID", dti_cos_cnt %d: "DF_RC"\n",
		sync ? "sync" : "async", DP_DTI(dti), sub_modification_cnt,
		dth->dth_ver, DP_UOID(*leader_oid), dti_cos_cnt, DP_RC(rc));

	if (rc != 0)
		D_FREE(dlh->dlh_subs);

	return rc;
}

static int
dtx_leader_wait(struct dtx_leader_handle *dlh)
{
	int	rc;

	rc = ABT_future_wait(dlh->dlh_future);
	D_ASSERTF(rc == ABT_SUCCESS, "ABT_future_wait failed %d.\n", rc);

	ABT_future_free(&dlh->dlh_future);
	D_DEBUG(DB_IO, "dth "DF_DTI" rc "DF_RC"\n",
		DP_DTI(&dlh->dlh_handle.dth_xid), DP_RC(dlh->dlh_result));

	return dlh->dlh_result;
};

/**
 * Stop the leader thandle.
 *
 * \param dlh		[IN]	The DTX handle on leader node.
 * \param cont		[IN]	Per-thread container cache.
 * \param result	[IN]	Operation result.
 *
 * \return			Zero on success, negative value if error.
 */
int
dtx_leader_end(struct dtx_leader_handle *dlh, struct ds_cont_child *cont,
	       int result)
{
	struct dtx_handle		*dth = &dlh->dlh_handle;
	struct dtx_entry		*dte;
	daos_epoch_t			 epoch = dth->dth_epoch;
	int				 saved = result;
	int				 rc = 0;

	D_ASSERT(cont != NULL);

	/* NB: even the local request failure, dth_ent == NULL, we
	 * should still wait for remote object to finish the request.
	 */

	if (dlh->dlh_sub_cnt != 0)
		rc = dtx_leader_wait(dlh);
	else if (dth->dth_modification_cnt <= 1)
		goto out;

	if (daos_is_zero_dti(&dth->dth_xid))
		D_GOTO(out, result = result < 0 ? result : rc);

	if (result < 0 || rc < 0 || (!dth->dth_active && !dth->dth_resent))
		D_GOTO(abort, result = result < 0 ? result : rc);

again:
	/* If the DTX is started befoe DTX resync (for rebuild), then it is
	 * possbile that the DTX resync ULT may have aborted or committed
	 * the DTX during current ULT waiting for other non-leaders' reply.
	 * Let's check DTX status locally before marking as 'committable'.
	 */
	if (dth->dth_ver < cont->sc_dtx_resync_ver) {
		rc = vos_dtx_check(cont->sc_hdl, &dth->dth_xid,
				   NULL, NULL, false);
		/* Committed by race, do nothing. */
		if (rc == DTX_ST_COMMITTED)
			D_GOTO(abort, result = 0);

		/* Aborted by race, restart it. */
		if (rc == -DER_NONEXIST) {
			D_WARN(DF_UUID": DTX "DF_DTI" is aborted with "
			       "old epoch "DF_U64" by resync\n",
			       DP_UUID(cont->sc_uuid), DP_DTI(&dth->dth_xid),
			       dth->dth_epoch);
			D_GOTO(abort, result = -DER_TX_RESTART);
		}

		if (rc != DTX_ST_PREPARED) {
			D_ASSERT(rc < 0);

			D_WARN(DF_UUID": Failed to check local DTX "DF_DTI
			       "status: "DF_RC"\n",
			       DP_UUID(cont->sc_uuid), DP_DTI(&dth->dth_xid),
			       DP_RC(rc));
			D_GOTO(abort, result = rc);
		}
	}

	rc = vos_dtx_check_sync(dth->dth_coh, dth->dth_leader_oid, &epoch);
	/* Only add async DTX into the CoS cache. */
	if (rc == 0) {
		struct dtx_memberships	*mbs;
		size_t			 size;

		/* When we come here, the modification on all participants have
		 * been done successfully. If 'dth->dth_active' is false, means
		 * that it is for resent case. Under such case, we have no way
		 * to mark it as committable, then commit it sychronously.
		 */
		if (!dth->dth_active) {
			D_ASSERT(dth->dth_resent);

			dth->dth_sync = 1;
		}

		/* For synchronous DTX, do not add it into CoS cache, otherwise,
		 * we may have no way to remove it from the cache.
		 */
		if (dth->dth_sync)
			goto sync;

		size = sizeof(*dte) + sizeof(*mbs) + dth->dth_mbs->dm_data_size;
		D_ALLOC(dte, size);
		if (dte == NULL) {
			dth->dth_sync = 1;
			goto sync;
		}

		mbs = (struct dtx_memberships *)(dte + 1);
		memcpy(mbs, dth->dth_mbs, size - sizeof(*dte));

		dte->dte_xid = dth->dth_xid;
		dte->dte_ver = dth->dth_ver;
		dte->dte_refs = 1;
		dte->dte_mbs = mbs;

		/* Use the new created @dte instead of dth->dth_dte that will be
		 * released after dtx_leader_end().
		 *
		 * Only the DTX for single RDG may be added into CoS cache.
		 */
		rc = dtx_add_cos(cont, dte, &dth->dth_leader_oid,
				 dth->dth_dkey_hash, dth->dth_epoch,
				 dth->dth_modify_shared ? DCF_SHARED : 0);
		dtx_entry_put(dte);
		if (rc == 0)
			vos_dtx_mark_committable(dth);
	}

	if (rc == -DER_TX_RESTART) {
		D_WARN(DF_UUID": Fail to add DTX "DF_DTI" to CoS "
		       "because of using old epoch "DF_U64"\n",
		       DP_UUID(cont->sc_uuid), DP_DTI(&dth->dth_xid),
		       dth->dth_epoch);
		D_GOTO(abort, result = rc);
	}

	if (rc == -DER_NONEXIST) {
		D_WARN(DF_UUID": Fail to add DTX "DF_DTI" to CoS "
		       "because of target object disappeared unexpectedly.\n",
		       DP_UUID(cont->sc_uuid), DP_DTI(&dth->dth_xid));
		/* Handle it as IO failure. */
		D_GOTO(abort, result = -DER_IO);
	}

	if (rc == -DER_AGAIN) {
		/* The object may be in-dying, let's yield and retry locally. */
		ABT_thread_yield();
		goto again;
	}

	if (rc != 0 && epoch < dth->dth_epoch) {
		D_WARN(DF_UUID": Fail to add DTX "DF_DTI" to CoS cache: "
		       DF_RC". Try to commit it sychronously.\n",
		       DP_UUID(cont->sc_uuid), DP_DTI(&dth->dth_xid),
		       DP_RC(rc));
		dth->dth_sync = 1;
	}

sync:
	if (dth->dth_sync) {
		dte = &dth->dth_dte;
		rc = dtx_commit(cont->sc_pool->spc_uuid, cont->sc_uuid,
				&dte, 1, false);
		if (rc != 0) {
			D_ERROR(DF_UUID": Fail to sync commit DTX "DF_DTI
				": "DF_RC"\n", DP_UUID(cont->sc_uuid),
				DP_DTI(&dth->dth_xid), DP_RC(rc));
			D_GOTO(abort, result = rc);
		}
	}

abort:
	/* Some remote replica(s) ask retry. We do not make such replica
	 * to locally retry for avoiding RPC timeout. The leader replica
	 * will trigger retry globally without aborting 'prepared' ones.
	 */
	if (result < 0 && result != -DER_AGAIN) {
		dte = &dth->dth_dte;
		dtx_abort(cont->sc_pool->spc_uuid, cont->sc_uuid,
			  dth->dth_epoch, &dte, 1);
	}

	vos_dtx_rsrvd_fini(dth);
out:
	if (!daos_is_zero_dti(&dth->dth_xid))
		D_DEBUG(DB_IO,
			"Stop the DTX "DF_DTI" ver %u, dkey %lu, %s, "
			"%s participator(s): rc "DF_RC"\n",
			DP_DTI(&dth->dth_xid), dth->dth_ver,
			(unsigned long)dth->dth_dkey_hash,
			dth->dth_sync ? "sync" : "async",
			dth->dth_solo ? "single" : "multiple", DP_RC(result));

	D_ASSERTF(result <= 0, "unexpected return value %d\n", result);

	/* Local modification is done, then need to handle CoS cache. */
	if (saved >= 0) {
		int	i;

		for (i = 0; i < dth->dth_dti_cos_count; i++)
			dtx_del_cos(cont, &dth->dth_dti_cos[i],
				    &dth->dth_leader_oid, dth->dth_dkey_hash);
	}

	D_FREE(dlh->dlh_subs);
	D_FREE(dth->dth_oid_array);

	return result;
}

/**
 * Prepare the DTX handle in DRAM.
 *
 * \param cont		[IN]	Pointer to the container.
 * \param dti		[IN]	The DTX identifier.
 * \param epoch		[IN]	Epoch for the DTX.
 * \param sub_modification_cnt
 *			[IN]	Sub modifications count.
 * \param pm_ver	[IN]	Pool map version for the DTX.
 * \param leader_oid	[IN]    The object ID is used to elect the DTX leader.
 * \param dkey_hash	[IN]	Hash of the dkey to be modified if applicable.
 * \param intent	[IN]	The intent of related operations.
 * \param dti_cos	[IN]	The DTX array to be committed because of shared.
 * \param dti_cos_cnt	[IN]	The @dti_cos array size.
 * \param mbs		[IN]	DTX participants information.
 * \param dth		[OUT]	Pointer to the DTX handle.
 *
 * \return			Zero on success, negative value if error.
 */
int
dtx_begin(struct ds_cont_child *cont, struct dtx_id *dti,
	  struct dtx_epoch *epoch, uint16_t sub_modification_cnt,
	  uint32_t pm_ver, daos_unit_oid_t *leader_oid,
	  struct dtx_id *dti_cos, int dti_cos_cnt,
	  struct dtx_memberships *mbs, struct dtx_handle *dth)
{
	int	rc;

	D_ASSERT(dth != NULL);

	if (daos_is_zero_dti(dti)) {
		daos_dti_gen(&dth->dth_xid, true);
		return 0;
	}

	rc = dtx_handle_init(dti, cont->sc_hdl, epoch, sub_modification_cnt,
			     pm_ver, leader_oid, dti_cos, dti_cos_cnt, mbs,
			     false, false, false, dth);

	D_DEBUG(DB_IO, "Start DTX "DF_DTI" sub_reqs %d, ver %u, "
		"dti_cos_cnt %d: "DF_RC"\n",
		DP_DTI(dti), sub_modification_cnt,
		dth->dth_ver, dti_cos_cnt, DP_RC(rc));

	return rc;
}

int
dtx_end(struct dtx_handle *dth, struct ds_cont_child *cont, int result)
{
	D_ASSERT(dth != NULL);

	if (daos_is_zero_dti(&dth->dth_xid))
		return result;

	if (result < 0) {
		if (dth->dth_dti_cos_count > 0) {
			int	rc;

			/* XXX: For non-leader replica, even if we fail to
			 *	make related modification for some reason,
			 *	we still need to commit the DTXs for CoS.
			 *	Because other replica may have already
			 *	committed them. For leader case, it is
			 *	not important even if we fail to commit
			 *	the CoS DTXs, because they are still in
			 *	CoS cache, and can be committed next time.
			 */
			rc = vos_dtx_commit(cont->sc_hdl, dth->dth_dti_cos,
					    dth->dth_dti_cos_count, NULL);
			if (rc < 0)
				D_ERROR(DF_UUID": Fail to DTX CoS commit: %d\n",
					DP_UUID(cont->sc_uuid), rc);
		}
	}

	D_DEBUG(DB_IO,
		"Stop the DTX "DF_DTI" ver %u, dkey %lu: "DF_RC"\n",
		DP_DTI(&dth->dth_xid), dth->dth_ver,
		(unsigned long)dth->dth_dkey_hash, DP_RC(result));

	D_ASSERTF(result <= 0, "unexpected return value %d\n", result);

	D_FREE(dth->dth_oid_array);

	vos_dtx_rsrvd_fini(dth);

	return result;
}

#define DTX_COS_BTREE_ORDER		23

int
dtx_batched_commit_register(struct ds_cont_child *cont)
{
	struct dtx_batched_commit_args	*dbca;
	d_list_t			*head;
	struct umem_attr		 uma;
	int				 rc;

	D_ASSERT(cont != NULL);

	head = &dss_get_module_info()->dmi_dtx_batched_list;
	d_list_for_each_entry(dbca, head, dbca_link) {
		if (dbca->dbca_deregistering != NULL)
			continue;

		if (uuid_compare(dbca->dbca_cont->sc_uuid,
				 cont->sc_uuid) == 0)
			return 0;
	}

	D_ALLOC_PTR(dbca);
	if (dbca == NULL)
		return -DER_NOMEM;

	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
	rc = dbtree_create_inplace_ex(DBTREE_CLASS_DTX_COS, 0,
				      DTX_COS_BTREE_ORDER, &uma,
				      &cont->sc_dtx_cos_btr,
				      DAOS_HDL_INVAL, cont,
				      &cont->sc_dtx_cos_hdl);
	if (rc != 0) {
		D_ERROR("Failed to create DTX CoS btree: "DF_RC"\n",
			DP_RC(rc));
		D_FREE(dbca);
		return rc;
	}

	cont->sc_dtx_committable_count = 0;
	D_INIT_LIST_HEAD(&cont->sc_dtx_cos_list);
	cont->sc_dtx_resync_ver = 1;

	ds_cont_child_get(cont);
	dbca->dbca_cont = cont;
	d_list_add_tail(&dbca->dbca_link, head);

	return 0;
}

void
dtx_batched_commit_deregister(struct ds_cont_child *cont)
{
	struct dtx_batched_commit_args	*dbca;
	d_list_t			*head;
	ABT_future			 future;
	int				 rc;

	D_ASSERT(cont != NULL);
	D_ASSERT(cont->sc_open == 0);

	head = &dss_get_module_info()->dmi_dtx_batched_list;
	d_list_for_each_entry(dbca, head, dbca_link) {
		if (uuid_compare(dbca->dbca_cont->sc_uuid,
				 cont->sc_uuid) != 0)
			continue;

		/*
		 * Notify the dtx_batched_commit ULT to flush the
		 * committable DTXs.
		 *
		 * Then current ULT will wait here until the DTXs
		 * have been committed by dtx_batched_commit ULT
		 * that will wakeup current ULT.
		 */
		D_ASSERT(dbca->dbca_deregistering == NULL);
		rc = ABT_future_create(1, NULL, &future);
		if (rc != ABT_SUCCESS) {
			D_ERROR("ABT_future_create failed for DTX flush on "
				DF_UUID" %d\n", DP_UUID(cont->sc_uuid), rc);
			return;
		}

		dbca->dbca_deregistering = future;
		rc = ABT_future_wait(future);
		D_ASSERTF(rc == ABT_SUCCESS, "ABT_future_wait failed "
			  "for DTX flush (2) on "DF_UUID": rc = %d\n",
			  DP_UUID(cont->sc_uuid), rc);

		D_ASSERT(d_list_empty(&dbca->dbca_link));
		dtx_free_dbca(dbca);
		ABT_future_free(&future);
		break;
	}
}

int
dtx_handle_resend(daos_handle_t coh,  struct dtx_id *dti,
		  daos_epoch_t *epoch, uint32_t *pm_ver)
{
	uint64_t	age;
	int		rc;

	if (daos_is_zero_dti(dti))
		/* If DTX is disabled, then means that the application does
		 * not care about the replicas consistency. Under such case,
		 * if client resends some modification RPC, then just handle
		 * it as non-resent case, return -DER_NONEXIST.
		 *
		 * It will cause trouble if related modification has ever
		 * been handled before the resending. But since we cannot
		 * trace (if without DTX) whether it has ever been handled
		 * or not, then just handle it as original without DTX case.
		 */
		return -DER_NONEXIST;

again:
	rc = vos_dtx_check(coh, dti, epoch, pm_ver, true);
	switch (rc) {
	case DTX_ST_PREPARED:
		return 0;
	case DTX_ST_COMMITTED:
		return -DER_ALREADY;
	case -DER_NONEXIST:
		age = dtx_hlc_age2sec(dti->dti_hlc);
		if (age > DTX_AGG_THRESHOLD_AGE_LOWER ||
		    DAOS_FAIL_CHECK(DAOS_DTX_LONG_TIME_RESEND)) {
			D_ERROR("Not sure about whether the old RPC "DF_DTI
				" is resent or not. Age="DF_U64" s.\n",
				DP_DTI(dti), age);
			rc = -DER_EP_OLD;
		}
		return rc;
	case -DER_AGAIN:
		/* Re-index committed DTX table is not completed yet,
		 * let's wait and retry.
		 */
		ABT_thread_yield();
		goto again;
	default:
		return rc >= 0 ? -DER_INVAL : rc;
	}
}

static void
dtx_comp_cb(void **arg)
{
	struct dtx_leader_handle	*dlh;
	uint32_t			i;

	dlh = arg[0];

	if (dlh->dlh_agg_cb) {
		dlh->dlh_result = dlh->dlh_agg_cb(dlh, dlh->dlh_agg_cb_arg);
		return;
	}

	for (i = 0; i < dlh->dlh_sub_cnt; i++) {
		struct dtx_sub_status	*sub = &dlh->dlh_subs[i];

		if (sub->dss_result == 0)
			continue;

		/* Ignore DER_INPROGRESS if there are other failures */
		if (dlh->dlh_result == 0 || dlh->dlh_result == -DER_INPROGRESS)
			dlh->dlh_result = sub->dss_result;
	}
}

static void
dtx_sub_comp_cb(struct dtx_leader_handle *dlh, int idx, int rc)
{
	struct dtx_sub_status	*sub = &dlh->dlh_subs[idx];
	ABT_future		future = dlh->dlh_future;

	sub->dss_result = rc;
	rc = ABT_future_set(future, dlh);
	D_ASSERTF(rc == ABT_SUCCESS, "ABT_future_set failed %d.\n", rc);

	D_DEBUG(DB_TRACE, "execute from rank %d tag %d, rc %d.\n",
		sub->dss_tgt.st_rank, sub->dss_tgt.st_tgt_idx,
		sub->dss_result);
}

struct dtx_ult_arg {
	dtx_sub_func_t			func;
	void				*func_arg;
	struct dtx_leader_handle	*dlh;
};

static void
dtx_leader_exec_ops_ult(void *arg)
{
	struct dtx_ult_arg	  *ult_arg = arg;
	struct dtx_leader_handle  *dlh = ult_arg->dlh;
	ABT_future		  future = dlh->dlh_future;
	uint32_t		  i;
	int			  rc = 0;

	D_ASSERT(future != ABT_FUTURE_NULL);
	for (i = 0; i < dlh->dlh_sub_cnt; i++) {
		struct dtx_sub_status *sub = &dlh->dlh_subs[i];

		sub->dss_result = 0;

		if (sub->dss_tgt.st_rank == DAOS_TGT_IGNORE) {
			int ret;

			ret = ABT_future_set(future, dlh);
			D_ASSERTF(ret == ABT_SUCCESS,
				  "ABT_future_set failed %d.\n", ret);
			continue;
		}

		rc = ult_arg->func(dlh, ult_arg->func_arg, i,
				   dtx_sub_comp_cb);
		if (rc) {
			sub->dss_result = rc;
			break;
		}
	}

	if (rc != 0) {
		for (i++; i < dlh->dlh_sub_cnt; i++) {
			int ret;

			ret = ABT_future_set(future, dlh);
			D_ASSERTF(ret == ABT_SUCCESS,
				  "ABT_future_set failed %d.\n", ret);
		}
	}

	D_FREE_PTR(ult_arg);
}

/**
 * Execute the operations on all targets.
 */
int
dtx_leader_exec_ops(struct dtx_leader_handle *dlh, dtx_sub_func_t func,
		    dtx_agg_cb_t agg_cb, void *agg_cb_arg, void *func_arg)
{
	struct dtx_ult_arg	*ult_arg;
	int			rc;

	if (dlh->dlh_sub_cnt == 0)
		goto exec;

	D_ALLOC_PTR(ult_arg);
	if (ult_arg == NULL)
		return -DER_NOMEM;
	ult_arg->func	= func;
	ult_arg->func_arg = func_arg;
	ult_arg->dlh	= dlh;
	dlh->dlh_agg_cb = agg_cb;
	dlh->dlh_agg_cb_arg = agg_cb_arg;

	/* the future should already be freed */
	D_ASSERT(dlh->dlh_future == ABT_FUTURE_NULL);
	rc = ABT_future_create(dlh->dlh_sub_cnt, dtx_comp_cb, &dlh->dlh_future);
	if (rc != ABT_SUCCESS) {
		D_ERROR("ABT_future_create failed %d.\n", rc);
		D_FREE_PTR(ult_arg);
		return dss_abterr2der(rc);
	}

	/*
	 * XXX ideally, we probably should create ULT for each shard, but
	 * for performance reasons, let's only create one for all remote
	 * targets for now.
	 */
	dlh->dlh_result = 0;
	rc = dss_ult_create(dtx_leader_exec_ops_ult, ult_arg, DSS_ULT_IOFW,
			    dss_get_module_info()->dmi_tgt_id, 0, NULL);
	if (rc != 0) {
		D_ERROR("ult create failed "DF_RC"\n", DP_RC(rc));
		D_FREE_PTR(ult_arg);
		ABT_future_free(&dlh->dlh_future);
		D_GOTO(out, rc);
	}

exec:
	/* Then execute the local operation */
	rc = func(dlh, func_arg, -1, NULL);
out:
	return rc;
}

int
dtx_obj_sync(uuid_t po_uuid, uuid_t co_uuid, struct ds_cont_child *cont,
	     daos_unit_oid_t *oid, daos_epoch_t epoch)
{
	int	rc = 0;

	while (1) {
		struct dtx_entry	**dtes = NULL;

		rc = dtx_fetch_committable(cont, DTX_THRESHOLD_COUNT, oid,
					   epoch, &dtes);
		if (rc < 0) {
			D_ERROR("Failed to fetch dtx: "DF_RC"\n", DP_RC(rc));
			break;
		}

		if (rc == 0)
			break;

		rc = dtx_commit(po_uuid, co_uuid, dtes, rc, true);
		dtx_free_committable(dtes);
		if (rc < 0) {
			D_ERROR("Fail to commit dtx: "DF_RC"\n", DP_RC(rc));
			break;
		}
	}

	if (rc == 0 && oid != NULL)
		rc = vos_dtx_mark_sync(cont->sc_hdl, *oid, epoch);

	return rc;
}
