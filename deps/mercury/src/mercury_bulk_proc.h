/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MERCURY_BULK_PROC_H
#define MERCURY_BULK_PROC_H

#include "mercury_bulk.h"

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

/*****************/
/* Public Macros */
/*****************/

/* Additional internal bulk flags (can hold up to 8 bits) */
#define HG_BULK_EAGER (1 << 2) /* embeds data along descriptor */
#define HG_BULK_SM    (1 << 3) /* bulk transfer through shared-memory */

/*********************/
/* Public Prototypes */
/*********************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get pointer to cached serialized buffer if any was priorly set.
 */
HG_PRIVATE void *
hg_bulk_get_serialize_cached_ptr(hg_bulk_t handle);

/**
 * Get pointer to cached serialized buffer size if any was priorly set.
 */
HG_PRIVATE hg_size_t
hg_bulk_get_serialize_cached_size(hg_bulk_t handle);

/**
 * Set cached pointer to serialization buffer.
 */
HG_PRIVATE void
hg_bulk_set_serialize_cached_ptr(hg_bulk_t handle, void *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* MERCURY_BULK_PROC_H */
