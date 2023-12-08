/**
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * object client: Module Definitions
 */
#define D_LOGFAC	DD_FAC(object)

#include <pthread.h>
#include <daos/common.h>
#include <daos/rpc.h>
#include <daos/mgmt.h>
#include <daos/tls.h>
#include <daos/metrics.h>
#include <daos/job.h>
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_producer.h>
#include <daos_types.h>
#include "obj_rpc.h"
#include "obj_internal.h"

#define OBJ_COLL_PUNCH_THD_MIN	31

unsigned int	obj_coll_punch_thd;
unsigned int	srv_io_mode = DIM_DTX_FULL_ENABLED;
int		dc_obj_proto_version;

static void *
dc_obj_tls_init(int tags, int xs_id, int pid)
{
	struct dc_obj_tls *tls;
	int                opc;
	int                rc;
	unsigned long      tid = pthread_self();

	D_ALLOC_PTR(tls);
	if (tls == NULL)
		return NULL;

	/** register different per-opcode sensors */
	for (opc = 0; opc < OBJ_PROTO_CLI_COUNT; opc++) {
		/** Start with number of active requests, of type gauge */
		rc = d_tm_add_metric(
		    &tls->cot_op_active[opc], D_TM_STATS_GAUGE, "number of active object RPCs",
		    "ops", "%s/%u/%lu/io/ops/%s/active", dc_jobid, pid, tid, obj_opc_to_str(opc));
		if (rc) {
			D_WARN("Failed to create active counter: " DF_RC "\n", DP_RC(rc));
			D_GOTO(out, rc);
		}

		if (opc == DAOS_OBJ_RPC_UPDATE || opc == DAOS_OBJ_RPC_TGT_UPDATE ||
		    opc == DAOS_OBJ_RPC_FETCH)
			/** See below, latency reported per size for those */
			continue;

		/** And finally the per-opcode latency, of type gauge */
		rc = d_tm_add_metric(
		    &tls->cot_op_lat[opc], D_TM_STATS_GAUGE, "object RPC processing time", "us",
		    "%s/%u/%lu/io/ops/%s/latency", dc_jobid, pid, tid, obj_opc_to_str(opc));
		if (rc) {
			D_WARN("Failed to create latency sensor: " DF_RC "\n", DP_RC(rc));
			D_GOTO(out, rc);
		}
	}

	/**
	 * Maintain per-I/O size latency for update & fetch RPCs
	 * of type gauge
	 */
	rc = obj_latency_tm_init(DAOS_OBJ_RPC_UPDATE, pid, tls->cot_update_lat,
				 obj_opc_to_str(DAOS_OBJ_RPC_UPDATE), "update RPC processing time",
				 false);
	if (rc)
		D_GOTO(out, rc);

	rc = obj_latency_tm_init(DAOS_OBJ_RPC_FETCH, pid, tls->cot_fetch_lat,
				 obj_opc_to_str(DAOS_OBJ_RPC_FETCH), "fetch RPC processing time",
				 false);
	if (rc)
		D_GOTO(out, rc);

out:
	if (rc) {
		D_FREE(tls);
		tls = NULL;
	}

	return tls;
}

static void
dc_obj_tls_fini(int tags, void *data)
{
	struct dc_obj_tls *tls = data;

	D_FREE(tls);
}

struct daos_module_key dc_obj_module_key = {
    .dmk_tags  = DAOS_CLI_TAG,
    .dmk_index = -1,
    .dmk_init  = dc_obj_tls_init,
    .dmk_fini  = dc_obj_tls_fini,
};

static void *
dc_obj_metrics_alloc(const char *path, int tgt_id)
{
	return obj_metrics_alloc_internal(path, tgt_id, false);
}

static void
dc_obj_metrics_free(void *data)
{
	D_FREE(data);
}

/* metrics per pool */
struct daos_module_metrics dc_obj_metrics = {
    .dmm_tags       = DAOS_CLI_TAG,
    .dmm_init       = dc_obj_metrics_alloc,
    .dmm_fini       = dc_obj_metrics_free,
    .dmm_nr_metrics = obj_metrics_count,
};

/**
 * Initialize object interface
 */
int
dc_obj_init(void)
{
	uint32_t ver_array[2] = {DAOS_OBJ_VERSION - 1, DAOS_OBJ_VERSION};
	int      rc;

	d_getenv_bool(DAOS_CLIENT_METRICS_ENABLE, &daos_client_metric);
	if (daos_client_metric) {
		daos_register_key(&dc_obj_module_key);
		rc = daos_register_metrics(DAOS_CLI_TAG, DAOS_OBJ_MODULE, &dc_obj_metrics);
		if (rc) {
			DL_ERROR(rc, "register object failed");
			return rc;
		}
	}

	rc = obj_utils_init();
	if (rc)
		return rc;

	rc = obj_class_init();
	if (rc)
		D_GOTO(out_utils, rc);

	dc_obj_proto_version = 0;
	rc = daos_rpc_proto_query(obj_proto_fmt_v9.cpf_base, ver_array, 2, &dc_obj_proto_version);
	if (rc)
		D_GOTO(out_class, rc);

	if (dc_obj_proto_version == DAOS_OBJ_VERSION - 1) {
		rc = daos_rpc_register(&obj_proto_fmt_v9, OBJ_PROTO_CLI_COUNT, NULL,
				       DAOS_OBJ_MODULE);
	} else if (dc_obj_proto_version == DAOS_OBJ_VERSION) {
		rc = daos_rpc_register(&obj_proto_fmt_v10, OBJ_PROTO_CLI_COUNT, NULL,
				       DAOS_OBJ_MODULE);
	} else {
		D_ERROR("%d version object RPC not supported.\n", dc_obj_proto_version);
		rc = -DER_PROTO;
	}

	if (rc) {
		D_ERROR("failed to register daos %d version obj RPCs: "DF_RC"\n",
			dc_obj_proto_version, DP_RC(rc));
		D_GOTO(out_class, rc);
	}

	rc = obj_ec_codec_init();
	if (rc) {
		D_ERROR("failed to obj_ec_codec_init: "DF_RC"\n", DP_RC(rc));
		if (dc_obj_proto_version == DAOS_OBJ_VERSION - 1)
			daos_rpc_unregister(&obj_proto_fmt_v9);
		else
			daos_rpc_unregister(&obj_proto_fmt_v10);
		D_GOTO(out_class, rc);
	}

	obj_coll_punch_thd = OBJ_COLL_PUNCH_THD_MIN;
	d_getenv_uint("DAOS_OBJ_COLL_PUNCH_THD", &obj_coll_punch_thd);
	if (obj_coll_punch_thd < OBJ_COLL_PUNCH_THD_MIN) {
		D_WARN("Invalid collective punch threshold %u, it cannot be smaller than %u, "
		       "use the default value %u\n", obj_coll_punch_thd,
		       OBJ_COLL_PUNCH_THD_MIN, OBJ_COLL_PUNCH_THD_MIN);
		obj_coll_punch_thd = OBJ_COLL_PUNCH_THD_MIN;
	}
	D_INFO("Set object collective punch threshold as %u\n", obj_coll_punch_thd);

	tx_verify_rdg = false;
	d_getenv_bool("DAOS_TX_VERIFY_RDG", &tx_verify_rdg);
	D_INFO("%s TX redundancy group verification\n", tx_verify_rdg ? "Enable" : "Disable");

out_class:
	if (rc)
		obj_class_fini();
out_utils:
	if (rc)
		obj_utils_fini();

	return rc;
}

/**
 * Finalize object interface
 */
void
dc_obj_fini(void)
{
	if (dc_obj_proto_version == DAOS_OBJ_VERSION - 1)
		daos_rpc_unregister(&obj_proto_fmt_v9);
	else
		daos_rpc_unregister(&obj_proto_fmt_v10);
	obj_ec_codec_fini();
	obj_class_fini();
	obj_utils_fini();
	if (daos_client_metric)
		daos_unregister_key(&dc_obj_module_key);
}
