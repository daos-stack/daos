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
* provided in Contract No. 8F-30005.
* Any reproduction of computer software, computer software documentation, or
* portions thereof marked with this legend must also reproduce the markings.
*/

#include "spdk/stdinc.h"
#include "spdk/nvme.h"
#include "spdk/env.h"

#include "nvme_control.h"
#include "nvme_control_common.h"

void
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
		printf("Controller %-20.20s (%-20.20s): Skip inactive NS %u\n",
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

bool
probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	 struct spdk_nvme_ctrlr_opts *opts)
{
	return true;
}

void
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

	if (spdk_pci_addr_parse(&entry->pci_addr, trid->traddr) != 0) {
		perror("pci_addr_parse");
		exit(1);
	}
	entry->ctrlr = ctrlr;
	entry->dev_health = NULL;
	entry->next = g_controllers;
	g_controllers = entry;

	/*
	 * Each controller has one or more namespaces.  An NVMe namespace is
	 *  basically equivalent to a SCSI LUN.  The controller's IDENTIFY data
	 *  tells us how many namespaces exist on the controller.
	 *  For Intel(R) P3X00 controllers, it will just be one namespace.
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

struct ret_t *
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

int
check_size(int written, int max, char *msg, struct ret_t *ret)
{
	if (written >= max) {
		snprintf(ret->err, sizeof(ret->err), "%s", msg);
		ret->rc = -NVMEC_ERR_CHK_SIZE;
		return ret->rc;
	}

	return NVMEC_SUCCESS;
}

struct ctrlr_entry *
get_controller(char *addr, struct ret_t *ret)
{
	struct spdk_pci_addr			pci_addr;
	struct ctrlr_entry			*entry = NULL;

	entry = g_controllers;

	if (spdk_pci_addr_parse(&pci_addr, addr) != 0) {
		snprintf(ret->err, sizeof(ret->err),
			 "pci addr could not be parsed: %s", addr);
		ret->rc = -NVMEC_ERR_PCI_ADDR_PARSE;
		return entry;
	}

	while (entry) {
		if (spdk_pci_addr_compare(&entry->pci_addr, &pci_addr) == 0)
			break;

		entry = entry->next;
	}

	if (entry == NULL) {
		snprintf(ret->err, sizeof(ret->err), "controller not found");
		ret->rc = -NVMEC_ERR_CTRLR_NOT_FOUND;
		return entry;
	}

	return entry;
}

void
collect(struct ret_t *ret)
{
	struct ns_entry				*ns_entry;
	struct ctrlr_entry			*ctrlr_entry;
	const struct spdk_nvme_ctrlr_data	*cdata;
	struct spdk_pci_device			*pci_dev;
	struct spdk_pci_addr			pci_addr;
	struct ctrlr_t				*ctrlr_tmp;
	struct ns_t				*ns_tmp;
	int					written;
	int					rc;

	ns_entry = g_namespaces;
	ctrlr_entry = g_controllers;

	while (ns_entry) {
		ns_tmp = malloc(sizeof(struct ns_t));

		if (ns_tmp == NULL) {
			snprintf(ret->err, sizeof(ret->err), "ns_t malloc");
			ret->rc = -ENOMEM;
			return;
		}

		ns_tmp->id = spdk_nvme_ns_get_id(ns_entry->ns);
		/* capacity in GBytes */
		ns_tmp->size = spdk_nvme_ns_get_size(ns_entry->ns) /
			       NVMECONTROL_GBYTE_BYTES;

		pci_dev = spdk_nvme_ctrlr_get_pci_device(ns_entry->ctrlr);
		if (!pci_dev) {
			snprintf(ret->err, sizeof(ret->err),
				 "%s: get_pci_device", __func__);

			ret->rc = -NVMEC_ERR_GET_PCI_DEV;
			return;
		}

		pci_addr = spdk_pci_device_get_addr(pci_dev);
		rc = spdk_pci_addr_fmt(ns_tmp->ctrlr_pci_addr,
				       sizeof(ns_tmp->ctrlr_pci_addr),
				       &pci_addr);
		if (rc != 0) {
			snprintf(ret->err, sizeof(ret->err),
				 "spdk_pci_addr_fmt: rc %d", rc);
			ret->rc = -NVMEC_ERR_PCI_ADDR_FMT;
			return;
		}

		ns_tmp->next = ret->nss;
		ret->nss = ns_tmp;

		ns_entry = ns_entry->next;
	}

	while (ctrlr_entry) {
		ctrlr_tmp = malloc(sizeof(struct ctrlr_t));

		if (ctrlr_tmp == NULL) {
			perror("ctrlr_t malloc");
			ret->rc = -ENOMEM;
			return;
		}

		cdata = spdk_nvme_ctrlr_get_data(ctrlr_entry->ctrlr);

		written = snprintf(ctrlr_tmp->model, sizeof(ctrlr_tmp->model),
				   "%-20.20s", cdata->mn);
		if (check_size(written, sizeof(ctrlr_tmp->model),
			       "model truncated", ret) != 0) {
			return;
		}

		written = snprintf(ctrlr_tmp->serial, sizeof(ctrlr_tmp->serial),
				   "%-20.20s", cdata->sn);
		if (check_size(written, sizeof(ctrlr_tmp->serial),
			       "serial truncated", ret) != 0) {
			return;
		}

		written = snprintf(ctrlr_tmp->fw_rev, sizeof(ctrlr_tmp->fw_rev),
				   "%s", cdata->fr);
		if (check_size(written, sizeof(ctrlr_tmp->fw_rev),
			       "firmware revision truncated", ret) != 0) {
			return;
		}

		rc = spdk_pci_addr_fmt(ctrlr_tmp->pci_addr,
				       sizeof(ctrlr_tmp->pci_addr),
				       &ctrlr_entry->pci_addr);
		if (rc != 0) {
			snprintf(ret->err, sizeof(ret->err),
				 "spdk_pci_addr_fmt: rc %d", rc);
			ret->rc = -NVMEC_ERR_PCI_ADDR_FMT;
			return;
		}

		pci_dev = spdk_nvme_ctrlr_get_pci_device(ctrlr_entry->ctrlr);
		if (!pci_dev) {
			snprintf(ret->err, sizeof(ret->err), "get_pci_device");
			ret->rc = -NVMEC_ERR_GET_PCI_DEV;
			return;
		}

		/* populate numa socket id */
		ctrlr_tmp->socket_id = spdk_pci_device_get_socket_id(pci_dev);

		/*
		 * Alloc device health stats per controller only if device
		 * health stats are queried, not by default for discovery.
		 */
		if (ctrlr_entry->dev_health != NULL)
			ret->rc = collect_health_stats(ctrlr_entry->dev_health,
						       ctrlr_tmp);

		/* cdata->cntlid is not unique per host, only per subsystem */
		ctrlr_tmp->next = ret->ctrlrs;
		ret->ctrlrs = ctrlr_tmp;

		ctrlr_entry = ctrlr_entry->next;
	}

	ret->rc = 0;
}

int
collect_health_stats(struct dev_health_entry *entry, struct ctrlr_t *ctrlr)
{
	struct dev_health_t			 *h_tmp;
	struct spdk_nvme_health_information_page health_pg;
	union spdk_nvme_critical_warning_state	 cwarn;

	h_tmp = malloc(sizeof(struct dev_health_t));
	if (h_tmp == NULL) {
		perror("dev_health_t malloc");
		return -ENOMEM;
	}

	health_pg = entry->health_page;
	h_tmp->temperature = health_pg.temperature;
	h_tmp->warn_temp_time = health_pg.warning_temp_time;
	h_tmp->crit_temp_time = health_pg.critical_temp_time;
	h_tmp->ctrl_busy_time = health_pg.controller_busy_time[0];
	h_tmp->power_cycles = health_pg.power_cycles[0];
	h_tmp->power_on_hours = health_pg.power_on_hours[0];
	h_tmp->unsafe_shutdowns = health_pg.unsafe_shutdowns[0];
	h_tmp->media_errors = health_pg.media_errors[0];
	h_tmp->error_log_entries = health_pg.num_error_info_log_entries[0];
	/* Critical warnings */
	cwarn = entry->health_page.critical_warning;
	h_tmp->temp_warning = cwarn.bits.temperature ? true : false;
	h_tmp->avail_spare_warning = cwarn.bits.available_spare ? true : false;
	h_tmp->dev_reliabilty_warning = cwarn.bits.device_reliability ?
					true : false;
	h_tmp->read_only_warning = cwarn.bits.read_only ? true : false;
	h_tmp->volatile_mem_warning = cwarn.bits.volatile_memory_backup ?
					true : false;

	ctrlr->dev_health = h_tmp;

	return 0;
}

