/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * ds_cont: Target Operations
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related target states.
 *
 * Data structures used here:
 *
 *                 Pool           Container
 *
 *         Global  ds_pool
 *                 ds_pool_hdl
 *
 *   Thread-local  ds_pool_child  ds_cont_child
 *                                ds_cont_hdl
 */
#define D_LOGFAC	DD_FAC(container)

#include <daos_srv/container.h>

#include <daos/checksum.h>
#include <daos/rpc.h>
#include <daos_srv/pool.h>
#include <daos_srv/vos.h>
#include <daos_srv/iv.h>
#include <daos_srv/srv_obj_ec.h>
#include "rpc.h"
#include "srv_internal.h"
#include <daos/cont_props.h>
#include <daos/dedup.h>

/* Per VOS container aggregation ULT ***************************************/

/*
 * VOS aggregation should try to avoid aggregating in the epoch range where
 * lots of data records are pending to commit, so the highest aggregate epoch
 * will be:
 *
 * current HLC - (DTX batched commit threshold + buffer period)
 */
#define DAOS_AGG_THRESHOLD	(DTX_COMMIT_THRESHOLD_AGE + 10) /* seconds */

static inline int
cont_aggregate_epr(struct ds_cont_child *cont, daos_epoch_range_t *epr)
{
	int	rc;

	/*
	 * Avoid calling into vos_aggregate() when aborting aggregation
	 * on ds_cont_child purging.
	 */
	D_ASSERT(cont->sc_agg_req != NULL);
	if (dss_ult_exiting(cont->sc_agg_req))
		return 1;

	rc = ds_obj_ec_aggregate(cont, epr, dss_ult_yield,
				 (void *)cont->sc_agg_req);
	if (rc)
		D_ERROR("EC aggregation returned: "DF_RC"\n", DP_RC(rc));

	if (dss_ult_exiting(cont->sc_agg_req))
		return 1;

	rc = vos_aggregate(cont->sc_hdl, epr, ds_csum_recalc, dss_ult_yield,
			   (void *)cont->sc_agg_req);

	/* Wake up GC ULT */
	sched_req_wakeup(cont->sc_pool->spc_gc_req);
	return rc;
}

int
ds_get_cont_props(struct cont_props *cont_props, struct ds_iv_ns *pool_ns,
		  uuid_t cont_uuid)
{
	daos_prop_t	*props;
	int		 rc;

	props = daos_prop_alloc(7);
	if (props == NULL)
		return -DER_NOMEM;

	props->dpp_entries[0].dpe_type = DAOS_PROP_CO_CSUM;
	props->dpp_entries[1].dpe_type = DAOS_PROP_CO_CSUM_CHUNK_SIZE;
	props->dpp_entries[2].dpe_type = DAOS_PROP_CO_CSUM_SERVER_VERIFY;
	props->dpp_entries[3].dpe_type = DAOS_PROP_CO_DEDUP;
	props->dpp_entries[4].dpe_type = DAOS_PROP_CO_DEDUP_THRESHOLD;
	props->dpp_entries[5].dpe_type = DAOS_PROP_CO_COMPRESS;
	props->dpp_entries[6].dpe_type = DAOS_PROP_CO_ENCRYPT;

	rc = cont_iv_prop_fetch(pool_ns, cont_uuid, props);

	if (rc == DER_SUCCESS)
		daos_props_2cont_props(props, cont_props);

	daos_prop_free(props);

	return rc;
}

int
ds_cont_csummer_init(struct ds_cont_child *cont)
{
	uint32_t		csum_val;
	int			rc;
	struct cont_props	*cont_props;
	bool			dedup_only = false;

	D_ASSERT(cont != NULL);
	cont_props = &cont->sc_props;

	if (cont->sc_props_fetched)
		return 0;

	/** Get the container csum related properties
	 * Need the pool for the IV namespace
	 */
	D_ASSERT(cont->sc_csummer == NULL);
	rc = ds_get_cont_props(cont_props, cont->sc_pool->spc_pool->sp_iv_ns,
			       cont->sc_uuid);
	if (rc != 0)
		goto done;

	/* Check again since IV fetch yield */
	if (cont->sc_props_fetched)
		goto done;
	cont->sc_props_fetched = 1;

	csum_val = cont_props->dcp_csum_type;
	if (!daos_cont_csum_prop_is_enabled(csum_val)) {
		dedup_only = true;
		csum_val = dedup_get_csum_algo(cont_props);
	}

	/** If enabled, initialize the csummer for the container */
	if (daos_cont_csum_prop_is_enabled(csum_val)) {
		rc = daos_csummer_init_with_type(&cont->sc_csummer,
					    daos_contprop2hashtype(csum_val),
					    cont_props->dcp_chunksize,
					    cont_props->dcp_srv_verify);
		if (dedup_only)
			dedup_configure_csummer(cont->sc_csummer, cont_props);
	}
done:
	return rc;
}


static bool
cont_aggregate_runnable(struct ds_cont_child *cont)
{
	struct ds_pool		*pool = cont->sc_pool->spc_pool;
	struct sched_request	*req = cont->sc_agg_req;

	if (!cont->sc_props_fetched)
		ds_cont_csummer_init(cont);

	if (cont->sc_props.dcp_dedup_enabled ||
	    cont->sc_props.dcp_compress_enabled ||
	    cont->sc_props.dcp_encrypt_enabled) {
		D_DEBUG(DB_EPC, DF_CONT": skip aggregation for "
			"deduped/compressed/encrypted container\n",
			DP_CONT(cont->sc_pool->spc_uuid, cont->sc_uuid));
		return false;
	}

	/* snapshot list isn't fetched yet */
	if (cont->sc_aggregation_max == 0) {
		D_DEBUG(DB_EPC, "No aggregation before snapshots fetched\n");
		/* fetch snapshot list */
		if (dss_get_module_info()->dmi_tgt_id == 0)
			ds_cont_tgt_snapshots_refresh(cont->sc_pool->spc_uuid,
						      cont->sc_uuid);
		return false;
	}

	if (pool->sp_reclaim == DAOS_RECLAIM_DISABLED) {
		D_DEBUG(DB_EPC, "Pool reclaim strategy is disabled\n");
		return false;
	}

	if (pool->sp_reclaim == DAOS_RECLAIM_LAZY && dss_xstream_is_busy() &&
	    sched_req_space_check(req) == SCHED_SPACE_PRESS_NONE) {
		D_DEBUG(DB_EPC, "Pool reclaim strategy is lazy, service is "
			"busy and no space pressure\n");
		return false;
	}

	return true;
}

static int
cont_child_aggregate(struct ds_cont_child *cont, uint64_t *msecs)
{
	daos_epoch_t		epoch_max, epoch_min;
	daos_epoch_range_t	epoch_range;
	vos_cont_info_t		cinfo;
	struct sched_request	*req = cont->sc_agg_req;
	uint64_t		hlc = crt_hlc_get();
	uint64_t		change_hlc;
	uint64_t		interval;
	uint64_t		*snapshots = NULL;
	int			snapshots_nr;
	int			tgt_id = dss_get_module_info()->dmi_tgt_id;
	int			i, rc;

	/* Check if it's ok to start aggregation in every 2 seconds */
	*msecs = 2ULL * 1000;
	if (!cont_aggregate_runnable(cont))
		return 0;

	/*
	 * Query the 'Highest Aggregated Epoch', the HAE will be bumped
	 * in vos_aggregate()
	 */
	rc = vos_cont_query(cont->sc_hdl, &cinfo);
	if (rc)
		return rc;

	change_hlc = max(cont->sc_snapshot_delete_hlc,
			 cont->sc_pool->spc_rebuild_end_hlc);
	if (cont->sc_aggregation_full_scan_hlc < change_hlc) {
		/* Snapshot has been deleted or rebuild happens since the last
		 * aggregation, let's restart from 0.
		 */
		epoch_min = 0;
		D_DEBUG(DB_EPC, "change hlc "DF_U64" > full "DF_U64"\n",
			change_hlc, cont->sc_aggregation_full_scan_hlc);
	} else {
		epoch_min = cinfo.ci_hae;
	}

	interval = (uint64_t)DAOS_AGG_THRESHOLD * NSEC_PER_SEC;
	D_ASSERT(hlc > (interval * 2));
	/*
	 * Assume 'current hlc - interval' as the highest stable view (all
	 * transactions under this epoch is either committed or aborted).
	 */
	epoch_max = hlc - interval;

	if (epoch_min > epoch_max) {
		/* Nothing can be aggregated */
		*msecs = max(*msecs, (epoch_min - epoch_max) / NSEC_PER_MSEC);
		return 0;
	} else if (epoch_min > epoch_max - interval &&
		   sched_req_space_check(req) == SCHED_SPACE_PRESS_NONE) {
		/*
		 * When there isn't space pressure, don't aggregate too often,
		 * otherwise, aggregation will be inefficient because the data
		 * to be aggregated could be changed by new update very soon.
		 */
		return 0;
	}

	/* Cap the aggregation upper bound to the snapshot in creating */
	if (epoch_max >= cont->sc_aggregation_max)
		epoch_max = cont->sc_aggregation_max - 1;

	D_ASSERTF(epoch_min <= epoch_max, "Min "DF_U64", Max "DF_U64"\n",
		  epoch_min, epoch_max);

	if (cont->sc_pool->spc_rebuild_fence != 0) {
		uint64_t rebuild_fence = cont->sc_pool->spc_rebuild_fence;
		int	j;
		int	insert_idx;

		D_DEBUG(DB_EPC, "rebuild fence "DF_U64"\n", rebuild_fence);
		/* Insert the rebuild_epoch into snapshots */
		D_ALLOC(snapshots, (cont->sc_snapshots_nr + 1) *
			sizeof(daos_epoch_t));
		if (snapshots == NULL)
			return -DER_NOMEM;

		for (j = 0, insert_idx = 0; j < cont->sc_snapshots_nr; j++) {
			if (cont->sc_snapshots[j] < rebuild_fence) {
				snapshots[j] = cont->sc_snapshots[j];
				insert_idx++;
			} else {
				snapshots[j+1] = cont->sc_snapshots[j];
			}
		}
		snapshots[insert_idx] = rebuild_fence;
		snapshots_nr = cont->sc_snapshots_nr + 1;
	} else {
		/* Since sc_snapshots might be freed by other ULT, let's
		 * always copy here.
		 */
		snapshots_nr = cont->sc_snapshots_nr;
		if (snapshots_nr > 0) {
			D_ALLOC(snapshots, snapshots_nr * sizeof(daos_epoch_t));
			if (snapshots == NULL)
				return -DER_NOMEM;

			memcpy(snapshots, cont->sc_snapshots,
					snapshots_nr * sizeof(daos_epoch_t));
		}
	}

	/* Find highest snapshot less than last aggregated epoch. */
	for (i = 0; i < snapshots_nr && snapshots[i] < epoch_min; ++i)
		;

	if (i == 0)
		epoch_range.epr_lo = 0;
	else
		epoch_range.epr_lo = snapshots[i - 1] + 1;

	if (epoch_range.epr_lo >= epoch_max)
		D_GOTO(free, rc = 0);

	*msecs = 0;
	D_DEBUG(DB_EPC, DF_CONT"[%d]: MIN: %lu; HLC: %lu\n",
		DP_CONT(cont->sc_pool->spc_uuid, cont->sc_uuid),
		tgt_id, epoch_min, hlc);

	for ( ; i < snapshots_nr && snapshots[i] < epoch_max; ++i) {
		epoch_range.epr_hi = snapshots[i];
		D_DEBUG(DB_EPC, DF_CONT"[%d]: Aggregating {%lu -> %lu}\n",
			DP_CONT(cont->sc_pool->spc_uuid, cont->sc_uuid),
			tgt_id, epoch_range.epr_lo, epoch_range.epr_hi);

		rc = cont_aggregate_epr(cont, &epoch_range);
		if (rc)
			D_GOTO(free, rc);
		epoch_range.epr_lo = epoch_range.epr_hi + 1;
	}

	D_ASSERT(epoch_range.epr_lo <= epoch_max);
	if (epoch_range.epr_lo == epoch_max)
		goto out;

	epoch_range.epr_hi = epoch_max;
	D_DEBUG(DB_EPC, DF_CONT"[%d]: Aggregating {%lu -> %lu}\n",
		DP_CONT(cont->sc_pool->spc_uuid, cont->sc_uuid),
		tgt_id, epoch_range.epr_lo, epoch_range.epr_hi);

	rc = cont_aggregate_epr(cont, &epoch_range);
out:
	if (rc == 0 && epoch_min == 0)
		cont->sc_aggregation_full_scan_hlc = hlc;

	D_DEBUG(DB_EPC, DF_CONT"[%d]: Aggregating finished\n",
		DP_CONT(cont->sc_pool->spc_uuid, cont->sc_uuid), tgt_id);
free:
	if (snapshots != NULL)
		D_FREE(snapshots);

	return rc;
}

static void
cont_aggregate_ult(void *arg)
{
	struct ds_cont_child	*cont = arg;
	struct dss_module_info	*dmi = dss_get_module_info();
	int			 rc = 0;

	D_DEBUG(DB_EPC, DF_CONT"[%d]: Aggregation ULT started\n",
		DP_CONT(cont->sc_pool->spc_uuid, cont->sc_uuid),
		dmi->dmi_tgt_id);

	D_ASSERT(cont->sc_agg_req != NULL);
	while (!dss_ult_exiting(cont->sc_agg_req)) {
		uint64_t msecs;	/* milli seconds */

		rc = cont_child_aggregate(cont, &msecs);
		if (rc == -DER_SHUTDOWN) {
			break;	/* pool destroyed */
		} else if (rc < 0) {
			D_ERROR(DF_CONT": VOS aggregate failed. "DF_RC"\n",
				DP_CONT(cont->sc_pool->spc_uuid, cont->sc_uuid),
				DP_RC(rc));
			/* Sleep 2 seconds when last aggregation failed */
			msecs = 2ULL * 1000;
		}

		if (dss_ult_exiting(cont->sc_agg_req))
			break;

		sched_req_sleep(cont->sc_agg_req, msecs);
	}

	D_DEBUG(DB_EPC, DF_CONT"[%d]: Aggregation ULT stopped\n",
		DP_CONT(cont->sc_pool->spc_uuid, cont->sc_uuid),
		dmi->dmi_tgt_id);
}

static int
cont_start_agg_ult(struct ds_cont_child *cont)
{
	struct dss_module_info	*dmi = dss_get_module_info();
	struct sched_req_attr	 attr;
	ABT_thread		 agg_ult = ABT_THREAD_NULL;
	int			 rc;

	D_ASSERT(cont != NULL);
	if (cont->sc_agg_req != NULL)
		return 0;

	rc = dss_ult_create(cont_aggregate_ult, cont, DSS_ULT_GC,
			    DSS_TGT_SELF, 0, &agg_ult);
	if (rc) {
		D_ERROR(DF_CONT"[%d]: Failed to create aggregation ULT. %d\n",
			DP_CONT(cont->sc_pool->spc_uuid, cont->sc_uuid),
			dmi->dmi_tgt_id, rc);
		return rc;
	}

	D_ASSERT(agg_ult != ABT_THREAD_NULL);
	sched_req_attr_init(&attr, SCHED_REQ_GC, &cont->sc_pool->spc_uuid);
	cont->sc_agg_req = sched_req_get(&attr, agg_ult);
	if (cont->sc_agg_req == NULL) {
		D_CRIT(DF_CONT"[%d]: Failed to get req for aggregation ULT\n",
		       DP_CONT(cont->sc_pool->spc_uuid, cont->sc_uuid),
		       dmi->dmi_tgt_id);
		ABT_thread_join(agg_ult);
		return -DER_NOMEM;
	}

	return 0;
}

static void
cont_stop_agg_ult(struct ds_cont_child *cont)
{
	if (cont->sc_agg_req == NULL)
		return;

	D_DEBUG(DB_EPC, DF_CONT"[%d]: Stopping aggregation ULT\n",
		DP_CONT(cont->sc_pool->spc_uuid, cont->sc_uuid),
		dss_get_module_info()->dmi_tgt_id);

	sched_req_wait(cont->sc_agg_req, true);
	sched_req_put(cont->sc_agg_req);
	cont->sc_agg_req = NULL;
}

/* Per VOS container DTX re-index ULT ***************************************/

void
ds_cont_dtx_reindex_ult(void *arg)
{
	struct ds_cont_child		*cont	= arg;
	struct dss_module_info		*dmi	= dss_get_module_info();
	uint64_t			 hint	= 0;
	int				 rc;

	D_DEBUG(DF_DSMS, DF_CONT": starting DTX reindex ULT on xstream %d\n",
		DP_CONT(NULL, cont->sc_uuid), dmi->dmi_tgt_id);

	while (!cont->sc_dtx_reindex_abort &&
	       !dss_xstream_exiting(dmi->dmi_xstream)) {
		rc = vos_dtx_cmt_reindex(cont->sc_hdl, &hint);
		if (rc < 0) {
			D_ERROR(DF_UUID": DTX reindex failed: rc = %d\n",
				DP_UUID(cont->sc_uuid), rc);
			goto out;
		}

		if (rc > 0) {
			D_DEBUG(DF_DSMS, DF_CONT": DTX reindex done\n",
				DP_CONT(NULL, cont->sc_uuid));
			goto out;
		}

		ABT_thread_yield();
	}

	D_DEBUG(DF_DSMS, DF_CONT": stopping DTX reindex ULT on stream %d\n",
		DP_CONT(NULL, cont->sc_uuid), dmi->dmi_tgt_id);

out:
	cont->sc_dtx_reindex = 0;
	ds_cont_child_put(cont);
}

static int
cont_start_dtx_reindex_ult(struct ds_cont_child *cont)
{
	int rc;

	D_ASSERT(cont != NULL);

	if (cont->sc_dtx_reindex || cont->sc_dtx_reindex_abort)
		return 0;

	ds_cont_child_get(cont);
	cont->sc_dtx_reindex = 1;
	rc = dss_ult_create(ds_cont_dtx_reindex_ult, cont,
			    DSS_ULT_DTX_RESYNC, DSS_TGT_SELF, 0, NULL);
	if (rc != 0) {
		D_ERROR(DF_UUID": Failed to create DTX reindex ULT: rc %d\n",
			DP_UUID(cont->sc_uuid), rc);
		cont->sc_dtx_reindex = 0;
		ds_cont_child_put(cont);
	}

	return rc;
}

static void
cont_stop_dtx_reindex_ult(struct ds_cont_child *cont)
{
	if (!cont->sc_dtx_reindex)
		return;

	cont->sc_dtx_reindex_abort = 1;

	while (cont->sc_dtx_reindex)
		ABT_thread_yield();

	cont->sc_dtx_reindex_abort = 0;
}

/* ds_cont_child *******************************************************/
static inline struct ds_cont_child *
cont_child_obj(struct daos_llink *llink)
{
	return container_of(llink, struct ds_cont_child, sc_list);
}

static int
cont_child_alloc_ref(void *co_uuid, unsigned int ksize, void *po_uuid,
		     struct daos_llink **link)
{
	struct ds_cont_child	*cont;
	int			rc;

	D_ASSERT(po_uuid != NULL);
	D_DEBUG(DF_DSMS, DF_CONT": opening\n", DP_CONT(po_uuid, co_uuid));

	D_ALLOC_PTR(cont);
	if (cont == NULL)
		return -DER_NOMEM;

	rc = ABT_mutex_create(&cont->sc_mutex);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out;
	}

	rc = ABT_cond_create(&cont->sc_dtx_resync_cond);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out_mutex;
	}

	cont->sc_pool = ds_pool_child_lookup(po_uuid);
	if (cont->sc_pool == NULL) {
		rc = -DER_NO_HDL;
		goto out_cond;
	}

	rc = vos_cont_open(cont->sc_pool->spc_hdl, co_uuid, &cont->sc_hdl);
	if (rc != 0)
		goto out_pool;

	uuid_copy(cont->sc_uuid, co_uuid);
	cont->sc_aggregation_full_scan_hlc = 0;
	/* prevent aggregation till snapshot iv refreshed */
	cont->sc_aggregation_max = 0;
	cont->sc_snapshots_nr = 0;
	cont->sc_snapshots = NULL;
	D_INIT_LIST_HEAD(&cont->sc_link);

	*link = &cont->sc_list;
	return 0;

out_pool:
	ds_pool_child_put(cont->sc_pool);
out_cond:
	ABT_cond_free(&cont->sc_dtx_resync_cond);
out_mutex:
	ABT_mutex_free(&cont->sc_mutex);
out:
	D_FREE(cont);
	return rc;
}

static void
cont_child_free_ref(struct daos_llink *llink)
{
	struct ds_cont_child *cont = cont_child_obj(llink);

	D_ASSERT(cont->sc_pool != NULL);
	D_ASSERT(!daos_handle_is_inval(cont->sc_hdl));

	D_DEBUG(DF_DSMS, DF_CONT": freeing\n",
		DP_CONT(cont->sc_pool->spc_uuid, cont->sc_uuid));

	vos_cont_close(cont->sc_hdl);
	ds_pool_child_put(cont->sc_pool);
	daos_csummer_destroy(&cont->sc_csummer);

	ABT_cond_free(&cont->sc_dtx_resync_cond);
	ABT_mutex_free(&cont->sc_mutex);
	D_FREE(cont);
}

static bool
cont_child_cmp_keys(const void *key, unsigned int ksize,
		    struct daos_llink *llink)
{
	struct ds_cont_child *cont = cont_child_obj(llink);

	return uuid_compare(key, cont->sc_uuid) == 0;
}

static uint32_t
cont_child_rec_hash(struct daos_llink *llink)
{
	struct ds_cont_child *cont = cont_child_obj(llink);

	return d_hash_string_u32((const char *)cont->sc_uuid, sizeof(uuid_t));
}

static struct daos_llink_ops cont_child_cache_ops = {
	.lop_alloc_ref	= cont_child_alloc_ref,
	.lop_free_ref	= cont_child_free_ref,
	.lop_cmp_keys	= cont_child_cmp_keys,
	.lop_rec_hash	= cont_child_rec_hash,
};

int
ds_cont_child_cache_create(struct daos_lru_cache **cache)
{
	/*
	 * ds_cont_child isn't cached in LRU, it'll be removed from hash table
	 * then freed on last user putting reference count.
	 */
	return daos_lru_cache_create(-1 /* no lru cache */, D_HASH_FT_NOLOCK,
				     &cont_child_cache_ops, cache);
}

void
ds_cont_child_cache_destroy(struct daos_lru_cache *cache)
{
	daos_lru_cache_destroy(cache);
}

/*
 * If "po_uuid == NULL", then this is assumed to be a pure lookup. In this case,
 * -DER_NONEXIST is returned if the ds_cont_child object does not exist.
 */
static int
cont_child_lookup(struct daos_lru_cache *cache, const uuid_t co_uuid,
		  const uuid_t po_uuid, struct ds_cont_child **cont)
{
	struct daos_llink      *llink;
	int			rc;

	rc = daos_lru_ref_hold(cache, (void *)co_uuid, sizeof(uuid_t),
			       (void *)po_uuid, &llink);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DF_DSMS, DF_CONT": failed to lookup%s "
				"container: "DF_RC"\n",
				DP_CONT(po_uuid, co_uuid),
				po_uuid == NULL ? "" : "/create", DP_RC(rc));
		else
			D_ERROR(DF_CONT": failed to lookup%s container: "
				""DF_RC"\n", DP_CONT(po_uuid, co_uuid),
				po_uuid == NULL ? "" : "/create", DP_RC(rc));
		return rc;
	}

	*cont = cont_child_obj(llink);
	return 0;
}

static void
cont_child_put(struct daos_lru_cache *cache, struct ds_cont_child *cont)
{
	daos_lru_ref_release(cache, &cont->sc_list);
}

static inline bool
cont_child_started(struct ds_cont_child *cont_child)
{
	/* Started container is linked in spc_cont_list */
	return !d_list_empty(&cont_child->sc_link);
}

static void
cont_child_stop(struct ds_cont_child *cont_child)
{
	if (!cont_child->sc_stopping) {
		D_DEBUG(DF_DSMS, DF_CONT"[%d]: Stopping container\n",
			DP_CONT(cont_child->sc_pool->spc_uuid,
				cont_child->sc_uuid),
			dss_get_module_info()->dmi_tgt_id);

		cont_child->sc_stopping = 1;
		d_list_del_init(&cont_child->sc_link);

		/* cont_stop_agg_ult() may yield */
		cont_stop_agg_ult(cont_child);
		ds_cont_child_put(cont_child);
	} else {
		D_ASSERT(!cont_child_started(cont_child));
	}
}

void
ds_cont_child_stop_all(struct ds_pool_child *pool_child)
{
	d_list_t		*cont_list;
	struct ds_cont_child	*cont_child;

	D_DEBUG(DF_DSMS, DF_UUID"[%d]: Stopping all containers\n",
		DP_UUID(pool_child->spc_uuid),
		dss_get_module_info()->dmi_tgt_id);

	D_ASSERT(d_list_empty(&pool_child->spc_list));

	cont_list = &pool_child->spc_cont_list;
	while (!d_list_empty(cont_list)) {
		cont_child = d_list_entry(cont_list->next,
					  struct ds_cont_child, sc_link);
		cont_child_stop(cont_child);
	}
}

static int
cont_child_start(struct ds_pool_child *pool_child, const uuid_t co_uuid,
		 struct ds_cont_child **cont_out)
{
	struct dsm_tls		*tls = dsm_tls_get();
	struct ds_cont_child	*cont_child;
	int			 tgt_id = dss_get_module_info()->dmi_tgt_id;
	int			 rc;

	D_DEBUG(DF_DSMS, DF_CONT"[%d]: Starting container\n",
		DP_CONT(pool_child->spc_uuid, co_uuid), tgt_id);

	rc = cont_child_lookup(tls->dt_cont_cache, co_uuid,
			       pool_child->spc_uuid, &cont_child);
	if (rc) {
		D_CDEBUG(rc != -DER_NONEXIST, DLOG_ERR, DF_DSMS,
			 DF_CONT"[%d]: Load container error:%d\n",
			 DP_CONT(pool_child->spc_uuid, co_uuid), tgt_id, rc);
		return rc;
	}

	/*
	 * The container is in stopping because:
	 * 1. Container is going to be destroyed, or;
	 * 2. Pool is going to be destroyed, or;
	 * 3. Pool service is going to be stopped;
	 */
	if (cont_child->sc_stopping) {
		D_ERROR(DF_CONT"[%d]: Container is in stopping\n",
			DP_CONT(pool_child->spc_uuid, co_uuid), tgt_id);
		rc = -DER_SHUTDOWN;
	} else if (!cont_child_started(cont_child)) {
		rc = cont_start_agg_ult(cont_child);
		if (!rc) {
			d_list_add_tail(&cont_child->sc_link,
					&pool_child->spc_cont_list);
			ds_cont_child_get(cont_child);
		}
	}

	if (!rc && cont_out != NULL) {
		*cont_out = cont_child;
		ds_cont_child_get(cont_child);
	}

	/* Put the ref from cont_child_lookup() */
	ds_cont_child_put(cont_child);
	return rc;
}

static int
cont_child_start_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		    vos_iter_type_t type, vos_iter_param_t *iter_param,
		    void *data, unsigned *acts)
{
	struct ds_pool_child	*pool_child = data;

	return cont_child_start(pool_child, entry->ie_couuid, NULL);
}

int
ds_cont_child_start_all(struct ds_pool_child *pool_child)
{
	vos_iter_param_t	iter_param = { 0 };
	struct vos_iter_anchors	anchors = { 0 };
	int			rc;

	D_DEBUG(DF_DSMS, DF_UUID"[%d]: Starting all containers\n",
		DP_UUID(pool_child->spc_uuid),
		dss_get_module_info()->dmi_tgt_id);

	iter_param.ip_hdl = pool_child->spc_hdl;
	/* The quantity of container is small, no need to yield */
	rc = vos_iterate(&iter_param, VOS_ITER_COUUID, false, &anchors,
			 cont_child_start_cb, NULL, (void *)pool_child, NULL);
	return rc;
}

/* ds_cont_hdl ****************************************************************/

static inline struct ds_cont_hdl *
cont_hdl_obj(d_list_t *rlink)
{
	return container_of(rlink, struct ds_cont_hdl, sch_entry);
}

static bool
cont_hdl_key_cmp(struct d_hash_table *htable, d_list_t *rlink,
		 const void *key, unsigned int ksize)
{
	struct ds_cont_hdl *hdl = cont_hdl_obj(rlink);

	D_ASSERTF(ksize == sizeof(uuid_t), "%u\n", ksize);
	return uuid_compare(hdl->sch_uuid, key) == 0;
}

static void
cont_hdl_rec_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	cont_hdl_obj(rlink)->sch_ref++;
}

static bool
cont_hdl_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ds_cont_hdl *hdl = cont_hdl_obj(rlink);

	hdl->sch_ref--;
	return hdl->sch_ref == 0;
}

static void
cont_hdl_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ds_cont_hdl     *hdl = cont_hdl_obj(rlink);
	struct dsm_tls	       *tls = dsm_tls_get();

	D_ASSERT(d_hash_rec_unlinked(&hdl->sch_entry));
	D_ASSERTF(hdl->sch_ref == 0, "%d\n", hdl->sch_ref);
	D_DEBUG(DF_DSMS, "freeing "DF_UUID"\n", DP_UUID(hdl->sch_uuid));
	/* The sch_cont is NULL for global rebuild cont handle */
	if (hdl->sch_cont != NULL) {
		D_DEBUG(DF_DSMS, DF_CONT": freeing\n",
			DP_CONT(hdl->sch_cont->sc_pool->spc_uuid,
			hdl->sch_cont->sc_uuid));
		cont_child_put(tls->dt_cont_cache, hdl->sch_cont);
	}
	D_FREE(hdl);
}

static d_hash_table_ops_t cont_hdl_hash_ops = {
	.hop_key_cmp	= cont_hdl_key_cmp,
	.hop_rec_addref	= cont_hdl_rec_addref,
	.hop_rec_decref	= cont_hdl_rec_decref,
	.hop_rec_free	= cont_hdl_rec_free
};

int
ds_cont_hdl_hash_create(struct d_hash_table *hash)
{
	return d_hash_table_create_inplace(D_HASH_FT_NOLOCK /* feats */,
					   8 /* bits */,
					   NULL /* priv */,
					   &cont_hdl_hash_ops, hash);
}

void
ds_cont_hdl_hash_destroy(struct d_hash_table *hash)
{
	d_hash_table_destroy_inplace(hash, true /* force */);
}

static int
cont_hdl_add(struct d_hash_table *hash, struct ds_cont_hdl *hdl)
{
	return d_hash_rec_insert(hash, hdl->sch_uuid, sizeof(uuid_t),
				 &hdl->sch_entry, true /* exclusive */);
}

static void
cont_hdl_delete(struct d_hash_table *hash, struct ds_cont_hdl *hdl)
{
	bool deleted;

	deleted = d_hash_rec_delete(hash, hdl->sch_uuid, sizeof(uuid_t));
	D_ASSERT(deleted == true);
}

static struct ds_cont_hdl *
cont_hdl_lookup_internal(struct d_hash_table *hash, const uuid_t uuid)
{
	d_list_t *rlink;

	rlink = d_hash_rec_find(hash, uuid, sizeof(uuid_t));
	if (rlink == NULL)
		return NULL;

	return cont_hdl_obj(rlink);
}

/**
 * lookup target container handle by container handle uuid (usually from req)
 *
 * \param uuid [IN]		container handle uuid
 *
 * \return			target container handle if succeeds.
 * \return			NULL if it does not find.
 */
struct ds_cont_hdl *
ds_cont_hdl_lookup(const uuid_t uuid)
{
	struct d_hash_table *hash = &dsm_tls_get()->dt_cont_hdl_hash;

	return cont_hdl_lookup_internal(hash, uuid);
}

static void
cont_hdl_put_internal(struct d_hash_table *hash,
		      struct ds_cont_hdl *hdl)
{
	d_hash_rec_decref(hash, &hdl->sch_entry);
}

static void
cont_hdl_get_internal(struct d_hash_table *hash,
		      struct ds_cont_hdl *hdl)
{
	d_hash_rec_addref(hash, &hdl->sch_entry);
}

/**
 * Put target container handle.
 *
 * \param hdl [IN]		container handle to be put.
 **/
void
ds_cont_hdl_put(struct ds_cont_hdl *hdl)
{
	struct d_hash_table *hash = &dsm_tls_get()->dt_cont_hdl_hash;

	cont_hdl_put_internal(hash, hdl);
}

/**
 * Get target container handle.
 *
 * \param hdl [IN]		container handle to be get.
 **/
void
ds_cont_hdl_get(struct ds_cont_hdl *hdl)
{
	struct d_hash_table *hash = &dsm_tls_get()->dt_cont_hdl_hash;

	cont_hdl_get_internal(hash, hdl);
}

/* #define CONT_DESTROY_SYNC_WAIT */
static void
cont_destroy_wait(struct ds_pool_child *child, uuid_t co_uuid)
{
#ifdef CONT_DESTROY_SYNC_WAIT
	struct dss_module_info	*dmi = dss_get_module_info();
	struct sched_req_attr	 attr;
	struct sched_request	*req;

	D_DEBUG(DF_DSMS, DF_CONT": wait container destroy\n",
		DP_CONT(child->spc_uuid, co_uuid));

	D_ASSERT(child != NULL);
	sched_req_attr_init(&attr, SCHED_REQ_FETCH, &child->spc_uuid);
	req = sched_req_get(&attr, ABT_THREAD_NULL);
	if (req == NULL) {
		D_CRIT(DF_UUID"[%d]: Failed to get sched req\n",
		       DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);
		return;
	}

	while (!dss_xstream_exiting(dmi->dmi_xstream)) {
		if (vos_gc_pool_idle(child->spc_hdl))
			break;
		sched_req_sleep(req, 500);
	}
	sched_req_put(req);

	D_DEBUG(DF_DSMS, DF_CONT": container destroy done\n",
		DP_CONT(child->spc_uuid, co_uuid));
#endif
}

/*
 * Called via dss_collective() to destroy the ds_cont object as well as the vos
 * container.
 */
static int
cont_child_destroy_one(void *vin)
{
	struct dsm_tls		       *tls = dsm_tls_get();
	struct cont_tgt_destroy_in     *in = vin;
	struct ds_pool_child	       *pool;
	int				rc, retry_cnt = 0;

	pool = ds_pool_child_lookup(in->tdi_pool_uuid);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	while (1) {
		struct ds_cont_child *cont;

		rc = cont_child_lookup(tls->dt_cont_cache, in->tdi_uuid, NULL,
				       &cont);
		if (rc == -DER_NONEXIST)
			break;
		if (rc != 0)
			D_GOTO(out_pool, rc);

		if (cont->sc_open > 0) {
			D_ERROR(DF_CONT": Container is still in open(%d)\n",
				DP_CONT(cont->sc_pool->spc_uuid,
					cont->sc_uuid), cont->sc_open);
			cont_child_put(tls->dt_cont_cache, cont);
			rc = -DER_BUSY;
			goto out_pool;
		}

		cont_child_stop(cont);

		ABT_mutex_lock(cont->sc_mutex);
		if (cont->sc_dtx_resyncing)
			ABT_cond_wait(cont->sc_dtx_resync_cond, cont->sc_mutex);
		ABT_mutex_unlock(cont->sc_mutex);
		/*
		 * If this is the last user, ds_cont_child will be removed from
		 * hash & freed on put.
		 */
		cont_child_put(tls->dt_cont_cache, cont);

		retry_cnt++;
		if (retry_cnt > 1) {
			D_ERROR("container is still in-use\n");
			D_GOTO(out_pool, rc = -DER_BUSY);
		} /* else: resync should have completed, try again */
	}

	D_DEBUG(DF_DSMS, DF_CONT": destroying vos container\n",
		DP_CONT(pool->spc_uuid, in->tdi_uuid));

	rc = vos_cont_destroy(pool->spc_hdl, in->tdi_uuid);
	if (rc == -DER_NONEXIST) {
		/** VOS container creation is effectively delayed until
		 * container open time, so it might legitimately not exist if
		 * the container has never been opened */
		rc = 0;
	} else if (rc) {
		D_ERROR(DF_CONT": destroy vos container failed "DF_RC"\n",
			DP_CONT(pool->spc_uuid, in->tdi_uuid), DP_RC(rc));
	} else {
		/* Wakeup GC ULT */
		sched_req_wakeup(pool->spc_gc_req);
		cont_destroy_wait(pool, in->tdi_uuid);
	}

out_pool:
	ds_pool_child_put(pool);
out:
	return rc;
}

int
ds_cont_tgt_destroy(uuid_t pool_uuid, uuid_t cont_uuid)
{
	struct cont_tgt_destroy_in in;
	int rc;

	uuid_copy(in.tdi_pool_uuid, pool_uuid);
	uuid_copy(in.tdi_uuid, cont_uuid);

	rc = dss_thread_collective(cont_child_destroy_one, &in, 0, DSS_ULT_IO);
	return rc;
}

void
ds_cont_tgt_destroy_handler(crt_rpc_t *rpc)
{
	struct cont_tgt_destroy_in     *in = crt_req_get(rpc);
	struct cont_tgt_destroy_out    *out = crt_reply_get(rpc);
	int				rc = 0;

	D_DEBUG(DF_DSMS, DF_CONT": handling rpc %p\n",
		DP_CONT(in->tdi_pool_uuid, in->tdi_uuid), rpc);

	rc = ds_cont_tgt_destroy(in->tdi_pool_uuid, in->tdi_uuid);
	out->tdo_rc = (rc == 0 ? 0 : 1);
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d "DF_RC"\n",
		DP_CONT(in->tdi_pool_uuid, in->tdi_uuid), rpc, out->tdo_rc,
		DP_RC(rc));
	crt_reply_send(rpc);
}

int
ds_cont_tgt_destroy_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct cont_tgt_destroy_out    *out_source = crt_reply_get(source);
	struct cont_tgt_destroy_out    *out_result = crt_reply_get(result);

	out_result->tdo_rc += out_source->tdo_rc;
	return 0;
}

/**
 * lookup ds_cont_child by pool/container uuid.
 **/
int
ds_cont_child_lookup(uuid_t pool_uuid, uuid_t cont_uuid,
		     struct ds_cont_child **ds_cont)
{
	struct dsm_tls		*tls = dsm_tls_get();

	return cont_child_lookup(tls->dt_cont_cache, cont_uuid, pool_uuid,
				 ds_cont);
}

/**
 * ds_cont_child create and start. If the container is created,
 * it will return 1, otherwise return 0 or error code.
 **/
static int
cont_child_create_start(uuid_t pool_uuid, uuid_t cont_uuid,
			struct ds_cont_child **cont_out)
{
	struct ds_pool_child	*pool_child;
	int rc;

	pool_child = ds_pool_child_lookup(pool_uuid);
	if (pool_child == NULL) {
		D_ERROR(DF_CONT" : failed to find pool child\n",
			DP_CONT(pool_uuid, cont_uuid));
		return -DER_NO_HDL;
	}

	rc = cont_child_start(pool_child, cont_uuid, cont_out);
	if (rc != -DER_NONEXIST) {
		ds_pool_child_put(pool_child);
		return rc;
	}

	D_DEBUG(DF_DSMS, DF_CONT": creating new vos container\n",
		DP_CONT(pool_uuid, cont_uuid));

	rc = vos_cont_create(pool_child->spc_hdl, cont_uuid);
	if (!rc) {
		rc = cont_child_start(pool_child, cont_uuid, cont_out);
		if (rc != 0)
			vos_cont_destroy(pool_child->spc_hdl, cont_uuid);
	}

	ds_pool_child_put(pool_child);
	return rc == 0 ? 1 : rc;
}

int
ds_cont_local_close(uuid_t cont_hdl_uuid)
{
	struct dsm_tls		*tls = dsm_tls_get();
	struct ds_cont_hdl	*hdl;

	hdl = cont_hdl_lookup_internal(&tls->dt_cont_hdl_hash, cont_hdl_uuid);
	if (hdl == NULL)
		return 0;

	cont_hdl_delete(&tls->dt_cont_hdl_hash, hdl);

	ds_cont_hdl_put(hdl);
	return 0;
}

void
ds_cont_child_get(struct ds_cont_child *cont)
{
	daos_lru_ref_add(&cont->sc_list);
}

void
ds_cont_child_put(struct ds_cont_child *cont)
{
	struct dsm_tls	*tls = dsm_tls_get();

	cont_child_put(tls->dt_cont_cache, cont);
}

struct ds_dtx_resync_args {
	struct ds_pool_child	*pool;
	uuid_t			 co_uuid;
};

static void
ds_dtx_resync(void *arg)
{
	struct ds_dtx_resync_args	*ddra = arg;
	int				 rc;

	rc = dtx_resync(ddra->pool->spc_hdl, ddra->pool->spc_uuid,
			ddra->co_uuid, ddra->pool->spc_map_version,
			false, true);
	if (rc != 0)
		D_WARN("Fail to resync some DTX(s) for the pool/cont "
		       DF_UUID"/"DF_UUID" that may affect subsequent "
		       "operations: rc = "DF_RC".\n",
		       DP_UUID(ddra->pool->spc_uuid),
		       DP_UUID(ddra->co_uuid), DP_RC(rc));

	ds_pool_child_put(ddra->pool);
	D_FREE(ddra);
}

int
ds_cont_child_open_create(uuid_t pool_uuid, uuid_t cont_uuid,
			  struct ds_cont_child **cont)
{
	int rc;

	rc = cont_child_create_start(pool_uuid, cont_uuid, cont);
	if (rc == 1)
		rc = 0;

	return rc;
}

int
ds_cont_local_open(uuid_t pool_uuid, uuid_t cont_hdl_uuid, uuid_t cont_uuid,
		   uint64_t flags, uint64_t sec_capas,
		   struct ds_cont_hdl **cont_hdl)
{
	struct dsm_tls		*tls = dsm_tls_get();
	struct ds_cont_hdl	*hdl;
	daos_handle_t		poh = DAOS_HDL_INVAL;
	int			rc = 0;

	hdl = cont_hdl_lookup_internal(&tls->dt_cont_hdl_hash, cont_hdl_uuid);
	if (hdl != NULL) {
		if (flags != 0) {
			if (hdl->sch_flags != flags) {
				D_ERROR(DF_CONT": conflicting container : hdl="
					DF_UUID" capas="DF_U64"\n",
					DP_CONT(pool_uuid, cont_uuid),
					DP_UUID(cont_hdl_uuid), flags);
				rc = -DER_EXIST;
			} else {
				D_DEBUG(DF_DSMS, DF_CONT": found compatible"
					" container handle: hdl="DF_UUID
					" capas="DF_U64"\n",
				      DP_CONT(pool_uuid, cont_uuid),
				      DP_UUID(cont_hdl_uuid), hdl->sch_flags);
			}
		}

		if (cont_hdl != NULL && rc == 0)
			*cont_hdl = hdl;
		else
			cont_hdl_put_internal(&tls->dt_cont_hdl_hash, hdl);
		return rc;
	}

	D_ALLOC_PTR(hdl);
	if (hdl == NULL)
		return -DER_NOMEM;

	D_ASSERT(pool_uuid != NULL);

	/* cont_uuid is NULL when open rebuild global cont handle */
	if (cont_uuid != NULL && !uuid_is_null(cont_uuid)) {
		struct ds_cont_child *cont;

		rc = cont_child_create_start(pool_uuid, cont_uuid, &cont);
		if (rc < 0)
			D_GOTO(err_hdl, rc);

		hdl->sch_cont = cont;
		if (rc == 1) {
			poh = hdl->sch_cont->sc_pool->spc_hdl;
			rc = 0;
		}
	}

	uuid_copy(hdl->sch_uuid, cont_hdl_uuid);
	hdl->sch_flags = flags;
	hdl->sch_sec_capas = sec_capas;

	rc = cont_hdl_add(&tls->dt_cont_hdl_hash, hdl);
	if (rc != 0)
		D_GOTO(err_cont, rc);

	/* It is possible to sync DTX status before destroy the CoS for close
	 * the container. But that may be not enough. Because the server may
	 * crashed before closing the container. Then the DTXs' status in the
	 * CoS cache will be lost. So we need to re-sync the DTXs status when
	 * open the container for the first time (not for cached open handle).
	 *
	 * On the other hand, even if we skip the DTX sync before destroy the
	 * CoS cache when close the container, resync DTX when open container
	 * is enough to guarantee related data records' visibility. That also
	 * simplify the DTX logic.
	 *
	 * XXX: The logic is related with DAOS server re-intergration, but we
	 *	do not support that currently. Then resync DTX when container
	 *	open will be used as temporary solution for DTX related logic.
	 *
	 * We do not trigger dtx_resync() when start the server. Because:
	 * 1. Currently, we do not support server re-integrate after restart.
	 * 2. A server may has multiple pools and each pool may has multiple
	 *    containers. These containers may not related with one another.
	 *    Make all the DTXs resync together during the server start will
	 *    cause the DTX resync time to be much longer than resync against
	 *    single container just when use (open) it. On the other hand, if
	 *    some servers are ready for dtx_resync, but others may not start
	 *    yet, then the ready ones may have to wait or failed dtx_resync.
	 *    Both cases are not expected.
	 */
	if (cont_uuid != NULL && !uuid_is_null(cont_uuid)) {
		struct ds_dtx_resync_args	*ddra = NULL;

		/*
		 * NB: When cont_uuid == NULL, it's not a real container open
		 *     but for creating rebuild global container handle.
		 */
		hdl->sch_cont->sc_open++;

		if (hdl->sch_cont->sc_open > 1)
			goto opened;

		rc = cont_start_dtx_reindex_ult(hdl->sch_cont);
		if (rc != 0) {
			hdl->sch_cont->sc_open--;
			goto err_cont;
		}

		rc = dtx_batched_commit_register(hdl->sch_cont);
		if (rc != 0) {
			D_ERROR("Failed to register the container "DF_UUID
				" to the DTX batched commit list: "
				"rc = "DF_RC"\n", DP_UUID(cont_uuid),
				DP_RC(rc));
			hdl->sch_cont->sc_open--;
			D_GOTO(err_cont, rc);
		}

		D_ALLOC_PTR(ddra);
		if (ddra == NULL)
			D_GOTO(err_register, rc = -DER_NOMEM);

		ddra->pool = ds_pool_child_get(hdl->sch_cont->sc_pool);
		uuid_copy(ddra->co_uuid, cont_uuid);
		rc = dss_ult_create(ds_dtx_resync, ddra, DSS_ULT_DTX_RESYNC,
				    DSS_TGT_SELF, 0, NULL);
		if (rc != 0) {
			ds_pool_child_put(hdl->sch_cont->sc_pool);
			D_FREE(ddra);
			D_GOTO(err_register, rc);
		}

		D_ASSERT(hdl->sch_cont != NULL);
		D_ASSERT(hdl->sch_cont->sc_pool != NULL);
		rc = ds_cont_csummer_init(hdl->sch_cont);

		if (rc != 0)
			D_GOTO(err_register, rc);
	}
opened:
	if (cont_hdl != NULL) {
		cont_hdl_get_internal(&tls->dt_cont_hdl_hash, hdl);
		*cont_hdl = hdl;
	}

	return 0;

err_register:
	D_ASSERT(hdl->sch_cont->sc_open > 0);
	hdl->sch_cont->sc_open--;
	if (hdl->sch_cont->sc_open == 0)
		dtx_batched_commit_deregister(hdl->sch_cont);
err_cont:
	if (hdl->sch_cont) {
		cont_stop_dtx_reindex_ult(hdl->sch_cont);
		cont_child_put(tls->dt_cont_cache, hdl->sch_cont);
	}

	if (!daos_handle_is_inval(poh)) {
		D_DEBUG(DF_DSMS, DF_CONT": destroying new vos container\n",
			DP_CONT(pool_uuid, cont_uuid));
		D_ASSERT(hdl->sch_cont != NULL);
		cont_child_stop(hdl->sch_cont);
		vos_cont_destroy(poh, cont_uuid);
	}
err_hdl:
	D_FREE(hdl);
	return rc;
}

struct cont_tgt_open_arg {
	uuid_t		pool_uuid;
	uuid_t		cont_uuid;
	uuid_t		cont_hdl_uuid;
	uint64_t	flags;
	uint64_t	sec_capas;
};

/*
 * Called via dss_collective() to establish the ds_cont_hdl object as well as
 * the ds_cont object.
 */
static int
cont_open_one(void *vin)
{
	struct cont_tgt_open_arg	*arg = vin;

	return ds_cont_local_open(arg->pool_uuid, arg->cont_hdl_uuid,
				  arg->cont_uuid, arg->flags, arg->sec_capas,
				  NULL);
}

int
ds_cont_tgt_open(uuid_t pool_uuid, uuid_t cont_hdl_uuid,
		 uuid_t cont_uuid, uint64_t flags, uint64_t sec_capas)
{
	struct cont_tgt_open_arg arg = { 0 };
	struct dss_coll_ops	coll_ops = { 0 };
	struct dss_coll_args	coll_args = { 0 };
	int			rc;

	uuid_copy(arg.pool_uuid, pool_uuid);
	uuid_copy(arg.cont_hdl_uuid, cont_hdl_uuid);
	if (cont_uuid)
		uuid_copy(arg.cont_uuid, cont_uuid);
	arg.flags = flags;
	arg.sec_capas = sec_capas;

	D_DEBUG(DB_TRACE, "open pool/cont/hdl "DF_UUID"/"DF_UUID"/"DF_UUID"\n",
		DP_UUID(pool_uuid), DP_UUID(cont_uuid), DP_UUID(cont_hdl_uuid));

	/* collective operations */
	coll_ops.co_func = cont_open_one;
	coll_args.ca_func_args	= &arg;

	/* setting aggregator args */
	rc = ds_pool_get_failed_tgt_idx(pool_uuid, &coll_args.ca_exclude_tgts,
					&coll_args.ca_exclude_tgts_cnt);
	if (rc) {
		D_ERROR(DF_UUID "failed to get index : rc "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		return rc;
	}

	rc = dss_thread_collective_reduce(&coll_ops, &coll_args, 0,
					  DSS_ULT_IO);
	if (coll_args.ca_exclude_tgts)
		D_FREE(coll_args.ca_exclude_tgts);

	if (rc != 0) {
		/* Once it exclude the target from the pool, since the target
		 * might still in the cart group, so IV cont open might still
		 * come to this target, especially if cont open/close will be
		 * done by IV asynchronously, so this cont_open_one might return
		 * -DER_NO_HDL if it can not find pool handle. (DAOS-3185)
		 */
		D_ERROR("open "DF_UUID"/"DF_UUID"/"DF_UUID":"DF_RC"\n",
			DP_UUID(pool_uuid), DP_UUID(cont_uuid),
			DP_UUID(cont_hdl_uuid), DP_RC(rc));
		return rc;
	}

	return rc;
}

/* Close a single per-thread open container handle */
static int
cont_close_hdl(uuid_t cont_hdl_uuid)
{
	struct dsm_tls		*tls = dsm_tls_get();
	struct ds_cont_hdl	*hdl;
	struct ds_cont_child	*cont_child;

	hdl = cont_hdl_lookup_internal(&tls->dt_cont_hdl_hash, cont_hdl_uuid);

	if (hdl == NULL) {
		D_DEBUG(DF_DSMS, DF_CONT": already closed: hdl="DF_UUID"\n",
			DP_CONT(NULL, NULL), DP_UUID(cont_hdl_uuid));
		return 0;
	}

	/* Remove the handle from hash first, following steps may yield */
	ds_cont_local_close(cont_hdl_uuid);

	cont_child = hdl->sch_cont;
	if (cont_child != NULL) {
		D_DEBUG(DF_DSMS, DF_CONT": closing (%d): hdl="DF_UUID"\n",
			DP_CONT(cont_child->sc_pool->spc_uuid,
				cont_child->sc_uuid),
			cont_child->sc_open, DP_UUID(cont_hdl_uuid));

		D_ASSERT(cont_child->sc_open > 0);
		cont_child->sc_open--;
		if (cont_child->sc_open == 0)
			dtx_batched_commit_deregister(cont_child);
	}

	cont_hdl_put_internal(&tls->dt_cont_hdl_hash, hdl);
	return 0;
}

static int
cont_close_all_cb(d_list_t *rlink, void *arg)
{
	uuid_t *cont_uuid = arg;
	struct ds_cont_hdl *hdl = cont_hdl_obj(rlink);
	int rc;

	if (hdl->sch_cont == NULL)
		return DER_SUCCESS;

	if (uuid_compare(*cont_uuid, hdl->sch_cont->sc_uuid) == 0) {
		rc = cont_close_hdl(hdl->sch_uuid);
		if (rc != 0) {
			D_ERROR("cont_close_hdl failed: rc="DF_RC, DP_RC(rc));
			return rc;
		}
	}

	return DER_SUCCESS;
}

/* Called via dss_collective() to close all container handles for this thread */
static int
cont_close_all(void *vin)
{
	struct dsm_tls *tls = dsm_tls_get();
	uuid_t *cont_uuid = vin;
	int rc;

	rc = d_hash_table_traverse(&tls->dt_cont_hdl_hash, cont_close_all_cb,
				   cont_uuid);
	if (rc != 0) {
		D_ERROR("d_hash_table_traverse failed: rc="DF_RC, DP_RC(rc));
		return rc;
	}

	return DER_SUCCESS;
}

int
ds_cont_tgt_force_close(uuid_t cont_uuid)
{
	int rc;

	D_DEBUG(DF_DSMS, DF_CONT": Force closing all handles for container "
		DF_UUID"\n", DP_CONT(NULL, NULL), cont_uuid);

	rc = dss_thread_collective(cont_close_all, &cont_uuid, 0, DSS_ULT_IO);
	if (rc != 0)
		D_ERROR("dss_thread_collective failed: rc="DF_RC, DP_RC(rc));
	return rc;
}

struct coll_close_arg {
	uuid_t	uuid;
};

/* Called via dss_collective() to close the containers belong to this thread. */
static int
cont_close_one_hdl(void *vin)
{
	struct coll_close_arg *arg = vin;

	return cont_close_hdl(arg->uuid);
}

int
ds_cont_tgt_close(uuid_t hdl_uuid)
{
	struct coll_close_arg arg;

	uuid_copy(arg.uuid, hdl_uuid);
	return dss_thread_collective(cont_close_one_hdl, &arg, 0, DSS_ULT_IO);
}

struct xstream_cont_query {
	struct cont_tgt_query_in	*xcq_rpc_in;
	daos_epoch_t			 xcq_hae;
};

static int
cont_query_one(void *vin)
{
	struct dss_coll_stream_args	*reduce	   = vin;
	struct dss_stream_arg_type	*streams   = reduce->csa_streams;
	struct dss_module_info		*info	   = dss_get_module_info();
	int				tid	   = info->dmi_tgt_id;
	struct xstream_cont_query	*pack_args = streams[tid].st_arg;
	struct cont_tgt_query_in	*in	   = pack_args->xcq_rpc_in;
	struct ds_pool_hdl		*pool_hdl;
	struct ds_pool_child		*pool_child;
	daos_handle_t			vos_chdl;
	vos_cont_info_t			vos_cinfo;
	char				*opstr;
	int				rc;

	info = dss_get_module_info();
	pool_hdl = ds_pool_hdl_lookup(in->tqi_pool_uuid);
	if (pool_hdl == NULL)
		return -DER_NO_HDL;

	pool_child = ds_pool_child_lookup(pool_hdl->sph_pool->sp_uuid);
	if (pool_child == NULL)
		D_GOTO(ds_pool_hdl, rc = -DER_NO_HDL);

	opstr = "Opening VOS container open handle\n";
	rc = vos_cont_open(pool_child->spc_hdl, in->tqi_cont_uuid,
			   &vos_chdl);
	if (rc != 0) {
		D_ERROR(DF_CONT": Failed %s: "DF_RC"",
			DP_CONT(in->tqi_pool_uuid, in->tqi_cont_uuid), opstr,
			DP_RC(rc));
		D_GOTO(ds_child, rc);
	}

	opstr = "Querying VOS container open handle\n";
	rc = vos_cont_query(vos_chdl, &vos_cinfo);
	if (rc != 0) {
		D_ERROR(DF_CONT": Failed :%s: "DF_RC"",
			DP_CONT(in->tqi_pool_uuid, in->tqi_cont_uuid), opstr,
			DP_RC(rc));
		D_GOTO(out, rc);
	}
	pack_args->xcq_hae = vos_cinfo.ci_hae;

out:
	vos_cont_close(vos_chdl);
ds_child:
	ds_pool_child_put(pool_child);
ds_pool_hdl:
	ds_pool_hdl_put(pool_hdl);
	return rc;
}

static void
ds_cont_query_coll_reduce(void *a_args, void *s_args)
{
	struct	xstream_cont_query	 *aggregator = a_args;
	struct  xstream_cont_query	 *stream     = s_args;
	daos_epoch_t			 *min_epoch;

	min_epoch = &aggregator->xcq_hae;
	*min_epoch = MIN(*min_epoch, stream->xcq_hae);
}

static int
ds_cont_query_stream_alloc(struct dss_stream_arg_type *args,
			   void *a_arg)
{
	struct xstream_cont_query	*rarg = a_arg;

	D_ALLOC(args->st_arg, sizeof(struct xstream_cont_query));
	if (args->st_arg == NULL)
		return -DER_NOMEM;
	memcpy(args->st_arg, rarg, sizeof(struct xstream_cont_query));

	return 0;
}

static void
ds_cont_query_stream_free(struct dss_stream_arg_type *c_args)
{
	D_ASSERT(c_args->st_arg != NULL);
	D_FREE(c_args->st_arg);
}

void
ds_cont_tgt_query_handler(crt_rpc_t *rpc)
{
	int				rc;
	struct cont_tgt_query_in	*in  = crt_req_get(rpc);
	struct cont_tgt_query_out	*out = crt_reply_get(rpc);
	struct dss_coll_ops		coll_ops;
	struct dss_coll_args		coll_args = { 0 };
	struct xstream_cont_query	pack_args;

	out->tqo_hae			= DAOS_EPOCH_MAX;

	/** on all available streams */

	coll_ops.co_func		= cont_query_one;
	coll_ops.co_reduce		= ds_cont_query_coll_reduce;
	coll_ops.co_reduce_arg_alloc	= ds_cont_query_stream_alloc;
	coll_ops.co_reduce_arg_free	= ds_cont_query_stream_free;

	/** packing arguments for aggregator args */
	pack_args.xcq_rpc_in		= in;
	pack_args.xcq_hae		= DAOS_EPOCH_MAX;

	/** setting aggregator args */
	coll_args.ca_aggregator		= &pack_args;
	coll_args.ca_func_args		= &coll_args.ca_stream_args;


	rc = dss_task_collective_reduce(&coll_ops, &coll_args, 0, DSS_ULT_IO);

	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	out->tqo_hae	= MIN(out->tqo_hae, pack_args.xcq_hae);
	out->tqo_rc	= (rc == 0 ? 0 : 1);

	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d "DF_RC"\n",
		DP_CONT(NULL, NULL), rpc, out->tqo_rc, DP_RC(rc));
	crt_reply_send(rpc);
}

int
ds_cont_tgt_query_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct cont_tgt_query_out	*out_source = crt_reply_get(source);
	struct cont_tgt_query_out	*out_result = crt_reply_get(result);

	out_result->tqo_hae = MIN(out_result->tqo_hae, out_source->tqo_hae);
	out_result->tqo_rc += out_source->tqo_rc;
	return 0;
}

struct cont_snap_args {
	uuid_t		 pool_uuid;
	uuid_t		 cont_uuid;
	int		 snap_count;
	uint64_t	*snapshots;
};

static int
cont_snap_update_one(void *vin)
{
	struct cont_snap_args	*args = vin;
	struct ds_cont_child	*cont;
	int			 rc;

	rc = ds_cont_child_lookup(args->pool_uuid, args->cont_uuid, &cont);
	if (rc != 0)
		return rc;
	if (args->snap_count == 0) {
		if (cont->sc_snapshots != NULL) {
			D_ASSERT(cont->sc_snapshots_nr > 0);
			D_FREE(cont->sc_snapshots);
			cont->sc_snapshots = NULL;
		}
	} else {
		void	*buf;
		size_t	 bufsize;

		bufsize = args->snap_count * sizeof(*args->snapshots);
		D_REALLOC(buf, cont->sc_snapshots, bufsize);
		if (buf == NULL) {
			rc = -DER_NOMEM;
			goto out_cont;
		}
		memcpy(buf, args->snapshots, bufsize);
		cont->sc_snapshots = buf;
	}

	/* Snapshot deleted, reset aggregation lower bound epoch */
	if (cont->sc_snapshots_nr > args->snap_count) {
		cont->sc_snapshot_delete_hlc = crt_hlc_get();
		D_DEBUG(DB_EPC, DF_CONT": Reset aggregation lower bound\n",
			DP_CONT(args->pool_uuid, args->cont_uuid));
	}
	cont->sc_snapshots_nr = args->snap_count;
	cont->sc_aggregation_max = DAOS_EPOCH_MAX;
out_cont:
	ds_cont_child_put(cont);
	return rc;
}

int
ds_cont_tgt_snapshots_update(uuid_t pool_uuid, uuid_t cont_uuid,
			     uint64_t *snapshots, int snap_count)
{
	struct cont_snap_args	 args;

	uuid_copy(args.pool_uuid, pool_uuid);
	uuid_copy(args.cont_uuid, cont_uuid);
	args.snap_count = snap_count;
	args.snapshots = snapshots;
	D_DEBUG(DB_TRACE, DF_UUID": refreshing snapshots %d\n",
		DP_UUID(cont_uuid), snap_count);
	return dss_thread_collective(cont_snap_update_one, &args, 0,
				     DSS_ULT_IO);
}

void
cont_snapshots_refresh_ult(void *data)
{
	struct cont_snap_args	*args = data;
	struct ds_pool		*pool;
	int			 rc;

	pool = ds_pool_lookup(args->pool_uuid);
	if (pool == NULL) {
		rc = -DER_NO_HDL;
		goto out;
	}
	rc = cont_iv_snapshots_refresh(pool->sp_iv_ns, args->cont_uuid);
	ds_pool_put(pool);
out:
	if (rc != 0)
		D_WARN(DF_UUID": failed to refresh snapshots IV: rc "DF_RC";"
			" Aggregation may not work correctly\n",
			DP_UUID(args->cont_uuid), DP_RC(rc));
	D_FREE(args);
}

int
ds_cont_tgt_snapshots_refresh(uuid_t pool_uuid, uuid_t cont_uuid)
{
	struct cont_snap_args	*args;
	int			 rc;

	D_ALLOC_PTR(args);
	if (args == NULL)
		return -DER_NOMEM;
	uuid_copy(args->pool_uuid, pool_uuid);
	uuid_copy(args->cont_uuid, cont_uuid);
	rc = dss_ult_create(cont_snapshots_refresh_ult, args,
			    DSS_ULT_POOL_SRV, 0, 0, NULL);
	if (rc != 0)
		D_FREE(args);
	return rc;
}

static int
cont_snap_notify_one(void *vin)
{
	struct cont_snap_args	*args = vin;
	struct ds_cont_child	*cont;
	int			 rc;

	rc = ds_cont_child_lookup(args->pool_uuid, args->cont_uuid, &cont);
	if (rc != 0)
		return rc;
	cont->sc_aggregation_max = crt_hlc_get();
	ds_cont_child_put(cont);
	return rc;
}

void
ds_cont_tgt_snapshot_notify_handler(crt_rpc_t *rpc)
{
	struct cont_tgt_snapshot_notify_in	*in	= crt_req_get(rpc);
	struct cont_tgt_snapshot_notify_out	*out	= crt_reply_get(rpc);
	struct cont_snap_args			 args	= { 0 };

	D_DEBUG(DB_EPC, DF_CONT": handling rpc %p\n",
		DP_CONT(in->tsi_pool_uuid, in->tsi_cont_uuid), rpc);

	uuid_copy(args.pool_uuid, in->tsi_pool_uuid);
	uuid_copy(args.cont_uuid, in->tsi_cont_uuid);
	out->tso_rc = dss_thread_collective(cont_snap_notify_one, &args, 0,
					    DSS_ULT_IO);
	if (out->tso_rc != 0)
		D_ERROR(DF_CONT": Snapshot notify failed: "DF_RC"\n",
			DP_CONT(in->tsi_pool_uuid, in->tsi_cont_uuid),
			DP_RC(out->tso_rc));
	crt_reply_send(rpc);
}

int
ds_cont_tgt_snapshot_notify_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				       void *priv)
{
	struct cont_tgt_snapshot_notify_out      *out_source;
	struct cont_tgt_snapshot_notify_out      *out_result;

	out_source = crt_reply_get(source);
	out_result = crt_reply_get(result);
	out_result->tso_rc += out_source->tso_rc;
	return 0;
}

static int
cont_epoch_aggregate_one(void *vin)
{
	return 0;
}

void
ds_cont_tgt_epoch_aggregate_handler(crt_rpc_t *rpc)
{
	struct cont_tgt_epoch_aggregate_in	*in  = crt_req_get(rpc);
	struct cont_tgt_epoch_aggregate_out	*out = crt_reply_get(rpc);
	int					 rc;

	D_DEBUG(DF_DSMS, DF_CONT": handling rpc %p: epr (%p) [#"DF_U64"]\n",
		DP_CONT(in->tai_pool_uuid, in->tai_cont_uuid), rpc,
		in->tai_epr_list.ca_arrays, in->tai_epr_list.ca_count);
	/* Reply without waiting for the aggregation ULTs to finish. */
	out->tao_rc = 0;
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: "DF_RC"\n",
		DP_CONT(in->tai_pool_uuid, in->tai_cont_uuid), rpc,
		DP_RC(out->tao_rc));
	crt_reply_send(rpc);
	if (out->tao_rc != 0)
		return;

	rc = dss_thread_collective(cont_epoch_aggregate_one, NULL, 0,
				   DSS_ULT_IO);
	if (rc != 0)
		D_ERROR(DF_CONT": Aggregation failed: "DF_RC"\n",
			DP_CONT(in->tai_pool_uuid, in->tai_cont_uuid),
				DP_RC(rc));
}

int
ds_cont_tgt_epoch_aggregate_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				       void *priv)
{
	struct cont_tgt_epoch_aggregate_out      *out_source;
	struct cont_tgt_epoch_aggregate_out      *out_result;

	out_source = crt_reply_get(source);
	out_result = crt_reply_get(result);
	out_result->tao_rc += out_source->tao_rc;
	return 0;
}

/* iterate all of objects or uncommitted DTXs of the container. */
int
ds_cont_iter(daos_handle_t ph, uuid_t co_uuid, cont_iter_cb_t callback,
	     void *arg, uint32_t type)
{
	vos_iter_param_t param;
	daos_handle_t	 iter_h;
	daos_handle_t	 coh;
	int		 rc;

	rc = vos_cont_open(ph, co_uuid, &coh);
	if (rc != 0) {
		D_ERROR("Open container "DF_UUID" failed: rc = "DF_RC"\n",
			DP_UUID(co_uuid), DP_RC(rc));
		return rc;
	}

	memset(&param, 0, sizeof(param));
	param.ip_hdl = coh;
	param.ip_epr.epr_lo = 0;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	param.ip_flags = VOS_IT_FOR_REBUILD;

	rc = vos_iter_prepare(type, &param, &iter_h, NULL);
	if (rc != 0) {
		D_ERROR("prepare obj iterator failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(close, rc);
	}

	rc = vos_iter_probe(iter_h, NULL);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			rc = 0;
		else
			D_ERROR("set iterator cursor failed: "DF_RC"\n",
				DP_RC(rc));
		D_GOTO(iter_fini, rc);
	}

	while (1) {
		vos_iter_entry_t ent;

		rc = vos_iter_fetch(iter_h, &ent, NULL);
		if (rc != 0) {
			/* reach to the end of the container */
			if (rc == -DER_NONEXIST)
				rc = 0;
			else
				D_ERROR("Fetch obj failed: "DF_RC"\n",
					DP_RC(rc));
			break;
		}

		D_DEBUG(DB_ANY, "iter "DF_UOID"/"DF_UUID"\n",
			DP_UOID(ent.ie_oid), DP_UUID(co_uuid));

		rc = callback(co_uuid, &ent, arg);
		if (rc) {
			D_DEBUG(DB_ANY, "iter "DF_UOID" rc "DF_RC"\n",
				DP_UOID(ent.ie_oid), DP_RC(rc));
			if (rc > 0)
				rc = 0;
			break;
		}

		rc = vos_iter_next(iter_h);
		if (rc != 0) {
			/* reach to the end of the container */
			if (rc == -DER_NONEXIST)
				rc = 0;
			else
				D_ERROR("Fetch obj failed: "DF_RC"\n",
					DP_RC(rc));
			break;
		}
	}

iter_fini:
	vos_iter_finish(iter_h);
close:
	vos_cont_close(coh);
	return rc;
}

static int
cont_oid_alloc(struct ds_pool_hdl *pool_hdl, crt_rpc_t *rpc)
{
	struct cont_oid_alloc_in	*in = crt_req_get(rpc);
	struct cont_oid_alloc_out	*out;
	d_sg_list_t			sgl;
	d_iov_t				iov;
	struct oid_iv_range		rg;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": oid alloc: num_oids="DF_U64"\n",
		 DP_CONT(pool_hdl->sph_pool->sp_uuid, in->coai_op.ci_uuid),
		 in->num_oids);

	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	d_iov_set(&iov, &rg, sizeof(rg));

	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	rc = oid_iv_reserve(pool_hdl->sph_pool->sp_iv_ns,
			    in->coai_op.ci_pool_hdl, in->coai_op.ci_uuid,
			    in->coai_op.ci_hdl, in->num_oids, &sgl);
	if (rc)
		D_GOTO(out, rc);

	out->oid = rg.oid;

out:
	out->coao_op.co_rc = rc;
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: "DF_RC"\n",
		 DP_CONT(pool_hdl->sph_pool->sp_uuid, in->coai_op.ci_uuid),
		 rpc, DP_RC(rc));

	return rc;
}

void
ds_cont_oid_alloc_handler(crt_rpc_t *rpc)
{
	struct cont_op_in	*in = crt_req_get(rpc);
	struct cont_op_out	*out = crt_reply_get(rpc);
	struct ds_pool_hdl	*pool_hdl;
	crt_opcode_t		opc = opc_get(rpc->cr_opc);
	int			rc;

	pool_hdl = ds_pool_hdl_lookup(in->ci_pool_hdl);
	if (pool_hdl == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID" opc=%u\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->ci_uuid), rpc,
		DP_UUID(in->ci_hdl), opc);

	D_ASSERT(opc == CONT_OID_ALLOC);

	rc = cont_oid_alloc(pool_hdl, rpc);

	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: hdl="DF_UUID
		" opc=%u rc="DF_RC"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->ci_uuid), rpc,
		DP_UUID(in->ci_hdl), opc, DP_RC(rc));

	ds_pool_hdl_put(pool_hdl);
out:
	out->co_rc = rc;
	out->co_map_version = 0;
	crt_reply_send(rpc);
}
