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
#include <gurt/types.h>

/**
 * Common file names used by each layer to store persistent data
 */
#define	VOS_FILE	"vos-" /* suffixed by thread id */
#define	DSM_META_FILE	"meta"
#define RDB_FILE	"rdb-"

int
ds_mgmt_tgt_file(const uuid_t pool_uuid, const char *fname, int *idx,
		 char **fpath);
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

/** Flags in the system self-heal policy */
#define DS_MGMT_SELF_HEAL_EXCLUDE      (1ULL << 0) /**< self_heal.exclude */
#define DS_MGMT_SELF_HEAL_POOL_EXCLUDE (1ULL << 1) /**< self_heal.pool_exclude */
#define DS_MGMT_SELF_HEAL_POOL_REBUILD (1ULL << 2) /**< self_heal.pool_rebuild */
#define DS_MGMT_SELF_HEAL_ALL          ((unsigned long long)-1)

int
ds_mgmt_get_self_heal_policy(bool (*abort)(void *arg), void *abort_arg, uint64_t *policy);

#endif /* __MGMT_SRV_H__ */
