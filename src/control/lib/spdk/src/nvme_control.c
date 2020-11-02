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

enum lba0_write_result {
	LBA0_WRITE_PENDING	= 0x0,
	LBA0_WRITE_SUCCESS	= 0x1,
	LBA0_WRITE_FAIL		= 0x2,
};

struct lba0_data {
	struct ns_entry		*ns_entry;
	char			*buf;
	enum lba0_write_result	 write_result;
};

static void
get_spdk_log_page_completion(void *cb_arg, const struct spdk_nvme_cpl *cpl)
{
	struct health_entry *entry = cb_arg;

	if (spdk_nvme_cpl_is_error(cpl))
		fprintf(stderr, "Error with SPDK health log page\n");

	entry->inflight--;
}

static int
get_health_logs(struct spdk_nvme_ctrlr *ctrlr, struct health_entry *health)
{
	struct spdk_nvme_health_information_page hp;
	int					 rc = 0;

	health->inflight++;
	rc = spdk_nvme_ctrlr_cmd_get_log_page(ctrlr,
					      SPDK_NVME_LOG_HEALTH_INFORMATION,
					      SPDK_NVME_GLOBAL_NS_TAG,
					      &hp,
					      sizeof(hp),
					      0, get_spdk_log_page_completion,
					      health);
	if (rc != 0)
		return rc;

	while (health->inflight)
		spdk_nvme_ctrlr_process_admin_completions(ctrlr);

	health->page = hp;
	return rc;
}

struct ret_t *
nvme_discover(void)
{
	return _discover(&spdk_nvme_probe, true, &get_health_logs);
}

static void
read_complete(void *arg, const struct spdk_nvme_cpl *completion)
{
	struct lba0_data *data = arg;

	spdk_free(data->buf);

	if (spdk_nvme_cpl_is_success(completion)) {
		data->write_result = LBA0_WRITE_SUCCESS;
		return;
	}

	spdk_nvme_qpair_print_completion(data->ns_entry->qpair,
					 (struct spdk_nvme_cpl *)completion);
	fprintf(stderr, "I/O error status: %s\n",
		spdk_nvme_cpl_get_status_string(&completion->status));
	fprintf(stderr, "Read I/O failed, aborting run\n");
	data->write_result = LBA0_WRITE_FAIL;
}

static void
write_complete(void *arg, const struct spdk_nvme_cpl *completion)
{
	struct lba0_data	*data = arg;
	struct ns_entry		*ns_entry = data->ns_entry;
	int			 rc;

	if (spdk_nvme_cpl_is_success(completion)) {
		rc = spdk_nvme_ns_cmd_read(ns_entry->ns, ns_entry->qpair,
					   data->buf, 0, 1, read_complete,
					   (void *)data, 0);
		if (rc != 0) {
			fprintf(stderr, "starting read I/O failed (%d)\n", rc);
			data->write_result = LBA0_WRITE_FAIL;
		}
		return;
	}

	spdk_nvme_qpair_print_completion(data->ns_entry->qpair,
					 (struct spdk_nvme_cpl *)completion);
	fprintf(stderr, "I/O error status: %s\n",
		spdk_nvme_cpl_get_status_string(&completion->status));
	fprintf(stderr, "Read I/O failed, aborting run\n");
	data->write_result = LBA0_WRITE_FAIL;

	spdk_free(data->buf);
}

static struct wipe_res_t *
wipe_ctrlr(struct ctrlr_entry *centry, struct ns_entry *nentry)
{
	struct lba0_data	 data;
	struct wipe_res_t	*res = NULL, *tmp = NULL;
	int			 rc;

	while (nentry != NULL) {
		res = init_wipe_res();
		res->next = tmp;
		tmp = res;

		res->ns_id = spdk_nvme_ns_get_id(nentry->ns);

		rc = spdk_pci_addr_fmt(res->ctrlr_pci_addr,
				       sizeof(res->ctrlr_pci_addr),
				       &centry->pci_addr);
		if (rc != 0) {
			res->rc = -NVMEC_ERR_PCI_ADDR_FMT;
			return res;
		}

		nentry->qpair = spdk_nvme_ctrlr_alloc_io_qpair(centry->ctrlr,
							       NULL, 0);
		if (nentry->qpair == NULL) {
			snprintf(res->info, sizeof(res->info),
				 "spdk_nvme_ctrlr_alloc_io_qpair()\n");
			res->rc = -1;
			return res;
		}

		data.buf = spdk_zmalloc(0x1000, 0x1000, NULL,
					SPDK_ENV_SOCKET_ID_ANY,
					SPDK_MALLOC_DMA);
		if (data.buf == NULL) {
			snprintf(res->info, sizeof(res->info),
				 "spdk_zmalloc()\n");
			res->rc = -1;
			spdk_nvme_ctrlr_free_io_qpair(nentry->qpair);
			return res;
		}
		data.write_result = LBA0_WRITE_PENDING;
		data.ns_entry = nentry;

		rc = spdk_nvme_ns_cmd_write(nentry->ns, nentry->qpair,
					    data.buf, 0, /* LBA start */
					    1, /* number of LBAs */
					    write_complete, &data, 0);
		if (rc != 0) {
			snprintf(res->info, sizeof(res->info),
				 "spdk_nvme_ns_cmd_write() (%d)\n", rc);
			res->rc = -1;
			spdk_free(data.buf);
			spdk_nvme_ctrlr_free_io_qpair(nentry->qpair);
			return res;
		}

		while (data.write_result == LBA0_WRITE_PENDING) {
			rc = spdk_nvme_qpair_process_completions(nentry->qpair,
								 0);
			if (rc < 0) {
				fprintf(stderr,
					"process completions returns %d\n", rc);
				break;
			}
		}

		if (data.write_result != LBA0_WRITE_SUCCESS) {
			snprintf(res->info, sizeof(res->info),
				 "spdk_nvme_ns_cmd_write() callback\n");
			res->rc = -1;
			spdk_nvme_ctrlr_free_io_qpair(nentry->qpair);
			return res;
		}

		spdk_nvme_ctrlr_free_io_qpair(nentry->qpair);
		nentry = nentry->next;
	}

	return res;
}

static struct wipe_res_t *
wipe_ctrlrs(void)
{
	struct ctrlr_entry	*centry = g_controllers;
	struct wipe_res_t	*start = NULL, *end = NULL;

	while (centry != NULL) {
		struct wipe_res_t *results = wipe_ctrlr(centry, centry->nss);
		struct wipe_res_t *tmp = results;

		if (results == NULL) {
			continue;
		}

		if (start == NULL) {
			start = results;
		} else if (end != NULL) {
			end->next = results;
		}

		/* update ptr to last in list */
		while (tmp != NULL) {
			end = tmp;
			tmp = tmp->next;
		}

		centry = centry->next;
	}

	return start;
}

struct ret_t *
nvme_wipe_namespaces(void)
{
	struct ret_t	*ret = init_ret();
	int		 rc;

	/*
	 * Start the SPDK NVMe enumeration process.  probe_cb will be called
	 *  for each NVMe controller found, giving our application a choice on
	 *  whether to attach to each controller.  attach_cb will then be
	 *  called for each controller after the SPDK NVMe driver has completed
	 *  initializing the controller we chose to attach.
	 */
	rc = spdk_nvme_probe(NULL, NULL, probe_cb, attach_cb, NULL);
	if (rc < 0) {
		snprintf(ret->info, sizeof(ret->info),
			 "spdk_nvme_probe() (%d)\n", rc);
		cleanup(true);
		ret->rc = -1;
		return ret;
	}

	if (g_controllers == NULL) {
		snprintf(ret->info, sizeof(ret->info),
			 "no controllers found\n");
		cleanup(true);
		ret->rc = -1;
		return ret;
	}

	ret->wipe_results = wipe_ctrlrs();
	if (ret->wipe_results == NULL) {
		snprintf(ret->info, sizeof(ret->info),
			 "no namespaces on controller\n");
		cleanup(true);
		ret->rc = -1;
		return ret;
	}

	cleanup(true);
	return ret;
}

struct ret_t *
nvme_format(char *ctrlr_pci_addr)
{
	int					 nsid;
	const struct spdk_nvme_ctrlr_data	*cdata;
	struct spdk_nvme_ns			*ns;
	struct spdk_nvme_format			 format = {};
	struct ctrlr_entry			*ctrlr_entry;
	struct ret_t				*ret;

	ret = init_ret();

	ret->rc = get_controller(&ctrlr_entry, ctrlr_pci_addr);
	if (ret->rc != 0)
		return ret;

	cdata = spdk_nvme_ctrlr_get_data(ctrlr_entry->ctrlr);
	if (!cdata->oacs.format) {
		snprintf(ret->info, sizeof(ret->info),
			 "controller does not support format nvm command");
		ret->rc = -NVMEC_ERR_NOT_SUPPORTED;
		return ret;
	}

	if (cdata->fna.format_all_ns) {
		nsid = SPDK_NVME_GLOBAL_NS_TAG;
		ns = spdk_nvme_ctrlr_get_ns(ctrlr_entry->ctrlr, 1);
	} else {
		nsid = 1; /* just format first ns */
		ns = spdk_nvme_ctrlr_get_ns(ctrlr_entry->ctrlr, nsid);
	}

	if (ns == NULL) {
		snprintf(ret->info, sizeof(ret->info),
			 "namespace with id %d not found", nsid);
		ret->rc = -NVMEC_ERR_NS_NOT_FOUND;
		return ret;
	}

	format.lbaf	= 0; /* LBA format defaulted to 0 */
	format.ms	= 0; /* metadata xfer as part of separate buffer */
	format.pi	= 0; /* protection information is not enabled */
	format.pil	= 0; /* protection information location N/A */
	format.ses	= 0; /* secure erase operation set user data erase */

	ret->rc = spdk_nvme_ctrlr_format(ctrlr_entry->ctrlr, nsid, &format);
	if (ret->rc != 0) {
		snprintf(ret->info, sizeof(ret->info), "format failed");
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
	void					*fw_image = NULL;
	enum spdk_nvme_fw_commit_action		commit_action;
	struct spdk_nvme_status			status;
	struct ctrlr_entry			*ctrlr_entry;
	struct ret_t				*ret;

	ret = init_ret();

	ret->rc = get_controller(&ctrlr_entry, ctrlr_pci_addr);
	if (ret->rc != 0)
		return ret;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		sprintf(ret->info, "Open file failed");
		ret->rc = 1;
		return ret;
	}
	rc = fstat(fd, &fw_stat);
	if (rc < 0) {
		close(fd);
		sprintf(ret->info, "Fstat failed");
		ret->rc = 1;
		return ret;
	}

	if (fw_stat.st_size % 4) {
		close(fd);
		sprintf(ret->info, "Firmware image size is not multiple of 4");
		ret->rc = 1;
		return ret;
	}

	size = fw_stat.st_size;

	fw_image = spdk_dma_zmalloc(size, 4096, NULL);
	if (fw_image == NULL) {
		close(fd);
		sprintf(ret->info, "Allocation error");
		ret->rc = 1;
		return ret;
	}

	if (read(fd, fw_image, size) != (ssize_t)size) {
		close(fd);
		spdk_dma_free(fw_image);
		sprintf(ret->info, "Read firmware image failed");
		ret->rc = 1;
		return ret;
	}
	close(fd);

	commit_action = SPDK_NVME_FW_COMMIT_REPLACE_AND_ENABLE_IMG;
	rc = spdk_nvme_ctrlr_update_firmware(ctrlr_entry->ctrlr, fw_image, size,
					     slot, commit_action, &status);
	if (rc == -ENXIO && status.sct == SPDK_NVME_SCT_COMMAND_SPECIFIC &&
		status.sc == SPDK_NVME_SC_FIRMWARE_REQ_CONVENTIONAL_RESET) {
		sprintf(ret->info,
			"conventional reset is needed to enable firmware !");
	} else if (rc) {
		sprintf(ret->info, "spdk_nvme_ctrlr_update_firmware failed");
	} else {
		sprintf(ret->info, "spdk_nvme_ctrlr_update_firmware success");
	}
	spdk_dma_free(fw_image);

	ret->rc = rc;
	return ret;
}
