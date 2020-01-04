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
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#define D_LOGFAC	DD_FAC(bio)

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/falloc.h>
#include <libgen.h>

#include <daos/common.h>
#include <daos/btree_class.h>
#include "smd_internal.h"

static struct sys_db	*smd_db;

int
smd_db_fetch(char *table, void *key, int key_size, void *val, int val_size)
{
	d_iov_t	key_iov;
	d_iov_t	val_iov;

	d_iov_set(&key_iov, key, key_size);
	d_iov_set(&val_iov, val, val_size);

	return smd_db->sd_fetch(smd_db, table, &key_iov, &val_iov);
}

int
smd_db_upsert(char *table, void *key, int key_size, void *val, int val_size)
{
	d_iov_t	key_iov;
	d_iov_t	val_iov;

	d_iov_set(&key_iov, key, key_size);
	d_iov_set(&val_iov, val, val_size);

	return smd_db->sd_upsert(smd_db, table, &key_iov, &val_iov);
}

int
smd_db_delete(char *table, void *key, int key_size)
{
	d_iov_t	key_iov;

	d_iov_set(&key_iov, key, key_size);
	return smd_db->sd_delete(smd_db, table, &key_iov);
}

int
smd_db_traverse(char *table, sys_db_trav_cb_t cb, struct smd_trav_data *td)
{
	return smd_db->sd_traverse(smd_db, table, cb, td);
}

int
smd_db_tx_begin(void)
{
	if (smd_db->sd_tx_begin)
		return smd_db->sd_tx_begin(smd_db);
	else
		return 0;
}

int
smd_db_tx_end(int rc)
{
	if (smd_db->sd_tx_end)
		return smd_db->sd_tx_end(smd_db, rc);
	else
		return rc;
}

void
smd_db_lock(void)
{
	if (smd_db->sd_lock)
		smd_db->sd_lock(smd_db);
}

void
smd_db_unlock(void)
{
	if (smd_db->sd_unlock)
		smd_db->sd_unlock(smd_db);
}

void
smd_fini(void)
{
	smd_db = NULL;
}

int
smd_init(struct sys_db *db)
{
	D_ASSERT(db->sd_fetch);
	D_ASSERT(db->sd_upsert);
	D_ASSERT(db->sd_delete);
	D_ASSERT(db->sd_traverse);

	smd_db = db;
	return 0;
}
