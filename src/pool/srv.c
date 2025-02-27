/**
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * ds_pool: Pool Server
 *
 * This is part of daos_server. It exports the pool RPC handlers and implements
 * Pool Server API.
 */
#define D_LOGFAC	DD_FAC(pool)

#include <daos_srv/pool.h>
#include <daos/rpc.h>
#include <daos/metrics.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/bio.h>
#include "rpc.h"
#include "srv_internal.h"
#include "srv_layout.h"

bool		ec_agg_disabled;
uint32_t        pw_rf = -1; /* pool wise redundancy factor */
uint32_t        ps_cache_intvl = 2;  /* pool space cache expiration time, in seconds */
#define PW_RF_DEFAULT (2)
#define PW_RF_MIN     (0)
#define PW_RF_MAX     (4)

static inline bool
check_pool_redundancy_factor(const char *variable)
{
	d_getenv_uint32_t(variable, &pw_rf);
	if (pw_rf == -1)
		return false;

	D_INFO("Checked threshold %s=%d\n", variable, pw_rf);

	if (pw_rf <= PW_RF_MAX)
		return true;

	D_INFO("pw_rf %d is out of range [%d, %d], take default %d\n", pw_rf, PW_RF_MIN, PW_RF_MAX,
	       PW_RF_DEFAULT);
	pw_rf = PW_RF_DEFAULT;

	return true;
}

static int
init(void)
{
	int rc;

	rc = ds_pool_cache_init();
	if (rc != 0)
		D_GOTO(err, rc);

	rc = ds_pool_hdl_hash_init();
	if (rc != 0)
		D_GOTO(err_pool_cache, rc);

	rc = ds_pool_iv_init();
	if (rc)
		D_GOTO(err_hdl_hash, rc);

	rc = ds_pool_prop_default_init();
	if (rc)
		D_GOTO(err_pool_iv, rc);

	ec_agg_disabled = false;
	d_getenv_bool("DAOS_EC_AGG_DISABLE", &ec_agg_disabled);
	if (unlikely(ec_agg_disabled))
		D_WARN("EC aggregation is disabled.\n");

	pw_rf = -1;
	if (!check_pool_redundancy_factor("DAOS_POOL_RF"))
		pw_rf = PW_RF_DEFAULT;
	D_INFO("pool redundancy factor %d\n", pw_rf);

	d_getenv_uint32_t("DAOS_POOL_SPACE_CACHE_INTVL", &ps_cache_intvl);
	if (ps_cache_intvl > 20) {
		D_WARN("pool space cache expiration time %u is too large, use default value\n",
		       ps_cache_intvl);
		ps_cache_intvl = 2;
	}
	D_INFO("pool space cache expiration time set to %u seconds\n", ps_cache_intvl);

	ds_pool_rsvc_class_register();

	bio_register_ract_ops(&nvme_reaction_ops);
	return 0;

err_pool_iv:
	ds_pool_iv_fini();
err_hdl_hash:
	ds_pool_hdl_hash_fini();
err_pool_cache:
	ds_pool_cache_fini();
err:
	return rc;
}

static int
fini(void)
{
	ds_pool_rsvc_class_unregister();
	ds_pool_hdl_hash_fini();
	ds_pool_iv_fini();
	ds_pool_cache_fini();
	ds_pool_prop_default_fini();
	return 0;
}

static int
setup(void)
{
	bool start = true;

	if (!engine_in_check()) {
		d_getenv_bool("DAOS_START_POOL_SVC", &start);
		if (start)
			return ds_pool_start_all();
	}

	return 0;
}

static int
cleanup(void)
{
	int rc;

	rc = ds_pool_stop_all();
	if (rc)
		D_ERROR("Stop pools failed. "DF_RC"\n", DP_RC(rc));

	return rc;
}

static struct crt_corpc_ops ds_pool_tgt_disconnect_co_ops = {
	.co_aggregate	= ds_pool_tgt_disconnect_aggregator,
	.co_pre_forward	= NULL,
};

static struct crt_corpc_ops ds_pool_tgt_query_co_ops_v6 = {
    .co_aggregate   = ds_pool_tgt_query_aggregator_v6,
    .co_pre_forward = NULL,
};

static struct crt_corpc_ops ds_pool_tgt_query_co_ops = {
	.co_aggregate	= ds_pool_tgt_query_aggregator,
	.co_pre_forward	= NULL,
};

/* Define for cont_rpcs[] array population below.
 * See POOL_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.dr_opc       = a,	\
	.dr_hdlr      = d,	\
	.dr_corpc_ops = e,	\
},

static struct daos_rpc_handler pool_handlers_v6[] = {POOL_PROTO_CLI_RPC_LIST(6)
							 POOL_PROTO_SRV_RPC_LIST(6)};

static struct daos_rpc_handler pool_handlers_v7[] = {POOL_PROTO_CLI_RPC_LIST(7)
							 POOL_PROTO_SRV_RPC_LIST(7)};

#undef X

static void *
pool_tls_init(int tags, int xs_id, int tgt_id)
{
	struct pool_tls *tls;

	D_ALLOC_PTR(tls);
	if (tls == NULL)
		return NULL;

	D_INIT_LIST_HEAD(&tls->dt_pool_list);
	return tls;
}

static void
pool_tls_fini(int tags, void *data)
{
	struct pool_tls		*tls = data;
	struct ds_pool_child	*child;

	D_ASSERT(tls != NULL);

	/* pool child cache should be empty now */
	d_list_for_each_entry(child, &tls->dt_pool_list, spc_list) {
		D_ERROR(DF_UUID": ref: %d\n",
			DP_UUID(child->spc_uuid), child->spc_ref);
	}

	if (!d_list_empty(&tls->dt_pool_list)) {
		bool strict = false;

		d_getenv_bool("DAOS_STRICT_SHUTDOWN", &strict);
		if (strict)
			D_ASSERTF(false, "dt_pool_list not empty\n");
		else
			D_ERROR("dt_pool_list not empty\n");
	}

	D_FREE(tls);
}

struct dss_module_key pool_module_key = {
	.dmk_tags = DAOS_SERVER_TAG,
	.dmk_index = -1,
	.dmk_init = pool_tls_init,
	.dmk_fini = pool_tls_fini,
};

struct daos_module_metrics pool_metrics = {
    .dmm_tags       = DAOS_SYS_TAG,
    .dmm_init       = ds_pool_metrics_alloc,
    .dmm_fini       = ds_pool_metrics_free,
    .dmm_nr_metrics = ds_pool_metrics_count,
};

struct dss_module pool_module = {
    .sm_name        = "pool",
    .sm_mod_id      = DAOS_POOL_MODULE,
    .sm_ver         = DAOS_POOL_VERSION,
    .sm_proto_count = 2,
    .sm_init        = init,
    .sm_fini        = fini,
    .sm_setup       = setup,
    .sm_cleanup     = cleanup,
    .sm_proto_fmt   = {&pool_proto_fmt_v6, &pool_proto_fmt_v7},
    .sm_cli_count   = {POOL_PROTO_CLI_COUNT, POOL_PROTO_CLI_COUNT},
    .sm_handlers    = {pool_handlers_v6, pool_handlers_v7},
    .sm_key         = &pool_module_key,
    .sm_metrics     = &pool_metrics,
};
