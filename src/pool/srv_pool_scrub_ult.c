/*
 * (C) Copyright 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(csum)

#include <daos_srv/vos.h>
#include <daos_srv/srv_csum.h>
#include <gurt/telemetry_producer.h>
#include <daos/pool.h>
#include <daos_prop.h>
#include "srv_internal.h"

#define C_TRACE(...) D_DEBUG(DB_CSUM, __VA_ARGS__)

/*
 * telemetry metrics for the scrubber
 * The target must be parsed out by
 * lib/telemetry/promexp/collector.go:extractLabels(). If the format here
 * changes, extractLabels() must be updated as well.
 */
#define DF_POOL_DIR "%s/tgt_%d/scrubber"
#define DP_POOL_DIR(ctx) (ctx)->sc_pool->sp_path, (ctx)->sc_dmi->dmi_tgt_id

#define M_CSUM_COUNTER "csums/current"
#define M_CSUM_COUNTER_TOTAL "csums/total"
#define M_CSUM_COUNTER_PREV "csums/prev"
#define M_BYTES_SCRUBBED "bytes_scrubbed/current"
#define M_BYTES_SCRUBBED_TOTAL "bytes_scrubbed/total"
#define M_BYTES_SCRUBBED_PREV "bytes_scrubbed/prev"
#define M_CSUM_CORRUPTION "corruption/current"
#define M_CSUM_CORRUPTION_TOTAL "corruption/total"
#define M_STARTED "scrubber_started"
#define M_LAST_DURATION "prev_duration"

/*
 * DAOS_CSUM_SCRUB_DISABLED can be set in the server config to disable the
 * scrubbing ULT completely for the engine.
 */
static inline bool
scrubbing_is_enabled()
{
	bool result = false;

	d_getenv_bool("DAOS_CSUM_SCRUB_DISABLED", &result);
	return !result;
}

static inline int
yield_fn(void *arg)
{
	sched_req_yield(arg);

	return 0;
}

static inline int
sleep_fn(void *arg, uint32_t msec)
{
	sched_req_sleep(arg, msec);

	return 0;
}

static int
cont_lookup_cb(uuid_t pool_uuid, uuid_t cont_uuid, void *arg,
	       struct cont_scrub *cont)
{
	struct ds_cont_child	*cont_child = NULL;
	int			 rc;

	rc = ds_cont_child_lookup(pool_uuid, cont_uuid, &cont_child);
	if (rc != 0) {
		D_ERROR("failed to get cont: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	cont->scs_cont_csummer = cont_child->sc_csummer;
	cont->scs_cont_hdl = cont_child->sc_hdl;
	uuid_copy(cont->scs_cont_uuid, cont_uuid);
	cont->scs_cont_src = cont_child;
	cont->scs_props_fetched = cont_child->sc_props_fetched;

	ABT_mutex_lock(cont_child->sc_mutex);
	cont_child->sc_scrubbing = 1;
	ABT_mutex_unlock(cont_child->sc_mutex);

	return 0;
}

static inline void
cont_put_cb(void *cont)
{
	struct ds_cont_child *cont_child = cont;

	ABT_mutex_lock(cont_child->sc_mutex);
	cont_child->sc_scrubbing = 0;
	ABT_cond_broadcast(cont_child->sc_scrub_cond);
	ABT_mutex_unlock(cont_child->sc_mutex);

	ds_cont_child_put(cont_child);
}

static inline bool
cont_is_stopping_cb(void *cont)
{
	struct ds_cont_child *cont_child = cont;

	return cont_child->sc_stopping == 1;
}

static void
sc_add_pool_metrics(struct scrub_ctx *ctx)
{
	int	rc;

	rc = d_tm_add_metric(&ctx->sc_metrics.scm_scrub_count,
			     D_TM_COUNTER, "Number of times the VOS tree has been scanned and "
					   "scrubbed (since the scrubber started)",
			     NULL, DF_POOL_DIR"/scrubs_completed", DP_POOL_DIR(ctx));
	if (rc)
		D_WARN("Failed to create scm_scrub_count metric: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&ctx->sc_metrics.scm_start,
			     D_TM_TIMESTAMP,
			     "Time stamp when the current tree scrub started", NULL,
			     DF_POOL_DIR"/"M_STARTED, DP_POOL_DIR(ctx));
	if (rc)
		D_WARN("Failed to create scm_start timestamp metric: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&ctx->sc_metrics.scm_next_csum_scrub, D_TM_GAUGE,
			     "milliseconds until next csum scrub", "msec",
			     DF_POOL_DIR"/next_csum_scrub", DP_POOL_DIR(ctx));
	if (rc)
		D_WARN("Failed to create scm_next_csum_scrub metric: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&ctx->sc_metrics.scm_next_tree_scrub, D_TM_GAUGE,
			     "seconds until next tree scrub", "sec",
			     DF_POOL_DIR"/next_tree_scrub", DP_POOL_DIR(ctx));
	if (rc)
		D_WARN("Failed to create scm_next_tree_scrub metric: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&ctx->sc_metrics.scm_busy_time, D_TM_GAUGE,
			     "current seconds scrubber isn't able to proceed because of active IO",
			     "sec",
			     DF_POOL_DIR"/busy_time", DP_POOL_DIR(ctx));
	if (rc)
		D_WARN("Failed to create scm_next_tree_scrub metric: "DF_RC"\n", DP_RC(rc));



	rc = d_tm_add_metric(&ctx->sc_metrics.scm_last_duration,
			     D_TM_DURATION,
			     "The duration the previous tree scrub took", "ms",
			     DF_POOL_DIR"/"M_LAST_DURATION, DP_POOL_DIR(ctx));
	if (rc)
		D_WARN("Failed to create scm_last_duration metric: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&ctx->sc_metrics.scm_csum_calcs,
			     D_TM_COUNTER, "Number of checksums calculated during  "
					   "current tree scrub", NULL,
			     DF_POOL_DIR"/"M_CSUM_COUNTER, DP_POOL_DIR(ctx));
	if (rc)
		D_WARN("Failed to create scm_csum_calcs metric: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&ctx->sc_metrics.scm_csum_calcs_last,
			     D_TM_COUNTER, "Number of checksums calculated in last tree scrub",
			     NULL, DF_POOL_DIR"/"M_CSUM_COUNTER_PREV, DP_POOL_DIR(ctx));
	if (rc)
		D_WARN("Failed to create scm_csum_calcs_last metric: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&ctx->sc_metrics.scm_csum_calcs_total,
			     D_TM_COUNTER, "Total number of checksums scrubbed (since the "
					   "scrubber process started)", NULL,
			     DF_POOL_DIR"/"M_CSUM_COUNTER_TOTAL, DP_POOL_DIR(ctx));
	if (rc)
		D_WARN("Failed to create scm_csum_calcs_total metric: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&ctx->sc_metrics.scm_bytes_scrubbed,
			     D_TM_COUNTER, "Number of bytes scrubbed during the current tree scrub",
			     "bytes",
			     DF_POOL_DIR"/"M_BYTES_SCRUBBED, DP_POOL_DIR(ctx));
	if (rc)
		D_WARN("Failed to create scm_bytes_scrubbed metric: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&ctx->sc_metrics.scm_bytes_scrubbed_last,
			     D_TM_COUNTER, "Number of bytes scrubbed in last tree scrub",
			     "bytes",
			     DF_POOL_DIR"/"M_BYTES_SCRUBBED_PREV, DP_POOL_DIR(ctx));
	if (rc)
		D_WARN("Failed to create scm_bytes_scrubbed_last metric: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&ctx->sc_metrics.scm_bytes_scrubbed_total,
			     D_TM_COUNTER, "Total number of bytes scrubbed bytes (since the "
					   "scrubber process started)",
			     "bytes",
			     DF_POOL_DIR"/"M_BYTES_SCRUBBED_TOTAL, DP_POOL_DIR(ctx));
	if (rc)
		D_WARN("Failed to create scm_bytes_scrubbed_total metric: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&ctx->sc_metrics.scm_corruption,
			     D_TM_COUNTER, "Number of silent data corruption "
					   "detected during current tree scrub",
			     NULL,
			     DF_POOL_DIR"/"M_CSUM_CORRUPTION, DP_POOL_DIR(ctx));
	if (rc)
		D_WARN("Failed to create scm_corruption metric: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&ctx->sc_metrics.scm_corruption_total,
			     D_TM_COUNTER, "Total number of silent data corruption detected (since "
					   "the pool was created)",
			     NULL,
			     DF_POOL_DIR"/"M_CSUM_CORRUPTION_TOTAL,
			     DP_POOL_DIR(ctx));
	if (rc)
		D_WARN("Failed to create scm_corruption_total metric: "DF_RC"\n", DP_RC(rc));
}

static int
drain_pool_target(uuid_t pool_uuid, d_rank_t rank, uint32_t target)
{
	d_rank_list_t			 out_ranks = {0};
	struct pool_target_addr_list	 target_list = {0};
	struct pool_target_addr		 addr = {0};
	int rc;

	D_ERROR("Draining target. rank: %d, target: %d\n", rank, target);

	rc = ds_pool_get_ranks(pool_uuid, PO_COMP_ST_UP | PO_COMP_ST_UPIN | PO_COMP_ST_NEW,
			       &out_ranks);
	if (rc != DER_SUCCESS) {
		D_ERROR("Couldn't get ranks: "DF_RC"\n", DP_RC(rc));
		return rc;
	}
	if (out_ranks.rl_nr == 0 || out_ranks.rl_ranks == NULL) {
		D_ERROR("No ranks to drain to!\n");
		return 0;
	}

	target_list.pta_number = 1;
	addr.pta_rank = rank;
	addr.pta_target = target;
	target_list.pta_addrs = &addr;

	rc = ds_pool_target_update_state(pool_uuid, &out_ranks, &target_list, PO_COMP_ST_DRAIN);
	if (rc != DER_SUCCESS)
		D_ERROR("pool target update status failed: "DF_RC"\n", DP_RC(rc));
	map_ranks_fini(&out_ranks);

	return rc;
}

struct drain_args {
	struct ds_pool *ptd_pool;
	d_rank_t ptd_rank;
	uint32_t ptd_target_id;
};

void
drain_pool_tgt_ult(void *arg)
{
	struct drain_args	*drain_args = arg;
	int			 rc;

	rc = drain_pool_target(drain_args->ptd_pool->sp_uuid, drain_args->ptd_rank,
			       drain_args->ptd_target_id);

	if (rc != 0)
		D_ERROR("Pool target drain failed: "DF_RC"\n", DP_RC(rc));
}

static int
drain_pool_tgt_cb(struct ds_pool *pool)
{
	int			 rc;
	ABT_thread		 thread = ABT_THREAD_NULL;
	struct dss_module_info	*dmi = dss_get_module_info();
	struct drain_args	 drain_args = {
		.ptd_pool = pool,
		.ptd_target_id = dmi->dmi_tgt_id
	};

	rc = crt_group_rank(NULL, &drain_args.ptd_rank);
	if (rc != 0) {
		D_ERROR("Unable to get rank: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	/* Create a ULT to update in xstream 0. */
	rc = dss_ult_create(drain_pool_tgt_ult, &drain_args, DSS_XS_SYS, 0, 0, &thread);

	if (rc != 0) {
		D_ERROR("Error starting ULT: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	ABT_thread_join(thread);
	ABT_thread_free(&thread);

	return 0;
}

static inline bool
is_idle()
{
	return !dss_xstream_is_busy();
}

/** Setup scrubbing context and start scrubbing the pool */
static void
scrubbing_ult(void *arg)
{
	struct scrub_ctx	 ctx = {0};
	struct ds_pool_child	*child = arg;
	uuid_t			 pool_uuid;
	daos_handle_t		 poh;
	int			 rc;

	poh = child->spc_hdl;
	uuid_copy(pool_uuid, child->spc_uuid);

	if (child->spc_scrubbing_req == NULL)
		return;

	uuid_copy(ctx.sc_pool_uuid, pool_uuid);
	ctx.sc_vos_pool_hdl = poh;
	ctx.sc_sleep_fn = sleep_fn;
	ctx.sc_yield_fn = yield_fn;
	ctx.sc_sched_arg = child->spc_scrubbing_req;
	ctx.sc_pool = child->spc_pool;
	ctx.sc_cont_lookup_fn = cont_lookup_cb;
	ctx.sc_cont_put_fn = cont_put_cb;
	ctx.sc_cont_is_stopping_fn = cont_is_stopping_cb;
	ctx.sc_dmi =  dss_get_module_info();
	ctx.sc_drain_pool_tgt_fn = drain_pool_tgt_cb;
	ctx.sc_is_idle_fn = is_idle;

	sc_add_pool_metrics(&ctx);
	while (!dss_ult_exiting(child->spc_scrubbing_req)) {
		uint32_t sleep_time = 1000;

		rc = vos_scrub_pool(&ctx);
		if (rc == -DER_SHUTDOWN) {
			D_ERROR("tgt_id %d shutting down. Scrubber should quit\n",
				ctx.sc_dmi->dmi_tgt_id);
			break;
		}
		if (rc != 0) {
			D_ERROR("Issue with VOS Scrub (tgt_id: %d): "DF_RC"\n",
				ctx.sc_dmi->dmi_tgt_id, DP_RC(rc));
			sleep_time = 60000; /* wait longer if there's an error */
		}

		sched_req_sleep(child->spc_scrubbing_req, sleep_time);
	}
}

int
ds_start_scrubbing_ult(struct ds_pool_child *child)
{
	struct dss_module_info	*dmi = dss_get_module_info();
	struct sched_req_attr	 attr;

	D_ASSERT(child != NULL);
	D_ASSERT(child->spc_scrubbing_req == NULL);

	/** Don't even create the ULT if scrubbing is disabled. */
	if (!scrubbing_is_enabled()) {
		C_TRACE("Checksum scrubbing DISABLED. xs_id: %d, tgt_id: %d, ctx_id: %d\n",
			dmi->dmi_xs_id, dmi->dmi_tgt_id, dmi->dmi_ctx_id);
		return 0;
	}

	C_TRACE("Checksum scrubbing ENABLED. xs_id: %d, tgt_id: %d, ctx_id: %d\n",
		dmi->dmi_xs_id, dmi->dmi_tgt_id, dmi->dmi_ctx_id);

	/* There will be several levels iteration, such as pool, container, object, and lower,
	 * and so on. Let's use DSS_DEEP_STACK_SZ to avoid ULT overflow.
	 */
	sched_req_attr_init(&attr, SCHED_REQ_SCRUB, &child->spc_uuid);
	child->spc_scrubbing_req = sched_create_ult(&attr, scrubbing_ult, child,
						    DSS_DEEP_STACK_SZ);
	if (child->spc_scrubbing_req == NULL) {
		D_ERROR(DF_UUID"[%d]: Failed to create Scrubbing ULT.\n",
			DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);
		return -DER_NOMEM;
	}

	return 0;
}

void
ds_stop_scrubbing_ult(struct ds_pool_child *child)
{
	D_ASSERT(child != NULL);
	/* Scrubbing ULT is not started */
	if (child->spc_scrubbing_req == NULL)
		return;

	sched_req_wait(child->spc_scrubbing_req, true);
	sched_req_put(child->spc_scrubbing_req);
	child->spc_scrubbing_req = NULL;
}
