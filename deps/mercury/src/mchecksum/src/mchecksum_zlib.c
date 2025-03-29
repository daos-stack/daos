/**
 * Copyright (c) 2013-2021 UChicago Argonne, LLC and The HDF Group.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mchecksum_plugin.h"

#include "mchecksum_error.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/****************/
/* Local Macros */
/****************/

/************************************/
/* Local Type and Struct Definition */
/************************************/

/********************/
/* Local Prototypes */
/********************/

static int
mchecksum_crc32_init(void **data_p);
static void
mchecksum_crc32_destroy(void *data);
static void
mchecksum_crc32_reset(void *data);
static size_t
mchecksum_crc32_get_size(void *data);
static int
mchecksum_crc32_get(void *data, void *buf, size_t size, int finalize);
static void
mchecksum_crc32_update(void *data, const void *buf, size_t size);

static int
mchecksum_adler32_init(void **data_p);
static void
mchecksum_adler32_destroy(void *data);
static void
mchecksum_adler32_reset(void *data);
static size_t
mchecksum_adler32_get_size(void *data);
static int
mchecksum_adler32_get(void *data, void *buf, size_t size, int finalize);
static void
mchecksum_adler32_update(void *data, const void *buf, size_t size);

/*******************/
/* Local Variables */
/*******************/

const struct mchecksum_ops MCHECKSUM_PLUGIN_OPS(crc32) = {
    "crc32",                  /* name */
    mchecksum_crc32_init,     /* init */
    mchecksum_crc32_destroy,  /* destroy */
    mchecksum_crc32_reset,    /* reset */
    mchecksum_crc32_get_size, /* get_size */
    mchecksum_crc32_get,      /* get */
    mchecksum_crc32_update    /* update */
};

const struct mchecksum_ops MCHECKSUM_PLUGIN_OPS(adler32) = {
    "adler32",                  /* name */
    mchecksum_adler32_init,     /* init */
    mchecksum_adler32_destroy,  /* destroy */
    mchecksum_adler32_reset,    /* reset */
    mchecksum_adler32_get_size, /* get_size */
    mchecksum_adler32_get,      /* get */
    mchecksum_adler32_update    /* update */
};

/*---------------------------------------------------------------------------*/
static int
mchecksum_crc32_init(void **data_p)
{
    void *data;
    int rc;

    data = malloc(sizeof(uint32_t));
    MCHECKSUM_CHECK_ERROR(
        data == NULL, error, rc, -1, "Could not allocate private data");
    mchecksum_crc32_reset(data);

    *data_p = data;

    return 0;

error:
    return rc;
}

/*---------------------------------------------------------------------------*/
static void
mchecksum_crc32_destroy(void *data)
{
    free(data);
}

/*---------------------------------------------------------------------------*/
static void
mchecksum_crc32_reset(void *data)
{
    *(uint32_t *) data = (uint32_t) crc32(0L, Z_NULL, 0);
}

/*---------------------------------------------------------------------------*/
static size_t
mchecksum_crc32_get_size(void MCHECKSUM_UNUSED *data)
{
    return sizeof(uint32_t);
}

/*---------------------------------------------------------------------------*/
static int
mchecksum_crc32_get(
    void *data, void *buf, size_t size, int MCHECKSUM_UNUSED finalize)
{
    int rc;

    MCHECKSUM_CHECK_ERROR(size < sizeof(uint32_t), error, rc, -1,
        "Buffer is too small to store checksum");

    memcpy(buf, data, sizeof(uint32_t));

    return 0;

error:
    return rc;
}

/*---------------------------------------------------------------------------*/
static void
mchecksum_crc32_update(void *data, const void *buf, size_t size)
{
    uint32_t *state = (uint32_t *) data;

    *state = (uint32_t) crc32(*state, buf, (unsigned int) size);
}

/*---------------------------------------------------------------------------*/
static int
mchecksum_adler32_init(void **data_p)
{
    void *data;
    int rc;

    data = malloc(sizeof(uint32_t));
    MCHECKSUM_CHECK_ERROR(
        data == NULL, error, rc, -1, "Could not allocate private data");
    mchecksum_adler32_reset(data);

    *data_p = data;

    return 0;

error:
    return rc;
}

/*---------------------------------------------------------------------------*/
static void
mchecksum_adler32_destroy(void *data)
{
    free(data);
}

/*---------------------------------------------------------------------------*/
static void
mchecksum_adler32_reset(void *data)
{
    *(uint32_t *) data = (uint32_t) adler32(0L, Z_NULL, 0);
}

/*---------------------------------------------------------------------------*/
static size_t
mchecksum_adler32_get_size(void MCHECKSUM_UNUSED *data)
{
    return sizeof(uint32_t);
}

/*---------------------------------------------------------------------------*/
static int
mchecksum_adler32_get(
    void *data, void *buf, size_t size, int MCHECKSUM_UNUSED finalize)
{
    int rc;

    MCHECKSUM_CHECK_ERROR(size < sizeof(uint32_t), error, rc, -1,
        "Buffer is too small to store checksum");

    memcpy(buf, data, sizeof(uint32_t));

    return 0;

error:
    return rc;
}

/*---------------------------------------------------------------------------*/
static void
mchecksum_adler32_update(void *data, const void *buf, size_t size)
{
    uint32_t *state = (uint32_t *) data;

    *state = (uint32_t) adler32(*state, buf, (unsigned int) size);
}
