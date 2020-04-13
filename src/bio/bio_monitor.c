/**
 * (C) Copyright 2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B620873.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#define D_LOGFAC	DD_FAC(bio)

#include <spdk/nvme.h>
#include <spdk/bdev.h>
#include <spdk/io_channel.h>
#include "bio_internal.h"
#include <daos_srv/smd.h>

/* Used to preallocate buffer to query error log pages from SPDK health info */
#define NVME_MAX_ERROR_LOG_PAGES	256

/*
 * Used for getting bio device state, which requires exclusive access from
 * the device owner xstream.
 */
struct dev_state_msg_arg {
	struct bio_xs_context	*xs;
	struct bio_dev_state	 devstate;
	ABT_eventual		 eventual;
};

/* Copy out the bio_dev_state in the device owner xstream context */
static void
bio_get_dev_state_internal(void *msg_arg)
{
	struct dev_state_msg_arg	*dsm = msg_arg;

	D_ASSERT(dsm != NULL);

	dsm->devstate = dsm->xs->bxc_blobstore->bb_dev_health.bdh_health_state;
	ABT_eventual_set(dsm->eventual, NULL, 0);
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

	ABT_eventual_set(dsm->eventual, &rc, sizeof(rc));
}

/* Call internal method to increment CSUM media error. */
void
bio_log_csum_err(struct bio_xs_context *bxc, int tgt_id)
{
	struct media_error_msg	*mem;

	D_ALLOC_PTR(mem);
	if (mem == NULL)
		return;
	mem->mem_bs		= bxc->bxc_blobstore;
	mem->mem_err_type	= MET_CSUM;
	mem->mem_tgt_id		= tgt_id;
	spdk_thread_send_msg(owner_thread(mem->mem_bs), bio_media_error, mem);
}


/* Call internal method to get BIO device state from the device owner xstream */
int
bio_get_dev_state(struct bio_dev_state *dev_state, struct bio_xs_context *xs)
{
	struct dev_state_msg_arg	 dsm = { 0 };
	int				 rc;

	rc = ABT_eventual_create(0, &dsm.eventual);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);

	dsm.xs = xs;

	spdk_thread_send_msg(owner_thread(xs->bxc_blobstore),
			     bio_get_dev_state_internal, &dsm);
	rc = ABT_eventual_wait(dsm.eventual, NULL);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);

	*dev_state = dsm.devstate;

	rc = ABT_eventual_free(&dsm.eventual);
	if (rc != ABT_SUCCESS)
		rc = dss_abterr2der(rc);

	return rc;
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
	rc = ABT_eventual_wait(dsm.eventual, (void **)&dsm_rc);
	if (rc == 0)
		rc = *dsm_rc;
	else
		rc = dss_abterr2der(rc);

	if (ABT_eventual_free(&dsm.eventual) != ABT_SUCCESS)
		rc = dss_abterr2der(rc);

	return rc;
}

static void
get_spdk_err_log_page_completion(struct spdk_bdev_io *bdev_io, bool success,
				 void *cb_arg)
{
	struct bio_dev_health			 *dev_health = cb_arg;
	int					  sc, sct;
	uint32_t				  cdw0;

	D_ASSERT(dev_health->bdh_inflights == 1);

	/* Additional NVMe status information */
	spdk_bdev_io_get_nvme_status(bdev_io, &cdw0, &sct, &sc);
	if (sc)
		D_ERROR("NVMe status code/type: %d/%d\n", sc, sct);

	/* Free I/O request in the competion callback */
	spdk_bdev_free_io(bdev_io);
	/*Decrease inflights on error or successful callback completion chain*/
	dev_health->bdh_inflights--;
}

static void
get_spdk_identify_ctrlr_completion(struct spdk_bdev_io *bdev_io, bool success,
				   void *cb_arg)
{
	struct bio_dev_health		*dev_health = cb_arg;
	struct spdk_nvme_ctrlr_data	*cdata;
	struct spdk_bdev		*bdev;
	struct spdk_nvme_cmd		 cmd;
	uint32_t			 ep_sz;
	uint32_t			 ep_buf_sz;
	uint32_t			 numd, numdl, numdu;
	int				 rc;
	int				 sc, sct;
	uint32_t			 cdw0;

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
	cdata = dev_health->bdh_ctrlr_buf;

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
	if (cdata->elpe >= NVME_MAX_ERROR_LOG_PAGES) {
		D_ERROR("Device error log page size exceeds buffer size\n");
		dev_health->bdh_inflights--;
		goto out;
	}
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
					   dev_health);
	if (rc) {
		D_ERROR("NVMe admin passthru (error log), rc:%d\n", rc);
		dev_health->bdh_inflights--;
	}

out:
	/* Free I/O request in the competion callback */
	spdk_bdev_free_io(bdev_io);
}

static void
get_spdk_log_page_completion(struct spdk_bdev_io *bdev_io, bool success,
			     void *cb_arg)
{
	struct bio_dev_health			 *dev_health = cb_arg;
	struct bio_dev_state			 *dev_state;
	struct spdk_nvme_health_information_page *hp;
	struct spdk_bdev			 *bdev;
	struct spdk_nvme_cmd			  cmd;
	uint32_t				  cp_sz;
	uint8_t					  crit_warn;
	int					  rc;
	int					  sc, sct;
	uint32_t				  cdw0;

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
	hp = dev_health->bdh_health_buf;

	/* Store device health info in in-memory health state log. */
	dev_state = &dev_health->bdh_health_state;
	dev_state->bds_timestamp = dev_health->bdh_stat_age;
	dev_state->bds_temperature = hp->temperature;
	crit_warn = hp->critical_warning.bits.temperature;
	dev_state->bds_temp_warning = crit_warn;
	crit_warn = hp->critical_warning.bits.available_spare;
	dev_state->bds_avail_spare_warning = crit_warn;
	crit_warn = hp->critical_warning.bits.device_reliability;
	dev_state->bds_dev_reliabilty_warning = crit_warn;
	crit_warn = hp->critical_warning.bits.read_only;
	dev_state->bds_read_only_warning = crit_warn;
	crit_warn = hp->critical_warning.bits.volatile_memory_backup;
	dev_state->bds_volatile_mem_warning = crit_warn;
	memcpy(dev_state->bds_media_errors, hp->media_errors,
	       sizeof(hp->media_errors));

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
					   dev_health);
	if (rc) {
		D_ERROR("NVMe admin passthru (identify ctrlr), rc:%d\n", rc);
		dev_health->bdh_inflights--;
	}

out:
	/* Free I/O request in the competion callback */
	spdk_bdev_free_io(bdev_io);
}

static int
auto_detect_faulty(struct bio_blobstore *bbs)
{
	if (bbs->bb_state != BIO_BS_STATE_NORMAL &&
	    bbs->bb_state != BIO_BS_STATE_REPLACED &&
	    bbs->bb_state != BIO_BS_STATE_REINT)
		return 0;
	/*
	 * TODO: Check the health data stored in @bbs, and mark the bbs as
	 *	 faulty when certain faulty criteria are satisfied.
	 */
	if (DAOS_FAIL_CHECK(DAOS_NVME_FAULTY))
		return bio_bs_state_set(bbs, BIO_BS_STATE_FAULTY);

	return 0;
}

/* Collect the raw device health state through SPDK admin APIs */
static void
collect_raw_health_data(struct bio_dev_health *dev_health)
{
	struct spdk_bdev	*bdev;
	struct spdk_nvme_cmd	 cmd;
	uint32_t		 numd, numdl, numdu;
	uint32_t		 health_page_sz;
	int			 rc;

	D_ASSERT(dev_health != NULL);
	D_ASSERT(dev_health->bdh_io_channel != NULL);
	D_ASSERT(dev_health->bdh_desc != NULL);

	bdev = spdk_bdev_desc_get_bdev(dev_health->bdh_desc);
	if (bdev == NULL) {
		D_ERROR("No bdev assoicated with device health descriptor\n");
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
	health_page_sz = sizeof(struct spdk_nvme_health_information_page);
	numd = health_page_sz / sizeof(uint32_t) - 1u;
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
					   health_page_sz,
					   get_spdk_log_page_completion,
					   dev_health);
	if (rc) {
		D_ERROR("NVMe admin passthru (health log), rc:%d\n", rc);
		dev_health->bdh_inflights--;
	}
}

void
bio_bs_monitor(struct bio_xs_context *ctxt, uint64_t now)
{
	struct bio_dev_health	*dev_health;
	int			 rc;
	uint64_t		 monitor_period;

	D_ASSERT(ctxt != NULL);
	D_ASSERT(ctxt->bxc_blobstore != NULL);
	dev_health = &ctxt->bxc_blobstore->bb_dev_health;

	if (dev_health->bdh_monitor_pd > 0)
		monitor_period = dev_health->bdh_monitor_pd;
	else
		monitor_period = NVME_MONITOR_PERIOD;

	if (dev_health->bdh_stat_age + monitor_period >= now)
		return;
	dev_health->bdh_stat_age = now;

	rc = auto_detect_faulty(ctxt->bxc_blobstore);
	if (rc)
		D_ERROR("Auto faulty detect on target %d failed. %d\n",
			ctxt->bxc_tgt_id, rc);

	rc = bio_bs_state_transit(ctxt->bxc_blobstore);
	if (rc)
		D_ERROR("State transition on target %d failed. %d\n",
			ctxt->bxc_tgt_id, rc);

	collect_raw_health_data(dev_health);
}

/* Print the io stat every few seconds, for debug only */
void
bio_xs_io_stat(struct bio_xs_context *ctxt, uint64_t now)
{
	struct spdk_bdev_io_stat	 stat;
	struct spdk_bdev		*bdev;
	struct spdk_io_channel		*channel;

	/* check if IO_STAT_PERIOD environment variable is set */
	if (io_stat_period == 0)
		return;

	if (ctxt->bxc_io_stat_age + io_stat_period >= now)
		return;

	if (ctxt->bxc_desc != NULL) {
		channel = spdk_bdev_get_io_channel(ctxt->bxc_desc);
		D_ASSERT(channel != NULL);
		spdk_bdev_get_io_stat(NULL, channel, &stat);
		spdk_put_io_channel(channel);

		bdev = spdk_bdev_desc_get_bdev(ctxt->bxc_desc);

		D_ASSERT(bdev != NULL);

		D_PRINT("SPDK IO STAT: tgt[%d] dev[%s] read_bytes["DF_U64"], "
			"read_ops["DF_U64"], write_bytes["DF_U64"], "
			"write_ops["DF_U64"], read_latency_ticks["DF_U64"], "
			"write_latency_ticks["DF_U64"]\n",
			ctxt->bxc_tgt_id, spdk_bdev_get_name(bdev),
			stat.bytes_read, stat.num_read_ops, stat.bytes_written,
			stat.num_write_ops, stat.read_latency_ticks,
			stat.write_latency_ticks);
	}

	ctxt->bxc_io_stat_age = now;
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
bio_init_health_monitoring(struct bio_blobstore *bb,
			   struct spdk_bdev *bdev)
{
	struct spdk_io_channel		*channel;
	uint32_t			 hp_sz;
	uint32_t			 cp_sz;
	uint32_t			 ep_sz;
	uint32_t			 ep_buf_sz;
	int				 rc;

	D_ASSERT(bb != NULL);
	D_ASSERT(bdev != NULL);

	hp_sz = sizeof(struct spdk_nvme_health_information_page);
	bb->bb_dev_health.bdh_health_buf = spdk_dma_zmalloc(hp_sz, 0, NULL);
	if (bb->bb_dev_health.bdh_health_buf == NULL)
		return -DER_NOMEM;

	cp_sz = sizeof(struct spdk_nvme_ctrlr_data);
	bb->bb_dev_health.bdh_ctrlr_buf = spdk_dma_zmalloc(cp_sz, 0, NULL);
	if (bb->bb_dev_health.bdh_ctrlr_buf == NULL) {
		spdk_dma_free(bb->bb_dev_health.bdh_health_buf);
		return -DER_NOMEM;
	}

	ep_sz = sizeof(struct spdk_nvme_error_information_entry);
	ep_buf_sz = ep_sz * NVME_MAX_ERROR_LOG_PAGES;
	bb->bb_dev_health.bdh_error_buf = spdk_dma_zmalloc(ep_buf_sz, 0, NULL);
	if (bb->bb_dev_health.bdh_error_buf == NULL) {
		spdk_dma_free(bb->bb_dev_health.bdh_health_buf);
		spdk_dma_free(bb->bb_dev_health.bdh_ctrlr_buf);
		return -DER_NOMEM;
	}


	 /* Writable descriptor required for device health monitoring */
	rc = spdk_bdev_open(bdev, true, NULL, NULL,
			    &bb->bb_dev_health.bdh_desc);
	if (rc != 0) {
		D_ERROR("Failed to open bdev %s, %d\n",
			spdk_bdev_get_name(bdev), rc);
		return daos_errno2der(-rc);
	}

	/* Get and hold I/O channel for device health monitoring */
	channel = spdk_bdev_get_io_channel(bb->bb_dev_health.bdh_desc);
	D_ASSERT(channel != NULL);
	bb->bb_dev_health.bdh_io_channel = channel;

	bb->bb_dev_health.bdh_inflights = 0;
	bb->bb_dev_health.bdh_monitor_pd = NVME_MONITOR_PERIOD;

	return 0;
}
