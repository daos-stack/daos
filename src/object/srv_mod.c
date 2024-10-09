/**
 * (C) Copyright 2016-2024 Intel Corporation.
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
#include <daos/metrics.h>
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

static struct daos_rpc_handler obj_handlers_v9[] = {
	OBJ_PROTO_CLI_RPC_LIST(9)
};

static struct daos_rpc_handler obj_handlers_v10[] = {
	OBJ_PROTO_CLI_RPC_LIST(10)
};

#undef X

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
			    obj_opc_to_str(DAOS_OBJ_RPC_UPDATE), "update RPC processing time",
			    true);
	obj_latency_tm_init(DAOS_OBJ_RPC_FETCH, tgt_id, tls->ot_fetch_lat,
			    obj_opc_to_str(DAOS_OBJ_RPC_FETCH), "fetch RPC processing time", true);

	obj_latency_tm_init(DAOS_OBJ_RPC_TGT_UPDATE, tgt_id, tls->ot_tgt_update_lat,
			    obj_opc_to_str(DAOS_OBJ_RPC_TGT_UPDATE),
			    "update tgt RPC processing time", true);
	obj_latency_tm_init(DAOS_OBJ_RPC_UPDATE, tgt_id, tls->ot_update_bulk_lat, "bulk_update",
			    "Bulk update processing time", true);
	obj_latency_tm_init(DAOS_OBJ_RPC_FETCH, tgt_id, tls->ot_fetch_bulk_lat, "bulk_fetch",
			    "Bulk fetch processing time", true);

	obj_latency_tm_init(DAOS_OBJ_RPC_UPDATE, tgt_id, tls->ot_update_vos_lat, "vos_update",
			    "VOS update processing time", true);
	obj_latency_tm_init(DAOS_OBJ_RPC_FETCH, tgt_id, tls->ot_fetch_vos_lat, "vos_fetch",
			    "VOS fetch processing time", true);

	obj_latency_tm_init(DAOS_OBJ_RPC_UPDATE, tgt_id, tls->ot_update_bio_lat, "bio_update",
			    "BIO update processing time", true);
	obj_latency_tm_init(DAOS_OBJ_RPC_FETCH, tgt_id, tls->ot_fetch_bio_lat, "bio_fetch",
			    "BIO fetch processing time", true);

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
	int	opc = opc_get(rpc->cr_opc);
	int	proto_ver = crt_req_get_proto_ver(rpc);
	int	rc = 0;

	D_ASSERT(proto_ver == DAOS_OBJ_VERSION || proto_ver == DAOS_OBJ_VERSION - 1);

	/* Extract hint from RPC */
	attr->sra_enqueue_id = 0;

	switch (opc) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_TGT_UPDATE:
	case DAOS_OBJ_RPC_FETCH: {
		struct obj_rw_in	*orw = crt_req_get(rpc);

		sched_req_attr_init(attr, obj_rpc_is_update(rpc) ?
				    SCHED_REQ_UPDATE : SCHED_REQ_FETCH,
				    &orw->orw_pool_uuid);
		break;
	}
	case DAOS_OBJ_RPC_MIGRATE: {
		struct obj_migrate_in *omi = crt_req_get(rpc);

		sched_req_attr_init(attr, SCHED_REQ_MIGRATE, &omi->om_pool_uuid);
		break;
	}
	/*
	 * To enhance system performance, following RPCs are currently not
	 * enqueued. Recent benchmarks have indicated a 2%~3% drop in stat
	 * and removal operations when this is done. It may be worthwhile to
	 * reassess this decision in the future, especially if Quality of
	 * Service(QoS) requirements are introduced. (See DAOS-15076)
	 */
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE: {
		struct obj_key_enum_in *oei = crt_req_get(rpc);

		sched_req_attr_init(attr, SCHED_REQ_ANONYM, &oei->oei_pool_uuid);
		break;
	}
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_AKEYS: {
		struct obj_punch_in *opi = crt_req_get(rpc);

		sched_req_attr_init(attr, SCHED_REQ_ANONYM, &opi->opi_pool_uuid);
		break;
	}
	case DAOS_OBJ_RPC_QUERY_KEY: {
		struct obj_query_key_in *okqi = crt_req_get(rpc);

		sched_req_attr_init(attr, SCHED_REQ_ANONYM, &okqi->okqi_pool_uuid);
		break;
	}
	case DAOS_OBJ_RPC_SYNC: {
		struct obj_sync_in *osi = crt_req_get(rpc);

		sched_req_attr_init(attr, SCHED_REQ_ANONYM, &osi->osi_pool_uuid);
		break;
	}
	case DAOS_OBJ_RPC_KEY2ANCHOR: {
		struct obj_key2anchor_in *oki = crt_req_get(rpc);

		sched_req_attr_init(attr, SCHED_REQ_ANONYM, &oki->oki_pool_uuid);
		break;
	}
	case DAOS_OBJ_RPC_EC_AGGREGATE: {
		struct obj_ec_agg_in *ea = crt_req_get(rpc);

		sched_req_attr_init(attr, SCHED_REQ_ANONYM, &ea->ea_pool_uuid);
		break;
	}
	case DAOS_OBJ_RPC_EC_REPLICATE: {
		struct obj_ec_rep_in *er = crt_req_get(rpc);

		sched_req_attr_init(attr, SCHED_REQ_ANONYM, &er->er_pool_uuid);
		break;
	}
	case DAOS_OBJ_RPC_CPD: {
		struct obj_cpd_in *oci = crt_req_get(rpc);

		sched_req_attr_init(attr, SCHED_REQ_ANONYM, &oci->oci_pool_uuid);
		break;
	}
	case DAOS_OBJ_RPC_COLL_PUNCH: {
		struct obj_coll_punch_in *ocpi = crt_req_get(rpc);

		sched_req_attr_init(attr, SCHED_REQ_ANONYM, &ocpi->ocpi_po_uuid);
		break;
	}
	case DAOS_OBJ_RPC_COLL_QUERY: {
		struct obj_coll_query_in *ocqi = crt_req_get(rpc);

		sched_req_attr_init(attr, SCHED_REQ_ANONYM, &ocqi->ocqi_po_uuid);
		break;
	}
	default:
		/* Other requests will not be queued, see dss_rpc_hdlr() */
		rc = -DER_NOSYS;
		break;
	}

	/* disable RPC throttle */
	attr->sra_flags |= SCHED_REQ_FL_NO_REJECT;

	return rc;
}

static int
obj_set_req(crt_rpc_t *rpc, struct sched_req_attr *attr)
{
	/*
	int	opc = opc_get(rpc->cr_opc);
	int	proto_ver = crt_req_get_proto_ver(rpc);
	int	rc = -DER_OVERLOAD_RETRY;

	D_ASSERT(proto_ver == DAOS_OBJ_VERSION);

	switch (opc) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_TGT_UPDATE:
	case DAOS_OBJ_RPC_FETCH: {
		struct obj_rw_v10_out	*orwo_v10 = crt_reply_get(rpc);

		orwo_v10->orw_comm_out.req_out_enqueue_id = attr->sra_enqueue_id;
		orwo_v10->orw_ret = -DER_OVERLOAD_RETRY;
		break;
	}
	case DAOS_OBJ_RPC_MIGRATE: {
		struct obj_migrate_out *om = crt_reply_get(rpc);

		om->om_comm_out.req_out_enqueue_id = attr->sra_enqueue_id;
		om->om_status = -DER_OVERLOAD_RETRY;
		break;
	}
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE: {
		struct obj_key_enum_v10_out *oeo_v10 = crt_reply_get(rpc);

		oeo_v10->oeo_comm_out.req_out_enqueue_id = attr->sra_enqueue_id;
		oeo_v10->oeo_ret = -DER_OVERLOAD_RETRY;
		break;
	}
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_AKEYS: {
		struct obj_punch_v10_out *opo_v10 = crt_reply_get(rpc);

		opo_v10->opo_comm_out.req_out_enqueue_id = attr->sra_enqueue_id;
		opo_v10->opo_ret = -DER_OVERLOAD_RETRY;
		break;
	}
	case DAOS_OBJ_RPC_QUERY_KEY: {
		struct obj_query_key_v10_out *okqo_v10 = crt_reply_get(rpc);

		okqo_v10->okqo_comm_out.req_out_enqueue_id = attr->sra_enqueue_id;
		okqo_v10->okqo_ret = -DER_OVERLOAD_RETRY;
		break;
	}
	case DAOS_OBJ_RPC_SYNC: {
		struct obj_sync_v10_out *oso_v10 = crt_reply_get(rpc);

		oso_v10->oso_comm_out.req_out_enqueue_id = attr->sra_enqueue_id;
		oso_v10->oso_ret = -DER_OVERLOAD_RETRY;
		break;
	}
	case DAOS_OBJ_RPC_KEY2ANCHOR: {
		struct obj_key2anchor_v10_out *oko_v10 = crt_reply_get(rpc);

		oko_v10->oko_comm_out.req_out_enqueue_id = attr->sra_enqueue_id;
		oko_v10->oko_ret = -DER_OVERLOAD_RETRY;
		break;
	}
	case DAOS_OBJ_RPC_EC_AGGREGATE: {
		struct obj_ec_agg_out *ea_out = crt_reply_get(rpc);

		ea_out->ea_comm_out.req_out_enqueue_id = attr->sra_enqueue_id;
		ea_out->ea_status = -DER_OVERLOAD_RETRY;
		break;
	}
	case DAOS_OBJ_RPC_EC_REPLICATE: {
		struct obj_ec_rep_out *er_out = crt_reply_get(rpc);

		er_out->er_comm_out.req_out_enqueue_id = attr->sra_enqueue_id;
		er_out->er_status = -DER_OVERLOAD_RETRY;
		break;
	}
	case DAOS_OBJ_RPC_CPD:
		rc = -DER_TIMEDOUT;
		break;
	case DAOS_OBJ_RPC_COLL_PUNCH: {
		struct obj_coll_punch_out *ocpo = crt_reply_get(rpc);

		ocpo->ocpo_comm_out.req_out_enqueue_id = attr->sra_enqueue_id;
		ocpo->ocpo_ret = -DER_OVERLOAD_RETRY;
		break;
	}
	case DAOS_OBJ_RPC_COLL_QUERY: {
		struct obj_coll_query_out *ocqo = crt_reply_get(rpc);

		ocqo->ocqo_comm_out.req_out_enqueue_id = attr->sra_enqueue_id;
		ocqo->ocqo_ret = -DER_OVERLOAD_RETRY;
		break;
	}
	default:
		rc = -DER_TIMEDOUT;
		break;
	}
	*/

	return -DER_TIMEDOUT;
}

static struct dss_module_ops ds_obj_mod_ops = {
	.dms_get_req_attr = obj_get_req_attr,
	.dms_set_req	  = obj_set_req,
};

static void *
obj_metrics_alloc(const char *path, int tgt_id)
{
	return obj_metrics_alloc_internal(path, tgt_id, true);
}

struct daos_module_metrics obj_metrics = {
    .dmm_tags       = DAOS_TGT_TAG,
    .dmm_init       = obj_metrics_alloc,
    .dmm_fini       = obj_metrics_free,
    .dmm_nr_metrics = obj_metrics_count,
};

struct dss_module obj_module = {
	.sm_name	= "obj",
	.sm_mod_id	= DAOS_OBJ_MODULE,
	.sm_ver		= DAOS_OBJ_VERSION,
	.sm_init	= obj_mod_init,
	.sm_fini	= obj_mod_fini,
	.sm_proto_count	= 2,
	.sm_proto_fmt	= {&obj_proto_fmt_v9, &obj_proto_fmt_v10},
	.sm_cli_count	= {OBJ_PROTO_CLI_COUNT, OBJ_PROTO_CLI_COUNT},
	.sm_handlers	= {obj_handlers_v9, obj_handlers_v10},
	.sm_key		= &obj_module_key,
	.sm_mod_ops	= &ds_obj_mod_ops,
	.sm_metrics	= &obj_metrics,
};
