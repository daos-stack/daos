/**
 * (C) Copyright 2016-2022 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Server-side management API offering the following functionalities:
 * - manage storage allocation (PMEM files, disk partitions, ...)
 * - initialize pool and target service
 * - provide fault domains
 */

#ifndef __MGMT_SRV_H__
#define __MGMT_SRV_H__

#include <uuid/uuid.h>

#include <daos_srv/mgmt_tgt_common.h>

int
ds_mgmt_tgt_pool_iterate(int (*cb)(uuid_t uuid, void *arg), void *arg);
int
ds_mgmt_newborn_pool_iterate(int (*cb)(uuid_t uuid, void *arg), void *arg);
int
ds_mgmt_zombie_pool_iterate(int (*cb)(uuid_t uuid, void *arg), void *arg);
int
ds_mgmt_pool_exist(uuid_t uuid);
int
ds_mgmt_tgt_pool_exist(uuid_t uuid, char **path);
int
ds_mgmt_tgt_pool_destroy_ranks(uuid_t pool_uuid, d_rank_list_t *ranks);
int
ds_mgmt_tgt_pool_shard_destroy(uuid_t pool_uuid, int shard_idx, d_rank_t rank);
bool
ds_mgmt_pbl_has_pool(uuid_t uuid);

#endif /* __MGMT_SRV_H__ */
