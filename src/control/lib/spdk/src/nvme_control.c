/**
* (C) Copyright 2018-2020 Intel Corporation.
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
* provided in Contract No. 8F-30005.
* Any reproduction of computer software, computer software documentation, or
* portions thereof marked with this legend must also reproduce the markings.
*/

#include <spdk/stdinc.h>
#include <spdk/nvme.h>
#include <spdk/env.h>

#include "nvme_control.h"
#include "nvme_control_common.h"

static void
get_spdk_log_page_completion(void *cb_arg, const struct spdk_nvme_cpl *cpl)
{
	struct dev_health_entry *entry = cb_arg;

	if (spdk_nvme_cpl_is_error(cpl))
		fprintf(stderr, "Error with SPDK health log page\n");

	entry->inflight--;
}

static int
get_dev_health_logs(struct spdk_nvme_ctrlr *ctrlr,
		    struct dev_health_entry *entry)
{
	struct spdk_nvme_health_information_page health_page;
	int					 rc = 0;

	entry->inflight++;
	rc = spdk_nvme_ctrlr_cmd_get_log_page(ctrlr,
					      SPDK_NVME_LOG_HEALTH_INFORMATION,
					      SPDK_NVME_GLOBAL_NS_TAG,
					      &health_page,
					      sizeof(health_page),
					      0, get_spdk_log_page_completion,
					      entry);
	if (rc != 0)
		return rc;

	while (entry->inflight)
		spdk_nvme_ctrlr_process_admin_completions(ctrlr);

	entry->health_page = health_page;
	return rc;
}

struct ret_t *
nvme_discover(void)
{
	return _discover(&spdk_nvme_probe, true, &get_dev_health_logs);
}

static void
write_complete(void *arg, const struct spdk_nvme_cpl *completion)
{
	struct format_sequence	*sequence = arg;

	if (sequence->using_cmb_io) {
		spdk_nvme_ctrlr_free_cmb_io_buffer(sequence->ctrlr,
						   sequence->buf, 0x1000);
	} else {
		spdk_free(sequence->buf);
	}
	sequence->is_completed = 1;
}

struct ret_t *
nvme_wipe_first_ns(char *ctrlr_pci_addr)
{
	int			 ns_id, rc;
	struct ctrlr_entry	*ctrlr_entry;
	struct format_sequence   sequence;
	struct ret_t		*ret;

	ret = init_ret(0);

	ret->rc = get_controller(&ctrlr_entry, ctrlr_pci_addr);
	if (ret->rc != 0)
		return ret;

	sequence.ctrlr = ctrlr_entry->ctrlr;

	ns_id = spdk_nvme_ctrlr_get_first_active_ns(sequence.ctrlr);
	if (ns_id != 1) {
		/* unexpected, don't wipe */
		snprintf(ret->err, sizeof(ret->err),
			"expected first active namespace at id 1, got %d",
			ns_id);
		ret->rc = -NVMEC_ERR_NS_ID_UNEXPECTED;
		return ret;
	}

	if (spdk_nvme_ctrlr_get_next_active_ns(sequence.ctrlr, ns_id) != 0) {
		/* unexpected, don't wipe */
		snprintf(ret->err, sizeof(ret->err),
			"expected a single active namespace, got multiple");
		ret->rc = -NVMEC_ERR_MULTIPLE_ACTIVE_NS;
		return ret;
	}

	sequence.ns = spdk_nvme_ctrlr_get_ns(sequence.ctrlr, ns_id);
	if (!sequence.ns) {
		snprintf(ret->err, sizeof(ret->err), "null ns");
		ret->rc = -NVMEC_ERR_NULL_NS;
		return ret;
	}

	sequence.qpair = spdk_nvme_ctrlr_alloc_io_qpair(sequence.ctrlr, NULL,
							0);
	if (sequence.qpair == NULL) {
		snprintf(ret->err, sizeof(ret->err),
			"spdk_nvme_ctrlr_alloc_io_qpair() failed");
		ret->rc = -NVMEC_ERR_ALLOC_IO_QPAIR;
		return ret;
	}

	sequence.using_cmb_io = 1;
	sequence.buf = spdk_nvme_ctrlr_alloc_cmb_io_buffer(sequence.ctrlr,
							   0x1000);
	if (sequence.buf == NULL) {
		sequence.using_cmb_io = 0;
		sequence.buf = spdk_zmalloc(0x1000, 0x1000, NULL,
					    SPDK_ENV_SOCKET_ID_ANY,
					    SPDK_MALLOC_DMA);
	}
	if (sequence.buf == NULL) {
		snprintf(ret->err, sizeof(ret->err),
			"write buffer allocation failed\n");
		ret->rc = -NVMEC_ERR_ALLOC_SEQUENCE_BUF;
		return ret;
	}
	if (sequence.using_cmb_io) {
		printf("INFO: using controller memory buffer for IO\n");
	} else {
		printf("INFO: using host memory buffer for IO\n");
	}
	sequence.is_completed = 0;

	rc = spdk_nvme_ns_cmd_write(sequence.ns, sequence.qpair, sequence.buf,
		0, /* LBA start */
		1, /* number of LBAs */
		write_complete, &sequence, 0);
	if (rc != 0) {
		snprintf(ret->err, sizeof(ret->err),
			"starting write i/o failed (rc: %d)", rc);
		ret->rc = -NVMEC_ERR_NS_WRITE_FAIL;
		return ret;
	}

	while (!sequence.is_completed) {
		spdk_nvme_qpair_process_completions(sequence.qpair, 0);
	}
	spdk_nvme_ctrlr_free_io_qpair(sequence.qpair);

	return ret;
}

struct ret_t *
nvme_format(char *ctrlr_pci_addr)
{
	int					 ns_id;
	const struct spdk_nvme_ctrlr_data	*cdata;
	struct spdk_nvme_ns			*ns;
	struct spdk_nvme_format			 format = {};
	struct ctrlr_entry			*ctrlr_entry;
	struct ret_t				*ret;

	ret = init_ret(0);

	ret->rc = get_controller(&ctrlr_entry, ctrlr_pci_addr);
	if (ret->rc != 0)
		return ret;

	cdata = spdk_nvme_ctrlr_get_data(ctrlr_entry->ctrlr);
	if (!cdata->oacs.format) {
		snprintf(ret->err, sizeof(ret->err),
			"controller does not support format nvm command");
		ret->rc = -NVMEC_ERR_NOT_SUPPORTED;
		return ret;
	}

	if (cdata->fna.format_all_ns) {
		ns_id = SPDK_NVME_GLOBAL_NS_TAG;
		ns = spdk_nvme_ctrlr_get_ns(ctrlr_entry->ctrlr, 1);
	} else {
		ns_id = 1; /* just format first ns */
		ns = spdk_nvme_ctrlr_get_ns(ctrlr_entry->ctrlr, ns_id);
	}

	if (ns == NULL) {
		snprintf(ret->err, sizeof(ret->err),
			"namespace with id %d not found", ns_id);
		ret->rc = -NVMEC_ERR_NS_NOT_FOUND;
		return ret;
	}

	format.lbaf	= 0; /* LBA format defaulted to 0 */
	format.ms	= 0; /* metadata xfer as part of separate buffer */
	format.pi	= 0; /* protection information is not enabled */
	format.pil	= 0; /* protection information location N/A */
	format.ses	= 0; /* secure erase operation set user data erase */

	ret->rc = spdk_nvme_ctrlr_format(ctrlr_entry->ctrlr, ns_id, &format);
	if (ret->rc != 0) {
		snprintf(ret->err, sizeof(ret->err), "format failed");
		return ret;
	}

	/* print address of device updated for verification purposes */
	printf("Formatted NVMe Controller at %04x:%02x:%02x.%x\n",
	       ctrlr_entry->pci_addr.domain, ctrlr_entry->pci_addr.bus,
	       ctrlr_entry->pci_addr.dev, ctrlr_entry->pci_addr.func);

	return ret;
}

struct ret_t *
nvme_fwupdate(char *ctrlr_pci_addr, char *path, unsigned int slot)
{
	int					rc = 1;
	int					fd = -1;
	unsigned int				size;
	struct stat				fw_stat;
	void					*fw_image;
	enum spdk_nvme_fw_commit_action		commit_action;
	struct spdk_nvme_status			status;
	struct ctrlr_entry			*ctrlr_entry;
	struct ret_t				*ret;

	ret = init_ret(0);

	ret->rc = get_controller(&ctrlr_entry, ctrlr_pci_addr);
	if (ret->rc != 0)
		return ret;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		sprintf(ret->err, "Open file failed");
		ret->rc = 1;
		return ret;
	}
	rc = fstat(fd, &fw_stat);
	if (rc < 0) {
		close(fd);
		sprintf(ret->err, "Fstat failed");
		ret->rc = 1;
		return ret;
	}

	if (fw_stat.st_size % 4) {
		close(fd);
		sprintf(ret->err, "Firmware image size is not multiple of 4");
		ret->rc = 1;
		return ret;
	}

	size = fw_stat.st_size;

	fw_image = spdk_dma_zmalloc(size, 4096, NULL);
	if (fw_image == NULL) {
		close(fd);
		sprintf(ret->err, "Allocation error");
		ret->rc = 1;
		return ret;
	}

	if (read(fd, fw_image, size) != ((ssize_t)(size))) {
		close(fd);
		spdk_dma_free(fw_image);
		sprintf(ret->err, "Read firmware image failed");
		ret->rc = 1;
		return ret;
	}
	close(fd);

	commit_action = SPDK_NVME_FW_COMMIT_REPLACE_AND_ENABLE_IMG;
	rc = spdk_nvme_ctrlr_update_firmware(ctrlr_entry->ctrlr, fw_image, size, slot, commit_action, &status);
	if (rc == -ENXIO && status.sct == SPDK_NVME_SCT_COMMAND_SPECIFIC &&
		status.sc == SPDK_NVME_SC_FIRMWARE_REQ_CONVENTIONAL_RESET) {
		sprintf(ret->err, "conventional reset is needed to enable firmware !");
	} else if (rc) {
		sprintf(ret->err, "spdk_nvme_ctrlr_update_firmware failed");
	} else {
		sprintf(ret->err, "spdk_nvme_ctrlr_update_firmware success");
	}
	spdk_dma_free(fw_image);

	ret->rc = rc;
	if (ret->rc != 0)
		return ret;

	/* collect() will allocate and return a new ret structure */
	clean_ret(ret);
	free(ret);
	return collect();
}

void
nvme_cleanup(void)
{
	cleanup(true);
}
