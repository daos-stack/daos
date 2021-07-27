/**
 * (C) Copyright 2019-2021 Intel Corporation.
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

uint64_t dtx_agg_gen;
struct dtx_batched_cont_args;

struct dtx_batched_pool_args {
	/* Link to dss_module_info::dmi_dtx_batched_pool_list. */
	d_list_t			 dbpa_sys_link;
	/* The list of containers belong to the pool. */
	d_list_t			 dbpa_cont_list;
	struct ds_pool_child		*dbpa_pool;
	/* The container that needs to do DTX aggregation. */
	struct dtx_batched_cont_args	*dbpa_victim;
	struct dtx_stat			 dbpa_stat;
	uint32_t			 dbpa_aggregating:1;
};

struct dtx_batched_cont_args {
	/* Link to dss_module_info::dmi_dtx_batched_cont_list. */
	d_list_t			 dbca_sys_link;
	/* Link to dtx_batched_pool_args::dbpa_cont_list. */
	d_list_t			 dbca_pool_link;
	uint64_t			 dbca_gen;
	int				 dbca_refs;
	uint32_t			 dbca_deregister:1;
	struct sched_request		*dbca_cleanup_req;
	struct sched_request		*dbca_commit_req;
	struct sched_request		*dbca_agg_req;
	struct ds_cont_child		*dbca_cont;
	struct dtx_batched_pool_args	*dbca_pool;
};

struct dtx_cleanup_stale_cb_args {
	d_list_t		dcsca_list;
	int			dcsca_count;
};

static inline void
dtx_free_committable(struct dtx_entry **dtes, struct dtx_cos_key *dcks,
		     int count)
{
	int	i;

	for (i = 0; i < count; i++)
		dtx_entry_put(dtes[i]);
	D_FREE(dtes);
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

	/* Nobody re-opened it during waiting dtx_flush_on_deregister(). */
	if (cont->sc_closing) {
		if (daos_handle_is_valid(cont->sc_dtx_cos_hdl)) {
			dbtree_destroy(cont->sc_dtx_cos_hdl, NULL);
			cont->sc_dtx_cos_hdl = DAOS_HDL_INVAL;
		}

		D_ASSERT(cont->sc_dtx_committable_count == 0);
		D_ASSERT(d_list_empty(&cont->sc_dtx_cos_list));
	}

	/* Even if the container is reopened during current deregister, the
	 * reopen will use new dbca, so current dbca needs to be cleanup.
	 */

	D_ASSERT(d_list_empty(&dbca->dbca_sys_link));

	if (dbca->dbca_cleanup_req != NULL)
		sched_req_wait(dbca->dbca_cleanup_req, true);

	if (dbca->dbca_commit_req != NULL)
		sched_req_wait(dbca->dbca_commit_req, true);

	if (dbca->dbca_agg_req != NULL)
		sched_req_wait(dbca->dbca_agg_req, true);

	/* dtx_batched_commit() ULT may hold the last reference on the dbca. */
	while (dbca->dbca_refs > 0) {
		D_DEBUG(DB_TRACE, "Sleep 10 mseconds for batched commit ULT\n");
		dss_sleep(10);
	}

	D_ASSERT(dbca->dbca_cleanup_req == NULL);
	D_ASSERT(dbca->dbca_commit_req == NULL);
	D_ASSERT(dbca->dbca_agg_req == NULL);

	if (d_list_empty(&dbpa->dbpa_cont_list)) {
		d_list_del(&dbpa->dbpa_sys_link);
		D_FREE(dbpa);
	}

	D_FREE_PTR(dbca);
	ds_cont_child_put(cont);
}

static void
dtx_init_sched_req(struct ds_cont_child *cont, struct sched_request **sched_req,
		   ABT_thread ult)
{
	uuid_t			anonym_uuid;
	struct sched_req_attr	attr;

	D_ASSERT(sched_req != NULL);
	D_ASSERT(*sched_req == NULL);

	if (cont == NULL || !cont->sc_closing) {
		uuid_clear(anonym_uuid);
		sched_req_attr_init(&attr, SCHED_REQ_ANONYM, &anonym_uuid);
		*sched_req = sched_req_get(&attr, ult);
	}
}

static void
dtx_stat(struct ds_cont_child *cont, struct dtx_stat *stat)
{
	vos_dtx_stat(cont->sc_hdl, stat, DSF_SKIP_BAD);

	stat->dtx_committable_count = cont->sc_dtx_committable_count;
	stat->dtx_oldest_committable_time = dtx_cos_oldest(cont);
}

static int
dtx_cleanup_stale_iter_cb(uuid_t co_uuid, vos_iter_entry_t *ent, void *args)
{
	struct dtx_cleanup_stale_cb_args	*dcsca = args;
	struct dtx_memberships			*mbs;
	struct dtx_share_peer			*dsp;

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

	/* Stop the iteration if current DTX is not too old. */
	if (dtx_hlc_age2sec(ent->ie_dtx_start_time) <=
	    DTX_CLEANUP_THD_AGE_LO)
		return 1;

	D_ALLOC(dsp, sizeof(*dsp) + ent->ie_dtx_mbs_dsize);
	if (dsp == NULL)
		return -DER_NOMEM;

	dsp->dsp_xid = ent->ie_dtx_xid;
	dsp->dsp_oid = ent->ie_dtx_oid;
	dsp->dsp_epoch = ent->ie_epoch;

	mbs = &dsp->dsp_mbs;
	mbs->dm_tgt_cnt = ent->ie_dtx_tgt_cnt;
	mbs->dm_grp_cnt = ent->ie_dtx_grp_cnt;
	mbs->dm_data_size = ent->ie_dtx_mbs_dsize;
	mbs->dm_flags = ent->ie_dtx_mbs_flags;
	mbs->dm_dte_flags = ent->ie_dtx_flags;
	memcpy(mbs->dm_data, ent->ie_dtx_mbs, ent->ie_dtx_mbs_dsize);

	d_list_add_tail(&dsp->dsp_link, &dcsca->dcsca_list);
	dcsca->dcsca_count++;

	return 0;
}

static void
dtx_cleanup_stale(void *arg)
{
	struct dtx_batched_cont_args		*dbca = arg;
	struct ds_cont_child			*cont = dbca->dbca_cont;
	struct dtx_share_peer			*dsp;
	struct dtx_cleanup_stale_cb_args	 dcsca;
	int					 count;
	int					 rc;

	if (dbca->dbca_cleanup_req == NULL)
		goto out;

	D_INIT_LIST_HEAD(&dcsca.dcsca_list);
	dcsca.dcsca_count = 0;
	rc = ds_cont_iter(cont->sc_pool->spc_hdl, cont->sc_uuid,
			  dtx_cleanup_stale_iter_cb, &dcsca, VOS_ITER_DTX,
			  VOS_IT_CLEANUP_DTX);
	if (rc < 0)
		D_WARN("Failed to scan stale DTX entry for "
		       DF_UUID": "DF_RC"\n", DP_UUID(cont->sc_uuid), DP_RC(rc));

	while (!dss_ult_exiting(dbca->dbca_cleanup_req) &&
	       !d_list_empty(&dcsca.dcsca_list)) {
		if (dcsca.dcsca_count > DTX_REFRESH_MAX) {
			count = DTX_REFRESH_MAX;
			dcsca.dcsca_count -= DTX_REFRESH_MAX;
		} else {
			D_ASSERT(dcsca.dcsca_count > 0);

			count = dcsca.dcsca_count;
			dcsca.dcsca_count = 0;
		}

		/* Use false as the "failout" parameter that should guarantee
		 * that all the DTX entries in the check list will be handled
		 * even if some former ones hit failure.
		 */
		rc = dtx_refresh_internal(cont, &count, &dcsca.dcsca_list,
					  NULL, NULL, NULL, false);
		D_ASSERTF(count == 0, "%d entries are not handled: "DF_RC"\n",
			  count, DP_RC(rc));
	}

	while ((dsp = d_list_pop_entry(&dcsca.dcsca_list,
				       struct dtx_share_peer,
				       dsp_link)) != NULL)
		D_FREE(dsp);

	sched_req_put(dbca->dbca_cleanup_req);
	dbca->dbca_cleanup_req = NULL;

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
		    (stat.dtx_cont_cmt_count <= DTX_AGG_THD_CNT_LO &&
		     dtx_hlc_age2sec(stat.dtx_first_cmt_blob_time_lo) <=
		     DTX_AGG_THD_AGE_LO))
			break;
	}

	sched_req_put(dbca->dbca_agg_req);
	dbca->dbca_agg_req = NULL;

out:
	dtx_put_dbca(dbca);
}

static void
dtx_aggregation_pool(struct dtx_batched_pool_args *dbpa)
{
	ABT_thread			 child;
	struct dtx_batched_cont_args	*dbca;
	struct ds_cont_child		*cont;
	int				 rc;

	while (1) {
		struct dtx_stat		 stat = { 0 };

		if (d_list_empty(&dbpa->dbpa_cont_list))
			return;

		dbca = d_list_entry(dbpa->dbpa_cont_list.next,
				    struct dtx_batched_cont_args,
				    dbca_pool_link);

		/* Finish this cycle scan. */
		if (dbca->dbca_gen == dtx_agg_gen)
			break;

		dbca->dbca_gen = dtx_agg_gen;
		d_list_move_tail(&dbca->dbca_pool_link, &dbpa->dbpa_cont_list);

		if (dbca->dbca_deregister)
			continue;

		cont = dbca->dbca_cont;
		if (cont->sc_closing)
			continue;

		if (dbca->dbca_agg_req != NULL) {
			dbpa->dbpa_aggregating = 1;
			continue;
		}

		dtx_stat(cont, &stat);
		if (stat.dtx_cont_cmt_count == 0 ||
		    stat.dtx_first_cmt_blob_time_lo == 0)
			continue;

		if (stat.dtx_cont_cmt_count >= DTX_AGG_THD_CNT_UP ||
		    ((stat.dtx_cont_cmt_count > DTX_AGG_THD_CNT_LO ||
		      stat.dtx_pool_cmt_count >= DTX_AGG_THD_CNT_UP) &&
		     (dtx_hlc_age2sec(stat.dtx_first_cmt_blob_time_lo) >=
		      DTX_AGG_THD_AGE_UP))) {
			dtx_get_dbca(dbca);
			rc = dss_ult_create(dtx_aggregate, dbca,
					    DSS_XS_SELF, 0, 0, &child);
			if (rc != 0) {
				D_WARN("Fail to start DTX agg ULT (1) for "
				       DF_UUID": "DF_RC"\n",
				       DP_UUID(cont->sc_uuid), DP_RC(rc));
				dtx_put_dbca(dbca);
				continue;
			}

			dtx_init_sched_req(cont, &dbca->dbca_agg_req, child);
			if (dbca->dbca_agg_req == NULL) {
				D_WARN("Fail to get agg sched req (1) for "
				       DF_UUID"\n", DP_UUID(cont->sc_uuid));
				ABT_thread_join(child);
				continue;
			}

			dbpa->dbpa_aggregating = 1;
			continue;
		}

		if (dbpa->dbpa_stat.dtx_first_cmt_blob_time_lo == 0 ||
		    dbpa->dbpa_stat.dtx_first_cmt_blob_time_lo >
		    stat.dtx_first_cmt_blob_time_lo ||
		    (dbpa->dbpa_stat.dtx_first_cmt_blob_time_lo ==
		     stat.dtx_first_cmt_blob_time_lo &&
		     dbpa->dbpa_stat.dtx_first_cmt_blob_time_up >
		     stat.dtx_first_cmt_blob_time_up) ||
		    (dbpa->dbpa_stat.dtx_first_cmt_blob_time_lo ==
		     stat.dtx_first_cmt_blob_time_lo &&
		     dbpa->dbpa_stat.dtx_first_cmt_blob_time_up ==
		     stat.dtx_first_cmt_blob_time_up &&
		     dbpa->dbpa_stat.dtx_cont_cmt_count <
		     stat.dtx_cont_cmt_count)) {
			dbpa->dbpa_stat = stat;
			dbpa->dbpa_victim = dbca;
		}
	}

	if (dbpa->dbpa_aggregating || dbpa->dbpa_victim == NULL ||
	    dbpa->dbpa_stat.dtx_pool_cmt_count <= DTX_AGG_THD_CNT_LO ||
	    dbpa->dbpa_stat.dtx_first_cmt_blob_time_lo == 0 ||
	    dtx_hlc_age2sec(dbpa->dbpa_stat.dtx_first_cmt_blob_time_lo) <=
	    DTX_AGG_THD_AGE_LO)
		return;

	/* No single pool exceeds DTX thresholds, but the whole pool does,
	 * we choose the victim container to do the DTX aggregation.
	 */

	dbca = dbpa->dbpa_victim;
	cont = dbca->dbca_cont;
	dtx_get_dbca(dbca);

	rc = dss_ult_create(dtx_aggregate, dbca, DSS_XS_SELF, 0, 0, &child);
	if (rc != 0) {
		D_WARN("Fail to start DTX agg ULT (2) for "DF_UUID": "DF_RC"\n",
		       DP_UUID(cont->sc_uuid), DP_RC(rc));
		dtx_put_dbca(dbca);
	} else {
		dtx_init_sched_req(cont, &dbca->dbca_agg_req, child);
		if (dbca->dbca_agg_req == NULL) {
			D_WARN("Fail to get agg sched req (2) for "DF_UUID"\n",
			       DP_UUID(cont->sc_uuid));
			ABT_thread_join(child);
		} else {
			dbpa->dbpa_aggregating = 1;
		}
	}
}

static void
dtx_aggregation_main(void *arg)
{
	struct dss_module_info		*dmi = dss_get_module_info();
	struct dtx_batched_pool_args	*dbpa;

	if (dmi->dmi_dtx_agg_req == NULL)
		return;

	while (1) {
		int	sleep_time = 50; /* ms */

		if (!d_list_empty(&dmi->dmi_dtx_batched_pool_list)) {
			dbpa = d_list_entry(dmi->dmi_dtx_batched_pool_list.next,
					    struct dtx_batched_pool_args,
					    dbpa_sys_link);
			d_list_move_tail(&dbpa->dbpa_sys_link,
					 &dmi->dmi_dtx_batched_pool_list);

			dtx_agg_gen++;
			dbpa->dbpa_victim = NULL;
			dbpa->dbpa_aggregating = 0;
			dtx_aggregation_pool(dbpa);
			if (dbpa->dbpa_aggregating)
				sleep_time = 0;
		}

		if (dss_xstream_exiting(dmi->dmi_xstream))
			break;

		sched_req_sleep(dmi->dmi_dtx_agg_req, sleep_time);
	}

	sched_req_put(dmi->dmi_dtx_agg_req);
	dmi->dmi_dtx_agg_req = NULL;
}

static void
dtx_batched_commit_one(void *arg)
{
	struct dss_module_info		*dmi = dss_get_module_info();
	struct dtx_batched_cont_args	*dbca = arg;
	struct ds_cont_child		*cont = dbca->dbca_cont;

	if (dbca->dbca_commit_req == NULL)
		goto out;

	while (!dss_ult_exiting(dbca->dbca_commit_req)) {
		struct dtx_entry	**dtes = NULL;
		struct dtx_cos_key	 *dcks = NULL;
		struct dtx_stat		  stat = { 0 };
		int			  cnt;
		int			  rc;

		cnt = dtx_fetch_committable(cont, DTX_THRESHOLD_COUNT, NULL,
					    DAOS_EPOCH_MAX, &dtes, &dcks);
		if (cnt <= 0)
			break;

		rc = dtx_commit(cont, dtes, dcks, cnt);
		dtx_free_committable(dtes, dcks, cnt);
		if (rc != 0)
			break;

		dtx_stat(cont, &stat);

		if (stat.dtx_pool_cmt_count >= DTX_AGG_THD_CNT_UP &&
		    !dbca->dbca_pool->dbpa_aggregating)
			sched_req_wakeup(dmi->dmi_dtx_agg_req);

		if ((stat.dtx_committable_count <= DTX_THRESHOLD_COUNT) &&
		    (stat.dtx_oldest_committable_time == 0 ||
		     dtx_hlc_age2sec(stat.dtx_oldest_committable_time) <
		     DTX_COMMIT_THRESHOLD_AGE))
			break;
	}

	sched_req_put(dbca->dbca_commit_req);
	dbca->dbca_commit_req = NULL;

out:
	dtx_put_dbca(dbca);
}

void
dtx_batched_commit(void *arg)
{
	struct dss_module_info		*dmi = dss_get_module_info();
	struct dtx_batched_cont_args	*dbca;
	struct dtx_batched_cont_args	*tmp;
	ABT_thread			 child;
	int				 rc;

	dtx_init_sched_req(NULL, &dmi->dmi_dtx_cmt_req, ABT_THREAD_NULL);
	if (dmi->dmi_dtx_cmt_req == NULL) {
		D_ERROR("Failed to get DTX batched commit sched request.\n");
		return;
	}

	rc = dss_ult_create(dtx_aggregation_main, NULL,
			    DSS_XS_SELF, 0, 0, &child);
	if (rc != 0) {
		D_ERROR("Fail to start DTX aggregation main ULT: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

	dtx_init_sched_req(NULL, &dmi->dmi_dtx_agg_req, child);
	if (dmi->dmi_dtx_agg_req == NULL) {
		D_ERROR("Failed to get DTX aggregation sched request.\n");
		ABT_thread_join(child);
		goto out;
	}

	dmi->dmi_dtx_batched_started = 1;

	while (1) {
		struct ds_cont_child	*cont;
		struct dtx_stat		 stat = { 0 };
		int			 sleep_time = 10; /* ms */

		if (d_list_empty(&dmi->dmi_dtx_batched_cont_list))
			goto check;

		if (DAOS_FAIL_CHECK(DAOS_DTX_NO_BATCHED_CMT) ||
		    DAOS_FAIL_CHECK(DAOS_DTX_NO_COMMITTABLE))
			goto check;

		dbca = d_list_entry(dmi->dmi_dtx_batched_cont_list.next,
				    struct dtx_batched_cont_args,
				    dbca_sys_link);
		cont = dbca->dbca_cont;
		d_list_move_tail(&dbca->dbca_sys_link,
				 &dmi->dmi_dtx_batched_cont_list);
		dtx_stat(cont, &stat);

		if (!cont->sc_closing &&
		    !dbca->dbca_deregister && dbca->dbca_commit_req == NULL &&
		    ((stat.dtx_committable_count > DTX_THRESHOLD_COUNT) ||
		     (stat.dtx_oldest_committable_time != 0 &&
		      dtx_hlc_age2sec(stat.dtx_oldest_committable_time) >=
		      DTX_COMMIT_THRESHOLD_AGE))) {
			sleep_time = 0;
			dtx_get_dbca(dbca);
			rc = dss_ult_create(dtx_batched_commit_one, dbca,
					    DSS_XS_SELF, 0, 0, &child);
			if (rc != 0) {
				D_WARN("Fail to start DTX ULT (1) for "
				       DF_UUID": "DF_RC"\n",
				       DP_UUID(cont->sc_uuid), DP_RC(rc));
				dtx_put_dbca(dbca);
			} else {
				dtx_init_sched_req(cont, &dbca->dbca_commit_req,
						   child);
				if (dbca->dbca_commit_req == NULL) {
					D_WARN("Fail to get sched req (1) for "
					       DF_UUID"\n",
					       DP_UUID(cont->sc_uuid));
					ABT_thread_join(child);
				}
			}
		}

		if (!cont->sc_closing &&
		    !dbca->dbca_deregister && dbca->dbca_cleanup_req == NULL &&
		    stat.dtx_oldest_active_time != 0 &&
		    dtx_hlc_age2sec(stat.dtx_oldest_active_time) >=
		    DTX_CLEANUP_THD_AGE_UP) {
			sleep_time = 0;
			dtx_get_dbca(dbca);
			rc = dss_ult_create(dtx_cleanup_stale, dbca,
					    DSS_XS_SELF, 0, 0, &child);
			if (rc != 0) {
				D_WARN("Fail to start DTX ULT (3) for "
				       DF_UUID": "DF_RC"\n",
				       DP_UUID(cont->sc_uuid), DP_RC(rc));
				dtx_put_dbca(dbca);
			} else {
				dtx_init_sched_req(cont,
						   &dbca->dbca_cleanup_req,
						   child);
				if (dbca->dbca_cleanup_req == NULL) {
					D_WARN("Fail to get sched req (3) for "
					       DF_UUID"\n",
					       DP_UUID(cont->sc_uuid));
					ABT_thread_join(child);
				}
			}
		}

check:
		if (dss_xstream_exiting(dmi->dmi_xstream))
			break;

		sched_req_sleep(dmi->dmi_dtx_cmt_req, sleep_time);
	}

	if (dmi->dmi_dtx_agg_req != NULL)
		sched_req_wait(dmi->dmi_dtx_agg_req, true);

out:
	sched_req_put(dmi->dmi_dtx_cmt_req);
	dmi->dmi_dtx_cmt_req = NULL;

	d_list_for_each_entry_safe(dbca, tmp, &dmi->dmi_dtx_batched_cont_list,
				   dbca_sys_link)
		dbca->dbca_cont->sc_dtx_cos_shutdown = 1;

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
		D_FREE(dsp);

	while ((dsp = d_list_pop_entry(&dth->dth_share_abt_list,
				       struct dtx_share_peer,
				       dsp_link)) != NULL)
		D_FREE(dsp);

	while ((dsp = d_list_pop_entry(&dth->dth_share_act_list,
				       struct dtx_share_peer,
				       dsp_link)) != NULL)
		D_FREE(dsp);

	while ((dsp = d_list_pop_entry(&dth->dth_share_tbd_list,
				       struct dtx_share_peer,
				       dsp_link)) != NULL)
		D_FREE(dsp);

	dth->dth_share_tbd_count = 0;
}

int
dtx_handle_reinit(struct dtx_handle *dth)
{
	D_ASSERT(dth->dth_ent == NULL);
	D_ASSERT(dth->dth_pinned == 0);

	dth->dth_modify_shared = 0;
	dth->dth_active = 0;
	dth->dth_touched_leader_oid = 0;
	dth->dth_local_tx_started = 0;
	dth->dth_local_retry = 0;
	dth->dth_cos_done = 0;

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
dtx_handle_init(struct dtx_id *dti, daos_handle_t coh, struct dtx_epoch *epoch,
		uint16_t sub_modification_cnt, uint32_t pm_ver,
		daos_unit_oid_t *leader_oid, struct dtx_id *dti_cos,
		int dti_cos_cnt, struct dtx_memberships *mbs, bool leader,
		bool solo, bool sync, bool dist, bool migration,
		bool ignore_uncommitted, bool force_refresh, bool resent,
		bool prepared, struct dtx_handle *dth)
{
	if (sub_modification_cnt > DTX_SUB_MOD_MAX) {
		D_ERROR("Too many modifications in a single transaction:"
			"%u > %u\n", sub_modification_cnt, DTX_SUB_MOD_MAX);
		return -DER_OVERFLOW;
	}
	dth->dth_modification_cnt = sub_modification_cnt;

	dtx_shares_init(dth);

	dth->dth_xid = *dti;
	dth->dth_coh = coh;

	dth->dth_leader_oid = *leader_oid;
	dth->dth_ver = pm_ver;
	dth->dth_refs = 1;
	dth->dth_mbs = mbs;

	dth->dth_pinned = 0;
	dth->dth_cos_done = 0;
	dth->dth_resent = resent ? 1 : 0;
	dth->dth_solo = solo ? 1 : 0;
	dth->dth_modify_shared = 0;
	dth->dth_active = 0;
	dth->dth_touched_leader_oid = 0;
	dth->dth_local_tx_started = 0;
	dth->dth_local_retry = 0;
	dth->dth_dist = dist ? 1 : 0;
	dth->dth_for_migration = migration ? 1 : 0;
	dth->dth_ignore_uncommitted = ignore_uncommitted ? 1 : 0;
	dth->dth_force_refresh = force_refresh ? 1 : 0;
	dth->dth_prepared = prepared ? 1 : 0;

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

	dth->dth_op_seq = 0;
	dth->dth_oid_cnt = 0;
	dth->dth_oid_cap = 0;
	dth->dth_oid_array = NULL;

	dth->dth_dkey_hash = 0;

	if (daos_is_zero_dti(dti))
		return 0;

	if (!dtx_epoch_chosen(epoch)) {
		D_ERROR("initializing DTX "DF_DTI" with invalid epoch: value="
			DF_U64" first="DF_U64" flags=%x\n",
			DP_DTI(dti), epoch->oe_value, epoch->oe_first,
			epoch->oe_flags);
		return -DER_INVAL;
	}
	dth->dth_epoch = epoch->oe_value;
	dth->dth_epoch_bound = dtx_epoch_bound(epoch);

	if (dth->dth_modification_cnt == 0)
		return 0;

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
 * \param tgt_cnt	[IN]	number of targets.
 * \param flags		[IN]	See dtx_flags.
 * \param mbs		[IN]	DTX participants information.
 * \param dth		[OUT]	Pointer to the DTX handle.
 *
 * \return			Zero on success, negative value if error.
 */
int
dtx_leader_begin(daos_handle_t coh, struct dtx_id *dti,
		 struct dtx_epoch *epoch, uint16_t sub_modification_cnt,
		 uint32_t pm_ver, daos_unit_oid_t *leader_oid,
		 struct dtx_id *dti_cos, int dti_cos_cnt,
		 struct daos_shard_tgt *tgts, int tgt_cnt, uint32_t flags,
		 struct dtx_memberships *mbs, struct dtx_leader_handle *dlh)
{
	struct dtx_handle	*dth = &dlh->dlh_handle;
	int			 rc;
	int			 i;

	memset(dlh, 0, sizeof(*dlh));

	if (tgt_cnt > 0) {
		dlh->dlh_future = ABT_FUTURE_NULL;
		D_ALLOC_ARRAY(dlh->dlh_subs, tgt_cnt);
		if (dlh->dlh_subs == NULL)
			return -DER_NOMEM;

		for (i = 0; i < tgt_cnt; i++)
			dlh->dlh_subs[i].dss_tgt = tgts[i];
		dlh->dlh_sub_cnt = tgt_cnt;
	}

	rc = dtx_handle_init(dti, coh, epoch, sub_modification_cnt, pm_ver,
			     leader_oid, dti_cos, dti_cos_cnt, mbs, true,
			     (flags & DTX_SOLO) ? true : false,
			     (flags & DTX_SYNC) ? true : false,
			     (flags & DTX_DIST) ? true : false,
			     (flags & DTX_FOR_MIGRATION) ? true : false, false,
			     (flags & DTX_FORCE_REFRESH) ? true : false,
			     (flags & DTX_RESEND) ? true : false,
			     (flags & DTX_PREPARED) ? true : false, dth);

	D_DEBUG(DB_IO, "Start DTX "DF_DTI" sub modification %d, ver %u, leader "
		DF_UOID", dti_cos_cnt %d, flags %x: "DF_RC"\n",
		DP_DTI(dti), sub_modification_cnt, dth->dth_ver,
		DP_UOID(*leader_oid), dti_cos_cnt, flags, DP_RC(rc));

	if (rc != 0)
		D_FREE(dlh->dlh_subs);

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
dtx_leader_end(struct dtx_leader_handle *dlh, struct ds_cont_child *cont,
	       int result)
{
	struct dtx_handle		*dth = &dlh->dlh_handle;
	struct dtx_entry		*dte;
	struct dtx_memberships		*mbs;
	size_t				 size;
	uint32_t			 flags;
	int				 status = -1;
	int				 rc = 0;
	bool				 aborted = false;
	bool				 unpin = false;

	D_ASSERT(cont != NULL);

	dtx_shares_fini(dth);

	/* NB: even the local request failure, dth_ent == NULL, we
	 * should still wait for remote object to finish the request.
	 */

	if (dlh->dlh_sub_cnt != 0)
		rc = dtx_leader_wait(dlh);

	if (daos_is_zero_dti(&dth->dth_xid))
		D_GOTO(out, result = result < 0 ? result : rc);

	if (dth->dth_pinned || dth->dth_prepared) {
		status = vos_dtx_validation(dth);
		if (status == DTX_ST_COMMITTED || status == DTX_ST_COMMITTABLE)
			D_GOTO(out, result = 0);
	}

	if (result < 0 || rc < 0 || dth->dth_solo)
		D_GOTO(abort, result = result < 0 ? result : rc);

	switch (status) {
	case -1:
	case DTX_ST_PREPARED:
		break;
	case DTX_ST_INITED:
		if (dth->dth_modification_cnt == 0 ||
		    !dth->dth_active)
			break;
		/* full through */
	case DTX_ST_ABORTED:
		D_GOTO(abort, result = -DER_INPROGRESS);
	default:
		D_ASSERT(0);
	}

	if ((!dth->dth_active && dth->dth_dist) || dth->dth_prepared) {
		/* We do not know whether some other participants have
		 * some active entry for this DTX, consider distributed
		 * transaction case, the other participants may execute
		 * different operations. Sync commit the DTX for safe.
		 */
		dth->dth_sync = 1;
		goto sync;
	}

	/* For standalone modification, if leader modified nothing, then
	 * non-leader(s) must be the same, unpin the DTX via dtx_abort().
	 */
	if (!dth->dth_active) {
		unpin = true;
		D_GOTO(abort, result = 0);
	}

	/* If the DTX is started befoe DTX resync (for rebuild), then it is
	 * possbile that the DTX resync ULT may have aborted or committed
	 * the DTX during current ULT waiting for other non-leaders' reply.
	 * Let's check DTX status locally before marking as 'committable'.
	 */
	if (dth->dth_ver < cont->sc_dtx_resync_ver) {
		rc = vos_dtx_check(cont->sc_hdl, &dth->dth_xid,
				   NULL, NULL, NULL, NULL, false);
		/* Committed by race, do nothing. */
		if (rc == DTX_ST_COMMITTED || rc == DTX_ST_COMMITTABLE)
			D_GOTO(abort, result = 0);

		/* The DTX is marked as 'corrupted' by DTX resync by race,
		 * then let's abort it.
		 */
		if (rc == DTX_ST_CORRUPTED) {
			D_WARN(DF_UUID": DTX "DF_DTI" is marked as corrupted "
			       "by resync because of lost some participants\n",
			       DP_UUID(cont->sc_uuid), DP_DTI(&dth->dth_xid));
			D_GOTO(abort, result = -DER_TX_RESTART);
		}

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

	if (DAOS_FAIL_CHECK(DAOS_DTX_SKIP_PREPARE))
		D_GOTO(abort, result = 0);

	if (DAOS_FAIL_CHECK(DAOS_DTX_MISS_ABORT))
		D_GOTO(abort, result = -DER_IO);

	if (DAOS_FAIL_CHECK(DAOS_DTX_MISS_COMMIT))
		dth->dth_sync = 1;

	/* For synchronous DTX, do not add it into CoS cache, otherwise,
	 * we may have no way to remove it from the cache.
	 */
	if (dth->dth_sync)
		goto sync;

	D_ASSERT(dth->dth_mbs != NULL);

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
	 */

	if (!(mbs->dm_flags & DMF_SRDG_REP))
		flags = DCF_EXP_CMT;
	else if (dth->dth_modify_shared)
		flags = DCF_SHARED;
	else
		flags = 0;
	rc = dtx_add_cos(cont, dte, &dth->dth_leader_oid,
			 dth->dth_dkey_hash, dth->dth_epoch, flags);
	dtx_entry_put(dte);
	if (rc == 0) {
		if (!DAOS_FAIL_CHECK(DAOS_DTX_NO_COMMITTABLE)) {
			vos_dtx_mark_committable(dth);
			if (cont->sc_dtx_committable_count >
			    DTX_THRESHOLD_COUNT) {
				struct dss_module_info	*dmi;

				dmi = dss_get_module_info();
				sched_req_wakeup(dmi->dmi_dtx_cmt_req);
			}
		}
	} else {
		dth->dth_sync = 1;
	}

sync:
	if (dth->dth_sync) {
		dte = &dth->dth_dte;
		rc = dtx_commit(cont, &dte, NULL, 1);
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
	if (unpin || (result < 0 && result != -DER_AGAIN && !dth->dth_solo)) {
		/* Drop partial modification for distributed transaction. */
		vos_dtx_cleanup(dth);
		dte = &dth->dth_dte;
		dtx_abort(cont, dth->dth_epoch, &dte, 1);
		aborted = true;
	}

out:
	if (!daos_is_zero_dti(&dth->dth_xid)) {
		if (result < 0 && !aborted)
			vos_dtx_cleanup(dth);

		vos_dtx_rsrvd_fini(dth);

		D_DEBUG(DB_IO,
			"Stop the DTX "DF_DTI" ver %u, dkey %lu, %s, "
			"%s participator(s), cos %d/%d: rc "DF_RC"\n",
			DP_DTI(&dth->dth_xid), dth->dth_ver,
			(unsigned long)dth->dth_dkey_hash,
			dth->dth_sync ? "sync" : "async",
			dth->dth_solo ? "single" : "multiple",
			dth->dth_dti_cos_count,
			dth->dth_cos_done ? dth->dth_dti_cos_count : 0,
			DP_RC(result));
	}

	D_ASSERTF(result <= 0, "unexpected return value %d\n", result);

	/* Local modification is done, then need to handle CoS cache. */
	if (dth->dth_cos_done) {
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
 * \param coh		[IN]	Container handle.
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
 * \param dth		[OUT]	Pointer to the DTX handle.
 *
 * \return			Zero on success, negative value if error.
 */
int
dtx_begin(daos_handle_t coh, struct dtx_id *dti,
	  struct dtx_epoch *epoch, uint16_t sub_modification_cnt,
	  uint32_t pm_ver, daos_unit_oid_t *leader_oid,
	  struct dtx_id *dti_cos, int dti_cos_cnt, uint32_t flags,
	  struct dtx_memberships *mbs, struct dtx_handle *dth)
{
	int	rc;

	rc = dtx_handle_init(dti, coh, epoch, sub_modification_cnt,
			     pm_ver, leader_oid, dti_cos, dti_cos_cnt, mbs,
			     false, false, false,
			     (flags & DTX_DIST) ? true : false,
			     (flags & DTX_FOR_MIGRATION) ? true : false,
			     (flags & DTX_IGNORE_UNCOMMITTED) ? true : false,
			     (flags & DTX_FORCE_REFRESH) ? true : false,
			     (flags & DTX_RESEND) ? true : false, false, dth);

	D_DEBUG(DB_IO, "Start DTX "DF_DTI" sub modification %d, ver %u, "
		"dti_cos_cnt %d, flags %x: "DF_RC"\n",
		DP_DTI(dti), sub_modification_cnt,
		dth->dth_ver, dti_cos_cnt, flags, DP_RC(rc));

	return rc;
}

int
dtx_end(struct dtx_handle *dth, struct ds_cont_child *cont, int result)
{
	D_ASSERT(dth != NULL);

	dtx_shares_fini(dth);

	if (daos_is_zero_dti(&dth->dth_xid))
		return result;

	if (result < 0) {
		if (dth->dth_dti_cos_count > 0 && !dth->dth_cos_done) {
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
			else
				dth->dth_cos_done = 1;
		}

		/* 1. Drop partial modification for distributed transaction.
		 * 2. Remove the pinned DTX entry.
		 */
		vos_dtx_cleanup(dth);
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

static void
dtx_flush_on_deregister(struct dss_module_info *dmi,
			struct dtx_batched_cont_args *dbca)
{
	struct ds_cont_child	*cont = dbca->dbca_cont;
	struct dtx_stat		 stat = { 0 };
	uint64_t		 total = 0;
	uint32_t		 gen = cont->sc_dtx_batched_gen;
	int			 cnt;
	int			 rc = 0;

	dtx_stat(cont, &stat);

	/* gen != cont->sc_dtx_batched_gen means someone reopen the cont. */
	while (gen == cont->sc_dtx_batched_gen && rc >= 0) {
		struct dtx_entry	**dtes = NULL;
		struct dtx_cos_key	 *dcks = NULL;

		cnt = dtx_fetch_committable(cont, DTX_THRESHOLD_COUNT,
					    NULL, DAOS_EPOCH_MAX, &dtes, &dcks);
		if (cnt <= 0) {
			rc = cnt;
			break;
		}

		total += cnt;
		/* When flush_on_deregister, nobody will add more DTX
		 * into the CoS cache. So if accumulated commit count
		 * is more than the total committable ones, then some
		 * DTX entries cannot be removed from the CoS cache.
		 */
		D_ASSERTF(total <= stat.dtx_committable_count,
			  "Some DTX in CoS may cannot be removed: %lu/%lu\n",
			  (unsigned long)total,
			  (unsigned long)stat.dtx_committable_count);

		rc = dtx_commit(cont, dtes, dcks, cnt);
		dtx_free_committable(dtes, dcks, cnt);
	}

	if (rc < 0)
		D_ERROR(DF_UUID": Fail to flush CoS cache: rc = %d\n",
			DP_UUID(cont->sc_uuid), rc);
}

int
dtx_batched_commit_register(struct ds_cont_child *cont)
{
	struct dss_module_info		*dmi = dss_get_module_info();
	d_list_t			*pool_head;
	d_list_t			*cont_head;
	struct dtx_batched_pool_args	*dbpa;
	struct dtx_batched_cont_args	*dbca;
	struct umem_attr		 uma;
	int				 rc;
	bool				 new_pool = true;

	/* If batched commit ULT is not enabled, then sync commit DTX. */
	if (!dmi->dmi_dtx_batched_started) {
		cont->sc_dtx_cos_shutdown = 1;
		goto out;
	}

	D_ASSERT(cont != NULL);
	D_ASSERT(cont->sc_open > 0);

	pool_head = &dmi->dmi_dtx_batched_pool_list;
	cont_head = &dmi->dmi_dtx_batched_cont_list;

	d_list_for_each_entry(dbpa, pool_head, dbpa_sys_link) {
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
			return -DER_NOMEM;

		D_INIT_LIST_HEAD(&dbpa->dbpa_sys_link);
		D_INIT_LIST_HEAD(&dbpa->dbpa_cont_list);
		dbpa->dbpa_pool = cont->sc_pool;
	}

	D_ALLOC_PTR(dbca);
	if (dbca == NULL) {
		if (new_pool)
			D_FREE(dbpa);
		return -DER_NOMEM;
	}

	/* Former dtx_batched_commit_deregister is waiting for
	 * dtx_flush_on_deregister, we reopening the container.
	 * Let's reuse the CoS tree.
	 */
	if (daos_handle_is_valid(cont->sc_dtx_cos_hdl))
		goto add;

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
		if (new_pool)
			D_FREE(dbpa);
		return rc;
	}

	cont->sc_dtx_committable_count = 0;
	D_INIT_LIST_HEAD(&cont->sc_dtx_cos_list);
	cont->sc_dtx_resync_ver = 1;

add:
	cont->sc_dtx_cos_shutdown = 0;
	ds_cont_child_get(cont);
	dbca->dbca_refs = 0;
	dbca->dbca_cont = cont;
	dbca->dbca_pool = dbpa;
	dbca->dbca_gen = dtx_agg_gen;
	d_list_add_tail(&dbca->dbca_sys_link, cont_head);
	d_list_add_tail(&dbca->dbca_pool_link, &dbpa->dbpa_cont_list);
	if (new_pool)
		d_list_add_tail(&dbpa->dbpa_sys_link, pool_head);

out:
	cont->sc_closing = 0;
	cont->sc_dtx_batched_gen++;

	return 0;
}

void
dtx_batched_commit_deregister(struct ds_cont_child *cont)
{
	struct dss_module_info		*dmi = dss_get_module_info();
	struct dtx_batched_pool_args	*dbpa;
	struct dtx_batched_cont_args	*dbca;

	D_ASSERT(cont != NULL);
	D_ASSERT(cont->sc_open == 0);
	D_ASSERT(cont->sc_closing == 0);

	cont->sc_closing = 1;
	cont->sc_dtx_cos_shutdown = 1;

	d_list_for_each_entry(dbpa, &dmi->dmi_dtx_batched_pool_list,
			      dbpa_sys_link) {
		if (dbpa->dbpa_pool == cont->sc_pool) {
			d_list_for_each_entry(dbca, &dbpa->dbpa_cont_list,
					      dbca_pool_link) {
				if (dbca->dbca_cont == cont) {
					/* Unlink the dbca firstly, then even
					 * if the container is reopened during
					 * my waiting for current deregister,
					 * it will not find current dbca.
					 */
					d_list_del_init(&dbca->dbca_sys_link);
					d_list_del_init(&dbca->dbca_pool_link);
					dbca->dbca_deregister = 1;
					dtx_flush_on_deregister(dmi, dbca);
					dtx_free_dbca(dbca);
					return;
				}
			}
		}
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

	rc = vos_dtx_check(coh, dti, epoch, pm_ver, NULL, NULL, true);
	switch (rc) {
	case DTX_ST_PREPARED:
		return 0;
	case DTX_ST_COMMITTED:
	case DTX_ST_COMMITTABLE:
		return -DER_ALREADY;
	case DTX_ST_CORRUPTED:
		return -DER_DATA_LOSS;
	case -DER_NONEXIST:
		age = dtx_hlc_age2sec(dti->dti_hlc);
		if (age > DTX_AGG_THD_AGE_LO ||
		    DAOS_FAIL_CHECK(DAOS_DTX_LONG_TIME_RESEND)) {
			D_ERROR("Not sure about whether the old RPC "DF_DTI
				" is resent or not. Age="DF_U64" s.\n",
				DP_DTI(dti), age);
			rc = -DER_EP_OLD;
		}
		return rc;
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

		if (sub->dss_tgt.st_rank == DAOS_TGT_IGNORE ||
		    (i == daos_fail_value_get() &&
		     DAOS_FAIL_CHECK(DAOS_DTX_SKIP_PREPARE))) {
			int ret;

			ret = ABT_future_set(future, dlh);
			D_ASSERTF(ret == ABT_SUCCESS,
				  "ABT_future_set failed %d.\n", ret);
			continue;
		}

		rc = ult_arg->func(dlh, ult_arg->func_arg, i, dtx_sub_comp_cb);
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

	D_FREE(ult_arg);
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
	rc = dss_ult_create(dtx_leader_exec_ops_ult, ult_arg, DSS_XS_IOFW,
			    dss_get_module_info()->dmi_tgt_id,
			    DSS_DEEP_STACK_SZ, NULL);
	if (rc != 0) {
		D_ERROR("ult create failed "DF_RC"\n", DP_RC(rc));
		D_FREE(ult_arg);
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
dtx_obj_sync(struct ds_cont_child *cont, daos_unit_oid_t *oid,
	     daos_epoch_t epoch)
{
	int	cnt;
	int	rc = 0;

	while (!cont->sc_closing) {
		struct dtx_entry	**dtes = NULL;
		struct dtx_cos_key	 *dcks = NULL;

		cnt = dtx_fetch_committable(cont, DTX_THRESHOLD_COUNT, oid,
					    epoch, &dtes, &dcks);
		if (cnt <= 0) {
			rc = cnt;
			if (rc < 0)
				D_ERROR("Failed to fetch dtx: "DF_RC"\n",
					DP_RC(rc));
			break;
		}

		rc = dtx_commit(cont, dtes, dcks, cnt);
		dtx_free_committable(dtes, dcks, cnt);
		if (rc < 0) {
			D_ERROR("Fail to commit dtx: "DF_RC"\n", DP_RC(rc));
			break;
		}
	}

	if (rc == 0 && oid != NULL && !cont->sc_closing)
		rc = vos_dtx_mark_sync(cont->sc_hdl, *oid, epoch);

	return rc;
}
