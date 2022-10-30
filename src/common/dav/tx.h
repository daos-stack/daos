/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2016-2020, Intel Corporation */

/*
 * tx.h -- internal definitions for transactions
 */

#ifndef DAV_INTERNAL_TX_H
#define DAV_INTERNAL_TX_H 1

#include <stdint.h>
#include "obj.h"
#include "ulog.h"

#define TX_DEFAULT_RANGE_CACHE_SIZE (1 << 15)

typedef struct dav_obj dav_obj_t;

/*
 * Returns the current transaction's pool handle, NULL if not within
 * a transaction.
 */
dav_obj_t *tx_get_pop(void);

#endif
