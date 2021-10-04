/*
 * (C) Copyright 2021 Intel Corporation.
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

#define DF_PTGT DF_UUID"[%d]"
#define DP_PTGT(uuid, tgt) DP_UUID(uuid), tgt

/*
 * DAOS_CSUM_SCRUB_DISABLED can be set in the server config to disable the
 * scrubbing ULT completely for the engine.
 */
static inline bool
scrubbing_is_enabled()
{
	char *disabled = getenv("DAOS_CSUM_SCRUB_DISABLED");

	return disabled == NULL;
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
		D_ERROR("failed to get cont child: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	cont->scs_cont_csummer = cont_child->sc_csummer;
	cont->scs_cont_hdl = cont_child->sc_hdl;
	uuid_copy(cont->scs_cont_uuid, cont_uuid);
	cont->scs_cont_src = cont_child;

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
	d_tm_add_metric(&ctx->sc_metrics.scm_start,
			D_TM_TIMESTAMP,
			"When the current scrubbing started", NULL,
			DF_POOL_DIR"/"M_STARTED, DP_POOL_DIR(ctx));
	d_tm_add_metric(&ctx->sc_metrics.scm_end, D_TM_TIMESTAMP, "", "",
			DF_POOL_DIR"/"M_ENDED, DP_POOL_DIR(ctx));
	d_tm_add_metric(&ctx->sc_metrics.scm_pool_ult_wait_time, D_TM_GAUGE,
			"How long waiting between checksum calculations", "ms",
			DF_POOL_DIR"/sleep", DP_POOL_DIR(ctx));
	d_tm_add_metric(&ctx->sc_metrics.scm_last_duration,
			D_TM_DURATION,
			"How long the previous scrub took", "ms",
			DF_POOL_DIR"/"M_LAST_DURATION, DP_POOL_DIR(ctx));
	d_tm_add_metric(&ctx->sc_metrics.scm_csum_calcs,
			D_TM_COUNTER, "Number of checksums calculated for "
				      "current scan",
			NULL,
			DF_POOL_DIR"/"M_CSUM_COUNTER, DP_POOL_DIR(ctx));
	d_tm_add_metric(&ctx->sc_metrics.scm_last_csum_calcs,
			D_TM_COUNTER, "Number of checksums calculated in last "
				      "scan", NULL,
			DF_POOL_DIR"/"M_CSUM_PREV_COUNTER,
			DP_POOL_DIR(ctx));
	d_tm_add_metric(&ctx->sc_metrics.scm_total_csum_calcs,
			D_TM_COUNTER, "Total number of checksums calculated",
			NULL,
			DF_POOL_DIR"/"M_CSUM_TOTAL_COUNTER, DP_POOL_DIR(ctx));
	d_tm_add_metric(&ctx->sc_metrics.scm_corruption,
			D_TM_COUNTER, "Number of silent data corruption "
				      "detected during current scan",
			NULL,
			DF_POOL_DIR"/"M_CSUM_CORRUPTION, DP_POOL_DIR(ctx));
	d_tm_add_metric(&ctx->sc_metrics.scm_total_corruption,
			D_TM_COUNTER, "Total number of silent data corruption "
				      "detected",
			NULL,
			DF_POOL_DIR"/"M_CSUM_TOTAL_CORRUPTION,
			DP_POOL_DIR(ctx));
}

/** Setup scrubbing context and start scrubbing the pool */
static void
scrubbing_ult(void *arg)
{
	struct scrub_ctx	 ctx = {0};
	struct ds_pool_child	*child = arg;
	struct dss_module_info	*dmi = dss_get_module_info();
	uuid_t			 pool_uuid;
	daos_handle_t		 poh;
	int			 tgt_id;

	poh = child->spc_hdl;
	uuid_copy(pool_uuid, child->spc_uuid);
	tgt_id = dmi->dmi_tgt_id;

	C_TRACE(DF_PTGT": Scrubbing ULT started\n", DP_PTGT(pool_uuid, tgt_id));

	D_ASSERT(child->spc_scrubbing_req != NULL);

	uuid_copy(ctx.sc_pool_uuid, pool_uuid);
	ctx.sc_vos_pool_hdl = poh;
	ctx.sc_sleep_fn = sleep_fn;
	ctx.sc_yield_fn = yield_fn;
	ctx.sc_sched_arg = child->spc_scrubbing_req;
	ctx.sc_pool = child->spc_pool;
	ctx.sc_cont_lookup_fn = cont_lookup_cb;
	ctx.sc_cont_put_fn = cont_put_cb;
	ctx.sc_cont_is_stopping_fn = cont_is_stopping_cb;
	ctx.sc_status = SCRUB_STATUS_NOT_RUNNING;
	ctx.sc_credits_left = ctx.sc_pool->sp_scrub_cred;
	ctx.sc_dmi =  dss_get_module_info();

	sc_add_pool_metrics(&ctx);
	while (!dss_ult_exiting(child->spc_scrubbing_req)) {
		vos_scrub_pool(&ctx);
	}
}

/** Setup and create the scrubbing ult */
int
ds_start_scrubbing_ult(struct ds_pool_child *child)
{
	struct dss_module_info	*dmi = dss_get_module_info();
	struct sched_req_attr	 attr = {0};
	ABT_thread		 thread = ABT_THREAD_NULL;
	int			 rc;

	D_ASSERT(child != NULL);
	D_ASSERT(child->spc_scrubbing_req == NULL);

	/** Don't even create the ULT if scrubbing is disabled. */
	if (!scrubbing_is_enabled()) {
		C_TRACE(DF_PTGT": Checksum scrubbing DISABLED.\n",
			DP_PTGT(child->spc_uuid, dmi->dmi_tgt_id));
		return 0;
	}

	C_TRACE(DF_PTGT": Checksum scrubbing Enabled. Creating ULT.\n",
		DP_PTGT(child->spc_uuid, dmi->dmi_tgt_id));

	rc = dss_ult_create(scrubbing_ult, child, DSS_XS_SELF, 0, 0,
			    &thread);
	if (rc) {
		D_ERROR(DF_PTGT": Failed to create Scrubbing ULT. "DF_RC"\n",
			DP_PTGT(child->spc_uuid, dmi->dmi_tgt_id), DP_RC(rc));
		return rc;
	}

	D_ASSERT(thread != ABT_THREAD_NULL);

	sched_req_attr_init(&attr, SCHED_REQ_SCRUB, &child->spc_uuid);
	child->spc_scrubbing_req = sched_req_get(&attr, thread);
	if (child->spc_scrubbing_req == NULL) {
		D_CRIT(DF_PTGT": Failed to get req for Scrubbing ULT\n",
		       DP_PTGT(child->spc_uuid, dmi->dmi_tgt_id));
		ABT_thread_join(thread);
		return -DER_NOMEM;
	}

	return 0;
}

void
ds_stop_scrubbing_ult(struct ds_pool_child *child)
{
	struct dss_module_info *dmi = dss_get_module_info();

	D_ASSERT(child != NULL);
	/* Scrubbing ULT is not started */
	if (child->spc_scrubbing_req == NULL)
		return;

	C_TRACE(DF_PTGT": Stopping Scrubbing ULT\n",
		DP_PTGT(child->spc_uuid, dmi->dmi_tgt_id));

	sched_req_wait(child->spc_scrubbing_req, true);
	sched_req_put(child->spc_scrubbing_req);
	child->spc_scrubbing_req = NULL;
}
