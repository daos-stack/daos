/**
* (C) Copyright 2018-2019 Intel Corporation.
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
_nvme_discover(prober probe, detacher detach, health_getter get_health)
{
	int			 rc;
	struct ret_t		*ret;
	struct ctrlr_entry	*ctrlr_entry;
	struct dev_health_entry	*health_entry;

	ret = init_ret();

	/*
	 * Start the SPDK NVMe enumeration process.  probe_cb will be called
	 *  for each NVMe controller found, giving our application a choice on
	 *  whether to attach to each controller.  attach_cb will then be
	 *  called for each controller after the SPDK NVMe driver has completed
	 *  initializing the controller we chose to attach.
	 */
	rc = probe(NULL, NULL, probe_cb, attach_cb, NULL);
	if (rc != 0) {
		sprintf(ret->err, "spdk_nvme_probe() failed");
		ret->rc = rc;
		_cleanup(detach);
		return ret;
	}

	if (!g_controllers || !g_controllers->ctrlr) {
		sprintf(ret->err, "no nvme controllers found");
		_cleanup(detach);
		return ret;
	}

	/*
	 * Collect NVMe SSD health stats for each probed controller.
	 * TODO: move to attach_cb?
	 */
	ctrlr_entry = g_controllers;

	while (ctrlr_entry) {
		health_entry = malloc(sizeof(struct dev_health_entry));
		if (health_entry == NULL) {
			sprintf(ret->err, "health_entry malloc failed");
			ret->rc = -ENOMEM;
			_cleanup(detach);
			return ret;
		}

		rc = get_health(ctrlr_entry->ctrlr, health_entry);
		if (rc != 0) {
			sprintf(ret->err,
				"unable to get SPDK ctrlr health logs");
			ret->rc = rc;
			free(health_entry);
			_cleanup(detach);
			return ret;
		}

		ctrlr_entry->dev_health = health_entry;
		ctrlr_entry = ctrlr_entry->next;
	}

	collect(ret);

	return ret;
}

struct ret_t *
nvme_discover(void)
{
	return _nvme_discover(&spdk_nvme_probe, &spdk_nvme_detach,
			      &get_dev_health_logs);
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

	ret = init_ret();

	ctrlr_entry = get_controller(ctrlr_pci_addr, ret);
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
	collect(ret);

	return ret;
}

struct ret_t *
nvme_format(char *ctrlr_pci_addr)
{
	int					ns_id;
	const struct spdk_nvme_ctrlr_data	*cdata;
	struct spdk_nvme_ns			*ns;
	struct spdk_nvme_format			format = {};
	struct ctrlr_entry			*ctrlr_entry;
	struct spdk_pci_device			*pci_dev;
	struct spdk_pci_addr			pci_addr;
	struct ret_t				*ret;

	ret = init_ret();

	ctrlr_entry = get_controller(ctrlr_pci_addr, ret);
	if (ret->rc != 0)
		return ret;

	cdata = spdk_nvme_ctrlr_get_data(ctrlr_entry->ctrlr);
	if (!cdata->oacs.format) {
		snprintf(ret->err, sizeof(ret->err),
			"Controller does not support Format NVM command\n");
		ret->rc = -NVMEC_ERR_NOT_SUPPORTED;
		return ret;
	}

	if (cdata->fna.format_all_ns) {
		ns_id = SPDK_NVME_GLOBAL_NS_TAG;
		ns = spdk_nvme_ctrlr_get_ns(ctrlr_entry->ctrlr, 1);
	} else {
		ns_id = 1; // just format first ns
		ns = spdk_nvme_ctrlr_get_ns(ctrlr_entry->ctrlr, ns_id);
	}

	if (ns == NULL) {
		snprintf(ret->err, sizeof(ret->err),
			"Namespace ID %d not found", ns_id);
		ret->rc = -NVMEC_ERR_NS_NOT_FOUND;
		return ret;
	}

	format.lbaf	= 0; /* LBA format defaulted to 0 */
	format.ms	= 0; /* metadata xfer as part of separate buffer */
	format.pi	= 0; /* protection information is not enabled */
	format.pil	= 0; /* protection information location N/A */
	format.ses	= 1; /* secure erase operation set user data erase */

	ret->rc = spdk_nvme_ctrlr_format(ctrlr_entry->ctrlr, ns_id, &format);
	if (ret->rc != 0) {
		snprintf(ret->err, sizeof(ret->err), "format failed");
		return ret;
	}

	pci_dev = spdk_nvme_ctrlr_get_pci_device(ctrlr_entry->ctrlr);
	if (!pci_dev) {
		snprintf(ret->err, sizeof(ret->err), "get_pci_device");
		ret->rc = -NVMEC_ERR_GET_PCI_DEV;
		return ret;
	}

	// print address of device updated for verification purposes
	pci_addr = spdk_pci_device_get_addr(pci_dev);
	printf("Formatted NVMe Controller:       %04x:%02x:%02x.%02x\n",
	       pci_addr.domain, pci_addr.bus, pci_addr.dev, pci_addr.func);

	collect(ret);

	return ret;
}

void
_cleanup(detacher detach)
{
	struct ns_entry		*nentry;
	struct ctrlr_entry	*centry;

	centry = g_controllers;

	printf("Cleanup NVMe");

	while (centry) {
		struct ctrlr_entry *cnext = centry->next;

		if (centry->dev_health)
			free(centry->dev_health);
		if (centry->ctrlr)
			detach(centry->ctrlr);
		while (centry->nss) {
			nentry = centry->nss->next;
			free(centry->nss);
			centry->nss = nentry;
		}

		free(ctrlr_entry);
		ctrlr_entry = next;
	}

	g_controllers = NULL;
}

void
nvme_cleanup(void)
{
	_cleanup(&spdk_nvme_detach);
}
