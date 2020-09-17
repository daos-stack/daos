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

#define BUFLEN 1024

/**
 * \brief NVMECONTROL return codes
 */
enum nvme_control_status_code {
	NVMEC_SUCCESS			= 0x0,
	NVMEC_ERR_CHK_SIZE		= 0x1,
	NVMEC_ERR_GET_PCI_DEV		= 0x2,
	NVMEC_ERR_PCI_ADDR_FMT		= 0x3,
	NVMEC_ERR_PCI_ADDR_PARSE	= 0x4,
	NVMEC_ERR_CTRLR_NOT_FOUND	= 0x5,
	NVMEC_ERR_NS_NOT_FOUND		= 0x6,
	NVMEC_ERR_NOT_SUPPORTED		= 0x7,
	NVMEC_ERR_BAD_LBA		= 0x8,
	NVMEC_ERR_ALLOC_IO_QPAIR	= 0x9,
	NVMEC_ERR_NS_ID_UNEXPECTED	= 0xA,
	NVMEC_ERR_NS_WRITE_FAIL		= 0xB,
	NVMEC_ERR_MULTIPLE_ACTIVE_NS	= 0xC,
	NVMEC_ERR_NULL_NS		= 0xD,
	NVMEC_ERR_ALLOC_SEQUENCE_BUF	= 0xE,
	NVMEC_ERR_NO_VMD_CTRLRS		= 0xF,
	NVMEC_ERR_WRITE_TRUNC		= 0x10,
	NVMEC_LAST_STATUS_VALUE
};

/**
 * \brief NVMe controller details
 */
struct ctrlr_t {
	char				 model[BUFLEN];
	char				 serial[BUFLEN];
	char				 pci_addr[BUFLEN];
	char				 fw_rev[BUFLEN];
	char				 pci_type[BUFLEN];
	int				 socket_id;
	struct ns_t			*nss;
	struct nvme_health_stats	*stats;
	struct ctrlr_t			*next;
};

/**
 * \brief NVMe namespace details
 */
struct ns_t {
	uint32_t	id;
	uint64_t	size;
	struct ns_t    *next;
};

/**
 * \brief Result struct for namespace wipe operation containing return code,
 * namespace id, parent controller pci address, info message and link to next
 * list element.
 */
struct wipe_res_t {
	char			 ctrlr_pci_addr[BUFLEN];
	uint32_t		 ns_id;
	int			 rc;
	char			 info[BUFLEN];
	struct wipe_res_t	*next;
};

/**
 * \brief Return containing return code, controllers, namespaces, wwipe
 * results and info message
 */
struct ret_t {
	struct ctrlr_t		*ctrlrs;
	struct wipe_res_t	*wipe_results;
	int			 rc;
	char			 info[BUFLEN];
};

struct ctrlr_entry {
	struct spdk_nvme_ctrlr	*ctrlr;
	struct spdk_pci_addr	 pci_addr;
	struct ns_entry		*nss;
	struct health_entry	*health;
	int			 socket_id;
	struct ctrlr_entry	*next;
};

struct ns_entry {
	struct spdk_nvme_ns	*ns;
	struct spdk_nvme_qpair	*qpair;
	struct ns_entry		*next;
};

struct health_entry {
	struct spdk_nvme_health_information_page page;
	struct spdk_nvme_error_information_entry error_page[256];
	int					 inflight;
};

extern struct ctrlr_entry	*g_controllers;

bool
probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	 struct spdk_nvme_ctrlr_opts *opts);

void
register_ns(struct ctrlr_entry *centry, struct spdk_nvme_ns *ns);

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

/**
 * Initialize the wipe_res_t struct by allocating memory and setting references
 * to NULL.
 *
 * \return a pointer to a wipe result struct (wipe_res_t).
 **/
struct wipe_res_t *
init_wipe_res(void);

/**
 * Initialize the ret_t struct by allocating memory and setting references
 * to NULL.
 *
 * \return a pointer to a return struct (ret_t).
 **/
struct ret_t *
init_ret(void);

/**
 * Free memory allocated in linked lists attached to the ret_t struct.
 *
 * \param ret A pointer to a return struct (ret_t) which itself needs to be
 *            freed explicitly after calling clean_ret.
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
(*health_getter)(struct spdk_nvme_ctrlr *, struct health_entry *);

struct ret_t *
_discover(prober, bool, health_getter);

/**
 * Provide ability to pass function pointers to _collect for mocking
 * in unit tests.
 */
typedef int
(*data_copier)(struct ctrlr_t *, const struct spdk_nvme_ctrlr_data *);

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
