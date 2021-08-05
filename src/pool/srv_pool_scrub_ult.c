/*
 * (C) Copyright 2020-2021 Intel Corporation.
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
static bool
scrubbing_is_enabled()
{
	char *disabled = getenv("DAOS_CSUM_SCRUB_DISABLED");

	return disabled == NULL;
}

static int
yield_fn(void *arg)
{
	sched_req_yield(arg);

	return 0;
}

static int
sleep_fn(void *arg, uint32_t msec)
{
	sched_req_sleep(arg, msec);

	return 0;
}

struct set_schedule_args {
	struct scrub_ctx	*ssa_ctx;
	uint32_t		 ssa_sched;
	int			 ssa_rc;
};

static void
sc_set_schedule_ult(void *arg)
{
	struct set_schedule_args	*ssa = arg;
	struct daos_prop_entry		*prop_entry;
	daos_prop_t			*props = NULL;

	ds_pool_prop_fetch(ssa->ssa_ctx->sc_pool, DAOS_PO_QUERY_PROP_ALL,
			   &props);
	if (props == NULL) {
		ssa->ssa_rc = -DER_NOMEM;
		return;
	}

	prop_entry = daos_prop_entry_get(props, DAOS_PROP_PO_SCRUB_SCHED);

	if (prop_entry->dpe_val != ssa->ssa_sched) {
		prop_entry->dpe_val = ssa->ssa_sched;
		ssa->ssa_rc = ds_pool_iv_prop_update(ssa->ssa_ctx->sc_pool,
						     props);
	}
	daos_prop_free(props);
}

static int
sc_set_schedule_off(struct scrub_ctx *ctx)
{
	struct set_schedule_args ssa = {0};
	ABT_thread		 thread = ABT_THREAD_NULL;
	int			 rc;

	/*
	 * Sleep for a few seconds to make sure that all pool targets
	 * have had a chance to start scrubbing before turning back
	 * off. Because each pool target shares the same pool
	 * properties, if one pool target doesn't have anything to scrub
	 * and finishes very quickly, it could turn off scrubbing even
	 * before the other targets could start. This helps prevent that
	 * situation.
	 */
	sched_req_sleep(ctx->sc_sched_arg, 5000);
	D_DEBUG(DB_CSUM, DF_PTGT": Turning off scrubbing.\n",
		DP_PTGT(ctx->sc_pool_uuid, dss_get_module_info()->dmi_tgt_id));

	ssa.ssa_ctx = ctx;
	ssa.ssa_sched = DAOS_SCRUB_SCHED_OFF;

	/* Create a ULT to call ds_pool_iv_prop_update() in xstream 0. */
	rc = dss_ult_create(sc_set_schedule_ult, &ssa, DSS_XS_SYS, 0, 0,
			    &thread);
	if (rc != 0)
		return rc;

	ABT_thread_join(thread);
	ABT_thread_free(&thread);
	if (ssa.ssa_rc != 0)
		D_WARN("Pool property DAOS_PROP_PO_SCRUB_SCHED was not "
		       "updated. Error: "DF_RC"\n", DP_RC(ssa.ssa_rc));
	return ssa.ssa_rc;
}

static int
sc_get_schedule(struct scrub_ctx *ctx)
{
	return ctx->sc_pool->sp_scrub_sched;
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

	ds_cont_child_put(cont_child);

	return 0;
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
	int			 schedule;
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
	ctx.sc_status = SCRUB_STATUS_NOT_RUNNING;
	ctx.sc_credits_left = ctx.sc_pool->sp_scrub_cred;

	while (!dss_ult_exiting(child->spc_scrubbing_req)) {
		if (dss_ult_exiting(child->spc_scrubbing_req))
			break;

		schedule = sc_get_schedule(&ctx);
		if (schedule != DAOS_SCRUB_SCHED_OFF) {
			C_TRACE(DF_PTGT": Pool Scrubbing started\n",
				DP_PTGT(pool_uuid, tgt_id));
			ds_scrub_pool(&ctx);
			if (schedule == DAOS_SCRUB_SCHED_RUN_ONCE)
				sc_set_schedule_off(&ctx);
		}

		ds_scrub_sched_control(&ctx);
	}
}

/** Setup and create the scrubbing ult */
int
ds_start_scrubbing_ult(struct ds_pool_child *child)
{
	struct dss_module_info	*dmi = dss_get_module_info();
	struct sched_req_attr	 attr;
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
	attr.sra_flags = SCHED_REQ_FL_NO_DELAY;
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
