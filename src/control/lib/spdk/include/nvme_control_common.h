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

#ifndef NVMECONTROL_COMMON_H
#define NVMECONTROL_COMMON_H

#include <stdbool.h>

#define NVMECONTROL_GBYTE_BYTES 1000000000

/**
 * @brief NVMECONTROL return codes
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
	NVMEC_LAST_STATUS_VALUE
};

/**
 * Register the namespace by obtaining the NVMe controller data,
 * verifying the namespace is active and allocating memory for
 * the namespace.
 *
 * \param pointer to spdk_nvme_ctrlr struct
 * \param pointer to spdk_nvme_ns struct
 *
 */
void
register_ns(struct spdk_nvme_ctrlr *ctrlr, struct spdk_nvme_ns *ns);

/**
 * Probe call back function.
 *
 * \param cb_ctx
 * \param trid pointer to spdk_nvme_transport_id struct
 * \param opts pointer to spdk_nvme_ctrlr_opts struct
 *
 * \returns a bool: True
 *
 */
bool
probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	 struct spdk_nvme_ctrlr_opts *opts);

/**
 * Attach call back function to report a device that has been
 * attached to the userspace NVMe driver.
 *
 * \param cb_ctx Opaque value passed to spdk_nvme_attach_cb()
 * \param trid NVMe transport identifier
 * \param ctrlr opaque handle to NVMe controller
 * \param opts NVMe controller init options that were actually used.
 *
 */
void
attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	  struct spdk_nvme_ctrlr *ctrlr,
	  const struct spdk_nvme_ctrlr_opts *opts);

/**
 * Initialize the ret_t struct by allocating memory and setting attributes
 * to NULL
 *
 * \return struct ret_t
 **/
struct ret_t *
init_ret(void);

/**
 * Check size of the controller attributes
 *
 * \return int
 **/
int
check_size(int written, int max, char *msg, struct ret_t *ret);

/**
 * Get the NVMe controller
 *
 * \return ctrlr_entry *
 **/
struct ctrlr_entry *
get_controller(char *addr, struct ret_t *ret);

/**
 * Provide ability to pass function pointers to collect for mocking
 * in unit tests.
 */
typedef const struct spdk_nvme_ctrlr_data *
(*data_getter)(struct spdk_nvme_ctrlr *);

typedef struct spdk_pci_device *
(*pci_getter)(struct spdk_nvme_ctrlr *);

typedef int
(*socket_id_getter)(struct spdk_pci_device *);

void
_collect(struct ret_t *ret, data_getter, pci_getter, socket_id_getter);

/**
 * Collect controller and namespace information of the NVMe devices.
 */
void
collect(struct ret_t *ret);

/**
 * Collect health statistics for the NVMe device.
 *
 * \param entry to read health stats from
 * \param ctrlr to populate health stats to
 * \return int
 **/
int
collect_health_stats(struct dev_health_entry *entry, struct ctrlr_t *ctrlr);

#endif
