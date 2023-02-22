/*
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(pool)

#include <daos_srv/vos.h>
#include <daos/mem.h>
#include <daos/pool.h>
#include <daos_prop.h>
#include "srv_internal.h"

#define C_TRACE(...) D_DEBUG(DB_TRACE, __VA_ARGS__)

static int
yield_fn(struct chkpt_ctx *ctx)
{
	sched_req_yield(ctx->cc_sched_arg);

	return 0;
}

static bool
is_idle()
{
	return !dss_xstream_is_busy();
}

static int
wait_fn(struct chkpt_ctx *ctx)
{
	int          rc;

	rc = ABT_eventual_wait(ctx->cc_eventual, NULL);
	if (rc != ABT_SUCCESS)
		rc = dss_abterr2der(rc);

	ABT_eventual_reset(ctx->cc_eventual);
	return rc;
}

static void
wake_fn(struct chkpt_ctx *ctx)
{
	ABT_eventual_set(ctx->cc_eventual, NULL, 0);
}

/** Setup checkpointing context and start checkpointing the pool */
static void
chkpt_ult(void *arg)
{
	struct chkpt_ctx      ctx   = {0};
	struct ds_pool_child *child = arg;
	uuid_t                pool_uuid;
	daos_handle_t         poh;
	int                   rc;

	poh = child->spc_hdl;
	uuid_copy(pool_uuid, child->spc_uuid);

	if (child->spc_chkpt_req == NULL)
		return;

	rc = ABT_eventual_create(0, &ctx.cc_eventual);
	if (rc != ABT_SUCCESS) {
		D_ERROR("Failed to create ABT eventual: %d", dss_abterr2der(rc));
		return;
	}

	uuid_copy(ctx.cc_pool_uuid, pool_uuid);
	ctx.cc_dmi          = dss_get_module_info();
	ctx.cc_vos_pool_hdl = poh;
	ctx.cc_pool         = child->spc_pool;
	ctx.cc_sched_arg    = child->spc_chkpt_req;
	ctx.cc_yield_fn     = yield_fn;
	ctx.cc_wait_fn      = wait_fn;
	ctx.cc_wake_fn      = wake_fn;
	ctx.cc_is_idle_fn   = is_idle;

	while (!dss_ult_exiting(child->spc_chkpt_req)) {
		uint32_t sleep_time = 5000;

		rc = vos_pool_checkpoint(&ctx);
		if (rc == -DER_SHUTDOWN) {
			D_ERROR("tgt_id %d shutting down. Checkpointer should quit\n",
				ctx.cc_dmi->dmi_tgt_id);
			break;
		}
		if (rc != 0) {
			D_ERROR("Issue with VOS checkpoint (tgt_id: %d): " DF_RC "\n",
				ctx.cc_dmi->dmi_tgt_id, DP_RC(rc));
			sleep_time = 60000; /* wait longer if there's an error */
		}

		sched_req_sleep(child->spc_chkpt_req, sleep_time);
	}
	ABT_eventual_free(&ctx.cc_eventual);
}

int
ds_start_chkpt_ult(struct ds_pool_child *child)
{
	struct dss_module_info *dmi = dss_get_module_info();
	struct sched_req_attr   attr;

	D_ASSERT(child != NULL);
	D_ASSERT(child->spc_chkpt_req == NULL);

	/** Only start the ULT if the pool in question is on SSD */
	if (!vos_pool_needs_checkpoint(child->spc_hdl))
		return 0;

	/** We probably need something that runs with higher priority than GC but start with
	 *  that for now.
	 */
	sched_req_attr_init(&attr, SCHED_REQ_GC, &child->spc_uuid);
	child->spc_chkpt_req = sched_create_ult(&attr, chkpt_ult, child, DSS_DEEP_STACK_SZ);
	if (child->spc_chkpt_req == NULL) {
		D_ERROR(DF_UUID "[%d]: Failed to create checkpoint ULT.\n",
			DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);
		return -DER_NOMEM;
	}

	return 0;
}

void
ds_stop_chkpt_ult(struct ds_pool_child *child)
{
	D_ASSERT(child != NULL);
	/* Scrubbing ULT is not started */
	if (child->spc_chkpt_req == NULL)
		return;

	sched_req_wait(child->spc_chkpt_req, true);
	sched_req_put(child->spc_chkpt_req);
	child->spc_chkpt_req = NULL;
}
