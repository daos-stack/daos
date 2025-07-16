/**
 * (C) Copyright 2019-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dtx: DTX rpc service
 */
#define D_LOGFAC	DD_FAC(dtx)

#include <daos/rpc.h>
#include <daos/metrics.h>
#include <daos/btree_class.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/container.h>
#include <daos_srv/vos.h>
#include <daos_srv/dtx_srv.h>
#include <gurt/telemetry_consumer.h>
#include "dtx_internal.h"

static void *
dtx_tls_init(int tags, int xs_id, int tgt_id)
{
	struct dtx_tls  *tls;
	int              rc;

	D_ALLOC_PTR(tls);
	if (tls == NULL)
		return NULL;

	/** Skip sensor setup on system xstreams */
	if (tgt_id < 0)
		return tls;

	tls->dt_agg_gen = 1;
	rc = d_tm_add_metric(&tls->dt_committable, D_TM_STATS_GAUGE,
			     "total number of committable DTX entries", "entry",
			     "io/dtx/committable/tgt_%u", tgt_id);
	if (rc != DER_SUCCESS)
		D_WARN("Failed to create DTX committable metric: " DF_RC"\n",
		       DP_RC(rc));

	rc = d_tm_add_metric(&tls->dt_dtx_leader_total, D_TM_GAUGE,
			     "total number of leader dtx in cache", "entry",
			     "mem/dtx/dtx_leader_handle_%u/tgt_%u",
			     sizeof(struct dtx_leader_handle), tgt_id);
	if (rc != DER_SUCCESS)
		D_WARN("Failed to create DTX leader metric: " DF_RC"\n",
		       DP_RC(rc));

	rc = d_tm_add_metric(&tls->dt_async_cmt_lat, D_TM_STATS_GAUGE,
			     "DTX async commit latency", "ms",
			     "io/dtx/async_cmt_lat/tgt_%u", tgt_id);
	if (rc != DER_SUCCESS)
		D_WARN("Failed to create DTX async commit latency metric: " DF_RC"\n",
		       DP_RC(rc));

	rc = d_tm_add_metric(&tls->dt_chore_retry, D_TM_COUNTER, "DTX chore retry", NULL,
			     "io/dtx/chore_retry/tgt_%u", tgt_id);
	if (rc != DER_SUCCESS)
		D_WARN("Failed to create DTX chore retry metric: " DF_RC "\n", DP_RC(rc));

	return tls;
}

static void
dtx_tls_fini(int tags, void *data)
{
	D_FREE(data);
}

struct dss_module_key dtx_module_key = {
	.dmk_tags       = DAOS_SERVER_TAG,
	.dmk_index      = -1,
	.dmk_init       = dtx_tls_init,
	.dmk_fini       = dtx_tls_fini,
};

static inline char *
dtx_opc_to_str(crt_opcode_t opc)
{
	switch (opc) {
#define X(a, b, c, d, e, f) case a: return f;
		DTX_PROTO_SRV_RPC_LIST
#undef X
	}
	return "dtx_unknown";
}

static void *
dtx_metrics_alloc(const char *path, int tgt_id)
{
	struct dtx_pool_metrics *metrics;
	uint32_t		opc;
	int			rc;

	D_ASSERT(tgt_id >= 0);

	D_ALLOC_PTR(metrics);
	if (metrics == NULL)
		return NULL;

	rc = d_tm_add_metric(&metrics->dpm_batched_degree, D_TM_GAUGE,
			     "degree of DTX entries per batched commit RPC",
			     "entries", "%s/entries/dtx_batched_degree/tgt_%u",
			     path, tgt_id);
	if (rc != DER_SUCCESS)
		D_WARN("Failed to create DTX batched degree metric: "DF_RC"\n",
		       DP_RC(rc));

	rc = d_tm_add_metric(&metrics->dpm_batched_total, D_TM_COUNTER,
			     "total DTX entries via batched commit RPC",
			     "entries", "%s/entries/dtx_batched_total/tgt_%u",
			     path, tgt_id);
	if (rc != DER_SUCCESS)
		D_WARN("Failed to create DTX batched total metric: "DF_RC"\n",
		       DP_RC(rc));

	/** Register different per-opcode counters */
	for (opc = 0; opc < DTX_PROTO_SRV_RPC_COUNT; opc++) {
		rc = d_tm_add_metric(&metrics->dpm_total[opc], D_TM_COUNTER,
				     "total number of processed DTX RPCs",
				     "ops", "%s/ops/%s/tgt_%u", path,
				     dtx_opc_to_str(opc), tgt_id);
		if (rc != DER_SUCCESS)
			D_WARN("Failed to create DTX RPC cnt metric for %s: "
			       DF_RC"\n", dtx_opc_to_str(opc), DP_RC(rc));
	}

	rc = d_tm_add_metric(&metrics->dpm_total[DTX_PROTO_SRV_RPC_COUNT], D_TM_COUNTER,
			     "total number of processed sync DTX_COMMIT", "ops",
			     "%s/ops/dtx_sync_commit/tgt_%u", path, tgt_id);
	if (rc != DER_SUCCESS)
		D_WARN("Failed to create sync DTX_COMMIT RPC cnt metric: "DF_RC"\n", DP_RC(rc));

	return metrics;
}

static void
dtx_metrics_free(void *data)
{
	D_FREE(data);
}

static int
dtx_metrics_count(void)
{
	return (sizeof(struct dtx_pool_metrics) / sizeof(struct d_tm_node_t *));
}

struct daos_module_metrics dtx_metrics = {
    .dmm_tags       = DAOS_TGT_TAG,
    .dmm_init       = dtx_metrics_alloc,
    .dmm_fini       = dtx_metrics_free,
    .dmm_nr_metrics = dtx_metrics_count,
};

static void
dtx_handler(crt_rpc_t *rpc)
{
	struct dtx_pool_metrics	*dpm = NULL;
	struct dtx_in		*din = crt_req_get(rpc);
	struct dtx_out		*dout = crt_reply_get(rpc);
	struct ds_cont_child	*cont = NULL;
	struct dtx_id		*dtis;
	struct dtx_cos_key       dcks[DTX_REFRESH_MAX] = {0};
	uint32_t		 opc = opc_get(rpc->cr_opc);
	uint32_t		 committed = 0;
	uint32_t		*flags = NULL;
	int			*ptr;
	int			 count = DTX_YIELD_CYCLE;
	int			 i = 0;
	int			 rc1 = 0;
	int			 rc;

	rc = ds_cont_child_lookup(din->di_po_uuid, din->di_co_uuid, &cont);
	if (rc != 0) {
		D_ERROR("Failed to locate pool="DF_UUID" cont="DF_UUID
			" for DTX rpc %u: rc = "DF_RC"\n",
			DP_UUID(din->di_po_uuid), DP_UUID(din->di_co_uuid),
			opc, DP_RC(rc));
		goto out;
	}

	dpm = cont->sc_pool->spc_metrics[DAOS_DTX_MODULE];

	switch (opc) {
	case DTX_COMMIT: {
		uint64_t	opc_cnt = 0;
		uint64_t	ent_cnt = 0;

		if (DAOS_FAIL_CHECK(DAOS_DTX_MISS_COMMIT))
			break;

		if (unlikely(din->di_epoch == 1))
			D_GOTO(out, rc = -DER_IO);

		while (i < din->di_dtx_array.ca_count) {
			if (i + count > din->di_dtx_array.ca_count)
				count = din->di_dtx_array.ca_count - i;

			dtis = (struct dtx_id *)din->di_dtx_array.ca_arrays + i;
			rc1 = vos_dtx_commit(cont->sc_hdl, dtis, count, false, NULL);
			if (rc1 > 0)
				committed += rc1;
			else if (rc == 0 && rc1 < 0)
				rc = rc1;

			i += count;
		}

		if (din->di_flags.ca_count > 0)
			flags = din->di_flags.ca_arrays;

		if (flags != NULL && (*flags & DRF_SYNC_COMMIT)) {
			D_ASSERT(din->di_dtx_array.ca_count == 1);
			d_tm_inc_counter(dpm->dpm_total[DTX_PROTO_SRV_RPC_COUNT], 1);
		} else {
			d_tm_inc_counter(dpm->dpm_batched_total,
					 din->di_dtx_array.ca_count);
			rc1 = d_tm_get_counter(NULL, &ent_cnt, dpm->dpm_batched_total);
			D_ASSERT(rc1 == DER_SUCCESS);

			rc1 = d_tm_get_counter(NULL, &opc_cnt, dpm->dpm_total[opc]);
			D_ASSERT(rc1 == DER_SUCCESS);

			d_tm_set_gauge(dpm->dpm_batched_degree, ent_cnt / (opc_cnt + 1));
		}

		break;
	}
	case DTX_ABORT:
		if (DAOS_FAIL_CHECK(DAOS_DTX_MISS_ABORT))
			break;

		if (din->di_epoch != 0) {
			/* Currently, only support to abort single DTX. */
			if (din->di_dtx_array.ca_count != 1)
				D_GOTO(out, rc = -DER_PROTO);

			rc = vos_dtx_abort(cont->sc_hdl,
					   (struct dtx_id *)din->di_dtx_array.ca_arrays,
					   din->di_epoch);
		} else {
			rc = vos_dtx_set_flags(cont->sc_hdl,
					       (struct dtx_id *)din->di_dtx_array.ca_arrays,
					       din->di_dtx_array.ca_count, DTE_CORRUPTED);
		}
		break;
	case DTX_CHECK:
		/* Currently, only support to check single DTX state. */
		if (din->di_dtx_array.ca_count != 1)
			D_GOTO(out, rc = -DER_PROTO);

		rc = vos_dtx_check(cont->sc_hdl, din->di_dtx_array.ca_arrays,
				   NULL, NULL, NULL, false);
		if (rc == DTX_ST_INITED) {
			/* For DTX_CHECK, non-ready one is equal to non-exist. Do not directly
			 * return 'DTX_ST_INITED' to avoid interoperability trouble if related
			 * request is from old server.
			 */
			rc = -DER_NONEXIST;
		} else if (rc == -DER_INPROGRESS && !dtx_cont_opened(cont)) {
			/* Trigger DTX re-index for subsequent (retry) DTX_CHECK. */
			rc1 = start_dtx_reindex_ult(cont);
			if (rc1 != 0)
				D_ERROR(DF_UUID": Failed to trigger DTX reindex: "DF_RC"\n",
					DP_UUID(cont->sc_uuid), DP_RC(rc1));
		}

		break;
	case DTX_REFRESH:
		count = din->di_dtx_array.ca_count;
		if (count == 0)
			D_GOTO(out, rc = 0);

		if (count > DTX_REFRESH_MAX)
			D_GOTO(out, rc = -DER_PROTO);

		D_ALLOC(dout->do_sub_rets.ca_arrays, sizeof(int32_t) * count);
		if (dout->do_sub_rets.ca_arrays == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		dout->do_sub_rets.ca_count = count;

		if (DAOS_FAIL_CHECK(DAOS_DTX_UNCERTAIN)) {
			for (i = 0; i < count; i++) {
				ptr = (int *)dout->do_sub_rets.ca_arrays + i;
				*ptr = -DER_TX_UNCERTAIN;
			}

			D_GOTO(out, rc = 0);
		}

		flags = din->di_flags.ca_arrays;

		for (i = 0, rc1 = 0; i < count; i++) {
			ptr = (int *)dout->do_sub_rets.ca_arrays + i;
			dtis = (struct dtx_id *)din->di_dtx_array.ca_arrays + i;
			*ptr = vos_dtx_check(cont->sc_hdl, dtis, NULL, &din->di_version, &dcks[i],
					     true);
			if (*ptr == -DER_NONEXIST && !(flags[i] & DRF_INITIAL_LEADER)) {
				struct dtx_stat		stat = { 0 };

				/* dtx_id::dti_hlc is client side time stamp. If it is
				 * older than the time of the most new DTX entry that
				 * has been aggregated, then it may has been removed by
				 * DTX aggregation. Under such case, return -DER_TX_UNCERTAIN.
				 */
				vos_dtx_stat(cont->sc_hdl, &stat, DSF_SKIP_BAD);
				if (dtis->dti_hlc <= stat.dtx_newest_aggregated) {
					D_WARN("Not sure about whether the old DTX "
					       DF_DTI" is committed or not: %lu/%lu\n",
					       DP_DTI(dtis), dtis->dti_hlc,
					       stat.dtx_newest_aggregated);
					*ptr = -DER_TX_UNCERTAIN;
				}
			} else if (*ptr == DTX_ST_INITED) {
				/* Leader is in progress, it is not important whether ready or not.
				 * Return DTX_ST_PREPARED to the remote non-leader to handle it as
				 * non-committable case. If we directly return DTX_ST_INITED, then
				 * it will cause interoperability trouble if remote server is old.
				 */
				*ptr = DTX_ST_PREPARED;
			} else if (*ptr == DTX_ST_COMMITTABLE) {
				/* Higher priority for the DTX, then it can be committed ASAP. */
				dtx_cos_prio(cont, dtis, &dcks[i].oid, dcks[i].dkey_hash);
			}
		}
		break;
	default:
		rc = -DER_INVAL;
		break;
	}

out:
	D_DEBUG(DB_TRACE, "Handle DTX ("DF_DTI") rpc %u, count %d, epoch "
		DF_X64" : rc = "DF_RC"\n",
		DP_DTI(din->di_dtx_array.ca_arrays), opc,
		(int)din->di_dtx_array.ca_count, din->di_epoch, DP_RC(rc));

	dout->do_status = rc;
	/* For DTX_COMMIT, it is the count of real committed DTX entries. */
	dout->do_misc = committed;
	rc            = crt_reply_send_input_free(rpc);
	if (rc != 0)
		D_ERROR("send reply failed for DTX rpc %u: rc = "DF_RC"\n", opc,
			DP_RC(rc));

	if (likely(dpm != NULL))
		d_tm_inc_counter(dpm->dpm_total[opc], 1);

	D_FREE(dout->do_sub_rets.ca_arrays);
	dout->do_sub_rets.ca_count = 0;

	if (cont != NULL)
		ds_cont_child_put(cont);
}

static void
dtx_coll_handler(crt_rpc_t *rpc)
{
	struct dtx_coll_in		*dci = crt_req_get(rpc);
	struct dtx_coll_out		*dco = crt_reply_get(rpc);
	struct dtx_coll_prep_args	 dcpa = { 0 };
	d_rank_t			 myrank = dss_self_rank();
	uint32_t			 bitmap_sz = 0;
	uint32_t			 opc = opc_get(rpc->cr_opc);
	uint8_t				*hints = dci->dci_hints.ca_arrays;
	uint8_t				*bitmap = NULL;
	int				*results = NULL;
	bool				 force_check = false;
	int				 len;
	int				 rc;
	int				 i;

	D_ASSERT(hints != NULL);

	if (unlikely(dci->dci_hints.ca_count != dci->dci_max_rank - dci->dci_min_rank + 1)) {
		D_ERROR("On-wire data corruption: hints_cnt %u, max_rank %u, min_rank %u\n",
			(uint32_t)dci->dci_hints.ca_count, dci->dci_max_rank, dci->dci_min_rank);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (unlikely(myrank < dci->dci_min_rank || myrank > dci->dci_max_rank)) {
		D_ERROR("On-wire data corruption: myrank %u, max_rank %u, min_rank %u\n", myrank,
			dci->dci_max_rank, dci->dci_min_rank);
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_DEBUG(DB_TRACE, "Handling collective DTX PRC %u on rank %d for " DF_DTI " with hint %d\n",
		opc, myrank, DP_DTI(&dci->dci_xid), (int)hints[myrank - dci->dci_min_rank]);

	dcpa.dcpa_rpc = rpc;
	rc = ABT_future_create(1, NULL, &dcpa.dcpa_future);
	if (rc != ABT_SUCCESS) {
		D_ERROR("ABT_future_create failed: rc = %d\n", rc);
		D_GOTO(out, rc = dss_abterr2der(rc));
	}

	rc = dss_ult_create(dtx_coll_prep_ult, &dcpa, DSS_XS_VOS, hints[myrank - dci->dci_min_rank],
			    0, NULL);
	if (rc != 0) {
		ABT_future_free(&dcpa.dcpa_future);
		D_ERROR("Failed to create ult on XS %u: " DF_RC "\n",
			hints[myrank - dci->dci_min_rank], DP_RC(rc));
		goto out;
	}

	rc = ABT_future_wait(dcpa.dcpa_future);
	D_ASSERT(rc == ABT_SUCCESS);

	ABT_future_free(&dcpa.dcpa_future);

	switch (dcpa.dcpa_result) {
	case 0:
		D_ASSERT(dcpa.dcpa_dce != NULL);

		if (unlikely(dcpa.dcpa_dce->dce_bitmap == NULL))
			/*
			 * For DTX check, if all local shards are either migrated or
			 * not suitable for check, then assume that they are prepared.
			 * For other cases, DTX commit or abort, the bitmap should not
			 * be empty, so there must be some data corruption if empty.
			 */
			D_GOTO(out, rc = (opc == DTX_COLL_CHECK) ? DTX_ST_PREPARED : -DER_IO);

		bitmap = dcpa.dcpa_dce->dce_bitmap;
		bitmap_sz = dcpa.dcpa_dce->dce_bitmap_sz;
		break;
	case 1:
		/* The DTX has been committed, then depends on the RPC type. */
		if (opc == DTX_COLL_ABORT) {
			D_ERROR("NOT allow to abort committed DTX "DF_DTI"\n",
				DP_DTI(&dci->dci_xid));
			D_GOTO(out, rc = -DER_NO_PERM);
		}

		if (opc == DTX_COLL_CHECK)
			D_GOTO(out, rc = DTX_ST_COMMITTED);

		D_ASSERT(opc == DTX_COLL_COMMIT);
		/*
		 * We do not know whether the DTX on the other VOS targets has been committed
		 * or not, let's continue the commit on the other local VOS targets by force.
		 */
		break;
	case -DER_INPROGRESS:
		/* Fall through. */
	case -DER_NONEXIST:
		/* The shard on the hint VOS target may not exist, then depends on the RPC type. */
		if (opc == DTX_COLL_CHECK)
			force_check = true;
		/*
		 * It is unknown whether the DTX on the other VOS targets has been committed/aborted
		 * or not, let's continue related operation on the other local VOS targets by force.
		 */
		break;
	default:
		D_ASSERTF(dcpa.dcpa_result < 0, "Unexpected result when load MBS for DTX "
			  DF_DTI": "DF_RC"\n", DP_DTI(&dci->dci_xid), DP_RC(dcpa.dcpa_result));
		D_GOTO(out, rc = dcpa.dcpa_result);
	}

	len = dtx_coll_local_exec(dci->dci_po_uuid, dci->dci_co_uuid, &dci->dci_xid, dci->dci_epoch,
				  opc, bitmap_sz, bitmap, &results);
	if (len < 0)
		D_GOTO(out, rc = len);

	if (opc == DTX_COLL_CHECK) {
		for (i = 0; i < len; i++) {
			if (bitmap == NULL || isset(bitmap, i))
				dtx_merge_check_result(&rc, results[i]);
		}

		/*
		 * For force check case, if no shard has been committed, we cannot trust the result
		 * of -DER_NONEXIST, instead, returning -DER_INPROGRESS to make the leader to retry.
		 */
		if (force_check && rc == -DER_NONEXIST)
			D_GOTO(out, rc = -DER_INPROGRESS);
	} else {
		for (i = 0; i < len; i++) {
			if (bitmap == NULL || isset(bitmap, i)) {
				if (results[i] >= 0)
					dco->dco_misc += results[i];
				else if (results[i] != -DER_NONEXIST && rc == 0)
					rc = results[i];
			}
		}
	}

out:
	D_CDEBUG(rc < 0, DLOG_ERR, DB_TRACE,
		 "Handled collective DTX PRC %u on rank %u for "DF_DTI" in "
		 DF_UUID"/"DF_UUID": "DF_RC"\n", opc, myrank, DP_DTI(&dci->dci_xid),
		 DP_UUID(dci->dci_po_uuid), DP_UUID(dci->dci_co_uuid), DP_RC(rc));

	dco->dco_status = rc;
	rc = crt_reply_send(rpc);
	if (rc < 0)
		D_ERROR("Failed to send collective RPC %p reply: "DF_RC"\n", rpc, DP_RC(rc));

	dtx_coll_entry_put(dcpa.dcpa_dce);
	D_FREE(results);
}

static int
dtx_init(void)
{
	int	rc;

	dtx_agg_thd_cnt_up = DTX_AGG_THD_CNT_DEF;
	d_getenv_uint32_t("DAOS_DTX_AGG_THD_CNT", &dtx_agg_thd_cnt_up);
	if (dtx_agg_thd_cnt_up < DTX_AGG_THD_CNT_MIN || dtx_agg_thd_cnt_up > DTX_AGG_THD_CNT_MAX) {
		D_WARN("Invalid DTX aggregation count threshold %u, the valid range is [%u, %u], "
		       "use the default value %u\n", dtx_agg_thd_cnt_up, DTX_AGG_THD_CNT_MIN,
		       DTX_AGG_THD_CNT_MAX, DTX_AGG_THD_CNT_DEF);
		dtx_agg_thd_cnt_up = DTX_AGG_THD_CNT_DEF;
	}

	dtx_agg_thd_cnt_lo = dtx_agg_thd_cnt_up * 19 / 20;
	D_INFO("Set DTX aggregation count threshold as %u (entries)\n", dtx_agg_thd_cnt_up);

	dtx_agg_thd_age_up = DTX_AGG_THD_AGE_DEF;
	d_getenv_uint32_t("DAOS_DTX_AGG_THD_AGE", &dtx_agg_thd_age_up);
	if (dtx_agg_thd_age_up < DTX_AGG_THD_AGE_MIN || dtx_agg_thd_age_up > DTX_AGG_THD_AGE_MAX) {
		D_WARN("Invalid DTX aggregation age threshold %u, the valid range is [%u, %u], "
		       "use the default value %u\n", dtx_agg_thd_age_up, DTX_AGG_THD_AGE_MIN,
		       DTX_AGG_THD_AGE_MAX, DTX_AGG_THD_AGE_DEF);
		dtx_agg_thd_age_up = DTX_AGG_THD_AGE_DEF;
	}

	dtx_agg_thd_age_lo = dtx_agg_thd_age_up * 19 / 20;
	D_INFO("Set DTX aggregation time threshold as %u (seconds)\n", dtx_agg_thd_age_up);

	dtx_batched_ult_max = DTX_BATCHED_ULT_DEF;
	d_getenv_uint32_t("DAOS_DTX_BATCHED_ULT_MAX", &dtx_batched_ult_max);
	D_INFO("Set the max count of DTX batched commit ULTs as %d\n", dtx_batched_ult_max);

	rc = dbtree_class_register(DBTREE_CLASS_DTX_CF,
				   BTR_FEAT_UINT_KEY | BTR_FEAT_DYNAMIC_ROOT,
				   &dbtree_dtx_cf_ops);
	if (rc == 0)
		rc = dbtree_class_register(DBTREE_CLASS_DTX_COS, 0,
					   &dtx_btr_cos_ops);

	return rc;
}

static int
dtx_fini(void)
{
	return 0;
}

static int
dtx_setup(void)
{
	int	rc;

	rc = dss_ult_create_all(dtx_batched_commit, NULL, true);
	if (rc != 0) {
		D_ERROR("Failed to create DTX batched commit ULT: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	rc = dss_ult_create_all(dtx_aggregation_main, NULL, true);
	if (rc != 0)
		D_ERROR("Failed to create DTX aggregation ULT: "DF_RC"\n", DP_RC(rc));

	return rc;
}

#define X(a, b, c, d, e, f)	\
{				\
	.dr_opc       = a,	\
	.dr_hdlr      = d,	\
	.dr_corpc_ops = e,	\
},

static struct daos_rpc_handler dtx_handlers[] = {
	DTX_PROTO_SRV_RPC_LIST
};

#undef X

struct dss_module dtx_module = {
    .sm_name        = "dtx",
    .sm_mod_id      = DAOS_DTX_MODULE,
    .sm_ver         = DAOS_DTX_VERSION,
    .sm_proto_count = 1,
    .sm_init        = dtx_init,
    .sm_fini        = dtx_fini,
    .sm_setup       = dtx_setup,
    .sm_proto_fmt   = {&dtx_proto_fmt},
    .sm_cli_count   = {0},
    .sm_handlers    = {dtx_handlers},
    .sm_key         = &dtx_module_key,
    .sm_metrics     = &dtx_metrics,
};
