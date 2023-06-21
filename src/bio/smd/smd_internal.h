/**
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * smd : Internal Declarations
 *
 * This file contains all declarations that are only used by
 * nvme persistent metadata.
 */

#ifndef __SMD_INTERNAL_H__
#define __SMD_INTERNAL_H__

#include <abt.h>
#include <daos_types.h>
#include <daos/btree.h>
#include <daos/sys_db.h>
#include <daos/mem.h>
#include <daos_srv/smd.h>

#define TABLE_DEV		"device"
#define SMD_DEV_NAME_MAX	16

extern char TABLE_TGTS[SMD_DEV_TYPE_MAX][SMD_DEV_NAME_MAX];

extern char TABLE_POOLS[SMD_DEV_TYPE_MAX][SMD_DEV_NAME_MAX];

#define SMD_MAX_TGT_CNT		64

/** callback parameter for smd_db_traverse */
struct smd_trav_data {
	d_list_t		td_list;
	int			td_count;
};

int smd_db_fetch(char *table, void *key, int key_sz, void *val, int val_sz);
int smd_db_upsert(char *table, void *key, int key_sz, void *val, int val_sz);
int smd_db_delete(char *table, void *key, int key_sz);
int smd_db_traverse(char *table, sys_db_trav_cb_t cb, struct smd_trav_data *td);
int smd_db_tx_begin(void);
int smd_db_tx_end(int rc);
void smd_db_lock(void);
void smd_db_unlock(void);
bool smd_db_ready(void);

/* smd_pool.c */
int
smd_pool_replace_blobs_locked(struct smd_pool_info *info, int tgt_cnt,
			      uint32_t *tgts);

#endif /** __SMD_INTERNAL_H__ */
