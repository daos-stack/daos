/**
 * (C) Copyright 2016-2022 Intel Corporation.
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
ds_mgmt_tgt_pool_destroy(uuid_t pool_uuid, d_rank_list_t *ranks);

#endif /* __MGMT_SRV_H__ */
