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

/* Period to query SPDK device health stats (1 min period) */
#define DAOS_SPDK_STATS_PERIOD	(60 * (NSEC_PER_SEC / NSEC_PER_USEC))
/* Used to preallocate buffer to query error log pages from SPDK health info */
#define DAOS_MAX_ERROR_LOG_PAGES 256

/* See DAOS-3319 on this.  We should generally try to avoid reading unaligned
 * variables directly as it results in more than one instruction for each such
 * access.  The instances of these possible unaligned accesses happen with
 * default gcc on Fedora 30.
 */
#if !defined(__has_warning)  /* gcc */
#if __GNUC__ >= 9
	#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif /* warning only defined in version 9 or later */
#else /* __has_warning is defined */
#if __has_warning("-Waddress-of-packed-member") /* valid clang warning */
	#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif /* Warning is defined in clang */
#endif /* __has_warning not defined */



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

/* Call internal method to get BIO device state from the device owner xstream */
int
bio_get_dev_state(struct bio_dev_state *dev_state, struct bio_xs_context *xs)
{
	struct dev_state_msg_arg	 dsm = { 0 };
	int				 rc;

	rc = ABT_eventual_create(0, &dsm.eventual);
	if (rc != ABT_SUCCESS)
		return rc;

	dsm.xs = xs;

	spdk_thread_send_msg(owner_thread(xs->bxc_blobstore),
			     bio_get_dev_state_internal, &dsm);
	ABT_eventual_wait(dsm.eventual, NULL);

	*dev_state = dsm.devstate;

	rc = ABT_eventual_free(&dsm.eventual);
	if (rc != ABT_SUCCESS)
		D_ERROR("BIO get device state ABT future not freed\n");

	return rc;
}

static void
dprint_uint128_hex(uint64_t *v)
{
	unsigned long long lo = v[0], hi = v[1];

	if (hi)
		D_PRINT("0x%llX%016llX", hi, lo);
	else
		D_PRINT("0x%llX", lo);
}

static void
dprint_uint128_dec(uint64_t *v)
{
	unsigned long long lo = v[0], hi = v[1];

	if (hi)
		/* can't handle large (>64-bit) decimal values */
		dprint_uint128_hex(v);
	else
		D_PRINT("%llu", (unsigned long long)lo);
}
static void
dprint_ascii_string(const void *buf, size_t size)
{
	const uint8_t *str = buf;

	/* Trim trailing spaces */
	while (size > 0 && str[size - 1] == ' ')
		size--;

	while (size--) {
		if (*str >= 0x20 && *str <= 0x7E)
			D_PRINT("%c", *str);
		else
			D_PRINT(".");

		str++;
	}
}

static void
get_spdk_err_log_page_completion(struct spdk_bdev_io *bdev_io, bool success,
				 void *cb_arg)
{
	struct bio_dev_health			 *dev_health = cb_arg;
	struct bio_dev_state			 *dev_state;
	struct spdk_nvme_error_information_entry *error_entries;
	struct spdk_nvme_error_information_entry *error_entry;
	struct spdk_nvme_ctrlr_data		 *cdata;
	struct spdk_bdev			 *bdev;
	uint32_t				  i;
	int					  sc, sct;

	D_ASSERT(dev_health->bdh_inflights == 1);

	/* Additional NVMe status information */
	spdk_bdev_io_get_nvme_status(bdev_io, &sct, &sc);
	if (sc) {
		D_ERROR("NVMe status code/type: %d/%d\n", sc, sct);
		goto out;
	}

	dev_state = &dev_health->bdh_health_state;

	bdev = spdk_bdev_desc_get_bdev(dev_health->bdh_desc);

	D_ASSERT(bdev != NULL);

	cdata = dev_health->bdh_ctrlr_buf;
	error_entries = dev_health->bdh_error_buf;

	if (getenv("PRINT_HEALTH_INFO") != NULL) {
		D_PRINT("==================================================\n");
		D_PRINT("SPDK Device Error Logs [%s]:\n",
			spdk_bdev_get_name(bdev));
		D_PRINT("==================================================\n");
	}

	for (i = 0; i < cdata->elpe; i++) {
		error_entry = &error_entries[i];
		dev_state->bds_error_count = error_entry->error_count;
		if (error_entry->error_count == 0) {
			if (getenv("PRINT_HEALTH_INFO") != NULL)
				D_PRINT("No errors found!\n");
			goto out;
		}
		if (i != 0)
			D_PRINT("-------------\n");

		/* Only print device error logs to console if env is set */
		if (getenv("PRINT_HEALTH_INFO") == NULL)
			goto out;

		D_PRINT("Entry: %u\n", i);
		D_PRINT("Error count:         0x%"PRIx64"\n",
			error_entry->error_count);
		D_PRINT("Submission queue ID: 0x%x\n", error_entry->sqid);
		D_PRINT("Command ID:          0x%x\n", error_entry->cid);
		D_PRINT("Phase bit:           %x\n", error_entry->status.p);
		D_PRINT("Status code:         0x%x\n", error_entry->status.sc);
		D_PRINT("Status code type:    0x%x\n", error_entry->status.sct);
		D_PRINT("Do not retry:        %x\n", error_entry->status.dnr);
		D_PRINT("Error location:      0x%x\n",
			error_entry->error_location);
		D_PRINT("LBA:                 0x%"PRIx64"\n", error_entry->lba);
		D_PRINT("Namespace:           0x%x\n", error_entry->nsid);
		D_PRINT("Vendor log page:     0x%x\n",
			error_entry->vendor_specific);
		D_PRINT("\n");
	}

out:
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

	D_ASSERT(dev_health->bdh_inflights == 1);

	/* Additional NVMe status information */
	spdk_bdev_io_get_nvme_status(bdev_io, &sct, &sc);
	if (sc) {
		D_ERROR("NVMe status code/type: %d/%d\n", sc, sct);
		dev_health->bdh_inflights--;
		goto out;
	}

	D_ASSERT(dev_health->bdh_io_channel != NULL);
	bdev = spdk_bdev_desc_get_bdev(dev_health->bdh_desc);
	D_ASSERT(bdev != NULL);
	cdata = dev_health->bdh_ctrlr_buf;

	/* Only print device controller data to console if env is set */
	if (getenv("PRINT_HEALTH_INFO") == NULL)
		goto prep_cmd;

	/*TODO Add additional relevant controller data */
	D_PRINT("==========================================================\n");
	D_PRINT("SPDK Device Controller Data [%s]:\n",
		spdk_bdev_get_name(bdev));
	D_PRINT("==========================================================\n");
	D_PRINT("Vendor ID: %04x\n", cdata->vid);
	D_PRINT("Serial Number: ");
	dprint_ascii_string(cdata->sn, sizeof(cdata->sn));
	D_PRINT("\n");
	D_PRINT("Model Number: ");
	dprint_ascii_string(cdata->mn, sizeof(cdata->mn));
	D_PRINT("\n");
	D_PRINT("Firmware Version: ");
	dprint_ascii_string(cdata->fr, sizeof(cdata->fr));
	D_PRINT("\n");
	D_PRINT("Error log page entries supported: %d\n", cdata->elpe + 1);

prep_cmd:
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
	if (cdata->elpe >= DAOS_MAX_ERROR_LOG_PAGES) {
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

	D_ASSERT(dev_health->bdh_inflights == 1);

	/* Additional NVMe status information */
	spdk_bdev_io_get_nvme_status(bdev_io, &sct, &sc);
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
	dev_state->bds_media_errors = hp->media_errors;

	/* Only print device health info to console if env is set */
	if (getenv("PRINT_HEALTH_INFO") == NULL)
		goto prep_cmd;

	D_PRINT("==========================================================\n");
	D_PRINT("SPDK Device Health Information [%s]:\n",
		spdk_bdev_get_name(bdev));
	D_PRINT("==========================================================\n");
	D_PRINT("Critical Warnings:\n");
	D_PRINT("  Available Spare Space:     %s\n",
		hp->critical_warning.bits.available_spare ? "WARNING" : "OK");
	D_PRINT("  Temperature:               %s\n",
		hp->critical_warning.bits.temperature ? "WARNING" : "OK");
	D_PRINT("  Device Reliability:        %s\n",
		hp->critical_warning.bits.device_reliability ?
		"WARNING" : "OK");
	D_PRINT("  Read Only:                 %s\n",
		hp->critical_warning.bits.read_only ? "Yes" : "No");
	D_PRINT("  Volatile Memory Backup:    %s\n",
		hp->critical_warning.bits.volatile_memory_backup ?
		"WARNING" : "OK");
	D_PRINT("  Current Temperature:       %u Kelvin (%d Celsius)\n",
		hp->temperature, (int)hp->temperature - 273);
	D_PRINT("Available Spare:             %u%%\n", hp->available_spare);
	D_PRINT("Available Spare Threshold:   %u%%\n",
		hp->available_spare_threshold);
	D_PRINT("Life Percentage Used:        %u%%\n", hp->percentage_used);
	D_PRINT("Data Units Read:             ");
	dprint_uint128_dec(hp->data_units_read);
	D_PRINT("\n");
	D_PRINT("Data Units Written:          ");
	dprint_uint128_dec(hp->data_units_written);
	D_PRINT("\n");
	D_PRINT("Host Read Commands:          ");
	dprint_uint128_dec(hp->host_read_commands);
	D_PRINT("\n");
	D_PRINT("Host Write Commands:         ");
	dprint_uint128_dec(hp->host_write_commands);
	D_PRINT("\n");
	D_PRINT("Controller Busy Time:        ");
	dprint_uint128_dec(hp->controller_busy_time);
	D_PRINT(" minutes\n");
	D_PRINT("Power Cycles:                ");
	dprint_uint128_dec(hp->power_cycles);
	D_PRINT("\n");
	D_PRINT("Power On Hours:              ");
	dprint_uint128_dec(hp->power_on_hours);
	D_PRINT(" hours\n");
	D_PRINT("Unsafe Shutdowns:	     ");
	dprint_uint128_dec(hp->unsafe_shutdowns);
	D_PRINT("\n");
	D_PRINT("Unrecoverable Media Errors:  ");
	dprint_uint128_dec(hp->media_errors);
	D_PRINT("\n");
	D_PRINT("Lifetime Error Log Entries:  ");
	dprint_uint128_dec(hp->num_error_info_log_entries);
	D_PRINT("\n");
	D_PRINT("Warning Temperature Time:    %u minutes\n",
		hp->warning_temp_time);
	D_PRINT("Critical Temperature Time:   %u minutes\n",
		hp->critical_temp_time);

prep_cmd:
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

/* Get the SPDK device health state log and print all useful stats */
void
bio_bs_monitor(struct bio_xs_context *ctxt, uint64_t now)
{
	struct bio_dev_health	*dev_health;
	struct spdk_bdev	*bdev;
	struct spdk_nvme_cmd	 cmd;
	int			 rc;
	uint32_t		 numd, numdl, numdu;
	uint32_t		 health_page_sz;

	D_ASSERT(ctxt != NULL);
	D_ASSERT(ctxt->bxc_blobstore != NULL);
	dev_health = &ctxt->bxc_blobstore->bb_dev_health;
	D_ASSERT(dev_health->bdh_io_channel != NULL);
	D_ASSERT(dev_health->bdh_desc != NULL);

	/*
	 * TODO Decide on an appropriate period to query device health
	 * stats. Currently set at 1 min.
	 */
	if (dev_health->bdh_stat_age + DAOS_SPDK_STATS_PERIOD >= now)
		return;
	dev_health->bdh_stat_age = now;

	bdev = spdk_bdev_desc_get_bdev(dev_health->bdh_desc);
	if (bdev == NULL) {
		D_ERROR("No bdev assoicated with device health descriptor\n");
		return;
	}

	/* Return if non-NVMe device */
	if (get_bdev_type(bdev) != BDEV_CLASS_NVME)
		return;

	/*
	 * TODO:
	 * Faulty device criteria check (Check current in-memory device health
	 * state).
	 * Mechanism to notify admin of if any BIO errors exist. Given I/O
	 * error, checksum error and health monitoring information, admin can
	 * determine if device should be marked as FAULTY (SMD device state
	 * updated).
	 */


	/*
	 * Continue querying current SPDK device health stats.
	 */

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
	/* Free NVMe admin passthru DMA buffers */
	spdk_dma_free(bb->bb_dev_health.bdh_health_buf);
	spdk_dma_free(bb->bb_dev_health.bdh_ctrlr_buf);
	spdk_dma_free(bb->bb_dev_health.bdh_error_buf);

	/* Release I/O channel reference */
	spdk_put_io_channel(bb->bb_dev_health.bdh_io_channel);

	/* Close device health monitoring descriptor */
	spdk_bdev_close(bb->bb_dev_health.bdh_desc);
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
	ep_buf_sz = ep_sz * DAOS_MAX_ERROR_LOG_PAGES;
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

	return 0;
}

/*
 * MEDIA ERROR event.
 * Store BIO I/O error in in-memory device state. Called from device owner
 * xstream only.
 */
void
bio_media_error(void *msg_arg)
{
	struct media_error_msg	*mem = msg_arg;
	struct bio_dev_state	*dev_state;

	dev_state = &mem->mem_bs->bb_dev_health.bdh_health_state;

	if (mem->mem_unmap) {
		/* Update unmap error counter */
		dev_state->bds_bio_unmap_errs++;
		D_ERROR("Unmap error logged from tgt_id:%d\n", mem->mem_tgt_id);
	} else {
		/* Update read/write I/O error counters */
		if (mem->mem_update)
			dev_state->bds_bio_write_errs++;
		else
			dev_state->bds_bio_read_errs++;
		D_ERROR("%s error logged from xs_id:%d\n",
			mem->mem_update ? "Write" : "Read", mem->mem_tgt_id);
	}

	/* TODO Implement checksum error counter */
	dev_state->bds_checksum_errs = 0;

	D_FREE(mem);
}
