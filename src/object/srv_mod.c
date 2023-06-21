/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * object server: module definitions
 */
#define D_LOGFAC	DD_FAC(object)

#include <daos_srv/daos_engine.h>
#include <daos_srv/vos.h>
#include <daos_srv/pool.h>
#include <daos/rpc.h>
#include "obj_rpc.h"
#include "srv_internal.h"

/**
 * Switch of enable DTX or not, enabled by default.
 */
static int
obj_mod_init(void)
{
	int	rc;

	rc = obj_utils_init();
	if (rc)
		goto out;

	rc = obj_class_init();
	if (rc)
		goto out_utils;

	rc = obj_ec_codec_init();
	if (rc) {
		D_ERROR("failed to obj_ec_codec_init\n");
		goto out_class;
	}

	return 0;

out_class:
	obj_class_fini();
out_utils:
	obj_utils_fini();
out:
	D_ERROR("Object module init error: " DF_RC "\n", DP_RC(rc));
	return rc;
}

static int
obj_mod_fini(void)
{
	obj_ec_codec_fini();
	obj_class_fini();
	obj_utils_fini();
	return 0;
}

/* Define for cont_rpcs[] array population below.
 * See OBJ_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e, f)	\
{				\
	.dr_opc       = a,	\
	.dr_hdlr      = d,	\
	.dr_corpc_ops = e,	\
},

static struct daos_rpc_handler obj_handlers[] = {
	OBJ_PROTO_CLI_RPC_LIST
};

#undef X

static int
obj_latency_tm_init(uint32_t opc, int tgt_id, struct d_tm_node_t **tm, char *op, char *desc)
{
	unsigned int	bucket_max = 256;
	int		i;
	int		rc = 0;

	for (i = 0; i < NR_LATENCY_BUCKETS; i++) {
		char *path;

		if (bucket_max < 1024) /** B */
			D_ASPRINTF(path, "io/latency/%s/%uB/tgt_%u",
				   op, bucket_max, tgt_id);
		else if (bucket_max < 1024 * 1024) /** KB */
			D_ASPRINTF(path, "io/latency/%s/%uKB/tgt_%u",
				   op, bucket_max / 1024, tgt_id);
		else if (bucket_max <= 1024 * 1024 * 4) /** MB */
			D_ASPRINTF(path, "io/latency/%s/%uMB/tgt_%u",
				   op, bucket_max / (1024 * 1024), tgt_id);
		else /** >4MB */
			D_ASPRINTF(path, "io/latency/%s/GT4MB/tgt_%u",
				   op, tgt_id);

		rc = d_tm_add_metric(&tm[i], D_TM_STATS_GAUGE, desc, "us", path);
		if (rc)
			D_WARN("Failed to create per-I/O size latency "
			       "sensor: "DF_RC"\n", DP_RC(rc));
		D_FREE(path);

		bucket_max <<= 1;
	}

	return rc;
}

static void *
obj_tls_init(int tags, int xs_id, int tgt_id)
{
	struct obj_tls	*tls;
	uint32_t	opc;
	int		rc;

	D_ALLOC_PTR(tls);
	if (tls == NULL)
		return NULL;

	D_INIT_LIST_HEAD(&tls->ot_pool_list);

	if (tgt_id < 0)
		/** skip sensor setup on system xstreams */
		return tls;

	/** register different per-opcode sensors */
	for (opc = 0; opc < OBJ_PROTO_CLI_COUNT; opc++) {
		/** Start with number of active requests, of type gauge */
		rc = d_tm_add_metric(&tls->ot_op_active[opc], D_TM_STATS_GAUGE,
				     "number of active object RPCs", "ops",
				     "io/ops/%s/active/tgt_%u",
				     obj_opc_to_str(opc), tgt_id);
		if (rc)
			D_WARN("Failed to create active counter: "DF_RC"\n",
			       DP_RC(rc));

		if (opc == DAOS_OBJ_RPC_UPDATE ||
		    opc == DAOS_OBJ_RPC_TGT_UPDATE ||
		    opc == DAOS_OBJ_RPC_FETCH)
			/** See below, latency reported per size for those */
			continue;

		/** And finally the per-opcode latency, of type gauge */
		rc = d_tm_add_metric(&tls->ot_op_lat[opc], D_TM_STATS_GAUGE,
				     "object RPC processing time", "us",
				     "io/ops/%s/latency/tgt_%u",
				     obj_opc_to_str(opc), tgt_id);
		if (rc)
			D_WARN("Failed to create latency sensor: "DF_RC"\n",
			       DP_RC(rc));
	}

	/**
	 * Maintain per-I/O size latency for update & fetch RPCs
	 * of type gauge
	 */

	obj_latency_tm_init(DAOS_OBJ_RPC_UPDATE, tgt_id, tls->ot_update_lat,
			    obj_opc_to_str(DAOS_OBJ_RPC_UPDATE), "update RPC processing time");
	obj_latency_tm_init(DAOS_OBJ_RPC_FETCH, tgt_id, tls->ot_fetch_lat,
			    obj_opc_to_str(DAOS_OBJ_RPC_FETCH), "fetch RPC processing time");

	obj_latency_tm_init(DAOS_OBJ_RPC_TGT_UPDATE, tgt_id, tls->ot_tgt_update_lat,
			    obj_opc_to_str(DAOS_OBJ_RPC_TGT_UPDATE),
			    "update tgt RPC processing time");
	obj_latency_tm_init(DAOS_OBJ_RPC_UPDATE, tgt_id, tls->ot_update_bulk_lat,
			    "bulk_update", "Bulk update processing time");
	obj_latency_tm_init(DAOS_OBJ_RPC_FETCH, tgt_id, tls->ot_fetch_bulk_lat,
			    "bulk_fetch", "Bulk fetch processing time");

	obj_latency_tm_init(DAOS_OBJ_RPC_UPDATE, tgt_id, tls->ot_update_vos_lat,
			    "vos_update", "VOS update processing time");
	obj_latency_tm_init(DAOS_OBJ_RPC_FETCH, tgt_id, tls->ot_fetch_vos_lat,
			    "vos_fetch", "VOS fetch processing time");

	obj_latency_tm_init(DAOS_OBJ_RPC_UPDATE, tgt_id, tls->ot_update_bio_lat,
			    "bio_update", "BIO update processing time");
	obj_latency_tm_init(DAOS_OBJ_RPC_FETCH, tgt_id, tls->ot_fetch_bio_lat,
			    "bio_fetch", "BIO fetch processing time");

	return tls;
}

static void
obj_tls_fini(int tags, void *data)
{
	struct obj_tls *tls = data;
	struct migrate_pool_tls *pool_tls;
	struct migrate_pool_tls *tmp;

	d_list_for_each_entry_safe(pool_tls, tmp, &tls->ot_pool_list,
				   mpt_list)
		migrate_pool_tls_destroy(pool_tls);

	d_sgl_fini(&tls->ot_echo_sgl, true);

	D_FREE(tls);
}

struct dss_module_key obj_module_key = {
	.dmk_tags = DAOS_SERVER_TAG,
	.dmk_index = -1,
	.dmk_init = obj_tls_init,
	.dmk_fini = obj_tls_fini,
};

static int
obj_get_req_attr(crt_rpc_t *rpc, struct sched_req_attr *attr)
{
	if (obj_rpc_is_update(rpc)) {
		struct obj_rw_in	*orw = crt_req_get(rpc);

		sched_req_attr_init(attr, SCHED_REQ_UPDATE,
				    &orw->orw_pool_uuid);
	} else if (obj_rpc_is_fetch(rpc)) {
		struct obj_rw_in	*orw = crt_req_get(rpc);

		sched_req_attr_init(attr, SCHED_REQ_FETCH,
				    &orw->orw_pool_uuid);
	} else if (obj_rpc_is_migrate(rpc)) {
		struct obj_migrate_in	*omi = crt_req_get(rpc);

		sched_req_attr_init(attr, SCHED_REQ_MIGRATE,
				    &omi->om_pool_uuid);
	} else {
		/* Other requests will not be queued, see dss_rpc_hdlr() */
		return -DER_NOSYS;
	}

	return 0;
}

static struct dss_module_ops ds_obj_mod_ops = {
	.dms_get_req_attr = obj_get_req_attr,
};

static void *
obj_metrics_alloc(const char *path, int tgt_id)
{
	struct obj_pool_metrics	*metrics;
	uint32_t		opc;
	int			rc;

	D_ASSERT(tgt_id >= 0);

	D_ALLOC_PTR(metrics);
	if (metrics == NULL)
		return NULL;

	/** register different per-opcode counters */
	for (opc = 0; opc < OBJ_PROTO_CLI_COUNT; opc++) {
		/** Then the total number of requests, of type counter */
		rc = d_tm_add_metric(&metrics->opm_total[opc], D_TM_COUNTER,
				     "total number of processed object RPCs",
				     "ops", "%s/ops/%s/tgt_%u", path,
				     obj_opc_to_str(opc), tgt_id);
		if (rc)
			D_WARN("Failed to create total counter: "DF_RC"\n",
			       DP_RC(rc));
	}

	/** Total number of silently restarted updates, of type counter */
	rc = d_tm_add_metric(&metrics->opm_update_restart, D_TM_COUNTER,
			     "total number of restarted update ops", "updates",
			     "%s/restarted/tgt_%u", path, tgt_id);
	if (rc)
		D_WARN("Failed to create restarted counter: "DF_RC"\n",
		       DP_RC(rc));

	/** Total number of resent updates, of type counter */
	rc = d_tm_add_metric(&metrics->opm_update_resent, D_TM_COUNTER,
			     "total number of resent update RPCs", "updates",
			     "%s/resent/tgt_%u", path, tgt_id);
	if (rc)
		D_WARN("Failed to create resent counter: "DF_RC"\n",
		       DP_RC(rc));

	/** Total number of retry updates locally, of type counter */
	rc = d_tm_add_metric(&metrics->opm_update_retry, D_TM_COUNTER,
			     "total number of retried update RPCs", "updates",
			     "%s/retry/tgt_%u", path, tgt_id);
	if (rc)
		D_WARN("Failed to create retry cnt sensor: "DF_RC"\n", DP_RC(rc));

	/** Total bytes read */
	rc = d_tm_add_metric(&metrics->opm_fetch_bytes, D_TM_COUNTER,
			     "total number of bytes fetched/read", "bytes",
			     "%s/xferred/fetch/tgt_%u", path, tgt_id);
	if (rc)
		D_WARN("Failed to create bytes fetch counter: "DF_RC"\n",
		       DP_RC(rc));

	/** Total bytes written */
	rc = d_tm_add_metric(&metrics->opm_update_bytes, D_TM_COUNTER,
			     "total number of bytes updated/written", "bytes",
			     "%s/xferred/update/tgt_%u", path, tgt_id);
	if (rc)
		D_WARN("Failed to create bytes update counter: "DF_RC"\n",
		       DP_RC(rc));

	/** Total number of EC full-stripe update operations, of type counter */
	rc = d_tm_add_metric(&metrics->opm_update_ec_full, D_TM_COUNTER,
			     "total number of EC sull-stripe updates", "updates",
			     "%s/EC_update/full_stripe/tgt_%u", path, tgt_id);
	if (rc)
		D_WARN("Failed to create EC full stripe update counter: "DF_RC"\n",
		       DP_RC(rc));

	/** Total number of EC partial update operations, of type counter */
	rc = d_tm_add_metric(&metrics->opm_update_ec_partial, D_TM_COUNTER,
			     "total number of EC sull-partial updates", "updates",
			     "%s/EC_update/partial/tgt_%u", path, tgt_id);
	if (rc)
		D_WARN("Failed to create EC partial update counter: "DF_RC"\n",
		       DP_RC(rc));

	return metrics;
}

static void
obj_metrics_free(void *data)
{
	D_FREE(data);
}

static int
obj_metrics_count(void)
{
	return (sizeof(struct obj_pool_metrics) / sizeof(struct d_tm_node_t *));
}

struct dss_module_metrics obj_metrics = {
	.dmm_tags = DAOS_TGT_TAG,
	.dmm_init = obj_metrics_alloc,
	.dmm_fini = obj_metrics_free,
	.dmm_nr_metrics = obj_metrics_count,
};

struct dss_module obj_module = {
	.sm_name	= "obj",
	.sm_mod_id	= DAOS_OBJ_MODULE,
	.sm_ver		= DAOS_OBJ_VERSION,
	.sm_init	= obj_mod_init,
	.sm_fini	= obj_mod_fini,
	.sm_proto_count	= 1,
	.sm_proto_fmt	= {&obj_proto_fmt},
	.sm_cli_count	= {OBJ_PROTO_CLI_COUNT},
	.sm_handlers	= {obj_handlers},
	.sm_key		= &obj_module_key,
	.sm_mod_ops	= &ds_obj_mod_ops,
	.sm_metrics	= &obj_metrics,
};
