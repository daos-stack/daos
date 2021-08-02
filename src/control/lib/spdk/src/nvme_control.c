/**
* (C) Copyright 2018-2021 Intel Corporation.
*
* SPDX-License-Identifier: BSD-2-Clause-Patent
*/

#include <spdk/stdinc.h>
#include <spdk/nvme.h>
#include <spdk/env.h>
#include <spdk/nvme_intel.h>
#include <spdk/pci_ids.h>

#include "nvme_control.h"
#include "nvme_control_common.h"

enum lba0_write_result {
	LBA0_WRITE_PENDING	= 0x0,
	LBA0_WRITE_SUCCESS	= 0x1,
	LBA0_WRITE_FAIL		= 0x2,
};

/** data structure passed to NVMe cmd completion */
struct lba0_data {
	struct ns_entry		*ns_entry;
	enum lba0_write_result	 result;
};

static void
get_spdk_log_page_completion(void *cb_arg, const struct spdk_nvme_cpl *cpl)
{
	struct health_entry *entry = cb_arg;

	if (spdk_nvme_cpl_is_error(cpl))
		fprintf(stderr, "Error with SPDK log page\n");

	entry->inflight--;
}

static int
get_health_logs(struct spdk_nvme_ctrlr *ctrlr, struct health_entry *health)
{
	struct spdk_nvme_health_information_page	hp;
	struct spdk_nvme_intel_smart_information_page	isp;
	const struct spdk_nvme_ctrlr_data		*cdata;
	int						rc = 0;

	cdata = spdk_nvme_ctrlr_get_data(ctrlr);

	/** NVMe SSDs on GCP do not support this */
	if (!spdk_nvme_ctrlr_is_log_page_supported(ctrlr,
					SPDK_NVME_LOG_HEALTH_INFORMATION))
		/** not much we can do, just skip */
		return 0;

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

	/* Non-Intel SSDs do not support this */
	if (cdata->vid != SPDK_PCI_VID_INTEL)
		return 0;
	if (!spdk_nvme_ctrlr_is_log_page_supported(ctrlr,
					SPDK_NVME_INTEL_LOG_SMART))
		/** not much we can do, just skip */
		return 0;

	health->inflight++;
	rc = spdk_nvme_ctrlr_cmd_get_log_page(ctrlr,
					      SPDK_NVME_INTEL_LOG_SMART,
					      SPDK_NVME_GLOBAL_NS_TAG,
					      &isp,
					      sizeof(isp),
					      0, get_spdk_log_page_completion,
					      health);
	if (rc != 0)
		return rc;

	while (health->inflight)
		spdk_nvme_ctrlr_process_admin_completions(ctrlr);

	health->intel_smart_page = isp;

	return rc;
}

struct ret_t *
nvme_discover(void)
{
	return _discover(&spdk_nvme_probe, true, &get_health_logs);
}

/** callback for write command completion when wiping out a ns */
static void
write_complete(void *arg, const struct spdk_nvme_cpl *completion)
{
	struct lba0_data *data = arg;

	if (spdk_nvme_cpl_is_success(completion)) {
		data->result = LBA0_WRITE_SUCCESS;
	} else {
		fprintf(stderr, "I/O error status: %s\n",
			spdk_nvme_cpl_get_status_string(&completion->status));
		fprintf(stderr, "Write I/O failed, aborting run\n");
		data->result = LBA0_WRITE_FAIL;
	}
}

static struct wipe_res_t *
wipe_ctrlr(struct ctrlr_entry *centry, struct ns_entry *nentry)
{
	struct lba0_data	 data;
	struct wipe_res_t	*res = NULL, *tmp = NULL;
	int			 rc;
	struct spdk_nvme_qpair	*qpair;
	char			*buf;

	res = init_wipe_res();

	/** convert pci addr to string */
	rc = spdk_pci_addr_fmt(res->ctrlr_pci_addr, sizeof(res->ctrlr_pci_addr),
			       &centry->pci_addr);
	if (rc != 0) {
		res->rc = -NVMEC_ERR_PCI_ADDR_FMT;
		return res;
	}

	/** allocate NVMe queue pair for the controller */
	qpair = spdk_nvme_ctrlr_alloc_io_qpair(centry->ctrlr, NULL, 0);
	if (qpair == NULL) {
		snprintf(res->info, sizeof(res->info),
			 "spdk_nvme_ctrlr_alloc_io_qpair()\n");
		res->rc = -1;
		return res;
	}

	/** allocate a 4K page, with 4K alignment */
	buf =  spdk_dma_zmalloc(4096, 4096, NULL);
	if (buf == NULL) {
		snprintf(res->info, sizeof(res->info),
			 "spdk_dma_zmalloc()\n");
		res->rc = -1;
		spdk_nvme_ctrlr_free_io_qpair(qpair);
		return res;
	}

	/** iterate over the namespaces and wipe them out individually */
	while (nentry != NULL) {
		uint32_t sector_size;

		if (tmp == NULL) {
			/** first iteration */
			tmp = res;
		} else {
			/** allocate new res */
			res = init_wipe_res();
			res->next = tmp;
			tmp = res;
		}

		/** retrieve namespace ID and sector size */
		res->ns_id = spdk_nvme_ns_get_id(nentry->ns);
		sector_size = spdk_nvme_ns_get_sector_size(nentry->ns);

		data.result = LBA0_WRITE_PENDING;
		data.ns_entry = nentry;

		/** zero out the first 4K block */
		rc = spdk_nvme_ns_cmd_write(nentry->ns, qpair,
					    buf, 0 /** LBA start */,
					    4096 / sector_size /** #LBAS */,
					    write_complete, &data, 0);
		if (rc != 0) {
			snprintf(res->info, sizeof(res->info),
				 "spdk_nvme_ns_cmd_write() (%d)\n", rc);
			res->rc = -1;
			break;
		}

		/** wait for command completion */
		while (data.result == LBA0_WRITE_PENDING) {
			rc = spdk_nvme_qpair_process_completions(qpair, 0);
			if (rc < 0) {
				fprintf(stderr,
					"process completions returns %d\n", rc);
				break;
			}
		}

		/** check command result */
		if (data.result != LBA0_WRITE_SUCCESS) {
			snprintf(res->info, sizeof(res->info),
				 "spdk_nvme_ns_cmd_write() failed\n");
			res->rc = -1;
			break;
		}

		nentry = nentry->next;
	}

	spdk_free(buf);
	spdk_nvme_ctrlr_free_io_qpair(qpair);

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
	 * for each NVMe controller found, giving our application a choice on
	 * whether to attach to each controller.  attach_cb will then be
	 * called for each controller after the SPDK NVMe driver has completed
	 * initializing the controller we chose to attach.
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

static int
is_addr_in_allowlist(char *pci_addr, const struct spdk_pci_addr *allowlist,
		     int num_allowlist_devices)
{
	int			i;
	struct spdk_pci_addr    tmp;

	if (spdk_pci_addr_parse(&tmp, pci_addr) != 0) {
		fprintf(stderr, "invalid address %s\n", pci_addr);
		return -EINVAL;
	}

	for (i = 0; i < num_allowlist_devices; i++) {
		if (spdk_pci_addr_compare(&tmp, &allowlist[i]) == 0) {
			return 1;
		}
	}

	return 0;
}

/** Add PCI address to spdk_env_opts allowlist, ignoring any duplicates. */
static int
opts_add_pci_addr(struct spdk_env_opts *opts, struct spdk_pci_addr **list,
		  char *traddr)
{
	int			rc;
	size_t			count = opts->num_pci_addr;
	struct spdk_pci_addr   *tmp = *list;

	rc = is_addr_in_allowlist(traddr, *list, count);
	if (rc < 0)
		return rc;
	if (rc == 1)
		return 0;

	tmp = realloc(tmp, sizeof(*tmp) * (count + 1));
	if (tmp == NULL) {
		fprintf(stderr, "realloc error\n");
		return -ENOMEM;
	}

	*list = tmp;
	if (spdk_pci_addr_parse(*list + count, traddr) < 0) {
		fprintf(stderr, "Invalid address %s\n", traddr);
		return -EINVAL;
	}

	opts->num_pci_addr++;
	return 0;
}

struct ret_t *
daos_spdk_init(int mem_sz, char *env_ctx, size_t nr_pcil, char **pcil)
{
	struct ret_t		*ret = init_ret();
	struct spdk_env_opts	 opts = {};
	int			 rc, i;

	spdk_env_opts_init(&opts);

	if (mem_sz > 0)
		opts.mem_size = mem_sz;
	if (env_ctx != NULL)
		opts.env_context = env_ctx;
	if (nr_pcil > 0) {
		for (i = 0; i < nr_pcil; i++) {
			fprintf(stderr, "spdk env adding pci: %s\n", pcil[i]);

			rc = opts_add_pci_addr(&opts, &opts.pci_allowed,
					       pcil[i]);
			if (rc < 0) {
				fprintf(stderr, "spdk env add pci: %d\n", rc);
				sprintf(ret->info, "DAOS SPDK add pci failed");
				goto out;
			}
		}
		opts.num_pci_addr = nr_pcil;
	}
	opts.name = "daos_admin";

	rc = spdk_env_init(&opts);
	if (rc < 0) {
		fprintf(stderr, "spdk env init: %d\n", rc);
		sprintf(ret->info, "DAOS SPDK init failed");
	}

out:
	ret->rc = rc;
	return ret;
}

