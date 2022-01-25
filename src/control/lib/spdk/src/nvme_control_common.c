/**
* (C) Copyright 2019-2022 Intel Corporation.
*
* SPDX-License-Identifier: BSD-2-Clause-Patent
*/

#include <spdk/stdinc.h>
#include <spdk/nvme.h>
#include <spdk/env.h>
#include <spdk/vmd.h>
#include <spdk/nvme_intel.h>
#include <spdk/util.h>
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

/* Return a uint64 raw value from a byte array */
static uint64_t
extend_to_uint64(uint8_t *array, unsigned int len)
{
	uint64_t value = 0;
	int i = len;

	while (i > 0) {
		value += (uint64_t)array[i - 1] << (8 * (i - 1));
		i--;
	}

	return value;
}

static void
populate_dev_health(struct nvme_stats *stats,
		    struct spdk_nvme_health_information_page *hp,
		    struct spdk_nvme_intel_smart_information_page *isp,
		    const struct spdk_nvme_ctrlr_data *cdata)
{
	union spdk_nvme_critical_warning_state	cw = hp->critical_warning;
	struct spdk_nvme_intel_smart_attribute  atb;
	int i;

	stats->warn_temp_time = hp->warning_temp_time;
	stats->crit_temp_time = hp->critical_temp_time;
	stats->ctrl_busy_time = hp->controller_busy_time[0];
	stats->power_cycles = hp->power_cycles[0];
	stats->power_on_hours = hp->power_on_hours[0];
	stats->unsafe_shutdowns = hp->unsafe_shutdowns[0];
	stats->media_errs = hp->media_errors[0];
	stats->err_log_entries = hp->num_error_info_log_entries[0];
	stats->temperature = hp->temperature;
	stats->temp_warn = cw.bits.temperature ? true : false;
	stats->avail_spare_warn = cw.bits.available_spare ? true : false;
	stats->dev_reliability_warn = cw.bits.device_reliability ? true : false;
	stats->read_only_warn = cw.bits.read_only ? true : false;
	stats->volatile_mem_warn = cw.bits.volatile_memory_backup ?
				   true : false;

	/* Intel Smart Information Attributes */
	if (cdata->vid != SPDK_PCI_VID_INTEL)
		return;
	for (i = 0; i < SPDK_COUNTOF(isp->attributes); i++) {
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_PROGRAM_FAIL_COUNT) {
			atb = isp->attributes[i];
			stats->program_fail_cnt_norm = atb.normalized_value;
			stats->program_fail_cnt_raw =
					extend_to_uint64(atb.raw_value, 6);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_ERASE_FAIL_COUNT) {
			atb = isp->attributes[i];
			stats->erase_fail_cnt_norm = atb.normalized_value;
			stats->erase_fail_cnt_raw =
					extend_to_uint64(atb.raw_value, 6);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_WEAR_LEVELING_COUNT) {
			atb = isp->attributes[i];
			stats->wear_leveling_cnt_norm = atb.normalized_value;
			stats->wear_leveling_cnt_min = atb.raw_value[0] |
						       atb.raw_value[1] << 8;
			stats->wear_leveling_cnt_max = atb.raw_value[2] |
						       atb.raw_value[3] << 8;
			stats->wear_leveling_cnt_avg = atb.raw_value[4] |
						       atb.raw_value[5] << 8;
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_E2E_ERROR_COUNT) {
			atb = isp->attributes[i];
			stats->endtoend_err_cnt_raw =
					extend_to_uint64(atb.raw_value, 6);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_CRC_ERROR_COUNT) {
			atb = isp->attributes[i];
			stats->crc_err_cnt_raw =
					extend_to_uint64(atb.raw_value, 6);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_MEDIA_WEAR) {
			atb = isp->attributes[i];
			stats->media_wear_raw =
					extend_to_uint64(atb.raw_value, 6);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_HOST_READ_PERCENTAGE) {
			atb = isp->attributes[i];
			stats->host_reads_raw =
					extend_to_uint64(atb.raw_value, 6);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_TIMER) {
			atb = isp->attributes[i];
			stats->workload_timer_raw =
					extend_to_uint64(atb.raw_value, 6);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_THERMAL_THROTTLE_STATUS) {
			atb = isp->attributes[i];
			stats->thermal_throttle_status = atb.raw_value[0];
			stats->thermal_throttle_event_cnt =
					extend_to_uint64(&atb.raw_value[1], 4);
		}
		if (isp->attributes[i].code ==
			  SPDK_NVME_INTEL_SMART_RETRY_BUFFER_OVERFLOW_COUNTER) {
			atb = isp->attributes[i];
			stats->retry_buffer_overflow_cnt =
					extend_to_uint64(atb.raw_value, 6);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_PLL_LOCK_LOSS_COUNT) {
			atb = isp->attributes[i];
			stats->pll_lock_loss_cnt =
					extend_to_uint64(atb.raw_value, 6);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_NAND_BYTES_WRITTEN) {
			atb = isp->attributes[i];
			stats->nand_bytes_written =
					extend_to_uint64(atb.raw_value, 6);
		}
		if (isp->attributes[i].code ==
				SPDK_NVME_INTEL_SMART_HOST_BYTES_WRITTEN) {
			atb = isp->attributes[i];
			stats->host_bytes_written =
					extend_to_uint64(atb.raw_value, 6);
		}
	}
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
			populate_dev_health(cstats, &ctrlr_entry->health->page,
				&ctrlr_entry->health->intel_smart_page, cdata);
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
		/* Catch unexpected failures */
		ret->rc = -EINVAL;
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

