/**
 * (C) Copyright 2019-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dtx: DTX common logic
 */
#define D_LOGFAC	DD_FAC(dtx)

#include <abt.h>
#include <uuid/uuid.h>
#include <daos/btree_class.h>
#include <daos_srv/container.h>
#include <daos_srv/vos.h>
#include <daos_srv/dtx_srv.h>
#include <daos_srv/daos_engine.h>
#include "dtx_internal.h"

struct dtx_batched_cont_args;
uint32_t dtx_agg_thd_cnt_up;
uint32_t dtx_agg_thd_cnt_lo;
uint32_t dtx_agg_thd_age_up;
uint32_t dtx_agg_thd_age_lo;
uint32_t dtx_batched_ult_max;

struct dtx_batched_pool_args {
	/* Link to dss_module_info::dmi_dtx_batched_pool_list. */
	d_list_t			 dbpa_sys_link;
	/* The list of containers belong to the pool. */
	d_list_t			 dbpa_cont_list;
	struct ds_pool_child		*dbpa_pool;
	/* The container that needs to do DTX aggregation. */
	struct dtx_batched_cont_args	*dbpa_victim;
	struct dtx_stat			 dbpa_stat;
	uint32_t			 dbpa_aggregating;
};

struct dtx_batched_cont_args {
	/* Link to dss_module_info::dmi_dtx_batched_cont_{open,close}_list. */
	d_list_t			 dbca_sys_link;
	/* Link to dtx_batched_pool_args::dbpa_cont_list. */
	d_list_t			 dbca_pool_link;
	uint64_t			 dbca_agg_gen;
	struct sched_request		*dbca_cleanup_req;
	struct sched_request		*dbca_commit_req;
	struct sched_request		*dbca_agg_req;
	struct ds_cont_child		*dbca_cont;
	struct dtx_batched_pool_args	*dbca_pool;
	int                              dbca_refs;
	uint32_t                         dbca_cleanup_thd;
	uint32_t dbca_deregister : 1, dbca_cleanup_done : 1, dbca_commit_done : 1,
	    dbca_agg_done : 1, dbca_flush_pending : 1;
};

struct dtx_partial_cmt_item {
	d_list_t		dpci_link;
	uint32_t		dpci_inline_mbs:1;
	struct dtx_entry	dpci_dte;
};

struct dtx_cleanup_cb_args {
	/* The list for stale DTX entries. */
	d_list_t		dcca_st_list;
	/* The list for partial committed DTX entries. */
	d_list_t		dcca_pc_list;
	int			dcca_st_count;
	int			dcca_pc_count;
	uint32_t		dcca_cleanup_thd;
};

static inline void
dtx_free_committable(struct dtx_entry **dtes, struct dtx_cos_key *dcks,
		     struct dtx_coll_entry *dce, int count)
{
	int	i;

	if (dce != NULL) {
		D_ASSERT(count == 1);

		dtx_coll_entry_put(dce);
	} else {
		for (i = 0; i < count; i++)
			dtx_entry_put(dtes[i]);
		D_FREE(dtes);
	}
	D_FREE(dcks);
}

static inline void
dtx_get_dbca(struct dtx_batched_cont_args *dbca)
{
	dbca->dbca_refs++;
	D_ASSERT(dbca->dbca_refs >= 1);
}

static inline void
dtx_put_dbca(struct dtx_batched_cont_args *dbca)
{
	D_ASSERT(dbca->dbca_refs >= 1);
	dbca->dbca_refs--;
}

static void
dtx_free_dbca(struct dtx_batched_cont_args *dbca)
{
	struct ds_cont_child		*cont = dbca->dbca_cont;
	struct dtx_batched_pool_args	*dbpa = dbca->dbca_pool;

	if (daos_handle_is_valid(cont->sc_dtx_cos_hdl)) {
		dbtree_destroy(cont->sc_dtx_cos_hdl, NULL);
		cont->sc_dtx_cos_hdl = DAOS_HDL_INVAL;
	}

	D_ASSERT(cont->sc_dtx_committable_count == 0);
	D_ASSERT(cont->sc_dtx_committable_coll_count == 0);
	D_ASSERT(d_list_empty(&cont->sc_dtx_cos_list));
	D_ASSERT(d_list_empty(&cont->sc_dtx_coll_list));

	/* Even if the container is reopened during current deregister, the
	 * reopen will use new dbca, so current dbca needs to be cleanup.
	 */

	if (dbca->dbca_cleanup_req != NULL) {
		if (!dbca->dbca_cleanup_done)
			sched_req_wait(dbca->dbca_cleanup_req, true);
		/* dtx_batched_commit might put it while we were waiting. */
		if (dbca->dbca_cleanup_req != NULL) {
			D_ASSERT(dbca->dbca_cleanup_done);
			sched_req_put(dbca->dbca_cleanup_req);
			dbca->dbca_cleanup_req = NULL;
			dbca->dbca_cleanup_done = 0;
		}
	}

	if (dbca->dbca_commit_req != NULL) {
		if (!dbca->dbca_commit_done)
			sched_req_wait(dbca->dbca_commit_req, true);
		/* dtx_batched_commit might put it while we were waiting. */
		if (dbca->dbca_commit_req != NULL) {
			D_ASSERT(dbca->dbca_commit_done);
			sched_req_put(dbca->dbca_commit_req);
			dbca->dbca_commit_req = NULL;
			dbca->dbca_commit_done = 0;
		}
	}

	if (dbca->dbca_agg_req != NULL) {
		if (!dbca->dbca_agg_done)
			sched_req_wait(dbca->dbca_agg_req, true);
		/* Just to be safe... */
		if (dbca->dbca_agg_req != NULL) {
			D_ASSERT(dbca->dbca_agg_done);
			sched_req_put(dbca->dbca_agg_req);
			dbca->dbca_agg_req = NULL;
			dbca->dbca_agg_done = 0;
		}
	}

	/* batched_commit/aggreagtion ULT may hold reference on the dbca. */
	while (dbca->dbca_refs > 0) {
		D_DEBUG(DB_TRACE, "Sleep 10 mseconds for reference release\n");
		dss_sleep(10);
	}

	if (d_list_empty(&dbpa->dbpa_cont_list)) {
		d_list_del(&dbpa->dbpa_sys_link);
		D_FREE(dbpa);
	}

	D_FREE(dbca);
	cont->sc_dtx_registered = 0;
	ds_cont_child_put(cont);
}

static inline uint64_t
dtx_sec2age(uint64_t sec)
{
	uint64_t	cur = daos_wallclock_secs();

	if (unlikely(cur <= sec))
		return 0;

	return cur - sec;
}

static void
dtx_stat(struct ds_cont_child *cont, struct dtx_stat *stat)
{
	vos_dtx_stat(cont->sc_hdl, stat, DSF_SKIP_BAD);

	stat->dtx_committable_count = cont->sc_dtx_committable_count;
	stat->dtx_committable_coll_count = cont->sc_dtx_committable_coll_count;
	stat->dtx_oldest_committable_time = dtx_cos_oldest(cont);
}

static int
dtx_cleanup_iter_cb(uuid_t co_uuid, vos_iter_entry_t *ent, void *args)
{
	struct dtx_cleanup_cb_args	*dcca = args;
	struct dtx_share_peer		*dsp = NULL;
	struct dtx_partial_cmt_item	*dpci = NULL;
	struct dtx_memberships		*mbs;
	struct dtx_entry		*dte;

	/* We commit the DTXs periodically, there will be very limited DTXs
	 * to be checked when cleanup. So we can load all those uncommitted
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

	/* Skip unprepared entry. */
	if (ent->ie_dtx_tgt_cnt == 0)
		return 0;

	/* Stop the iteration if current DTX is not too old. */
	if (dtx_sec2age(ent->ie_dtx_start_time) <= dcca->dcca_cleanup_thd)
		return 1;

	D_ASSERT(ent->ie_dtx_mbs_dsize > 0);

	/*
	 * NOTE: Usually, the partial committed DTX entries will be handled by batched commit ULT
	 *	 (if related container is not closed) or DTX resync (after the container re-open).
	 *	 So here, the left ones are for rare failure cases in these process, they will be
	 *	 re-committed via dtx_cleanup logic.
	 */
	if (unlikely(ent->ie_dtx_flags & DTE_PARTIAL_COMMITTED)) {
		if (ent->ie_dtx_mbs_dsize > DTX_INLINE_MBS_SIZE)
			D_ALLOC_PTR(dpci);
		else
			D_ALLOC(dpci, sizeof(*dpci) + sizeof(*mbs) + ent->ie_dtx_mbs_dsize);
		if (dpci == NULL)
			return -DER_NOMEM;

		dte = &dpci->dpci_dte;
		dte->dte_xid = ent->ie_dtx_xid;
		dte->dte_ver = ent->ie_dtx_ver;
		dte->dte_refs = 1;

		if (ent->ie_dtx_mbs_dsize > DTX_INLINE_MBS_SIZE)
			goto add;

		mbs = (struct dtx_memberships *)(dte + 1);
		dpci->dpci_inline_mbs = 1;
		dte->dte_mbs = mbs;
	} else {
		/* Skip the DTX which leader resides on current target and may be still alive. */
		if (ent->ie_dtx_flags & DTE_LEADER)
			return 0;

		if (ent->ie_dtx_mbs_dsize > DTX_INLINE_MBS_SIZE)
			D_ALLOC_PTR(dsp);
		else
			D_ALLOC(dsp, sizeof(*dsp) + sizeof(*mbs) + ent->ie_dtx_mbs_dsize);
		if (dsp == NULL)
			return -DER_NOMEM;

		dsp->dsp_xid = ent->ie_dtx_xid;
		dsp->dsp_oid = ent->ie_dtx_oid;
		dsp->dsp_epoch = ent->ie_epoch;
		dsp->dsp_version = ent->ie_dtx_ver;

		if (ent->ie_dtx_mbs_dsize > DTX_INLINE_MBS_SIZE)
			goto add;

		mbs = (struct dtx_memberships *)(dsp + 1);
		dsp->dsp_inline_mbs = 1;
		dsp->dsp_mbs = mbs;
	}

	mbs->dm_tgt_cnt = ent->ie_dtx_tgt_cnt;
	mbs->dm_grp_cnt = ent->ie_dtx_grp_cnt;
	mbs->dm_data_size = ent->ie_dtx_mbs_dsize;
	mbs->dm_flags = ent->ie_dtx_mbs_flags;
	mbs->dm_dte_flags = ent->ie_dtx_flags;
	memcpy(mbs->dm_data, ent->ie_dtx_mbs, ent->ie_dtx_mbs_dsize);

add:
	if (ent->ie_dtx_flags & DTE_PARTIAL_COMMITTED) {
		d_list_add_tail(&dpci->dpci_link, &dcca->dcca_pc_list);
		dcca->dcca_pc_count++;
	} else {
		d_list_add_tail(&dsp->dsp_link, &dcca->dcca_st_list);
		dcca->dcca_st_count++;
	}

	return 0;
}

static inline void
dtx_dpci_free(struct dtx_partial_cmt_item *dpci)
{
	if (dpci->dpci_inline_mbs == 0)
		D_FREE(dpci->dpci_dte.dte_mbs);

	D_FREE(dpci);
}

static void
dtx_cleanup(void *arg)
{
	struct dss_module_info		*dmi = dss_get_module_info();
	struct dtx_batched_cont_args	*dbca = arg;
	struct ds_cont_child		*cont = dbca->dbca_cont;
	struct dtx_share_peer		*dsp;
	struct dtx_partial_cmt_item	*dpci;
	struct dtx_entry		*dte;
	struct dtx_cleanup_cb_args	 dcca;
	daos_unit_oid_t			 oid;
	d_list_t			 cmt_list;
	d_list_t			 abt_list;
	d_list_t			 act_list;
	int				 count;
	int				 rc;

	if (dbca->dbca_cleanup_req == NULL)
		goto out;

	D_INIT_LIST_HEAD(&cmt_list);
	D_INIT_LIST_HEAD(&abt_list);
	D_INIT_LIST_HEAD(&act_list);
	D_INIT_LIST_HEAD(&dcca.dcca_st_list);
	D_INIT_LIST_HEAD(&dcca.dcca_pc_list);
	dcca.dcca_st_count = 0;
	dcca.dcca_pc_count = 0;
	/* Cleanup stale DTX entries within about 10 seconds windows each time. */
	dcca.dcca_cleanup_thd = dbca->dbca_cleanup_thd - 10;
	rc = ds_cont_iter(cont->sc_pool->spc_hdl, cont->sc_uuid,
			  dtx_cleanup_iter_cb, &dcca, VOS_ITER_DTX, 0);
	if (rc < 0)
		D_WARN("Failed to scan DTX entry for cleanup "
		       DF_UUID": "DF_RC"\n", DP_UUID(cont->sc_uuid), DP_RC(rc));

	while (!dss_ult_exiting(dbca->dbca_cleanup_req) && !d_list_empty(&dcca.dcca_st_list)) {
		if (dcca.dcca_st_count > DTX_REFRESH_MAX) {
			count = DTX_REFRESH_MAX;
			dcca.dcca_st_count -= DTX_REFRESH_MAX;
		} else {
			D_ASSERT(dcca.dcca_st_count > 0);

			count = dcca.dcca_st_count;
			dcca.dcca_st_count = 0;
		}

		/* Use false as the "failout" parameter that should guarantee
		 * that all the DTX entries in the check list will be handled
		 * even if some former ones hit failure.
		 */
		rc = dtx_refresh_internal(cont, &count, &dcca.dcca_st_list,
					  &cmt_list, &abt_list, &act_list, false);
		D_ASSERTF(count == 0, "%d entries are not handled: "DF_RC"\n",
			  count, DP_RC(rc));
	}

	D_ASSERT(d_list_empty(&cmt_list));
	D_ASSERT(d_list_empty(&abt_list));
	D_ASSERT(d_list_empty(&act_list));

	while (!dss_ult_exiting(dbca->dbca_cleanup_req) && !d_list_empty(&dcca.dcca_pc_list)) {
		dpci = d_list_pop_entry(&dcca.dcca_pc_list, struct dtx_partial_cmt_item, dpci_link);
		dcca.dcca_pc_count--;

		dte = &dpci->dpci_dte;
		if (dte->dte_mbs == NULL)
			rc = vos_dtx_load_mbs(cont->sc_hdl, &dte->dte_xid, &oid, &dte->dte_mbs);
		if (dte->dte_mbs != NULL) {
			if (dte->dte_mbs->dm_flags & DMF_COLL_TARGET) {
				struct dtx_coll_entry	*dce = NULL;

				rc = dtx_coll_prep(cont->sc_pool_uuid, oid, &dte->dte_xid,
						   dte->dte_mbs, dmi->dmi_tgt_id, dte->dte_ver,
						   cont->sc_pool->spc_map_version, false, true, &dce);
				if (rc == 0) {
					D_ASSERT(dce != NULL);

					rc = dtx_coll_commit(cont, dce, NULL, false);
					dtx_coll_entry_put(dce);
				}
			} else {
				rc = dtx_commit(cont, &dte, NULL, 1, false);
			}
		}

		D_DEBUG(DB_IO, "Cleanup partial committed DTX "DF_DTI", left %d: %d\n",
			DP_DTI(&dte->dte_xid), dcca.dcca_pc_count, rc);
		dtx_dpci_free(dpci);
	}

	while ((dsp = d_list_pop_entry(&dcca.dcca_st_list, struct dtx_share_peer,
				       dsp_link)) != NULL)
		dtx_dsp_free(dsp);

	while ((dpci = d_list_pop_entry(&dcca.dcca_pc_list, struct dtx_partial_cmt_item,
					dpci_link)) != NULL)
		dtx_dpci_free(dpci);

	dbca->dbca_cleanup_done = 1;

out:
	dtx_put_dbca(dbca);
}

static void
dtx_aggregate(void *arg)
{
	struct dtx_batched_cont_args	*dbca = arg;
	struct ds_cont_child		*cont = dbca->dbca_cont;

	if (dbca->dbca_agg_req == NULL)
		goto out;

	while (!dss_ult_exiting(dbca->dbca_agg_req)) {
		struct dtx_stat		stat = { 0 };
		int			rc;

		rc = vos_dtx_aggregate(cont->sc_hdl);
		if (rc != 0)
			break;

		ABT_thread_yield();

		dtx_stat(cont, &stat);

		/* If current container does not exceeds DTX thresholds,
		 * but the whole pool still exceeds the thresholds, then
		 * we need to choose a proper (maybe the same) container
		 * to do DTX aggregation.
		 */

		if (stat.dtx_cont_cmt_count == 0 ||
		    stat.dtx_first_cmt_blob_time_lo == 0 ||
		    (stat.dtx_cont_cmt_count <= dtx_agg_thd_cnt_lo &&
		     dtx_sec2age(stat.dtx_first_cmt_blob_time_lo) <= dtx_agg_thd_age_lo))
			break;

		if (dtx_sec2age(stat.dtx_first_cmt_blob_time_lo) <= DTX_AGG_AGE_PRESERVE)
			break;
	}

	dbca->dbca_agg_done = 1;

out:
	D_ASSERT(dbca->dbca_pool->dbpa_aggregating != 0);
	dbca->dbca_pool->dbpa_aggregating--;
	dtx_put_dbca(dbca);
}

static void
dtx_aggregation_pool(struct dss_module_info *dmi, struct dtx_batched_pool_args *dbpa)
{
	struct dtx_batched_cont_args	*dbca;
	struct ds_cont_child		*cont;
	struct sched_req_attr		 attr;
	struct dtx_batched_cont_args	*victim_dbca = NULL;
	struct dtx_stat			 victim_stat = { 0 };
	struct dtx_tls			*tls = dtx_tls_get();

	D_ASSERT(dbpa->dbpa_pool);
	sched_req_attr_init(&attr, SCHED_REQ_GC, &dbpa->dbpa_pool->spc_uuid);

	while (!dss_xstream_exiting(dmi->dmi_xstream)) {
		struct dtx_stat		 stat = { 0 };

		if (d_list_empty(&dbpa->dbpa_cont_list))
			return;

		dbca = d_list_entry(dbpa->dbpa_cont_list.next,
				    struct dtx_batched_cont_args,
				    dbca_pool_link);
		D_ASSERT(!dbca->dbca_deregister);

		if (dbca->dbca_agg_req != NULL && dbca->dbca_agg_done) {
			sched_req_put(dbca->dbca_agg_req);
			dbca->dbca_agg_req = NULL;
			dbca->dbca_agg_done = 0;
		}

		/* Finish this cycle scan. */
		if (dbca->dbca_agg_gen == tls->dt_agg_gen)
			break;

		dbca->dbca_agg_gen = tls->dt_agg_gen;
		d_list_move_tail(&dbca->dbca_pool_link, &dbpa->dbpa_cont_list);

		if (dbca->dbca_agg_req != NULL)
			continue;

		cont = dbca->dbca_cont;
		dtx_stat(cont, &stat);
		if (stat.dtx_cont_cmt_count == 0 ||
		    stat.dtx_first_cmt_blob_time_lo == 0)
			continue;

		if (dtx_sec2age(stat.dtx_first_cmt_blob_time_lo) <= DTX_AGG_AGE_PRESERVE)
			continue;

		if (stat.dtx_cont_cmt_count >= dtx_agg_thd_cnt_up ||
		    ((stat.dtx_cont_cmt_count > dtx_agg_thd_cnt_lo ||
		      stat.dtx_pool_cmt_count >= dtx_agg_thd_cnt_up) &&
		     (dtx_sec2age(stat.dtx_first_cmt_blob_time_lo) >= dtx_agg_thd_age_up))) {
			D_ASSERT(!dbca->dbca_agg_done);
			dtx_get_dbca(dbca);
			dbca->dbca_agg_req = sched_create_ult(&attr, dtx_aggregate, dbca, 0);
			if (dbca->dbca_agg_req == NULL) {
				D_WARN("Fail to start DTX agg ULT (1) for "DF_UUID"\n",
				       DP_UUID(cont->sc_uuid));
				dtx_put_dbca(dbca);
				continue;
			}

			dbpa->dbpa_aggregating++;
			continue;
		}

		if (victim_stat.dtx_first_cmt_blob_time_lo == 0 ||
		    victim_stat.dtx_first_cmt_blob_time_lo > stat.dtx_first_cmt_blob_time_lo ||
		    (victim_stat.dtx_first_cmt_blob_time_lo == stat.dtx_first_cmt_blob_time_lo &&
		     victim_stat.dtx_first_cmt_blob_time_up > stat.dtx_first_cmt_blob_time_up) ||
		    (victim_stat.dtx_first_cmt_blob_time_lo == stat.dtx_first_cmt_blob_time_lo &&
		     victim_stat.dtx_first_cmt_blob_time_up == stat.dtx_first_cmt_blob_time_up &&
		     victim_stat.dtx_cont_cmt_count < stat.dtx_cont_cmt_count)) {
			victim_stat = stat;
			victim_dbca = dbca;
		}
	}

	/* No single container exceeds DTX thresholds, but the whole pool does,
	 * then we choose the victim container to do the DTX aggregation.
	 */

	if (!dss_xstream_exiting(dmi->dmi_xstream) && dbpa->dbpa_aggregating == 0 &&
	    victim_dbca != NULL && victim_stat.dtx_pool_cmt_count >= dtx_agg_thd_cnt_up) {
		D_ASSERT(victim_dbca->dbca_agg_req == NULL && !victim_dbca->dbca_agg_done);

		dtx_get_dbca(victim_dbca);
		victim_dbca->dbca_agg_req = sched_create_ult(&attr, dtx_aggregate, victim_dbca, 0);
		if (victim_dbca->dbca_agg_req == NULL) {
			D_WARN("Fail to start DTX agg ULT (2) for "DF_UUID"\n",
				DP_UUID(victim_dbca->dbca_cont->sc_uuid));
			dtx_put_dbca(victim_dbca);
		} else {
			dbpa->dbpa_aggregating++;
		}
	}
}

void
dtx_aggregation_main(void *arg)
{
	struct dtx_tls			*tls = dtx_tls_get();
	struct dss_module_info		*dmi = dss_get_module_info();
	struct dtx_batched_pool_args	*dbpa;
	struct sched_req_attr		 attr;
	uuid_t				 anonym_uuid;

	uuid_clear(anonym_uuid);
	sched_req_attr_init(&attr, SCHED_REQ_ANONYM, &anonym_uuid);

	D_ASSERT(dmi->dmi_dtx_agg_req == NULL);
	dmi->dmi_dtx_agg_req = sched_req_get(&attr, ABT_THREAD_NULL);
	if (dmi->dmi_dtx_agg_req == NULL) {
		D_ERROR("Failed to get DTX aggregation sched request.\n");
		return;
	}

	while (1) {
		if (!d_list_empty(&dmi->dmi_dtx_batched_pool_list)) {
			dbpa = d_list_entry(dmi->dmi_dtx_batched_pool_list.next,
					    struct dtx_batched_pool_args,
					    dbpa_sys_link);
			d_list_move_tail(&dbpa->dbpa_sys_link,
					 &dmi->dmi_dtx_batched_pool_list);

			tls->dt_agg_gen++;
			dtx_aggregation_pool(dmi, dbpa);
		}

		if (dss_xstream_exiting(dmi->dmi_xstream))
			break;

		sched_req_sleep(dmi->dmi_dtx_agg_req, 500 /* ms */);
	}

	sched_req_put(dmi->dmi_dtx_agg_req);
	dmi->dmi_dtx_agg_req = NULL;
}

static inline bool
dtx_need_batched_commit(struct dtx_batched_cont_args *dbca)
{
	return dtx_cont_opened(dbca->dbca_cont) || dbca->dbca_flush_pending;
}

static void
dtx_batched_commit_one(void *arg)
{
	struct dss_module_info		*dmi = dss_get_module_info();
	struct dtx_tls			*tls = dtx_tls_get();
	struct dtx_batched_cont_args	*dbca = arg;
	struct ds_cont_child		*cont = dbca->dbca_cont;

	if (dbca->dbca_commit_req == NULL)
		goto out;

	tls->dt_batched_ult_cnt++;

	while (!dss_ult_exiting(dbca->dbca_commit_req) && dtx_need_batched_commit(dbca)) {
		struct dtx_entry	**dtes = NULL;
		struct dtx_coll_entry	 *dce = NULL;
		struct dtx_stat		  stat = { 0 };
		int			  cnt;
		int			  rc;

		cnt = dtx_fetch_committable(cont, DTX_THRESHOLD_COUNT, NULL,
					    DAOS_EPOCH_MAX, false, &dtes, NULL, &dce);
		if (cnt == 0) {
			if (dbca->dbca_flush_pending) {
				D_ASSERT(!dtx_cont_opened(cont));

				dbca->dbca_flush_pending = 0;
				d_list_del(&dbca->dbca_sys_link);
				d_list_add_tail(&dbca->dbca_sys_link,
						&dmi->dmi_dtx_batched_cont_close_list);
			}
			break;
		}

		if (cnt < 0) {
			D_WARN("Fail to fetch committable for "DF_UUID": "DF_RC"\n",
			       DP_UUID(cont->sc_uuid), DP_RC(cnt));
			break;
		}

		if (dce != NULL) {
			/* Currently, commit collective DTX one by one. */
			D_ASSERT(cnt == 1);

			rc = dtx_coll_commit(cont, dce, NULL, true);
		} else {
			rc = dtx_commit(cont, dtes, NULL, cnt, true);
		}
		dtx_free_committable(dtes, NULL, dce, cnt);
		if (rc != 0) {
			D_WARN("Fail to batched commit %d entries for "DF_UUID": "DF_RC"\n",
			       cnt, DP_UUID(cont->sc_uuid), DP_RC(rc));
			break;
		}

		dtx_stat(cont, &stat);

		if (stat.dtx_pool_cmt_count >= dtx_agg_thd_cnt_up &&
		    dbca->dbca_pool->dbpa_aggregating == 0)
			sched_req_wakeup(dmi->dmi_dtx_agg_req);

		if ((stat.dtx_committable_count <= DTX_THRESHOLD_COUNT) &&
		    (stat.dtx_committable_coll_count == 0) &&
		    (stat.dtx_oldest_committable_time == 0 ||
		     d_hlc_age2sec(stat.dtx_oldest_committable_time) <
		     DTX_COMMIT_THRESHOLD_AGE))
			break;
	}

	dbca->dbca_commit_done = 1;
	tls->dt_batched_ult_cnt--;

out:
	dtx_put_dbca(dbca);
}

void
dtx_batched_commit(void *arg)
{
	struct dss_module_info		*dmi = dss_get_module_info();
	struct dtx_tls			*tls = dtx_tls_get();
	struct dtx_batched_cont_args	*dbca;
	struct sched_req_attr		 attr;
	uuid_t				 anonym_uuid;

	uuid_clear(anonym_uuid);
	sched_req_attr_init(&attr, SCHED_REQ_ANONYM, &anonym_uuid);

	D_ASSERT(dmi->dmi_dtx_cmt_req == NULL);
	dmi->dmi_dtx_cmt_req = sched_req_get(&attr, ABT_THREAD_NULL);
	if (dmi->dmi_dtx_cmt_req == NULL) {
		D_ERROR("Failed to get DTX batched commit sched request.\n");
		return;
	}

	dmi->dmi_dtx_batched_started = 1;

	while (1) {
		struct ds_cont_child	*cont;
		struct dtx_stat		 stat = { 0 };
		int			 sleep_time = 50; /* ms */

		if (d_list_empty(&dmi->dmi_dtx_batched_cont_open_list)) {
			sleep_time = 500;
			goto check;
		}

		if (DAOS_FAIL_CHECK(DAOS_DTX_NO_BATCHED_CMT) ||
		    DAOS_FAIL_CHECK(DAOS_DTX_NO_COMMITTABLE))
			goto check;

		dbca = d_list_entry(dmi->dmi_dtx_batched_cont_open_list.next,
				    struct dtx_batched_cont_args, dbca_sys_link);
		D_ASSERT(!dbca->dbca_deregister);

		dtx_get_dbca(dbca);
		cont = dbca->dbca_cont;
		d_list_move_tail(&dbca->dbca_sys_link,
				 &dmi->dmi_dtx_batched_cont_open_list);
		dtx_stat(cont, &stat);

		if (dbca->dbca_commit_req != NULL && dbca->dbca_commit_done) {
			sched_req_put(dbca->dbca_commit_req);
			dbca->dbca_commit_req = NULL;
			dbca->dbca_commit_done = 0;
		}

		if (dtx_need_batched_commit(dbca) && dbca->dbca_commit_req == NULL &&
		    (dtx_batched_ult_max != 0 && tls->dt_batched_ult_cnt < dtx_batched_ult_max) &&
		    ((stat.dtx_committable_count > DTX_THRESHOLD_COUNT) ||
		     (stat.dtx_committable_coll_count > 0) ||
		     (stat.dtx_oldest_committable_time != 0 &&
		      d_hlc_age2sec(stat.dtx_oldest_committable_time) >=
			  DTX_COMMIT_THRESHOLD_AGE))) {
			D_ASSERT(!dbca->dbca_commit_done);
			sleep_time = 0;
			dtx_get_dbca(dbca);

			D_ASSERT(dbca->dbca_cont);
			sched_req_attr_init(&attr, SCHED_REQ_GC, &dbca->dbca_cont->sc_pool_uuid);
			dbca->dbca_commit_req = sched_create_ult(&attr, dtx_batched_commit_one,
								 dbca, 0);
			if (dbca->dbca_commit_req == NULL) {
				D_WARN("Fail to start DTX ULT (1) for "DF_UUID"\n",
				       DP_UUID(cont->sc_uuid));
				dtx_put_dbca(dbca);
			}
		}

		if (dbca->dbca_cleanup_req != NULL && dbca->dbca_cleanup_done) {
			sched_req_put(dbca->dbca_cleanup_req);
			dbca->dbca_cleanup_req = NULL;
			dbca->dbca_cleanup_done = 0;
		}

		if (dtx_cont_opened(cont) &&
		    !dbca->dbca_deregister && dbca->dbca_cleanup_req == NULL &&
		    stat.dtx_oldest_active_time != 0 &&
		    dtx_sec2age(stat.dtx_oldest_active_time) >= dbca->dbca_cleanup_thd) {
			D_ASSERT(!dbca->dbca_cleanup_done);
			dtx_get_dbca(dbca);

			D_ASSERT(dbca->dbca_cont);
			sched_req_attr_init(&attr, SCHED_REQ_GC, &dbca->dbca_cont->sc_pool_uuid);
			dbca->dbca_cleanup_req = sched_create_ult(&attr, dtx_cleanup, dbca, 0);
			if (dbca->dbca_cleanup_req == NULL) {
				D_WARN("Fail to start DTX ULT (3) for "DF_UUID"\n",
				       DP_UUID(cont->sc_uuid));
				dtx_put_dbca(dbca);
			}
		}

		dtx_put_dbca(dbca);

check:
		if (dss_xstream_exiting(dmi->dmi_xstream))
			break;

		sched_req_sleep(dmi->dmi_dtx_cmt_req, sleep_time);
	}

	sched_req_put(dmi->dmi_dtx_cmt_req);
	dmi->dmi_dtx_cmt_req = NULL;
	dmi->dmi_dtx_batched_started = 0;
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

	limit = d_hlc_epsilon_get_bound(epoch->oe_first);
	if (epoch->oe_value >= limit)
		/*
		 * The epoch is already out of the potential uncertainty
		 * window.
		 */
		return epoch->oe_value;

	return limit;
}

static void
dtx_shares_init(struct dtx_handle *dth)
{
	D_INIT_LIST_HEAD(&dth->dth_share_cmt_list);
	D_INIT_LIST_HEAD(&dth->dth_share_abt_list);
	D_INIT_LIST_HEAD(&dth->dth_share_act_list);
	D_INIT_LIST_HEAD(&dth->dth_share_tbd_list);
	dth->dth_share_tbd_count = 0;
	dth->dth_shares_inited = 1;
}

static void
dtx_shares_fini(struct dtx_handle *dth)
{
	struct dtx_share_peer	*dsp;

	if (!dth->dth_shares_inited)
		return;

	while ((dsp = d_list_pop_entry(&dth->dth_share_cmt_list,
				       struct dtx_share_peer,
				       dsp_link)) != NULL)
		dtx_dsp_free(dsp);

	while ((dsp = d_list_pop_entry(&dth->dth_share_abt_list,
				       struct dtx_share_peer,
				       dsp_link)) != NULL)
		dtx_dsp_free(dsp);

	while ((dsp = d_list_pop_entry(&dth->dth_share_act_list,
				       struct dtx_share_peer,
				       dsp_link)) != NULL)
		dtx_dsp_free(dsp);

	while ((dsp = d_list_pop_entry(&dth->dth_share_tbd_list,
				       struct dtx_share_peer,
				       dsp_link)) != NULL)
		dtx_dsp_free(dsp);

	dth->dth_share_tbd_count = 0;
}

int
dtx_handle_reinit(struct dtx_handle *dth)
{
	if (dth->dth_modification_cnt > 0) {
		D_ASSERT(dth->dth_ent != NULL);
		D_ASSERT(dth->dth_pinned != 0);
	}
	D_ASSERT(dth->dth_already == 0);

	dth->dth_modify_shared = 0;
	dth->dth_active = 0;
	dth->dth_touched_leader_oid = 0;
	dth->dth_local_tx_started = 0;
	dth->dth_cos_done = 0;
	dth->dth_aborted = 0;

	dth->dth_op_seq = 0;
	dth->dth_oid_cnt = 0;
	dth->dth_oid_cap = 0;
	D_FREE(dth->dth_oid_array);
	dth->dth_dkey_hash = 0;
	vos_dtx_rsrvd_fini(dth);

	return vos_dtx_rsrvd_init(dth);
}

/**
 * Init local dth handle.
 */
static int
dtx_handle_init(struct dtx_id *dti, daos_handle_t xoh, struct dtx_epoch *epoch, bool leader,
		uint16_t sub_modification_cnt, uint32_t pm_ver, daos_unit_oid_t *leader_oid,
		struct dtx_id *dti_cos, int dti_cos_cnt, uint32_t flags,
		struct dtx_memberships *mbs, struct dtx_handle *dth)
{
	int rc;

	if (sub_modification_cnt > DTX_SUB_MOD_MAX) {
		D_ERROR("Too many modifications in a single transaction:"
			"%u > %u\n", sub_modification_cnt, DTX_SUB_MOD_MAX);
		return -DER_OVERFLOW;
	}
	dth->dth_modification_cnt = sub_modification_cnt;

	dtx_shares_init(dth);

	if (flags & DTX_LOCAL) {
		dth->dth_xid.dti_hlc = 1;
		dth->dth_poh         = xoh;
	} else {
		dth->dth_xid        = *dti;
		dth->dth_leader_oid = *leader_oid;
		dth->dth_coh        = xoh;
	}

	dth->dth_ver = pm_ver;
	dth->dth_refs = 1;
	dth->dth_mbs = mbs;

	dth->dth_pinned = 0;
	dth->dth_cos_done = 0;
	dth->dth_modify_shared = 0;
	dth->dth_active = 0;
	dth->dth_touched_leader_oid = 0;
	dth->dth_local_tx_started = 0;
	dth->dth_solo = (flags & DTX_SOLO) ? 1 : 0;
	dth->dth_drop_cmt = (flags & DTX_DROP_CMT) ? 1 : 0;
	dth->dth_dist = (flags & DTX_DIST) ? 1 : 0;
	dth->dth_for_migration = (flags & DTX_FOR_MIGRATION) ? 1 : 0;
	dth->dth_ignore_uncommitted = (flags & DTX_IGNORE_UNCOMMITTED) ? 1 : 0;
	dth->dth_prepared = (flags & DTX_PREPARED) ? 1 : 0;
	dth->dth_epoch_owner = (flags & DTX_EPOCH_OWNER) ? 1 : 0;
	dth->dth_aborted = 0;
	dth->dth_already = 0;
	dth->dth_need_validation = 0;
	dth->dth_local              = (flags & DTX_LOCAL) ? 1 : 0;

	dth->dth_dti_cos = dti_cos;
	dth->dth_dti_cos_count = dti_cos_cnt;
	dth->dth_ent = NULL;
	dth->dth_flags = leader ? DTE_LEADER : 0;

	if (flags & DTX_SYNC) {
		dth->dth_flags |= DTE_BLOCK;
		dth->dth_sync = 1;
	} else {
		dth->dth_sync = 0;
	}

	dth->dth_op_seq = 0;
	dth->dth_oid_cnt = 0;
	dth->dth_oid_cap = 0;
	dth->dth_oid_array = NULL;

	dth->dth_dkey_hash = 0;

	if (!(flags & DTX_LOCAL)) {
		if (daos_is_zero_dti(dti))
			return 0;

		if (!dtx_epoch_chosen(epoch)) {
			D_ERROR("initializing DTX " DF_DTI " with invalid epoch: value=" DF_U64
				" first=" DF_U64 " flags=%x\n",
				DP_DTI(dti), epoch->oe_value, epoch->oe_first, epoch->oe_flags);
			return -DER_INVAL;
		}
		dth->dth_epoch       = epoch->oe_value;
		dth->dth_epoch_bound = dtx_epoch_bound(epoch);
	}

	rc = vos_dtx_rsrvd_init(dth);
	if (rc != 0) {
		D_ERROR("Failed to allocate space for scm reservations: rc=" DF_RC "\n", DP_RC(rc));
		return rc;
	}

	if (flags & DTX_LOCAL) {
		rc = vos_dtx_local_begin(dth, xoh);
		if (rc) {
			goto error;
		}
	}

	return 0;

error:
	vos_dtx_rsrvd_fini(dth);
	return rc;
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

void
dtx_renew_epoch(struct dtx_epoch *epoch, struct dtx_handle *dth)
{
	dth->dth_epoch = epoch->oe_value;
	dth->dth_epoch_bound = dtx_epoch_bound(epoch);
	vos_dtx_renew_epoch(dth);
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

	if (dth->dth_op_seq == VOS_SUB_OP_MAX) {
		D_ERROR("Transaction exceeds maximum number of suboperations"
			" (%d)\n", VOS_SUB_OP_MAX);
		return -DER_NO_PERM;
	}

	dth->dth_dkey_hash = dkey_hash;
	dth->dth_op_seq++;

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
 * \param coh		[IN]	Container handle.
 * \param dti		[IN]	The DTX identifier.
 * \param epoch		[IN]	Epoch for the DTX.
 * \param sub_modification_cnt
 *			[IN]	Sub modifications count
 * \param pm_ver	[IN]	Pool map version for the DTX.
 * \param leader_oid	[IN]	The object ID is used to elect the DTX leader.
 * \param dti_cos	[IN]	The DTX array to be committed because of shared.
 * \param dti_cos_cnt	[IN]	The @dti_cos array size.
 * \param tgts		[IN]	targets for distribute transaction.
 * \param tgt_cnt	[IN]	number of targets (not count the leader itself).
 * \param flags		[IN]	See dtx_flags.
 * \param mbs		[IN]	DTX participants information.
 * \param dce		[IN]	The pointer to collective DTX entry.
 * \param p_dlh		[OUT]	Pointer to the DTX handle.
 *
 * \return			Zero on success, negative value if error.
 */
int
dtx_leader_begin(daos_handle_t coh, struct dtx_id *dti, struct dtx_epoch *epoch,
		 uint16_t sub_modification_cnt, uint32_t pm_ver, daos_unit_oid_t *leader_oid,
		 struct dtx_id *dti_cos, int dti_cos_cnt, struct daos_shard_tgt *tgts, int tgt_cnt,
		 uint32_t flags, struct dtx_memberships *mbs, struct dtx_coll_entry *dce,
		 struct dtx_leader_handle **p_dlh)
{
	struct dtx_leader_handle	*dlh;
	struct dtx_handle		*dth;
	int				 rc;
	int				 i;

	D_ALLOC(dlh, sizeof(*dlh) + sizeof(struct dtx_sub_status) * tgt_cnt);
	if (dlh == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dlh->dlh_future = ABT_FUTURE_NULL;
	dlh->dlh_coll_entry = dce;
	if (flags & DTX_TGT_COLL)
		dlh->dlh_coll = 1;

	if (tgt_cnt > 0) {
		dlh->dlh_subs = (struct dtx_sub_status *)(dlh + 1);

		if (flags & DTX_TGT_COLL) {
			/*
			 * NOTE: Do not support DTF_DELAY_FORWARD for collective DTX.
			 *	 The target information will be filled sometime later
			 *	 when dispatch related IO request.
			 */
			dlh->dlh_delay_sub_cnt = 0;
			dlh->dlh_normal_sub_cnt = tgt_cnt;
		} else {
			for (i = 0; i < tgt_cnt; i++) {
				dlh->dlh_subs[i].dss_tgt = tgts[i];
				if (unlikely(tgts[i].st_flags & DTF_DELAY_FORWARD))
					dlh->dlh_delay_sub_cnt++;
			}

			dlh->dlh_normal_sub_cnt = tgt_cnt - dlh->dlh_delay_sub_cnt;
		}
	}

	if (flags & DTX_RELAY)
		dlh->dlh_relay = 1;

	dth = &dlh->dlh_handle;
	rc = dtx_handle_init(dti, coh, epoch, dlh->dlh_relay ? false : true, sub_modification_cnt,
			     pm_ver, leader_oid, dti_cos, dti_cos_cnt, flags, mbs, dth);
	if (rc == 0 && sub_modification_cnt > 0)
		rc = vos_dtx_attach(dth, false, (flags & DTX_PREPARED) ? true : false);

out:
	DL_CDEBUG(rc != 0, DLOG_ERR, DB_IO, rc,
		  "Start (%s) DTX " DF_DTI " sub modification %d, "
		  "ver %u, eph " DF_X64 ", leader " DF_UOID ", cos_cnt %d, tgt_cnt %d, flags %x: ",
		  flags & DTX_TGT_COLL ? (flags & DTX_RELAY ? "relay" : "collective") : "regular",
		  DP_DTI(dti), sub_modification_cnt, pm_ver, epoch->oe_value, DP_UOID(*leader_oid),
		  dti_cos_cnt, tgt_cnt, flags);

	if (rc != 0) {
		D_FREE(dlh);
	} else {
		*p_dlh = dlh;
		d_tm_inc_gauge(dtx_tls_get()->dt_dtx_leader_total, 1);
	}

	return rc;
}

static int
dtx_leader_wait(struct dtx_leader_handle *dlh)
{
	int	rc;

	if (dlh->dlh_future != ABT_FUTURE_NULL) {
		rc = ABT_future_wait(dlh->dlh_future);
		D_ASSERTF(rc == ABT_SUCCESS,
			  "ABT_future_wait failed %d.\n", rc);

		ABT_future_free(&dlh->dlh_future);
	}

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
dtx_leader_end(struct dtx_leader_handle *dlh, struct ds_cont_child *cont, int result)
{
	struct dtx_handle		*dth = &dlh->dlh_handle;
	struct dtx_entry		*dte;
	struct dtx_memberships		*mbs;
	size_t				 size;
	uint32_t			 flags;
	int				 status = -1;
	int				 rc = 0;
	bool				 aborted = false;

	D_ASSERT(cont != NULL);

	dtx_shares_fini(dth);

	if (daos_is_zero_dti(&dth->dth_xid) || unlikely(result == -DER_ALREADY))
		goto out;

	/* For solo transaction, the validation has already been processed inside vos
	 * when necessary. That is enough, do not need to revalid again.
	 */
	if (dth->dth_solo)
		goto out;

	if (dth->dth_need_validation) {
		/* During waiting for bulk data transfer or other non-leaders, the DTX
		 * status may be changes by others (such as DTX resync or DTX refresh)
		 * by race. Let's check it before handling the case of 'result < 0' to
		 * avoid aborting 'ready' one.
		 */
		status = vos_dtx_validation(dth);
		if (unlikely(status == DTX_ST_COMMITTED || status == DTX_ST_COMMITTABLE ||
			     status == DTX_ST_COMMITTING))
			D_GOTO(out, result = -DER_ALREADY);
	}

	if (result < 0)
		goto abort;

	switch (status) {
	case -1:
		break;
	case DTX_ST_PREPARED:
		if (likely(!dth->dth_aborted))
			break;
		/* Fall through */
	case DTX_ST_INITED:
	case DTX_ST_PREPARING:
		aborted = true;
		result = -DER_AGAIN;
		goto out;
	case DTX_ST_ABORTED:
	case DTX_ST_ABORTING:
		aborted = true;
		result = -DER_INPROGRESS;
		goto out;
	default:
		D_ASSERTF(0, "Unexpected DTX "DF_DTI" status %d\n", DP_DTI(&dth->dth_xid), status);
	}

	if (dlh->dlh_relay)
		goto out;

	/*
	 * Even if the transaction modifies nothing locally, we still need to store
	 * it persistently. Otherwise, the subsequent DTX resync may not find it as
	 * to regard it as failed transaction and abort it.
	 */
	if (!dth->dth_active && !dth->dth_prepared &&
	    (dth->dth_dist || dth->dth_modification_cnt > 0)) {
		result = vos_dtx_attach(dth, true, dth->dth_ent != NULL ? true : false);
		if (unlikely(result < 0)) {
			D_ERROR(DF_UUID": Fail to persistently store DTX "DF_DTI": "DF_RC"\n",
				DP_UUID(cont->sc_uuid), DP_DTI(&dth->dth_xid), DP_RC(result));
			goto abort;
		}
	}

	if ((dth->dth_prepared && !dlh->dlh_coll) || dtx_batched_ult_max == 0) {
		dth->dth_sync = 1;
		goto sync;
	}

	if (DAOS_FAIL_CHECK(DAOS_DTX_SKIP_PREPARE))
		D_GOTO(abort, result = 0);

	if (DAOS_FAIL_CHECK(DAOS_DTX_MISS_ABORT))
		D_GOTO(abort, result = -DER_IO);

	if (DAOS_FAIL_CHECK(DAOS_DTX_MISS_COMMIT))
		dth->dth_sync = 1;

	if (dth->dth_sync)
		goto sync;

	D_ASSERT(dth->dth_mbs != NULL);

cache:
	if (dlh->dlh_coll) {
		rc = dtx_cos_add(cont, dlh->dlh_coll_entry, &dth->dth_leader_oid,
				 dth->dth_dkey_hash, dth->dth_epoch, DCF_EXP_CMT | DCF_COLL);
	} else {
		size = sizeof(*dte) + sizeof(*mbs) + dth->dth_mbs->dm_data_size;
		D_ALLOC(dte, size);
		if (dte == NULL) {
			rc = -DER_NOMEM;
		} else {
			mbs = (struct dtx_memberships *)(dte + 1);
			memcpy(mbs, dth->dth_mbs, size - sizeof(*dte));

			dte->dte_xid = dth->dth_xid;
			dte->dte_ver = dth->dth_ver;
			dte->dte_refs = 1;
			dte->dte_mbs = mbs;

			if (!(mbs->dm_flags & DMF_SRDG_REP))
				flags = DCF_EXP_CMT;
			else if (dth->dth_modify_shared)
				flags = DCF_SHARED;
			else
				flags = 0;

			rc = dtx_cos_add(cont, dte, &dth->dth_leader_oid, dth->dth_dkey_hash,
					 dth->dth_epoch, flags);
			dtx_entry_put(dte);
		}
	}

	/*
	 * NOTE: If we failed to add the committable DTX into CoS cache, then we also have no way
	 *	 to commit (or abort) the DTX because of out of memory. Such DTX will be finally
	 *	 committed via next DTX resync (after recovered from OOM).
	 *
	 *	 Here, we only warning to notify the trouble, but not failed the transaction.
	 */
	if (rc != 0) {
		D_WARN(DF_UUID": Fail to cache %s DTX "DF_DTI": "DF_RC"\n",
		       DP_UUID(cont->sc_uuid), dlh->dlh_coll ? "collective" : "regular",
		       DP_DTI(&dth->dth_xid), DP_RC(rc));
		D_GOTO(out, result = 0);
	}

	if (!DAOS_FAIL_CHECK(DAOS_DTX_NO_COMMITTABLE)) {
		vos_dtx_mark_committable(dth);
		if (cont->sc_dtx_committable_count > DTX_THRESHOLD_COUNT || dlh->dlh_coll)
			sched_req_wakeup(dss_get_module_info()->dmi_dtx_cmt_req);
	}

sync:
	if (dth->dth_sync) {
		/*
		 * TBD: We need to reserve some space to guarantee that the local commit can be
		 *	done successfully. That is not only for sync commit, but also for async
		 *	batched commit.
		 */
		vos_dtx_mark_committable(dth);

		if (dlh->dlh_coll) {
			rc = dtx_coll_commit(cont, dlh->dlh_coll_entry, NULL, false);
		} else {
			dte = &dth->dth_dte;
			rc = dtx_commit(cont, &dte, NULL, 1, false);
		}

		if (rc != 0) {
			D_WARN(DF_UUID": Fail to sync %s commit DTX "DF_DTI": "DF_RC"\n",
			       DP_UUID(cont->sc_uuid), dlh->dlh_coll ? "collective" : "regular",
			       DP_DTI(&dth->dth_xid), DP_RC(rc));
			if (likely(dtx_batched_ult_max != 0)) {
				dth->dth_sync = 0;
				goto cache;
			}
		}

		/*
		 * NOTE: The semantics of 'sync' commit does not guarantee that all
		 *	 participants of the DTX can commit it on each local target
		 *	 successfully, instead, we try to commit the DTX immediately
		 *	 after all participants claiming 'prepared'. But even if we
		 *	 failed to commit it, we will not rollback the commit since
		 *	 the DTX has been marked as 'committable' and may has been
		 *	 accessed by others. The subsequent dtx_cleanup logic will
		 *	 handle (re-commit) current failed commit.
		 */
		D_GOTO(out, result = 0);
	}

abort:
	/* If some remote participant ask retry. We do not make such participant
	 * to locally retry for avoiding related forwarded RPC timeout, instead,
	 * The leader will trigger retry globally without abort 'prepared' ones.
	 */
	if (result < 0 && result != -DER_AGAIN && !dth->dth_solo) {
		/* 1. Drop partial modification for distributed transaction.
		 * 2. Remove the pinned DTX entry.
		 */
		vos_dtx_cleanup(dth, true);
		if (dlh->dlh_coll)
			dtx_coll_abort(cont, dlh->dlh_coll_entry, dth->dth_epoch);
		else
			dtx_abort(cont, &dth->dth_dte, dth->dth_epoch);
		aborted = true;
	}

out:
	if (unlikely(result == -DER_ALREADY))
		result = 0;

	if (!daos_is_zero_dti(&dth->dth_xid)) {
		/* Drop partial modification and remove the pinned DTX entry. */
		if (result < 0 && !aborted && dth->dth_modification_cnt > 0)
			vos_dtx_cleanup(dth, true);

		/* For solo DTX, just let client retry for DER_AGAIN case. */
		if (result == -DER_AGAIN && dth->dth_solo)
			result = -DER_INPROGRESS;

		vos_dtx_rsrvd_fini(dth);
		vos_dtx_detach(dth);
	}

	D_ASSERTF(result <= 0, "unexpected return value %d\n", result);

	/* If piggyback DTX has been done everywhere, then need to handle CoS cache.
	 * It is harmless to keep some partially committed DTX entries in CoS cache.
	 */
	dtx_cos_put_piggyback(cont, &dth->dth_leader_oid, dth->dth_dkey_hash, dth->dth_dti_cos,
			      dth->dth_dti_cos_count,
			      (result == 0 && dth->dth_cos_done) ? true : false);

	D_DEBUG(DB_IO, "Stop the DTX "DF_DTI" ver %u, dkey %lu, %s, cos %d/%d: result "DF_RC"\n",
		DP_DTI(&dth->dth_xid), dth->dth_ver, (unsigned long)dth->dth_dkey_hash,
		dth->dth_sync ? "sync" : "async", dth->dth_dti_cos_count,
		dth->dth_cos_done ? dth->dth_dti_cos_count : 0, DP_RC(result));

	D_FREE(dth->dth_oid_array);
	D_FREE(dlh);
	d_tm_dec_gauge(dtx_tls_get()->dt_dtx_leader_total, 1);

	return result;
}

/**
 * Prepare the DTX handle in DRAM.
 *
 * \param xoh		[IN]	Container handle or pool handle.
 * \param dti		[IN]	The DTX identifier.
 * \param epoch		[IN]	Epoch for the DTX.
 * \param sub_modification_cnt
 *			[IN]	Sub modifications count.
 * \param pm_ver	[IN]	Pool map version for the DTX.
 * \param leader_oid	[IN]    The object ID is used to elect the DTX leader.
 * \param dti_cos	[IN]	The DTX array to be committed because of shared.
 * \param dti_cos_cnt	[IN]	The @dti_cos array size.
 * \param flags		[IN]	See dtx_flags.
 * \param mbs		[IN]	DTX participants information.
 * \param p_dth		[OUT]	Pointer to the DTX handle.
 *
 * \return			Zero on success, negative value if error.
 */
int
dtx_begin(daos_handle_t xoh, struct dtx_id *dti, struct dtx_epoch *epoch,
	  uint16_t sub_modification_cnt, uint32_t pm_ver, daos_unit_oid_t *leader_oid,
	  struct dtx_id *dti_cos, int dti_cos_cnt, uint32_t flags, struct dtx_memberships *mbs,
	  struct dtx_handle **p_dth)
{
	struct dtx_handle	*dth;
	int			 rc;

	D_ALLOC(dth, sizeof(*dth));
	if (dth == NULL)
		return -DER_NOMEM;

	rc = dtx_handle_init(dti, xoh, epoch, false, sub_modification_cnt, pm_ver, leader_oid,
			     dti_cos, dti_cos_cnt, flags, mbs, dth);
	if (rc == 0 && sub_modification_cnt > 0 && !(flags & DTX_LOCAL))
		rc = vos_dtx_attach(dth, false, false);

	if (flags & DTX_LOCAL) {
		D_DEBUG(DB_IO, "Start local DTX sub modification %d, ver %u, flags %x: " DF_RC "\n",
			sub_modification_cnt, dth->dth_ver, flags, DP_RC(rc));
	} else {
		D_DEBUG(DB_IO,
			"Start DTX " DF_DTI " sub modification %d, ver %u, epoch " DF_X64
			", dti_cos_cnt %d, flags %x: " DF_RC "\n",
			DP_DTI(dti), sub_modification_cnt, dth->dth_ver, epoch->oe_value,
			dti_cos_cnt, flags, DP_RC(rc));
	}

	if (rc != 0)
		D_FREE(dth);
	else
		*p_dth = dth;

	return rc;
}

int
dtx_end(struct dtx_handle *dth, struct ds_cont_child *cont, int result)
{
	D_ASSERT(dth != NULL);

	dtx_shares_fini(dth);

	if (daos_is_zero_dti(&dth->dth_xid))
		goto out;

	if (dth->dth_local) {
		result = vos_dtx_local_end(dth, result);
		D_DEBUG(DB_IO, "Stop the local transaction ver %u: " DF_RC "\n", dth->dth_ver,
			DP_RC(result));
		goto fini;
	}

	/*
	 * Even if the transaction modifies nothing locally, we still need to store
	 * it persistently. Otherwise, the subsequent DTX resync may not find it as
	 * to regard it as failed transaction and abort it.
	 */
	if (result == 0 && !dth->dth_active && (dth->dth_dist || dth->dth_modification_cnt > 0))
		result = vos_dtx_attach(dth, true, dth->dth_ent != NULL ? true : false);

	if (result < 0) {
		if (dth->dth_dti_cos_count > 0 && !dth->dth_cos_done) {
			int	rc;

			/* NOTE: For non-leader participant, even if we fail to make
			 *	 related modification for some reason, we still need
			 *	 to commit the piggyback DTXs those may have already
			 *	 been committed on other participants.
			 *	 For leader case, it is not important even if we fail
			 *	 to commit them, because they are still in CoS cache,
			 *	 and can be committed next time.
			 */
			rc = vos_dtx_commit(cont->sc_hdl, dth->dth_dti_cos,
					    dth->dth_dti_cos_count, false, NULL);
			if (rc < 0)
				D_ERROR(DF_UUID": Fail to DTX CoS commit: %d\n",
					DP_UUID(cont->sc_uuid), rc);
			else
				dth->dth_cos_done = 1;
		}

		/* Drop partial modification and remove the pinned DTX entry. */
		if (dth->dth_modification_cnt > 0)
			vos_dtx_cleanup(dth, true);

		D_DEBUG(DB_IO, "Stop the DTX " DF_DTI " ver %u, dkey %lu: " DF_RC "\n",
			DP_DTI(&dth->dth_xid), dth->dth_ver, (unsigned long)dth->dth_dkey_hash,
			DP_RC(result));
	}

fini:
	D_ASSERTF(result <= 0, "unexpected return value %d\n", result);

	vos_dtx_rsrvd_fini(dth);
	vos_dtx_detach(dth);

out:
	D_FREE(dth->dth_oid_array);
	D_FREE(dth);

	return result;
}

#define DTX_COS_BTREE_ORDER		23

static void
dtx_flush_on_close(struct dss_module_info *dmi, struct dtx_batched_cont_args *dbca)
{
	struct dss_xstream	*dx = dss_current_xstream();
	struct ds_cont_child	*cont = dbca->dbca_cont;
	struct dtx_stat		 stat = { 0 };
	uint64_t		 total = 0;
	int			 cnt;
	int			 rc = 0;

	dbca->dbca_flush_pending = 1;
	dtx_stat(cont, &stat);

	while (!dss_xstream_exiting(dx) && !dtx_cont_opened(cont) && rc >= 0) {
		struct dtx_entry	**dtes = NULL;
		struct dtx_cos_key	 *dcks = NULL;
		struct dtx_coll_entry	 *dce = NULL;

		cnt = dtx_fetch_committable(cont, DTX_THRESHOLD_COUNT,
					    NULL, DAOS_EPOCH_MAX, true, &dtes,
					    dbca->dbca_commit_req != NULL ? &dcks : NULL, &dce);
		if (cnt <= 0)
			D_GOTO(out, rc = cnt);

		total += cnt;
		/* When flush_on_deregister, nobody will add more DTX
		 * into the CoS cache. So if accumulated commit count
		 * is more than the total committable ones, then some
		 * DTX entries cannot be removed from the CoS cache.
		 * Under such case, have to break the dtx_flush.
		 */
		if (unlikely(total > stat.dtx_committable_count)) {
			D_WARN("Some DTX in CoS cannot be committed: %lu/%lu\n",
			       (unsigned long)total, (unsigned long)stat.dtx_committable_count);
			dtx_free_committable(dtes, dcks, dce, cnt);
			D_GOTO(out, rc = -DER_MISC);
		}

		if (dce != NULL) {
			D_ASSERT(cnt == 1);

			rc = dtx_coll_commit(cont, dce, dcks, true);
		} else {
			rc = dtx_commit(cont, dtes, dcks, cnt, true);
		}
		dtx_free_committable(dtes, dcks, dce, cnt);
	}

out:
	if (rc < 0) {
		D_ERROR(DF_UUID": Fail to flush CoS cache: rc = %d\n",
			DP_UUID(cont->sc_uuid), rc);
		if (likely(!dtx_cont_opened(cont))) {
			d_list_del(&dbca->dbca_sys_link);
			/* Give it to the batched commit for further handling asynchronously. */
			d_list_add_tail(&dbca->dbca_sys_link, &dmi->dmi_dtx_batched_cont_open_list);
		}
	} else {
		dbca->dbca_flush_pending = 0;
	}
}

/* Per VOS container DTX re-index ULT ***************************************/

void
dtx_reindex_ult(void *arg)
{
	struct ds_cont_child		*cont	= arg;
	struct dss_module_info		*dmi	= dss_get_module_info();
	int				 rc	= 0;

	D_DEBUG(DB_MD, DF_CONT": starting DTX reindex ULT on xstream %d, ver %u\n",
		DP_CONT(NULL, cont->sc_uuid), dmi->dmi_tgt_id, dtx_cont2ver(cont));

	while (!cont->sc_dtx_reindex_abort && !dss_xstream_exiting(dmi->dmi_xstream)) {
		rc = vos_dtx_cmt_reindex(cont->sc_hdl);
		if (rc != 0)
			break;

		ABT_thread_yield();
	}

	D_CDEBUG(rc < 0, DLOG_ERR, DLOG_DBG,
		 DF_CONT": stopping DTX reindex ULT on stream %d, ver %u: rc = %d\n",
		 DP_CONT(NULL, cont->sc_uuid), dmi->dmi_tgt_id, dtx_cont2ver(cont), rc);

	cont->sc_dtx_reindex = 0;
	ds_cont_child_put(cont);
}

int
start_dtx_reindex_ult(struct ds_cont_child *cont)
{
	int rc;

	D_ASSERT(cont != NULL);

	/* Someone is trying to stop former DTX reindex ULT, wait until its done. */
	while (cont->sc_dtx_reindex_abort)
		ABT_thread_yield();

	if (cont->sc_stopping)
		return -DER_SHUTDOWN;

	cont->sc_dtx_delay_reset = 0;
	if (cont->sc_dtx_reindex)
		return 0;

	ds_cont_child_get(cont);
	cont->sc_dtx_reindex = 1;
	rc = dss_ult_create(dtx_reindex_ult, cont, DSS_XS_SELF, 0, 0, NULL);
	if (rc != 0) {
		D_ERROR(DF_UUID": Failed to create DTX reindex ULT: "DF_RC"\n",
			DP_UUID(cont->sc_uuid), DP_RC(rc));
		cont->sc_dtx_reindex = 0;
		ds_cont_child_put(cont);
	}

	return rc;
}

void
stop_dtx_reindex_ult(struct ds_cont_child *cont, bool force)
{
	/* DTX reindex has been done or not has not been started. */
	if (!cont->sc_dtx_reindex)
		return;

	/* Do not stop DTX reindex if the container is still opened. */
	if (dtx_cont_opened(cont))
		return;

	/*
	 * For non-force case, do not stop DTX re-index if DTX resync
	 * is in-progress. Related DTX resource will be released after
	 * DTX resync globally done (via rebuild scanning).
	 */
	if (unlikely(cont->sc_dtx_resyncing && !force)) {
		cont->sc_dtx_delay_reset = 1;
		return;
	}

	cont->sc_dtx_reindex_abort = 1;

	while (cont->sc_dtx_reindex)
		ABT_thread_yield();

	cont->sc_dtx_reindex_abort = 0;
}

int
dtx_cont_register(struct ds_cont_child *cont)
{
	struct dtx_tls			*tls = dtx_tls_get();
	struct dss_module_info		*dmi = dss_get_module_info();
	struct dtx_batched_pool_args	*dbpa = NULL;
	struct dtx_batched_cont_args	*dbca = NULL;
	struct umem_attr		 uma;
	uint32_t			 timeout;
	int				 rc;
	bool				 new_pool = true;

	if (unlikely((!dmi->dmi_dtx_batched_started)))
		return -DER_SHUTDOWN;

	D_ASSERT(cont != NULL);
	D_ASSERT(!dtx_cont_opened(cont));
	D_ASSERT(daos_handle_is_inval(cont->sc_dtx_cos_hdl));

	d_list_for_each_entry(dbpa, &dmi->dmi_dtx_batched_pool_list, dbpa_sys_link) {
		if (dbpa->dbpa_pool == cont->sc_pool) {
			/* NOT allow one container to register more than
			 * once unless its former registered instance has
			 * already deregistered.
			 */
			d_list_for_each_entry(dbca, &dbpa->dbpa_cont_list,
					      dbca_pool_link)
				D_ASSERT(dbca->dbca_cont != cont);
			new_pool = false;
			break;
		}
	}

	if (new_pool) {
		D_ALLOC_PTR(dbpa);
		if (dbpa == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		D_INIT_LIST_HEAD(&dbpa->dbpa_sys_link);
		D_INIT_LIST_HEAD(&dbpa->dbpa_cont_list);
		dbpa->dbpa_pool = cont->sc_pool;
	}

	D_ALLOC_PTR(dbca);
	if (dbca == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = crt_context_get_timeout(dmi->dmi_ctx, &timeout);
	if (rc != 0) {
		D_ERROR("Failed to get DTX cleanup timeout: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	/*
	 * Give related DTX leader sometime after default RPC timeout to commit or abort
	 * the DTX. If the DTX is still prepared after that, then trigger DTX cleanup to
	 * handle potential stale DTX entries.
	 */
	dbca->dbca_cleanup_thd = timeout + DTX_COMMIT_THRESHOLD_AGE * 2;

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
		D_GOTO(out, rc = -DER_NOMEM);
	}

	ds_cont_child_get(cont);
	dbca->dbca_refs = 0;
	dbca->dbca_cont = cont;
	dbca->dbca_pool = dbpa;
	dbca->dbca_agg_gen = tls->dt_agg_gen;
	d_list_add_tail(&dbca->dbca_sys_link, &dmi->dmi_dtx_batched_cont_close_list);
	d_list_add_tail(&dbca->dbca_pool_link, &dbpa->dbpa_cont_list);
	if (new_pool)
		d_list_add_tail(&dbpa->dbpa_sys_link, &dmi->dmi_dtx_batched_pool_list);

out:
	if (rc == 0) {
		cont->sc_dtx_registered = 1;
	} else {
		D_FREE(dbca);
		if (new_pool)
			D_FREE(dbpa);
	}

	return rc;
}

void
dtx_cont_deregister(struct ds_cont_child *cont)
{
	struct dss_module_info		*dmi = dss_get_module_info();
	struct dtx_batched_pool_args	*dbpa;
	struct dtx_batched_cont_args	*dbca;

	D_ASSERT(cont != NULL);
	D_ASSERT(!dtx_cont_opened(cont));

	d_list_for_each_entry(dbpa, &dmi->dmi_dtx_batched_pool_list, dbpa_sys_link) {
		if (dbpa->dbpa_pool != cont->sc_pool)
			continue;

		d_list_for_each_entry(dbca, &dbpa->dbpa_cont_list, dbca_pool_link) {
			if (dbca->dbca_cont == cont) {
				d_list_del_init(&dbca->dbca_sys_link);
				d_list_del_init(&dbca->dbca_pool_link);
				dbca->dbca_deregister = 1;
				dtx_free_dbca(dbca);
				return;
			}
		}
	}
}

int
dtx_cont_open(struct ds_cont_child *cont)
{
	struct dss_module_info		*dmi = dss_get_module_info();
	struct dtx_batched_pool_args	*dbpa;
	struct dtx_batched_cont_args	*dbca;
	int				 rc;

	D_ASSERT(cont != NULL);
	D_ASSERT(cont->sc_open == 1);

	d_list_for_each_entry(dbpa, &dmi->dmi_dtx_batched_pool_list, dbpa_sys_link) {
		if (dbpa->dbpa_pool != cont->sc_pool)
			continue;

		d_list_for_each_entry(dbca, &dbpa->dbpa_cont_list, dbca_pool_link) {
			if (dbca->dbca_cont == cont) {
				rc = start_dtx_reindex_ult(cont);
				if (rc != 0)
					return rc;

				dbca->dbca_flush_pending = 0;
				d_list_del(&dbca->dbca_sys_link);
				d_list_add_tail(&dbca->dbca_sys_link,
						&dmi->dmi_dtx_batched_cont_open_list);
				return 0;
			}
		}
	}

	D_ASSERTF(0, "The container "DF_UUID" does not register before open\n",
		  DP_UUID(cont->sc_uuid));

	return -DER_MISC;
}

void
dtx_cont_close(struct ds_cont_child *cont, bool force)
{
	struct dss_module_info		*dmi = dss_get_module_info();
	struct dtx_batched_pool_args	*dbpa;
	struct dtx_batched_cont_args	*dbca;

	D_ASSERT(cont != NULL);
	D_ASSERT(!dtx_cont_opened(cont));

	d_list_for_each_entry(dbpa, &dmi->dmi_dtx_batched_pool_list, dbpa_sys_link) {
		if (dbpa->dbpa_pool != cont->sc_pool)
			continue;

		d_list_for_each_entry(dbca, &dbpa->dbpa_cont_list, dbca_pool_link) {
			if (dbca->dbca_cont == cont) {
				dtx_get_dbca(dbca);
				stop_dtx_reindex_ult(cont, force);

				/* To handle potentially re-open by race. */
				if (unlikely(dtx_cont_opened(cont))) {
					dtx_put_dbca(dbca);
					return;
				}

				d_list_del(&dbca->dbca_sys_link);
				d_list_add_tail(&dbca->dbca_sys_link,
						&dmi->dmi_dtx_batched_cont_close_list);

				dtx_flush_on_close(dmi, dbca);

				/* If nobody reopen the container during dtx_flush_on_close,
				 * then reset DTX table in VOS to release related resources.
				 *
				 * For non-force case, do not reset DTX table if DTX resync
				 * is in-progress to avoid redoing DTX re-index. We will do
				 * that after DTX resync done globally.
				 */
				if (likely(!dtx_cont_opened(cont) && cont->sc_dtx_delay_reset == 0))
					vos_dtx_cache_reset(cont->sc_hdl, false);

				dtx_put_dbca(dbca);
				return;
			}
		}
	}
}

int
dtx_handle_resend(daos_handle_t coh,  struct dtx_id *dti,
		  daos_epoch_t *epoch, uint32_t *pm_ver)
{
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

	rc = vos_dtx_check(coh, dti, epoch, pm_ver, NULL, false);
	switch (rc) {
	case DTX_ST_INITED:
		return -DER_INPROGRESS;
	case DTX_ST_PREPARED:
		return 0;
	case DTX_ST_COMMITTED:
	case DTX_ST_COMMITTABLE:
		return -DER_ALREADY;
	case DTX_ST_CORRUPTED:
		return -DER_DATA_LOSS;
	case -DER_NONEXIST: {
		struct dtx_stat		stat = { 0 };

		/* dtx_id::dti_hlc is client side time stamp. If it is older than the time
		 * of the most new DTX entry that has been aggregated, then it may has been
		 * removed by DTX aggregation. Under such case, return -DER_EP_OLD.
		 */
		vos_dtx_stat(coh, &stat, DSF_SKIP_BAD);
		if (dti->dti_hlc <= stat.dtx_newest_aggregated ||
		    DAOS_FAIL_CHECK(DAOS_DTX_LONG_TIME_RESEND)) {
			D_ERROR("Not sure about whether the old RPC "
				DF_DTI" is resent or not: %lu/%lu\n",
				DP_DTI(dti), dti->dti_hlc, stat.dtx_newest_aggregated);
			rc = -DER_EP_OLD;
		}
		return rc;
	}
	default:
		return rc >= 0 ? -DER_INVAL : rc;
	}
}

struct dtx_chore {
	struct dss_chore		 chore;
	dtx_sub_func_t			 func;
	void				*func_arg;
	struct dtx_leader_handle	*dlh;

	/* Chore-internal state variables */
	uint32_t			 i;
	uint32_t			 j;
	uint32_t			 k;
};

static void
dtx_comp_cb(void **arg)
{
	struct dtx_leader_handle	*dlh = arg[0];
	struct dtx_sub_status		*sub;
	uint32_t			 i;
	uint32_t			 j;

	if (!dlh->dlh_need_agg) {
		for (i = dlh->dlh_forward_idx, j = 0; j < dlh->dlh_forward_cnt; i++, j++) {
			sub = &dlh->dlh_subs[i];

			if (sub->dss_tgt.st_rank == DAOS_TGT_IGNORE || sub->dss_comp == 0 ||
			    sub->dss_result == 0 || sub->dss_result == -DER_ALREADY ||
			    sub->dss_result == dlh->dlh_allow_failure)
				continue;

			if (dlh->dlh_rmt_ver < sub->dss_version)
				dlh->dlh_rmt_ver = sub->dss_version;

			/* Ignore DER_INPROGRESS and DER_AGAIN if there is other failure. */
			if (dlh->dlh_result == 0 || dlh->dlh_result == -DER_INPROGRESS ||
			    dlh->dlh_result == -DER_AGAIN)
				dlh->dlh_result = sub->dss_result;
		}
	}
}

static void
dtx_sub_comp_cb(struct dtx_leader_handle *dlh, int idx, int rc)
{
	struct dtx_sub_status	*sub = &dlh->dlh_subs[idx];
	struct daos_shard_tgt	*tgt = &sub->dss_tgt;

	if ((dlh->dlh_normal_sub_done == 0 && !(tgt->st_flags & DTF_DELAY_FORWARD)) ||
	    (dlh->dlh_normal_sub_done == 1 && tgt->st_flags & DTF_DELAY_FORWARD)) {
		D_ASSERTF(sub->dss_comp == 0,
			  "Repeat sub completion for idx %d (%d:%d), flags %x: %d\n",
			  idx, tgt->st_rank, tgt->st_tgt_idx, tgt->st_flags, rc);
		sub->dss_comp = 1;
		sub->dss_result = rc;

		DL_CDEBUG(rc == -DER_NOMEM, DLOG_ERR, DB_TRACE, rc,
			  "execute from idx %d (%d:%d), flags %x", idx, tgt->st_rank,
			  tgt->st_tgt_idx, tgt->st_flags);
	}

	rc = ABT_future_set(dlh->dlh_future, dlh);
	D_ASSERTF(rc == ABT_SUCCESS,
		  "ABT_future_set failed for idx %d (%d:%d), flags %x: %d\n",
		  idx, tgt->st_rank, tgt->st_tgt_idx, tgt->st_flags, rc);
}

static enum dss_chore_status
dtx_leader_exec_ops_chore(struct dss_chore *chore, bool is_reentrance)
{
	struct dtx_chore		*dtx_chore = container_of(chore, struct dtx_chore, chore);
	struct dtx_leader_handle	*dlh = dtx_chore->dlh;
	struct dtx_sub_status		*sub;
	struct daos_shard_tgt		*tgt;
	int				 rc = 0;

	/*
	 * If this is the first entrance, initialize the chore-internal state
	 * variables.
	 */
	if (is_reentrance) {
		D_DEBUG(DB_TRACE, "%p: resume: i=%u j=%u k=%u forward_cnt=%u\n", chore,
			dtx_chore->i, dtx_chore->j, dtx_chore->k, dlh->dlh_forward_cnt);
		dtx_chore->i++;
		dtx_chore->j++;
	} else {
		D_DEBUG(DB_TRACE, "%p: initialize: forward_idx=%u forward_cnt=%u\n", chore,
			dlh->dlh_forward_idx, dlh->dlh_forward_cnt);
		dtx_chore->i = dlh->dlh_forward_idx;
		dtx_chore->j = 0;
		dtx_chore->k = 0;
	}

	for (; dtx_chore->j < dlh->dlh_forward_cnt; dtx_chore->i++, dtx_chore->j++) {
		sub = &dlh->dlh_subs[dtx_chore->i];
		tgt = &sub->dss_tgt;

		if (dlh->dlh_normal_sub_done == 0) {
			sub->dss_result = 0;
			sub->dss_comp = 0;

			if (unlikely(tgt->st_flags & DTF_DELAY_FORWARD)) {
				dtx_sub_comp_cb(dlh, dtx_chore->i, 0);
				continue;
			}
		} else {
			if (!(tgt->st_flags & DTF_DELAY_FORWARD))
				continue;

			sub->dss_result = 0;
			sub->dss_comp = 0;
		}

		if (tgt->st_rank == DAOS_TGT_IGNORE ||
		    (dtx_chore->i == daos_fail_value_get() &&
		     DAOS_FAIL_CHECK(DAOS_DTX_SKIP_PREPARE))) {
			if (dlh->dlh_normal_sub_done == 0 || tgt->st_flags & DTF_DELAY_FORWARD)
				dtx_sub_comp_cb(dlh, dtx_chore->i, 0);
			continue;
		}

		rc = dtx_chore->func(dlh, dtx_chore->func_arg, dtx_chore->i, dtx_sub_comp_cb);
		if (rc != 0) {
			if (sub->dss_comp == 0)
				dtx_sub_comp_cb(dlh, dtx_chore->i, rc);
			break;
		}

		/* Yield to avoid holding CPU for too long time. */
		if (++(dtx_chore->k) % DTX_RPC_YIELD_THD == 0)
			return DSS_CHORE_YIELD;
	}

	if (rc != 0) {
		for (dtx_chore->i++, dtx_chore->j++; dtx_chore->j < dlh->dlh_forward_cnt;
		     dtx_chore->i++, dtx_chore->j++) {
			sub = &dlh->dlh_subs[dtx_chore->i];
			tgt = &sub->dss_tgt;

			if (dlh->dlh_normal_sub_done == 0 || tgt->st_flags & DTF_DELAY_FORWARD) {
				sub->dss_result = 0;
				sub->dss_comp = 0;
				dtx_sub_comp_cb(dlh, dtx_chore->i, 0);
			}
		}
	}

	/* To indicate that the IO forward ULT itself has done. */
	rc = ABT_future_set(dlh->dlh_future, dlh);
	D_ASSERTF(rc == ABT_SUCCESS, "ABT_future_set failed [%u, %u), for delay %s: %d\n",
		  dlh->dlh_forward_idx, dlh->dlh_forward_idx + dlh->dlh_forward_cnt,
		  dlh->dlh_normal_sub_done == 1 ? "yes" : "no", rc);

	return DSS_CHORE_DONE;
}

/**
 * Execute the operations on all targets.
 */
int
dtx_leader_exec_ops(struct dtx_leader_handle *dlh, dtx_sub_func_t func,
		    dtx_agg_cb_t agg_cb, int allow_failure, void *func_arg)
{
	struct dtx_chore	dtx_chore;
	int			sub_cnt = dlh->dlh_normal_sub_cnt + dlh->dlh_delay_sub_cnt;
	int			rc = 0;
	int			local_rc = 0;
	int			remote_rc = 0;

	dtx_chore.func = func;
	dtx_chore.func_arg = func_arg;
	dtx_chore.dlh = dlh;

	dtx_chore.chore.cho_func     = dtx_leader_exec_ops_chore;
	dtx_chore.chore.cho_priority = 0;

	dlh->dlh_result = 0;
	dlh->dlh_allow_failure = allow_failure;
	dlh->dlh_normal_sub_done = 0;
	dlh->dlh_drop_cond = 0;
	dlh->dlh_forward_idx = 0;
	dlh->dlh_need_agg = 0;
	dlh->dlh_agg_done = 0;

	if (sub_cnt > DTX_REG_RPC_STEP_LENGTH) {
		dlh->dlh_forward_cnt = DTX_REG_RPC_STEP_LENGTH;
	} else {
		dlh->dlh_forward_cnt = sub_cnt;
		if (likely(dlh->dlh_delay_sub_cnt == 0) && agg_cb != NULL)
			dlh->dlh_need_agg = 1;
	}

	if (dlh->dlh_normal_sub_cnt == 0)
		goto exec;

again1:
	D_ASSERT(dlh->dlh_future == ABT_FUTURE_NULL);

	/*
	 * Create the future with dlh->dlh_forward_cnt + 1, the additional one is used by the IO
	 * forward ULT itself to prevent the DTX handle being freed before the IO forward ULT exit.
	 */
	rc = ABT_future_create(dlh->dlh_forward_cnt + 1, dtx_comp_cb, &dlh->dlh_future);
	if (rc != ABT_SUCCESS) {
		D_ERROR("ABT_future_create failed [%u, %u] (1): "DF_RC"\n",
			dlh->dlh_forward_idx, dlh->dlh_forward_cnt, DP_RC(rc));
		D_GOTO(out, rc = dss_abterr2der(rc));
	}

again2:
	dtx_chore.chore.cho_credits = dlh->dlh_forward_cnt;
	dtx_chore.chore.cho_hint    = NULL;
	rc                          = dss_chore_register(&dtx_chore.chore);
	if (rc != 0) {
		if (rc != -DER_AGAIN) {
			DL_ERROR(rc, "chore create failed [%u, %u] (2)", dlh->dlh_forward_idx,
				 dlh->dlh_forward_cnt);
			ABT_future_free(&dlh->dlh_future);
			goto out;
		}

		d_tm_inc_counter(dtx_tls_get()->dt_chore_retry, 1);

		/*
		 * To avoid the whole task is split too many pieces. If there are very few
		 * credits, we may prefer to wait instead of shrink the credits quirement.
		 */
		if (dtx_chore.chore.cho_credits > dlh->dlh_normal_sub_cnt / 8) {
			D_DEBUG(DB_TRACE, "Retry IO forward with credits from %d to %d\n",
				dlh->dlh_forward_cnt, dtx_chore.chore.cho_credits);
			ABT_future_free(&dlh->dlh_future);
			dlh->dlh_forward_cnt = dtx_chore.chore.cho_credits;
			goto again1;
		}

		/*
		 * If more than half sub-requests have been processed, let's handle the left
		 * part ASAP to avoid the whole task timeout. Otherwise once timeout, it may
		 * cause more overhead for rollback.
		 */
		if (dlh->dlh_forward_idx > sub_cnt / 2) {
			dtx_chore.chore.cho_priority = 1;

			if (dlh->dlh_forward_cnt > DTX_PRI_RPC_STEP_LENGTH) {
				D_DEBUG(DB_TRACE, "Retry (prio) IO forward with credits %d => %d\n",
					dlh->dlh_forward_cnt, DTX_PRI_RPC_STEP_LENGTH);
				ABT_future_free(&dlh->dlh_future);
				dlh->dlh_forward_cnt = DTX_PRI_RPC_STEP_LENGTH;
				goto again1;
			}

			D_DEBUG(DB_TRACE, "Retry (prio) IO forward with credits %d\n",
				dlh->dlh_forward_cnt);
			goto again2;
		}

		D_DEBUG(DB_TRACE, "Not enough credits (%d vs %d) for IO forward, wait and retry\n",
			dlh->dlh_forward_cnt, dtx_chore.chore.cho_credits);
		ABT_thread_yield();
		goto again2;
	}

exec:
	/* Execute the local operation only for once. */
	if (dlh->dlh_forward_idx == 0)
		local_rc = func(dlh, func_arg, -1, NULL);

	/* Even the local request failure, we still need to wait for remote sub request. */
	if (dlh->dlh_normal_sub_cnt > 0) {
		remote_rc = dtx_leader_wait(dlh);
		dss_chore_deregister(&dtx_chore.chore);
	}

	if (local_rc != 0 && local_rc != allow_failure)
		D_GOTO(out, rc = local_rc);

	if (remote_rc != 0 && remote_rc != allow_failure)
		D_GOTO(out, rc = remote_rc);

	sub_cnt -= dlh->dlh_forward_cnt;
	if (sub_cnt > 0) {
		dlh->dlh_forward_idx += dlh->dlh_forward_cnt;
		if (sub_cnt <= DTX_REG_RPC_STEP_LENGTH) {
			dlh->dlh_forward_cnt = sub_cnt;
			if (likely(dlh->dlh_delay_sub_cnt == 0) && agg_cb != NULL)
				dlh->dlh_need_agg = 1;
		}

		D_DEBUG(DB_IO, "More dispatch sub-requests for "DF_DTI", normal %u, "
			"delay %u, idx %u, cnt %d, allow_failure %d\n",
			DP_DTI(&dlh->dlh_handle.dth_xid), dlh->dlh_normal_sub_cnt,
			dlh->dlh_delay_sub_cnt, dlh->dlh_forward_idx,
			dlh->dlh_forward_cnt, allow_failure);

		dtx_chore.chore.cho_priority = 0;
		goto again1;
	}

	dlh->dlh_normal_sub_done = 1;
	dlh->dlh_drop_cond = 1;

	if (agg_cb != NULL) {
		remote_rc = agg_cb(dlh, func_arg);
		dlh->dlh_agg_done = 1;
		if (remote_rc != 0) {
			if (remote_rc != allow_failure)
				D_GOTO(out, rc = remote_rc);

			dlh->dlh_drop_cond = 0;
		}
	}

	if (likely(dlh->dlh_delay_sub_cnt == 0))
		goto out;

	/* Need more aggregation for delayed sub-requests. */
	dlh->dlh_agg_done = 0;
	if (agg_cb != NULL)
		dlh->dlh_need_agg = 1;

	D_ASSERT(dlh->dlh_future == ABT_FUTURE_NULL);

	/*
	 * Delay forward is rare case, the count of targets with delay forward
	 * will be very limited. So le's handle them via another one cycle dispatch.
	 */
	rc = ABT_future_create(dlh->dlh_delay_sub_cnt + 1, dtx_comp_cb, &dlh->dlh_future);
	if (rc != ABT_SUCCESS) {
		D_ERROR("ABT_future_create failed (3): "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc = dss_abterr2der(rc));
	}

	dlh->dlh_forward_idx = 0;
	/* The ones without DELAY flag will be skipped when scan the targets array. */
	dlh->dlh_forward_cnt = dlh->dlh_normal_sub_cnt + dlh->dlh_delay_sub_cnt;

	/*
	 * Since non-delay sub-requests have already been processed, let's use high priority
	 * to apply chore credits, then the left delayed sub-requests can be handled quickly
	 * to reduce the possibility of the whole IO timeout.
	 */
	if (unlikely(dlh->dlh_delay_sub_cnt > DTX_PRI_RPC_STEP_LENGTH))
		D_WARN("Too many delayed sub-requests %u\n", dlh->dlh_delay_sub_cnt);

	dtx_chore.chore.cho_priority = 1;
	dtx_chore.chore.cho_credits  = dlh->dlh_delay_sub_cnt;
	dtx_chore.chore.cho_hint     = NULL;
	rc                           = dss_chore_register(&dtx_chore.chore);
	if (rc != 0) {
		DL_ERROR(rc, "chore create failed (4)");
		ABT_future_free(&dlh->dlh_future);
		goto out;
	}

	remote_rc = dtx_leader_wait(dlh);
	dss_chore_deregister(&dtx_chore.chore);
	if (remote_rc != 0 && remote_rc != allow_failure)
		rc = remote_rc;

	D_DEBUG(DB_IO, "Delay dispatched sub-requests for "DF_DTI", normal %u, delay %u, cnt %d, "
		"allow_failure %d, local_rc %d, remote_rc %d\n", DP_DTI(&dlh->dlh_handle.dth_xid),
		dlh->dlh_normal_sub_cnt, dlh->dlh_delay_sub_cnt, dlh->dlh_forward_cnt,
		allow_failure, local_rc, remote_rc);

out:
	/* The agg_cb may contain cleanup, let's do it even if hit failure at some former step. */
	if (agg_cb != NULL && !dlh->dlh_agg_done) {
		remote_rc = agg_cb(dlh, func_arg);
		dlh->dlh_agg_done = 1;
		if (remote_rc != 0 && remote_rc != allow_failure &&
		    (rc == 0 || rc == allow_failure))
			rc = remote_rc;
	}

	if (rc == 0 && local_rc == allow_failure &&
	    (dlh->dlh_normal_sub_cnt + dlh->dlh_delay_sub_cnt == 0 || remote_rc == allow_failure))
		rc = allow_failure;

	return rc;
}

int
dtx_obj_sync(struct ds_cont_child *cont, daos_unit_oid_t *oid,
	     daos_epoch_t epoch)
{
	struct dss_xstream	*dx = dss_current_xstream();
	int			 cnt;
	int			 rc = 0;

	while (!dss_xstream_exiting(dx) && (dtx_cont_opened(cont) || oid == NULL)) {
		struct dtx_entry	**dtes = NULL;
		struct dtx_cos_key	 *dcks = NULL;
		struct dtx_coll_entry	 *dce = NULL;

		cnt = dtx_fetch_committable(cont, DTX_THRESHOLD_COUNT, oid,
					    epoch, true, &dtes, &dcks, &dce);
		if (cnt <= 0) {
			rc = cnt;
			if (rc < 0)
				D_ERROR("Failed to fetch dtx: "DF_RC"\n",
					DP_RC(rc));
			break;
		}

		if (dce != NULL) {
			D_ASSERT(cnt == 1);

			rc = dtx_coll_commit(cont, dce, dcks, true);
		} else {
			rc = dtx_commit(cont, dtes, dcks, cnt, true);
		}
		dtx_free_committable(dtes, dcks, dce, cnt);
		if (rc < 0) {
			D_ERROR("Fail to commit dtx: "DF_RC"\n", DP_RC(rc));
			break;
		}
	}

	if (rc == 0 && oid != NULL && dtx_cont_opened(cont))
		rc = vos_dtx_mark_sync(cont->sc_hdl, *oid, epoch);

	return rc;
}

void
dtx_merge_check_result(int *tgt, int src)
{
	/* As long as one target has committed, then the DTX is committable on all targets. */
	if (*tgt != DTX_ST_COMMITTED && *tgt != DTX_ST_COMMITTABLE) {
		switch (src) {
		case DTX_ST_COMMITTED:
		case DTX_ST_COMMITTABLE:
			*tgt = src;
			break;
		case -DER_EXCLUDED:
			/*
			 * If non-leader is excluded, handle it as 'prepared'. If other
			 * non-leaders are also 'prepared' then related DTX maybe still
			 * committable or 'corrupted'. The subsequent DTX resync logic
			 * will handle related things, see dtx_verify_groups().
			 *
			 * Fall through.
			 */
		case DTX_ST_PREPARED:
			if (*tgt == 0 || *tgt == DTX_ST_CORRUPTED)
				*tgt = src;
			break;
		case DTX_ST_CORRUPTED:
			if (*tgt == 0)
				*tgt = src;
			break;
		default:
			if (src >= 0) {
				if (*tgt != -DER_NONEXIST)
					*tgt = -DER_IO;
			} else {
				if (src == -DER_NONEXIST || *tgt >= 0 ||
				    (*tgt != -DER_IO && *tgt != -DER_NONEXIST))
					*tgt = src;
			}
			break;
		}
	}
}

int
dtx_leader_get(struct ds_pool *pool, struct dtx_memberships *mbs, daos_unit_oid_t *oid,
	       uint32_t version, struct pool_target **p_tgt)
{
	struct pl_map		*map = NULL;
	struct pl_obj_layout	*layout = NULL;
	struct dtx_coll_target	*dct;
	struct daos_obj_md	 md = { 0 };
	int			 rc = 0;
	int			 i;

	D_ASSERT(mbs != NULL);

	/* The first UPIN (and join before DTX) target is the (new) leader of the DTX. */
	for (i = 0; i < mbs->dm_tgt_cnt; i++) {
		rc = ds_pool_target_status_check(pool, mbs->dm_tgts[i].ddt_id,
						 (uint8_t)PO_COMP_ST_UPIN, p_tgt);
		if (rc < 0)
			D_GOTO(out, rc);

		/* The target that (re-)joined the system after DTX cannot be the leader. */
		if (rc == 1 && (*p_tgt)->ta_comp.co_in_ver <= version)
			D_GOTO(out, rc = 0);
	}

	if (!(mbs->dm_flags & DMF_COLL_TARGET))
		D_GOTO(out, rc = -DER_NONEXIST);

	map = pl_map_find(pool->sp_uuid, oid->id_pub);
	if (map == NULL) {
		D_ERROR("Failed to find valid placement map in pool "DF_UUID"\n",
			DP_UUID(pool->sp_uuid));
		D_GOTO(out, rc = -DER_INVAL);
	}

	dct = (struct dtx_coll_target *)(mbs->dm_tgts + mbs->dm_tgt_cnt);
	md.omd_id = oid->id_pub;
	md.omd_ver = pool->sp_map_version;
	md.omd_fdom_lvl = dct->dct_fdom_lvl;
	md.omd_pda = dct->dct_pda;
	md.omd_pdom_lvl = dct->dct_pdom_lvl;

	rc = pl_obj_place(map, oid->id_layout_ver, &md, DAOS_OO_RW, NULL, &layout);
	if (rc != 0) {
		D_ERROR("Failed to load object layout for "DF_OID" in pool "DF_UUID"\n",
			DP_OID(oid->id_pub), DP_UUID(pool->sp_uuid));
		goto out;
	}

	for (i = 0; i < layout->ol_nr; i++) {
		if (layout->ol_shards[i].po_target == -1 || layout->ol_shards[i].po_shard == -1)
			continue;

		rc = pool_map_find_target(map->pl_poolmap, layout->ol_shards[i].po_target, p_tgt);
		D_ASSERT(rc == 1);

		/* The target that (re-)joined the system after DTX cannot be the leader. */
		if ((*p_tgt)->ta_comp.co_in_ver <= version)
			D_GOTO(out, rc = 0);
	}

	rc = -DER_NONEXIST;

out:
	if (layout != NULL)
		pl_obj_layout_free(layout);

	if (map != NULL)
		pl_map_decref(map);

	return rc;
}
