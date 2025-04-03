/**
 * Copyright (c) 2013-2021 UChicago Argonne, LLC and The HDF Group.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MCHECKSUM_PLUGIN_H
#define MCHECKSUM_PLUGIN_H

#include "mchecksum.h"

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

/* Checksum object definition */
struct mchecksum_object {
    const struct mchecksum_ops *ops; /* Operations */
    void *data;                      /* Plugin data */
};

/* Callbacks */
struct mchecksum_ops {
    const char *name;
    int (*init)(void **data_p);
    void (*destroy)(void *data);
    void (*reset)(void *data);
    size_t (*get_size)(void *data);
    int (*get)(void *data, void *buf, size_t size, int finalize);
    void (*update)(void *data, const void *buf, size_t size);
};

/*****************/
/* Public Macros */
/*****************/

/* Remove warnings when plugin does not use callback arguments */
#if defined(__cplusplus)
#    define MCHECKSUM_UNUSED
#elif defined(__GNUC__) && (__GNUC__ >= 4)
#    define MCHECKSUM_UNUSED __attribute__((unused))
#else
#    define MCHECKSUM_UNUSED
#endif

/**
 * Plugin ops definition
 */
#define MCHECKSUM_PLUGIN_OPS(plugin_name) mchecksum_##plugin_name##_ops_g

/*********************/
/* Public Prototypes */
/*********************/

/*********************/
/* Public Variables */
/*********************/

extern MCHECKSUM_PRIVATE const struct mchecksum_ops MCHECKSUM_PLUGIN_OPS(crc16);
/* Keep non const for sse42 detection */
extern MCHECKSUM_PRIVATE struct mchecksum_ops MCHECKSUM_PLUGIN_OPS(crc32c);
extern MCHECKSUM_PRIVATE const struct mchecksum_ops MCHECKSUM_PLUGIN_OPS(crc64);

#ifdef MCHECKSUM_HAS_ZLIB
extern MCHECKSUM_PRIVATE const struct mchecksum_ops MCHECKSUM_PLUGIN_OPS(crc32);
extern MCHECKSUM_PRIVATE const struct mchecksum_ops MCHECKSUM_PLUGIN_OPS(
    adler32);
#endif

#endif /* MCHECKSUM_PLUGIN_H */
