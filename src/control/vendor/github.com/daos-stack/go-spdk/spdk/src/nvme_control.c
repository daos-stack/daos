//
// (C) Copyright 2018-2019 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

#include "spdk/stdinc.h"

#include "spdk/nvme.h"
#include "spdk/env.h"

#include "nvme_control.h"

struct ctrlr_entry {
	struct spdk_nvme_ctrlr	*ctrlr;
	const char		*tr_addr;
	struct dev_health_entry *dev_health;
	struct ctrlr_entry	*next;
};

struct ns_entry {
	struct spdk_nvme_ctrlr	*ctrlr;
	struct spdk_nvme_ns	*ns;
	struct ns_entry		*next;
	struct spdk_nvme_qpair	*qpair;
};

struct dev_health_entry {
	struct spdk_nvme_health_information_page health_page;
	struct spdk_nvme_error_information_entry error_page[256];
	int					 inflight;
};

static struct ctrlr_entry	*g_controllers;
static struct ns_entry		*g_namespaces;

static void
register_ns(struct spdk_nvme_ctrlr *ctrlr, struct spdk_nvme_ns *ns)
{
	struct ns_entry				*entry;
	const struct spdk_nvme_ctrlr_data	*cdata;

	/*
	 * spdk_nvme_ctrlr is the logical abstraction in SPDK for an NVMe
	 *  controller.  During initialization, the IDENTIFY data for the
	 *  controller is read using an NVMe admin command, and that data
	 *  can be retrieved using spdk_nvme_ctrlr_get_data() to get
	 *  detailed information on the controller.  Refer to the NVMe
	 *  specification for more details on IDENTIFY for NVMe controllers.
	 */
	cdata = spdk_nvme_ctrlr_get_data(ctrlr);

	if (!spdk_nvme_ns_is_active(ns)) {
		printf("Controller %-20.20s (%-20.20s): Skipping inactive NS %u\n",
			cdata->mn, cdata->sn, spdk_nvme_ns_get_id(ns));
		return;
	}

	entry = malloc(sizeof(struct ns_entry));
	if (entry == NULL) {
		perror("ns_entry malloc");
		exit(1);
	}

	entry->ctrlr = ctrlr;
	entry->ns = ns;
	entry->next = g_namespaces;
	g_namespaces = entry;
}

static bool
probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	 struct spdk_nvme_ctrlr_opts *opts)
{
	return true;
}

static void
attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	  struct spdk_nvme_ctrlr *ctrlr,
	  const struct spdk_nvme_ctrlr_opts *opts)
{
	int			nsid, num_ns;
	struct ctrlr_entry	*entry;
	struct spdk_nvme_ns	*ns;

	entry = malloc(sizeof(struct ctrlr_entry));
	if (entry == NULL) {
		perror("ctrlr_entry malloc");
		exit(1);
	}

	entry->ctrlr = ctrlr;
	entry->tr_addr = trid->traddr;
	entry->dev_health = NULL;
	entry->next = g_controllers;
	g_controllers = entry;

	/*
	 * Each controller has one or more namespaces.  An NVMe namespace is basically
	 *  equivalent to a SCSI LUN.  The controller's IDENTIFY data tells us how
	 *  many namespaces exist on the controller.  For Intel(R) P3X00 controllers,
	 *  it will just be one namespace.
	 *
	 * Note that in NVMe, namespace IDs start at 1, not 0.
	 */
	num_ns = spdk_nvme_ctrlr_get_num_ns(ctrlr);
	for (nsid = 1; nsid <= num_ns; nsid++) {
		ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
		if (ns == NULL)
			continue;
		register_ns(ctrlr, ns);
	}
}

static struct ret_t *
init_ret(void)
{
	struct ret_t *ret;

	ret = malloc(sizeof(struct ret_t));
	ret->rc = 0;
	ret->ctrlrs = NULL;
	ret->nss = NULL;
	snprintf(ret->err, sizeof(ret->err), "none");

	return ret;
}

static int
check_size(int written, int max, char *msg, struct ret_t *ret)
{
	if (written >= max) {
		snprintf(ret->err, sizeof(ret->err), "%s", msg);
		ret->rc = -NVMEC_ERR_CHK_SIZE;
		return ret->rc;
	}

	return NVMEC_SUCCESS;
}

static int
set_pci_addr(
	struct spdk_nvme_ctrlr *ctrlr, char *ctrlr_pci_addr, size_t size,
	struct ret_t *ret)
{
	int 			rc;
	struct spdk_pci_device	*pci_dev;
	struct spdk_pci_addr	pci_addr;

	pci_dev = spdk_nvme_ctrlr_get_pci_device(ctrlr);
	if (!pci_dev) {
		snprintf(ret->err, sizeof(ret->err), "get_pci_device");
		ret->rc = -NVMEC_ERR_GET_PCI_DEV;
		return ret->rc;
	}

	// populate ns_t.ctrlr_pci_addr to map ns->ctrlr
	pci_addr = spdk_pci_device_get_addr(pci_dev);
	rc = spdk_pci_addr_fmt(ctrlr_pci_addr, size, &pci_addr);
	if (rc != 0) {
		snprintf(ret->err, sizeof(ret->err),
			"spdk_pci_addr_fmt: rc %d", rc);
		ret->rc = -NVMEC_ERR_PCI_ADDR_FMT;
		return ret->rc;
	}

	return NVMEC_SUCCESS;
}

static int
collect_health_stats(struct dev_health_entry *dev_health_entry,
		     struct ctrlr_t *ctrlr)
{
	struct dev_health_t *health_tmp;
	union spdk_nvme_critical_warning_state cwarn;

	health_tmp = malloc(sizeof(struct dev_health_t));
	if (health_tmp == NULL) {
		perror("dev_health_t malloc");
		return -ENOMEM;
	}

	health_tmp->temperature = dev_health_entry->health_page.temperature;
	health_tmp->warn_temp_time = dev_health_entry->health_page.warning_temp_time;
	health_tmp->crit_temp_time = dev_health_entry->health_page.critical_temp_time; 
	health_tmp->ctrl_busy_time = dev_health_entry->health_page.controller_busy_time[0];
	health_tmp->power_cycles = dev_health_entry->health_page.power_cycles[0];
	health_tmp->power_on_hours = dev_health_entry->health_page.power_on_hours[0];
	health_tmp->unsafe_shutdowns = dev_health_entry->health_page.unsafe_shutdowns[0];
	health_tmp->media_errors = dev_health_entry->health_page.media_errors[0];
	health_tmp->error_log_entries = dev_health_entry->health_page.num_error_info_log_entries[0];
	/* Critical warnings */
	cwarn = dev_health_entry->health_page.critical_warning;
	health_tmp->temp_warning = cwarn.bits.temperature ? true : false;
	health_tmp->avail_spare_warning = cwarn.bits.available_spare ? true : false;
	health_tmp->dev_reliabilty_warning = cwarn.bits.device_reliability ? true : false;
	health_tmp->read_only_warning = cwarn.bits.read_only ? true : false;
	health_tmp->volatile_mem_warning = cwarn.bits.volatile_memory_backup ? true : false;

	ctrlr->dev_health = health_tmp;

	return 0;
}

static void
collect(struct ret_t *ret)
{
	struct ns_entry				*ns_entry;
	struct ctrlr_entry			*ctrlr_entry;
	const struct spdk_nvme_ctrlr_data	*cdata;
	int					written;

	ns_entry = g_namespaces;
	ctrlr_entry = g_controllers;

	while (ns_entry) {
		struct ns_t *ns_tmp;

		ns_tmp = malloc(sizeof(struct ns_t));

		if (ns_tmp == NULL) {
			snprintf(ret->err, sizeof(ret->err), "ns_t malloc");
			ret->rc = -ENOMEM;
			return;
		}

		cdata = spdk_nvme_ctrlr_get_data(ns_entry->ctrlr);

		ns_tmp->id = spdk_nvme_ns_get_id(ns_entry->ns);
		// capacity in GBytes
		ns_tmp->size = spdk_nvme_ns_get_size(ns_entry->ns) / \
			       NVMECONTROL_GBYTE_BYTES;

		if (set_pci_addr(
			ns_entry->ctrlr, ns_tmp->ctrlr_pci_addr,
			sizeof(ns_tmp->ctrlr_pci_addr), ret) != 0) {

			return;
		}

		ns_tmp->next = ret->nss;
		ret->nss = ns_tmp;

		ns_entry = ns_entry->next;
	}

	while (ctrlr_entry) {
		struct ctrlr_t *ctrlr_tmp;
		struct dev_health_t *health_tmp;

		ctrlr_tmp = malloc(sizeof(struct ctrlr_t));

		if (ctrlr_tmp == NULL) {
			perror("ctrlr_t malloc");
			ret->rc = -ENOMEM;
			return;
		}

		health_tmp = malloc(sizeof(struct dev_health_t));
		if (health_tmp == NULL) {
			perror("dev_health_t malloc");
			ret->rc = -ENOMEM;
			return;
		}

		cdata = spdk_nvme_ctrlr_get_data(ctrlr_entry->ctrlr);

		written = snprintf(
			ctrlr_tmp->model, sizeof(ctrlr_tmp->model),
			"%-20.20s", cdata->mn
		);
		if (check_size(
			written, sizeof(ctrlr_tmp->model),
			"model truncated", ret) != 0) {

			return;
		}

		written = snprintf(
			ctrlr_tmp->serial, sizeof(ctrlr_tmp->serial),
			"%-20.20s", cdata->sn
		);
		if (check_size(
			written, sizeof(ctrlr_tmp->serial),
			"serial truncated", ret) != 0) {

			return;
		}

		written = snprintf(
			ctrlr_tmp->fw_rev, sizeof(ctrlr_tmp->fw_rev),
			"%s", cdata->fr
		);
		if (check_size(
			written, sizeof(ctrlr_tmp->fw_rev),
			"firmware revision truncated", ret) != 0) {

			return;
		}

		if (set_pci_addr(
			ctrlr_entry->ctrlr, ctrlr_tmp->pci_addr,
			sizeof(ctrlr_tmp->pci_addr), ret) != 0) {

			return;
		}

		/*
		 * Alloc device health stats per controller only if device
		 * health stats are queried, not by default for discovery.
		 */
		if (ctrlr_entry->dev_health)
			ret->rc = collect_health_stats(ctrlr_entry->dev_health, ctrlr_tmp);

		// cdata->cntlid is not unique per host, only per subsystem
		ctrlr_tmp->next = ret->ctrlrs;
		ret->ctrlrs = ctrlr_tmp;

		ctrlr_entry = ctrlr_entry->next;
	}

	ret->rc = 0;
}

static void
cleanup(void)
{
	struct ns_entry		*ns_entry;
	struct ctrlr_entry	*ctrlr_entry;

	ns_entry = g_namespaces;
	ctrlr_entry = g_controllers;

	while (ns_entry) {
		struct ns_entry *next = ns_entry->next;

		free(ns_entry);
		ns_entry = next;
	}

	while (ctrlr_entry) {
		struct ctrlr_entry *next = ctrlr_entry->next;

		free(ctrlr_entry->dev_health);
		spdk_nvme_detach(ctrlr_entry->ctrlr);
		free(ctrlr_entry);
		ctrlr_entry = next;
	}
}

static void
get_spdk_log_page_completion(void *cb_arg, const struct spdk_nvme_cpl *cpl)
{
	struct dev_health_entry *entry = cb_arg;

	if (spdk_nvme_cpl_is_error(cpl))
		printf("Error getting SPDK log page\n");

	entry->inflight--;
}

static void
get_dev_health_logs(struct spdk_nvme_ctrlr *ctrlr, struct dev_health_entry *entry)
{
	struct spdk_nvme_health_information_page health_page;

	entry->inflight++;
	if (spdk_nvme_ctrlr_cmd_get_log_page(ctrlr,
					     SPDK_NVME_LOG_HEALTH_INFORMATION,
					     SPDK_NVME_GLOBAL_NS_TAG,
					     &health_page,
					     sizeof(health_page),
					     0, get_spdk_log_page_completion, entry)) {
		printf("spdk_nvme_ctrlr_cmd_get_log_page() failed for health log\n");
		return;
	}

	while (entry->inflight)
		spdk_nvme_ctrlr_process_admin_completions(ctrlr);

	entry->health_page = health_page;
}

struct ret_t *
nvme_dev_health(void)
{
	struct ret_t		*ret;
	struct ctrlr_entry	*ctrlr_entry;
	struct dev_health_entry	*dev_health_entry;

	ret = init_ret();

	ctrlr_entry = g_controllers;
	if (ctrlr_entry == NULL) {
		printf("NVMe controllers not found/probed\n");
		return ret;
	}

	while (ctrlr_entry) {
		dev_health_entry = malloc(sizeof(struct dev_health_entry));
		if (dev_health_entry == NULL) {
			perror("dev_health_entry malloc");
			exit(1);
		}

		get_dev_health_logs(ctrlr_entry->ctrlr, dev_health_entry);

		ctrlr_entry->dev_health = dev_health_entry;

		ctrlr_entry = ctrlr_entry->next;
	}

	collect(ret);

	return ret;
}

struct ret_t *
nvme_discover(void)
{
	int 		rc;
	struct ret_t	*ret;

	ret = init_ret();

	/*
	 * Start the SPDK NVMe enumeration process.  probe_cb will be called
	 *  for each NVMe controller found, giving our application a choice on
	 *  whether to attach to each controller.  attach_cb will then be
	 *  called for each controller after the SPDK NVMe driver has completed
	 *  initializing the controller we chose to attach.
	 */
	rc = spdk_nvme_probe(NULL, NULL, probe_cb, attach_cb, NULL);

	if (rc != 0) {
		fprintf(stderr, "spdk_nvme_probe() failed\n");
		cleanup();
		return ret;
	}

	if (g_controllers == NULL) {
		fprintf(stderr, "no NVMe controllers found\n");
		cleanup();
		return ret;
	}

	collect(ret);

	return ret;
}

static int
get_controller(char *addr, struct ctrlr_entry *ctrlr_entry, struct ret_t *ret)
{
	struct spdk_pci_device			*pci_dev;
	struct spdk_pci_addr			pci_addr, entry_pci_addr;

	if (spdk_pci_addr_parse(&pci_addr, addr) < 0) {
		snprintf(
			ret->err, sizeof(ret->err),
			"pci addr could not be parsed: %s", addr);
		ret->rc = -NVMEC_ERR_PCI_ADDR_PARSE;

		return ret->rc;
	}

	while (ctrlr_entry) {
		pci_dev = spdk_nvme_ctrlr_get_pci_device(ctrlr_entry->ctrlr);
		if (!pci_dev) {
			snprintf(ret->err, sizeof(ret->err), "get_pci_device");
			ret->rc = -NVMEC_ERR_GET_PCI_DEV;

			return ret->rc;
		}

		entry_pci_addr = spdk_pci_device_get_addr(pci_dev);

		if (spdk_pci_addr_compare(&pci_addr, &entry_pci_addr) == 0)
			break;

		ctrlr_entry = ctrlr_entry->next;
	}

	if (ctrlr_entry == NULL) {
		snprintf(ret->err, sizeof(ret->err), "controller not found");
		ret->rc = -NVMEC_ERR_CTRLR_NOT_FOUND;

		return ret->rc;
	}

	return NVMEC_SUCCESS;
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
	ctrlr_entry = g_controllers;

	rc = get_controller(ctrlr_pci_addr, ctrlr_entry, ret);
	if (rc != 0)
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
	int					rc;
	int					ns_id;
	const struct spdk_nvme_ctrlr_data	*cdata;
	struct spdk_nvme_ns			*ns;
	struct spdk_nvme_format 		format = {};
	struct ctrlr_entry			*ctrlr_entry;
	struct ret_t				*ret;

	ret = init_ret();
	ctrlr_entry = g_controllers;

	rc = get_controller(ctrlr_pci_addr, ctrlr_entry, ret);
	if (rc != 0)
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

	format.lbaf	= 0; // LBA format defaulted to 0
	format.ms	= 0; // metadata transferred as part of a separate buffer
	format.pi	= 0; // protection information is not enabled
	format.pil	= 0; // protection information location N/A
	format.ses	= 0; // no secure erase operation requested

	ret->rc = spdk_nvme_ctrlr_format(ctrlr_entry->ctrlr, ns_id, &format);

	if (ret->rc != 0) {
		snprintf(ret->err, sizeof(ret->err), "format failed");

		return ret;
	}

	collect(ret);

	return ret;
}

void
nvme_cleanup()
{
	cleanup();
}
