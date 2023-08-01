/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos. It implements some miscellaneous policy functions
 */
#ifndef __SYS_DB_H__
#define __SYS_DB_H__

#include <daos_types.h>

struct sys_db;
typedef int (*sys_db_trav_cb_t)(struct sys_db *db, char *table, d_iov_t *key,
				void *args);

#define SYS_DB_NAME_SZ		32

/** system database is a simple local KV store */
struct sys_db {
	char	 sd_name[SYS_DB_NAME_SZ];
	/** look up the provided key in \a table and return its value */
	int	(*sd_fetch)(struct sys_db *db, char *table,
			    d_iov_t *key, d_iov_t *val);
	/** update or insert a KV pair to \a table */
	int	(*sd_upsert)(struct sys_db *db, char *table,
			     d_iov_t *key, d_iov_t *val);
	/** reserved */
	int	(*sd_insert)(struct sys_db *db, char *table,
			     d_iov_t *key, d_iov_t *val);
	/** reserved */
	int	(*sd_update)(struct sys_db *db, char *table,
			     d_iov_t *key, d_iov_t *val);
	/** delete provided key and its value from the \a table */
	int	(*sd_delete)(struct sys_db *db, char *table, d_iov_t *key);
	/** traverse all keys in the \a table */
	int	(*sd_traverse)(struct sys_db *db, char *table,
			       sys_db_trav_cb_t cb, void *args);
	int	(*sd_tx_begin)(struct sys_db *db);
	int	(*sd_tx_end)(struct sys_db *db, int rc);
	void	(*sd_lock)(struct sys_db *db);
	void	(*sd_unlock)(struct sys_db *db);
};

#endif /* __SYS_DB_H__ */
