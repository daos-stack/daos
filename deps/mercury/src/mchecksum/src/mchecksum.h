/**
 * Copyright (c) 2013-2021 UChicago Argonne, LLC and The HDF Group.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MCHECKSUM_H
#define MCHECKSUM_H

#include "mchecksum_config.h"

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

typedef struct mchecksum_object *mchecksum_object_t;

/*****************/
/* Public Macros */
/*****************/

#define MCHECKSUM_OBJECT_NULL ((mchecksum_object_t) 0)

#define MCHECKSUM_NOFINALIZE (0)
#define MCHECKSUM_FINALIZE   (1)

/*********************/
/* Public Prototypes */
/*********************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the checksum with the specified hash method.
 *
 * \param hash_method [IN]      hash method string
 *                              Available methods are: "crc16", "crc64"
 * \param checksum [OUT]        pointer to abstract checksum
 *
 * \return 0 success or negative value on failure
 */
MCHECKSUM_PUBLIC int
mchecksum_init(const char *hash_method, mchecksum_object_t *checksum_p);

/**
 * Destroy the checksum.
 *
 * \param checksum [IN/OUT]     abstract checksum
 *
 * \return 0 on success or negative value on failure
 */
MCHECKSUM_PUBLIC void
mchecksum_destroy(mchecksum_object_t checksum);

/**
 * Reset the checksum.
 *
 * \param checksum [IN/OUT]     abstract checksum
 *
 * \return 0 on success or negative value on failure
 */
MCHECKSUM_PUBLIC int
mchecksum_reset(mchecksum_object_t checksum);

/**
 * Get size of checksum.
 *
 * \param checksum [IN]         abstract checksum
 *
 * \return Non-negative value
 */
MCHECKSUM_PUBLIC size_t
mchecksum_get_size(mchecksum_object_t checksum);

/**
 * Get checksum and copy it into buf.
 *
 * \param checksum [IN/OUT]     abstract checksum
 * \param buf [IN]              pointer to buffer
 * \param size [IN]             size of buffer
 * \param finalize [IN]         one of:
 *       MCHECKSUM_FINALIZE     no more data will be added to this checksum
 *                              (only valid call to follow is reset or
 *                              destroy)
 *       MCHECKSUM_NOFINALIZE   More data might be added to this checksum
 *                              later
 *
 * \return 0 on success or negative value on failure
 */
MCHECKSUM_PUBLIC int
mchecksum_get(
    mchecksum_object_t checksum, void *buf, size_t size, int finalize);

/**
 * Accumulates a partial checksum of the input data.
 *
 * \param checksum [IN/OUT]     abstract checksum
 * \param buf [IN]              pointer to buffer
 * \param size [IN]             size of buffer
 *
 * \return 0 on success or negative value on failure
 */
MCHECKSUM_PUBLIC int
mchecksum_update(mchecksum_object_t checksum, const void *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* MCHECKSUM_H */
