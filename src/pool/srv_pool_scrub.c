/*
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pool)

#include <daos_srv/vos.h>
#include <daos_srv/srv_csum.h>
#include "srv_internal.h"

#define C_TRACE(...) D_DEBUG(DB_CSUM, __VA_ARGS__)

/** Hardcoded target interval for scrubber to complete */
#define MSEC_IN_SEC 1000

struct scrub_ctx {
	/**
	 * Pool
	 **/
	uuid_t			 pool_uuid;
	daos_handle_t		 pool_hdl;
	/** pool ULT schedule request. Used for yielding and sleeping */
	struct sched_request	*req;
	/** Number of checksums scrubbed by a single scrubbing iteration */
	daos_size_t		 pool_csums_scrubbed;

	/**
	 * Container
	 **/
	struct ds_cont_child	*cur_cont;

	/** Number of msec between checksum calculations */
	daos_size_t		 msec_between_calcs;
	/** Target number of seconds to scrub entire pool */
	daos_size_t		 interval_sec;
};

static void
sc_reset(struct scrub_ctx *ctx)
{
	ctx->pool_csums_scrubbed = 0;
}

/**
 * The following 3 functions will be replaced by pool/container properties,
 * but for now, to make testing easier using environment variables to configure.
 */
static bool
scrubbing_is_enabled()
{
	char *enabled = getenv("DAOS_CSUM_SCRUB");

	return enabled != NULL && strncmp(enabled, "ON", strlen("ON")) == 0;
}

static uint64_t
interval_sec()
{
	char *sec = getenv("DAOS_CSUM_SCRUB_INTERVAL_SEC");

	return sec != NULL ? atoll(sec) : 7 * 24 * 60 * 60;
}

static uint64_t
between_scrub_sec()
{
	char *sec = getenv("DAOS_CSUM_SCRUB_PAUSE_SEC");

	return sec != NULL ? atoll(sec) : 24 * 60 * 60;
}

/** vos_iter_cb_t */
static int
obj_iter_scrub_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		  vos_iter_type_t type, vos_iter_param_t *param,
		  void *cb_arg, unsigned int *acts)
{
	struct scrub_ctx	*ctx = cb_arg;
	struct daos_csummer	*csummer = ctx->cur_cont->sc_csummer;

	if (!(type == VOS_ITER_RECX || type == VOS_ITER_SINGLE))
		return 0;

	C_TRACE("Scrubbing akey: "DF_KEY", type: %s, record size: "
			DF_U64", extent: "DF_RECX", csum_size: %d\n",
		DP_KEY(&param->ip_akey),
		(type == VOS_ITER_RECX) ? "ARRAY" : "SV",
		entry->ie_rsize,
		DP_RECX(entry->ie_orig_recx),
		daos_csummer_get_csum_len(csummer)
	);

	/** TODO - implement the actual scrubbing of the checksum by fetching
	 * the data, calculating a new checksum, and comparing to the stored
	 * checksum.
	 */
	ctx->pool_csums_scrubbed++;

	if (ctx->msec_between_calcs == 0) {
		C_TRACE("Yield after data scrub\n");
		dss_ult_yield(ctx->req);
	} else {
		C_TRACE("Sleeping after data scrub for "DF_U64" msec\n",
			ctx->msec_between_calcs);
		sched_req_sleep(ctx->req, ctx->msec_between_calcs);
	}

	return 0;
}

static int
scrub_cont(struct scrub_ctx *ctx)
{
	vos_iter_param_t		param = {0};
	struct vos_iter_anchors		anchor = {0};
	int				rc;

	/** Do the actual scrubbing */
	C_TRACE("Scrubbing container '"DF_UUIDF"'\n",
		DP_UUID(ctx->cur_cont->sc_uuid));

	param.ip_hdl = ctx->cur_cont->sc_hdl;
	param.ip_epr.epr_lo = 0;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;

	rc = vos_iterate(&param, VOS_ITER_OBJ, true, &anchor,
			 obj_iter_scrub_cb,
			 NULL, ctx, NULL);

	if (rc != DER_SUCCESS) {
		D_ERROR("Object scrub failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	return 0;
}

/** vos_iter_cb_t */
static int
cont_iter_scrub_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		   vos_iter_type_t type, vos_iter_param_t *param,
		   void *cb_arg, unsigned int *acts)
{
	struct scrub_ctx	*ctx = cb_arg;
	struct ds_cont_child	*cont = NULL;
	int			 rc = 0;

	D_ASSERT(type == VOS_ITER_COUUID);

	/** get per-thread container object which has the csummer */
	rc = ds_cont_child_lookup(ctx->pool_uuid, entry->ie_couuid, &cont);
	if (rc != DER_SUCCESS) {
		D_ERROR("Lookup container '"DF_UUIDF"' failed: "DF_RC"\n",
			DP_UUID(entry->ie_couuid), DP_RC(rc));
		return rc;
	}

	if (daos_csummer_initialized(cont->sc_csummer)) {
		ctx->cur_cont = cont;
		rc = scrub_cont(ctx);
		ctx->cur_cont = NULL;

		C_TRACE("Scrubbed "DF_U64" checksums for pool: "
			DF_UUIDF", cont: "DF_UUIDF"\n",
			ctx->pool_csums_scrubbed, DP_UUID(ctx->pool_uuid),
			DP_UUID(cont->sc_uuid));
	}
	ds_cont_child_put(cont);

	return rc;
}

/**
 * based on numbers from previous scrubbing update the amount of time to sleep
 * between checksum calculations. This will help to spread out the scrubbing
 * processes so that it completes roughly in the desired interval.
 **/
static void
calculate_new_timing(struct scrub_ctx *ctx)
{
	if (ctx->pool_csums_scrubbed > 0) {
		ctx->msec_between_calcs =
			(ctx->interval_sec * 1000) / ctx->pool_csums_scrubbed;

		C_TRACE("With "DF_U64" csums scrubbed, the scrubber"
			" will wait "DF_U64" msec between data scrubbing"
			" so all will be done in roughly "DF_U64" seconds\n",
			ctx->pool_csums_scrubbed,
			ctx->msec_between_calcs,
			ctx->interval_sec);
	}
}

static int
scrub_pool(struct scrub_ctx *ctx)
{
	vos_iter_param_t	param = {0};
	struct vos_iter_anchors	anchor = {0};
	int			rc;

	param.ip_hdl = ctx->pool_hdl;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	rc = vos_iterate(&param, VOS_ITER_COUUID, false, &anchor,
			 NULL, cont_iter_scrub_cb, ctx, NULL);
	if (rc == 0)
		calculate_new_timing(ctx);

	return rc;
}

static void
sc_init(struct scrub_ctx *ctx, struct ds_pool_child *child)
{
	uuid_copy(ctx->pool_uuid, child->spc_uuid);
	ctx->req = child->spc_scrubbing_req;
	ctx->pool_hdl = child->spc_hdl;
	ctx->interval_sec = interval_sec();
}

/** Setup scrubbing context and start scrubbing the pool */
static void
scrubbing_ult(void *arg)
{
	struct ds_pool_child	*child = (struct ds_pool_child *)arg;
	struct dss_module_info	*dmi = dss_get_module_info();
	struct scrub_ctx	 ctx = {0};
	struct timespec		 start, end;
	uint64_t		 sleep_sec;

	C_TRACE("Scrubbing ULT started for pool: "DF_UUIDF"[%d]\n",
		DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);

	if (child->spc_scrubbing_req == NULL)
		return;

	sc_init(&ctx, child);
	sleep_sec = between_scrub_sec();

	while (!dss_ult_exiting(child->spc_scrubbing_req)) {
		if (dss_ult_exiting(child->spc_scrubbing_req))
			break;

		C_TRACE("["DF_UUIDF"] Pool scrubbing started.\n",
			DP_UUID(ctx.pool_uuid));
		d_gettime(&start);
		scrub_pool(&ctx);
		d_gettime(&end);
		struct timespec diff = d_timediff(start, end);

		C_TRACE("["DF_UUIDF"] Pool scrubbing finished.\n"
				"\tChecksums Scrubbed:\t"DF_U64" csums\n"
				"\tTime Spent sec:\t"DF_U64"\n"
				"\tTime Spent ns:\t"DF_U64"\n",
			DP_UUID(ctx.pool_uuid),
			ctx.pool_csums_scrubbed,
			diff.tv_sec,
			diff.tv_nsec);
		sc_reset(&ctx);

		C_TRACE("Waiting "DF_U64" seconds\n", sleep_sec);
		sched_req_sleep(child->spc_scrubbing_req,
				sleep_sec * MSEC_IN_SEC);
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
		C_TRACE("Checksum scrubbing DISABLED. "
			"xs_id: %d, tgt_id: %d, ctx_id: %d, ",
			dmi->dmi_xs_id, dmi->dmi_tgt_id, dmi->dmi_ctx_id);
		return 0;
	}

	C_TRACE("Checksum scrubbing ENABLED. "
		"xs_id: %d, tgt_id: %d, ctx_id: %d, ",
		dmi->dmi_xs_id, dmi->dmi_tgt_id, dmi->dmi_ctx_id);

	/* There will be several levels iteration, such as pool, container, object, and lower,
	 * and so on. Let's use DSS_DEEP_STACK_SZ to avoid ULT overflow.
	 */
	rc = dss_ult_create(scrubbing_ult, child, DSS_XS_SELF, 0, DSS_DEEP_STACK_SZ, &thread);
	if (rc) {
		D_ERROR(DF_UUID"[%d]: Failed to create Scrubbing ULT. %d\n",
			DP_UUID(child->spc_uuid), dmi->dmi_tgt_id, rc);
		return rc;
	}

	D_ASSERT(thread != ABT_THREAD_NULL);

	sched_req_attr_init(&attr, SCHED_REQ_SCRUB, &child->spc_uuid);
	attr.sra_flags = SCHED_REQ_FL_NO_DELAY;
	child->spc_scrubbing_req = sched_req_get(&attr, thread);
	if (child->spc_scrubbing_req == NULL) {
		D_CRIT(DF_UUID"[%d]: Failed to get req for Scrubbing ULT\n",
		       DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);
		DABT_THREAD_FREE(&thread);
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

	C_TRACE(DF_UUID"[%d]: Stopping Scrubbing ULT\n",
		DP_UUID(child->spc_uuid), dss_get_module_info()->dmi_tgt_id);

	sched_req_wait(child->spc_scrubbing_req, true);
	sched_req_put(child->spc_scrubbing_req);
	child->spc_scrubbing_req = NULL;
}
