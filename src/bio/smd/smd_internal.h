/**
 * (C) Copyright 2018-2020 Intel Corporation.
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
#include <daos/mem.h>
#include <daos_srv/smd.h>

#define TABLE_DEV	"device"
#define TABLE_TGT	"target"
#define TABLE_POOL	"pool"

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

#endif /** __SMD_INTERNAL_H__ */
