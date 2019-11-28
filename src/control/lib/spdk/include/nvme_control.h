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

#ifndef NVMECONTROL_H
#define NVMECONTROL_H

/**
 * \brief NVMe controller details
 */
struct ctrlr_t {
	char		     model[1024];
	char		     serial[1024];
	char		     pci_addr[1024];
	char		     fw_rev[1024];
	int		     socket_id;
	struct dev_health_t *dev_health;
	struct ctrlr_t	    *next;
};

/**
 * \brief NVMe namespace details
 */
struct ns_t {
	int		id;
	int		size;
	char		ctrlr_pci_addr[1024];
	struct ns_t    *next;
};

/*
 * \brief Raw SPDK device health statistics.
 */
struct dev_health_t {
	uint16_t	 temperature; /* in Kelvin */
	uint32_t	 warn_temp_time;
	uint32_t	 crit_temp_time;
	uint64_t	 ctrl_busy_time;
	uint64_t	 power_cycles;
	uint64_t	 power_on_hours;
	uint64_t	 unsafe_shutdowns;
	uint64_t	 media_errors;
	uint64_t	 error_log_entries;
	/* Critical warnings */
	bool		 temp_warning;
	bool		 avail_spare_warning;
	bool		 dev_reliabilty_warning;
	bool		 read_only_warning;
	bool		 volatile_mem_warning;
};

/**
 * \brief Return containing return code, controllers, namespaces and error
 * message
 */
struct ret_t {
	int		rc;
	struct ctrlr_t *ctrlrs;
	struct ns_t    *nss;
	char		err[1024];
};

struct ctrlr_entry {
	struct spdk_nvme_ctrlr	*ctrlr;
	struct spdk_pci_addr	 pci_addr;
	struct dev_health_entry	*dev_health;
	int			 socket_id;
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

extern struct ctrlr_entry	*g_controllers;
extern struct ns_entry		*g_namespaces;

/**
 * Provide ability to pass function pointers to nvme_discover for mocking
 * in unit tests.
 */
typedef int (*prober)(const struct spdk_nvme_transport_id *, void *,
		      spdk_nvme_probe_cb,
		      spdk_nvme_attach_cb,
		      spdk_nvme_remove_cb);
typedef int (*detacher)(struct spdk_nvme_ctrlr *);
typedef int (*health_getter)(struct spdk_nvme_ctrlr *,
			     struct dev_health_entry *);
struct ret_t *
_nvme_discover(prober, detacher, health_getter);

/**
 * Discover NVMe controllers and namespaces, as well as return device health
 * information.
 *
 * \return a pointer to a return struct (ret_t).
 */
struct ret_t *
nvme_discover(void);

/**
 * Update NVMe controller firmware.
 *
 * \param ctrlr_pci_addr PCI address of NVMe controller.
 * \param path Local filepath where firmware image is stored.
 * \param slot Identifier of software slot/register to upload to.
 *
 * \return a pointer to a return struct (ret_t).
 */
struct ret_t *
nvme_fwupdate(char *ctrlr_pci_addr, char *path, unsigned int slot);

/**
 * Format NVMe controller namespace.
 *
 * \param ctrlr_pci_addr PCI address of NVMe controller.
 *
 * \return a pointer to a return struct (ret_t).
 */
struct ret_t *
nvme_format(char *ctrlr_pci_addr);

/**
 * Cleanup structs held in memory.
 */
void
nvme_cleanup(detacher);

#endif
