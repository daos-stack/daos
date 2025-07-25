/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2016-2023, Intel Corporation */

/*
 * tx.h -- internal definitions for transactions
 */

#ifndef __DAOS_COMMON_INTERNAL_TX_H
#define __DAOS_COMMON_INTERNAL_TX_H 1

#include <stdint.h>

#define TX_DEFAULT_RANGE_CACHE_SIZE (1 << 15)

struct ulog_entry_base;
struct mo_ops;
/*
 * tx_create_wal_entry -- convert to WAL a single ulog UNDO entry
 */
int tx_create_wal_entry(struct ulog_entry_base *e, void *arg, const struct mo_ops *p_ops);

int
obj_realloc(dav_obj_t *pop, uint64_t *offp, size_t *sizep, size_t size, uint16_t class_id);

#endif
