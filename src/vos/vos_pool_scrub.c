/*
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(csum)

#include <daos_srv/vos.h>
#include <daos_srv/srv_csum.h>
#include "vos_internal.h"

#define C_TRACE(...) D_DEBUG(DB_CSUM, __VA_ARGS__)

#define SCRUB_POOL_OFF 1
#define SCRUB_CONT_STOPPING 2
#define MS2NS(s) (s * 1000000)
#define SEC2NS(s) (s * 1E9)
#define NS2MS(s) (s / 1E6)

static inline void
sc_csum_calc_inc(struct scrub_ctx *ctx)
{
	ctx->sc_pool_csum_calcs++;
}

/* Telemetry Metrics */
static void
sc_m_pool_start(struct scrub_ctx *ctx)
{
	d_tm_record_timestamp(ctx->sc_metrics.scm_start);
	d_tm_mark_duration_start(
		ctx->sc_metrics.scm_last_duration,
		D_TM_CLOCK_REALTIME);
}

static void
sc_m_pool_stop(struct scrub_ctx *ctx)
{
	ctx->sc_pool_last_csum_calcs = ctx->sc_pool_csum_calcs;

	d_tm_mark_duration_end(ctx->sc_metrics.scm_last_duration);
	d_tm_set_counter(ctx->sc_metrics.scm_last_csum_calcs,
			 ctx->sc_pool_last_csum_calcs);

	d_tm_record_timestamp(ctx->sc_metrics.scm_end);
}

static void
sc_m_pool_csum_inc(struct scrub_ctx *ctx)
{
	m_inc_counter(ctx->sc_metrics.scm_csum_calcs);
	m_inc_counter(ctx->sc_metrics.scm_total_csum_calcs);
}

static void
sc_m_pool_corr_inc(struct scrub_ctx *ctx)
{
	m_inc_counter(ctx->sc_metrics.scm_corruption);
	m_inc_counter(ctx->sc_metrics.scm_total_corruption);
}

static void
sc_m_pool_csum_reset(struct scrub_ctx *ctx)
{
	m_reset_counter(ctx->sc_metrics.scm_csum_calcs);
	m_reset_counter(ctx->sc_metrics.scm_corruption);
}

static inline struct daos_csummer *
sc_csummer(const struct scrub_ctx *ctx)
{
	return ctx->sc_cont.scs_cont_csummer;
}

static inline uint32_t
sc_chunksize(const struct scrub_ctx *ctx)
{
	return daos_csummer_get_rec_chunksize(sc_csummer(ctx),
					      ctx->sc_iod.iod_size);
}

static inline int
sc_schedule(const struct scrub_ctx *ctx)
{
	return ctx->sc_pool->sp_scrub_sched;
}

static inline int
sc_freq(const struct scrub_ctx *ctx)
{
	return ctx->sc_pool->sp_scrub_freq_sec;
}

static inline uint32_t
sc_thresh(const struct scrub_ctx *ctx)
{
	return ctx->sc_pool->sp_scrub_thresh;
}
static inline void
sc_yield(struct scrub_ctx *ctx)
{
	if (ctx->sc_yield_fn) {
		d_tm_set_gauge(ctx->sc_metrics.scm_pool_ult_wait_time, 0);
		ctx->sc_yield_fn(ctx->sc_sched_arg);
		ctx->sc_did_yield = true;
	}
}

static inline void
sc_sleep(struct scrub_ctx *ctx, uint32_t ms)
{
	if (ctx->sc_sleep_fn) {
		d_tm_set_gauge(ctx->sc_metrics.scm_pool_ult_wait_time, ms);
		ctx->sc_sleep_fn(ctx->sc_sched_arg, ms);
		ctx->sc_did_yield = true;
		d_tm_set_gauge(ctx->sc_metrics.scm_pool_ult_wait_time, 0);
	}
}

static inline bool
sc_cont_is_stopping(struct scrub_ctx *ctx)
{
	if (ctx->sc_cont_is_stopping_fn == NULL)
		return false;
	return ctx->sc_cont_is_stopping_fn(ctx->sc_cont.scs_cont_src);
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

static void
sc_credit_decrement(struct scrub_ctx *ctx)
{
	if (ctx->sc_credits_left == 0)
		return;
	ctx->sc_credits_left--;
}

static void
sc_credit_reset(struct scrub_ctx *ctx)
{
	if (ctx->sc_credits_left == 0)
		ctx->sc_credits_left = ctx->sc_pool->sp_scrub_cred;
}

void
sc_yield_sleep_while_running(struct scrub_ctx *ctx)
{
	uint64_t msec_between = 0;
	struct timespec now;

	/* must have a frequency set */
	D_ASSERT(ctx->sc_pool->sp_scrub_freq_sec > 0);

	d_gettime(&now);
	sc_credit_decrement(ctx);
	if (ctx->sc_credits_left > 0)
		return;

	if (sc_schedule(ctx) == DAOS_SCRUB_SCHED_CONTINUOUS) {
		msec_between = get_ms_between_periods(ctx->sc_pool_start_scrub,
			now, ctx->sc_pool->sp_scrub_freq_sec,
			ctx->sc_pool_last_csum_calcs,
			/* -1 to convert to index (from count) */
			ctx->sc_pool_csum_calcs - 1);
	}

	if (msec_between == 0)
		sc_yield(ctx);
	else
		sc_sleep(ctx, msec_between);

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

void
sc_yield_or_sleep(struct scrub_ctx *ctx)
{
	struct timespec	now, diff;
	uint32_t	left_sec;

	d_gettime(&now);
	diff = d_timediff(ctx->sc_pool_start_scrub, now);

	if (diff.tv_sec < sc_freq(ctx)) {
		left_sec = sc_freq(ctx) - diff.tv_sec;
		sc_sleep(ctx, left_sec * 1000);
	} else {
		sc_yield(ctx);
	}
}

static inline bool
sc_scrub_enabled(struct scrub_ctx *ctx)
{
	return sc_schedule(ctx) != DAOS_SCRUB_SCHED_OFF && sc_freq(ctx) > 0;
}

static void
sc_verify_finish(struct scrub_ctx *ctx)
{
	sc_csum_calc_inc(ctx);
	sc_m_pool_csum_inc(ctx);
	sc_yield_sleep_while_running(ctx);
}

/**
 * Will verify the checksum(s) for the current recx. It will do it one chunk
 * at a time instead of all at once so that it can yield/sleep between each
 * calculation.
 */
static int
sc_verify_recx(struct scrub_ctx *ctx, d_iov_t *data)
{
	daos_key_t		 chunk_iov = {0};
	uint8_t			*csum_buf = NULL;
	daos_recx_t		*recx;
	daos_size_t		 rec_len;
	daos_size_t		 processed_bytes = 0;
	uint32_t		 i;
	uint32_t		 chunksize;
	uint32_t		 csum_nr;
	int			 rc = 0;
	uint16_t		 csum_len;

	D_ASSERT(ctx->sc_iod.iod_nr == 1);
	D_ASSERT(data != NULL);
	D_ASSERT(ctx->sc_iod.iod_recxs != NULL);

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

		if (sc_cont_is_stopping(ctx))
			return 0;

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
			sc_verify_finish(ctx);

			D_GOTO(done, rc = -DER_CSUM);
		}

		processed_bytes += chunk_iov.iov_len;

		sc_verify_finish(ctx);
	}

done:
	D_FREE(csum_buf);

	return rc;
}

static int
sc_verify_sv(struct scrub_ctx *ctx, d_iov_t *data)
{
	int rc;

	if (sc_cont_is_stopping(ctx))
		return 0;

	rc = daos_csummer_verify_key(sc_csummer(ctx), data,
				     ctx->sc_csum_to_verify);
	sc_verify_finish(ctx);

	return rc;
}

static uuid_t *
sc_cont_uuid(struct scrub_ctx *ctx)
{
	return &ctx->sc_cont_uuid;
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
		ds_notify_ras_event(RAS_POOL_CORRUPTION_DETECTED,
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
sc_pool_drain(struct scrub_ctx *ctx)
{
	D_ASSERT(ctx->sc_drain_pool_tgt_fn);

	return ctx->sc_drain_pool_tgt_fn(ctx->sc_pool);
}

static bool
sc_should_evict(struct scrub_ctx *ctx)
{

	return sc_thresh(ctx) > 0 && /* threshold set */
	       ctx->sc_pool_tgt_corrupted_detected >= /* hit or exceeded */
	       sc_thresh(ctx);
}

static int
sc_verify_obj_value(struct scrub_ctx *ctx, struct bio_iov *biov,
		    daos_handle_t ih)
{
	d_iov_t			 data;
	daos_iod_t		*iod = &ctx->sc_iod;
	uint64_t		 data_len;
	struct bio_io_context	*bio_ctx;
	struct vos_iterator	*iter;
	struct vos_obj_iter	*oiter;
	int			 rc;

	/*
	 * Know that there will always only be 1 recx because verifying a
	 * single extent at a time so use first recx in iod for data_len
	 * calculation
	 */
	data_len = iod->iod_type == DAOS_IOD_ARRAY ?
		   iod->iod_recxs[0].rx_nr * iod->iod_size :
		   iod->iod_size;
	/* allocate memory to fetch data into */
	D_ALLOC(data.iov_buf, data_len);
	data.iov_buf_len = data_len;
	data.iov_len = data_len;

	/* Fetch data */
	iter = vos_hdl2iter(ih);
	oiter = vos_iter2oiter(iter);
	bio_ctx = oiter->it_obj->obj_cont->vc_pool->vp_io_ctxt;
	rc = bio_read(bio_ctx, biov->bi_addr, &data);

	/* if bio_read of NVME then it might have yielded */
	if (bio_iov2media(biov) == DAOS_MEDIA_NVME)
		ctx->sc_did_yield = true;

	if (BIO_ADDR_IS_CORRUPTED(&biov->bi_addr)) {
		/* Already know this is corrupt so just return */
		D_GOTO(out, rc = DER_SUCCESS);
	} else if (rc != 0) {
		D_WARN("Unable to fetch data for scrubber: "DF_RC"\n",
		       DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = iod->iod_type == DAOS_IOD_ARRAY ?
	     sc_verify_recx(ctx, &data) :
	     sc_verify_sv(ctx, &data);

	if (rc == -DER_CSUM) {
		sc_raise_ras(ctx);
		sc_m_pool_corr_inc(ctx);
		rc = sc_mark_corrupt(ctx);
		if (bio_iov2media(biov) == DAOS_MEDIA_NVME) {
			bio_log_csum_err(ctx->sc_dmi->dmi_nvme_ctxt);
		}
		if (rc != 0)
			D_ERROR("Error trying to mark corrupt: "DF_RC"\n",
				DP_RC(rc));
		ctx->sc_pool_tgt_corrupted_detected++;
		D_ERROR("Checksum scrubber found corruption. %d so far.\n",
			ctx->sc_pool_tgt_corrupted_detected);
		if (sc_should_evict(ctx)) {
			D_ERROR("Corruption threshold reached. %d >= %d",
				ctx->sc_pool_tgt_corrupted_detected,
				sc_thresh(ctx));
			d_tm_set_counter(ctx->sc_metrics.scm_csum_calcs, 0);
			d_tm_set_counter(ctx->sc_metrics.scm_last_csum_calcs, 0);
			rc = sc_pool_drain(ctx);
			if (rc != 0)
				D_ERROR("Drain error: "DF_RC"\n", DP_RC(rc));

			rc = -DER_SHUTDOWN;
		}
	} else if (rc != 0) {
		D_ERROR("Error while scrubbing: "DF_RC"\n", DP_RC(rc));
	}

out:
	D_FREE(data.iov_buf);

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
	ctx->sc_minor_epoch = entry->ie_minor_epc;

	ctx->sc_iod.iod_size = entry->ie_rsize;
	ctx->sc_iod.iod_nr = 1;
	ctx->sc_iod.iod_type = type == VOS_ITER_RECX ? DAOS_IOD_ARRAY :
			       DAOS_IOD_SINGLE;
	ctx->sc_iod.iod_name = param->ip_akey;
	ctx->sc_iod.iod_recxs = &entry->ie_recx;

	ctx->sc_csum_to_verify = &entry->ie_csum;

	ctx->sc_vos_iter_handle = ih;
}

static inline bool
oids_are_same(daos_unit_oid_t a, daos_unit_oid_t b)
{
	return daos_unit_oid_compare(a, b) == 0;
}

static inline bool
keys_are_same(daos_key_t key1, daos_key_t key2)
{
	if (key1.iov_len != key2.iov_len)
		return 0;

	return memcmp(key1.iov_buf, key2.iov_buf, key1.iov_len) == 0;
}

static inline bool
uuids_are_same(uuid_t a, uuid_t b)
{
	return uuid_compare(a, b) == 0;
}

static inline bool
epoch_eq(daos_epoch_t a, daos_epoch_t b)
{
	return a == b;
}

static inline bool
recx_eq(const daos_recx_t *a, const daos_recx_t *b)
{
	if (a == NULL || b == NULL)
		return false;

	return a->rx_nr == b->rx_nr && a->rx_idx == b->rx_idx;
}

static bool
sc_value_has_been_seen(struct scrub_ctx *ctx, vos_iter_entry_t *entry,
		       vos_iter_type_t type)
{
	if (type == VOS_ITER_RECX &&
	    !recx_eq(ctx->sc_iod.iod_recxs, &entry->ie_recx))
		return false;
	return epoch_eq(ctx->sc_epoch, entry->ie_epoch) &&
	       epoch_eq(ctx->sc_minor_epoch, entry->ie_minor_epc);
}

static void
sc_obj_value_reset(struct  scrub_ctx *ctx)
{
	ctx->sc_epoch = 0;
	ctx->sc_minor_epoch = 0;
}

/** vos_iter_cb_t */
static int
obj_iter_scrub_pre_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		  vos_iter_type_t type, vos_iter_param_t *param,
		  void *cb_arg, unsigned int *acts)
{
	struct scrub_ctx	*ctx = cb_arg;
	int			 rc;

	if (sc_cont_is_stopping(ctx)) {
		C_TRACE("Container is stopping.");
		return SCRUB_CONT_STOPPING;
	}

	if (!sc_scrub_enabled(ctx)) {
		C_TRACE("scrubbing is off");
		return SCRUB_POOL_OFF;
	}

	switch (type) {
	case VOS_ITER_OBJ:
		if (oids_are_same(ctx->sc_cur_oid, entry->ie_oid)) {
			*acts |= VOS_ITER_CB_SKIP;
			memset(&ctx->sc_cur_oid, 0, sizeof(ctx->sc_cur_oid));
		} else {
			ctx->sc_cur_oid = entry->ie_oid;
			/* reset dkey and akey */
			memset(&ctx->sc_dkey, 0, sizeof(ctx->sc_dkey));
			memset(&ctx->sc_iod, 0, sizeof(ctx->sc_iod));
		}
		break;
	case VOS_ITER_DKEY:
		if (keys_are_same(ctx->sc_dkey, entry->ie_key)) {
			*acts |= VOS_ITER_CB_SKIP;
			memset(&ctx->sc_dkey, 0, sizeof(ctx->sc_dkey));
		} else {
			ctx->sc_dkey = param->ip_dkey;
			/* reset akey */
			memset(&ctx->sc_iod, 0, sizeof(ctx->sc_iod));
		}
		break;
	case VOS_ITER_AKEY:
		if (keys_are_same(ctx->sc_iod.iod_name, entry->ie_key)) {
			*acts |= VOS_ITER_CB_SKIP;
			memset(&ctx->sc_iod, 0, sizeof(ctx->sc_iod));
		} else {
			ctx->sc_iod.iod_name = param->ip_akey;
			/* reset value */
			sc_obj_value_reset(ctx);
		}
		break;
	case VOS_ITER_SINGLE:
	case VOS_ITER_RECX: {
		if (sc_value_has_been_seen(ctx, entry, type)) {
			sc_obj_value_reset(ctx);
		} else {
			sc_obj_val_setup(ctx, entry, type, param, ih);

			rc = sc_verify_obj_value(ctx, &entry->ie_biov, ih);
			if (ctx->sc_did_yield) {
				*acts |= VOS_ITER_CB_YIELD;
				ctx->sc_did_yield = false;
			}

			if (rc != 0) {
				D_ERROR("Error Verifying:"DF_RC"\n", DP_RC(rc));
				return rc;
			}
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

	param.ip_hdl = sc_cont_hdl(ctx);
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	param.ip_epr.epr_lo = 0;
	param.ip_epc_expr = VOS_IT_EPC_RE;
	/*
	 * FIXME: Improve iteration by only iterating over visible
	 * recxs (set param.ip_flags = VOS_IT_RECX_VISIBLE). Will have to be
	 * smarter about checksum handling of visible recxs because potential of
	 * partial extents. Unit test multiple_overlapping_extents() verifies
	 * this case. srv_csum.c has some logic that might be useful/reused.
	 */
	rc = vos_iterate(&param, VOS_ITER_OBJ, true, &anchor,
			 obj_iter_scrub_pre_cb, NULL, ctx, NULL);

	if (rc != DER_SUCCESS) {
		if (rc == -DER_INPROGRESS)
			return 0;
		if (rc < 0) {
			D_ERROR("Object scrub failed: "DF_RC"\n", DP_RC(rc));
			return rc;
		}
		if (rc == SCRUB_POOL_OFF) {
			C_TRACE("Scrubbing is stopping for pool.");
			return SCRUB_POOL_OFF;
		} else if (rc == SCRUB_CONT_STOPPING) {
			C_TRACE("Container is stopping.");
			/* Just fall through to return 0 */
		}
	}

	return 0;
}

static int
sc_cont_setup(struct scrub_ctx *ctx, vos_iter_entry_t *entry)
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

	uuid_copy(ctx->sc_cont_uuid, entry->ie_couuid);

	return 0;
}

static void
sc_cont_teardown(struct scrub_ctx *ctx)
{
	if (ctx->sc_cont_put_fn != NULL)
		ctx->sc_cont_put_fn(ctx->sc_cont.scs_cont_src);
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

	if (uuids_are_same(*sc_cont_uuid(ctx), entry->ie_couuid)) {
		*acts = VOS_ITER_CB_SKIP;
		uuid_clear(ctx->sc_cont_uuid);
	} else {
		rc = sc_cont_setup(ctx, entry);
		if (rc != 0) {
			/* log error for container, but then keep going */
			D_ERROR("Unable to setup the container. "DF_RC"\n",
				DP_RC(rc));
			return 0;
		}

		rc = sc_scrub_cont(ctx);

		sc_cont_teardown(ctx);
		*acts = VOS_ITER_CB_YIELD;
	}

	return rc;
}

static void
sc_pool_start(struct scrub_ctx *ctx)
{
	/* remember previous checksum calculations */
	ctx->sc_pool_last_csum_calcs = ctx->sc_pool_csum_calcs;
	ctx->sc_pool_csum_calcs = 0;
	ctx->sc_did_yield = false;
	d_gettime(&ctx->sc_pool_start_scrub);

	sc_m_pool_csum_reset(ctx);
	sc_m_pool_start(ctx);
}

static void
sc_pool_stop(struct scrub_ctx *ctx)
{
	sc_m_pool_stop(ctx);
}

int
vos_scrub_pool(struct scrub_ctx *ctx)
{
	vos_iter_param_t	param = {0};
	struct vos_iter_anchors	anchor = {0};
	int			rc = 0;

	if (daos_handle_is_inval(ctx->sc_vos_pool_hdl)) {
		D_ERROR("vos_iter_handle is invalid.\n");
		return -DER_INVAL;
	}

	if (!sc_scrub_enabled(ctx)) {
		sc_sleep(ctx, seconds_to_wait_while_disabled() * 1000);
		return 0;
	}

	sc_pool_start(ctx);

	param.ip_hdl = ctx->sc_vos_pool_hdl;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	rc = vos_iterate(&param, VOS_ITER_COUUID, false, &anchor,
			 NULL, cont_iter_scrub_cb, ctx, NULL);
	d_tm_inc_counter(ctx->sc_metrics.scm_scrub_count, 1);
	sc_pool_stop(ctx);
	if (rc == SCRUB_POOL_OFF)
		return 0;
	if (rc == -DER_SHUTDOWN)
		return rc; /* Don't try again if is shutting down. */
	if (rc != 0) {
		/*
		 * If scrubbing failed for some reason, wait a minute
		 * before trying again
		 */
		D_ERROR("Scrubbing failed. "DF_RC"\n", DP_RC(rc));
		sc_sleep(ctx, 1000 * 60);
		rc = 0; /* error reported and handled */
	} else {
		sc_yield_or_sleep(ctx);
	}

	return rc;
}

uint64_t
get_ms_between_periods(struct timespec start_time, struct timespec cur_time,
		       uint64_t duration_seconds, uint64_t periods_nr,
		       uint64_t per_idx)
{
	uint64_t	exp_per_ms; /* seconds per period */
	struct timespec exp_curr_end; /* current period's expected finish */

	if (periods_nr == 0 || duration_seconds == 0)
		return 0;

	if (per_idx > periods_nr - 1)
		per_idx = periods_nr - 1;
	exp_per_ms = duration_seconds * 1000 / periods_nr;
	exp_curr_end = start_time;
	d_timeinc(&exp_curr_end, MS2NS(exp_per_ms * (per_idx + 1)));

	/* already past current period? */
	if (d_time2us(exp_curr_end) <= d_time2us(cur_time))
		return 0;

	return d_time2ms(d_timediff(cur_time, exp_curr_end));
}
