/**
 * (C) Copyright 2016-2021 Intel Corporation.
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
#include "obj_internal.h"

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
	D_ERROR("Object module init error: %s\n", d_errstr(rc));
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

static void *
obj_tls_init(int xs_id, int tgt_id)
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
		rc = d_tm_add_metric(&tls->ot_op_active[opc], D_TM_GAUGE,
				     "number of active object RPCs", "",
				     "io/%u/ops/%s/active", tgt_id,
				     obj_opc_to_str(opc));
		if (rc)
			D_WARN("Failed to create active cnt sensor: "DF_RC"\n",
			       DP_RC(rc));

		/** Then the total number of requests, of type counter */
		rc = d_tm_add_metric(&tls->ot_op_total[opc], D_TM_COUNTER,
				     "total number of processed object RPCs",
				     "", "io/%u/ops/%s/total", tgt_id,
				     obj_opc_to_str(opc));
		if (rc)
			D_WARN("Failed to create total cnt sensor: "DF_RC"\n",
			       DP_RC(rc));

		if (opc == DAOS_OBJ_RPC_UPDATE ||
		    opc == DAOS_OBJ_RPC_TGT_UPDATE ||
		    opc == DAOS_OBJ_RPC_FETCH)
			/** See below, latency reported per size for those */
			continue;

		/** And finally the per-opcode latency, of type gauge */
		rc = d_tm_add_metric(&tls->ot_op_lat[opc], D_TM_GAUGE,
				     "object RPC processing time (in us)", "",
				     "io/%u/ops/%s/latency", tgt_id,
				     obj_opc_to_str(opc));
		if (rc)
			D_WARN("Failed to create latency sensor: "DF_RC"\n",
			       DP_RC(rc));
	}

	/**
	 * Maintain per-I/O size latency for update & fetch RPCs
	 * of type gauge
	 */
	for (opc = 0; opc < 2; opc++) {
		int			i;
		unsigned int		bucket_max = 256;
		struct d_tm_node_t	**tm[2] = { tls->ot_update_lat,
						    tls->ot_fetch_lat };
		for (i = 0; i < NR_LATENCY_BUCKETS; i++) {
			char *path;

			if (bucket_max < 1024) /** B */
				D_ASPRINTF(path, "io/%u/%s_latency_%uB",
					   tgt_id, opc ? "fetch" : "update",
					   bucket_max);
			else if (bucket_max < 1024 * 1024) /** KB */
				D_ASPRINTF(path, "io/%u/%s_latency_%uKB",
					   tgt_id, opc ? "fetch" : "update",
					   bucket_max / 1024);
			else if (bucket_max <= 1024 * 1024 * 4) /** MB */
				D_ASPRINTF(path, "io/%u/%s_latency_%uMB",
					   tgt_id, opc ? "fetch" : "update",
					   bucket_max / (1024 * 1024));
			else /** >4MB */
				D_ASPRINTF(path, "io/%u/%s_latency_>4MB",
					   tgt_id, opc ? "fetch" : "update");
			rc = d_tm_add_metric(&tm[opc][i], D_TM_GAUGE,
					     "Per-I/O size RPC processing time "
					     "(in us)", "", path);
			D_FREE(path);
			if (rc)
				D_WARN("Failed to create per-I/O size latency "
				       "sensor: "DF_RC"\n", DP_RC(rc));
			bucket_max <<= 1;
		}
	}

	/** Total number of silently restarted updates, of type counter */
	rc = d_tm_add_metric(&tls->ot_update_restart, D_TM_COUNTER,
			     "total number of restarted update ops", "",
			     "io/%u/ops/%s/restarted", tgt_id,
			     obj_opc_to_str(DAOS_OBJ_RPC_UPDATE));
	if (rc)
		D_WARN("Failed to create restarted cnt sensor: "DF_RC"\n",
		       DP_RC(rc));

	/** Total number of resent updates, of type counter */
	rc = d_tm_add_metric(&tls->ot_update_resent, D_TM_COUNTER,
			     "total number of resent update RPCs", "",
			     "io/%u/ops/%s/resent", tgt_id,
			     obj_opc_to_str(DAOS_OBJ_RPC_UPDATE));
	if (rc)
		D_WARN("Failed to create resent cnt sensor: "DF_RC"\n",
		       DP_RC(rc));

	/** Total bytes read */
	rc = d_tm_add_metric(&tls->ot_fetch_bytes, D_TM_COUNTER,
			     "total number of bytes fetched/read", "",
			     "io/%u/fetch_bytes", tgt_id);
	if (rc)
		D_WARN("Failed to create bytes fetch sensor: "DF_RC"\n",
		       DP_RC(rc));

	/** Total bytes written */
	rc = d_tm_add_metric(&tls->ot_update_bytes, D_TM_COUNTER,
			     "total number of bytes updated/written", "",
			     "io/%u/update_bytes", tgt_id);
	if (rc)
		D_WARN("Failed to create bytes update sensor: "DF_RC"\n",
		       DP_RC(rc));

	return tls;
}

static void
obj_tls_fini(void *data)
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

struct dss_module obj_module =  {
	.sm_name	= "obj",
	.sm_mod_id	= DAOS_OBJ_MODULE,
	.sm_ver		= DAOS_OBJ_VERSION,
	.sm_init	= obj_mod_init,
	.sm_fini	= obj_mod_fini,
	.sm_proto_fmt	= &obj_proto_fmt,
	.sm_cli_count	= OBJ_PROTO_CLI_COUNT,
	.sm_handlers	= obj_handlers,
	.sm_key		= &obj_module_key,
	.sm_mod_ops	= &ds_obj_mod_ops,
};
