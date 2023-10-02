/*
 * (C) Copyright 2020-2023 Intel Corporation.
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

#define m_inc_counter(m) d_tm_inc_counter((m), 1)
#define m_reset_counter(m) d_tm_set_counter((m), 0)

static inline void
sc_csum_calc_inc(struct scrub_ctx *ctx)
{
	ctx->sc_pool_csum_calcs++;
}

static inline void
sc_scrub_count_inc(struct scrub_ctx *ctx)
{
	m_inc_counter(ctx->sc_metrics.scm_scrub_count);
	ctx->sc_pool_scrub_count++;
}

static inline void
sc_scrub_bytes_scrubbed(struct scrub_ctx *ctx, uint64_t bytes)
{
	ctx->sc_bytes_scrubbed += bytes;
	d_tm_inc_counter(ctx->sc_metrics.scm_bytes_scrubbed, bytes);
	d_tm_inc_counter(ctx->sc_metrics.scm_bytes_scrubbed_total, bytes);
}

static inline void
sc_scrub_bytes_scrubbed_reset(struct scrub_ctx *ctx)
{
	d_tm_set_counter(ctx->sc_metrics.scm_bytes_scrubbed_last, ctx->sc_bytes_scrubbed);
	d_tm_set_counter(ctx->sc_metrics.scm_bytes_scrubbed, 0);
	ctx->sc_bytes_scrubbed = 0;
}

static inline bool
sc_is_idle(struct scrub_ctx *ctx)
{
	if (ctx->sc_is_idle_fn)
		return ctx->sc_is_idle_fn();
	return false;
}

/* Telemetry Metrics */
static void
sc_m_pool_start(struct scrub_ctx *ctx)
{
	d_tm_record_timestamp(ctx->sc_metrics.scm_start);
	d_tm_mark_duration_start(ctx->sc_metrics.scm_last_duration, D_TM_CLOCK_REALTIME);
}

static void
sc_m_pool_stop(struct scrub_ctx *ctx)
{
	ctx->sc_pool_last_csum_calcs = ctx->sc_pool_csum_calcs;

	d_tm_mark_duration_end(ctx->sc_metrics.scm_last_duration);
	d_tm_set_counter(ctx->sc_metrics.scm_csum_calcs_last, ctx->sc_pool_last_csum_calcs);
	d_tm_set_gauge(ctx->sc_metrics.scm_next_csum_scrub, 0);
}

static void
sc_m_pool_csum_inc(struct scrub_ctx *ctx)
{
	m_inc_counter(ctx->sc_metrics.scm_csum_calcs);
	m_inc_counter(ctx->sc_metrics.scm_csum_calcs_total);
}

static void
sc_m_pool_corr_inc(struct scrub_ctx *ctx)
{
	m_inc_counter(ctx->sc_metrics.scm_corruption);
	m_inc_counter(ctx->sc_metrics.scm_corruption_total);
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
sc_mode(const struct scrub_ctx *ctx)
{
	return ctx->sc_pool->sp_scrub_mode;
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

static inline void
sc_sleep(struct scrub_ctx *ctx, uint32_t ms)
{
	if (ctx->sc_sleep_fn == NULL || ctx->sc_yield_fn == NULL)
		return;

	if (ms > 0)
		ctx->sc_sleep_fn(ctx->sc_sched_arg, ms);
	else
		ctx->sc_yield_fn(ctx->sc_sched_arg);
}

static inline bool
sc_cont_is_stopping(struct scrub_ctx *ctx)
{
	if (ctx->sc_cont_is_stopping_fn == NULL)
		return false;
	return ctx->sc_cont_is_stopping_fn(ctx->sc_cont.scs_cont_src);
}

static inline bool
sc_scrub_enabled(struct scrub_ctx *ctx)
{
	return sc_mode(ctx) != DAOS_SCRUB_MODE_OFF && sc_freq(ctx) > 0;
}

static bool
sc_frequency_time_over(struct scrub_ctx *ctx)
{
	struct timespec	period_over = ctx->sc_pool_start_scrub;
	int64_t		ns_left;

	d_timeinc(&period_over, SEC2NS(ctx->sc_pool->sp_scrub_freq_sec));

	ns_left = d_timeleft_ns(&period_over);

	d_tm_set_gauge(ctx->sc_metrics.scm_next_tree_scrub, ns_left / NSEC_PER_SEC);

	return ns_left <= 0;
}

static uint32_t
sc_get_ms_between_scrubs(struct scrub_ctx *ctx)
{
	struct timespec now;

	d_gettime(&now);

	return get_ms_between_periods(ctx->sc_pool_start_scrub,
				      now, ctx->sc_pool->sp_scrub_freq_sec,
				      ctx->sc_pool_last_csum_calcs,
				      ctx->sc_pool_csum_calcs - 1);
}

static inline void
sc_m_set_busy_time(struct scrub_ctx *ctx, uint64_t ns)
{
	d_tm_set_gauge(ctx->sc_metrics.scm_busy_time, ns / NSEC_PER_SEC);
}

static inline void
sc_m_track_idle(struct scrub_ctx *ctx)
{
	sc_m_set_busy_time(ctx, 0);
	ctx->sc_metrics.scm_busy_start.tv_nsec = 0;
	ctx->sc_metrics.scm_busy_start.tv_sec = 0;
}

static void
sc_m_track_busy(struct scrub_ctx *ctx)
{
	struct timespec now;

	if (d_time2us(ctx->sc_metrics.scm_busy_start) == 0) {
		d_gettime(&ctx->sc_metrics.scm_busy_start);
		return;
	}

	d_gettime(&now);
	int64_t diff_ns = d_timediff_ns(&ctx->sc_metrics.scm_busy_start, &now);

	sc_m_set_busy_time(ctx, diff_ns);
}

static bool
sc_should_start(struct scrub_ctx *ctx)
{
	D_ASSERT(ctx->sc_status == SCRUB_STATUS_NOT_RUNNING);
	if (!sc_scrub_enabled(ctx))
		return false;

	if (ctx->sc_pool_scrub_count == 0 || sc_frequency_time_over(ctx)) {
		if (ctx->sc_pool->sp_scrub_mode == DAOS_SCRUB_MODE_LAZY) { /* only run if idle */
			bool is_idle = sc_is_idle(ctx);

			if (!is_idle)
				sc_m_track_busy(ctx);
			else
				sc_m_track_idle(ctx);
			return is_idle;
		} else if (ctx->sc_pool->sp_scrub_mode == DAOS_SCRUB_MODE_TIMED)
			return true;
		D_ASSERTF(false, "Unknown scrubbing mode");
	}

	return false;
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
sc_wait_until_should_continue(struct scrub_ctx *ctx)
{
	if (sc_mode(ctx) == DAOS_SCRUB_MODE_TIMED) {
		struct timespec	now;
		uint64_t	msec_between;

		d_gettime(&now);
		while ((msec_between = sc_get_ms_between_scrubs(ctx)) > 0) {
			d_tm_set_gauge(ctx->sc_metrics.scm_next_csum_scrub, msec_between);
			/* don't wait longer than 1 sec each loop */
			sc_sleep(ctx, min(1000, msec_between));
		}
	} else if (sc_mode(ctx) == DAOS_SCRUB_MODE_LAZY) {
		sc_sleep(ctx, 0);
		while (!ctx->sc_is_idle_fn()) {
			sc_m_track_busy(ctx);
			/* Don't actually know how long it will be but wait for 1 second before
			 * trying again
			 */
			sc_sleep(ctx, 1000);
		}
		sc_m_track_idle(ctx);
	} else {
		D_ERROR("Unknown Scrub Mode: %d, Pool: " DF_UUID "\n", sc_mode(ctx),
			DP_UUID(ctx->sc_pool->sp_uuid));
		/* sleep for 5 minutes to give pool property chance to resolve */
		sc_sleep(ctx, 1000 * 60 * 5);
	}
}

static void
sc_verify_finish(struct scrub_ctx *ctx)
{
	sc_csum_calc_inc(ctx);
	sc_m_pool_csum_inc(ctx);
	sc_wait_until_should_continue(ctx);
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

static bool
sc_is_nvme(struct scrub_ctx *ctx)
{
	return bio_iov2media(ctx->sc_cur_biov) == DAOS_MEDIA_NVME;
}

static bool
sc_is_first_pass(struct scrub_ctx *ctx)
{
	return !ctx->sc_first_pass_done;
}

static int
sc_handle_corruption(struct scrub_ctx *ctx)
{
	int rc;

	/** It's ok if we do the checksum calculation after a yield, hoping for the best, but we
	 *  absolutely must check before modifying data at the current iterator position.  If the
	 *  entry has been deleted, we can ignore any corruption we found and move on.
	 */
	rc = vos_iter_validate(ctx->sc_vos_iter_handle);
	if (rc < 0)
		return rc;
	if (rc > 0) /** value no longer exists */
		return 0;

	sc_raise_ras(ctx);
	sc_m_pool_corr_inc(ctx);
	rc = sc_mark_corrupt(ctx);

	if (sc_is_nvme(ctx))
		bio_log_data_csum_err(ctx->sc_dmi->dmi_nvme_ctxt);
	if (rc != 0) {
		/* Log error but don't let it stop the scrubbing process */
		D_ERROR("Error trying to mark corrupt: "DF_RC"\n", DP_RC(rc));
		rc = 0;
	}
	ctx->sc_pool_tgt_corrupted_detected++;
	D_ERROR("[tgt_id: %d]Checksum scrubber found corruption. %d so far.\n",
		ctx->sc_dmi->dmi_tgt_id,
		ctx->sc_pool_tgt_corrupted_detected);
	if (sc_should_evict(ctx)) {
		D_ERROR("Corruption threshold reached. %d >= %d\n",
			ctx->sc_pool_tgt_corrupted_detected, sc_thresh(ctx));
		d_tm_set_counter(ctx->sc_metrics.scm_csum_calcs, 0);
		d_tm_set_counter(ctx->sc_metrics.scm_csum_calcs_last, 0);
		rc = sc_pool_drain(ctx);
		if (rc != 0)
			D_ERROR("Drain error: "DF_RC"\n", DP_RC(rc));

		rc = -DER_SHUTDOWN;
	}

	return rc;
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
			D_GOTO(done, rc = 0);

		orig_csum = ci_idx2csum(ctx->sc_csum_to_verify, i);
		rec_in_chunk = sc_get_rec_in_chunk_at_idx(ctx, i);

		/** set an iov with just the data for the current chunk */
		d_iov_set(&chunk_iov, data->iov_buf + processed_bytes,
			  rec_in_chunk * rec_len);
		D_ASSERT(processed_bytes + chunk_iov.iov_len <= data->iov_len);

		rc = daos_csummer_calc_for_iov(sc_csummer(ctx), &chunk_iov, csum_buf, csum_len);
		if (rc != 0) {
			D_ERROR("daos_csummer_calc_for_iov error: "DF_RC"\n", DP_RC(rc));
			D_GOTO(done, rc);
		}

		sc_scrub_bytes_scrubbed(ctx, chunk_iov.iov_len);

		match = daos_csummer_csum_compare(sc_csummer(ctx), orig_csum, csum_buf, csum_len);

		if (!match) {
			D_ERROR("Corruption found for chunk #%d of recx: "DF_RECX", epoch: %lu\n",
				i, DP_RECX(*recx), ctx->sc_epoch);

			rc = sc_handle_corruption(ctx);

			sc_verify_finish(ctx);

			D_GOTO(done, rc);
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

	rc = daos_csummer_verify_key(sc_csummer(ctx), data, ctx->sc_csum_to_verify);
	if (rc == -DER_CSUM)
		rc = sc_handle_corruption(ctx);
	sc_verify_finish(ctx);

	sc_scrub_bytes_scrubbed(ctx, data->iov_len);

	return rc;
}

static int
sc_verify_obj_value(struct scrub_ctx *ctx, struct bio_iov *biov, daos_handle_t ih)
{
	d_iov_t			 data;
	daos_iod_t		*iod = &ctx->sc_iod;
	uint64_t		 data_len;
	struct bio_io_context	*bio_ctx;
	struct umem_instance	*umem;
	struct vos_iterator	*iter;
	struct vos_obj_iter	*oiter;
	int			 rc;

	/* Don't verify a hole */
	if (bio_addr_is_hole(&biov->bi_addr))
		return 0;

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
	bio_ctx = vos_data_ioctxt(oiter->it_obj->obj_cont->vc_pool);
	umem = &oiter->it_obj->obj_cont->vc_pool->vp_umm;
	rc = vos_media_read(bio_ctx, umem, biov->bi_addr, &data);

	if (BIO_ADDR_IS_CORRUPTED(&biov->bi_addr)) {
		/* Already know this is corrupt so just return */
		if (sc_is_first_pass(ctx))
			/* Because metrics aren't persisted across engine resets,
			 * need to count the number of corrupted records found previously
			 */
			d_tm_inc_counter(ctx->sc_metrics.scm_corruption_total, 1);
		D_GOTO(out, rc = DER_SUCCESS);
	} else if (rc != 0) {
		D_WARN("Unable to fetch data for scrubber: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	ctx->sc_cur_biov = biov;
	rc = iod->iod_type == DAOS_IOD_ARRAY ?
	     sc_verify_recx(ctx, &data) :
	     sc_verify_sv(ctx, &data);
	ctx->sc_cur_biov = NULL;
	if (rc != 0)
		D_ERROR("Error while scrubbing: "DF_RC"\n", DP_RC(rc));

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
			D_ERROR("Unable to setup the container. "DF_RC"\n", DP_RC(rc));
			return 0;
		}

		rc = sc_scrub_cont(ctx);

		sc_cont_teardown(ctx);
		*acts = VOS_ITER_CB_YIELD;
	}

	return rc;
}

static void
sc_reset_iterator_checks(struct scrub_ctx *ctx)
{
	uuid_clear(ctx->sc_cont_uuid);
	memset(&ctx->sc_cur_oid, 0, sizeof(ctx->sc_cur_oid));
	memset(&ctx->sc_dkey, 0, sizeof(ctx->sc_dkey));
	memset(&ctx->sc_iod, 0, sizeof(ctx->sc_iod));
	sc_obj_value_reset(ctx);
}

static void
sc_pool_start(struct scrub_ctx *ctx)
{
	/* remember previous checksum calculations */
	ctx->sc_pool_last_csum_calcs = ctx->sc_pool_csum_calcs;
	ctx->sc_pool_csum_calcs      = 0;
	d_gettime(&ctx->sc_pool_start_scrub);

	sc_m_pool_csum_reset(ctx);
	sc_m_pool_start(ctx);
	sc_scrub_bytes_scrubbed_reset(ctx);
	ctx->sc_status = SCRUB_STATUS_RUNNING;
	sc_reset_iterator_checks(ctx);
}

static void
sc_pool_stop(struct scrub_ctx *ctx)
{
	sc_m_pool_stop(ctx);
	ctx->sc_status = SCRUB_STATUS_NOT_RUNNING;
}

/* structure for the cont_iter_is_loaded_cb args */
struct cont_are_loaded_args {
	struct scrub_ctx	*args_ctx;
	bool			 args_found_unloaded_container;
};

/** vos_iter_cb_t */
static int
cont_iter_is_loaded_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		       vos_iter_type_t type, vos_iter_param_t *param,
		       void *cb_arg, unsigned int *acts)
{
	struct cont_are_loaded_args	*args = cb_arg;
	struct scrub_ctx		*ctx  = args->args_ctx;
	int				 rc = 0;

	D_ASSERT(type == VOS_ITER_COUUID);

	rc = sc_cont_setup(ctx, entry);
	if (rc != 0)
		return rc;
	if (sc_cont_is_stopping(ctx))
		return 0;

	/*
	 * Is loaded when the properties have been fetched. That way the csummer has been
	 * initialized if csums are enabled
	 */
	if (!args->args_found_unloaded_container)
		args->args_found_unloaded_container = !args->args_ctx->sc_cont.scs_props_fetched;

	sc_cont_teardown(ctx);
	return 0;
}

/*
 * When the scrubber starts, make sure all containers are loaded. Using the "props_fetched" field
 * from the ds_cont_child which indicates that the csummer has been initialized if checksums are
 * enabled.
 */
static int
sc_ensure_containers_are_loaded(struct scrub_ctx *ctx)
{
	vos_iter_param_t	param = {0};
	struct cont_are_loaded_args args = {0};
	int			rc = 0;

	if (ctx->sc_cont_loaded)
		return 0;

	args.args_ctx = ctx;
	param.ip_hdl = ctx->sc_vos_pool_hdl;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	do {
		struct vos_iter_anchors	anchors = {0};

		args.args_found_unloaded_container = false;
		rc = vos_iterate(&param, VOS_ITER_COUUID, false, &anchors, NULL,
				 cont_iter_is_loaded_cb, &args, NULL);
		sc_sleep(ctx, 500);
	} while (args.args_found_unloaded_container || rc != 0);
	ctx->sc_cont_loaded = true;

	return rc;
}

int
vos_scrub_pool(struct scrub_ctx *ctx)
{
	vos_iter_param_t	param = {0};
	struct vos_iter_anchors	anchor = {0};
	int			rc = 0;

	ctx->sc_status = SCRUB_STATUS_NOT_RUNNING;
	if (daos_handle_is_inval(ctx->sc_vos_pool_hdl)) {
		D_ERROR("vos_iter_handle is invalid.\n");
		return -DER_INVAL;
	}

	if (!sc_should_start(ctx))
		return 0;

	rc = sc_ensure_containers_are_loaded(ctx);
	if (rc != 0) {
		D_ERROR("Error ensuring containers are loaded: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	sc_pool_start(ctx);

	param.ip_hdl = ctx->sc_vos_pool_hdl;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	rc = vos_iterate(&param, VOS_ITER_COUUID, false, &anchor,
			 NULL, cont_iter_scrub_cb, ctx, NULL);
	sc_scrub_count_inc(ctx);
	sc_pool_stop(ctx);
	if (rc == SCRUB_POOL_OFF)
		return 0;

	if (sc_is_first_pass(ctx))
		ctx->sc_first_pass_done = true;

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
