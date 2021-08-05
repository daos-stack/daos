/*
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(csum)

#include <daos_srv/vos.h>
#include <daos_srv/srv_csum.h>
#include "srv_internal.h"

#define C_TRACE(...) D_DEBUG(DB_CSUM, __VA_ARGS__)

static void
sc_pool_csum_calc_inc(struct scrub_ctx *ctx)
{
	ctx->sc_pool_csum_calcs++;
}

static struct daos_csummer *
sc_csummer(const struct scrub_ctx *ctx)
{
	return ctx->sc_cont.scs_cont_csummer;
}

static uint32_t
sc_chunksize(const struct scrub_ctx *ctx)
{
	return daos_csummer_get_rec_chunksize(sc_csummer(ctx),
					      ctx->sc_iod.iod_size);
}

static void
sc_yield(struct scrub_ctx *ctx)
{
	if (ctx->sc_yield_fn)
		ctx->sc_yield_fn(ctx->sc_sched_arg);
}

static void
sc_sleep(struct scrub_ctx *ctx, uint32_t ms)
{
	if (ctx->sc_sleep_fn)
		ctx->sc_sleep_fn(ctx->sc_sched_arg, ms);
}

static int
sc_get_schedule(struct scrub_ctx *ctx)
{
	return ctx->sc_pool->sp_scrub_sched;
}

/**
 * Get the number of records in the chunk at index 'i' of the current recx
 * set within the scrubbing context
 */
static daos_size_t
sc_get_rec_in_chunk_at_idx(const struct scrub_ctx *ctx, uint32_t i)
{
	daos_recx_t		*recx;
	daos_size_t		 rec_len;
	uint32_t		 chunksize;
	struct daos_csum_range	 range;

	recx = &ctx->sc_iod.iod_recxs[0];
	rec_len = ctx->sc_iod.iod_size;
	chunksize = sc_chunksize(ctx);
	range = csum_recx_chunkidx2range(recx, rec_len, chunksize, i);

	return range.dcr_nr;
}

/**
 * Will verify the checksum(s) for the current recx. It will do it one chunk
 * at a time instead of all at once so that it can yield/sleep between each
 * calculation.
 */
static int
sc_verify_recx(struct scrub_ctx *ctx, d_sg_list_t *sgl)
{
	daos_key_t		 chunk_iov = {0};
	uint8_t			*csum_buf = NULL;
	d_iov_t			*data;
	daos_recx_t		*recx;
	daos_size_t		 rec_len;
	daos_size_t		 processed_bytes = 0;
	uint32_t		 i;
	uint32_t		 chunksize;
	uint32_t		 csum_nr;
	int			 rc = 0;
	uint16_t		 csum_len;

	D_ASSERT(ctx->sc_iod.iod_nr == 1);
	D_ASSERT(sgl != NULL && sgl->sg_nr_out == 1);
	D_ASSERT(ctx->sc_iod.iod_recxs != NULL);

	data = &sgl->sg_iovs[0];
	recx = &ctx->sc_iod.iod_recxs[0];
	rec_len = ctx->sc_iod.iod_size;
	chunksize = sc_chunksize(ctx);
	csum_nr = daos_recx_calc_chunks(*recx, rec_len, chunksize);
	csum_len = daos_csummer_get_csum_len(sc_csummer(ctx));

	/** Create a buffer to calculate the checksum into */
	D_ALLOC(csum_buf, csum_len);

	/**
	 * loop through each checksum and chunk of the recx based
	 * on chunk size.
	 */
	for (i = 0; i < csum_nr; i++) {
		uint8_t		*orig_csum = NULL;
		bool		 match;
		daos_size_t	 rec_in_chunk;

		orig_csum = ci_idx2csum(ctx->sc_csum_to_verify, i);
		rec_in_chunk = sc_get_rec_in_chunk_at_idx(ctx, i);

		/** set an iov with just the data for the current chunk */
		d_iov_set(&chunk_iov, data->iov_buf + processed_bytes,
			  rec_in_chunk * rec_len);
		D_ASSERT(processed_bytes + chunk_iov.iov_len <= data->iov_len);

		rc = daos_csummer_calc_for_iov(sc_csummer(ctx), &chunk_iov,
					       csum_buf, csum_len);
		if (rc != 0) {
			D_ERROR("daos_csummer_calc_for_iov error: "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(done, rc);
		}

		match = daos_csummer_csum_compare(sc_csummer(ctx), orig_csum,
						  csum_buf, csum_len);

		if (!match) {
			D_ERROR("Corruption found for chunk #%d of recx: "
					DF_RECX"\n", i, DP_RECX(*recx));
			D_GOTO(done, rc = -DER_CSUM);
		}

		processed_bytes += chunk_iov.iov_len;
		sc_pool_csum_calc_inc(ctx);
		ds_scrub_sched_control(ctx);
	}

done:
	D_FREE(csum_buf);

	return rc;
}

static int
sc_verify_sv(struct scrub_ctx *ctx, d_sg_list_t *sgl)
{
	int rc;

	sc_pool_csum_calc_inc(ctx);
	rc = daos_csummer_verify_key(sc_csummer(ctx), &sgl->sg_iovs[0],
				     ctx->sc_csum_to_verify);
	ds_scrub_sched_control(ctx);
	return rc;
}

static uuid_t *
sc_cont_uuid(struct scrub_ctx *ctx)
{
	return &ctx->sc_cont.scs_cont_uuid;
}

static daos_handle_t
sc_cont_hdl(struct scrub_ctx *ctx)
{
	return ctx->sc_cont.scs_cont_hdl;
}

static void
sc_raise_ras(struct scrub_ctx *ctx)
{
	if (ds_notify_ras_event != NULL) {
		ds_notify_ras_event(RAS_CORRUPTION_DETECTED,
				    "Data corruption detected",
				    RAS_TYPE_INFO,
				    RAS_SEV_ERROR, NULL, NULL, NULL, NULL,
				    &ctx->sc_pool_uuid, sc_cont_uuid(ctx),
				    NULL, NULL, NULL);
	}
}

static int
sc_mark_corrupt(struct scrub_ctx *ctx)
{
	return vos_iter_process(ctx->sc_vos_iter_handle,
				VOS_ITER_PROC_OP_MARK_CORRUPT, NULL);
}

static int
sc_verify_obj_value(struct scrub_ctx *ctx)
{
	d_sg_list_t	 sgl;
	daos_iod_t	*iod = &ctx->sc_iod;
	uint64_t	 data_len;
	int		 rc;

	D_DEBUG(DB_CSUM, "Scrubbing iod: "DF_C_IOD"\n", DP_C_IOD(iod));
	/*
	 * Know that there will always only be 1 recx because verifying a
	 * single extent at a time so use first recx in iod for data_len
	 * calculation
	 */
	data_len = iod->iod_type == DAOS_IOD_ARRAY ?
		   iod->iod_recxs[0].rx_nr * iod->iod_size :
		   iod->iod_size;
	/* allocate memory to fetch data into */
	d_sgl_init(&sgl, 1);
	D_ALLOC(sgl.sg_iovs[0].iov_buf, data_len);
	sgl.sg_iovs[0].iov_buf_len = data_len;
	sgl.sg_iovs[0].iov_len = data_len;

	/* Fetch data */
	rc = vos_obj_fetch(sc_cont_hdl(ctx), ctx->sc_cur_oid,
			   ctx->sc_epoch, 0,
			   &ctx->sc_dkey, 1, iod, &sgl);

	if (rc == -DER_CSUM) {
		/* Already know this is corrupt so just return */
		D_GOTO(out, rc = DER_SUCCESS);
	} else if (rc != 0) {
		D_WARN("Unable to fetch data for scrubber");
		D_GOTO(out, rc);
	}

	/*
	 * if value was deleted while scrubbing, fetch will return no data.
	 * Just skip it
	 */
	if (sgl.sg_nr_out == 0)
		D_GOTO(out, rc);

	rc = iod->iod_type == DAOS_IOD_ARRAY ?
	     sc_verify_recx(ctx, &sgl) :
	     sc_verify_sv(ctx, &sgl);

	if (rc == -DER_CSUM) {
		D_WARN("Checksum scrubber found corruption");
		sc_raise_ras(ctx);
		rc = sc_mark_corrupt(ctx);
	}

out:
	D_FREE(sgl.sg_iovs[0].iov_buf);
	d_sgl_fini(&sgl, true);

	return rc;
}

static void
sc_obj_val_setup(struct scrub_ctx *ctx, vos_iter_entry_t *entry,
		 vos_iter_type_t type, vos_iter_param_t *param,
		 daos_handle_t ih)
{
	ctx->sc_cur_oid = param->ip_oid;
	ctx->sc_dkey = param->ip_dkey;
	ctx->sc_epoch = entry->ie_epoch;

	ctx->sc_iod.iod_size = entry->ie_rsize;
	ctx->sc_iod.iod_nr = 1;
	ctx->sc_iod.iod_type = type == VOS_ITER_RECX ? DAOS_IOD_ARRAY :
			       DAOS_IOD_SINGLE;
	ctx->sc_iod.iod_name = param->ip_akey;
	ctx->sc_iod.iod_recxs = &entry->ie_recx;

	ctx->sc_csum_to_verify = &entry->ie_csum;

	ctx->sc_vos_iter_handle = ih;
}

static bool
oids_are_same(daos_unit_oid_t a, daos_unit_oid_t b)
{
	return daos_unit_oid_compare(a, b) == 0;
}

static bool
keys_are_same(daos_key_t key1, daos_key_t key2)
{
	if (key1.iov_len != key2.iov_len)
		return 0;

	return memcmp(key1.iov_buf, key2.iov_buf, key1.iov_len) == 0;
}

static bool
epoch_is_same(daos_epoch_t a, daos_epoch_t b)
{
	return a == b;
}

/** vos_iter_cb_t */
static int
obj_iter_scrub_pre_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		  vos_iter_type_t type, vos_iter_param_t *param,
		  void *cb_arg, unsigned int *acts)
{
	struct scrub_ctx	*ctx = cb_arg;
	int			 rc;

	if (ctx->sc_pool->sp_scrub_sched == DAOS_SCRUB_SCHED_OFF) {
		C_TRACE("scrubbing is off now, aborting ...");
		*acts |= VOS_ITER_CB_ABORT;
		return 0;
	}

	switch (type) {
	case VOS_ITER_OBJ:
		if (oids_are_same(ctx->sc_cur_oid, entry->ie_oid)) {
			*acts |= VOS_ITER_CB_SKIP;
			memset(&ctx->sc_iod, 0, sizeof(ctx->sc_iod));
		} else {
			ctx->sc_cur_oid = entry->ie_oid;
		}
		break;
	case VOS_ITER_DKEY:
		if (keys_are_same(ctx->sc_dkey, entry->ie_key)) {
			*acts |= VOS_ITER_CB_SKIP;
			memset(&ctx->sc_dkey, 0, sizeof(ctx->sc_dkey));
		} else {
			ctx->sc_dkey = param->ip_dkey;
		}
		break;
	case VOS_ITER_AKEY:
		if (keys_are_same(ctx->sc_iod.iod_name, entry->ie_key)) {
			*acts |= VOS_ITER_CB_SKIP;
			memset(&ctx->sc_iod, 0, sizeof(ctx->sc_iod));
		} else {
			ctx->sc_iod.iod_name = param->ip_akey;
		}
		break;
	case VOS_ITER_SINGLE:
	case VOS_ITER_RECX: {
		if (epoch_is_same(ctx->sc_epoch, entry->ie_epoch)) {
			*acts |= VOS_ITER_CB_SKIP;
			ctx->sc_epoch = 0;
		} else {
			C_TRACE("Scrubbing akey: "DF_KEY", type: %s, rec size: "
					DF_U64", extent: "DF_RECX"\n",
				DP_KEY(&param->ip_akey),
				(type == VOS_ITER_RECX) ? "ARRAY" : "SV",
				entry->ie_rsize,
				DP_RECX(entry->ie_orig_recx)
			);

			ctx->sc_iod.iod_size = entry->ie_rsize;
			ctx->sc_iod.iod_nr = 1;
			ctx->sc_iod.iod_recxs = &entry->ie_recx;
			ctx->sc_iod.iod_type = type == VOS_ITER_RECX ?
					       DAOS_IOD_ARRAY :
					       DAOS_IOD_SINGLE;
			sc_obj_val_setup(ctx, entry, type, param, ih);

			rc = sc_verify_obj_value(ctx);
			*acts |= VOS_ITER_CB_YIELD;

			if (rc != 0) {
				D_ERROR("Error Verifying:"DF_RC"\n", DP_RC(rc));
				return rc;
			}
			ds_scrub_sched_control(ctx);
		}
		break;
	}

	case VOS_ITER_DTX:
	case VOS_ITER_COUUID:
	case VOS_ITER_NONE:
		D_ASSERTF(0, "Invalid.");
	}

	return 0;
}

static int
sc_scrub_cont(struct scrub_ctx *ctx)
{
	vos_iter_param_t		param = {0};
	struct vos_iter_anchors		anchor = {0};
	int				rc;

	/* not all containers in the pool will have checksums enabled */
	if (!daos_csummer_initialized(sc_csummer(ctx)))
		return 0;

	C_TRACE("Scrubbing container '"DF_UUIDF"'\n",
		DP_UUID(*sc_cont_uuid(ctx)));

	param.ip_hdl = sc_cont_hdl(ctx);
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	rc = vos_iterate(&param, VOS_ITER_OBJ, true, &anchor,
			 obj_iter_scrub_pre_cb,
			 NULL, ctx, NULL);

	if (rc != DER_SUCCESS) {
		D_ERROR("Object scrub failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	ds_scrub_sched_control(ctx);

	return 0;
}

static int
sc_cont_setup(struct scrub_ctx *ctx, vos_iter_param_t *param,
	      vos_iter_entry_t *entry)
{
	int			 rc;

	if (ctx->sc_cont_lookup_fn == NULL)
		return -DER_NOSYS;
	rc = ctx->sc_cont_lookup_fn(ctx->sc_pool_uuid, entry->ie_couuid,
				    ctx->sc_sched_arg, &ctx->sc_cont);

	if (rc != 0) {
		D_ERROR("Error opening vos container: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	return 0;
}

static void
sc_cont_teardown(struct scrub_ctx *ctx)
{
	memset(&ctx->sc_cont, 0, sizeof(ctx->sc_cont));
}

/** vos_iter_cb_t */
static int
cont_iter_scrub_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		   vos_iter_type_t type, vos_iter_param_t *param,
		   void *cb_arg, unsigned int *acts)
{
	struct scrub_ctx	*ctx = cb_arg;
	int			 rc = 0;

	D_ASSERT(type == VOS_ITER_COUUID);

	rc = sc_cont_setup(ctx, param, entry);
	if (rc != 0)
		return rc;

	D_DEBUG(DB_CSUM, "Scrubbing container: "DF_UUID"\n",
		DP_UUID(ctx->sc_cont.scs_cont_uuid));

	rc = sc_scrub_cont(ctx);

	sc_cont_teardown(ctx);

	return rc;
}

static void
sc_pool_start(struct scrub_ctx *ctx)
{
	ctx->sc_pool_last_csum_calcs = ctx->sc_pool_csum_calcs;
	ctx->sc_pool_csum_calcs = 0;
	d_gettime(&ctx->sc_pool_start_scrub);
	ctx->sc_status = SCRUB_STATUS_RUNNING;
}

int
ds_scrub_pool(struct scrub_ctx *ctx)
{
	vos_iter_param_t	param = {0};
	struct vos_iter_anchors	anchor = {0};
	int			rc;

	if (daos_handle_is_inval(ctx->sc_vos_pool_hdl)) {
		D_ERROR("vos_iter_handle is invalid.\n");
		return -DER_INVAL;
	}

	sc_pool_start(ctx);

	param.ip_hdl = ctx->sc_vos_pool_hdl;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	rc = vos_iterate(&param, VOS_ITER_COUUID, false, &anchor,
			 NULL, cont_iter_scrub_cb, ctx, NULL);

	return rc;
}

/* How many ms to wait between checksum calculations (for continuous spacing) */
uint64_t
ds_scrub_wait_between_msec(uint32_t sched, struct timespec start_time,
			   uint64_t last_csum_calcs, uint64_t freq_seconds)
{
	struct timespec	elapsed;
	uint64_t	elapsed_sec;

	if (sched != DAOS_SCRUB_SCHED_CONTINUOUS)
		return 0;

	elapsed = d_time_elapsed(start_time);

	elapsed_sec = d_time2s(elapsed);

	if (elapsed_sec >= freq_seconds)
		return 0; /* don't wait in between anymore */

	freq_seconds -= elapsed_sec;

	/*
	 * overflow protection - if freq seconds is this large just assume
	 * it's infinite anyway and don't really need to convert to ms
	 */
	if (freq_seconds * 1000 > freq_seconds)
		freq_seconds *= 1000;

	return freq_seconds / last_csum_calcs;
}

static void
sc_credit_decrement(struct scrub_ctx *ctx)
{
	ctx->sc_credits_left--;
	C_TRACE("credits now: %d\n", ctx->sc_credits_left);
}

static void
sc_credit_reset(struct scrub_ctx *ctx)
{
	if (ctx->sc_credits_left == 0)
		ctx->sc_credits_left = ctx->sc_pool->sp_scrub_cred;
	C_TRACE("credits now: %d\n", ctx->sc_credits_left);

}

static bool
sc_no_yield(struct scrub_ctx *ctx)
{
	return sc_get_schedule(ctx) == DAOS_SCRUB_SCHED_RUN_ONCE_NO_YIELD;
}

static void
sc_control_in_between(struct scrub_ctx *ctx)
{
	uint64_t msec_between = 0;

	if (ctx->sc_credits_left == 0) {
		sc_credit_reset(ctx);
		return;
	}

	sc_credit_decrement(ctx);
	if (ctx->sc_credits_left > 0) {
		C_TRACE("Still have %d credits\n", ctx->sc_credits_left);
		return;
	}

	C_TRACE("Credits expired, will yield/sleep\n");

	if (sc_get_schedule(ctx) == DAOS_SCRUB_SCHED_CONTINUOUS &&
	    ctx->sc_pool_last_csum_calcs > ctx->sc_pool_csum_calcs) {
		msec_between = ds_scrub_wait_between_msec(
			sc_get_schedule(ctx), ctx->sc_pool_start_scrub,
			ctx->sc_pool_last_csum_calcs - ctx->sc_pool_csum_calcs,
			ctx->sc_pool->sp_scrub_freq_sec);
	}

	if (!sc_no_yield(ctx)) {
		if (msec_between == 0)
			sc_yield(ctx);
		else
			sc_sleep(ctx, msec_between);

	}

	sc_credit_reset(ctx);
}

/*
 * DAOS_CSUM_SCRUB_DISABLED_WAIT_SEC can be set in the server config to change
 * how long to wait before checking again if the scrubber is enabled.
 */
static uint32_t
seconds_to_wait_while_disabled()
{
	char *sec = getenv("DAOS_CSUM_SCRUB_DISABLED_WAIT_SEC");

	return sec != NULL ? atoll(sec) : 5;
}

static void
sc_control_when_complete(struct scrub_ctx *ctx)
{
	struct timespec	now, diff;
	uint32_t	left_sec;

	d_gettime(&now);
	diff = d_timediff(ctx->sc_pool_start_scrub, now);

	if (diff.tv_sec < ctx->sc_pool->sp_scrub_freq_sec) {
		left_sec = ctx->sc_pool->sp_scrub_freq_sec - diff.tv_sec;
		C_TRACE("Sleep for %d sec\n", left_sec);
		sc_sleep(ctx, left_sec * 1000);
	} else {
		C_TRACE("Yield\n");
		sc_yield(ctx);
	}
}

void
ds_scrub_sched_control(struct scrub_ctx *ctx)
{
	uint32_t disabled_wait_sec = seconds_to_wait_while_disabled();

	if (ctx->sc_pool->sp_scrub_sched == DAOS_SCRUB_SCHED_OFF ||
	    ctx->sc_pool->sp_scrub_freq_sec == 0) {
		C_TRACE("Scrubbing not set to run. Sleeping %d sec.\n",
			disabled_wait_sec);
		sc_sleep(ctx, disabled_wait_sec * 1000);
		return;
	}

	if (ctx->sc_status == SCRUB_STATUS_RUNNING) {
		sc_control_in_between(ctx);
		return;
	}
	if (ctx->sc_status == SCRUB_STATUS_NOT_RUNNING) {
		sc_control_when_complete(ctx);
		return;
	}
	D_ASSERTF(false, "Should not get here.");
}
