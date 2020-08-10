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

struct wipe_sequence {
	struct ns_entry	*ns_entry;
	char		*buf;
	int		 is_completed;
};

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
read_complete(void *arg, const struct spdk_nvme_cpl *completion)
{
	struct wipe_sequence *sequence = arg;

	if (spdk_nvme_cpl_is_success(completion)) {
		sequence->is_completed = 1;
		return;
	}

	spdk_nvme_qpair_print_completion(sequence->ns_entry->qpair,
					 (struct spdk_nvme_cpl *)completion);
	fprintf(stderr, "I/O error status: %s\n",
		spdk_nvme_cpl_get_status_string(&completion->status));
	fprintf(stderr, "Read I/O failed, aborting run\n");
	sequence->is_completed = 2;
}

static void
write_complete(void *arg, const struct spdk_nvme_cpl *completion)
{
	struct wipe_sequence	*sequence = arg;
	struct ns_entry		*ns_entry = sequence->ns_entry;
	int			 rc;

	if (spdk_nvme_cpl_is_success(completion)) {
		rc = spdk_nvme_ns_cmd_read(ns_entry->ns, ns_entry->qpair,
					   sequence->buf, 0, 1, read_complete,
					   (void *)sequence, 0);
		if (rc != 0) {
			fprintf(stderr, "starting read I/O failed (%d)\n", rc);
			sequence->is_completed = 2;
		}
		return;
	}

	spdk_nvme_qpair_print_completion(sequence->ns_entry->qpair,
					 (struct spdk_nvme_cpl *)completion);
	fprintf(stderr, "I/O error status: %s\n",
		spdk_nvme_cpl_get_status_string(&completion->status));
	fprintf(stderr, "Read I/O failed, aborting run\n");
	sequence->is_completed = 2;
}

static struct ret_t *
wipe(void)
{
	struct ns_entry		*nentry;
	struct ret_t		*ret;
	struct wipe_sequence	 sequence;
	int			 rc;

	ret = init_ret(0);

	nentry = g_namespaces;
	while (nentry != NULL) {
		nentry->qpair = spdk_nvme_ctrlr_alloc_io_qpair(nentry->ctrlr,
							       NULL, 0);
		if (nentry->qpair == NULL) {
			snprintf(ret->info, sizeof(ret->info),
				 "spdk_nvme_ctrlr_alloc_io_qpair()\n");
			ret->rc = -1;
			return ret;
		}

		sequence.buf = spdk_zmalloc(0x1000, 0x1000, NULL,
					    SPDK_ENV_SOCKET_ID_ANY,
					    SPDK_MALLOC_DMA);
		if (sequence.buf == NULL) {
			snprintf(ret->info, sizeof(ret->info),
				 "spdk_zmalloc()\n");
			ret->rc = -1;
			spdk_nvme_ctrlr_free_io_qpair(nentry->qpair);
			return ret;
		}
		sequence.is_completed = 0;
		sequence.ns_entry = nentry;

		rc = spdk_nvme_ns_cmd_write(nentry->ns, nentry->qpair,
					    sequence.buf, 0, /* LBA start */
					    1, /* number of LBAs */
					    write_complete, &sequence, 0);
		if (rc != 0) {
			snprintf(ret->info, sizeof(ret->info),
				 "spdk_nvme_ns_cmd_write() (%d)\n", rc);
			ret->rc = -1;
			spdk_free(sequence.buf);
			spdk_nvme_ctrlr_free_io_qpair(nentry->qpair);
			return ret;
		}

		while (!sequence.is_completed) {
			rc = spdk_nvme_qpair_process_completions(nentry->qpair,
								 0);
			if (rc < 0) {
				fprintf(stderr,
					"process completions returns %d\n", rc);
				break;
			}
		}

		if (sequence.is_completed != 1) {
			snprintf(ret->info, sizeof(ret->info),
				 "spdk_nvme_ns_cmd_write() callback\n");
			ret->rc = -1;
			spdk_free(sequence.buf);
			spdk_nvme_ctrlr_free_io_qpair(nentry->qpair);
			return ret;
		}

		spdk_free(sequence.buf);
		spdk_nvme_ctrlr_free_io_qpair(nentry->qpair);
		nentry = nentry->next;
	}

	return ret;
}

static void
wipe_cleanup(void)
{
	struct ns_entry		*ns_entry = g_namespaces;
	struct ctrlr_entry	*ctrlr_entry = g_controllers;

	while (ns_entry) {
		struct ns_entry *next = ns_entry->next;

		free(ns_entry);
		ns_entry = next;
	}

	while (ctrlr_entry) {
		struct ctrlr_entry *next = ctrlr_entry->next;

		spdk_nvme_detach(ctrlr_entry->ctrlr);
		free(ctrlr_entry);
		ctrlr_entry = next;
	}
}

struct ret_t *
nvme_wipe_namespaces(char *ctrlr_pci_addr)
{
	struct ret_t		*ret;
	struct spdk_env_opts	 opts;
	int			 rc;

	ret = init_ret(0);

	/*
	 * SPDK relies on an abstraction around the local environment
	 * named env that handles memory allocation and PCI device operations.
	 * This library must be initialized first.
	 */
	spdk_env_opts_init(&opts);
	opts.name = "wipe";
	opts.shm_id = 0;
	rc = spdk_env_init(&opts);
	if (rc < 0) {
		snprintf(ret->info, sizeof(ret->info), "spdk_env_init() (%d)\n",
			 rc);
		ret->rc = -1;
		return ret;
	}

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
		wipe_cleanup();
		ret->rc = -1;
		return ret;
	}

	if (g_controllers == NULL) {
		snprintf(ret->info, sizeof(ret->info),
			 "no controllers found\n");
		wipe_cleanup();
		ret->rc = -1;
		return ret;
	}

	ret = wipe();
	wipe_cleanup();
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

	ret = init_ret(0);

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

	if (read(fd, fw_image, size) != ((ssize_t)(size))) {
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
