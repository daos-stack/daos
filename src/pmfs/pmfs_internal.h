/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef PMFS_INTERNAL_H
#define PMFS_INTERNAL_H

int
vos_client_obj_update_sync(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
			   uint32_t pm_ver, uint64_t flags, daos_key_t *dkey,
			   unsigned int iod_nr, daos_iod_t *iods, d_sg_list_t *sgls,
			   struct spdk_ring *task_ring);

int
vos_client_obj_fetch_sync(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
			  uint64_t flags, daos_key_t *dkey,
			  unsigned int iod_nr, daos_iod_t *iods, d_sg_list_t *sgls,
			  struct spdk_ring *task_ring);

int
vos_client_obj_punch_sync(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
			  uint32_t pm_ver, uint64_t flags, daos_key_t *dkey,
			  unsigned int akey_nr, daos_key_t *akeys,
			  struct spdk_ring *task_ring);

int
vos_client_obj_get_num_dkeys_sync(daos_handle_t coh, daos_obj_id_t oid, uint32_t *nr, size_t *len,
				  struct spdk_ring *task_ring);

int
vos_client_obj_list_dkeys_sync(daos_handle_t coh, daos_obj_id_t oid, uint32_t *nr, size_t *len,
			       daos_key_desc_t *kds, void *buf, struct spdk_ring *task_ring);
#endif
