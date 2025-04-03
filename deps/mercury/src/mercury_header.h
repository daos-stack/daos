/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MERCURY_HEADER_H
#define MERCURY_HEADER_H

#include "mercury_core_types.h"

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

#ifdef HG_HAS_CHECKSUMS
HG_PACKED(struct hg_header_hash {
    uint32_t payload; /* Payload checksum (32-bits checksum) */
});

HG_PACKED(struct hg_header_input {
    struct hg_header_hash hash; /* Hash */
    /* 160 bits here */
});

HG_PACKED(struct hg_header_output {
    struct hg_header_hash hash; /* Hash */
    /* 160 bits here */
});
#else
HG_PACKED(struct hg_header_input {
    uint32_t pad;
    /* 128 bits here */
});

HG_PACKED(struct hg_header_output {
    uint32_t pad;
    /* 128 bits here */
});
#endif

/* Common header struct input/output */
struct hg_header {
    union {
        struct hg_header_input input;
        struct hg_header_output output;
    } msg;      /* Header message */
    hg_op_t op; /* Header operation type */
};

/*****************/
/* Public Macros */
/*****************/

/*********************/
/* Public Prototypes */
/*********************/

#ifdef __cplusplus
extern "C" {
#endif

static HG_INLINE size_t
hg_header_get_size(hg_op_t op);

/**
 * Get size reserved for header (separate user data stored in payload).
 *
 * \return Non-negative size value
 */
static HG_INLINE size_t
hg_header_get_size(hg_op_t op)
{
    hg_size_t ret = 0;

    switch (op) {
        case HG_INPUT:
            ret = sizeof(struct hg_header_input);
            break;
        case HG_OUTPUT:
            ret = sizeof(struct hg_header_output);
            break;
        default:
            break;
    }

    return ret;
}

/**
 * Initialize RPC header.
 *
 * \param hg_header [IN/OUT]    pointer to header structure
 * \param op [IN]               HG operation type: HG_INPUT / HG_OUTPUT
 */
HG_PRIVATE void
hg_header_init(struct hg_header *hg_header, hg_op_t op);

/**
 * Finalize RPC header.
 *
 * \param hg_header [IN/OUT]    pointer to header structure
 */
HG_PRIVATE void
hg_header_finalize(struct hg_header *hg_header);

/**
 * Reset RPC header.
 *
 * \param hg_header [IN/OUT]    pointer to header structure
 * \param op [IN]               HG operation type: HG_INPUT / HG_OUTPUT
 */
HG_PRIVATE void
hg_header_reset(struct hg_header *hg_header, hg_op_t op);

/**
 * Process private information for sending/receiving RPC.
 *
 * \param op [IN]               operation type: HG_ENCODE / HG_DECODE
 * \param buf [IN/OUT]          buffer
 * \param buf_size [IN]         buffer size
 * \param hg_header [IN/OUT]    pointer to header structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_PRIVATE hg_return_t
hg_header_proc(
    hg_proc_op_t op, void *buf, size_t buf_size, struct hg_header *hg_header);

#ifdef __cplusplus
}
#endif

#endif /* MERCURY_HEADER_H */
