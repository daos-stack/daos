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

struct chkpt_ctx {
	struct dss_module_info *cc_dmi;
	uuid_t                  cc_pool_uuid;
	daos_handle_t           cc_vos_pool_hdl;
	struct ds_pool         *cc_pool; /* Used to get properties */
	struct umem_store      *cc_store;
	uint64_t                cc_commit_id;
	uint64_t                cc_wait_id;
	void                   *cc_sched_arg;
	ABT_eventual            cc_eventual;
	uint32_t                cc_max_used_blocks;
	uint32_t                cc_used_blocks;
	uint32_t                cc_total_blocks;
	uint32_t                cc_saved_thresh;
	uint32_t                cc_sleeping : 1, cc_waiting : 1;
};

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

	ctx->cc_waiting = 1;
	rc = ABT_eventual_wait(ctx->cc_eventual, NULL);
	if (rc != ABT_SUCCESS)
		rc = dss_abterr2der(rc);

	ABT_eventual_reset(ctx->cc_eventual);
	return rc;
}

static void
wake_fn(struct chkpt_ctx *ctx)
{
	ctx->cc_waiting = 0;
	ABT_eventual_set(ctx->cc_eventual, NULL, 0);
}

static void
wait_cb(void *arg, uint64_t chkpt_tx, uint64_t *committed_tx)
{
	struct chkpt_ctx  *ctx   = arg;
	struct umem_store *store = ctx->cc_store;

	if (store->stor_ops->so_wal_id_cmp(store, chkpt_tx, ctx->cc_commit_id) <= 0) {
		/** Sometimes we may need to yield here to make progress such as when we need
		 *  more DMA buffers to prepare entries.
		 */
		if (!is_idle())
			yield_fn(ctx);
		goto done;
	}

	ctx->cc_wait_id = chkpt_tx;
	wait_fn(ctx);
done:
	*committed_tx = ctx->cc_commit_id;
}

static void
update_cb(void *arg, uint64_t id, uint32_t used_blocks, uint32_t total_blocks)
{
	struct chkpt_ctx  *ctx   = arg;
	struct umem_store *store = ctx->cc_store;

	ctx->cc_used_blocks  = used_blocks;
	ctx->cc_total_blocks = total_blocks;
	ctx->cc_commit_id    = id;

	if (ctx->cc_sleeping) {
		/** the ULT not executing a checkpoint but sleeping waiting for either a timeout
		 *  or a size-based trigger.
		 */
		if (ctx->cc_used_blocks > ctx->cc_max_used_blocks)
			sched_req_wakeup(ctx->cc_sched_arg);
		return;
	}

	if (!ctx->cc_waiting)
		return;

	if (store->stor_ops->so_wal_id_cmp(store, id, ctx->cc_wait_id) >= 0) {
		wake_fn(ctx);
		ctx->cc_waiting = 0;
	}
}

/** Returns true if we should trigger a checkpoint.  Otherwise, it sleeps for some interval and
 *  returns false.
 */
static bool
need_checkpoint(struct ds_pool_child *child, struct chkpt_ctx *ctx, uint64_t *start)
{
	uint32_t        sleep_time = 60000; /* Set default to 60 seconds */
	uint64_t        elapsed;
	struct ds_pool *pool = child->spc_pool;

	if (pool->sp_checkpoint_mode == DAOS_CHECKPOINT_DISABLED) {
		*start = daos_getmtime_coarse();
		goto do_sleep;
	}

	if (pool->sp_checkpoint_thresh != ctx->cc_saved_thresh) {
		/** Recalculate the checkpoint max */
		ctx->cc_saved_thresh    = pool->sp_checkpoint_thresh;
		ctx->cc_max_used_blocks = (ctx->cc_total_blocks * ctx->cc_saved_thresh) / 100;
	}

	if (ctx->cc_used_blocks > ctx->cc_max_used_blocks)
		return true;

	if (pool->sp_checkpoint_mode == DAOS_CHECKPOINT_LAZY) {
		*start = daos_getmtime_coarse();
		goto do_sleep;
	}

	sleep_time = 1000 * pool->sp_checkpoint_freq;
	if (*start == 0) {
		*start = daos_getmtime_coarse();
		goto do_sleep;
	}
	/** If we've awoken from a prior sleep, we either have slept for the appropriate time or we
	 *  have been woken by another trigger, such as a change in checkpoint properties.  As
	 *  such, we need to check to see if we've actually slept the expected amount of time before
	 *  triggering a checkpoint.
	 */
	elapsed = daos_getmtime_coarse() - *start;
	if (elapsed >= sleep_time)
		return true;

	sleep_time -= elapsed;
do_sleep:
	D_DEBUG(DB_IO,
		"Checkpoint ULT to sleep for %d ms. Used blocks %d/%d, threshold=%d, mode=%s\n",
		sleep_time, ctx->cc_used_blocks, ctx->cc_total_blocks, ctx->cc_max_used_blocks,
		pool->sp_checkpoint_mode == DAOS_CHECKPOINT_TIMED
		    ? "timed"
		    : (pool->sp_checkpoint_mode == DAOS_CHECKPOINT_LAZY ? "lazy" : "disabled"));
	ctx->cc_sleeping = 1;
	sched_req_sleep(child->spc_chkpt_req, sleep_time);
	ctx->cc_sleeping = 0;

	return false;
}

/** Setup checkpointing context and start checkpointing the pool */
static void
chkpt_ult(void *arg)
{
	struct chkpt_ctx      ctx   = {0};
	struct ds_pool_child *child = arg;
	uuid_t                pool_uuid;
	daos_handle_t         poh;
	uint64_t              start = 0;
	int                   rc;

	poh = child->spc_hdl;
	uuid_copy(pool_uuid, child->spc_uuid);

	if (child->spc_chkpt_req == NULL)
		return;

	rc = ABT_eventual_create(0, &ctx.cc_eventual);
	if (rc != ABT_SUCCESS) {
		D_ERROR("Failed to create ABT eventual: %d\n", dss_abterr2der(rc));
		return;
	}

	uuid_copy(ctx.cc_pool_uuid, pool_uuid);
	ctx.cc_dmi          = dss_get_module_info();
	ctx.cc_vos_pool_hdl = poh;
	ctx.cc_pool         = child->spc_pool;
	ctx.cc_sched_arg    = child->spc_chkpt_req;

	vos_pool_checkpoint_init(poh, update_cb, wait_cb, &ctx, &ctx.cc_store);

	while (!dss_ult_exiting(child->spc_chkpt_req)) {
		if (!need_checkpoint(child, &ctx, &start))
			continue;

		rc = vos_pool_checkpoint(poh);
		if (rc == -DER_SHUTDOWN) {
			D_ERROR("tgt_id %d shutting down. Checkpointer should quit\n",
				ctx.cc_dmi->dmi_tgt_id);
			break;
		}
		if (rc != 0) {
			D_ERROR("Issue with VOS checkpoint (tgt_id: %d): " DF_RC "\n",
				ctx.cc_dmi->dmi_tgt_id, DP_RC(rc));
		}
		start = 0;
	}
	vos_pool_checkpoint_fini(poh);
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
	/* Checkpoint ULT is not started */
	if (child->spc_chkpt_req == NULL)
		return;

	sched_req_wait(child->spc_chkpt_req, true);
	sched_req_put(child->spc_chkpt_req);
	child->spc_chkpt_req = NULL;
}
