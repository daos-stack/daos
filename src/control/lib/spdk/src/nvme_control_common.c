/**
* (C) Copyright 2019-2020 Intel Corporation.
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
#include <spdk/vmd.h>
#include <daos_srv/control.h>

#include "nvme_control_common.h"

struct ctrlr_entry	*g_controllers;

bool
probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	 struct spdk_nvme_ctrlr_opts *opts)
{
	return true;
}

void
register_ns(struct ctrlr_entry *centry, struct spdk_nvme_ns *ns)
{
	struct ns_entry				*nentry;
	const struct spdk_nvme_ctrlr_data	*cdata;

	/*
	 * spdk_nvme_ctrlr is the logical abstraction in SPDK for an NVMe
	 *  controller.  During initialization, the IDENTIFY data for the
	 *  controller is read using an NVMe admin command, and that data
	 *  can be retrieved using spdk_nvme_ctrlr_get_data() to get
	 *  detailed information on the controller.  Refer to the NVMe
	 *  specification for more details on IDENTIFY for NVMe controllers.
	 */
	cdata = spdk_nvme_ctrlr_get_data(centry->ctrlr);

	if (!spdk_nvme_ns_is_active(ns)) {
		printf("Controller %-20.20s (%-20.20s): Skip inactive NS %u\n",
			cdata->mn, cdata->sn, spdk_nvme_ns_get_id(ns));
		return;
	}

	nentry = calloc(1, sizeof(struct ns_entry));
	if (nentry == NULL) {
		perror("ns_entry calloc");
		exit(1);
	}

	nentry->ns = ns;
	nentry->next = centry->nss;
	centry->nss = nentry;
}

void
attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	  struct spdk_nvme_ctrlr *ctrlr,
	  const struct spdk_nvme_ctrlr_opts *opts)
{
	struct ctrlr_entry	*entry;
	struct spdk_nvme_ns	*ns;
	int			 nsid, num_ns;

	entry = calloc(1, sizeof(struct ctrlr_entry));
	if (entry == NULL) {
		perror("ctrlr_entry calloc");
		exit(1);
	}

	if (spdk_pci_addr_parse(&entry->pci_addr, trid->traddr) != 0) {
		perror("pci_addr_parse");
		exit(1);
	}
	entry->ctrlr = ctrlr;
	entry->health = NULL;
	entry->nss = NULL;
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
		register_ns(entry, ns);
	}
}

struct wipe_res_t *
init_wipe_res(void)
{
	struct wipe_res_t *res = calloc(1, sizeof(struct wipe_res_t));

	if (res == NULL) {
		perror("wipe_res_t calloc");
		exit(1);
	}

	return res;
}

struct ret_t *
init_ret(void)
{
	struct ret_t *ret = calloc(1, sizeof(struct ret_t));

	if (ret == NULL) {
		perror("ret_t calloc");
		exit(1);
	}

	return ret;
}

void
clean_ret(struct ret_t *ret)
{
	struct ctrlr_t		*cnext;
	struct ns_t		*nnext;
	struct wipe_res_t	*wrnext;

	while (ret && (ret->wipe_results)) {
		wrnext = ret->wipe_results->next;
		free(ret->wipe_results);
		ret->wipe_results = wrnext;
	}

	while (ret && (ret->ctrlrs)) {
		while (ret->ctrlrs->nss) {
			nnext = ret->ctrlrs->nss->next;
			free(ret->ctrlrs->nss);
			ret->ctrlrs->nss = nnext;
		}
		if (ret->ctrlrs->stats)
			free(ret->ctrlrs->stats);

		cnext = ret->ctrlrs->next;
		free(ret->ctrlrs);
		ret->ctrlrs = cnext;
	}
}

int
get_controller(struct ctrlr_entry **entry, char *addr)
{
	struct ctrlr_entry	*centry;
	struct spdk_pci_addr	 pci_addr;

	centry = g_controllers;

	if (spdk_pci_addr_parse(&pci_addr, addr) != 0)
		return -NVMEC_ERR_PCI_ADDR_PARSE;

	while (centry) {
		if (spdk_pci_addr_compare(&centry->pci_addr, &pci_addr) == 0)
			break;

		centry = centry->next;
	}

	if (!centry)
		return -NVMEC_ERR_CTRLR_NOT_FOUND;
	*entry = centry;

	return 0;
}

struct ret_t *
_discover(prober probe, bool detach, health_getter get_health)
{
	struct ctrlr_entry	*ctrlr_entry;
	struct health_entry	*health_entry;
	struct ret_t		*ret;
	int			 rc;

	/*
	 * Start the SPDK NVMe enumeration process.  probe_cb will be called
	 *  for each NVMe controller found, giving our application a choice on
	 *  whether to attach to each controller.  attach_cb will then be
	 *  called for each controller after the SPDK NVMe driver has completed
	 *  initializing the controller we chose to attach.
	 */
	rc = probe(NULL, NULL, probe_cb, attach_cb, NULL);
	if (rc != 0)
		goto fail;

	if (!g_controllers || !g_controllers->ctrlr)
		return init_ret(); /* no controllers */

	/*
	 * Collect NVMe SSD health stats for each probed controller.
	 * TODO: move to attach_cb?
	 */
	ctrlr_entry = g_controllers;

	while (ctrlr_entry) {
		health_entry = calloc(1, sizeof(struct health_entry));
		if (health_entry == NULL) {
			rc = -ENOMEM;
			goto fail;
		}

		rc = get_health(ctrlr_entry->ctrlr, health_entry);
		if (rc != 0) {
			free(health_entry);
			goto fail;
		}

		ctrlr_entry->health = health_entry;
		ctrlr_entry = ctrlr_entry->next;
	}

	ret = collect();
	/* TODO: cleanup(detach); */
	return ret;

fail:
	cleanup(detach);
	ret = init_ret();
	ret->rc = rc;
	return ret;
}

static int
copy_ctrlr_data(struct ctrlr_t *cdst, const struct spdk_nvme_ctrlr_data *cdata)
{
	if (copy_ascii(cdst->model, sizeof(cdst->model), cdata->mn,
		       sizeof(cdata->mn)) != 0)
		return -NVMEC_ERR_CHK_SIZE;

	if (copy_ascii(cdst->serial, sizeof(cdst->serial), cdata->sn,
		       sizeof(cdata->sn)) != 0)
		return -NVMEC_ERR_CHK_SIZE;

	if (copy_ascii(cdst->fw_rev, sizeof(cdst->fw_rev), cdata->fr,
		       sizeof(cdata->fr)) != 0)
		return -NVMEC_ERR_CHK_SIZE;

	return 0;
}

static int
collect_namespaces(struct ns_entry *ns_entry, struct ctrlr_t *ctrlr)
{
	struct ns_t	*ns_tmp;

	while (ns_entry) {
		ns_tmp = calloc(1, sizeof(struct ns_t));
		if (ns_tmp == NULL) {
			perror("ns_t calloc");
			return -ENOMEM;
		}

		ns_tmp->id = spdk_nvme_ns_get_id(ns_entry->ns);
		ns_tmp->size = spdk_nvme_ns_get_size(ns_entry->ns);
		ns_tmp->next = ctrlr->nss;
		ctrlr->nss = ns_tmp;

		ns_entry = ns_entry->next;
	}

	return 0;
}

static int
populate_dev_health(struct nvme_stats *dev_state,
		    struct spdk_nvme_health_information_page *page,
		    const struct spdk_nvme_ctrlr_data *cdata)
{
	union spdk_nvme_critical_warning_state	cw = page->critical_warning;
	int					written;

	dev_state->warn_temp_time = page->warning_temp_time;
	dev_state->crit_temp_time = page->critical_temp_time;
	dev_state->ctrl_busy_time = page->controller_busy_time[0];
	dev_state->power_cycles = page->power_cycles[0];
	dev_state->power_on_hours = page->power_on_hours[0];
	dev_state->unsafe_shutdowns = page->unsafe_shutdowns[0];
	dev_state->media_errs = page->media_errors[0];
	dev_state->err_log_entries = page->num_error_info_log_entries[0];
	dev_state->temperature = page->temperature;
	dev_state->temp_warn = cw.bits.temperature ? true : false;
	dev_state->avail_spare_warn = cw.bits.available_spare ?
		true : false;
	dev_state->dev_reliability_warn = cw.bits.device_reliability ?
		true : false;
	dev_state->read_only_warn = cw.bits.read_only ? true : false;
	dev_state->volatile_mem_warn = cw.bits.volatile_memory_backup ?
		true : false;

	written = snprintf(dev_state->model, sizeof(dev_state->model),
			   "%-20.20s", cdata->mn);
	if (written >= sizeof(dev_state->model)) {
		return -NVMEC_ERR_WRITE_TRUNC;
	}

	written = snprintf(dev_state->serial, sizeof(dev_state->serial),
			   "%-20.20s", cdata->sn);
	if (written >= sizeof(dev_state->serial)) {
		return -NVMEC_ERR_WRITE_TRUNC;
	}

	return 0;
}

void
_collect(struct ret_t *ret, data_copier copy_data, pci_getter get_pci,
	 socket_id_getter get_socket_id)
{
	struct ctrlr_entry			*ctrlr_entry;
	const struct spdk_nvme_ctrlr_data	*cdata;
	struct spdk_pci_device			*pci_dev;
	struct nvme_stats			*cstats;
	struct ctrlr_t				*ctrlr_tmp;
	int					 rc, written;

	ctrlr_entry = g_controllers;

	while (ctrlr_entry) {
		ctrlr_tmp = calloc(1, sizeof(struct ctrlr_t));
		if (!ctrlr_tmp) {
			rc = -ENOMEM;
			goto fail;
		}

		ctrlr_tmp->nss = NULL;
		ctrlr_tmp->stats = NULL;
		ctrlr_tmp->next = NULL;

		cdata = spdk_nvme_ctrlr_get_data(ctrlr_entry->ctrlr);

		rc = copy_data(ctrlr_tmp, cdata);
		if (rc != 0)
			goto fail;

		rc = spdk_pci_addr_fmt(ctrlr_tmp->pci_addr,
				       sizeof(ctrlr_tmp->pci_addr),
				       &ctrlr_entry->pci_addr);
		if (rc != 0) {
			rc = -NVMEC_ERR_PCI_ADDR_FMT;
			goto fail;
		}

		pci_dev = get_pci(ctrlr_entry->ctrlr);
		if (!pci_dev) {
			rc = -NVMEC_ERR_GET_PCI_DEV;
			goto fail;
		}

		/* populate numa socket id & pci device type */
		ctrlr_tmp->socket_id = get_socket_id(pci_dev);
		written = snprintf(ctrlr_tmp->pci_type,
				   sizeof(ctrlr_tmp->pci_type), "%s",
				   spdk_pci_device_get_type(pci_dev));
		if (written >= sizeof(ctrlr_tmp->pci_type)) {
			rc = -NVMEC_ERR_CHK_SIZE;
			free(pci_dev);
			goto fail;
		}
		free(pci_dev);

		/* Alloc linked list of namespaces per controller */
		if (ctrlr_entry->nss) {
			rc = collect_namespaces(ctrlr_entry->nss, ctrlr_tmp);
			if (rc != 0)
				goto fail;
		}

		/* Alloc device health stats per controller */
		if (ctrlr_entry->health) {
			cstats = calloc(1, sizeof(struct nvme_stats));
			if (cstats == NULL) {
				rc = -ENOMEM;
				goto fail;
			}

			/* Store device health stats for export */
			rc = populate_dev_health(cstats,
						 &ctrlr_entry->health->page,
						 cdata);
			if (rc != 0) {
				free(cstats);
				goto fail;
			}

			ctrlr_tmp->stats = cstats;
		}

		ctrlr_tmp->next = ret->ctrlrs;
		ret->ctrlrs = ctrlr_tmp;

		ctrlr_entry = ctrlr_entry->next;
	}

	return;
fail:
	ret->rc = rc;
	if (ret->rc == 0)
		ret->rc = -1;
	if (ctrlr_tmp)
		free(ctrlr_tmp);
	clean_ret(ret);
	return;
}

struct ret_t *
collect(void)
{
	struct ret_t *ret;

	ret = init_ret();
	_collect(ret, &copy_ctrlr_data, &spdk_nvme_ctrlr_get_pci_device,
		 &spdk_pci_device_get_socket_id);

	return ret;
}

void
cleanup(bool detach)
{
	struct ns_entry		*nentry;
	struct ctrlr_entry	*centry, *cnext;

	centry = g_controllers;

	while (centry) {
		if ((centry->ctrlr) && (detach))
			spdk_nvme_detach(centry->ctrlr);
		while (centry->nss) {
			nentry = centry->nss->next;
			free(centry->nss);
			centry->nss = nentry;
		}
		if (centry->health)
			free(centry->health);

		cnext = centry->next;
		free(centry);
		centry = cnext;
	}

	g_controllers = NULL;
}

