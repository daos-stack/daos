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

#ifndef NVMECONTROL_COMMON_H
#define NVMECONTROL_COMMON_H

#include <stdbool.h>

/**
 * \brief NVMECONTROL return codes
 */
enum NvmeControlStatusCode {
	NVMEC_SUCCESS			= 0,
	NVMEC_ERR_CHK_SIZE		= 1,
	NVMEC_ERR_GET_PCI_DEV		= 2,
	NVMEC_ERR_PCI_ADDR_FMT		= 3,
	NVMEC_ERR_PCI_ADDR_PARSE	= 4,
	NVMEC_ERR_CTRLR_NOT_FOUND	= 5,
	NVMEC_ERR_NS_NOT_FOUND		= 6,
	NVMEC_ERR_NOT_SUPPORTED		= 7,
	NVMEC_ERR_BAD_LBA		= 8,
	NVMEC_ERR_ALLOC_IO_QPAIR	= 9,
	NVMEC_ERR_NS_ID_UNEXPECTED	= 10,
	NVMEC_ERR_NS_WRITE_FAIL		= 11,
	NVMEC_ERR_MULTIPLE_ACTIVE_NS	= 12,
	NVMEC_ERR_NULL_NS		= 13,
	NVMEC_ERR_ALLOC_SEQUENCE_BUF	= 14,
	NVMEC_LAST_STATUS_VALUE
};

/**
 * \brief NVMe controller details
 */
struct ctrlr_t {
	char		     model[1024];
	char		     serial[1024];
	char		     pci_addr[1024];
	char		     fw_rev[1024];
	int		     socket_id;
	struct ns_t	    *nss;
	struct dev_health_t *dev_health;
	struct ctrlr_t	    *next;
};

/**
 * \brief NVMe namespace details
 */
struct ns_t {
	uint32_t	id;
	uint64_t	size;
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
	char		err[1024];
};

struct ctrlr_entry {
	struct spdk_nvme_ctrlr	*ctrlr;
	struct spdk_pci_addr	 pci_addr;
	struct ns_entry		*nss;
	struct dev_health_entry	*dev_health;
	int			 socket_id;
	char			 name[1024];
	struct ctrlr_entry	*next;
};

struct ns_entry {
	struct spdk_nvme_ctrlr *ctrlr;
	struct spdk_nvme_ns	*ns;
	struct spdk_nvme_qpair	*qpair;
	struct ns_entry		*next;
};

struct dev_health_entry {
	struct spdk_nvme_health_information_page health_page;
	struct spdk_nvme_error_information_entry error_page[256];
	int					 inflight;
};

extern struct ctrlr_entry	*g_controllers;

/**
 * Attach call back function to report a device that has been
 * attached to the userspace NVMe driver.
 *
 * \param cb_ctx Opaque value passed to spdk_nvme_attach_cb()
 * \param trid NVMe transport identifier
 * \param ctrlr opaque handle to NVMe controller
 * \param opts NVMe controller init options that were actually used.
 *
 * \brief NVMe namespace details
 */
void
attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	  struct spdk_nvme_ctrlr *ctrlr,
	  const struct spdk_nvme_ctrlr_opts *opts);

/*
 * Initialize the ret_t struct by allocating memory and setting attributes
 * to NULL.
 *
 * \param rc initial rc value to set in returned ret_t.
 *
 * \return a pointer to a return struct (ret_t).
 **/
struct ret_t *
init_ret(int rc);

/*
 * Free memory allocated in linked lists attached to the ret_t struct.
 *
 * \param ret a pointer to a return struct (ret_t).
 **/
void
clean_ret(struct ret_t *ret);

/**
 * Get the NVMe controller
 *
 * \param centry (out) pointer to assign to pointer of found entry.
 * \param addr string of controller PCI address to find.
 *
 * \return int indicating success or failure.
 **/
int
get_controller(struct ctrlr_entry **centry, char *addr);

/**
 * Provide ability to pass function pointers to _discover for mocking
 * in unit tests.
 */
typedef int
(*prober)(const struct spdk_nvme_transport_id *, void *, spdk_nvme_probe_cb,
	  spdk_nvme_attach_cb, spdk_nvme_remove_cb);

typedef int
(*health_getter)(struct spdk_nvme_ctrlr *, struct dev_health_entry *);

struct ret_t *
_discover(prober, bool, health_getter);

/**
 * Provide ability to pass function pointers to _collect for mocking
 * in unit tests.
 */
typedef int
(*data_copier)(struct ctrlr_t *, struct ctrlr_entry *);

typedef struct spdk_pci_device *
(*pci_getter)(struct spdk_nvme_ctrlr *);

typedef int
(*socket_id_getter)(struct spdk_pci_device *);

void
_collect(struct ret_t *, data_copier, pci_getter, socket_id_getter);

/**
 * Collect controller and namespace information of the NVMe devices.
 *
 * \return a pointer to a return struct (ret_t).
 */
struct ret_t *
collect(void);

/**
 * Cleanup allocated memory for controller list generated by probe/attach.
 *
 * \param detach flag to signify whether nvme controllers should be detached
 *               from SPDK during cleanup.
 */
void
cleanup(bool detach);

#endif
