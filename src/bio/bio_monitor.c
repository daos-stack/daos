/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(bio)

#include <spdk/nvme.h>
#include <spdk/bdev.h>
#include <spdk/blob.h>
#include <spdk/thread.h>
#include <spdk/nvme_intel.h>
#include <spdk/util.h>
#include "bio_internal.h"
#include <daos_srv/smd.h>

/* Used to preallocate buffer to query error log pages from SPDK health info */
#define NVME_MAX_ERROR_LOG_PAGES	256

/*
 * Used for getting bio device state, which requires exclusive access from
 * the device owner xstream.
 */
struct dev_state_msg_arg {
	struct bio_xs_context		*xs;
	struct nvme_stats		 devstate;
	ABT_eventual			 eventual;
};

/*
 * Used for getting bio device list, which requires exclusive access from
 * the init xstream.
 */
struct bio_dev_list_msg_arg {
	struct bio_xs_context		*xs;
	d_list_t			*dev_list;
	int				 dev_list_cnt;
	ABT_eventual			 eventual;
	int				 rc;
};


/* Collect space utilization for blobstore */
static void
collect_bs_usage(struct spdk_blob_store *bs, struct nvme_stats *stats)
{
	uint64_t	cl_sz;

	if (bs == NULL)
		return;

	D_ASSERT(stats != NULL);

	cl_sz = spdk_bs_get_cluster_size(bs);
	stats->total_bytes = spdk_bs_total_data_cluster_count(bs) * cl_sz;
	stats->avail_bytes = spdk_bs_free_cluster_count(bs) * cl_sz;
}

/* Copy out the nvme_stats in the device owner xstream context */
static void
bio_get_dev_state_internal(void *msg_arg)
{
	struct dev_state_msg_arg	*dsm = msg_arg;

	D_ASSERT(dsm != NULL);

	dsm->devstate = dsm->xs->bxc_blobstore->bb_dev_health.bdh_health_state;
	collect_bs_usage(dsm->xs->bxc_blobstore->bb_bs, &dsm->devstate);
	DABT_EVENTUAL_SET(dsm->eventual, NULL, 0);
}

static void
bio_dev_set_faulty_internal(void *msg_arg)
{
	struct dev_state_msg_arg	*dsm = msg_arg;
	int				 rc;

	D_ASSERT(dsm != NULL);

	rc = bio_bs_state_set(dsm->xs->bxc_blobstore, BIO_BS_STATE_FAULTY);
	if (rc)
		D_ERROR("BIO FAULTY state set failed, rc=%d\n", rc);

	rc = bio_bs_state_transit(dsm->xs->bxc_blobstore);
	if (rc)
		D_ERROR("State transition failed, rc=%d\n", rc);

	DABT_EVENTUAL_SET(dsm->eventual, &rc, sizeof(rc));
}

/* Call internal method to increment CSUM media error. */
void
bio_log_csum_err(struct bio_xs_context *bxc, int tgt_id)
{
	struct media_error_msg	*mem;

	if (bxc->bxc_blobstore == NULL)
		return;
	D_ALLOC_PTR(mem); /* mem is freed in bio_media_error */
	if (mem == NULL)
		return;
	mem->mem_bs		= bxc->bxc_blobstore;
	mem->mem_err_type	= MET_CSUM;
	mem->mem_tgt_id		= tgt_id;
	spdk_thread_send_msg(owner_thread(mem->mem_bs), bio_media_error, mem);
}


/* Call internal method to get BIO device state from the device owner xstream */
int
bio_get_dev_state(struct nvme_stats *state, struct bio_xs_context *xs)
{
	struct dev_state_msg_arg	 dsm = { 0 };
	int				 rc;

	rc = ABT_eventual_create(0, &dsm.eventual);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);

	dsm.xs = xs;

	spdk_thread_send_msg(owner_thread(xs->bxc_blobstore),
			     bio_get_dev_state_internal, &dsm);
	DABT_EVENTUAL_WAIT(dsm.eventual, NULL);

	*state = dsm.devstate;

	DABT_EVENTUAL_FREE(&dsm.eventual);

	return 0;
}

/*
 * Copy out the internal BIO blobstore device state.
 */
void
bio_get_bs_state(int *bs_state, struct bio_xs_context *xs)
{
	*bs_state = xs->bxc_blobstore->bb_state;
}

/*
 * Call internal method to set BIO device state to FAULTY and trigger device
 * state transition. Called from the device owner xstream.
 */
int
bio_dev_set_faulty(struct bio_xs_context *xs)
{
	struct dev_state_msg_arg	dsm = { 0 };
	int				rc;
	int				*dsm_rc;

	rc = ABT_eventual_create(sizeof(*dsm_rc), &dsm.eventual);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);

	dsm.xs = xs;

	spdk_thread_send_msg(owner_thread(xs->bxc_blobstore),
			     bio_dev_set_faulty_internal, &dsm);
	DABT_EVENTUAL_WAIT(dsm.eventual, (void **)&dsm_rc);
	rc = *dsm_rc;

	DABT_EVENTUAL_FREE(&dsm.eventual);

	return rc;
}

static inline struct bio_dev_health *
xs_ctxt2dev_health(struct bio_xs_context *ctxt)
{
	D_ASSERT(ctxt != NULL);
	/* bio_xsctxt_free() is underway */
	if (ctxt->bxc_blobstore == NULL)
		return NULL;

	return &ctxt->bxc_blobstore->bb_dev_health;
}

static void
get_spdk_err_log_page_completion(struct spdk_bdev_io *bdev_io, bool success,
				 void *cb_arg)
{
	struct bio_xs_context	*ctxt = cb_arg;
	struct bio_dev_health	*dev_health = xs_ctxt2dev_health(ctxt);
	int			 sc, sct;
	uint32_t		 cdw0;

	if (dev_health == NULL)
		goto out;

	D_ASSERT(dev_health->bdh_inflights == 1);

	/* Additional NVMe status information */
	spdk_bdev_io_get_nvme_status(bdev_io, &cdw0, &sct, &sc);
	if (sc)
		D_ERROR("NVMe status code/type: %d/%d\n", sc, sct);

	/*Decrease inflights on error or successful callback completion chain*/
	dev_health->bdh_inflights--;
out:
	/* Free I/O request in the completion callback */
	spdk_bdev_free_io(bdev_io);
}

static void
get_spdk_identify_ctrlr_completion(struct spdk_bdev_io *bdev_io, bool success,
				   void *cb_arg)
{
	struct bio_xs_context		*ctxt = cb_arg;
	struct bio_dev_health		*dev_health = xs_ctxt2dev_health(ctxt);
	struct spdk_nvme_ctrlr_data	*cdata;
	struct spdk_bdev		*bdev;
	struct spdk_nvme_cmd		 cmd;
	uint32_t			 ep_sz;
	uint32_t			 ep_buf_sz;
	uint32_t			 numd, numdl, numdu;
	int				 rc;
	int				 sc, sct;
	uint32_t			 cdw0;

	if (dev_health == NULL)
		goto out;

	D_ASSERT(dev_health->bdh_inflights == 1);

	/* Additional NVMe status information */
	spdk_bdev_io_get_nvme_status(bdev_io, &cdw0, &sct, &sc);
	if (sc) {
		D_ERROR("NVMe status code/type: %d/%d\n", sc, sct);
		dev_health->bdh_inflights--;
		goto out;
	}

	D_ASSERT(dev_health->bdh_io_channel != NULL);
	bdev = spdk_bdev_desc_get_bdev(dev_health->bdh_desc);
	D_ASSERT(bdev != NULL);

	/* Prep NVMe command to get device error log pages */
	ep_sz = sizeof(struct spdk_nvme_error_information_entry);
	numd = ep_sz / sizeof(uint32_t) - 1u;
	numdl = numd & 0xFFFFu;
	numdu = (numd >> 16) & 0xFFFFu;
	memset(&cmd, 0, sizeof(cmd));
	cmd.opc = SPDK_NVME_OPC_GET_LOG_PAGE;
	cmd.nsid = SPDK_NVME_GLOBAL_NS_TAG;
	cmd.cdw10 = numdl << 16;
	cmd.cdw10 |= SPDK_NVME_LOG_ERROR;
	cmd.cdw11 = numdu;
	cdata = dev_health->bdh_ctrlr_buf;
	ep_buf_sz = ep_sz * (cdata->elpe + 1);

	/*
	 * Submit an NVMe Admin command to get device error log page
	 * to the bdev.
	 */
	rc = spdk_bdev_nvme_admin_passthru(dev_health->bdh_desc,
					   dev_health->bdh_io_channel,
					   &cmd,
					   dev_health->bdh_error_buf,
					   ep_buf_sz,
					   get_spdk_err_log_page_completion,
					   ctxt);
	if (rc) {
		D_ERROR("NVMe admin passthru (error log), rc:%d\n", rc);
		dev_health->bdh_inflights--;
	}

out:
	/* Free I/O request in the completion callback */
	spdk_bdev_free_io(bdev_io);
}

/* Return a uint64 raw value from a byte array */
static uint64_t
extend_to_uint64(uint8_t *array, unsigned int len)
{
	uint64_t value = 0;
	int i = len;

	while (i > 0 && len <= 8) {
		value += (uint64_t)array[i - 1] << (8 * (i - 1));
		i--;
	}

	return value;
}

static void
populate_intel_smart_stats(struct bio_dev_health *bdh)
{
	struct spdk_nvme_intel_smart_information_page	*isp;
	struct nvme_stats				*stats;
	struct spdk_nvme_intel_smart_attribute		atb;
	int i;

	isp		= bdh->bdh_intel_smart_buf;
	stats		= &bdh->bdh_health_state;

	/** Intel vendor unique SMART attributes */
	for (i = 0; i < SPDK_COUNTOF(isp->attributes); i++) {
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_PROGRAM_FAIL_COUNT) {
			atb = isp->attributes[i];
			stats->program_fail_cnt_norm = atb.normalized_value;
			d_tm_set_counter(bdh->bdh_prog_fail_cnt_norm,
					 atb.normalized_value);
			stats->program_fail_cnt_raw =
					extend_to_uint64(atb.raw_value, 6);
			d_tm_set_counter(bdh->bdh_prog_fail_cnt_raw,
					 stats->program_fail_cnt_raw);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_ERASE_FAIL_COUNT) {
			atb = isp->attributes[i];
			stats->erase_fail_cnt_norm = atb.normalized_value;
			d_tm_set_counter(bdh->bdh_erase_fail_cnt_norm,
					 atb.normalized_value);
			stats->erase_fail_cnt_raw =
					extend_to_uint64(atb.raw_value, 6);
			d_tm_set_counter(bdh->bdh_erase_fail_cnt_raw,
					 stats->erase_fail_cnt_raw);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_WEAR_LEVELING_COUNT) {
			atb = isp->attributes[i];
			stats->wear_leveling_cnt_norm = atb.normalized_value;
			d_tm_set_counter(bdh->bdh_wear_leveling_cnt_norm,
					 atb.normalized_value);
			stats->wear_leveling_cnt_min = atb.raw_value[0] |
						       atb.raw_value[1] << 8;
			d_tm_set_counter(bdh->bdh_wear_leveling_cnt_min,
					 stats->wear_leveling_cnt_min);
			stats->wear_leveling_cnt_max = atb.raw_value[2] |
						       atb.raw_value[3] << 8;
			d_tm_set_counter(bdh->bdh_wear_leveling_cnt_max,
					 stats->wear_leveling_cnt_max);
			stats->wear_leveling_cnt_avg = atb.raw_value[4] |
						       atb.raw_value[5] << 8;
			d_tm_set_counter(bdh->bdh_wear_leveling_cnt_avg,
					 stats->wear_leveling_cnt_avg);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_E2E_ERROR_COUNT) {
			atb = isp->attributes[i];
			stats->endtoend_err_cnt_raw =
					extend_to_uint64(atb.raw_value, 6);
			d_tm_set_counter(bdh->bdh_endtoend_err_cnt_raw,
					 stats->endtoend_err_cnt_raw);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_CRC_ERROR_COUNT) {
			atb = isp->attributes[i];
			stats->crc_err_cnt_raw =
					extend_to_uint64(atb.raw_value, 6);
			d_tm_set_counter(bdh->bdh_crc_err_cnt_raw,
					 stats->crc_err_cnt_raw);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_MEDIA_WEAR) {
			atb = isp->attributes[i];
			/* divide raw value by 1024 to derive the percentage */
			stats->media_wear_raw =
					extend_to_uint64(atb.raw_value, 6);
			d_tm_set_counter(bdh->bdh_media_wear_raw,
					 stats->media_wear_raw);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_HOST_READ_PERCENTAGE) {
			atb = isp->attributes[i];
			stats->host_reads_raw =
					extend_to_uint64(atb.raw_value, 6);
			d_tm_set_counter(bdh->bdh_host_reads_raw,
					 stats->host_reads_raw);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_TIMER) {
			atb = isp->attributes[i];
			stats->workload_timer_raw =
					extend_to_uint64(atb.raw_value, 6);
			d_tm_set_counter(bdh->bdh_workload_timer_raw,
					 stats->workload_timer_raw);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_THERMAL_THROTTLE_STATUS) {
			atb = isp->attributes[i];
			stats->thermal_throttle_status = atb.raw_value[0];
			d_tm_set_counter(bdh->bdh_thermal_throttle_status,
					 stats->thermal_throttle_status);
			stats->thermal_throttle_event_cnt =
					extend_to_uint64(&atb.raw_value[1], 4);
			d_tm_set_counter(bdh->bdh_thermal_throttle_event_cnt,
					 stats->thermal_throttle_event_cnt);
		}
		if (isp->attributes[i].code ==
			  SPDK_NVME_INTEL_SMART_RETRY_BUFFER_OVERFLOW_COUNTER) {
			atb = isp->attributes[i];
			stats->retry_buffer_overflow_cnt =
					extend_to_uint64(atb.raw_value, 6);
			d_tm_set_counter(bdh->bdh_retry_buffer_overflow_cnt,
					 stats->retry_buffer_overflow_cnt);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_PLL_LOCK_LOSS_COUNT) {
			atb = isp->attributes[i];
			stats->pll_lock_loss_cnt =
					extend_to_uint64(atb.raw_value, 6);
			d_tm_set_counter(bdh->bdh_pll_lock_loss_cnt,
					 stats->pll_lock_loss_cnt);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_NAND_BYTES_WRITTEN) {
			atb = isp->attributes[i];
			stats->nand_bytes_written =
					extend_to_uint64(atb.raw_value, 6);
			d_tm_set_counter(bdh->bdh_nand_bytes_written,
					 stats->nand_bytes_written);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_HOST_BYTES_WRITTEN) {
			atb = isp->attributes[i];
			stats->host_bytes_written =
					extend_to_uint64(atb.raw_value, 6);
			d_tm_set_counter(bdh->bdh_host_bytes_written,
					 stats->host_bytes_written);
		}
	}
}

static void
populate_health_stats(struct bio_dev_health *bdh)
{
	struct spdk_nvme_health_information_page	*page;
	struct nvme_stats				*dev_state;
	union spdk_nvme_critical_warning_state		cw;

	page		= bdh->bdh_health_buf;
	cw		= page->critical_warning;
	dev_state	= &bdh->bdh_health_state;

	/** commands */
	d_tm_set_counter(bdh->bdh_du_written, page->data_units_written[0]);
	d_tm_set_counter(bdh->bdh_du_read, page->data_units_read[0]);
	d_tm_set_counter(bdh->bdh_write_cmds, page->host_write_commands[0]);
	d_tm_set_counter(bdh->bdh_read_cmds, page->host_read_commands[0]);
	dev_state->ctrl_busy_time	= page->controller_busy_time[0];
	d_tm_set_counter(bdh->bdh_ctrl_busy_time,
			 page->controller_busy_time[0]);
	dev_state->media_errs		= page->media_errors[0];
	d_tm_set_counter(bdh->bdh_media_errs, page->media_errors[0]);

	dev_state->power_cycles		= page->power_cycles[0];
	d_tm_set_counter(bdh->bdh_power_cycles, page->power_cycles[0]);
	dev_state->power_on_hours	= page->power_on_hours[0];
	d_tm_set_counter(bdh->bdh_power_on_hours, page->power_on_hours[0]);
	dev_state->unsafe_shutdowns	= page->unsafe_shutdowns[0];
	d_tm_set_counter(bdh->bdh_unsafe_shutdowns,
			 page->unsafe_shutdowns[0]);

	/** temperature */
	dev_state->warn_temp_time	= page->warning_temp_time;
	d_tm_set_counter(bdh->bdh_temp_warn_time, page->warning_temp_time);
	dev_state->crit_temp_time	= page->critical_temp_time;
	d_tm_set_counter(bdh->bdh_temp_crit_time, page->critical_temp_time);
	dev_state->temperature		= page->temperature;
	d_tm_set_gauge(bdh->bdh_temp, page->temperature);
	dev_state->temp_warn		= cw.bits.temperature ? true : false;
	d_tm_set_gauge(bdh->bdh_temp_warn, dev_state->temp_warn);

	/** reliability */
	d_tm_set_counter(bdh->bdh_avail_spare, page->available_spare);
	d_tm_set_counter(bdh->bdh_avail_spare_thres,
			 page->available_spare_threshold);
	dev_state->avail_spare_warn	= cw.bits.available_spare ? true
								  : false;
	d_tm_set_gauge(bdh->bdh_avail_spare_warn, dev_state->avail_spare_warn);
	dev_state->dev_reliability_warn	= cw.bits.device_reliability ? true
								     : false;
	d_tm_set_gauge(bdh->bdh_reliability_warn,
		       dev_state->dev_reliability_warn);

	/** various critical warnings */
	dev_state->read_only_warn	= cw.bits.read_only ? true : false;
	d_tm_set_gauge(bdh->bdh_read_only_warn, dev_state->read_only_warn);
	dev_state->volatile_mem_warn	= cw.bits.volatile_memory_backup ? true
									: false;
	d_tm_set_gauge(bdh->bdh_volatile_mem_warn,
		       dev_state->volatile_mem_warn);

	/** number of error log entries, internal use */
	dev_state->err_log_entries = page->num_error_info_log_entries[0];
}

static void
get_spdk_intel_smart_log_completion(struct spdk_bdev_io *bdev_io, bool success,
				    void *cb_arg)
{
	struct bio_xs_context	*ctxt = cb_arg;
	struct bio_dev_health	*dev_health = xs_ctxt2dev_health(ctxt);
	struct spdk_bdev	*bdev;
	struct spdk_nvme_cmd	 cmd;
	uint32_t		 cp_sz;
	int			 rc, sc, sct;
	uint32_t		 cdw0;

	if (dev_health == NULL)
		goto out;

	D_ASSERT(dev_health->bdh_inflights == 1);

	/* Additional NVMe status information */
	spdk_bdev_io_get_nvme_status(bdev_io, &cdw0, &sct, &sc);
	if (sc) {
		D_ERROR("NVMe status code/type: %d/%d\n", sc, sct);
		dev_health->bdh_inflights--;
		goto out;
	}

	D_ASSERT(dev_health->bdh_io_channel != NULL);
	bdev = spdk_bdev_desc_get_bdev(dev_health->bdh_desc);
	D_ASSERT(bdev != NULL);

	/* Store Intel SMART stats in in-memory health state log. */
	if (dev_health->bdh_vendor_id == SPDK_PCI_VID_INTEL)
		populate_intel_smart_stats(dev_health);

	/* Prep NVMe command to get controller data */
	cp_sz = sizeof(struct spdk_nvme_ctrlr_data);
	memset(&cmd, 0, sizeof(cmd));
	cmd.opc = SPDK_NVME_OPC_IDENTIFY;
	cmd.cdw10 = SPDK_NVME_IDENTIFY_CTRLR;

	/*
	 * Submit an NVMe Admin command to get controller data
	 * to the bdev.
	 */
	rc = spdk_bdev_nvme_admin_passthru(dev_health->bdh_desc,
					   dev_health->bdh_io_channel,
					   &cmd,
					   dev_health->bdh_ctrlr_buf,
					   cp_sz,
					   get_spdk_identify_ctrlr_completion,
					   ctxt);
	if (rc) {
		D_ERROR("NVMe admin passthru (identify ctrlr), rc:%d\n", rc);
		dev_health->bdh_inflights--;
	}

out:
	/* Free I/O request in the completion callback */
	spdk_bdev_free_io(bdev_io);
}

static void
get_spdk_health_info_completion(struct spdk_bdev_io *bdev_io, bool success,
			     void *cb_arg)
{
	struct bio_xs_context	*ctxt = cb_arg;
	struct bio_dev_health	*dev_health = xs_ctxt2dev_health(ctxt);
	struct spdk_bdev	*bdev;
	struct spdk_nvme_cmd	 cmd;
	uint32_t		 page_sz;
	uint32_t		 numd, numdl, numdu;
	int			 rc, sc, sct;
	uint32_t		 cdw0;

	if (dev_health == NULL)
		goto out;

	D_ASSERT(dev_health->bdh_inflights == 1);

	/* Additional NVMe status information */
	spdk_bdev_io_get_nvme_status(bdev_io, &cdw0, &sct, &sc);
	if (sc) {
		D_ERROR("NVMe status code/type: %d/%d\n", sc, sct);
		dev_health->bdh_inflights--;
		goto out;
	}

	D_ASSERT(dev_health->bdh_io_channel != NULL);
	bdev = spdk_bdev_desc_get_bdev(dev_health->bdh_desc);
	D_ASSERT(bdev != NULL);

	/* Store device health info in in-memory health state log. */
	dev_health->bdh_health_state.timestamp = daos_wallclock_secs();
	populate_health_stats(dev_health);

	/* Prep NVMe command to get SPDK Intel NVMe SSD Smart Attributes */
	if (dev_health->bdh_vendor_id != SPDK_PCI_VID_INTEL) {
		get_spdk_intel_smart_log_completion(bdev_io, true, ctxt);
		return;
	}
	page_sz = sizeof(struct spdk_nvme_intel_smart_information_page);
	numd = page_sz / sizeof(uint32_t) - 1u;
	numdl = numd & 0xFFFFu;
	numdu = (numd >> 16) & 0xFFFFu;
	memset(&cmd, 0, sizeof(cmd));
	cmd.opc = SPDK_NVME_OPC_GET_LOG_PAGE;
	cmd.nsid = SPDK_NVME_GLOBAL_NS_TAG;
	cmd.cdw10 = numdl << 16;
	cmd.cdw10 |= SPDK_NVME_INTEL_LOG_SMART;
	cmd.cdw11 = numdu;

	/*
	 * Submit an NVMe Admin command to get NVMe vendor unique smart
	 * attributes.
	 */
	rc = spdk_bdev_nvme_admin_passthru(dev_health->bdh_desc,
					   dev_health->bdh_io_channel,
					   &cmd,
					   dev_health->bdh_intel_smart_buf,
					   page_sz,
					   get_spdk_intel_smart_log_completion,
					   ctxt);
	if (rc) {
		D_ERROR("NVMe admin passthru (Intel smart log), rc:%d\n", rc);
		dev_health->bdh_inflights--;
	}

out:
	/* Free I/O request in the completion callback */
	spdk_bdev_free_io(bdev_io);
}

static int
auto_detect_faulty(struct bio_blobstore *bbs)
{
	uint64_t	tgtidx;
	int		i;

	if (bbs->bb_state != BIO_BS_STATE_NORMAL)
		return 0;
	/*
	 * TODO: Check the health data stored in @bbs, and mark the bbs as
	 *	 faulty when certain faulty criteria are satisfied.
	 */

	/*
	 * Used for DAOS NVMe Recovery Tests. Will trigger bs faulty reaction
	 * only if the specified target is assigned to the device.
	 */
	if (DAOS_FAIL_CHECK(DAOS_NVME_FAULTY)) {
		tgtidx = daos_fail_value_get();
		for (i = 0; i < bbs->bb_ref; i++) {
			if (bbs->bb_xs_ctxts[i]->bxc_tgt_id == tgtidx)
				return bio_bs_state_set(bbs,
							BIO_BS_STATE_FAULTY);
		}
	}

	return 0;
}

/* Collect the raw device health state through SPDK admin APIs */
static void
collect_raw_health_data(struct bio_xs_context *ctxt)
{
	struct bio_dev_health	*dev_health = xs_ctxt2dev_health(ctxt);
	struct spdk_bdev	*bdev;
	struct spdk_nvme_cmd	 cmd;
	uint32_t		 numd, numdl, numdu;
	uint32_t		 page_sz;
	int			 rc;

	D_ASSERT(dev_health != NULL);
	if (dev_health->bdh_desc == NULL)
		return;

	D_ASSERT(dev_health->bdh_io_channel != NULL);

	bdev = spdk_bdev_desc_get_bdev(dev_health->bdh_desc);
	if (bdev == NULL) {
		D_ERROR("No bdev associated with device health descriptor\n");
		return;
	}

	if (get_bdev_type(bdev) != BDEV_CLASS_NVME)
		return;

	if (!spdk_bdev_io_type_supported(bdev, SPDK_BDEV_IO_TYPE_NVME_ADMIN)) {
		D_ERROR("Bdev NVMe admin passthru not supported!\n");
		return;
	}

	/* Check to avoid parallel SPDK device health query calls */
	if (dev_health->bdh_inflights)
		return;
	dev_health->bdh_inflights++;

	/* Prep NVMe command to get SPDK device health data */
	page_sz = sizeof(struct spdk_nvme_health_information_page);
	numd = page_sz / sizeof(uint32_t) - 1u;
	numdl = numd & 0xFFFFu;
	numdu = (numd >> 16) & 0xFFFFu;
	memset(&cmd, 0, sizeof(cmd));
	cmd.opc = SPDK_NVME_OPC_GET_LOG_PAGE;
	cmd.nsid = SPDK_NVME_GLOBAL_NS_TAG;
	cmd.cdw10 = numdl << 16;
	cmd.cdw10 |= SPDK_NVME_LOG_HEALTH_INFORMATION;
	cmd.cdw11 = numdu;

	/*
	 * Submit an NVMe Admin command to get device health log page
	 * to the bdev.
	 */
	rc = spdk_bdev_nvme_admin_passthru(dev_health->bdh_desc,
					   dev_health->bdh_io_channel,
					   &cmd,
					   dev_health->bdh_health_buf,
					   page_sz,
					   get_spdk_health_info_completion,
					   ctxt);
	if (rc) {
		D_ERROR("NVMe admin passthru (health log), rc:%d\n", rc);
		dev_health->bdh_inflights--;
	}
}

void
bio_bs_monitor(struct bio_xs_context *ctxt, uint64_t now)
{
	struct bio_dev_health	*dev_health;
	struct bio_blobstore	*bbs;
	int			 rc;
	uint64_t		 monitor_period;

	D_ASSERT(ctxt != NULL);
	bbs = ctxt->bxc_blobstore;

	D_ASSERT(bbs != NULL);
	dev_health = &bbs->bb_dev_health;

	if (bbs->bb_state == BIO_BS_STATE_NORMAL ||
	    bbs->bb_state == BIO_BS_STATE_OUT)
		monitor_period = NVME_MONITOR_PERIOD;
	else
		monitor_period = NVME_MONITOR_SHORT_PERIOD;

	if (dev_health->bdh_stat_age + monitor_period >= now)
		return;
	dev_health->bdh_stat_age = now;

	rc = auto_detect_faulty(bbs);
	if (rc)
		D_ERROR("Auto faulty detect on target %d failed. %d\n",
			ctxt->bxc_tgt_id, rc);

	rc = bio_bs_state_transit(bbs);
	if (rc)
		D_ERROR("State transition on target %d failed. %d\n",
			ctxt->bxc_tgt_id, rc);

	if (!bypass_health_collect())
		collect_raw_health_data(ctxt);
}

/* Free all device health monitoring info */
void
bio_fini_health_monitoring(struct bio_blobstore *bb)
{
	struct bio_dev_health	*bdh = &bb->bb_dev_health;

	/* Free NVMe admin passthru DMA buffers */
	if (bdh->bdh_health_buf) {
		spdk_dma_free(bdh->bdh_health_buf);
		bdh->bdh_health_buf = NULL;
	}
	if (bdh->bdh_ctrlr_buf) {
		spdk_dma_free(bdh->bdh_ctrlr_buf);
		bdh->bdh_ctrlr_buf = NULL;
	}
	if (bdh->bdh_error_buf) {
		spdk_dma_free(bdh->bdh_error_buf);
		bdh->bdh_error_buf = NULL;
	}
	if (bdh->bdh_intel_smart_buf) {
		spdk_dma_free(bdh->bdh_intel_smart_buf);
		bdh->bdh_intel_smart_buf = NULL;
	}

	/* Release I/O channel reference */
	if (bdh->bdh_io_channel) {
		spdk_put_io_channel(bdh->bdh_io_channel);
		bdh->bdh_io_channel = NULL;
	}

	/* Close device health monitoring descriptor */
	if (bdh->bdh_desc) {
		spdk_bdev_close(bdh->bdh_desc);
		bdh->bdh_desc = NULL;
	}
}

/*
 * Allocate device monitoring health data struct and preallocate
 * all SPDK DMA-safe buffers for querying log entries.
 */
int
bio_init_health_monitoring(struct bio_blobstore *bb, char *bdev_name)
{
	struct spdk_io_channel		*channel;
	uint32_t			 hp_sz;
	uint32_t			 cp_sz;
	uint32_t			 ep_sz;
	uint32_t			 ep_buf_sz;
	uint32_t			 isp_sz;
	int				 rc;

	D_ASSERT(bb != NULL);
	D_ASSERT(bdev_name != NULL);

	if (bypass_health_collect())
		return 0;

	hp_sz = sizeof(struct spdk_nvme_health_information_page);
	bb->bb_dev_health.bdh_health_buf = spdk_dma_zmalloc(hp_sz, 0, NULL);
	if (bb->bb_dev_health.bdh_health_buf == NULL)
		return -DER_NOMEM;

	cp_sz = sizeof(struct spdk_nvme_ctrlr_data);
	bb->bb_dev_health.bdh_ctrlr_buf = spdk_dma_zmalloc(cp_sz, 0, NULL);
	if (bb->bb_dev_health.bdh_ctrlr_buf == NULL) {
		rc = -DER_NOMEM;
		goto free_health_buf;
	}

	ep_sz = sizeof(struct spdk_nvme_error_information_entry);
	ep_buf_sz = ep_sz * NVME_MAX_ERROR_LOG_PAGES;
	bb->bb_dev_health.bdh_error_buf = spdk_dma_zmalloc(ep_buf_sz, 0, NULL);
	if (bb->bb_dev_health.bdh_error_buf == NULL) {
		rc = -DER_NOMEM;
		goto free_ctrlr_buf;
	}

	isp_sz = sizeof(struct spdk_nvme_intel_smart_information_page);
	bb->bb_dev_health.bdh_intel_smart_buf = spdk_dma_zmalloc(isp_sz, 0,
								 NULL);
	if (bb->bb_dev_health.bdh_intel_smart_buf == NULL) {
		rc = -DER_NOMEM;
		goto free_error_buf;
	}

	bb->bb_dev_health.bdh_inflights = 0;

	if (bb->bb_state == BIO_BS_STATE_OUT)
		return 0;

	 /* Writable descriptor required for device health monitoring */
	rc = spdk_bdev_open_ext(bdev_name, true, bio_bdev_event_cb, NULL,
				&bb->bb_dev_health.bdh_desc);
	if (rc != 0) {
		D_ERROR("Failed to open bdev %s, %d\n", bdev_name, rc);
		rc = daos_errno2der(-rc);
		goto free_smart_buf;
	}

	/* Get and hold I/O channel for device health monitoring */
	channel = spdk_bdev_get_io_channel(bb->bb_dev_health.bdh_desc);
	D_ASSERT(channel != NULL);
	bb->bb_dev_health.bdh_io_channel = channel;

	/* Set the NVMe SSD PCI Vendor ID */
	bio_set_vendor_id(bb, bdev_name);
	/* Register DAOS metrics to export NVMe SSD health stats */
	bio_export_health_stats(bb, bdev_name);
	bio_export_vendor_health_stats(bb, bdev_name);

	return 0;

free_smart_buf:
	spdk_dma_free(bb->bb_dev_health.bdh_intel_smart_buf);
	bb->bb_dev_health.bdh_intel_smart_buf = NULL;
free_error_buf:
	spdk_dma_free(bb->bb_dev_health.bdh_error_buf);
	bb->bb_dev_health.bdh_error_buf = NULL;
free_ctrlr_buf:
	spdk_dma_free(bb->bb_dev_health.bdh_ctrlr_buf);
	bb->bb_dev_health.bdh_ctrlr_buf = NULL;
free_health_buf:
	spdk_dma_free(bb->bb_dev_health.bdh_health_buf);
	bb->bb_dev_health.bdh_health_buf = NULL;

	return rc;
}

/*
 * Register DAOS metrics to export NVMe SSD health stats.
 */
void
bio_export_health_stats(struct bio_blobstore *bb, char *bdev_name)
{
	struct bio_dev_info		*binfo;
	int				 rc;

	D_ALLOC_PTR(binfo);
	if (binfo == NULL) {
		D_WARN("Failed to allocate binfo\n");
		return;
	}
#define X(field, fname, desc, unit, type)				\
	memset(binfo, 0, sizeof(*binfo));				\
	rc = fill_in_traddr(binfo, bdev_name);				\
	if (rc || binfo->bdi_traddr == NULL) {				\
		D_WARN("Failed to extract %s addr: "DF_RC"\n",		\
		       bdev_name, DP_RC(rc));				\
	} else {							\
		rc = d_tm_add_metric(&bb->bb_dev_health.field,		\
				     type,				\
				     desc,				\
				     unit,				\
				     "/nvme/%s/%s",			\
				     binfo->bdi_traddr,			\
				     fname);				\
		if (rc)							\
			D_WARN("Failed to create %s sensor for %s: "	\
			       DF_RC"\n", fname, bdev_name, DP_RC(rc));	\
		D_FREE(binfo->bdi_traddr);				\
	}

	BIO_PROTO_NVME_STATS_LIST
#undef X
	D_FREE(binfo);
}

/*
 * Register DAOS metrics to export Intel Vendor SMART NVMe SSD attributes.
 */
void
bio_export_vendor_health_stats(struct bio_blobstore *bb, char *bdev_name)
{
	struct bio_dev_info		*binfo;
	int				 rc;

	D_ALLOC_PTR(binfo);
	if (binfo == NULL) {
		D_WARN("Failed to allocate binfo\n");
		return;
	}
#define Y(field, fname, desc, unit, type)				\
	memset(binfo, 0, sizeof(*binfo));				\
	rc = fill_in_traddr(binfo, bdev_name);				\
	if (rc || binfo->bdi_traddr == NULL) {				\
		D_WARN("Failed to extract %s addr: "DF_RC"\n",		\
		       bdev_name, DP_RC(rc));				\
	} else {							\
		rc = d_tm_add_metric(&bb->bb_dev_health.field,		\
				     type,				\
				     desc,				\
				     unit,				\
				     "/nvme/%s/%s",			\
				     binfo->bdi_traddr,			\
				     fname);				\
		if (rc)							\
			D_WARN("Failed to create %s sensor for %s: "	\
			       DF_RC"\n", fname, bdev_name, DP_RC(rc));	\
		D_FREE(binfo->bdi_traddr);				\
	}

	BIO_PROTO_NVME_VENDOR_STATS_LIST
#undef Y
	D_FREE(binfo);
}


/*
 * Set the PCI Vendor ID for the NVMe SSD. This is used to determine if
 * Intel SMART stats will be monitored (vendor specific).
 */
void
bio_set_vendor_id(struct bio_blobstore *bb, char *bdev_name)
{
	struct bio_dev_info		 binfo = { 0 };
	struct spdk_pci_addr		 pci_addr;
	struct spdk_pci_device		*pci_device;
	uint16_t			 vid = 0;
	int				 rc;

	rc = fill_in_traddr(&binfo, bdev_name);
	if (rc || binfo.bdi_traddr == NULL) {
		D_ERROR("Unable to get traddr for device:%s\n", bdev_name);
		return;
	}

	if (spdk_pci_addr_parse(&pci_addr, binfo.bdi_traddr)) {
		D_ERROR("Unable to parse PCI address: %s\n", binfo.bdi_traddr);
		goto free_traddr;
	}

	for (pci_device = spdk_pci_get_first_device(); pci_device != NULL;
	     pci_device = spdk_pci_get_next_device(pci_device)) {
		if (spdk_pci_addr_compare(&pci_addr, &pci_device->addr) == 0) {
			vid = spdk_pci_device_get_vendor_id(pci_device);
			break;
		}
	}
	bb->bb_dev_health.bdh_vendor_id = vid;

free_traddr:
	D_FREE(binfo.bdi_traddr);
}
