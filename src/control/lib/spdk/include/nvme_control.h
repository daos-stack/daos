/**
* (C) Copyright 2018-2021 Intel Corporation.
*
* SPDX-License-Identifier: BSD-2-Clause-Patent
*/

#ifndef NVMECONTROL_H
#define NVMECONTROL_H

/**
 * Discover NVMe controllers and namespaces, as well as return device health
 * information.
 *
 * \return a pointer to a return struct (ret_t).
 */
struct ret_t *
nvme_discover(void);

/**
 * Wipe NVMe controller namespace LBA-0.
 *
 * Removes any data container structures e.g. blobstore.
 *
 * \param ctrlr_pci_addr PCI address of NVMe controller.
 *
 * \return a pointer to a return struct (ret_t).
 */
struct ret_t *
nvme_wipe_namespaces(void);

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
 * Initialize SPDK environment.
 *
 * \param mem_sz size of memory allocated to environment (mb).
 * \param env_ctx environment context string (DPDK).
 * \param nr_pcil size of pcil.
 * \param pcil list of allowed PCI addresses of NVMe controllers.
 *
 * \return a pointer to a return struct (ret_t).
 */
struct ret_t *
daos_spdk_init(int mem_sz, char *env_ctx, size_t nr_pcil, char **pcil);

#endif
