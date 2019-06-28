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

uint64_t io_stat_period;

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
	struct bio_health_monitoring			*dev_health = cb_arg;
	struct spdk_nvme_error_information_entry	*error_entries;
	struct spdk_nvme_error_information_entry	*error_entry;
	struct spdk_bdev				*bdev;
	uint32_t					 i;
	int						 sc, sct;

	/* Additional NVMe status information */
	spdk_bdev_io_get_nvme_status(bdev_io, &sct, &sc);
	if (sc)
		D_ERROR("NVMe status code/type: %d/%d\n", sc, sct);

	/* Free I/O request in the competion callback */
	spdk_bdev_free_io(bdev_io);

	bdev = spdk_bdev_desc_get_bdev(dev_health->bhm_desc);

	D_PRINT("==========================================================\n");
	D_PRINT("SPDK Device Error Logs [%s]:\n",
		bdev != NULL ? spdk_bdev_get_name(bdev) : "");
	D_PRINT("==========================================================\n");

	error_entries = dev_health->bhm_error_buf;

	for (i = 0; i < dev_health->bhm_elpe; i++) {
		error_entry = &error_entries[i];
		if (error_entry->error_count == 0) {
			D_PRINT("No errors found!\n");
			return;
		}
		if (i != 0)
			D_PRINT("-------------\n");

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
}

static void
get_spdk_identify_ctrlr_completion(struct spdk_bdev_io *bdev_io, bool success,
				   void *cb_arg)
{
	struct bio_health_monitoring			*dev_health = cb_arg;
	struct spdk_nvme_ctrlr_data			*cdata;
	struct spdk_io_channel				*channel;
	struct spdk_bdev				*bdev;
	struct spdk_bdev_desc				*health_desc;
	struct spdk_nvme_cmd				 cmd;
	uint32_t					 error_page_sz;
	uint32_t					 error_page_buf_sz;
	uint32_t					 numd, numdl, numdu;
	int						 rc;
	int						 sc, sct;

	/* Additional NVMe status information */
	spdk_bdev_io_get_nvme_status(bdev_io, &sct, &sc);
	if (sc)
		D_ERROR("NVMe status code/type: %d/%d\n", sc, sct);

	/* Free I/O request in the competion callback */
	spdk_bdev_free_io(bdev_io);

	health_desc = dev_health->bhm_desc;
	channel = spdk_bdev_get_io_channel(health_desc);
	if (!channel) {
		D_ERROR("Failed to get bdev I/O channel for health desc\n");
		return;
	}

	bdev = spdk_bdev_desc_get_bdev(health_desc);
	cdata = dev_health->bhm_ctrlr_buf;
	/* set the number of error log page entries supported by device */
	dev_health->bhm_elpe = cdata->elpe;


	/*TODO Add additional relevant controller data */
	D_PRINT("==========================================================\n");
	D_PRINT("SPDK Device Controller Data [%s]:\n",
		bdev != NULL ? spdk_bdev_get_name(bdev) : "");
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

	/* Prep NVMe command to get device error log pages */
	error_page_sz = sizeof(struct spdk_nvme_error_information_entry);
	numd = error_page_sz / sizeof(uint32_t) - 1u;
	numdl = numd & 0xFFFFu;
	numdu = (numd >> 16) & 0xFFFFu;
	memset(&cmd, 0, sizeof(cmd));
	cmd.opc = SPDK_NVME_OPC_GET_LOG_PAGE;
	cmd.nsid = SPDK_NVME_GLOBAL_NS_TAG;
	cmd.cdw10 = numdl << 16;
	cmd.cdw10 |= SPDK_NVME_LOG_ERROR;
	cmd.cdw11 = numdu;
	error_page_buf_sz = error_page_sz * (dev_health->bhm_elpe + 1);

	/*
	 * Submit an NVMe Admin command to get device error log page
	 * to the bdev.
	 */
	rc = spdk_bdev_nvme_admin_passthru(health_desc, channel, &cmd,
					   dev_health->bhm_error_buf,
					   error_page_buf_sz,
					   get_spdk_err_log_page_completion,
					   dev_health);
	if (rc)
		D_ERROR("NVMe admin passthru (error log), rc:%d\n", rc);
}

static void
get_spdk_log_page_completion(struct spdk_bdev_io *bdev_io, bool success,
			     void *cb_arg)
{
	struct bio_health_monitoring			*dev_health = cb_arg;
	struct spdk_nvme_health_information_page	*health_page;
	struct spdk_io_channel				*channel;
	struct spdk_bdev				*bdev;
	struct spdk_bdev_desc				*health_desc;
	struct spdk_nvme_cmd				 cmd;
	uint32_t					 ctrlr_page_sz;
	int						 rc;
	int						 sc, sct;

	/* Additional NVMe status information */
	spdk_bdev_io_get_nvme_status(bdev_io, &sct, &sc);
	if (sc)
		D_ERROR("NVMe status code/type: %d/%d\n", sc, sct);

	/* Free I/O request in the competion callback */
	spdk_bdev_free_io(bdev_io);

	health_desc = dev_health->bhm_desc;
	channel = spdk_bdev_get_io_channel(health_desc);
	if (!channel) {
		D_ERROR("Failed to get bdev I/O channel for health desc\n");
		return;
	}

	bdev = spdk_bdev_desc_get_bdev(health_desc);
	health_page = dev_health->bhm_health_buf;

	/*
	 * TODO Store SMART health info in in-memory per-device health state log
	 */
	D_PRINT("==========================================================\n");
	D_PRINT("SPDK Device Health Information [%s]:\n",
		bdev != NULL ? spdk_bdev_get_name(bdev) : "");
	D_PRINT("==========================================================\n");
	D_PRINT("Critical Warnings:\n");
	D_PRINT("  Available Spare Space:     %s\n",
		health_page->critical_warning.bits.available_spare ?
		"WARNING" : "OK");
	D_PRINT("  Temperature:               %s\n",
		health_page->critical_warning.bits.temperature ?
		"WARNING" : "OK");
	D_PRINT("  Device Reliability:        %s\n",
		health_page->critical_warning.bits.device_reliability ?
		"WARNING" : "OK");
	D_PRINT("  Read Only:                 %s\n",
		health_page->critical_warning.bits.read_only ? "Yes" : "No");
	D_PRINT("  Volatile Memory Backup:    %s\n",
		health_page->critical_warning.bits.volatile_memory_backup ?
		"WARNING" : "OK");
	D_PRINT("  Current Temperature:       %u Kelvin (%d Celsius)\n",
		health_page->temperature, (int)health_page->temperature - 273);
	D_PRINT("Available Spare:             %u%%\n",
		health_page->available_spare);
	D_PRINT("Available Spare Threshold:   %u%%\n",
		health_page->available_spare_threshold);
	D_PRINT("Life Percentage Used:        %u%%\n",
		health_page->percentage_used);
	D_PRINT("Data Units Read:             ");
	dprint_uint128_dec(health_page->data_units_read);
	D_PRINT("\n");
	D_PRINT("Data Units Written:          ");
	dprint_uint128_dec(health_page->data_units_written);
	D_PRINT("\n");
	D_PRINT("Host Read Commands:          ");
	dprint_uint128_dec(health_page->host_read_commands);
	D_PRINT("\n");
	D_PRINT("Host Write Commands:         ");
	dprint_uint128_dec(health_page->host_write_commands);
	D_PRINT("\n");
	D_PRINT("Controller Busy Time:        ");
	dprint_uint128_dec(health_page->controller_busy_time);
	D_PRINT(" minutes\n");
	D_PRINT("Power Cycles:                ");
	dprint_uint128_dec(health_page->power_cycles);
	D_PRINT("\n");
	D_PRINT("Power On Hours:              ");
	dprint_uint128_dec(health_page->power_on_hours);
	D_PRINT(" hours\n");
	D_PRINT("Unsafe Shutdowns:	     ");
	dprint_uint128_dec(health_page->unsafe_shutdowns);
	D_PRINT("\n");
	D_PRINT("Unrecoverable Media Errors:  ");
	dprint_uint128_dec(health_page->media_errors);
	D_PRINT("\n");
	D_PRINT("Lifetime Error Log Entries:  ");
	dprint_uint128_dec(health_page->num_error_info_log_entries);
	D_PRINT("\n");
	D_PRINT("Warning Temperature Time:    %u minutes\n",
		health_page->warning_temp_time);
	D_PRINT("Critical Temperature Time:   %u minutes\n",
		health_page->critical_temp_time);

	/* Prep NVMe command to get controller data */
	ctrlr_page_sz = sizeof(struct spdk_nvme_ctrlr_data);
	memset(&cmd, 0, sizeof(cmd));
	cmd.opc = SPDK_NVME_OPC_IDENTIFY;
	cmd.cdw10 = SPDK_NVME_IDENTIFY_CTRLR;

	/*
	 * Submit an NVMe Admin command to get controller data
	 * to the bdev.
	 */
	rc = spdk_bdev_nvme_admin_passthru(health_desc, channel, &cmd,
					   dev_health->bhm_ctrlr_buf,
					   ctrlr_page_sz,
					   get_spdk_identify_ctrlr_completion,
					   dev_health);
	if (rc)
		D_ERROR("NVMe admin passthru (identify ctrlr), rc:%d\n", rc);
}

/* Get the SPDK device health state log and print all useful stats */
void
query_spdk_health_stats(struct bio_xs_context *ctxt, uint64_t now)
{
	struct bio_health_monitoring	*dev_health;
	struct spdk_bdev		*bdev;
	struct spdk_io_channel		*channel;
	struct spdk_bdev_desc		*health_desc;
	struct spdk_nvme_cmd		 cmd;
	uint64_t			 health_stat_age;
	int				 rc;
	uint32_t			 numd, numdl, numdu;
	uint32_t			 health_page_sz;

	D_ASSERT(ctxt != NULL);
	D_ASSERT(ctxt->bxc_blobstore != NULL);

	dev_health = ctxt->bxc_blobstore->bb_dev_health;
	health_stat_age = dev_health->bhm_stat_age;
	/*
	 * TODO Decide on an appropriate period to query device health
	 * stats. Currently set at 1 min.
	 */
	if (health_stat_age + DAOS_SPDK_STATS_PERIOD >= now)
		return;
	dev_health->bhm_stat_age = now;


	if (!dev_health->bhm_desc)
		return;
	health_desc = dev_health->bhm_desc;

	bdev = spdk_bdev_desc_get_bdev(health_desc);
	if (bdev == NULL) {
		D_ERROR("Failed to get bdev from health desc:%p\n",
			health_desc);
		return;
	}

	if (!spdk_bdev_io_type_supported(bdev, SPDK_BDEV_IO_TYPE_NVME_ADMIN)) {
		D_ERROR("Bdev NVMe admin passthru not supported!\n");
		return;
	}

	channel = spdk_bdev_get_io_channel(health_desc);
	if (channel == NULL) {
		D_ERROR("Failed to get bdev I/O channel from health desc:%p\n",
			health_desc);
		return;
	}

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
	rc = spdk_bdev_nvme_admin_passthru(health_desc, channel, &cmd,
					   dev_health->bhm_health_buf,
					   health_page_sz,
					   get_spdk_log_page_completion,
					   dev_health);
	if (rc)
		D_ERROR("NVMe admin passthru (health log), rc:%d\n", rc);

	/* Release I/O channel reference */
	spdk_put_io_channel(channel);
}

/* Print the io stat every few seconds, for debug only */
void
print_io_stat(struct bio_xs_context *ctxt, uint64_t now)
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

		D_PRINT("SPDK IO STAT: xs_id[%d] dev[%s] read_bytes["DF_U64"], "
			"read_ops["DF_U64"], write_bytes["DF_U64"], "
			"write_ops["DF_U64"], read_latency_ticks["DF_U64"], "
			"write_latency_ticks["DF_U64"]\n",
			ctxt->bxc_xs_id, spdk_bdev_get_name(bdev),
			stat.bytes_read, stat.num_read_ops, stat.bytes_written,
			stat.num_write_ops, stat.read_latency_ticks,
			stat.write_latency_ticks);
	}

	ctxt->bxc_io_stat_age = now;
}
