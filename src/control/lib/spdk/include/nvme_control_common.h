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

#ifndef NVMECONTROL_STATIC_H
#define NVMECONTROL_STATIC_H

struct ctrlr_entry {
	struct spdk_nvme_ctrlr	*ctrlr;
	struct spdk_pci_addr	pci_addr;
	int 			socket_id;
	struct ctrlr_entry	*next;
};

struct ns_entry {
	struct spdk_nvme_ctrlr	*ctrlr;
	struct spdk_nvme_ns	*ns;
	struct ns_entry		*next;
	struct spdk_nvme_qpair	*qpair;
};

extern struct ctrlr_entry	*g_controllers;
extern struct ns_entry		*g_namespaces;

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
 * Set pci addresss of NVMe controller.
 *
 * \return int
 **/
int
set_pci_addr(
	struct spdk_nvme_ctrlr *ctrlr, char *ctrlr_pci_addr, size_t size,
	struct ret_t *ret);

/**
 * Get the NVMe controller
 *
 * \return int
 **/
int
get_controller(char *addr, struct ctrlr_entry *ctrlr_entry, struct ret_t *ret);

/**
 * Collect controller and namespace information of the NVMe devices.
 */
void
collect(struct ret_t *ret);

/**
 * Cleanup structs held in memory.
 */
void
cleanup(void);

#endif
